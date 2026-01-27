/**
 * HinaVK Descriptor Sets Example - Cross-Platform + MSAA Test
 *
 * Based on https://github.com/SaschaWillems/Vulkan/blob/master/examples/descriptorsets/descriptorsets.cpp
 *
 * This example demonstrates:
 * - Two cubes with different textures
 * - Per-object uniform buffers with MVP matrices
 * - Bind groups for textures and transforms
 * - Procedurally generated checkerboard textures
 * - ImGui overlay for runtime parameter adjustment
 * - **4x MSAA with color resolve** (tests legacy render pass path)
 *
 * Key differences from raw Vulkan:
 * - No explicit descriptor set allocation/management
 * - Bind groups built from HSL reflection
 *
 * Works on both Desktop (SDL) and Android (NativeActivity).
 */

#include <cmath>
#include <vector>

#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>

#include "../hina_example.h"

// ============================================================================
// Configuration
// ============================================================================

constexpr int TEXTURE_SIZE = 256;
constexpr bool ENABLE_MSAA = true;  // Toggle MSAA rendering

// ============================================================================
// Vertex Data Structure
// ============================================================================

struct Vertex {
    glm::vec3 position;
    glm::vec3 normal;
    glm::vec2 uv;
};

// Per-cube uniform buffer structure (must match shader)
struct CubeUBO {
    glm::mat4 projection;
    glm::mat4 view;
    glm::mat4 model;
    glm::vec4 light_pos;
};

// ============================================================================
// Cube Mesh Generation
// ============================================================================

struct Mesh {
    std::vector<Vertex> vertices;
    std::vector<uint32_t> indices;
};

static Mesh generate_cube() {
    Mesh mesh;

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

    // Face definitions: normal, 4 corner indices, UVs
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

    glm::vec2 uvs[4] = {
        {0.0f, 0.0f},
        {1.0f, 0.0f},
        {1.0f, 1.0f},
        {0.0f, 1.0f},
    };

    for (const auto& face : faces) {
        uint32_t base = static_cast<uint32_t>(mesh.vertices.size());

        // Add 4 vertices per face
        mesh.vertices.push_back({corners[face.v0], face.normal, uvs[0]});
        mesh.vertices.push_back({corners[face.v1], face.normal, uvs[1]});
        mesh.vertices.push_back({corners[face.v2], face.normal, uvs[2]});
        mesh.vertices.push_back({corners[face.v3], face.normal, uvs[3]});

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
// Procedural Texture Generation
// ============================================================================

static std::vector<uint8_t> generate_checkerboard_texture(
    int width, int height,
    glm::vec3 color1, glm::vec3 color2,
    int check_size = 32)
{
    std::vector<uint8_t> pixels(width * height * 4);

    for (int y = 0; y < height; y++) {
        for (int x = 0; x < width; x++) {
            int check_x = x / check_size;
            int check_y = y / check_size;
            bool is_color1 = ((check_x + check_y) % 2) == 0;

            glm::vec3 color = is_color1 ? color1 : color2;
            int idx = (y * width + x) * 4;
            pixels[idx + 0] = static_cast<uint8_t>(color.r * 255.0f);
            pixels[idx + 1] = static_cast<uint8_t>(color.g * 255.0f);
            pixels[idx + 2] = static_cast<uint8_t>(color.b * 255.0f);
            pixels[idx + 3] = 255;
        }
    }

    return pixels;
}

// ============================================================================
// Cube Instance Data
// ============================================================================

struct Cube {
    glm::vec3 position;
    glm::vec3 rotation;
    float rotation_speed;
    hina_buffer ubo_buffer;
    CubeUBO* ubo_mapped;
};

// ============================================================================
// Application State
// ============================================================================

struct DescriptorSetsApp {
    // Mesh data
    Mesh mesh;
    hina_buffer vbo;
    hina_buffer ibo;

    // Textures
    hina_texture texture1;
    hina_texture texture2;
    hina_sampler sampler;
    hina_texture_view tex1_view;
    hina_texture_view tex2_view;

    // Bind groups
    hina_bind_group tex1_bind_group;
    hina_bind_group tex2_bind_group;
    hina_bind_group_layout transforms_layout;
    hina_bind_group_layout material_layout;

    // Cubes
    Cube cubes[2];

    // Pipelines (1x and 4x MSAA versions)
    hina_pipeline pipeline_1x;
    hina_pipeline pipeline_4x;

    // Depth buffer (1x for non-MSAA fallback)
    hina_depth_buffer depth;

    // MSAA buffers (color + depth at 4x)
    hina_msaa_buffers msaa;

    // Camera

    // MSAA state
    bool msaa_enabled;
};

static DescriptorSetsApp g_app = {};

// ============================================================================
// Initialization
// ============================================================================

static bool example_init(hina_example_app* app) {
    EXAMPLE_LOGI("Initializing Descriptor Sets example...");

    // Generate Mesh
    g_app.mesh = generate_cube();
    EXAMPLE_LOGI("Generated cube: %zu vertices, %zu indices",
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

    // Create Procedural Textures
    // Texture 1: Red/White checkerboard
    std::vector<uint8_t> tex1_data = generate_checkerboard_texture(
        TEXTURE_SIZE, TEXTURE_SIZE,
        glm::vec3(1.0f, 0.2f, 0.2f),  // Red
        glm::vec3(1.0f, 1.0f, 1.0f),  // White
        32
    );

    hina_texture_desc tex1_desc = hina_texture_desc_default();
    tex1_desc.format = HINA_FORMAT_R8G8B8A8_UNORM;
    tex1_desc.width = TEXTURE_SIZE;
    tex1_desc.height = TEXTURE_SIZE;
    tex1_desc.initial_data = tex1_data.data();

    g_app.texture1 = hina_make_texture(&tex1_desc);
    if (!hina_texture_is_valid(g_app.texture1)) {
        EXAMPLE_LOGE("Failed to create texture 1");
        return false;
    }

    // Texture 2: Blue/Yellow checkerboard
    std::vector<uint8_t> tex2_data = generate_checkerboard_texture(
        TEXTURE_SIZE, TEXTURE_SIZE,
        glm::vec3(0.2f, 0.4f, 1.0f),  // Blue
        glm::vec3(1.0f, 1.0f, 0.2f),  // Yellow
        32
    );

    hina_texture_desc tex2_desc = hina_texture_desc_default();
    tex2_desc.format = HINA_FORMAT_R8G8B8A8_UNORM;
    tex2_desc.width = TEXTURE_SIZE;
    tex2_desc.height = TEXTURE_SIZE;
    tex2_desc.initial_data = tex2_data.data();

    g_app.texture2 = hina_make_texture(&tex2_desc);
    if (!hina_texture_is_valid(g_app.texture2)) {
        EXAMPLE_LOGE("Failed to create texture 2");
        return false;
    }

    // Create a sampler for the textures
    hina_sampler_desc sampler_desc = hina_sampler_desc_default();
    g_app.sampler = hina_make_sampler(&sampler_desc);
    if (!hina_sampler_is_valid(g_app.sampler)) {
        EXAMPLE_LOGE("Failed to create sampler");
        return false;
    }

    // Get texture views for bind groups
    g_app.tex1_view = hina_texture_get_default_view(g_app.texture1);
    g_app.tex2_view = hina_texture_get_default_view(g_app.texture2);

    // Create Depth Buffer (1x for non-MSAA path)
    if (!hina_depth_buffer_init(&g_app.depth, app->width, app->height)) {
        EXAMPLE_LOGE("Failed to create depth buffer");
        return false;
    }

    // Create MSAA buffers (4x color + depth)
    g_app.msaa_enabled = ENABLE_MSAA;
    if (g_app.msaa_enabled) {
        if (!hina_msaa_buffers_init(&g_app.msaa, app->width, app->height, HINA_SAMPLE_COUNT_4_BIT)) {
            EXAMPLE_LOGE("Failed to create MSAA buffers - falling back to 1x");
            g_app.msaa_enabled = false;
        } else {
            EXAMPLE_LOGI("Created 4x MSAA buffers: %ux%u", app->width, app->height);
        }
    }

    // Setup Cube Instances
    // Cube 1: Left side, red/white texture
    g_app.cubes[0].position = glm::vec3(-1.5f, 0.0f, 0.0f);
    g_app.cubes[0].rotation = glm::vec3(0.0f);
    g_app.cubes[0].rotation_speed = 45.0f;  // degrees per second

    // Cube 2: Right side, blue/yellow texture
    g_app.cubes[1].position = glm::vec3(1.5f, 0.0f, 0.0f);
    g_app.cubes[1].rotation = glm::vec3(0.0f);
    g_app.cubes[1].rotation_speed = -30.0f;  // Rotate opposite direction

    // Create uniform buffers for each cube
    for (int i = 0; i < 2; i++) {
        hina_buffer_desc ubo_desc = {0};
        ubo_desc.size = sizeof(CubeUBO);
        ubo_desc.memory = HINA_BUFFER_CPU;
        ubo_desc.usage = HINA_BUFFER_UNIFORM;

        g_app.cubes[i].ubo_buffer = hina_make_buffer(&ubo_desc);
        if (!hina_buffer_is_valid(g_app.cubes[i].ubo_buffer)) {
            EXAMPLE_LOGE("Failed to create UBO for cube %d", i);
            return false;
        }

        g_app.cubes[i].ubo_mapped = static_cast<CubeUBO*>(hina_mapped_buffer_ptr(g_app.cubes[i].ubo_buffer));
        if (!g_app.cubes[i].ubo_mapped) {
            EXAMPLE_LOGE("Failed to map UBO for cube %d", i);
        }
    }

    // Setup Vertex Layout
    hina_vertex_layout vertex_layout = {};
    vertex_layout.buffer_count = 1;
    vertex_layout.buffer_strides[0] = sizeof(Vertex);
    vertex_layout.input_rates[0] = HINA_VERTEX_INPUT_RATE_VERTEX;
    vertex_layout.attr_count = 3;
    vertex_layout.attrs[0] = { HINA_FORMAT_R32G32B32_SFLOAT, offsetof(Vertex, position), 0, 0 };
    vertex_layout.attrs[1] = { HINA_FORMAT_R32G32B32_SFLOAT, offsetof(Vertex, normal), 1, 0 };
    vertex_layout.attrs[2] = { HINA_FORMAT_R32G32_SFLOAT, offsetof(Vertex, uv), 2, 0 };

    // Create Graphics Pipelines (1x and 4x MSAA)
    char* shader_path = hina_example_shader_path(app, "cube.hina_sl");
    char* error = nullptr;

    // 1x pipeline (no MSAA)
    hina_hsl_pipeline_desc pip_desc_1x = hina_hsl_pipeline_desc_default();
    pip_desc_1x.front_face = HINA_FRONT_FACE_COUNTER_CLOCKWISE;
    pip_desc_1x.layout = vertex_layout;
    pip_desc_1x.color_formats[0] = hina_get_surface_format();
    pip_desc_1x.depth_format = HINA_FORMAT_D32_SFLOAT;
    pip_desc_1x.samples = HINA_SAMPLE_COUNT_1_BIT;

    g_app.pipeline_1x = hina_example_make_pipeline_from_hsl(app, shader_path, &pip_desc_1x, &error);
    if (!hina_pipeline_is_valid(g_app.pipeline_1x)) {
        EXAMPLE_LOGE("1x Pipeline creation failed: %s", error ? error : "Unknown error");
        if (error) hslc_free_log(error);
        free(shader_path);
        return false;
    }
    EXAMPLE_LOGI("1x Pipeline created successfully");

    // 4x MSAA pipeline
    hina_hsl_pipeline_desc pip_desc_4x = hina_hsl_pipeline_desc_default();
    pip_desc_4x.front_face = HINA_FRONT_FACE_COUNTER_CLOCKWISE;
    pip_desc_4x.layout = vertex_layout;
    pip_desc_4x.color_formats[0] = hina_get_surface_format();
    pip_desc_4x.depth_format = HINA_FORMAT_D32_SFLOAT;
    pip_desc_4x.samples = HINA_SAMPLE_COUNT_4_BIT;

    g_app.pipeline_4x = hina_example_make_pipeline_from_hsl(app, shader_path, &pip_desc_4x, &error);
    free(shader_path);

    if (!hina_pipeline_is_valid(g_app.pipeline_4x)) {
        EXAMPLE_LOGE("4x Pipeline creation failed: %s", error ? error : "Unknown error");
        if (error) hslc_free_log(error);
        // Don't fail - just disable MSAA
        g_app.msaa_enabled = false;
        EXAMPLE_LOGI("MSAA disabled due to pipeline creation failure");
    } else {
        EXAMPLE_LOGI("4x MSAA Pipeline created successfully");
    }

    // Get bind group layout for set 0 (Transforms) from the 1x pipeline
    g_app.transforms_layout = hina_pipeline_get_bind_group_layout(g_app.pipeline_1x, 0);
    if (!hina_bind_group_layout_is_valid(g_app.transforms_layout)) {
        EXAMPLE_LOGE("Failed to get bind group layout for set 0");
        return false;
    }

    // Get the bind group layout for set 1 (Material) from the 1x pipeline
    g_app.material_layout = hina_pipeline_get_bind_group_layout(g_app.pipeline_1x, 1);
    if (!hina_bind_group_layout_is_valid(g_app.material_layout)) {
        EXAMPLE_LOGE("Failed to get bind group layout for set 1");
        return false;
    }

    // Create bind group for texture 1 (red/white checkerboard)
    hina_bind_group_entry tex1_entry = {};
    tex1_entry.binding = 0;
    tex1_entry.type = HINA_DESC_TYPE_COMBINED_IMAGE_SAMPLER;
    tex1_entry.combined.view = g_app.tex1_view;
    tex1_entry.combined.sampler = g_app.sampler;

    hina_bind_group_desc tex1_group_desc = {};
    tex1_group_desc.layout = g_app.material_layout;
    tex1_group_desc.entries = &tex1_entry;
    tex1_group_desc.entry_count = 1;
    tex1_group_desc.label = "Texture1 Material";

    g_app.tex1_bind_group = hina_create_bind_group(&tex1_group_desc);
    if (!hina_bind_group_is_valid(g_app.tex1_bind_group)) {
        EXAMPLE_LOGE("Failed to create bind group for texture 1");
        return false;
    }

    // Create bind group for texture 2 (blue/yellow checkerboard)
    hina_bind_group_entry tex2_entry = {};
    tex2_entry.binding = 0;
    tex2_entry.type = HINA_DESC_TYPE_COMBINED_IMAGE_SAMPLER;
    tex2_entry.combined.view = g_app.tex2_view;
    tex2_entry.combined.sampler = g_app.sampler;

    hina_bind_group_desc tex2_group_desc = {};
    tex2_group_desc.layout = g_app.material_layout;
    tex2_group_desc.entries = &tex2_entry;
    tex2_group_desc.entry_count = 1;
    tex2_group_desc.label = "Texture2 Material";

    g_app.tex2_bind_group = hina_create_bind_group(&tex2_group_desc);
    if (!hina_bind_group_is_valid(g_app.tex2_bind_group)) {
        EXAMPLE_LOGE("Failed to create bind group for texture 2");
        return false;
    }

    EXAMPLE_LOGI("Texture bind groups created successfully");

    // Initialize Camera
    app->camera.rotation = glm::vec3(-15.0f, 0.0f, 0.0f);
    app->camera.zoom = -4.0f;

    EXAMPLE_LOGI("Descriptor Sets example initialized");
    EXAMPLE_LOGI("Two cubes with different textures, rotating at different speeds");

    return true;
}

// ============================================================================
// Rendering
// ============================================================================

static void example_render(hina_example_app* app) {
    // Begin UI frame (renders default FPS overlay)
    hina_example_begin_ui(app);

#ifdef HINA_EXAMPLE_HAS_IMGUI
    // Add custom settings in a separate window
    ImGui::SetNextWindowPos(ImVec2(10, 80), ImGuiCond_FirstUseEver);
    ImGui::SetNextWindowBgAlpha(0.5f);
    if (ImGui::Begin("Cube Controls", nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
        ImGui::Text("Cube 1 (Red/White):");
        ImGui::SliderFloat("Speed 1", &g_app.cubes[0].rotation_speed, -180.0f, 180.0f);
        ImGui::Text("Cube 2 (Blue/Yellow):");
        ImGui::SliderFloat("Speed 2", &g_app.cubes[1].rotation_speed, -180.0f, 180.0f);
        ImGui::Separator();
        if (ImGui::Button("Reset Rotations")) {
            g_app.cubes[0].rotation = glm::vec3(0.0f);
            g_app.cubes[1].rotation = glm::vec3(0.0f);
        }
        ImGui::Separator();
        // MSAA toggle
        bool can_toggle_msaa = hina_pipeline_is_valid(g_app.pipeline_4x);
        if (!can_toggle_msaa) ImGui::BeginDisabled();
        ImGui::Checkbox("4x MSAA", &g_app.msaa_enabled);
        if (!can_toggle_msaa) ImGui::EndDisabled();
        ImGui::Text("Render mode: %s", g_app.msaa_enabled ? "4x MSAA + Resolve" : "1x (No MSAA)");
    }
    ImGui::End();
#endif

    // Only update camera if UI doesn't want the mouse
    if (!hina_example_ui_want_mouse(app)) {
        app->camera.update(*app);
    }

    // Update cube rotations
    for (int i = 0; i < 2; i++) {
        g_app.cubes[i].rotation.y += g_app.cubes[i].rotation_speed * app->delta_time;
    }

    // Begin frame (acquires swapchain image)
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

    // Resize MSAA buffers if needed
    if (g_app.msaa_enabled) {
        if (!hina_msaa_buffers_resize(&g_app.msaa, w, h)) {
            EXAMPLE_LOGE("Failed to resize MSAA buffers");
            g_app.msaa_enabled = false;
        }
    }

    float aspect = static_cast<float>(w) / static_cast<float>(h);

    // Update uniform buffers for each cube
    glm::mat4 projection = glm::perspective(glm::radians(60.0f), aspect, 0.1f, 256.0f);
    projection[1][1] *= -1.0f; // Vulkan Y-flip (match deferred example)
    glm::mat4 view = app->camera.view_matrix();
    glm::vec4 light_pos = glm::vec4(5.0f, 5.0f, 5.0f, 1.0f);

    for (int i = 0; i < 2; i++) {
        if (g_app.cubes[i].ubo_mapped) {
            g_app.cubes[i].ubo_mapped->projection = projection;
            g_app.cubes[i].ubo_mapped->view = view;

            // Build model matrix with position and rotation
            glm::mat4 model = glm::translate(glm::mat4(1.0f), g_app.cubes[i].position);
            model = glm::rotate(model, glm::radians(g_app.cubes[i].rotation.x), glm::vec3(1.0f, 0.0f, 0.0f));
            model = glm::rotate(model, glm::radians(g_app.cubes[i].rotation.y), glm::vec3(0.0f, 1.0f, 0.0f));
            model = glm::rotate(model, glm::radians(g_app.cubes[i].rotation.z), glm::vec3(0.0f, 0.0f, 1.0f));

            g_app.cubes[i].ubo_mapped->model = model;
            g_app.cubes[i].ubo_mapped->light_pos = light_pos;
        }
    }

    // Begin command buffer
    hina_cmd* cmd = hina_cmd_begin_ex(HINA_QUEUE_GRAPHICS);
    if (!cmd) {
        hina_frame_end();
        return;
    }

    // Setup render pass - MSAA or direct to swapchain
    hina_pass_action pass = {};
    pass.colors[0].clear_color[0] = 0.1f;
    pass.colors[0].clear_color[1] = 0.1f;
    pass.colors[0].clear_color[2] = 0.15f;
    pass.colors[0].clear_color[3] = 1.0f;
    pass.depth.load_op = HINA_LOAD_OP_CLEAR;
    pass.depth.store_op = HINA_STORE_OP_DONT_CARE;
    pass.depth.depth_clear = 1.0f;
    pass.depth.stencil_clear = 0;

    hina_pipeline active_pipeline;
    if (g_app.msaa_enabled) {
        // MSAA: Render to MSAA color buffer, resolve to swapchain
        pass.colors[0].image = hina_texture_get_default_view(g_app.msaa.color);
        pass.colors[0].resolve = hina_texture_get_default_view(swapchain.texture);  // Resolve target
        pass.colors[0].load_op = HINA_LOAD_OP_CLEAR;
        pass.colors[0].store_op = HINA_STORE_OP_STORE;  // Store is for resolve target
        pass.depth.image = hina_texture_get_default_view(g_app.msaa.depth);
        active_pipeline = g_app.pipeline_4x;
    } else {
        // No MSAA: Render directly to swapchain
        pass.colors[0].image = hina_texture_get_default_view(swapchain.texture);
        pass.colors[0].resolve = {};  // No resolve
        pass.colors[0].load_op = HINA_LOAD_OP_CLEAR;
        pass.colors[0].store_op = HINA_STORE_OP_STORE;
        pass.depth.image = hina_texture_get_default_view(g_app.depth.texture);
        active_pipeline = g_app.pipeline_1x;
    }

    hina_cmd_begin_pass(cmd, &pass);
    hina_cmd_bind_pipeline(cmd, active_pipeline);

    // Setup vertex/index bindings (shared by both cubes)
    hina_vertex_input bindings = {};
    bindings.vertex_buffers[0] = g_app.vbo;
    bindings.vertex_offsets[0] = 0;
    bindings.index_buffer = g_app.ibo;
    bindings.index_type = HINA_INDEX_UINT32;

    uint32_t index_count = static_cast<uint32_t>(g_app.mesh.indices.size());

    // Store bind groups in array
    hina_bind_group cube_texture_groups[2] = { g_app.tex1_bind_group, g_app.tex2_bind_group };

    // Draw each cube with its own descriptor sets
    for (int i = 0; i < 2; i++) {
        // Bind this cube's uniform buffer to set 0 (Transforms)
        hina_bind_group_entry transforms_entry = {};
        transforms_entry.binding = 0;
        transforms_entry.type = HINA_DESC_TYPE_UNIFORM_BUFFER;
        transforms_entry.buffer.buffer = g_app.cubes[i].ubo_buffer;
        transforms_entry.buffer.offset = 0;
        transforms_entry.buffer.size = sizeof(CubeUBO);

        hina_bind_group_desc transforms_group_desc = {};
        transforms_group_desc.layout = g_app.transforms_layout;
        transforms_group_desc.entries = &transforms_entry;
        transforms_group_desc.entry_count = 1;
        transforms_group_desc.label = "Transforms";

        hina_transient_bind_group transforms_group = hina_example_make_transient_bind_group(&transforms_group_desc);

        hina_cmd_bind_transient_group(cmd, 0, transforms_group);
        // Bind this cube's texture bind group to set 1 (Material)
        hina_cmd_bind_group(cmd, 1, cube_texture_groups[i]);
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
    EXAMPLE_LOGI("Cleaning up Descriptor Sets example...");

    if (hina_bind_group_is_valid(g_app.tex2_bind_group))
        hina_destroy_bind_group(g_app.tex2_bind_group);
    if (hina_bind_group_is_valid(g_app.tex1_bind_group))
        hina_destroy_bind_group(g_app.tex1_bind_group);

    if (hina_pipeline_is_valid(g_app.pipeline_1x))
        hina_destroy_pipeline(g_app.pipeline_1x);
    if (hina_pipeline_is_valid(g_app.pipeline_4x))
        hina_destroy_pipeline(g_app.pipeline_4x);

    for (int i = 0; i < 2; i++) {
        if (hina_buffer_is_valid(g_app.cubes[i].ubo_buffer))
            hina_destroy_buffer(g_app.cubes[i].ubo_buffer);
    }

    hina_depth_buffer_destroy(&g_app.depth);
    hina_msaa_buffers_destroy(&g_app.msaa);

    if (hina_sampler_is_valid(g_app.sampler))
        hina_destroy_sampler(g_app.sampler);
    if (hina_texture_is_valid(g_app.texture2))
        hina_destroy_texture(g_app.texture2);
    if (hina_texture_is_valid(g_app.texture1))
        hina_destroy_texture(g_app.texture1);

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

HINA_EXAMPLE_MAIN("HinaVK Descriptor Sets", example_init, example_render, example_cleanup)
