/**
 * HinaVK Stencil Buffer Example - Outline Rendering
 *
 * This example demonstrates:
 * - Stencil buffer usage for outline rendering
 * - Two-pass rendering: toon shading + outline
 * - Vertex extrusion along normals for outline effect
 *
 * Based on https://github.com/SaschaWillems/Vulkan/blob/master/examples/stencilbuffer/stencilbuffer.cpp
 *
 * Technique:
 * 1. First pass (Toon): Render model with cel-shading, write 1 to stencil buffer
 * 2. Second pass (Outline): Render extruded model only where stencil != 1
 *    This creates a white outline around the model silhouette
 */

#include <cmath>
#include <vector>

#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>

#include "../hina_example.h"

// ============================================================================
// Vertex Data Structure
// ============================================================================

struct Vertex {
    glm::vec3 position;
    glm::vec3 normal;
};

// Uniform buffer structure (must match shader)
struct UniformData {
    glm::mat4 projection;
    glm::mat4 model;
    glm::vec4 light_pos;
    float outline_width;
    float _pad[3];
};

// ============================================================================
// Mesh Generation - Venus-like figure (simplified sphere for demo)
// ============================================================================

struct Mesh {
    std::vector<Vertex> vertices;
    std::vector<uint32_t> indices;
};

// Generate a UV sphere mesh
static Mesh generate_sphere(float radius, int stacks, int slices) {
    Mesh mesh;

    // Generate vertices
    for (int i = 0; i <= stacks; i++) {
        float phi = glm::pi<float>() * float(i) / float(stacks);
        float y = radius * cos(phi);
        float r = radius * sin(phi);

        for (int j = 0; j <= slices; j++) {
            float theta = 2.0f * glm::pi<float>() * float(j) / float(slices);
            float x = r * cos(theta);
            float z = r * sin(theta);

            Vertex v;
            v.position = glm::vec3(x, y, z);
            v.normal = glm::normalize(v.position);  // For sphere, normal = normalized position
            mesh.vertices.push_back(v);
        }
    }

    // Generate indices
    for (int i = 0; i < stacks; i++) {
        for (int j = 0; j < slices; j++) {
            int current = i * (slices + 1) + j;
            int next = current + slices + 1;

            // First triangle
            mesh.indices.push_back(current);
            mesh.indices.push_back(next);
            mesh.indices.push_back(current + 1);

            // Second triangle
            mesh.indices.push_back(current + 1);
            mesh.indices.push_back(next);
            mesh.indices.push_back(next + 1);
        }
    }

    return mesh;
}

// ============================================================================
// Application State
// ============================================================================

struct StencilBufferApp {
    // Mesh data
    Mesh mesh;

    // Vulkan resources
    hina_buffer vbo;
    hina_buffer ibo;
    hina_buffer ubo;
    UniformData* ubo_mapped;
    hina_texture depth_stencil;
    uint32_t ds_width;
    uint32_t ds_height;
    hina_pipeline toon_pipeline;
    hina_pipeline outline_pipeline;
    hina_bind_group_layout scene_layout;

    // Camera
    hina_camera camera;
    float rotation_angle;
};

static StencilBufferApp g_app = {};

// ============================================================================
// Initialization
// ============================================================================

static bool example_init(hina_example_app* app) {
    EXAMPLE_LOGI("Initializing Stencil Buffer example...");

    // Generate Mesh
    g_app.mesh = generate_sphere(1.0f, 32, 32);
    EXAMPLE_LOGI("Generated sphere: %zu vertices, %zu indices",
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

    // Create Uniform Buffer
    hina_buffer_desc ubo_desc = {0};
    ubo_desc.size = sizeof(UniformData);
    ubo_desc.flags = static_cast<hina_buffer_flags>(
        HINA_BUFFER_UNIFORM_BIT | HINA_BUFFER_HOST_VISIBLE_BIT | HINA_BUFFER_HOST_COHERENT_BIT);

    g_app.ubo = hina_make_buffer(&ubo_desc);
    if (!hina_buffer_is_valid(g_app.ubo)) {
        EXAMPLE_LOGE("Failed to create uniform buffer");
        return false;
    }

    g_app.ubo_mapped = static_cast<UniformData*>(hina_map_buffer(g_app.ubo));
    if (!g_app.ubo_mapped) {
        EXAMPLE_LOGE("Failed to map uniform buffer");
        return false;
    }

    // Create Depth-Stencil Buffer
    // We need D32_SFLOAT_S8_UINT or D24_UNORM_S8_UINT for stencil support
    hina_texture_desc ds_desc = hina_texture_desc_default();
    ds_desc.type = HINA_TEX_TYPE_2D;
    ds_desc.format = HINA_FORMAT_D32_SFLOAT_S8_UINT;  // Depth + Stencil
    ds_desc.width = app->width;
    ds_desc.height = app->height;
    ds_desc.layers = 1;
    ds_desc.mip_levels = 1;
    ds_desc.samples = HINA_SAMPLE_COUNT_1_BIT;
    ds_desc.usage = static_cast<hina_texture_usage_flags>(
        HINA_TEXTURE_RENDER_TARGET_BIT | HINA_TEXTURE_SAMPLED_BIT);

    g_app.depth_stencil = hina_make_texture(&ds_desc);
    if (!hina_texture_is_valid(g_app.depth_stencil)) {
        EXAMPLE_LOGE("Failed to create depth-stencil buffer");
        return false;
    }

    g_app.ds_width = app->width;
    g_app.ds_height = app->height;

    // Setup Vertex Layout
    hina_vertex_layout vertex_layout = {};
    vertex_layout.buffer_count = 1;
    vertex_layout.buffer_strides[0] = sizeof(Vertex);
    vertex_layout.input_rates[0] = HINA_VERTEX_INPUT_RATE_VERTEX;
    vertex_layout.attr_count = 2;
    vertex_layout.attrs[0] = { HINA_FORMAT_R32G32B32_SFLOAT, offsetof(Vertex, position), 0, 0 };
    vertex_layout.attrs[1] = { HINA_FORMAT_R32G32B32_SFLOAT, offsetof(Vertex, normal), 1, 0 };

    // Create Toon Pipeline (First Pass - writes to stencil)
    char* toon_shader_path = hina_example_shader_path(app, "toon.hina_sl");
    char* error = nullptr;

    char* toon_source = hina_example_load_file(app, toon_shader_path, nullptr);
    free(toon_shader_path);

    if (!toon_source) {
        EXAMPLE_LOGE("Failed to load toon shader file");
        return false;
    }

    hina_hsl_module* toon_module = hslc_compile_hsl_source(toon_source, "toon.hina_sl", &error);
    free(toon_source);

    if (!toon_module) {
        EXAMPLE_LOGE("Toon shader compilation failed: %s", error ? error : "Unknown");
        if (error) hslc_free_log(error);
        return false;
    }

    hina_hsl_pipeline_desc toon_pip_desc = hina_hsl_pipeline_desc_default();
    toon_pip_desc.layout = vertex_layout;
    toon_pip_desc.front_face = HINA_FRONT_FACE_COUNTER_CLOCKWISE;
    toon_pip_desc.cull_mode = HINA_CULL_MODE_NONE;  // No culling for this demo

    // Enable stencil test: write 1 to stencil where we render
    toon_pip_desc.depth.stencil_test = true;
    toon_pip_desc.depth.depth_test = true;
    toon_pip_desc.depth.depth_write = true;
    toon_pip_desc.depth_format = HINA_FORMAT_D32_SFLOAT_S8_UINT;
    toon_pip_desc.stencil_format = HINA_FORMAT_D32_SFLOAT_S8_UINT;

    // Stencil state: always pass, replace stencil with reference (1)
    toon_pip_desc.stencil_front.compare_op = HINA_COMPARE_OP_ALWAYS;
    toon_pip_desc.stencil_front.pass_op = HINA_STENCIL_REPLACE;
    toon_pip_desc.stencil_front.fail_op = HINA_STENCIL_REPLACE;
    toon_pip_desc.stencil_front.depth_fail_op = HINA_STENCIL_REPLACE;
    toon_pip_desc.stencil_front.compare_mask = 0xFF;
    toon_pip_desc.stencil_front.write_mask = 0xFF;
    toon_pip_desc.stencil_front.reference = 1;

    // Same for back faces
    toon_pip_desc.stencil_back = toon_pip_desc.stencil_front;

    g_app.toon_pipeline = hina_make_pipeline_from_module(toon_module, &toon_pip_desc, NULL);
    hslc_hsl_module_free(toon_module);

    if (!hina_pipeline_is_valid(g_app.toon_pipeline)) {
        EXAMPLE_LOGE("Toon pipeline creation failed");
        return false;
    }
    EXAMPLE_LOGI("Toon pipeline created successfully");

    // Create Outline Pipeline (Second Pass - reads stencil)
    char* outline_shader_path = hina_example_shader_path(app, "outline.hina_sl");

    char* outline_source = hina_example_load_file(app, outline_shader_path, nullptr);
    free(outline_shader_path);

    if (!outline_source) {
        EXAMPLE_LOGE("Failed to load outline shader file");
        return false;
    }

    hina_hsl_module* outline_module = hslc_compile_hsl_source(outline_source, "outline.hina_sl", &error);
    free(outline_source);

    if (!outline_module) {
        EXAMPLE_LOGE("Outline shader compilation failed: %s", error ? error : "Unknown");
        if (error) hslc_free_log(error);
        return false;
    }

    hina_hsl_pipeline_desc outline_pip_desc = hina_hsl_pipeline_desc_default();
    outline_pip_desc.layout = vertex_layout;
    outline_pip_desc.front_face = HINA_FRONT_FACE_COUNTER_CLOCKWISE;
    outline_pip_desc.cull_mode = HINA_CULL_MODE_NONE;

    // Disable depth test/write for outline (it should render on top/around)
    outline_pip_desc.depth.depth_test = false;
    outline_pip_desc.depth.depth_write = false;
    outline_pip_desc.depth.stencil_test = true;
    outline_pip_desc.depth_format = HINA_FORMAT_D32_SFLOAT_S8_UINT;
    outline_pip_desc.stencil_format = HINA_FORMAT_D32_SFLOAT_S8_UINT;

    // Stencil state: only render where stencil != 1
    outline_pip_desc.stencil_front.compare_op = HINA_COMPARE_OP_NOT_EQUAL;
    outline_pip_desc.stencil_front.pass_op = HINA_STENCIL_KEEP;
    outline_pip_desc.stencil_front.fail_op = HINA_STENCIL_KEEP;
    outline_pip_desc.stencil_front.depth_fail_op = HINA_STENCIL_KEEP;
    outline_pip_desc.stencil_front.compare_mask = 0xFF;
    outline_pip_desc.stencil_front.write_mask = 0xFF;
    outline_pip_desc.stencil_front.reference = 1;

    outline_pip_desc.stencil_back = outline_pip_desc.stencil_front;

    g_app.outline_pipeline = hina_make_pipeline_from_module(outline_module, &outline_pip_desc, NULL);
    hslc_hsl_module_free(outline_module);

    if (!hina_pipeline_is_valid(g_app.outline_pipeline)) {
        EXAMPLE_LOGE("Outline pipeline creation failed");
        return false;
    }
    EXAMPLE_LOGI("Outline pipeline created successfully");

    g_app.scene_layout = hina_pipeline_get_bind_group_layout(g_app.toon_pipeline, 0);
    if (!hina_bind_group_layout_is_valid(g_app.scene_layout)) {
        EXAMPLE_LOGE("Failed to get scene bind group layout");
        return false;
    }

    // Initialize Camera
    g_app.camera.rotation = glm::vec3(0.0f, 0.0f, 0.0f);
    g_app.camera.zoom = -4.0f;

    g_app.rotation_angle = 0.0f;

    EXAMPLE_LOGI("Stencil buffer outline rendering");
    EXAMPLE_LOGI("Red sphere with white outline using two-pass stencil technique");

    return true;
}

// ============================================================================
// Rendering
// ============================================================================

static void example_render(hina_example_app* app) {
    // Auto-rotate the model
    g_app.rotation_angle += 30.0f * app->delta_time;

    // Update camera from input
    g_app.camera.update(*app);

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

    // Recreate depth-stencil buffer if size changed
    if (g_app.ds_width != w || g_app.ds_height != h) {
        hina_destroy_texture(g_app.depth_stencil);

        hina_texture_desc ds_desc = hina_texture_desc_default();
        ds_desc.type = HINA_TEX_TYPE_2D;
        ds_desc.format = HINA_FORMAT_D32_SFLOAT_S8_UINT;
        ds_desc.width = w;
        ds_desc.height = h;
        ds_desc.layers = 1;
        ds_desc.mip_levels = 1;
        ds_desc.samples = HINA_SAMPLE_COUNT_1_BIT;
        ds_desc.usage = static_cast<hina_texture_usage_flags>(
            HINA_TEXTURE_RENDER_TARGET_BIT | HINA_TEXTURE_SAMPLED_BIT);

        g_app.depth_stencil = hina_make_texture(&ds_desc);
        if (!hina_texture_is_valid(g_app.depth_stencil)) {
            EXAMPLE_LOGE("Failed to recreate depth-stencil buffer");
            hina_frame_end();
            return;
        }
        g_app.ds_width = w;
        g_app.ds_height = h;
    }

    float aspect = static_cast<float>(w) / static_cast<float>(h);

    // Update uniform buffer
    g_app.ubo_mapped->projection = glm::perspective(glm::radians(60.0f), aspect, 0.1f, 256.0f);

    // Combine camera view with model rotation
    glm::mat4 model = glm::mat4(1.0f);
    model = glm::rotate(model, glm::radians(g_app.rotation_angle), glm::vec3(0.0f, 1.0f, 0.0f));
    model = glm::rotate(model, glm::radians(g_app.camera.rotation.x), glm::vec3(1.0f, 0.0f, 0.0f));
    model = glm::rotate(model, glm::radians(g_app.camera.rotation.y), glm::vec3(0.0f, 1.0f, 0.0f));

    glm::mat4 view = glm::translate(glm::mat4(1.0f), glm::vec3(0.0f, 0.0f, g_app.camera.zoom));
    g_app.ubo_mapped->model = view * model;

    g_app.ubo_mapped->light_pos = glm::vec4(0.0f, -2.0f, 1.0f, 1.0f);
    g_app.ubo_mapped->outline_width = 0.025f;

    // Begin command buffer
    hina_cmd* cmd = hina_cmd_begin_ex(HINA_QUEUE_GRAPHICS);
    if (!cmd) {
        hina_frame_end();
        return;
    }

    // Setup render pass with both color and depth-stencil attachments
    hina_pass_action pass = {};
    pass.colors[0].image = hina_texture_get_default_view(swapchain.texture);
    pass.colors[0].load_op = HINA_LOAD_OP_CLEAR;
    pass.colors[0].store_op = HINA_STORE_OP_STORE;
    pass.colors[0].clear_color[0] = 0.0f;
    pass.colors[0].clear_color[1] = 0.0f;
    pass.colors[0].clear_color[2] = 0.2f;
    pass.colors[0].clear_color[3] = 1.0f;

    pass.depth.image = hina_texture_get_default_view(g_app.depth_stencil);
    pass.depth.load_op = HINA_LOAD_OP_CLEAR;
    pass.depth.store_op = HINA_STORE_OP_STORE;  // Need to preserve for outline pass
    pass.depth.depth_clear = 1.0f;
    pass.depth.stencil_clear = 0;

    hina_cmd_begin_pass(cmd, &pass);

    // Setup vertex/index bindings (shared by both passes)
    hina_vertex_input bindings = {};
    bindings.vertex_buffers[0] = g_app.vbo;
    bindings.vertex_offsets[0] = 0;
    bindings.index_buffer = g_app.ibo;
    bindings.index_type = HINA_INDEX_UINT32;

    uint32_t index_count = static_cast<uint32_t>(g_app.mesh.indices.size());

    hina_bind_group_entry scene_entry = {};
    scene_entry.binding = 0;
    scene_entry.type = HINA_DESC_TYPE_UNIFORM_BUFFER;
    scene_entry.buffer.buffer = g_app.ubo;
    scene_entry.buffer.offset = 0;
    scene_entry.buffer.size = sizeof(UniformData);

    hina_bind_group_desc scene_group_desc = {};
    scene_group_desc.layout = g_app.scene_layout;
    scene_group_desc.entries = &scene_entry;
    scene_group_desc.entry_count = 1;
    scene_group_desc.label = "scene";

    hina_transient_bind_group scene_group = hina_example_make_transient_bind_group(&scene_group_desc);

    // ====================================================================
    // Pass 1: Toon shading - render model and write to stencil buffer
    // ====================================================================

    hina_cmd_bind_pipeline(cmd, g_app.toon_pipeline);
    hina_cmd_bind_transient_group(cmd, 0, scene_group);
    hina_cmd_apply_vertex_input(cmd, &bindings);
    hina_cmd_draw_indexed(cmd, index_count, 1, 0, 0, 0);

    // ====================================================================
    // Pass 2: Outline - render extruded model only where stencil != 1
    // ====================================================================

    hina_cmd_bind_pipeline(cmd, g_app.outline_pipeline);
    hina_cmd_bind_transient_group(cmd, 0, scene_group);
    hina_cmd_apply_vertex_input(cmd, &bindings);
    hina_cmd_draw_indexed(cmd, index_count, 1, 0, 0, 0);

    // Present frame (ends pass, renders ImGui, submits, ends frame)
    hina_example_present_frame(app, cmd, swapchain);
}

// ============================================================================
// Cleanup
// ============================================================================

static void example_cleanup(hina_example_app* app) {
    (void)app;
    EXAMPLE_LOGI("Cleaning up Stencil Buffer example...");

    if (hina_pipeline_is_valid(g_app.outline_pipeline))
        hina_destroy_pipeline(g_app.outline_pipeline);
    if (hina_pipeline_is_valid(g_app.toon_pipeline))
        hina_destroy_pipeline(g_app.toon_pipeline);

    if (hina_texture_is_valid(g_app.depth_stencil))
        hina_destroy_texture(g_app.depth_stencil);

    if (hina_buffer_is_valid(g_app.ubo))
        hina_destroy_buffer(g_app.ubo);
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

HINA_EXAMPLE_MAIN("HinaVK Stencil Buffer", example_init, example_render, example_cleanup)
