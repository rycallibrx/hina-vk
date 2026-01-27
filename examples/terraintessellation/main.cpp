/**
 * HinaVK Terrain Tessellation Example - Tessellation Shader Test
 *
 * Based on https://github.com/SaschaWillems/Vulkan/blob/master/examples/terraintessellation
 *
 * This example demonstrates:
 * - Tessellation control shader (TCS) for dynamic LOD
 * - Tessellation evaluation shader (TES) for vertex generation
 * - Heightmap displacement
 * - Screen-space adaptive tessellation
 * - Frustum culling in TCS
 * - Quad patch topology (PATCH_LIST with 4 control points)
 *
 * The terrain is rendered as quad patches that get tessellated based on
 * their screen-space size, creating smooth LOD transitions.
 */

#include <cmath>
#include <vector>

#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>

#include "../hina_example.h"

// ============================================================================
// Configuration
// ============================================================================

constexpr int TERRAIN_GRID_SIZE = 8;      // Grid of patches
constexpr float TERRAIN_SCALE = 16.0f;    // World size of terrain
constexpr int HEIGHTMAP_SIZE = 256;       // Heightmap resolution

// ============================================================================
// Vertex Data Structure
// ============================================================================

struct Vertex {
    glm::vec3 position;
    glm::vec3 normal;
    glm::vec2 uv;
};

// UBO matching shader layout
struct SceneUBO {
    glm::mat4 projection;
    glm::mat4 modelview;
    glm::vec4 lightPos;
    glm::vec4 frustumPlanes[6];
    float displacementFactor;
    float tessellationFactor;
    glm::vec2 viewportDim;
    float tessellatedEdgeSize;
    float padding1;
    float padding2;
    float padding3;
};

// ============================================================================
// Terrain Grid Generation (Quad Patches)
// ============================================================================

struct TerrainMesh {
    std::vector<Vertex> vertices;
    std::vector<uint32_t> indices;
};

static TerrainMesh generate_terrain_grid(int grid_size, float scale) {
    TerrainMesh mesh;

    float half_scale = scale * 0.5f;
    float cell_size = scale / static_cast<float>(grid_size);

    // Generate vertices for quad patches
    // Each patch is defined by 4 corner vertices
    for (int z = 0; z <= grid_size; z++) {
        for (int x = 0; x <= grid_size; x++) {
            Vertex v;
            v.position.x = -half_scale + static_cast<float>(x) * cell_size;
            v.position.y = 0.0f;  // Height will be set by tessellation shader
            v.position.z = -half_scale + static_cast<float>(z) * cell_size;
            v.normal = glm::vec3(0.0f, 1.0f, 0.0f);
            v.uv.x = static_cast<float>(x) / static_cast<float>(grid_size);
            v.uv.y = static_cast<float>(z) / static_cast<float>(grid_size);
            mesh.vertices.push_back(v);
        }
    }

    // Generate indices for quad patches (4 vertices per patch)
    // Order: bottom-left, bottom-right, top-right, top-left
    for (int z = 0; z < grid_size; z++) {
        for (int x = 0; x < grid_size; x++) {
            uint32_t bl = static_cast<uint32_t>(z * (grid_size + 1) + x);
            uint32_t br = bl + 1;
            uint32_t tl = bl + (grid_size + 1);
            uint32_t tr = tl + 1;

            // Quad patch: 4 control points
            mesh.indices.push_back(bl);
            mesh.indices.push_back(br);
            mesh.indices.push_back(tr);
            mesh.indices.push_back(tl);
        }
    }

    return mesh;
}

// ============================================================================
// Procedural Heightmap Generation
// ============================================================================

static std::vector<uint8_t> generate_heightmap(int size) {
    std::vector<uint8_t> heightmap(size * size);

    // Generate multi-octave noise for terrain
    for (int y = 0; y < size; y++) {
        for (int x = 0; x < size; x++) {
            float fx = static_cast<float>(x) / static_cast<float>(size);
            float fy = static_cast<float>(y) / static_cast<float>(size);

            // Simple multi-frequency sine wave terrain
            float height = 0.0f;
            height += sinf(fx * 3.14159f * 2.0f) * 0.3f;
            height += sinf(fy * 3.14159f * 3.0f) * 0.25f;
            height += sinf((fx + fy) * 3.14159f * 4.0f) * 0.15f;
            height += sinf((fx * 2.0f - fy) * 3.14159f * 5.0f) * 0.1f;

            // Add some peaks
            float cx = fx - 0.5f;
            float cy = fy - 0.5f;
            float dist = sqrtf(cx * cx + cy * cy);
            height += expf(-dist * dist * 8.0f) * 0.4f;

            // Normalize to 0-1 range
            height = (height + 1.0f) * 0.5f;
            height = glm::clamp(height, 0.0f, 1.0f);

            heightmap[y * size + x] = static_cast<uint8_t>(height * 255.0f);
        }
    }

    return heightmap;
}

// ============================================================================
// Frustum Plane Extraction
// ============================================================================

static void extract_frustum_planes(const glm::mat4& mvp, glm::vec4 planes[6]) {
    // Left plane
    planes[0] = glm::vec4(
        mvp[0][3] + mvp[0][0],
        mvp[1][3] + mvp[1][0],
        mvp[2][3] + mvp[2][0],
        mvp[3][3] + mvp[3][0]
    );
    // Right plane
    planes[1] = glm::vec4(
        mvp[0][3] - mvp[0][0],
        mvp[1][3] - mvp[1][0],
        mvp[2][3] - mvp[2][0],
        mvp[3][3] - mvp[3][0]
    );
    // Bottom plane
    planes[2] = glm::vec4(
        mvp[0][3] + mvp[0][1],
        mvp[1][3] + mvp[1][1],
        mvp[2][3] + mvp[2][1],
        mvp[3][3] + mvp[3][1]
    );
    // Top plane
    planes[3] = glm::vec4(
        mvp[0][3] - mvp[0][1],
        mvp[1][3] - mvp[1][1],
        mvp[2][3] - mvp[2][1],
        mvp[3][3] - mvp[3][1]
    );
    // Near plane
    planes[4] = glm::vec4(
        mvp[0][3] + mvp[0][2],
        mvp[1][3] + mvp[1][2],
        mvp[2][3] + mvp[2][2],
        mvp[3][3] + mvp[3][2]
    );
    // Far plane
    planes[5] = glm::vec4(
        mvp[0][3] - mvp[0][2],
        mvp[1][3] - mvp[1][2],
        mvp[2][3] - mvp[2][2],
        mvp[3][3] - mvp[3][2]
    );

    // Normalize planes
    for (int i = 0; i < 6; i++) {
        float len = glm::length(glm::vec3(planes[i]));
        planes[i] /= len;
    }
}

// ============================================================================
// Application State
// ============================================================================

struct TerrainTessellationApp {
    // Mesh data
    TerrainMesh mesh;
    hina_buffer vbo;
    hina_buffer ibo;

    // Heightmap texture
    hina_texture heightmap;
    hina_texture_view heightmap_view;
    hina_sampler sampler;

    // Uniform buffer
    hina_buffer ubo_buffer;
    SceneUBO* ubo_mapped;

    // Bind groups
    hina_bind_group_layout scene_layout;
    hina_bind_group_layout material_layout;
    hina_bind_group scene_bind_group;
    hina_bind_group material_bind_group;

    // Pipeline
    hina_pipeline pipeline;

    // Depth buffer
    hina_depth_buffer depth;

    // Camera

    // Tessellation parameters
    float tessellation_factor;
    float displacement_factor;
    bool wireframe;
};

static TerrainTessellationApp g_app = {};

// ============================================================================
// Initialization
// ============================================================================

static bool example_init(hina_example_app* app) {
    EXAMPLE_LOGI("Initializing Terrain Tessellation example...");

    // Initialize parameters
    g_app.tessellation_factor = 1.0f;
    g_app.displacement_factor = 2.0f;
    g_app.wireframe = false;

    // Generate terrain mesh
    g_app.mesh = generate_terrain_grid(TERRAIN_GRID_SIZE, TERRAIN_SCALE);
    EXAMPLE_LOGI("Generated terrain: %zu vertices, %zu indices (%zu patches)",
              g_app.mesh.vertices.size(), g_app.mesh.indices.size(),
              g_app.mesh.indices.size() / 4);

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

    // Generate and create heightmap texture
    std::vector<uint8_t> heightmap_data = generate_heightmap(HEIGHTMAP_SIZE);

    hina_texture_desc heightmap_desc = hina_texture_desc_default();
    heightmap_desc.format = HINA_FORMAT_R8_UNORM;
    heightmap_desc.width = HEIGHTMAP_SIZE;
    heightmap_desc.height = HEIGHTMAP_SIZE;
    heightmap_desc.initial_data = heightmap_data.data();

    g_app.heightmap = hina_make_texture(&heightmap_desc);
    if (!hina_texture_is_valid(g_app.heightmap)) {
        EXAMPLE_LOGE("Failed to create heightmap texture");
        return false;
    }

    g_app.heightmap_view = hina_texture_get_default_view(g_app.heightmap);

    // Create sampler with bilinear filtering for smooth terrain
    hina_sampler_desc sampler_desc = hina_sampler_desc_default();
    sampler_desc.min_filter = HINA_FILTER_LINEAR;
    sampler_desc.mag_filter = HINA_FILTER_LINEAR;
    sampler_desc.address_u = HINA_ADDRESS_CLAMP_TO_EDGE;
    sampler_desc.address_v = HINA_ADDRESS_CLAMP_TO_EDGE;

    g_app.sampler = hina_make_sampler(&sampler_desc);
    if (!hina_sampler_is_valid(g_app.sampler)) {
        EXAMPLE_LOGE("Failed to create sampler");
        return false;
    }

    // Create Uniform Buffer
    hina_buffer_desc ubo_desc = {0};
    ubo_desc.size = sizeof(SceneUBO);
    ubo_desc.memory = HINA_BUFFER_CPU;
    ubo_desc.usage = HINA_BUFFER_UNIFORM;

    g_app.ubo_buffer = hina_make_buffer(&ubo_desc);
    if (!hina_buffer_is_valid(g_app.ubo_buffer)) {
        EXAMPLE_LOGE("Failed to create uniform buffer");
        return false;
    }

  g_app.ubo_mapped = static_cast<SceneUBO*>(hina_mapped_buffer_ptr(g_app.ubo_buffer));
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
    vertex_layout.attrs[2] = { HINA_FORMAT_R32G32_SFLOAT, offsetof(Vertex, uv), 2, 0 };

    // Create Graphics Pipeline with Tessellation Shaders
    char* shader_path = hina_example_shader_path(app, "terrain.hina_sl");
    char* error = nullptr;

    hina_hsl_pipeline_desc pip_desc = hina_hsl_pipeline_desc_default();
    pip_desc.front_face = HINA_FRONT_FACE_CLOCKWISE;
    pip_desc.cull_mode = HINA_CULL_MODE_BACK;
    pip_desc.layout = vertex_layout;
    pip_desc.color_formats[0] = hina_get_surface_format();
    pip_desc.depth_format = HINA_FORMAT_D32_SFLOAT;

    // CRITICAL: Set patch topology for tessellation
    pip_desc.primitive_topology = HINA_PRIMITIVE_TOPOLOGY_PATCH_LIST;
    pip_desc.patch_control_points = 4;  // Quad patches

    g_app.pipeline = hina_example_make_pipeline_from_hsl(app, shader_path, &pip_desc, &error);
    free(shader_path);

    if (!hina_pipeline_is_valid(g_app.pipeline)) {
        EXAMPLE_LOGE("Pipeline creation failed: %s", error ? error : "Unknown error");
        if (error) hslc_free_log(error);
        return false;
    }

    EXAMPLE_LOGI("Pipeline with tessellation shaders created successfully");

    // Get bind group layouts
    g_app.scene_layout = hina_pipeline_get_bind_group_layout(g_app.pipeline, 0);
    g_app.material_layout = hina_pipeline_get_bind_group_layout(g_app.pipeline, 1);

    if (!hina_bind_group_layout_is_valid(g_app.scene_layout) ||
        !hina_bind_group_layout_is_valid(g_app.material_layout)) {
        EXAMPLE_LOGE("Failed to get bind group layouts");
        return false;
    }

    // Create Scene bind group (UBO)
    hina_bind_group_entry scene_entry = {};
    scene_entry.binding = 0;
    scene_entry.type = HINA_DESC_TYPE_UNIFORM_BUFFER;
    scene_entry.buffer.buffer = g_app.ubo_buffer;
    scene_entry.buffer.offset = 0;
    scene_entry.buffer.size = sizeof(SceneUBO);

    hina_bind_group_desc scene_bg_desc = {};
    scene_bg_desc.layout = g_app.scene_layout;
    scene_bg_desc.entries = &scene_entry;
    scene_bg_desc.entry_count = 1;
    scene_bg_desc.label = "Scene UBO";

    g_app.scene_bind_group = hina_create_bind_group(&scene_bg_desc);
    if (!hina_bind_group_is_valid(g_app.scene_bind_group)) {
        EXAMPLE_LOGE("Failed to create scene bind group");
        return false;
    }

    // Create Material bind group (heightmap)
    hina_bind_group_entry material_entry = {};
    material_entry.binding = 0;
    material_entry.type = HINA_DESC_TYPE_COMBINED_IMAGE_SAMPLER;
    material_entry.combined.view = g_app.heightmap_view;
    material_entry.combined.sampler = g_app.sampler;

    hina_bind_group_desc material_bg_desc = {};
    material_bg_desc.layout = g_app.material_layout;
    material_bg_desc.entries = &material_entry;
    material_bg_desc.entry_count = 1;
    material_bg_desc.label = "Heightmap";

    g_app.material_bind_group = hina_create_bind_group(&material_bg_desc);
    if (!hina_bind_group_is_valid(g_app.material_bind_group)) {
        EXAMPLE_LOGE("Failed to create material bind group");
        return false;
    }

    // Initialize Camera - position above terrain looking down
    app->camera.rotation = glm::vec3(-45.0f, 0.0f, 0.0f);
    app->camera.zoom = -20.0f;

    EXAMPLE_LOGI("Terrain Tessellation example initialized");
    EXAMPLE_LOGI("Controls: Mouse to rotate, scroll to zoom");
    EXAMPLE_LOGI("Tessellation factor: %.1f, Displacement: %.1f",
              g_app.tessellation_factor, g_app.displacement_factor);

    return true;
}

// ============================================================================
// Rendering
// ============================================================================

static void example_render(hina_example_app* app) {
    // Update camera from input
    app->camera.update(*app);

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

    float aspect = static_cast<float>(w) / static_cast<float>(h);

    // Update UBO
    glm::mat4 projection = glm::perspective(glm::radians(60.0f), aspect, 0.1f, 512.0f);
    glm::mat4 view = app->camera.view_matrix();
    glm::mat4 model = glm::mat4(1.0f);
    glm::mat4 mvp = projection * view * model;

    g_app.ubo_mapped->projection = projection;
    g_app.ubo_mapped->modelview = view * model;
    g_app.ubo_mapped->lightPos = glm::vec4(0.0f, 20.0f, 10.0f, 1.0f);
    g_app.ubo_mapped->displacementFactor = g_app.displacement_factor;
    g_app.ubo_mapped->tessellationFactor = g_app.tessellation_factor;
    g_app.ubo_mapped->viewportDim = glm::vec2(static_cast<float>(w), static_cast<float>(h));
    g_app.ubo_mapped->tessellatedEdgeSize = 20.0f;

    // Extract frustum planes for culling
    extract_frustum_planes(mvp, g_app.ubo_mapped->frustumPlanes);

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
    pass.colors[0].clear_color[0] = 0.4f;
    pass.colors[0].clear_color[1] = 0.6f;
    pass.colors[0].clear_color[2] = 0.9f;  // Sky blue
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
    hina_cmd_bind_group(cmd, 1, g_app.material_bind_group);

    // Setup vertex/index bindings
    hina_vertex_input bindings = {};
    bindings.vertex_buffers[0] = g_app.vbo;
    bindings.vertex_offsets[0] = 0;
    bindings.index_buffer = g_app.ibo;
    bindings.index_type = HINA_INDEX_UINT32;

    hina_cmd_apply_vertex_input(cmd, &bindings);

    // Draw terrain patches
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
    EXAMPLE_LOGI("Cleaning up Terrain Tessellation example...");

    if (hina_bind_group_is_valid(g_app.material_bind_group))
        hina_destroy_bind_group(g_app.material_bind_group);
    if (hina_bind_group_is_valid(g_app.scene_bind_group))
        hina_destroy_bind_group(g_app.scene_bind_group);

    if (hina_pipeline_is_valid(g_app.pipeline))
        hina_destroy_pipeline(g_app.pipeline);

    hina_depth_buffer_destroy(&g_app.depth);

    if (hina_sampler_is_valid(g_app.sampler))
        hina_destroy_sampler(g_app.sampler);
    if (hina_texture_is_valid(g_app.heightmap))
        hina_destroy_texture(g_app.heightmap);

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

HINA_EXAMPLE_MAIN("HinaVK Terrain Tessellation", example_init, example_render, example_cleanup)
