/**
 * HinaVK Specialization Constants Example
 *
 * Based on https://github.com/SaschaWillems/Vulkan/blob/master/examples/specializationconstants/specializationconstants.cpp
 *
 * This example demonstrates:
 * - Using specialization constants to create shader variants from a single source
 * - Three different lighting models (Phong, Toon, Textured) selected at pipeline creation
 * - Rendering the same scene three times with different pipelines side by side
 *
 * Specialization constants allow shader constants to be set at pipeline creation time,
 * enabling efficient shader variants without recompilation. The compiler can optimize
 * the shader code based on these constants, potentially eliminating dead code paths.
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

constexpr float PI = 3.14159265359f;

// Lighting model IDs (match constant_id = 0 in shader)
constexpr int LIGHTING_PHONG = 0;
constexpr int LIGHTING_TOON = 1;
constexpr int LIGHTING_TEXTURED = 2;

// ============================================================================
// Vertex Structure
// ============================================================================

struct Vertex {
    glm::vec3 position;
    glm::vec3 normal;
    glm::vec3 color;
    glm::vec2 uv;
};

// ============================================================================
// Uniform Buffer Structure
// ============================================================================

struct ViewUBO {
    glm::mat4 projection;
    glm::mat4 model;
    glm::mat4 view;
    glm::vec4 light_pos;
};

// ============================================================================
// Sphere Mesh Generation (UV sphere with colors)
// ============================================================================

struct Mesh {
    std::vector<Vertex> vertices;
    std::vector<uint32_t> indices;
};

static Mesh generate_sphere(float radius, int segments, int rings, glm::vec3 color) {
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
            v.color = color;
            v.uv = glm::vec2(xSegment, ySegment);
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

// Generate a teapot-like shape (simplified with spheres)
static void add_mesh_to_scene(Mesh& scene, const Mesh& mesh, glm::vec3 offset) {
    uint32_t base_idx = static_cast<uint32_t>(scene.vertices.size());

    for (const auto& v : mesh.vertices) {
        Vertex nv = v;
        nv.position += offset;
        scene.vertices.push_back(nv);
    }

    for (uint32_t idx : mesh.indices) {
        scene.indices.push_back(base_idx + idx);
    }
}

static Mesh generate_scene() {
    Mesh scene;

    // Create a scene with multiple colored spheres arranged in a pattern
    // Similar to the teapot + spheres scene in the original example

    // Central large sphere (body of teapot-like shape)
    Mesh body = generate_sphere(0.8f, 32, 16, glm::vec3(0.8f, 0.2f, 0.2f));
    add_mesh_to_scene(scene, body, glm::vec3(0.0f, 0.0f, 0.0f));

    // Spout (elongated sphere)
    Mesh spout = generate_sphere(0.3f, 24, 12, glm::vec3(0.2f, 0.8f, 0.2f));
    add_mesh_to_scene(scene, spout, glm::vec3(1.0f, 0.0f, 0.0f));

    // Handle (another sphere)
    Mesh handle = generate_sphere(0.25f, 24, 12, glm::vec3(0.2f, 0.2f, 0.8f));
    add_mesh_to_scene(scene, handle, glm::vec3(-1.0f, 0.3f, 0.0f));

    // Lid
    Mesh lid = generate_sphere(0.2f, 24, 12, glm::vec3(0.8f, 0.8f, 0.2f));
    add_mesh_to_scene(scene, lid, glm::vec3(0.0f, 0.9f, 0.0f));

    // Decorative spheres around the scene
    glm::vec3 colors[] = {
        glm::vec3(0.9f, 0.3f, 0.9f),
        glm::vec3(0.3f, 0.9f, 0.9f),
        glm::vec3(0.9f, 0.6f, 0.3f),
        glm::vec3(0.3f, 0.6f, 0.9f),
    };

    for (int i = 0; i < 4; i++) {
        float angle = (float(i) / 4.0f) * 2.0f * PI;
        float x = std::cos(angle) * 2.0f;
        float z = std::sin(angle) * 2.0f;
        Mesh small = generate_sphere(0.2f, 16, 8, colors[i]);
        add_mesh_to_scene(scene, small, glm::vec3(x, -0.5f, z));
    }

    return scene;
}

// ============================================================================
// Application State
// ============================================================================

struct SpecializationConstantsApp {
    // Mesh data
    Mesh mesh;

    // Vulkan resources
    hina_buffer vbo;
    hina_buffer ibo;
    hina_buffer ubo_buffer;
    ViewUBO* ubo;
    hina_depth_buffer depth;
    hina_pipeline phong_pipeline;
    hina_pipeline toon_pipeline;
    hina_pipeline textured_pipeline;
    hina_bind_group_layout scene_layout;

    // Camera
    hina_camera camera;
    float rotation_timer;
};

static SpecializationConstantsApp g_app = {};

// ============================================================================
// Create Pipeline with Specialization Constants
// ============================================================================

static hina_pipeline create_specialized_pipeline(
    hina_example_app* app,
    const hina_vertex_layout& vertex_layout,
    int lighting_model,
    float toon_desaturation = 0.5f
) {
    // Set up specialization constants
    hina_specialization_constant fs_specs[2];

    // constant_id = 0: Lighting model (int)
    fs_specs[0].constant_id = 0;
    fs_specs[0].size = sizeof(int32_t);
    fs_specs[0].value.i32 = lighting_model;

    // constant_id = 1: Toon desaturation factor (float)
    fs_specs[1].constant_id = 1;
    fs_specs[1].size = sizeof(float);
    fs_specs[1].value.f32 = toon_desaturation;

    // Load and compile shader
    char* shader_path = hina_example_shader_path(app, "specializationconstants.hina_sl");
    char* error = nullptr;

    char* source = hina_example_load_file(app, shader_path, nullptr);
    free(shader_path);

    if (!source) {
        EXAMPLE_LOGE("Failed to load shader file for lighting_model=%d", lighting_model);
        return hina_pipeline{};
    }

    hina_hsl_module* module = hslc_compile_hsl_source(source, "specializationconstants.hina_sl", &error);
    free(source);

    if (!module) {
        EXAMPLE_LOGE("Shader compilation failed: %s", error ? error : "Unknown");
        if (error) hslc_free_log(error);
        return hina_pipeline{};
    }

    hina_hsl_pipeline_desc pip_desc = hina_hsl_pipeline_desc_default();
    pip_desc.layout = vertex_layout;
    pip_desc.color_formats[0] = hina_get_surface_format();
    pip_desc.depth_format = HINA_FORMAT_D32_SFLOAT;

    // Set specialization constants for fragment shader
    pip_desc.fs_specializations = fs_specs;
    pip_desc.fs_specialization_count = 2;

    hina_pipeline pipeline = hina_make_pipeline_from_module(module, &pip_desc, NULL);
    hslc_hsl_module_free(module);

    if (!hina_pipeline_is_valid(pipeline)) {
        EXAMPLE_LOGE("Pipeline creation failed (lighting_model=%d)", lighting_model);
    }

    return pipeline;
}

// ============================================================================
// Initialization
// ============================================================================

static bool example_init(hina_example_app* app) {
    EXAMPLE_LOGI("Initializing Specialization Constants example...");

    // Generate Scene Mesh
    g_app.mesh = generate_scene();
    EXAMPLE_LOGI("Generated scene: %zu vertices, %zu indices",
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

    // Create View UBO
    hina_buffer_desc ubo_desc = {0};
    ubo_desc.size = sizeof(ViewUBO);
    ubo_desc.memory = HINA_BUFFER_CPU;
    ubo_desc.usage = HINA_BUFFER_UNIFORM;

    g_app.ubo_buffer = hina_make_buffer(&ubo_desc);
    if (!hina_buffer_is_valid(g_app.ubo_buffer)) {
        EXAMPLE_LOGE("Failed to create UBO");
        return false;
    }

  g_app.ubo = static_cast<ViewUBO*>(hina_mapped_buffer_ptr(g_app.ubo_buffer));
    if (!g_app.ubo) {
        EXAMPLE_LOGE("Failed to map UBO");
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
    vertex_layout.attr_count = 4;
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
    vertex_layout.attrs[3] = {
        HINA_FORMAT_R32G32_SFLOAT,
        static_cast<uint16_t>(offsetof(Vertex, uv)), 3, 0
    };

    // Create Three Pipelines with Different Specialization Constants
    EXAMPLE_LOGI("Creating Phong pipeline (lighting_model=0)...");
    g_app.phong_pipeline = create_specialized_pipeline(app, vertex_layout, LIGHTING_PHONG);
    if (!hina_pipeline_is_valid(g_app.phong_pipeline)) {
        return false;
    }

    EXAMPLE_LOGI("Creating Toon pipeline (lighting_model=1, desaturation=0.5)...");
    g_app.toon_pipeline = create_specialized_pipeline(app, vertex_layout, LIGHTING_TOON, 0.5f);
    if (!hina_pipeline_is_valid(g_app.toon_pipeline)) {
        return false;
    }

    EXAMPLE_LOGI("Creating Textured pipeline (lighting_model=2)...");
    g_app.textured_pipeline = create_specialized_pipeline(app, vertex_layout, LIGHTING_TEXTURED);
    if (!hina_pipeline_is_valid(g_app.textured_pipeline)) {
        return false;
    }

    EXAMPLE_LOGI("All pipelines created successfully");

    g_app.scene_layout = hina_pipeline_get_bind_group_layout(g_app.phong_pipeline, 0);
    if (!hina_bind_group_layout_is_valid(g_app.scene_layout)) {
        EXAMPLE_LOGE("Failed to get scene bind group layout");
        return false;
    }

    // Initialize Camera
    g_app.camera.rotation = glm::vec3(-20.0f, 0.0f, 0.0f);
    g_app.camera.zoom = -6.0f;

    g_app.rotation_timer = 0.0f;

    EXAMPLE_LOGI("Rendering scene with 3 different lighting models:");
    EXAMPLE_LOGI("  Left:   Phong shading");
    EXAMPLE_LOGI("  Center: Toon shading (50%% desaturation)");
    EXAMPLE_LOGI("  Right:  Textured shading");

    return true;
}

// ============================================================================
// Rendering
// ============================================================================

static void example_render(hina_example_app* app) {
    g_app.rotation_timer += app->delta_time * 45.0f;  // 45 degrees per second

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

    // Resize depth buffer if needed
    if (!hina_depth_buffer_resize(&g_app.depth, w, h)) {
        EXAMPLE_LOGE("Failed to resize depth buffer");
        hina_frame_end();
        return;
    }

    float aspect = static_cast<float>(w) / static_cast<float>(h);
    uint32_t third_width = w / 3;

    // Update UBO (shared between all pipelines)
    g_app.ubo->projection = glm::perspective(glm::radians(60.0f), aspect / 3.0f, 0.1f, 256.0f);
    g_app.ubo->view = g_app.camera.view_matrix();
    g_app.ubo->model = glm::rotate(glm::mat4(1.0f), glm::radians(g_app.rotation_timer), glm::vec3(0.0f, 1.0f, 0.0f));
    g_app.ubo->light_pos = glm::vec4(5.0f, 5.0f, 5.0f, 1.0f);

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
    pass.colors[0].clear_color[0] = 0.1f;
    pass.colors[0].clear_color[1] = 0.1f;
    pass.colors[0].clear_color[2] = 0.15f;
    pass.colors[0].clear_color[3] = 1.0f;
    pass.depth.image = hina_texture_get_default_view(g_app.depth.texture);
    pass.depth.load_op = HINA_LOAD_OP_CLEAR;
    pass.depth.store_op = HINA_STORE_OP_DONT_CARE;
    pass.depth.depth_clear = 1.0f;

    hina_cmd_begin_pass(cmd, &pass);

    hina_bind_group_entry scene_entry = {};
    scene_entry.binding = 0;
    scene_entry.type = HINA_DESC_TYPE_UNIFORM_BUFFER;
    scene_entry.buffer.buffer = g_app.ubo_buffer;
    scene_entry.buffer.offset = 0;
    scene_entry.buffer.size = sizeof(ViewUBO);

    hina_bind_group_desc scene_group_desc = {};
    scene_group_desc.layout = g_app.scene_layout;
    scene_group_desc.entries = &scene_entry;
    scene_group_desc.entry_count = 1;
    scene_group_desc.label = "scene";

    hina_transient_bind_group scene_group = hina_example_make_transient_bind_group(&scene_group_desc);

    // Setup vertex/index bindings (same for all draws)
    hina_vertex_input bindings = {};
    bindings.vertex_buffers[0] = g_app.vbo;
    bindings.vertex_offsets[0] = 0;
    bindings.index_buffer = g_app.ibo;
    bindings.index_type = HINA_INDEX_UINT32;

    uint32_t index_count = static_cast<uint32_t>(g_app.mesh.indices.size());

    // Pipeline array for three columns
    hina_pipeline pipelines[3] = {g_app.phong_pipeline, g_app.toon_pipeline, g_app.textured_pipeline};

    // Draw scene three times with different pipelines and viewports
    for (int i = 0; i < 3; i++) {
        float vp_x = static_cast<float>(i * third_width);
        float vp_width = (i == 2) ? static_cast<float>(w - third_width * 2) : static_cast<float>(third_width);

        hina_viewport viewport = {
            vp_x, 0.0f, vp_width, static_cast<float>(h), 0.0f, 1.0f
        };
        hina_scissor scissor = {
            static_cast<int32_t>(vp_x), 0, static_cast<uint32_t>(vp_width), h
        };

        hina_cmd_set_viewport(cmd, &viewport);
        hina_cmd_set_scissor(cmd, &scissor);

        hina_cmd_bind_pipeline(cmd, pipelines[i]);
    hina_cmd_bind_transient_group(cmd, 0, scene_group);
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
    EXAMPLE_LOGI("Cleaning up Specialization Constants example...");

    if (hina_pipeline_is_valid(g_app.textured_pipeline))
        hina_destroy_pipeline(g_app.textured_pipeline);
    if (hina_pipeline_is_valid(g_app.toon_pipeline))
        hina_destroy_pipeline(g_app.toon_pipeline);
    if (hina_pipeline_is_valid(g_app.phong_pipeline))
        hina_destroy_pipeline(g_app.phong_pipeline);

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

HINA_EXAMPLE_MAIN("HinaVK Specialization Constants", example_init, example_render, example_cleanup)
