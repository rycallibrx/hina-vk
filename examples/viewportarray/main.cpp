/**
 * HinaVK Viewport Array Example - Geometry Shader Test
 *
 * Based on https://github.com/SaschaWillems/Vulkan/blob/master/examples/viewportarray
 *
 * This example demonstrates:
 * - Geometry shader instancing (invocations = 2)
 * - Multi-viewport rendering in a single draw call
 * - gl_ViewportIndex for viewport selection
 * - Split-screen effect (could be used for VR stereo rendering)
 *
 * The geometry shader duplicates each triangle to two viewports,
 * applying different view matrices to create a stereo-like effect.
 */

#include <cmath>
#include <vector>

#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>

#include "../hina_example.h"

// ============================================================================
// Configuration
// ============================================================================

constexpr int NUM_VIEWPORTS = 2;

// ============================================================================
// Vertex Data Structure
// ============================================================================

struct Vertex {
    glm::vec3 position;
    glm::vec3 normal;
    glm::vec3 color;
};

// UBO matching shader layout (projection/modelview for each invocation)
struct SceneUBO {
    glm::mat4 projection[NUM_VIEWPORTS];
    glm::mat4 modelview[NUM_VIEWPORTS];
    glm::vec4 lightPos;
    glm::vec4 screenOffsets[NUM_VIEWPORTS];  // X offset in clip space
};

// ============================================================================
// Cube Mesh Generation (with vertex colors)
// ============================================================================

struct Mesh {
    std::vector<Vertex> vertices;
    std::vector<uint32_t> indices;
};

static Mesh generate_colored_cube() {
    Mesh mesh;

    // Face colors
    glm::vec3 colors[6] = {
        {1.0f, 0.0f, 0.0f},  // Front - Red
        {0.0f, 1.0f, 0.0f},  // Back - Green
        {0.0f, 0.0f, 1.0f},  // Right - Blue
        {1.0f, 1.0f, 0.0f},  // Left - Yellow
        {1.0f, 0.0f, 1.0f},  // Top - Magenta
        {0.0f, 1.0f, 1.0f},  // Bottom - Cyan
    };

    // Define the 8 corners of the cube
    glm::vec3 corners[8] = {
        {-0.5f, -0.5f, -0.5f}, // 0: left-bottom-back
        { 0.5f, -0.5f, -0.5f}, // 1: right-bottom-back
        { 0.5f,  0.5f, -0.5f}, // 2: right-top-back
        {-0.5f,  0.5f, -0.5f}, // 3: left-top-back
        {-0.5f, -0.5f,  0.5f}, // 4: left-bottom-front
        { 0.5f, -0.5f,  0.5f}, // 5: right-bottom-front
        { 0.5f,  0.5f,  0.5f}, // 6: right-top-front
        {-0.5f,  0.5f,  0.5f}, // 7: left-top-front
    };

    // Face definitions: normal, 4 corner indices
    struct Face {
        glm::vec3 normal;
        int v0, v1, v2, v3;
    };

    Face faces[6] = {
        // Front face (+Z)
        {{0.0f, 0.0f, 1.0f}, 4, 5, 6, 7},
        // Back face (-Z)
        {{0.0f, 0.0f, -1.0f}, 1, 0, 3, 2},
        // Right face (+X)
        {{1.0f, 0.0f, 0.0f}, 5, 1, 2, 6},
        // Left face (-X)
        {{-1.0f, 0.0f, 0.0f}, 0, 4, 7, 3},
        // Top face (+Y)
        {{0.0f, 1.0f, 0.0f}, 7, 6, 2, 3},
        // Bottom face (-Y)
        {{0.0f, -1.0f, 0.0f}, 0, 1, 5, 4},
    };

    for (int f = 0; f < 6; f++) {
        const auto& face = faces[f];
        uint32_t base = static_cast<uint32_t>(mesh.vertices.size());

        // Add 4 vertices per face
        mesh.vertices.push_back({corners[face.v0], face.normal, colors[f]});
        mesh.vertices.push_back({corners[face.v1], face.normal, colors[f]});
        mesh.vertices.push_back({corners[face.v2], face.normal, colors[f]});
        mesh.vertices.push_back({corners[face.v3], face.normal, colors[f]});

        // Two triangles per face
        mesh.indices.push_back(base + 0);
        mesh.indices.push_back(base + 1);
        mesh.indices.push_back(base + 2);
        mesh.indices.push_back(base + 0);
        mesh.indices.push_back(base + 2);
        mesh.indices.push_back(base + 3);
    }

    return mesh;
}

// ============================================================================
// Application State
// ============================================================================

struct ViewportArrayApp {
    // Mesh data
    Mesh mesh;
    hina_buffer vbo;
    hina_buffer ibo;

    // Uniform buffer
    hina_buffer ubo_buffer;
    SceneUBO* ubo_mapped;

    // Bind groups
    hina_bind_group_layout scene_layout;
    hina_bind_group scene_bind_group;

    // Pipeline
    hina_pipeline pipeline;

    // Depth buffer
    hina_depth_buffer depth;

    // Camera
    hina_camera camera;

    // Animation
    float rotation;
};

static ViewportArrayApp g_app = {};

// ============================================================================
// Initialization
// ============================================================================

static bool example_init(hina_example_app* app) {
    EXAMPLE_LOGI("Initializing Viewport Array (Geometry Shader) example...");

    // Generate Mesh
    g_app.mesh = generate_colored_cube();
    EXAMPLE_LOGI("Generated cube: %zu vertices, %zu indices",
              g_app.mesh.vertices.size(), g_app.mesh.indices.size());

    // Create Vertex Buffer
    hina_buffer_desc vbo_desc = {0};
    vbo_desc.size = g_app.mesh.vertices.size() * sizeof(Vertex);
    vbo_desc.flags = static_cast<hina_buffer_flags>(HINA_BUFFER_VERTEX_BIT | HINA_BUFFER_HOST_VISIBLE_BIT | HINA_BUFFER_HOST_COHERENT_BIT);
    vbo_desc.initial_data = g_app.mesh.vertices.data();

    g_app.vbo = hina_make_buffer(&vbo_desc);
    if (!hina_buffer_is_valid(g_app.vbo)) {
        EXAMPLE_LOGE("Failed to create vertex buffer");
        return false;
    }

    // Create Index Buffer
    hina_buffer_desc ibo_desc = {0};
    ibo_desc.size = g_app.mesh.indices.size() * sizeof(uint32_t);
    ibo_desc.flags = static_cast<hina_buffer_flags>(HINA_BUFFER_INDEX_BIT | HINA_BUFFER_HOST_VISIBLE_BIT | HINA_BUFFER_HOST_COHERENT_BIT);
    ibo_desc.initial_data = g_app.mesh.indices.data();

    g_app.ibo = hina_make_buffer(&ibo_desc);
    if (!hina_buffer_is_valid(g_app.ibo)) {
        EXAMPLE_LOGE("Failed to create index buffer");
        return false;
    }

    // Create Uniform Buffer
    hina_buffer_desc ubo_desc = {0};
    ubo_desc.size = sizeof(SceneUBO);
    ubo_desc.flags = static_cast<hina_buffer_flags>(HINA_BUFFER_UNIFORM_BIT | HINA_BUFFER_HOST_VISIBLE_BIT | HINA_BUFFER_HOST_COHERENT_BIT);

    g_app.ubo_buffer = hina_make_buffer(&ubo_desc);
    if (!hina_buffer_is_valid(g_app.ubo_buffer)) {
        EXAMPLE_LOGE("Failed to create uniform buffer");
        return false;
    }

    g_app.ubo_mapped = static_cast<SceneUBO*>(hina_map_buffer(g_app.ubo_buffer));
    if (!g_app.ubo_mapped) {
        EXAMPLE_LOGE("Failed to map uniform buffer");
        return false;
    }

    // Create Depth Buffer
    if (!hina_depth_buffer_init(&g_app.depth, app->width, app->height)) {
        EXAMPLE_LOGE("Failed to create depth buffer");
        return false;
    }

    // Setup Vertex Layout
    hina_vertex_layout vertex_layout = {};
    vertex_layout.buffer_count = 1;
    vertex_layout.buffer_strides[0] = sizeof(Vertex);
    vertex_layout.input_rates[0] = HINA_VERTEX_INPUT_RATE_VERTEX;
    vertex_layout.attr_count = 3;
    vertex_layout.attrs[0] = { HINA_FORMAT_R32G32B32_SFLOAT, offsetof(Vertex, position), 0, 0 };
    vertex_layout.attrs[1] = { HINA_FORMAT_R32G32B32_SFLOAT, offsetof(Vertex, normal), 1, 0 };
    vertex_layout.attrs[2] = { HINA_FORMAT_R32G32B32_SFLOAT, offsetof(Vertex, color), 2, 0 };

    // Create Graphics Pipeline with Geometry Shader
    char* shader_path = hina_example_shader_path(app, "multiview.hina_sl");
    char* error = nullptr;

    hina_hsl_pipeline_desc pip_desc = hina_hsl_pipeline_desc_default();
    pip_desc.front_face = HINA_FRONT_FACE_CLOCKWISE;
    pip_desc.layout = vertex_layout;
    pip_desc.depth_format = HINA_FORMAT_D32_SFLOAT;

    g_app.pipeline = hina_example_make_pipeline_from_hsl(app, shader_path, &pip_desc, &error);
    free(shader_path);

    if (!hina_pipeline_is_valid(g_app.pipeline)) {
        EXAMPLE_LOGE("Pipeline creation failed: %s", error ? error : "Unknown error");
        if (error) hslc_free_log(error);
        return false;
    }

    EXAMPLE_LOGI("Pipeline with geometry shader created successfully");

    // Get bind group layout
    g_app.scene_layout = hina_pipeline_get_bind_group_layout(g_app.pipeline, 0);
    if (!hina_bind_group_layout_is_valid(g_app.scene_layout)) {
        EXAMPLE_LOGE("Failed to get bind group layout");
        return false;
    }

    // Create bind group for UBO
    hina_bind_group_entry ubo_entry = {};
    ubo_entry.binding = 0;
    ubo_entry.type = HINA_DESC_TYPE_UNIFORM_BUFFER;
    ubo_entry.buffer.buffer = g_app.ubo_buffer;
    ubo_entry.buffer.offset = 0;
    ubo_entry.buffer.size = sizeof(SceneUBO);

    hina_bind_group_desc bg_desc = {};
    bg_desc.layout = g_app.scene_layout;
    bg_desc.entries = &ubo_entry;
    bg_desc.entry_count = 1;
    bg_desc.label = "Scene UBO";

    g_app.scene_bind_group = hina_create_bind_group(&bg_desc);
    if (!hina_bind_group_is_valid(g_app.scene_bind_group)) {
        EXAMPLE_LOGE("Failed to create bind group");
        return false;
    }

    // Initialize Camera
    g_app.camera.rotation = glm::vec3(-15.0f, 0.0f, 0.0f);
    g_app.camera.zoom = -3.0f;
    g_app.rotation = 0.0f;

    EXAMPLE_LOGI("Viewport Array example initialized");
    EXAMPLE_LOGI("Rendering to 2 viewports using geometry shader instancing");

    return true;
}

// ============================================================================
// Rendering
// ============================================================================

static void example_render(hina_example_app* app) {
    // Update rotation
    g_app.rotation += 45.0f * app->delta_time;

    // Update camera from input
    g_app.camera.update(*app);

    // Begin frame
    hina_swapchain_image swapchain = hina_frame_begin();
    if (swapchain.texture.id == HINA_INVALID_HANDLE) {
        hina_example_sleep(10);
        hina_frame_end();
        return;
    }

    // Query actual swapchain dimensions
    uint32_t w, h;
    hina_get_texture_size(swapchain.texture, &w, &h);

    // Recreate depth buffer if swapchain size changed
    if (!hina_depth_buffer_resize(&g_app.depth, w, h)) {
        EXAMPLE_LOGE("Failed to recreate depth buffer");
        hina_frame_end();
        return;
    }

    // Use full screen aspect but render to left/right halves via GS
    float aspect = static_cast<float>(w) / static_cast<float>(h) * 0.5f;  // Half width for each view

    // Update UBO with matrices for each GS invocation
    glm::mat4 model = glm::rotate(glm::mat4(1.0f), glm::radians(g_app.rotation), glm::vec3(0.0f, 1.0f, 0.0f));
    model = glm::rotate(model, glm::radians(g_app.rotation * 0.5f), glm::vec3(1.0f, 0.0f, 0.0f));

    // Light position
    g_app.ubo_mapped->lightPos = glm::vec4(5.0f, 5.0f, 5.0f, 1.0f);

    // Screen offsets: -0.5 for left half, +0.5 for right half (in NDC)
    g_app.ubo_mapped->screenOffsets[0] = glm::vec4(-0.5f, 0.0f, 0.0f, 0.0f);  // Left
    g_app.ubo_mapped->screenOffsets[1] = glm::vec4(0.5f, 0.0f, 0.0f, 0.0f);   // Right

    // Left invocation (index 0) - slightly rotated left
    {
        glm::mat4 view = g_app.camera.view_matrix();
        view = glm::rotate(view, glm::radians(-5.0f), glm::vec3(0.0f, 1.0f, 0.0f));  // Stereo offset
        g_app.ubo_mapped->projection[0] = glm::perspective(glm::radians(60.0f), aspect, 0.1f, 256.0f);
        g_app.ubo_mapped->modelview[0] = view * model;
    }

    // Right invocation (index 1) - slightly rotated right
    {
        glm::mat4 view = g_app.camera.view_matrix();
        view = glm::rotate(view, glm::radians(5.0f), glm::vec3(0.0f, 1.0f, 0.0f));  // Stereo offset
        g_app.ubo_mapped->projection[1] = glm::perspective(glm::radians(60.0f), aspect, 0.1f, 256.0f);
        g_app.ubo_mapped->modelview[1] = view * model;
    }

    // Begin command buffer
    hina_cmd* cmd = hina_cmd_begin_ex(HINA_QUEUE_GRAPHICS);
    if (!cmd) {
        hina_frame_end();
        return;
    }

    // Setup render pass
    hina_pass_action pass = {};
    pass.colors[0].image = hina_texture_get_default_view(swapchain.texture);
    pass.colors[0].load_op = HINA_LOAD_OP_CLEAR;
    pass.colors[0].store_op = HINA_STORE_OP_STORE;
    pass.colors[0].clear_color[0] = 0.1f;
    pass.colors[0].clear_color[1] = 0.1f;
    pass.colors[0].clear_color[2] = 0.15f;
    pass.colors[0].clear_color[3] = 1.0f;
    pass.depth.image = hina_texture_get_default_view(g_app.depth.texture);
    pass.depth.load_op = HINA_LOAD_OP_CLEAR;
    pass.depth.store_op = HINA_STORE_OP_DONT_CARE;
    pass.depth.depth_clear = 1.0f;
    pass.depth.stencil_clear = 0;

    hina_cmd_begin_pass(cmd, &pass);
    hina_cmd_bind_pipeline(cmd, g_app.pipeline);

    // Bind resources
    hina_cmd_bind_group(cmd, 0, g_app.scene_bind_group);

    // Setup vertex/index bindings
    hina_vertex_input bindings = {};
    bindings.vertex_buffers[0] = g_app.vbo;
    bindings.vertex_offsets[0] = 0;
    bindings.index_buffer = g_app.ibo;
    bindings.index_type = HINA_INDEX_UINT32;

    hina_cmd_apply_vertex_input(cmd, &bindings);

    // Draw - the geometry shader will duplicate to both viewports
    uint32_t index_count = static_cast<uint32_t>(g_app.mesh.indices.size());
    hina_cmd_draw_indexed(cmd, index_count, 1, 0, 0, 0);

    // Present frame (ends pass, renders ImGui, submits, ends frame)
    hina_example_present_frame(app, cmd, swapchain);
}

// ============================================================================
// Cleanup
// ============================================================================

static void example_cleanup(hina_example_app* app) {
    (void)app;
    EXAMPLE_LOGI("Cleaning up Viewport Array example...");

    if (hina_bind_group_is_valid(g_app.scene_bind_group))
        hina_destroy_bind_group(g_app.scene_bind_group);

    if (hina_pipeline_is_valid(g_app.pipeline))
        hina_destroy_pipeline(g_app.pipeline);

    hina_depth_buffer_destroy(&g_app.depth);

    if (hina_buffer_is_valid(g_app.ubo_buffer))
        hina_destroy_buffer(g_app.ubo_buffer);

    if (hina_buffer_is_valid(g_app.ibo))
        hina_destroy_buffer(g_app.ibo);

    if (hina_buffer_is_valid(g_app.vbo))
        hina_destroy_buffer(g_app.vbo);

    g_app = {};
    EXAMPLE_LOGI("Cleanup complete");
}

// ============================================================================
// Entry Point
// ============================================================================

HINA_EXAMPLE_MAIN("HinaVK Viewport Array (Geometry Shader)", example_init, example_render, example_cleanup)
