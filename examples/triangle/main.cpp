/**
 * HinaVK Triangle Example - Cross-Platform
 *
 * This example demonstrates indexed triangle rendering with:
 * - Interleaved vertex attributes (position + color)
 * - Index buffer
 * - Uniform buffer with MVP matrices
 * - Depth buffer
 * - Touch/mouse-controlled rotation
 *
 * Works on both Desktop (SDL) and Android (NativeActivity).
 */

#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>

#include "../hina_example.h"

// ============================================================================
// Vertex Data
// ============================================================================

struct Vertex {
    float position[3];
    float color[3];
};

static const Vertex vertices[] = {
    {{  1.0f,  1.0f, 0.0f }, { 1.0f, 0.0f, 0.0f }},  // Red
    {{ -1.0f,  1.0f, 0.0f }, { 0.0f, 1.0f, 0.0f }},  // Green
    {{  0.0f, -1.0f, 0.0f }, { 0.0f, 0.0f, 1.0f }}   // Blue
};

static const uint32_t indices[] = { 0, 1, 2 };

// Uniform buffer structure (must match shader)
struct UBO {
    glm::mat4 projection;
    glm::mat4 model;
    glm::mat4 view;
};

// ============================================================================
// Application State
// ============================================================================

struct TriangleApp {
    // Vulkan resources
    hina_buffer vbo;
    hina_buffer ibo;
    hina_buffer ubo_buffer;
    hina_pipeline pipeline;
    hina_bind_group_layout scene_layout;
    hina_depth_buffer depth;

    // Camera
    float rotation_x;
    float rotation_y;
    float zoom;
};

static TriangleApp g_app = {};

// ============================================================================
// Initialization
// ============================================================================

static bool example_init(hina_example_app* app) {
    EXAMPLE_LOGI("Initializing Triangle example...");

    // Vertex buffer
    hina_buffer_desc vbo_desc = {0};
    vbo_desc.size = sizeof(vertices);
    vbo_desc.flags = static_cast<hina_buffer_flags>(
        HINA_BUFFER_VERTEX_BIT | HINA_BUFFER_HOST_VISIBLE_BIT | HINA_BUFFER_HOST_COHERENT_BIT);
    vbo_desc.initial_data = vertices;
    g_app.vbo = hina_make_buffer(&vbo_desc);
    if (!hina_buffer_is_valid(g_app.vbo)) {
        EXAMPLE_LOGE("Failed to create vertex buffer");
        return false;
    }

    // Index buffer
    hina_buffer_desc ibo_desc = {0};
    ibo_desc.size = sizeof(indices);
    ibo_desc.flags = static_cast<hina_buffer_flags>(
        HINA_BUFFER_INDEX_BIT | HINA_BUFFER_HOST_VISIBLE_BIT | HINA_BUFFER_HOST_COHERENT_BIT);
    ibo_desc.initial_data = indices;
    g_app.ibo = hina_make_buffer(&ibo_desc);
    if (!hina_buffer_is_valid(g_app.ibo)) {
        EXAMPLE_LOGE("Failed to create index buffer");
        return false;
    }

    // Uniform buffer
    hina_buffer_desc ubo_desc = {0};
    ubo_desc.size = sizeof(UBO);
    ubo_desc.flags = static_cast<hina_buffer_flags>(
        HINA_BUFFER_UNIFORM_BIT | HINA_BUFFER_HOST_VISIBLE_BIT | HINA_BUFFER_HOST_COHERENT_BIT);
    g_app.ubo_buffer = hina_make_buffer(&ubo_desc);
    if (!hina_buffer_is_valid(g_app.ubo_buffer)) {
        EXAMPLE_LOGE("Failed to create uniform buffer");
        return false;
    }

    // Depth buffer
    if (!hina_depth_buffer_init(&g_app.depth, app->width, app->height)) {
        EXAMPLE_LOGE("Failed to create depth buffer");
        return false;
    }

    // Load and compile shader
    EXAMPLE_LOGI("Loading shader...");

    char* shader_path = hina_example_shader_path(app, "triangle.hina_sl");
    char* error = nullptr;
    char* source = hina_example_load_file(app, shader_path, nullptr);
    free(shader_path);

    if (!source) {
        EXAMPLE_LOGE("Failed to load shader file");
        return false;
    }

    hina_hsl_module* module = hslc_compile_hsl_source(source, "triangle.hina_sl", &error);
    free(source);

    if (!module) {
        EXAMPLE_LOGE("Shader compilation failed: %s", error ? error : "Unknown");
        if (error) hslc_free_log(error);
        return false;
    }

    // Create pipeline
    hina_vertex_layout vertex_layout = {};
    vertex_layout.buffer_count = 1;
    vertex_layout.buffer_strides[0] = sizeof(Vertex);
    vertex_layout.input_rates[0] = HINA_VERTEX_INPUT_RATE_VERTEX;
    vertex_layout.attr_count = 2;
    vertex_layout.attrs[0] = { HINA_FORMAT_R32G32B32_SFLOAT, offsetof(Vertex, position), 0, 0 };
    vertex_layout.attrs[1] = { HINA_FORMAT_R32G32B32_SFLOAT, offsetof(Vertex, color), 1, 0 };

    hina_hsl_pipeline_desc pip_desc = hina_hsl_pipeline_desc_default();
    pip_desc.cull_mode = HINA_CULL_MODE_NONE;
    pip_desc.layout = vertex_layout;
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

    // Initialize camera
    g_app.rotation_x = 0.0f;
    g_app.rotation_y = 0.0f;
    g_app.zoom = -2.5f;

    EXAMPLE_LOGI("Triangle example initialized");
    return true;
}

// ============================================================================
// Rendering
// ============================================================================

static void example_render(hina_example_app* app) {
    // Update camera from input (only if ImGui doesn't want the mouse)
    if (!hina_example_ui_want_mouse(app) && app->input_down) {
        g_app.rotation_x += app->input_delta_y * 0.5f;
        g_app.rotation_y += app->input_delta_x * 0.5f;
    }
    g_app.zoom += app->scroll_delta * 0.25f;  // scroll_delta is 0 on Android

    // Begin frame
    hina_swapchain_image swapchain = hina_frame_begin();
    if (swapchain.texture.id == HINA_INVALID_HANDLE) {
        hina_example_try_recover_surface(app);
        hina_frame_end();
        return;
    }

    // Resize depth buffer if needed
    uint32_t w, h;
    hina_get_texture_size(swapchain.texture, &w, &h);
    if (!hina_depth_buffer_resize(&g_app.depth, w, h)) {
        EXAMPLE_LOGE("Failed to resize depth buffer");
        hina_frame_end();
        return;
    }

    float aspect = static_cast<float>(w) / static_cast<float>(h);

    // Update uniform buffer
    UBO* ubo = static_cast<UBO*>(hina_map_buffer(g_app.ubo_buffer));
    if (ubo) {
        glm::mat4 rotM = glm::mat4(1.0f);
        rotM = glm::rotate(rotM, glm::radians(g_app.rotation_x), glm::vec3(1.0f, 0.0f, 0.0f));
        rotM = glm::rotate(rotM, glm::radians(g_app.rotation_y), glm::vec3(0.0f, 1.0f, 0.0f));

        glm::vec3 translation(0.0f, 0.0f, g_app.zoom);
        glm::mat4 transM = glm::translate(glm::mat4(1.0f), translation);

        ubo->projection = glm::perspective(glm::radians(60.0f), aspect, 0.1f, 256.0f);
        ubo->view = transM * rotM;
        ubo->model = glm::mat4(1.0f);
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
    pass.colors[0].clear_color[0] = 0.0f;
    pass.colors[0].clear_color[1] = 0.0f;
    pass.colors[0].clear_color[2] = 0.2f;
    pass.colors[0].clear_color[3] = 1.0f;
    pass.depth.image = hina_texture_get_default_view(g_app.depth.texture);
    pass.depth.load_op = HINA_LOAD_OP_CLEAR;
    pass.depth.store_op = HINA_STORE_OP_DONT_CARE;
    pass.depth.depth_clear = 1.0f;

    // Create bind group with current frame's UBO
    hina_bind_group_entry scene_entry = {};
    scene_entry.binding = 0;
    scene_entry.type = HINA_DESC_TYPE_UNIFORM_BUFFER;
    scene_entry.buffer.buffer = g_app.ubo_buffer;
    scene_entry.buffer.offset = 0;
    scene_entry.buffer.size = sizeof(UBO);

    hina_bind_group_desc scene_group_desc = {};
    scene_group_desc.layout = g_app.scene_layout;
    scene_group_desc.entries = &scene_entry;
    scene_group_desc.entry_count = 1;
    scene_group_desc.label = "scene";

    hina_transient_bind_group scene_group = hina_example_make_transient_bind_group(&scene_group_desc);

    // Draw
    hina_cmd_begin_pass(cmd, &pass);
    hina_cmd_bind_pipeline(cmd, g_app.pipeline);
    hina_cmd_bind_transient_group(cmd, 0, scene_group);

    hina_vertex_input bindings = {};
    bindings.vertex_buffers[0] = g_app.vbo;
    bindings.vertex_offsets[0] = 0;
    bindings.index_buffer = g_app.ibo;
    bindings.index_type = HINA_INDEX_UINT32;
    hina_cmd_apply_vertex_input(cmd, &bindings);

    hina_cmd_draw_indexed(cmd, 3, 1, 0, 0, 0);

    // Present frame (renders ImGui in separate pass, submits, ends frame)
    hina_example_present_frame(app, cmd, swapchain);
}

// ============================================================================
// Cleanup
// ============================================================================

static void example_cleanup(hina_example_app* app) {
    (void)app;
    EXAMPLE_LOGI("Cleaning up Triangle example...");

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

HINA_EXAMPLE_MAIN("HinaVK Triangle", example_init, example_render, example_cleanup)
