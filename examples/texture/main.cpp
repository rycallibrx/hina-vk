/**
 * HinaVK Texture Loading Example - Based on Sascha Willems Vulkan Example
 *
 * This example demonstrates:
 * - Loading a KTX texture file with pre-computed mipmaps
 * - Creating a Vulkan texture with mipmaps
 * - Texture sampling with anisotropic filtering
 * - Displaying a textured quad
 *
 * Based on https://github.com/SaschaWillems/Vulkan/blob/master/examples/texture/texture.cpp
 *
 * Key HinaVK features shown:
 * - hina_ktx_load() for KTX texture loading with mipmaps
 * - hina_texture_bindless_index() for bindless texture access
 * - hina_make_sampler() with anisotropic filtering
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
    glm::vec2 uv;
    glm::vec3 normal;
};

// Uniform buffer structure (must match shader)
struct UBO {
    glm::mat4 projection;
    glm::mat4 model;
    glm::mat4 view;
    glm::vec4 view_pos;
    uint32_t texture_id;
    uint32_t sampler_id;
    uint32_t _pad[2];
};

// ============================================================================
// Quad Mesh Generation
// ============================================================================

struct Mesh {
    std::vector<Vertex> vertices;
    std::vector<uint32_t> indices;
};

static Mesh generate_quad() {
    Mesh mesh;

    // Generate a simple quad in the XY plane
    // Front-facing (normal towards +Z)
    mesh.vertices = {
        // Position                 UV             Normal
        {{ -1.0f, -1.0f, 0.0f }, { 0.0f, 1.0f }, { 0.0f, 0.0f, 1.0f }},  // Bottom-left
        {{  1.0f, -1.0f, 0.0f }, { 1.0f, 1.0f }, { 0.0f, 0.0f, 1.0f }},  // Bottom-right
        {{  1.0f,  1.0f, 0.0f }, { 1.0f, 0.0f }, { 0.0f, 0.0f, 1.0f }},  // Top-right
        {{ -1.0f,  1.0f, 0.0f }, { 0.0f, 0.0f }, { 0.0f, 0.0f, 1.0f }},  // Top-left
    };

    // Two triangles (CCW winding)
    mesh.indices = {
        0, 1, 2,  // First triangle
        0, 2, 3   // Second triangle
    };

    return mesh;
}

// ============================================================================
// Debug Mipmap Texture - Each mip level is a distinct solid color
// ============================================================================

// Colors for each mip level (RGB)
// Mip 0: Red, Mip 1: Orange, Mip 2: Yellow, Mip 3: Green,
// Mip 4: Cyan, Mip 5: Blue, Mip 6: Purple, Mip 7: Magenta, Mip 8: White
static const uint8_t MIP_COLORS[][3] = {
    {255,   0,   0},  // Mip 0: Red
    {255, 128,   0},  // Mip 1: Orange
    {255, 255,   0},  // Mip 2: Yellow
    {  0, 255,   0},  // Mip 3: Green
    {  0, 255, 255},  // Mip 4: Cyan
    {  0,   0, 255},  // Mip 5: Blue
    {128,   0, 255},  // Mip 6: Purple
    {255,   0, 255},  // Mip 7: Magenta
    {255, 255, 255},  // Mip 8: White
    {128, 128, 128},  // Mip 9+: Gray
};

struct MipLevelData {
    std::vector<uint8_t> pixels;
    uint32_t width;
    uint32_t height;
};

// Generate texture data for a single mip level with a solid color
static MipLevelData generate_mip_level_data(uint32_t width, uint32_t height, uint32_t mip_level) {
    MipLevelData data;
    data.width = width;
    data.height = height;
    data.pixels.resize(width * height * 4);

    uint32_t color_index = std::min(mip_level, (uint32_t)9);
    uint8_t r = MIP_COLORS[color_index][0];
    uint8_t g = MIP_COLORS[color_index][1];
    uint8_t b = MIP_COLORS[color_index][2];

    for (uint32_t i = 0; i < width * height; i++) {
        data.pixels[i * 4 + 0] = r;
        data.pixels[i * 4 + 1] = g;
        data.pixels[i * 4 + 2] = b;
        data.pixels[i * 4 + 3] = 255;
    }

    return data;
}

// Create a debug texture where each mip level is a distinct solid color
// This allows visual verification that the GPU is actually sampling from different mip levels
static hina_texture create_debug_mipmap_texture(uint32_t base_size, uint32_t* out_mip_levels) {
    uint32_t mip_levels = static_cast<uint32_t>(std::floor(std::log2(base_size))) + 1;
    *out_mip_levels = mip_levels;

    EXAMPLE_LOGI("Creating debug mipmap texture: %ux%u, %u mip levels", base_size, base_size, mip_levels);
    EXAMPLE_LOGI("  Mip colors: Red(0) -> Orange(1) -> Yellow(2) -> Green(3) -> Cyan(4) -> Blue(5) -> Purple(6) -> Magenta(7) -> White(8)");

    // Create texture without initial data (we'll upload each mip separately)
    hina_texture_desc tex_desc = hina_texture_desc_default();
    tex_desc.format = HINA_FORMAT_R8G8B8A8_UNORM;
    tex_desc.width = base_size;
    tex_desc.height = base_size;
    tex_desc.mip_levels = static_cast<uint16_t>(mip_levels);
    tex_desc.usage = static_cast<hina_texture_usage_flags>(HINA_TEXTURE_SAMPLED_BIT);

    hina_texture texture = hina_make_texture(&tex_desc);
    if (!hina_texture_is_valid(texture)) {
        EXAMPLE_LOGE("Failed to create debug mipmap texture");
        return texture;
    }

    // Calculate total staging buffer size needed for all mip levels
    size_t total_size = 0;
    uint32_t w = base_size, h = base_size;
    for (uint32_t mip = 0; mip < mip_levels; mip++) {
        total_size += w * h * 4;
        w = std::max(1u, w / 2);
        h = std::max(1u, h / 2);
    }

    // Build all mip data in CPU memory first
    std::vector<uint8_t> all_mip_data(total_size);
    size_t offset = 0;
    w = base_size;
    h = base_size;
    for (uint32_t mip = 0; mip < mip_levels; mip++) {
        MipLevelData mip_data = generate_mip_level_data(w, h, mip);
        memcpy(all_mip_data.data() + offset, mip_data.pixels.data(), mip_data.pixels.size());
        EXAMPLE_LOGI("  Mip %u: %ux%u, color=(%u,%u,%u), offset=%zu",
                  mip, w, h, MIP_COLORS[std::min(mip, 9u)][0],
                  MIP_COLORS[std::min(mip, 9u)][1], MIP_COLORS[std::min(mip, 9u)][2], offset);
        offset += mip_data.pixels.size();
        w = std::max(1u, w / 2);
        h = std::max(1u, h / 2);
    }

    // Create staging buffer with initial_data (no manual mapping needed)
    hina_buffer_desc staging_desc = {0};
    staging_desc.size = total_size;
    staging_desc.flags = static_cast<hina_buffer_flags>(
        HINA_BUFFER_TRANSFER_SRC_BIT | HINA_BUFFER_HOST_VISIBLE_BIT | HINA_BUFFER_HOST_COHERENT_BIT);
    staging_desc.initial_data = all_mip_data.data();

    hina_buffer staging_buffer = hina_make_buffer(&staging_desc);
    if (!hina_buffer_is_valid(staging_buffer)) {
        EXAMPLE_LOGE("Failed to create staging buffer for debug texture");
        hina_destroy_texture(texture);
        return hina_texture{};
    }

    // Upload each mip level
    hina_cmd* cmd = hina_cmd_begin_ex(HINA_QUEUE_GRAPHICS);
    if (cmd) {
        offset = 0;
        w = base_size;
        h = base_size;
        for (uint32_t mip = 0; mip < mip_levels; mip++) {
            hina_cmd_copy_buffer_to_texture(cmd, staging_buffer, texture, offset, mip, 0);
            offset += w * h * 4;
            w = std::max(1u, w / 2);
            h = std::max(1u, h / 2);
        }
        hina_cmd_transition_texture(cmd, texture, HINA_TEXSTATE_SHADER_READ);
        hina_ticket ticket = hina_submit_immediate(cmd);
        hina_wait_ticket(ticket);
    }

    hina_destroy_buffer(staging_buffer);

    EXAMPLE_LOGI("Debug mipmap texture created successfully");
    return texture;
}

// ============================================================================
// Application State
// ============================================================================

struct TextureApp {
    // Mesh data
    Mesh mesh;

    // Vulkan resources
    hina_buffer vbo;
    hina_buffer ibo;
    hina_buffer ubo_buffer;
    UBO* ubo;
    hina_depth_buffer depth;
    hina_texture texture;
    uint32_t mip_levels;
    hina_sampler sampler;
    hina_pipeline pipeline;
    hina_bind_group_layout transforms_layout;
    hina_bind_group_layout texture_layout;

    // Camera
    hina_camera camera;
};

static TextureApp g_app = {};

// ============================================================================
// Initialization
// ============================================================================

static bool example_init(hina_example_app* app) {
    EXAMPLE_LOGI("Initializing Texture example...");

    // Generate Quad Mesh
    g_app.mesh = generate_quad();
    EXAMPLE_LOGI("Generated quad: %zu vertices, %zu indices",
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

    // Create Debug Mipmap Texture
    // Create debug texture where each mip level is a distinct solid color
    // This allows visual verification that the GPU is actually sampling from different mip levels
    g_app.texture = create_debug_mipmap_texture(512, &g_app.mip_levels);
    if (!hina_texture_is_valid(g_app.texture)) {
        EXAMPLE_LOGE("Failed to create debug mipmap texture");
        return false;
    }

    // Create Sampler (with anisotropic filtering)
    hina_sampler_desc sampler_desc = hina_sampler_desc_default();
    sampler_desc.min_filter = HINA_FILTER_LINEAR;
    sampler_desc.mag_filter = HINA_FILTER_LINEAR;
    sampler_desc.mipmap_filter = HINA_FILTER_LINEAR;
    sampler_desc.address_u = HINA_ADDRESS_REPEAT;
    sampler_desc.address_v = HINA_ADDRESS_REPEAT;
    sampler_desc.address_w = HINA_ADDRESS_REPEAT;
    sampler_desc.max_lod = static_cast<float>(g_app.mip_levels);
    sampler_desc.flags = HINA_SAMPLER_ANISOTROPY_ENABLE_BIT;
    sampler_desc.max_anisotropy = 8.0f;

    g_app.sampler = hina_make_sampler(&sampler_desc);
    if (!hina_sampler_is_valid(g_app.sampler)) {
        EXAMPLE_LOGE("Failed to create sampler");
        return false;
    }

    // Create Uniform Buffer
    hina_buffer_desc ubo_desc = {0};
    ubo_desc.size = sizeof(UBO);
    ubo_desc.flags = static_cast<hina_buffer_flags>(
        HINA_BUFFER_UNIFORM_BIT | HINA_BUFFER_HOST_VISIBLE_BIT | HINA_BUFFER_HOST_COHERENT_BIT);

    g_app.ubo_buffer = hina_make_buffer(&ubo_desc);
    if (!hina_buffer_is_valid(g_app.ubo_buffer)) {
        EXAMPLE_LOGE("Failed to create uniform buffer");
        return false;
    }

    // Map UBO for persistent writes
    g_app.ubo = static_cast<UBO*>(hina_map_buffer(g_app.ubo_buffer));
    if (!g_app.ubo) {
        EXAMPLE_LOGE("Failed to map uniform buffer");
        return false;
    }

    // Create Depth Buffer
    if (!hina_depth_buffer_init(&g_app.depth, app->width, app->height)) {
        EXAMPLE_LOGE("Failed to create depth buffer");
        return false;
    }

    // Create Graphics Pipeline
    char* shader_path = hina_example_shader_path(app, "texture.hina_sl");
    char* error = nullptr;

    char* source = hina_example_load_file(app, shader_path, nullptr);
    free(shader_path);

    if (!source) {
        EXAMPLE_LOGE("Failed to load shader file");
        return false;
    }

    hina_hsl_module* module = hslc_compile_hsl_source(source, "texture.hina_sl", &error);
    free(source);

    if (!module) {
        EXAMPLE_LOGE("Shader compilation failed: %s", error ? error : "Unknown");
        if (error) hslc_free_log(error);
        return false;
    }

    hina_vertex_layout vertex_layout = {};
    vertex_layout.buffer_count = 1;
    vertex_layout.buffer_strides[0] = sizeof(Vertex);
    vertex_layout.input_rates[0] = HINA_VERTEX_INPUT_RATE_VERTEX;
    vertex_layout.attr_count = 3;
    vertex_layout.attrs[0] = { HINA_FORMAT_R32G32B32_SFLOAT, offsetof(Vertex, position), 0, 0 };
    vertex_layout.attrs[1] = { HINA_FORMAT_R32G32_SFLOAT, offsetof(Vertex, uv), 1, 0 };
    vertex_layout.attrs[2] = { HINA_FORMAT_R32G32B32_SFLOAT, offsetof(Vertex, normal), 2, 0 };

    hina_hsl_pipeline_desc pip_desc = hina_hsl_pipeline_desc_default();
    pip_desc.layout = vertex_layout;
    pip_desc.cull_mode = HINA_CULL_MODE_NONE;  // Show both sides
    pip_desc.depth_format = HINA_FORMAT_D32_SFLOAT;

    g_app.pipeline = hina_make_pipeline_from_module(module, &pip_desc, NULL);
    hslc_hsl_module_free(module);

    if (!hina_pipeline_is_valid(g_app.pipeline)) {
        EXAMPLE_LOGE("Pipeline creation failed");
        return false;
    }

    // Get bind group layout for set 0 (transforms) from the pipeline
    g_app.transforms_layout = hina_pipeline_get_bind_group_layout(g_app.pipeline, 0);
    if (!hina_bind_group_layout_is_valid(g_app.transforms_layout)) {
        EXAMPLE_LOGE("Failed to get transforms bind group layout");
        return false;
    }

    // Get bind group layout for set 1 (texture) from the pipeline
    g_app.texture_layout = hina_pipeline_get_bind_group_layout(g_app.pipeline, 1);
    if (!hina_bind_group_layout_is_valid(g_app.texture_layout)) {
        EXAMPLE_LOGE("Failed to get texture bind group layout");
        return false;
    }

    EXAMPLE_LOGI("Pipeline created successfully");

    // Initialize Camera
    g_app.camera.rotation = glm::vec3(0.0f, 0.0f, 0.0f);
    g_app.camera.zoom = -2.5f;

    return true;
}

// ============================================================================
// Rendering
// ============================================================================

static void example_render(hina_example_app* app) {
    // Update camera from input
    g_app.camera.update(*app);

    // Begin frame (acquires swapchain image)
    hina_swapchain_image swapchain = hina_frame_begin();
    if (swapchain.texture.id == HINA_INVALID_HANDLE) {
        hina_example_try_recover_surface(app);
        hina_frame_end();
        return;
    }

    // Get actual swapchain dimensions
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
    g_app.ubo->view = g_app.camera.view_matrix();
    g_app.ubo->model = glm::mat4(1.0f);
    g_app.ubo->view_pos = glm::vec4(0.0f, 0.0f, g_app.camera.zoom, 1.0f);
    g_app.ubo->texture_id = 0;
    g_app.ubo->sampler_id = 0;

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

    // Create temporary bind group for texture (set 1)
    hina_texture_view tex_view = hina_texture_get_default_view(g_app.texture);

    hina_bind_group_entry tex_entry = {};
    tex_entry.binding = 0;
    tex_entry.type = HINA_DESC_TYPE_COMBINED_IMAGE_SAMPLER;
    tex_entry.combined.view = tex_view;
    tex_entry.combined.sampler = g_app.sampler;

    hina_bind_group_desc tex_group_desc = {};
    tex_group_desc.layout = g_app.texture_layout;
    tex_group_desc.entries = &tex_entry;
    tex_group_desc.entry_count = 1;
    tex_group_desc.label = "texture";

    hina_transient_bind_group tex_bind_group = hina_example_make_transient_bind_group(&tex_group_desc);

    hina_bind_group_entry transforms_entry = {};
    transforms_entry.binding = 0;
    transforms_entry.type = HINA_DESC_TYPE_UNIFORM_BUFFER;
    transforms_entry.buffer.buffer = g_app.ubo_buffer;
    transforms_entry.buffer.offset = 0;
    transforms_entry.buffer.size = sizeof(UBO);

    hina_bind_group_desc transforms_group_desc = {};
    transforms_group_desc.layout = g_app.transforms_layout;
    transforms_group_desc.entries = &transforms_entry;
    transforms_group_desc.entry_count = 1;
    transforms_group_desc.label = "transforms";

    hina_transient_bind_group transforms_group = hina_example_make_transient_bind_group(&transforms_group_desc);

    hina_cmd_begin_pass(cmd, &pass);
    hina_cmd_bind_pipeline(cmd, g_app.pipeline);
    hina_cmd_bind_transient_group(cmd, 0, transforms_group);
    hina_cmd_bind_transient_group(cmd, 1, tex_bind_group);  // Bind texture to set 1

    hina_vertex_input bindings = {};
    bindings.vertex_buffers[0] = g_app.vbo;
    bindings.vertex_offsets[0] = 0;
    bindings.index_buffer = g_app.ibo;
    bindings.index_type = HINA_INDEX_UINT32;
    hina_cmd_apply_vertex_input(cmd, &bindings);

    hina_cmd_draw_indexed(cmd, static_cast<uint32_t>(g_app.mesh.indices.size()), 1, 0, 0, 0);

    // Present frame (ends pass, renders ImGui, submits, ends frame)
    hina_example_present_frame(app, cmd, swapchain);
}

// ============================================================================
// Cleanup
// ============================================================================

static void example_cleanup(hina_example_app* app) {
    (void)app;
    EXAMPLE_LOGI("Cleaning up Texture example...");

    if (hina_pipeline_is_valid(g_app.pipeline))
        hina_destroy_pipeline(g_app.pipeline);

    hina_depth_buffer_destroy(&g_app.depth);

    if (hina_sampler_is_valid(g_app.sampler))
        hina_destroy_sampler(g_app.sampler);
    if (hina_texture_is_valid(g_app.texture))
        hina_destroy_texture(g_app.texture);

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

HINA_EXAMPLE_MAIN("HinaVK Texture Loading", example_init, example_render, example_cleanup)
