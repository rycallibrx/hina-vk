/**
 * HinaVK Push Constants Example
 *
 * Based on https://github.com/SaschaWillems/Vulkan/blob/master/examples/pushconstants/pushconstants.cpp
 *
 * This example demonstrates:
 * - Using push constants to pass per-object data directly to shaders
 * - Rendering multiple spheres with different colors and positions
 * - Push constants as an efficient alternative to uniform buffers for small data
 *
 * Push constants are ideal for small, frequently changing data (up to 128 bytes
 * on most hardware) that varies per draw call, like object transforms or colors.
 */

#include <cmath>
#include <cstdlib>
#include <ctime>
#include <random>
#include <vector>

#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>

#include "../hina_example.h"

// ============================================================================
// Configuration
// ============================================================================

constexpr int SPHERE_COUNT = 16;
constexpr float PI = 3.14159265359f;

// ============================================================================
// Vertex Structure
// ============================================================================

struct Vertex {
    glm::vec3 position;
    glm::vec3 normal;
    glm::vec3 color;
};

// ============================================================================
// Push Constant Structure
// ============================================================================

// This data is passed directly to the shader via push constants
// Must match the layout in the shader
struct SpherePushConstants {
    glm::vec4 color;     // RGB + alpha
    glm::vec4 position;  // XYZ + w (unused)
};

// ============================================================================
// Uniform Buffer Structure
// ============================================================================

struct ViewProjectionUBO {
    glm::mat4 projection;
    glm::mat4 view;
    glm::mat4 model;
};

// ============================================================================
// Sphere Data
// ============================================================================

struct SphereData {
    glm::vec4 color;
    glm::vec4 position;
};

// ============================================================================
// Sphere Mesh Generation (UV sphere)
// ============================================================================

struct Mesh {
    std::vector<Vertex> vertices;
    std::vector<uint32_t> indices;
};

static Mesh generate_sphere(float radius, int segments, int rings) {
    Mesh mesh;

    // Generate vertices
    for (int y = 0; y <= rings; y++) {
        for (int x = 0; x <= segments; x++) {
            float xSegment = static_cast<float>(x) / static_cast<float>(segments);
            float ySegment = static_cast<float>(y) / static_cast<float>(rings);
            float xPos = std::cos(xSegment * 2.0f * PI) * std::sin(ySegment * PI);
            float yPos = std::cos(ySegment * PI);
            float zPos = std::sin(xSegment * 2.0f * PI) * std::sin(ySegment * PI);

            Vertex v;
            v.position = glm::vec3(xPos, yPos, zPos) * radius;
            v.normal = glm::normalize(glm::vec3(xPos, yPos, zPos));
            v.color = glm::vec3(1.0f);  // Base color (will be modulated by push constant)
            mesh.vertices.push_back(v);
        }
    }

    // Generate indices
    for (int y = 0; y < rings; y++) {
        for (int x = 0; x < segments; x++) {
            uint32_t i0 = y * (segments + 1) + x;
            uint32_t i1 = i0 + 1;
            uint32_t i2 = (y + 1) * (segments + 1) + x;
            uint32_t i3 = i2 + 1;

            // Two triangles per quad
            mesh.indices.push_back(i0);
            mesh.indices.push_back(i2);
            mesh.indices.push_back(i1);

            mesh.indices.push_back(i1);
            mesh.indices.push_back(i2);
            mesh.indices.push_back(i3);
        }
    }

    return mesh;
}

// ============================================================================
// Application State
// ============================================================================

struct PushConstantsApp {
    // Mesh data
    Mesh mesh;

    // Vulkan resources
    hina_buffer vbo;
    hina_buffer ibo;
    hina_buffer ubo_buffer;
    ViewProjectionUBO* ubo;
    hina_depth_buffer depth;
    hina_pipeline pipeline;
    hina_bind_group_layout scene_layout;

    // Sphere data
    std::vector<SphereData> spheres;

    // Camera
};

static PushConstantsApp g_app = {};

// ============================================================================
// Initialization
// ============================================================================

static bool example_init(hina_example_app* app) {
    EXAMPLE_LOGI("Initializing Push Constants example...");

    // Generate Sphere Mesh
    g_app.mesh = generate_sphere(0.5f, 32, 16);
    EXAMPLE_LOGI("Generated sphere: %zu vertices, %zu indices",
              g_app.mesh.vertices.size(), g_app.mesh.indices.size());

    // Create Vertex Buffer
    hina_buffer_desc vbo_desc = {0};
    vbo_desc.size = g_app.mesh.vertices.size() * sizeof(Vertex);
    vbo_desc.memory = HINA_BUFFER_CPU;
    vbo_desc.usage = HINA_BUFFER_VERTEX;
    vbo_desc.initial_data = g_app.mesh.vertices.data();

    g_app.vbo = hina_make_buffer(&vbo_desc);
    if (!hina_buffer_is_valid(g_app.vbo)) {
        EXAMPLE_LOGE("Failed to create vertex buffer");
        return false;
    }

    // Create Index Buffer
    hina_buffer_desc ibo_desc = {0};
    ibo_desc.size = g_app.mesh.indices.size() * sizeof(uint32_t);
    ibo_desc.memory = HINA_BUFFER_CPU;
    ibo_desc.usage = HINA_BUFFER_INDEX;
    ibo_desc.initial_data = g_app.mesh.indices.data();

    g_app.ibo = hina_make_buffer(&ibo_desc);
    if (!hina_buffer_is_valid(g_app.ibo)) {
        EXAMPLE_LOGE("Failed to create index buffer");
        return false;
    }

    // Create View/Projection UBO
    hina_buffer_desc ubo_desc = {0};
    ubo_desc.size = sizeof(ViewProjectionUBO);
    ubo_desc.memory = HINA_BUFFER_CPU;
    ubo_desc.usage = HINA_BUFFER_UNIFORM;

    g_app.ubo_buffer = hina_make_buffer(&ubo_desc);
    if (!hina_buffer_is_valid(g_app.ubo_buffer)) {
        EXAMPLE_LOGE("Failed to create UBO");
        return false;
    }

  g_app.ubo = static_cast<ViewProjectionUBO*>(hina_mapped_buffer_ptr(g_app.ubo_buffer));
    if (!g_app.ubo) {
        EXAMPLE_LOGE("Failed to map UBO");
        return false;
    }

    // Create Depth Buffer
    if (!hina_depth_buffer_init(&g_app.depth, app->width, app->height)) {
        EXAMPLE_LOGE("Failed to create depth buffer");
        return false;
    }

    // Initialize Sphere Data with Random Colors
    g_app.spheres.resize(SPHERE_COUNT);

    std::default_random_engine rng(static_cast<unsigned>(time(nullptr)));
    std::uniform_real_distribution<float> color_dist(0.1f, 1.0f);

    for (int i = 0; i < SPHERE_COUNT; i++) {
        // Random color
        g_app.spheres[i].color = glm::vec4(
            color_dist(rng),
            color_dist(rng),
            color_dist(rng),
            1.0f
        );

        // Arrange spheres in a circle
        float angle = (static_cast<float>(i) / static_cast<float>(SPHERE_COUNT)) * 2.0f * PI;
        float radius = 3.0f;
        g_app.spheres[i].position = glm::vec4(
            std::cos(angle) * radius,
            std::sin(angle) * radius,
            0.0f,
            1.0f
        );
    }

    EXAMPLE_LOGI("Initialized %d spheres with random colors", SPHERE_COUNT);

    // Setup Vertex Layout
    hina_vertex_layout vertex_layout = {};
    vertex_layout.buffer_count = 1;
    vertex_layout.buffer_strides[0] = sizeof(Vertex);
    vertex_layout.input_rates[0] = HINA_VERTEX_INPUT_RATE_VERTEX;
    vertex_layout.attr_count = 3;
    vertex_layout.attrs[0] = {
        HINA_FORMAT_R32G32B32_SFLOAT,
        static_cast<uint16_t>(offsetof(Vertex, position)), 0, 0
    };
    vertex_layout.attrs[1] = {
        HINA_FORMAT_R32G32B32_SFLOAT,
        static_cast<uint16_t>(offsetof(Vertex, normal)), 1, 0
    };
    vertex_layout.attrs[2] = {
        HINA_FORMAT_R32G32B32_SFLOAT,
        static_cast<uint16_t>(offsetof(Vertex, color)), 2, 0
    };

    // Create Graphics Pipeline
    char* shader_path = hina_example_shader_path(app, "pushconstants.hina_sl");
    char* error = nullptr;

    char* source = hina_example_load_file(app, shader_path, nullptr);
    free(shader_path);

    if (!source) {
        EXAMPLE_LOGE("Failed to load shader file");
        return false;
    }

    hina_hsl_module* module = hslc_compile_hsl_source(source, "pushconstants.hina_sl", &error);
    free(source);

    if (!module) {
        EXAMPLE_LOGE("Shader compilation failed: %s", error ? error : "Unknown");
        if (error) hslc_free_log(error);
        return false;
    }

    hina_hsl_pipeline_desc pip_desc = hina_hsl_pipeline_desc_default();
    pip_desc.front_face = HINA_FRONT_FACE_CLOCKWISE;  // Override: CW winding for this mesh
    pip_desc.layout = vertex_layout;
    pip_desc.color_formats[0] = hina_get_surface_format();
    pip_desc.depth_format = HINA_FORMAT_D32_SFLOAT;

    g_app.pipeline = hina_make_pipeline_from_module(module, &pip_desc, NULL);
    hslc_hsl_module_free(module);

    if (!hina_pipeline_is_valid(g_app.pipeline)) {
        EXAMPLE_LOGE("Pipeline creation failed");
        return false;
    }

    g_app.scene_layout = hina_pipeline_get_bind_group_layout(g_app.pipeline, 0);
    if (!hina_bind_group_layout_is_valid(g_app.scene_layout)) {
        EXAMPLE_LOGE("Failed to get scene bind group layout");
        return false;
    }

    EXAMPLE_LOGI("Pipeline created successfully");

    // Initialize Camera
    app->camera.rotation = glm::vec3(-15.0f, 0.0f, 0.0f);
    app->camera.zoom = -10.0f;

    EXAMPLE_LOGI("Rendering %d spheres using push constants", SPHERE_COUNT);

    return true;
}

// ============================================================================
// Rendering
// ============================================================================

static void example_render(hina_example_app* app) {
    // Update camera from input
    app->camera.update(*app);

    // Begin frame (acquires swapchain image)
    hina_swapchain_image swapchain = hina_frame_begin();
    if (swapchain.texture.id == HINA_INVALID_HANDLE) {
        hina_example_try_recover_surface(app);
        hina_frame_end();
        return;
    }

    // Query actual swapchain dimensions
    uint32_t w, h;
    hina_get_texture_size(swapchain.texture, &w, &h);

    // Resize depth buffer if needed
    if (!hina_depth_buffer_resize(&g_app.depth, w, h)) {
        EXAMPLE_LOGE("Failed to resize depth buffer");
        hina_frame_end();
        return;
    }

    float aspect = static_cast<float>(w) / static_cast<float>(h);

    // Update UBO
    g_app.ubo->projection = glm::perspective(glm::radians(60.0f), aspect, 0.1f, 256.0f);
    g_app.ubo->view = app->camera.view_matrix();
    g_app.ubo->model = glm::mat4(1.0f);

    // Record Commands
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
    pass.colors[0].clear_color[0] = 0.0f;
    pass.colors[0].clear_color[1] = 0.0f;
    pass.colors[0].clear_color[2] = 0.2f;
    pass.colors[0].clear_color[3] = 1.0f;
    pass.depth.image = hina_texture_get_default_view(g_app.depth.texture);
    pass.depth.load_op = HINA_LOAD_OP_CLEAR;
    pass.depth.store_op = HINA_STORE_OP_DONT_CARE;
    pass.depth.depth_clear = 1.0f;

    hina_bind_group_entry scene_entry = {};
    scene_entry.binding = 0;
    scene_entry.type = HINA_DESC_TYPE_UNIFORM_BUFFER;
    scene_entry.buffer.buffer = g_app.ubo_buffer;
    scene_entry.buffer.offset = 0;
    scene_entry.buffer.size = sizeof(ViewProjectionUBO);

    hina_bind_group_desc scene_group_desc = {};
    scene_group_desc.layout = g_app.scene_layout;
    scene_group_desc.entries = &scene_entry;
    scene_group_desc.entry_count = 1;
    scene_group_desc.label = "scene";

    hina_transient_bind_group scene_group = hina_example_make_transient_bind_group(&scene_group_desc);

    hina_cmd_begin_pass(cmd, &pass);
    hina_cmd_bind_pipeline(cmd, g_app.pipeline);
    hina_cmd_bind_transient_group(cmd, 0, scene_group);

    // Setup vertex/index bindings (same for all spheres)
    hina_vertex_input bindings = {};
    bindings.vertex_buffers[0] = g_app.vbo;
    bindings.vertex_offsets[0] = 0;
    bindings.index_buffer = g_app.ibo;
    bindings.index_type = HINA_INDEX_UINT32;

    uint32_t index_count = static_cast<uint32_t>(g_app.mesh.indices.size());

    // Draw each sphere with its own push constant data
    for (int i = 0; i < SPHERE_COUNT; i++) {
        // Push the sphere's color and position
        SpherePushConstants pc;
        pc.color = g_app.spheres[i].color;
        pc.position = g_app.spheres[i].position;
        hina_cmd_push_constants(cmd, 0, sizeof(SpherePushConstants), &pc);

        hina_cmd_apply_vertex_input(cmd, &bindings);
        hina_cmd_draw_indexed(cmd, index_count, 1, 0, 0, 0);
    }

    // Present frame (ends pass, renders ImGui, submits, ends frame)
    hina_example_present_frame(app, cmd, swapchain);
}

// ============================================================================
// Cleanup
// ============================================================================

static void example_cleanup(hina_example_app* app) {
    (void)app;
    EXAMPLE_LOGI("Cleaning up Push Constants example...");

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

HINA_EXAMPLE_MAIN("HinaVK Push Constants", example_init, example_render, example_cleanup)
