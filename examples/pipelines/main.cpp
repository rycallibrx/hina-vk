/**
 * HinaVK Pipelines Example - Based on Sascha Willems Vulkan Example
 *
 * This example demonstrates:
 * - Multiple graphics pipelines (Phong, Toon, Wireframe)
 * - Dynamic viewport/scissor state for split-screen rendering
 * - Different shading techniques
 *
 * Based on https://github.com/SaschaWillems/Vulkan/blob/master/examples/pipelines/pipelines.cpp
 */

#include <cmath>
#include <vector>
#include <map>

#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>

#include "../hina_example.h"

// ============================================================================
// Vertex Data Structure
// ============================================================================

struct Vertex {
    glm::vec3 position;
    glm::vec3 normal;
    glm::vec3 color;
};

// Uniform buffer structure (must match shader)
struct UBO {
    glm::mat4 projection;
    glm::mat4 model;
    glm::mat4 view;
    glm::vec4 light_pos;
};

// ============================================================================
// Procedural Mesh Generation - Icosphere
// ============================================================================

struct Mesh {
    std::vector<Vertex> vertices;
    std::vector<uint32_t> indices;
};

// Generate an icosphere by subdividing an icosahedron
static Mesh generate_icosphere(int subdivisions, const glm::vec3& color) {
    Mesh mesh;

    // Golden ratio
    const float t = (1.0f + std::sqrt(5.0f)) / 2.0f;

    // Initial icosahedron vertices
    std::vector<glm::vec3> positions = {
        glm::normalize(glm::vec3(-1,  t,  0)),
        glm::normalize(glm::vec3( 1,  t,  0)),
        glm::normalize(glm::vec3(-1, -t,  0)),
        glm::normalize(glm::vec3( 1, -t,  0)),
        glm::normalize(glm::vec3( 0, -1,  t)),
        glm::normalize(glm::vec3( 0,  1,  t)),
        glm::normalize(glm::vec3( 0, -1, -t)),
        glm::normalize(glm::vec3( 0,  1, -t)),
        glm::normalize(glm::vec3( t,  0, -1)),
        glm::normalize(glm::vec3( t,  0,  1)),
        glm::normalize(glm::vec3(-t,  0, -1)),
        glm::normalize(glm::vec3(-t,  0,  1))
    };

    // Initial icosahedron faces (20 triangles)
    std::vector<uint32_t> indices = {
        0, 11, 5,   0, 5, 1,    0, 1, 7,    0, 7, 10,   0, 10, 11,
        1, 5, 9,    5, 11, 4,   11, 10, 2,  10, 7, 6,   7, 1, 8,
        3, 9, 4,    3, 4, 2,    3, 2, 6,    3, 6, 8,    3, 8, 9,
        4, 9, 5,    2, 4, 11,   6, 2, 10,   8, 6, 7,    9, 8, 1
    };

    // Subdivide
    for (int s = 0; s < subdivisions; s++) {
        std::vector<uint32_t> new_indices;
        std::map<std::pair<uint32_t, uint32_t>, uint32_t> midpoint_cache;

        auto get_midpoint = [&](uint32_t i1, uint32_t i2) -> uint32_t {
            auto key = std::make_pair(std::min(i1, i2), std::max(i1, i2));
            auto it = midpoint_cache.find(key);
            if (it != midpoint_cache.end()) return it->second;

            glm::vec3 mid = glm::normalize((positions[i1] + positions[i2]) * 0.5f);
            uint32_t idx = static_cast<uint32_t>(positions.size());
            positions.push_back(mid);
            midpoint_cache[key] = idx;
            return idx;
        };

        for (size_t i = 0; i < indices.size(); i += 3) {
            uint32_t v0 = indices[i];
            uint32_t v1 = indices[i + 1];
            uint32_t v2 = indices[i + 2];

            uint32_t a = get_midpoint(v0, v1);
            uint32_t b = get_midpoint(v1, v2);
            uint32_t c = get_midpoint(v2, v0);

            new_indices.insert(new_indices.end(), {v0, a, c});
            new_indices.insert(new_indices.end(), {v1, b, a});
            new_indices.insert(new_indices.end(), {v2, c, b});
            new_indices.insert(new_indices.end(), {a, b, c});
        }
        indices = std::move(new_indices);
    }

    // Build vertex buffer with normals (for sphere, normal = position)
    mesh.vertices.reserve(positions.size());
    for (const auto& pos : positions) {
        Vertex v;
        v.position = pos;
        v.normal = pos;  // For unit sphere, normal = position
        v.color = color;
        mesh.vertices.push_back(v);
    }
    mesh.indices = std::move(indices);

    return mesh;
}

// ============================================================================
// Application State
// ============================================================================

struct PipelinesApp {
    // Mesh data
    Mesh mesh;
    uint32_t index_count;

    // Vulkan resources
    hina_buffer vbo;
    hina_buffer ibo;
    hina_buffer ubo_buffer;
    hina_pipeline phong_pipeline;
    hina_pipeline toon_pipeline;
    hina_pipeline wireframe_pipeline;
    hina_bind_group_layout scene_layout;
    hina_depth_buffer depth;

    // Mapped UBO
    UBO* ubo;

    // Camera
    float rotation_timer;
};

static PipelinesApp g_app = {};

// ============================================================================
// Initialization
// ============================================================================

static bool example_init(hina_example_app* app) {
    EXAMPLE_LOGI("Initializing Pipelines example...");

    // Generate mesh
    g_app.mesh = generate_icosphere(2, glm::vec3(0.8f, 0.6f, 0.2f));
    g_app.index_count = static_cast<uint32_t>(g_app.mesh.indices.size());
    EXAMPLE_LOGI("Generated icosphere: %zu vertices, %zu indices",
              g_app.mesh.vertices.size(), g_app.mesh.indices.size());

    // Create vertex buffer
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

    // Create index buffer
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

    // Create uniform buffer
    hina_buffer_desc ubo_desc = {0};
    ubo_desc.size = sizeof(UBO);
    ubo_desc.memory = HINA_BUFFER_CPU;
    ubo_desc.usage = HINA_BUFFER_UNIFORM;

    g_app.ubo_buffer = hina_make_buffer(&ubo_desc);
    if (!hina_buffer_is_valid(g_app.ubo_buffer)) {
        EXAMPLE_LOGE("Failed to create uniform buffer");
        return false;
    }

  g_app.ubo = static_cast<UBO*>(hina_mapped_buffer_ptr(g_app.ubo_buffer));
    if (!g_app.ubo) {
        EXAMPLE_LOGE("Failed to map uniform buffer");
        return false;
    }

    // Create depth buffer
    if (!hina_depth_buffer_init(&g_app.depth, app->width, app->height)) {       
        EXAMPLE_LOGE("Failed to create depth buffer");
        return false;
    }

    // Create a shared bind group layout for the scene UBO (set 0)
    hina_bind_group_layout_entry scene_layout_entries[1] = {};
    scene_layout_entries[0].binding = 0;
    scene_layout_entries[0].type = HINA_DESC_TYPE_UNIFORM_BUFFER;
    scene_layout_entries[0].stage_flags = HINA_STAGE_VERTEX;
    scene_layout_entries[0].count = 1;
    scene_layout_entries[0].flags = HINA_BINDING_FLAG_NONE;

    hina_bind_group_layout_desc scene_layout_desc = {};
    scene_layout_desc.entries = scene_layout_entries;
    scene_layout_desc.entry_count = 1;
    scene_layout_desc.label = "scene_ubo";

    g_app.scene_layout = hina_create_bind_group_layout(&scene_layout_desc);
    if (!hina_bind_group_layout_is_valid(g_app.scene_layout)) {
        EXAMPLE_LOGE("Failed to create scene bind group layout");
        return false;
    }

    // Setup vertex layout (shared by all pipelines)
    hina_vertex_layout vertex_layout = {};
    vertex_layout.buffer_count = 1;
    vertex_layout.buffer_strides[0] = sizeof(Vertex);
    vertex_layout.input_rates[0] = HINA_VERTEX_INPUT_RATE_VERTEX;
    vertex_layout.attr_count = 3;
    vertex_layout.attrs[0] = { HINA_FORMAT_R32G32B32_SFLOAT, offsetof(Vertex, position), 0, 0 };
    vertex_layout.attrs[1] = { HINA_FORMAT_R32G32B32_SFLOAT, offsetof(Vertex, normal), 1, 0 };
    vertex_layout.attrs[2] = { HINA_FORMAT_R32G32B32_SFLOAT, offsetof(Vertex, color), 2, 0 };

    char* error = nullptr;

    // Create Phong pipeline
    char* phong_path = hina_example_shader_path(app, "phong.hina_sl");
    hina_hsl_pipeline_desc phong_desc = hina_hsl_pipeline_desc_default();
    phong_desc.layout = vertex_layout;
    phong_desc.color_formats[0] = hina_get_surface_format();
    phong_desc.depth_format = HINA_FORMAT_D32_SFLOAT;
    phong_desc.bind_group_layouts[0] = g_app.scene_layout;  // Count derived from array

    g_app.phong_pipeline = hina_example_make_pipeline_from_hsl(app, phong_path, &phong_desc, &error);
    free(phong_path);
    if (!hina_pipeline_is_valid(g_app.phong_pipeline)) {
        EXAMPLE_LOGE("Phong pipeline creation failed: %s", error ? error : "Unknown");
        if (error) hslc_free_log(error);
        return false;
    }

    // Create Toon pipeline
    char* toon_path = hina_example_shader_path(app, "toon.hina_sl");
    hina_hsl_pipeline_desc toon_desc = hina_hsl_pipeline_desc_default();
    toon_desc.layout = vertex_layout;
    toon_desc.color_formats[0] = hina_get_surface_format();
    toon_desc.depth_format = HINA_FORMAT_D32_SFLOAT;
    toon_desc.bind_group_layouts[0] = g_app.scene_layout;  // Count derived from array

    g_app.toon_pipeline = hina_example_make_pipeline_from_hsl(app, toon_path, &toon_desc, &error);
    free(toon_path);
    if (!hina_pipeline_is_valid(g_app.toon_pipeline)) {
        EXAMPLE_LOGE("Toon pipeline creation failed: %s", error ? error : "Unknown");
        if (error) hslc_free_log(error);
        return false;
    }

    // Create Wireframe pipeline
    char* wireframe_path = hina_example_shader_path(app, "wireframe.hina_sl");
    hina_hsl_pipeline_desc wireframe_desc = hina_hsl_pipeline_desc_default();
    wireframe_desc.polygon_mode = HINA_POLYGON_MODE_LINE;
    wireframe_desc.layout = vertex_layout;
    wireframe_desc.color_formats[0] = hina_get_surface_format();
    wireframe_desc.depth_format = HINA_FORMAT_D32_SFLOAT;
    wireframe_desc.bind_group_layouts[0] = g_app.scene_layout;  // Count derived from array

    g_app.wireframe_pipeline = hina_example_make_pipeline_from_hsl(app, wireframe_path, &wireframe_desc, &error);
    free(wireframe_path);
    if (!hina_pipeline_is_valid(g_app.wireframe_pipeline)) {
        EXAMPLE_LOGE("Wireframe pipeline creation failed: %s", error ? error : "Unknown");
        if (error) hslc_free_log(error);
        return false;
    }

    EXAMPLE_LOGI("All pipelines created successfully");

    // Initialize camera
    app->camera.rotation = glm::vec3(0.0f, 0.0f, 0.0f);
    app->camera.zoom = -3.0f;
    g_app.rotation_timer = 0.0f;

    EXAMPLE_LOGI("Pipelines example initialized");
    return true;
}

// ============================================================================
// Rendering
// ============================================================================

static void example_render(hina_example_app* app) {
    g_app.rotation_timer += app->delta_time * 45.0f;

    // Update camera from input
    app->camera.update(*app);

    // Begin frame
    hina_swapchain_image swapchain = hina_frame_begin();
    if (swapchain.texture.id == HINA_INVALID_HANDLE) {
        hina_example_sleep(10);
        hina_frame_end();
        return;
    }

    // Query swapchain dimensions
    uint32_t w, h;
    hina_get_texture_size(swapchain.texture, &w, &h);

    // Resize depth buffer if needed
    if (!hina_depth_buffer_resize(&g_app.depth, w, h)) {
        EXAMPLE_LOGE("Failed to resize depth buffer");
        hina_frame_end();
        return;
    }

    float aspect = static_cast<float>(w) / static_cast<float>(h);

    // Update uniform buffer
    g_app.ubo->projection = glm::perspective(glm::radians(60.0f), aspect, 0.1f, 256.0f);
    g_app.ubo->projection[1][1] *= -1.0f; // Vulkan Y-flip

    glm::mat4 model = glm::mat4(1.0f);
    model = glm::rotate(model, glm::radians(g_app.rotation_timer), glm::vec3(0.0f, 1.0f, 0.0f));

    g_app.ubo->view = app->camera.view_matrix();
    g_app.ubo->model = model;
    g_app.ubo->light_pos = glm::vec4(0.0f, 0.0f, 5.0f, 1.0f);

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
    pass.colors[0].clear_color[2] = 0.1f;
    pass.colors[0].clear_color[3] = 1.0f;
    pass.depth.image = hina_texture_get_default_view(g_app.depth.texture);
    pass.depth.load_op = HINA_LOAD_OP_CLEAR;
    pass.depth.store_op = HINA_STORE_OP_DONT_CARE;
    pass.depth.depth_clear = 1.0f;

    // Create bind group
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

    hina_cmd_begin_pass(cmd, &pass);

    // Setup vertex bindings
    hina_vertex_input bindings = {};
    bindings.vertex_buffers[0] = g_app.vbo;
    bindings.vertex_offsets[0] = 0;
    bindings.index_buffer = g_app.ibo;
    bindings.index_type = HINA_INDEX_UINT32;

    float viewport_width = static_cast<float>(w) / 3.0f;

    // Render left third: Phong
    {
        hina_viewport viewport = { 0.0f, 0.0f, viewport_width, static_cast<float>(h), 0.0f, 1.0f };
        hina_scissor scissor = { 0, 0, static_cast<uint32_t>(viewport_width), h };

        hina_cmd_set_viewport(cmd, &viewport);
        hina_cmd_set_scissor(cmd, &scissor);
        hina_cmd_bind_pipeline(cmd, g_app.phong_pipeline);
    hina_cmd_bind_transient_group(cmd, 0, scene_group);
        hina_cmd_apply_vertex_input(cmd, &bindings);
        hina_cmd_draw_indexed(cmd, g_app.index_count, 1, 0, 0, 0);
    }

    // Render middle third: Toon
    {
        hina_viewport viewport = { viewport_width, 0.0f, viewport_width, static_cast<float>(h), 0.0f, 1.0f };
        hina_scissor scissor = { static_cast<int32_t>(viewport_width), 0, static_cast<uint32_t>(viewport_width), h };

        hina_cmd_set_viewport(cmd, &viewport);
        hina_cmd_set_scissor(cmd, &scissor);
        hina_cmd_bind_pipeline(cmd, g_app.toon_pipeline);
        hina_cmd_bind_transient_group(cmd, 0, scene_group);
        hina_cmd_apply_vertex_input(cmd, &bindings);
        hina_cmd_draw_indexed(cmd, g_app.index_count, 1, 0, 0, 0);
    }

    // Render right third: Wireframe
    {
        hina_viewport viewport = { viewport_width * 2.0f, 0.0f, viewport_width, static_cast<float>(h), 0.0f, 1.0f };
        hina_scissor scissor = { static_cast<int32_t>(viewport_width * 2.0f), 0, static_cast<uint32_t>(viewport_width) + 1, h };

        hina_cmd_set_viewport(cmd, &viewport);
        hina_cmd_set_scissor(cmd, &scissor);
        hina_cmd_bind_pipeline(cmd, g_app.wireframe_pipeline);
        hina_cmd_set_line_width(cmd, 1.0f);
        hina_cmd_bind_transient_group(cmd, 0, scene_group);
        hina_cmd_apply_vertex_input(cmd, &bindings);
        hina_cmd_draw_indexed(cmd, g_app.index_count, 1, 0, 0, 0);
    }

    // Present frame (renders ImGui in separate pass, submits, ends frame)
    hina_example_present_frame(app, cmd, swapchain);
}

// ============================================================================
// Cleanup
// ============================================================================

static void example_cleanup(hina_example_app* app) {
    (void)app;
    EXAMPLE_LOGI("Cleaning up Pipelines example...");

    if (hina_pipeline_is_valid(g_app.wireframe_pipeline))
        hina_destroy_pipeline(g_app.wireframe_pipeline);
    if (hina_pipeline_is_valid(g_app.toon_pipeline))
        hina_destroy_pipeline(g_app.toon_pipeline);
    if (hina_pipeline_is_valid(g_app.phong_pipeline))
        hina_destroy_pipeline(g_app.phong_pipeline);

    if (hina_bind_group_layout_is_valid(g_app.scene_layout))
        hina_destroy_bind_group_layout(g_app.scene_layout);

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

HINA_EXAMPLE_MAIN("HinaVK Pipelines", example_init, example_render, example_cleanup)
