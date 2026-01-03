/**
 * HinaVK Dynamic Uniform Buffer Example - Cross-Platform
 *
 * Based on https://github.com/SaschaWillems/Vulkan/blob/master/examples/dynamicuniformbuffer/dynamicuniformbuffer.cpp
 *
 * This example demonstrates:
 * - Using dynamic uniform buffers to efficiently update per-object data
 * - Rendering multiple objects (125 cubes in a 5x5x5 grid) with a single descriptor set
 * - Proper alignment handling for dynamic uniform buffer offsets
 * - Random rotations and rotation speeds for visual variety
 *
 * Key concept: Instead of creating multiple descriptor sets (one per object),
 * we use a single large uniform buffer and bind different offsets dynamically.
 * This is more efficient as it reduces descriptor set switches.
 *
 * Works on both Desktop (SDL) and Android (NativeActivity).
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

// Grid configuration: 5x5x5 = 125 objects
constexpr int OBJECT_COUNT_X = 5;
constexpr int OBJECT_COUNT_Y = 5;
constexpr int OBJECT_COUNT_Z = 5;
constexpr int OBJECT_INSTANCES = OBJECT_COUNT_X * OBJECT_COUNT_Y * OBJECT_COUNT_Z;

// ============================================================================
// Vertex Structure
// ============================================================================

struct Vertex {
    glm::vec3 position;
    glm::vec3 color;
};

// ============================================================================
// Uniform Buffer Structures
// ============================================================================

// View/Projection UBO (shared across all objects)
struct ViewProjectionUBO {
    glm::mat4 projection;
    glm::mat4 view;
};

// Per-object model matrix (stored in dynamic UBO with proper alignment)
// Each entry in the dynamic UBO is aligned to minUniformBufferOffsetAlignment
struct alignas(256) DynamicUBOData {
    glm::mat4 model;
};

// ============================================================================
// Cube Mesh Generation (colored cube like Sascha Willems)
// ============================================================================

struct Mesh {
    std::vector<Vertex> vertices;
    std::vector<uint32_t> indices;
};

static Mesh generate_colored_cube() {
    Mesh mesh;

    // Cube vertices with colors (like Sascha Willems example)
    // Each vertex has a unique color based on its position
    mesh.vertices = {
        // Front face
        {{-1.0f, -1.0f,  1.0f}, {1.0f, 0.0f, 0.0f}},  // 0: red
        {{ 1.0f, -1.0f,  1.0f}, {0.0f, 1.0f, 0.0f}},  // 1: green
        {{ 1.0f,  1.0f,  1.0f}, {0.0f, 0.0f, 1.0f}},  // 2: blue
        {{-1.0f,  1.0f,  1.0f}, {1.0f, 1.0f, 0.0f}},  // 3: yellow
        // Back face
        {{-1.0f, -1.0f, -1.0f}, {1.0f, 0.0f, 1.0f}},  // 4: magenta
        {{ 1.0f, -1.0f, -1.0f}, {0.0f, 1.0f, 1.0f}},  // 5: cyan
        {{ 1.0f,  1.0f, -1.0f}, {1.0f, 1.0f, 1.0f}},  // 6: white
        {{-1.0f,  1.0f, -1.0f}, {0.0f, 0.0f, 0.0f}},  // 7: black
    };

    // Indices for 12 triangles (6 faces * 2 triangles)
    mesh.indices = {
        // Front
        0, 1, 2, 2, 3, 0,
        // Right
        1, 5, 6, 6, 2, 1,
        // Back
        5, 4, 7, 7, 6, 5,
        // Left
        4, 0, 3, 3, 7, 4,
        // Top
        3, 2, 6, 6, 7, 3,
        // Bottom
        4, 5, 1, 1, 0, 4,
    };

    return mesh;
}

// ============================================================================
// Object Instance Data
// ============================================================================

struct ObjectData {
    glm::vec3 rotation;        // Current rotation angles (degrees)
    glm::vec3 rotation_speed;  // Rotation speed (degrees per second)
};

// ============================================================================
// Dynamic UBO Management
// ============================================================================

// Calculate aligned size for dynamic UBO entries
// minUniformBufferOffsetAlignment is typically 64 or 256 bytes
static size_t calculate_dynamic_alignment(size_t data_size, size_t min_alignment) {
    if (min_alignment > 0) {
        return (data_size + min_alignment - 1) & ~(min_alignment - 1);
    }
    return data_size;
}

// ============================================================================
// Application State
// ============================================================================

struct DynamicUniformBufferApp {
    // Vulkan resources
    hina_buffer vbo;
    hina_buffer ibo;
    hina_buffer vp_ubo_buffer;
    hina_buffer dyn_ubo_buffer;
    hina_pipeline pipeline;
    hina_bind_group_layout scene_layout;
    hina_bind_group scene_group;
    hina_depth_buffer depth;

    // Mapped buffer pointers
    ViewProjectionUBO* vp_ubo;
    uint8_t* dyn_ubo_data;

    // Mesh data
    Mesh mesh;
    uint32_t index_count;

    // Object data
    std::vector<ObjectData> objects;

    // Camera
    hina_camera camera;

    // Alignment
    size_t dynamicAlignment;
};

static DynamicUniformBufferApp g_app = {};

// ============================================================================
// Initialization
// ============================================================================

static bool example_init(hina_example_app* app) {
    EXAMPLE_LOGI("Initializing Dynamic Uniform Buffer example...");

    // Query Device Alignment Requirements
    const hina_device_caps* caps = hina_get_device_caps();
    size_t minUboAlignment = caps ? static_cast<size_t>(caps->min_uniform_buffer_alignment) : 1;
    if (minUboAlignment == 0) minUboAlignment = 1;
    EXAMPLE_LOGI("Using UBO alignment: %zu bytes (device cap)", minUboAlignment);

    // Calculate the aligned size for each dynamic UBO entry
    g_app.dynamicAlignment = calculate_dynamic_alignment(sizeof(glm::mat4), minUboAlignment);
    EXAMPLE_LOGI("Dynamic UBO alignment: %zu bytes (mat4 is %zu bytes)",
           g_app.dynamicAlignment, sizeof(glm::mat4));

    // Generate Mesh
    g_app.mesh = generate_colored_cube();
    g_app.index_count = static_cast<uint32_t>(g_app.mesh.indices.size());
    EXAMPLE_LOGI("Generated cube: %zu vertices, %zu indices",
           g_app.mesh.vertices.size(), g_app.mesh.indices.size());

    // Create Vertex Buffer
    hina_buffer_desc vbo_desc = {0};
    vbo_desc.size = g_app.mesh.vertices.size() * sizeof(Vertex);
    vbo_desc.flags = static_cast<hina_buffer_flags>(
        HINA_BUFFER_VERTEX_BIT | HINA_BUFFER_HOST_VISIBLE_BIT | HINA_BUFFER_HOST_COHERENT_BIT);
    vbo_desc.initial_data = g_app.mesh.vertices.data();

    g_app.vbo = hina_make_buffer(&vbo_desc);
    if (!hina_buffer_is_valid(g_app.vbo)) {
        EXAMPLE_LOGE("Failed to create vertex buffer");
        return false;
    }

    // Create Index Buffer
    hina_buffer_desc ibo_desc = {0};
    ibo_desc.size = g_app.mesh.indices.size() * sizeof(uint32_t);
    ibo_desc.flags = static_cast<hina_buffer_flags>(
        HINA_BUFFER_INDEX_BIT | HINA_BUFFER_HOST_VISIBLE_BIT | HINA_BUFFER_HOST_COHERENT_BIT);
    ibo_desc.initial_data = g_app.mesh.indices.data();

    g_app.ibo = hina_make_buffer(&ibo_desc);
    if (!hina_buffer_is_valid(g_app.ibo)) {
        EXAMPLE_LOGE("Failed to create index buffer");
        return false;
    }

    // Create View/Projection UBO (static, shared by all objects)
    hina_buffer_desc vp_ubo_desc = {0};
    vp_ubo_desc.size = sizeof(ViewProjectionUBO);
    vp_ubo_desc.flags = static_cast<hina_buffer_flags>(
        HINA_BUFFER_UNIFORM_BIT | HINA_BUFFER_HOST_VISIBLE_BIT | HINA_BUFFER_HOST_COHERENT_BIT);

    g_app.vp_ubo_buffer = hina_make_buffer(&vp_ubo_desc);
    if (!hina_buffer_is_valid(g_app.vp_ubo_buffer)) {
        EXAMPLE_LOGE("Failed to create view/projection UBO");
        return false;
    }

    g_app.vp_ubo = static_cast<ViewProjectionUBO*>(hina_map_buffer(g_app.vp_ubo_buffer));
    if (!g_app.vp_ubo) {
        EXAMPLE_LOGE("Failed to map view/projection UBO");
        return false;
    }

    // Create Dynamic UBO (per-object model matrices with proper alignment)
    size_t dynamic_ubo_size = g_app.dynamicAlignment * OBJECT_INSTANCES;
    EXAMPLE_LOGI("Dynamic UBO total size: %zu bytes (%d objects)", dynamic_ubo_size, OBJECT_INSTANCES);

    hina_buffer_desc dyn_ubo_desc = {0};
    dyn_ubo_desc.size = dynamic_ubo_size;
    dyn_ubo_desc.flags = static_cast<hina_buffer_flags>(
        HINA_BUFFER_UNIFORM_BIT | HINA_BUFFER_HOST_VISIBLE_BIT | HINA_BUFFER_HOST_COHERENT_BIT);

    g_app.dyn_ubo_buffer = hina_make_buffer(&dyn_ubo_desc);
    if (!hina_buffer_is_valid(g_app.dyn_ubo_buffer)) {
        EXAMPLE_LOGE("Failed to create dynamic UBO");
        return false;
    }

    g_app.dyn_ubo_data = static_cast<uint8_t*>(hina_map_buffer(g_app.dyn_ubo_buffer));
    if (!g_app.dyn_ubo_data) {
        EXAMPLE_LOGE("Failed to map dynamic UBO");
        return false;
    }

    // Create Depth Buffer
    if (!hina_depth_buffer_init(&g_app.depth, app->width, app->height)) {
        EXAMPLE_LOGE("Failed to create depth buffer");
        return false;
    }

    // Initialize Object Data with Random Rotations
    g_app.objects.resize(OBJECT_INSTANCES);

    // Use random distributions for rotation speeds (like Sascha Willems)
    std::default_random_engine rng(static_cast<unsigned>(time(nullptr)));
    std::normal_distribution<float> rotation_dist(0.0f, 1.0f);
    std::uniform_real_distribution<float> speed_dist(-50.0f, 50.0f);

    for (int i = 0; i < OBJECT_INSTANCES; i++) {
        // Random initial rotation
        g_app.objects[i].rotation = glm::vec3(
            rotation_dist(rng) * 360.0f,
            rotation_dist(rng) * 360.0f,
            rotation_dist(rng) * 360.0f
        );

        // Random rotation speeds
        g_app.objects[i].rotation_speed = glm::vec3(
            speed_dist(rng),
            speed_dist(rng),
            speed_dist(rng)
        );
    }

    // Setup Vertex Layout
    hina_vertex_layout vertex_layout = {};
    vertex_layout.buffer_count = 1;
    vertex_layout.buffer_strides[0] = sizeof(Vertex);
    vertex_layout.input_rates[0] = HINA_VERTEX_INPUT_RATE_VERTEX;
    vertex_layout.attr_count = 2;
    vertex_layout.attrs[0] = {
        HINA_FORMAT_R32G32B32_SFLOAT,
        static_cast<uint16_t>(offsetof(Vertex, position)), 0, 0
    };
    vertex_layout.attrs[1] = {
        HINA_FORMAT_R32G32B32_SFLOAT,
        static_cast<uint16_t>(offsetof(Vertex, color)), 1, 0
    };

    // Create bind group layout with dynamic offset support
    hina_bind_group_layout_entry scene_entries[2] = {};
    scene_entries[0].binding = 0;
    scene_entries[0].type = HINA_DESC_TYPE_UNIFORM_BUFFER;
    scene_entries[0].stage_flags = HINA_STAGE_VERTEX;
    scene_entries[0].count = 1;
    scene_entries[0].flags = HINA_BINDING_FLAG_NONE;

    scene_entries[1].binding = 1;
    scene_entries[1].type = HINA_DESC_TYPE_UNIFORM_BUFFER;
    scene_entries[1].stage_flags = HINA_STAGE_VERTEX;
    scene_entries[1].count = 1;
    scene_entries[1].flags = HINA_BINDING_FLAG_DYNAMIC_OFFSET;

    hina_bind_group_layout_desc scene_layout_desc = {};
    scene_layout_desc.entries = scene_entries;
    scene_layout_desc.entry_count = 2;
    scene_layout_desc.label = "Scene";

    g_app.scene_layout = hina_create_bind_group_layout(&scene_layout_desc);
    if (!hina_bind_group_layout_is_valid(g_app.scene_layout)) {
        EXAMPLE_LOGE("Failed to create scene bind group layout");
        return false;
    }

    hina_bind_group_entry scene_group_entries[2] = {};
    scene_group_entries[0].binding = 0;
    scene_group_entries[0].type = HINA_DESC_TYPE_UNIFORM_BUFFER;
    scene_group_entries[0].buffer.buffer = g_app.vp_ubo_buffer;
    scene_group_entries[0].buffer.offset = 0;
    scene_group_entries[0].buffer.size = sizeof(ViewProjectionUBO);

    scene_group_entries[1].binding = 1;
    scene_group_entries[1].type = HINA_DESC_TYPE_UNIFORM_BUFFER;
    scene_group_entries[1].buffer.buffer = g_app.dyn_ubo_buffer;
    scene_group_entries[1].buffer.offset = 0;
    scene_group_entries[1].buffer.size = sizeof(glm::mat4);

    hina_bind_group_desc scene_group_desc = {};
    scene_group_desc.layout = g_app.scene_layout;
    scene_group_desc.entries = scene_group_entries;
    scene_group_desc.entry_count = 2;
    scene_group_desc.label = "scene";

    g_app.scene_group = hina_create_bind_group(&scene_group_desc);
    if (!hina_bind_group_is_valid(g_app.scene_group)) {
        EXAMPLE_LOGE("Failed to create scene bind group");
        return false;
    }

    // Create Graphics Pipeline
    char* shader_path = hina_example_shader_path(app, "dynamicuniformbuffer.hina_sl");
    char* error = nullptr;

    hina_hsl_pipeline_desc pip_desc = hina_hsl_pipeline_desc_default();
    pip_desc.front_face = HINA_FRONT_FACE_CLOCKWISE;  // Override: CW winding for this mesh
    pip_desc.layout = vertex_layout;
    pip_desc.depth_format = HINA_FORMAT_D32_SFLOAT;
    pip_desc.bind_group_layouts[0] = g_app.scene_layout;  // Count derived from array

    g_app.pipeline = hina_example_make_pipeline_from_hsl(app, shader_path, &pip_desc, &error);
    free(shader_path);

    if (!hina_pipeline_is_valid(g_app.pipeline)) {
        EXAMPLE_LOGE("Pipeline creation failed: %s", error ? error : "Unknown error");
        if (error) hslc_free_log(error);
        return false;
    }

    // Initialize Camera
    g_app.camera.rotation = glm::vec3(-15.0f, -45.0f, 0.0f);
    g_app.camera.zoom = -30.0f;  // Zoomed out to see all 125 cubes

    EXAMPLE_LOGI("Dynamic Uniform Buffer example initialized");
    EXAMPLE_LOGI("Rendering %d cubes in a %dx%dx%d grid",
           OBJECT_INSTANCES, OBJECT_COUNT_X, OBJECT_COUNT_Y, OBJECT_COUNT_Z);

    return true;
}

// ============================================================================
// Rendering
// ============================================================================

static void example_render(hina_example_app* app) {
    const float cube_spacing = 4.0f;  // Distance between cube centers

    // Update camera from input
    g_app.camera.update(*app);

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

    // Resize depth buffer if needed
    if (!hina_depth_buffer_resize(&g_app.depth, w, h)) {
        EXAMPLE_LOGE("Failed to resize depth buffer");
        hina_frame_end();
        return;
    }

    float aspect = static_cast<float>(w) / static_cast<float>(h);

    // Update View/Projection UBO
    g_app.vp_ubo->projection = glm::perspective(glm::radians(60.0f), aspect, 0.1f, 512.0f);
    g_app.vp_ubo->view = g_app.camera.view_matrix();

    // Update Dynamic UBO (all object model matrices)
    // Center offset for the grid
    float offset_x = -(OBJECT_COUNT_X - 1) * cube_spacing * 0.5f;
    float offset_y = -(OBJECT_COUNT_Y - 1) * cube_spacing * 0.5f;
    float offset_z = -(OBJECT_COUNT_Z - 1) * cube_spacing * 0.5f;

    int obj_idx = 0;
    for (int z = 0; z < OBJECT_COUNT_Z; z++) {
        for (int y = 0; y < OBJECT_COUNT_Y; y++) {
            for (int x = 0; x < OBJECT_COUNT_X; x++) {
                // Update rotation
                g_app.objects[obj_idx].rotation += g_app.objects[obj_idx].rotation_speed * app->delta_time;

                // Calculate position in grid
                glm::vec3 pos(
                    offset_x + x * cube_spacing,
                    offset_y + y * cube_spacing,
                    offset_z + z * cube_spacing
                );

                // Build model matrix
                glm::mat4 model = glm::translate(glm::mat4(1.0f), pos);
                model = glm::rotate(model, glm::radians(g_app.objects[obj_idx].rotation.x),
                                    glm::vec3(1.0f, 0.0f, 0.0f));
                model = glm::rotate(model, glm::radians(g_app.objects[obj_idx].rotation.y),
                                    glm::vec3(0.0f, 1.0f, 0.0f));
                model = glm::rotate(model, glm::radians(g_app.objects[obj_idx].rotation.z),
                                    glm::vec3(0.0f, 0.0f, 1.0f));

                // Write to aligned position in dynamic UBO
                glm::mat4* dst = reinterpret_cast<glm::mat4*>(
                    g_app.dyn_ubo_data + obj_idx * g_app.dynamicAlignment);
                *dst = model;

                obj_idx++;
            }
        }
    }

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

    hina_cmd_begin_pass(cmd, &pass);
    hina_cmd_bind_pipeline(cmd, g_app.pipeline);

    // Setup vertex/index bindings (same for all objects)
    hina_vertex_input bindings = {};
    bindings.vertex_buffers[0] = g_app.vbo;
    bindings.vertex_offsets[0] = 0;
    bindings.index_buffer = g_app.ibo;
    bindings.index_type = HINA_INDEX_UINT32;

    // Draw each object with dynamic offset into the UBO
    for (int i = 0; i < OBJECT_INSTANCES; i++) {
        // Bind model matrix UBO with dynamic offset (set 0, binding 1)
        uint32_t dynamic_offset = static_cast<uint32_t>(i * g_app.dynamicAlignment);
        hina_cmd_bind_group_with_offsets(cmd, 0, g_app.scene_group, &dynamic_offset, 1);
        hina_cmd_apply_vertex_input(cmd, &bindings);
        hina_cmd_draw_indexed(cmd, g_app.index_count, 1, 0, 0, 0);
    }

    // Present frame (ends pass, renders ImGui, submits, ends frame)
    hina_example_present_frame(app, cmd, swapchain);
}

// ============================================================================
// Cleanup
// ============================================================================

static void example_cleanup(hina_example_app* app) {
    (void)app;
    EXAMPLE_LOGI("Cleaning up Dynamic Uniform Buffer example...");

    if (hina_pipeline_is_valid(g_app.pipeline))
        hina_destroy_pipeline(g_app.pipeline);

    if (hina_bind_group_is_valid(g_app.scene_group))
        hina_destroy_bind_group(g_app.scene_group);

    if (hina_bind_group_layout_is_valid(g_app.scene_layout))
        hina_destroy_bind_group_layout(g_app.scene_layout);

    hina_depth_buffer_destroy(&g_app.depth);

    if (hina_buffer_is_valid(g_app.dyn_ubo_buffer))
        hina_destroy_buffer(g_app.dyn_ubo_buffer);
    if (hina_buffer_is_valid(g_app.vp_ubo_buffer))
        hina_destroy_buffer(g_app.vp_ubo_buffer);
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

HINA_EXAMPLE_MAIN("HinaVK Dynamic Uniform Buffer", example_init, example_render, example_cleanup)
