/**
 * HinaVK Deferred Rendering Example - Cross-Platform (Tile Pass)
 *
 * This example demonstrates deferred shading using the hina tile pass system
 * with input attachments (subpassInput/tile_load) for efficient G-buffer reads.
 * Based on Sascha Willems' subpasses example.
 *
 * Tile Pass with 2 subpasses:
 *   Subpass 0 (G-Buffer): Render opaque scene geometry to position/normal/albedo MRT
 *   Subpass 1 (Composition + Transparent):
 *     - Composition: Full-screen triangle reading G-buffer via tile inputs
 *     - Transparent: Forward render glass geometry with alpha blending
 *
 * Key benefits of tile inputs over texture sampling:
 *   - No intermediate texture fetches on tile-based GPUs (mobile)
 *   - Automatic synchronization between subpasses
 *   - Reduced memory bandwidth
 *
 * Works on both Desktop (SDL) and Android (NativeActivity).
 */

#include <cmath>
#include <cstdlib>
#include <vector>

#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>

#include "../hina_example.h"

// ============================================================================
// Configuration
// ============================================================================

constexpr int NUM_LIGHTS = 32;

// ============================================================================
// Vertex Data Structures
// ============================================================================

struct Vertex {
    glm::vec3 position;
    glm::vec3 normal;
    glm::vec3 color;
};

struct TransparentVertex {
    glm::vec3 position;
    glm::vec3 normal;
    glm::vec4 color;  // RGBA with alpha
    glm::vec2 uv;
};

// G-Buffer fill uniform buffer
struct GBufferUBO {
    glm::mat4 projection;
    glm::mat4 view;
};

// Push constants for model matrix and optional color override
struct ModelPushConstants {
    glm::mat4 model;
    glm::vec4 color_override;  // w > 0 means use this color (emissive multiplier)
};

// Light structure (matches GLSL std430)
struct Light {
    glm::vec4 position;  // xyz = position, w = unused
    glm::vec4 color;     // xyz = color, w = radius
};

// Composition uniform buffer (must match GLSL layout)
struct CompositionUBO {
    glm::vec4 view_pos;
    uint32_t light_count;
    uint32_t _pad[3];  // Padding to align with GLSL std140
};

// Transparent pass UBO
struct TransparentUBO {
    glm::mat4 projection;
    glm::mat4 view;
    glm::mat4 model;
    uint32_t depth_tex_id;
    float near_plane;
    float far_plane;
    uint32_t _pad;
};

// ============================================================================
// Mesh Generation
// ============================================================================

struct Mesh {
    std::vector<Vertex> vertices;
    std::vector<uint32_t> indices;
};

struct TransparentMesh {
    std::vector<TransparentVertex> vertices;
    std::vector<uint32_t> indices;
};

// Generate a colored cube
static Mesh generate_cube(glm::vec3 color, glm::vec3 offset = glm::vec3(0.0f), float scale = 1.0f) {
    Mesh mesh;

    glm::vec3 corners[8] = {
        {-0.5f, -0.5f, -0.5f},
        { 0.5f, -0.5f, -0.5f},
        { 0.5f,  0.5f, -0.5f},
        {-0.5f,  0.5f, -0.5f},
        {-0.5f, -0.5f,  0.5f},
        { 0.5f, -0.5f,  0.5f},
        { 0.5f,  0.5f,  0.5f},
        {-0.5f,  0.5f,  0.5f},
    };

    for (auto& c : corners) {
        c = c * scale + offset;
    }

    struct Face {
        glm::vec3 normal;
        int v0, v1, v2, v3;
    };

    Face faces[6] = {
        {{0.0f, 0.0f, 1.0f}, 4, 5, 6, 7},   // Front
        {{0.0f, 0.0f, -1.0f}, 1, 0, 3, 2},  // Back
        {{1.0f, 0.0f, 0.0f}, 5, 1, 2, 6},   // Right
        {{-1.0f, 0.0f, 0.0f}, 0, 4, 7, 3},  // Left
        {{0.0f, 1.0f, 0.0f}, 7, 6, 2, 3},   // Top
        {{0.0f, -1.0f, 0.0f}, 0, 1, 5, 4},  // Bottom
    };

    for (const auto& face : faces) {
        uint32_t base = static_cast<uint32_t>(mesh.vertices.size());

        mesh.vertices.push_back({corners[face.v0], face.normal, color});
        mesh.vertices.push_back({corners[face.v1], face.normal, color});
        mesh.vertices.push_back({corners[face.v2], face.normal, color});
        mesh.vertices.push_back({corners[face.v3], face.normal, color});

        mesh.indices.push_back(base + 0);
        mesh.indices.push_back(base + 1);
        mesh.indices.push_back(base + 2);
        mesh.indices.push_back(base + 0);
        mesh.indices.push_back(base + 2);
        mesh.indices.push_back(base + 3);
    }

    return mesh;
}

// Generate floor plane
static Mesh generate_floor(float size, glm::vec3 color) {
    Mesh mesh;

    float half = size / 2.0f;
    glm::vec3 normal(0.0f, 1.0f, 0.0f);

    mesh.vertices.push_back({{-half, 0.0f, -half}, normal, color});
    mesh.vertices.push_back({{ half, 0.0f, -half}, normal, color});
    mesh.vertices.push_back({{ half, 0.0f,  half}, normal, color});
    mesh.vertices.push_back({{-half, 0.0f,  half}, normal, color});

    mesh.indices = {0, 2, 1, 0, 3, 2};  // CW winding (becomes CCW after projection Y flip)

    return mesh;
}

// Generate transparent glass panel
static TransparentMesh generate_glass_panel(glm::vec3 center, glm::vec2 size, glm::vec4 color) {
    TransparentMesh mesh;

    float hw = size.x / 2.0f;
    float hh = size.y / 2.0f;
    glm::vec3 normal(0.0f, 0.0f, 1.0f);

    // Front face
    mesh.vertices.push_back({{center.x - hw, center.y - hh, center.z}, normal, color, {0.0f, 0.0f}});
    mesh.vertices.push_back({{center.x + hw, center.y - hh, center.z}, normal, color, {1.0f, 0.0f}});
    mesh.vertices.push_back({{center.x + hw, center.y + hh, center.z}, normal, color, {1.0f, 1.0f}});
    mesh.vertices.push_back({{center.x - hw, center.y + hh, center.z}, normal, color, {0.0f, 1.0f}});

    mesh.indices = {0, 1, 2, 0, 2, 3};

    // Back face (flipped normal and winding)
    glm::vec3 back_normal(0.0f, 0.0f, -1.0f);
    mesh.vertices.push_back({{center.x - hw, center.y - hh, center.z}, back_normal, color, {0.0f, 0.0f}});
    mesh.vertices.push_back({{center.x + hw, center.y - hh, center.z}, back_normal, color, {1.0f, 0.0f}});
    mesh.vertices.push_back({{center.x + hw, center.y + hh, center.z}, back_normal, color, {1.0f, 1.0f}});
    mesh.vertices.push_back({{center.x - hw, center.y + hh, center.z}, back_normal, color, {0.0f, 1.0f}});

    mesh.indices.push_back(4 + 0);
    mesh.indices.push_back(4 + 2);
    mesh.indices.push_back(4 + 1);
    mesh.indices.push_back(4 + 0);
    mesh.indices.push_back(4 + 3);
    mesh.indices.push_back(4 + 2);

    return mesh;
}

// Merge multiple meshes into one
static Mesh merge_meshes(const std::vector<Mesh>& meshes) {
    Mesh result;
    for (const auto& m : meshes) {
        uint32_t base = static_cast<uint32_t>(result.vertices.size());
        result.vertices.insert(result.vertices.end(), m.vertices.begin(), m.vertices.end());
        for (auto idx : m.indices) {
            result.indices.push_back(base + idx);
        }
    }
    return result;
}

static TransparentMesh merge_transparent_meshes(const std::vector<TransparentMesh>& meshes) {
    TransparentMesh result;
    for (const auto& m : meshes) {
        uint32_t base = static_cast<uint32_t>(result.vertices.size());
        result.vertices.insert(result.vertices.end(), m.vertices.begin(), m.vertices.end());
        for (auto idx : m.indices) {
            result.indices.push_back(base + idx);
        }
    }
    return result;
}

// ============================================================================
// G-Buffer Management
// ============================================================================

struct GBuffer {
    hina_texture position;
    hina_texture normal;
    hina_texture albedo;
    hina_texture depth;
    uint32_t width;
    uint32_t height;
};

static bool gbuffer_init(GBuffer* gb, uint32_t width, uint32_t height) {
    gb->width = width;
    gb->height = height;

    hina_texture_desc pos_desc = hina_texture_desc_default();
    pos_desc.format = HINA_FORMAT_R16G16B16A16_SFLOAT;
    pos_desc.width = width;
    pos_desc.height = height;
    pos_desc.usage = static_cast<hina_texture_usage_flags>(
        HINA_TEXTURE_RENDER_TARGET_BIT | HINA_TEXTURE_INPUT_ATTACHMENT_BIT);

    gb->position = hina_make_texture(&pos_desc);
    if (!hina_texture_is_valid(gb->position)) {
        EXAMPLE_LOGE("Failed to create G-buffer position texture");
        return false;
    }

    hina_texture_desc norm_desc = pos_desc;
    gb->normal = hina_make_texture(&norm_desc);
    if (!hina_texture_is_valid(gb->normal)) {
        EXAMPLE_LOGE("Failed to create G-buffer normal texture");
        hina_destroy_texture(gb->position);
        return false;
    }

    hina_texture_desc albedo_desc = hina_texture_desc_default();
    albedo_desc.format = HINA_FORMAT_R8G8B8A8_UNORM;
    albedo_desc.width = width;
    albedo_desc.height = height;
    albedo_desc.usage = static_cast<hina_texture_usage_flags>(
        HINA_TEXTURE_RENDER_TARGET_BIT | HINA_TEXTURE_INPUT_ATTACHMENT_BIT);

    gb->albedo = hina_make_texture(&albedo_desc);
    if (!hina_texture_is_valid(gb->albedo)) {
        EXAMPLE_LOGE("Failed to create G-buffer albedo texture");
        hina_destroy_texture(gb->position);
        hina_destroy_texture(gb->normal);
        return false;
    }

    hina_texture_desc depth_desc = hina_texture_desc_default();
    depth_desc.format = HINA_FORMAT_D32_SFLOAT;
    depth_desc.width = width;
    depth_desc.height = height;
    depth_desc.usage = static_cast<hina_texture_usage_flags>(
        HINA_TEXTURE_RENDER_TARGET_BIT | HINA_TEXTURE_SAMPLED_BIT);

    gb->depth = hina_make_texture(&depth_desc);
    if (!hina_texture_is_valid(gb->depth)) {
        EXAMPLE_LOGE("Failed to create G-buffer depth texture");
        hina_destroy_texture(gb->position);
        hina_destroy_texture(gb->normal);
        hina_destroy_texture(gb->albedo);
        return false;
    }

    return true;
}

static void gbuffer_destroy(GBuffer* gb) {
    if (hina_texture_is_valid(gb->position)) hina_destroy_texture(gb->position);
    if (hina_texture_is_valid(gb->normal)) hina_destroy_texture(gb->normal);
    if (hina_texture_is_valid(gb->albedo)) hina_destroy_texture(gb->albedo);
    if (hina_texture_is_valid(gb->depth)) hina_destroy_texture(gb->depth);
    *gb = {};
}

static bool gbuffer_resize(GBuffer* gb, uint32_t width, uint32_t height) {
    if (gb->width == width && gb->height == height) return true;
    gbuffer_destroy(gb);
    return gbuffer_init(gb, width, height);
}

// ============================================================================
// Light Initialization (like Sascha Willems)
// ============================================================================

static void init_lights(Light* lights, int count) {
    srand(0);  // Fixed seed for reproducible results

    // Distribute lights in a grid pattern with some randomness
    int grid_size = (int)ceilf(sqrtf((float)count));
    float spacing = 12.0f / (float)grid_size;  // Cover -6 to +6 area

    for (int i = 0; i < count; i++) {
        int gx = i % grid_size;
        int gz = i / grid_size;

        // Grid position with some random offset
        float x = -6.0f + (gx + 0.5f) * spacing + ((rand() / (float)RAND_MAX) - 0.5f) * spacing * 0.5f;
        float z = -6.0f + (gz + 0.5f) * spacing + ((rand() / (float)RAND_MAX) - 0.5f) * spacing * 0.5f;
        float height = 0.8f + (rand() / (float)RAND_MAX) * 2.0f;  // 0.8-2.8 height

        lights[i].position = glm::vec4(x, height, z, height);  // w = base height for animation

        // Vibrant random colors
        float hue = (rand() / (float)RAND_MAX);
        // HSV to RGB (simplified, saturation=1, value=1)
        float r, g, b;
        int hi = (int)(hue * 6.0f) % 6;
        float f = hue * 6.0f - hi;
        switch (hi) {
            case 0: r = 1.0f; g = f;       b = 0.0f; break;
            case 1: r = 1.0f - f; g = 1.0f; b = 0.0f; break;
            case 2: r = 0.0f; g = 1.0f;    b = f; break;
            case 3: r = 0.0f; g = 1.0f - f; b = 1.0f; break;
            case 4: r = f;    g = 0.0f;    b = 1.0f; break;
            default: r = 1.0f; g = 0.0f;   b = 1.0f - f; break;
        }

        lights[i].color = glm::vec4(r, g, b, 2.0f);  // radius 2.0
    }
}

// ============================================================================
// Application State
// ============================================================================

struct DeferredApp {
    // Scene geometry
    Mesh scene;
    Mesh light_indicator;
    TransparentMesh glass;

    // Opaque buffers
    hina_buffer vbo;
    hina_buffer ibo;

    // Light indicator buffers
    hina_buffer light_vbo;
    hina_buffer light_ibo;

    // Transparent buffers
    hina_buffer glass_vbo;
    hina_buffer glass_ibo;

    // Per-frame UBO copies to avoid CPU/GPU race at high frame rates
    static constexpr uint32_t FRAMES_IN_FLIGHT = 3;
    hina_buffer gbuffer_ubo[FRAMES_IN_FLIGHT];
    hina_buffer composition_ubo[FRAMES_IN_FLIGHT];
    hina_buffer transparent_ubo[FRAMES_IN_FLIGHT];
    hina_buffer lights_ssbo;

    GBufferUBO* gbuffer_ubo_mapped[FRAMES_IN_FLIGHT];
    CompositionUBO* composition_ubo_mapped[FRAMES_IN_FLIGHT];
    TransparentUBO* transparent_ubo_mapped[FRAMES_IN_FLIGHT];
    Light* lights_mapped;

    // G-Buffer
    GBuffer gbuffer;

    // Pipelines
    hina_pipeline gbuffer_pipeline;
    hina_pipeline composition_pipeline;
    hina_pipeline transparent_pipeline;

    // Bind group layouts
    hina_bind_group_layout gbuffer_scene_layout;
    hina_bind_group_layout comp_scene_layout;
    hina_bind_group_layout comp_gbuffer_layout;  // For tile inputs (INPUT_ATTACHMENT)
    hina_bind_group_layout transparent_scene_layout;

    // Camera
    hina_camera camera;
};

static DeferredApp g_app = {};

// ============================================================================
// Initialization
// ============================================================================

static bool example_init(hina_example_app* app) {
    EXAMPLE_LOGI("Initializing Deferred Rendering example...");

    // ========================================================================
    // Generate Scene Geometry (Opaque)
    // ========================================================================

    std::vector<Mesh> meshes;

    // Floor
    meshes.push_back(generate_floor(15.0f, glm::vec3(0.5f, 0.5f, 0.55f)));

    // Grid of cubes with different colors
    glm::vec3 colors[] = {
        {0.9f, 0.2f, 0.2f},
        {0.2f, 0.9f, 0.2f},
        {0.2f, 0.2f, 0.9f},
        {0.9f, 0.9f, 0.2f},
        {0.9f, 0.2f, 0.9f},
        {0.2f, 0.9f, 0.9f},
        {0.9f, 0.6f, 0.2f},
        {0.6f, 0.2f, 0.9f},
    };

    // Create a more interesting scene with multiple cubes
    int color_idx = 0;
    for (int x = -2; x <= 2; x++) {
        for (int z = -2; z <= 2; z++) {
            if (x == 0 && z == 0) continue;  // Skip center for glass
            float height = 0.5f + (rand() / (float)RAND_MAX) * 0.5f;
            meshes.push_back(generate_cube(
                colors[color_idx % 8],
                glm::vec3(x * 2.0f, height, z * 2.0f),
                0.8f + (rand() / (float)RAND_MAX) * 0.4f
            ));
            color_idx++;
        }
    }

    g_app.scene = merge_meshes(meshes);
    EXAMPLE_LOGI("Opaque scene: %zu vertices, %zu indices",
              g_app.scene.vertices.size(), g_app.scene.indices.size());

    // Light indicators (small emissive cubes)
    g_app.light_indicator = generate_cube(glm::vec3(5.0f, 5.0f, 5.0f), glm::vec3(0.0f), 0.08f);

    // ========================================================================
    // Generate Transparent Geometry
    // ========================================================================

    std::vector<TransparentMesh> glass_meshes;

    // Glass panels around center
    glass_meshes.push_back(generate_glass_panel(
        glm::vec3(0.0f, 1.5f, 1.5f), glm::vec2(2.5f, 3.0f),
        glm::vec4(0.2f, 0.4f, 0.9f, 0.4f)));  // Blue glass

    glass_meshes.push_back(generate_glass_panel(
        glm::vec3(0.0f, 1.5f, -1.5f), glm::vec2(2.5f, 3.0f),
        glm::vec4(0.9f, 0.2f, 0.2f, 0.4f)));  // Red glass

    glass_meshes.push_back(generate_glass_panel(
        glm::vec3(1.5f, 1.5f, 0.0f), glm::vec2(2.5f, 3.0f),
        glm::vec4(0.2f, 0.9f, 0.2f, 0.4f)));  // Green glass (rotated)

    g_app.glass = merge_transparent_meshes(glass_meshes);
    EXAMPLE_LOGI("Transparent: %zu vertices, %zu indices",
              g_app.glass.vertices.size(), g_app.glass.indices.size());

    // ========================================================================
    // Create Vertex/Index Buffers
    // ========================================================================

    // Opaque scene VBO/IBO
    hina_buffer_desc vbo_desc = {0};
    vbo_desc.size = g_app.scene.vertices.size() * sizeof(Vertex);
    vbo_desc.flags = static_cast<hina_buffer_flags>(
        HINA_BUFFER_VERTEX_BIT | HINA_BUFFER_HOST_VISIBLE_BIT | HINA_BUFFER_HOST_COHERENT_BIT);
    vbo_desc.initial_data = g_app.scene.vertices.data();

    g_app.vbo = hina_make_buffer(&vbo_desc);
    if (!hina_buffer_is_valid(g_app.vbo)) {
        EXAMPLE_LOGE("Failed to create vertex buffer");
        return false;
    }

    hina_buffer_desc ibo_desc = {0};
    ibo_desc.size = g_app.scene.indices.size() * sizeof(uint32_t);
    ibo_desc.flags = static_cast<hina_buffer_flags>(
        HINA_BUFFER_INDEX_BIT | HINA_BUFFER_HOST_VISIBLE_BIT | HINA_BUFFER_HOST_COHERENT_BIT);
    ibo_desc.initial_data = g_app.scene.indices.data();

    g_app.ibo = hina_make_buffer(&ibo_desc);
    if (!hina_buffer_is_valid(g_app.ibo)) {
        EXAMPLE_LOGE("Failed to create index buffer");
        return false;
    }

    // Light indicator VBO/IBO
    hina_buffer_desc light_vbo_desc = {0};
    light_vbo_desc.size = g_app.light_indicator.vertices.size() * sizeof(Vertex);
    light_vbo_desc.flags = static_cast<hina_buffer_flags>(
        HINA_BUFFER_VERTEX_BIT | HINA_BUFFER_HOST_VISIBLE_BIT | HINA_BUFFER_HOST_COHERENT_BIT);
    light_vbo_desc.initial_data = g_app.light_indicator.vertices.data();

    g_app.light_vbo = hina_make_buffer(&light_vbo_desc);
    if (!hina_buffer_is_valid(g_app.light_vbo)) {
        EXAMPLE_LOGE("Failed to create light indicator vertex buffer");
        return false;
    }

    hina_buffer_desc light_ibo_desc = {0};
    light_ibo_desc.size = g_app.light_indicator.indices.size() * sizeof(uint32_t);
    light_ibo_desc.flags = static_cast<hina_buffer_flags>(
        HINA_BUFFER_INDEX_BIT | HINA_BUFFER_HOST_VISIBLE_BIT | HINA_BUFFER_HOST_COHERENT_BIT);
    light_ibo_desc.initial_data = g_app.light_indicator.indices.data();

    g_app.light_ibo = hina_make_buffer(&light_ibo_desc);
    if (!hina_buffer_is_valid(g_app.light_ibo)) {
        EXAMPLE_LOGE("Failed to create light indicator index buffer");
        return false;
    }

    // Transparent VBO/IBO
    hina_buffer_desc glass_vbo_desc = {0};
    glass_vbo_desc.size = g_app.glass.vertices.size() * sizeof(TransparentVertex);
    glass_vbo_desc.flags = static_cast<hina_buffer_flags>(
        HINA_BUFFER_VERTEX_BIT | HINA_BUFFER_HOST_VISIBLE_BIT | HINA_BUFFER_HOST_COHERENT_BIT);
    glass_vbo_desc.initial_data = g_app.glass.vertices.data();

    g_app.glass_vbo = hina_make_buffer(&glass_vbo_desc);
    if (!hina_buffer_is_valid(g_app.glass_vbo)) {
        EXAMPLE_LOGE("Failed to create glass vertex buffer");
        return false;
    }

    hina_buffer_desc glass_ibo_desc = {0};
    glass_ibo_desc.size = g_app.glass.indices.size() * sizeof(uint32_t);
    glass_ibo_desc.flags = static_cast<hina_buffer_flags>(
        HINA_BUFFER_INDEX_BIT | HINA_BUFFER_HOST_VISIBLE_BIT | HINA_BUFFER_HOST_COHERENT_BIT);
    glass_ibo_desc.initial_data = g_app.glass.indices.data();

    g_app.glass_ibo = hina_make_buffer(&glass_ibo_desc);
    if (!hina_buffer_is_valid(g_app.glass_ibo)) {
        EXAMPLE_LOGE("Failed to create glass index buffer");
        return false;
    }

    // ========================================================================
    // Create Uniform/Storage Buffers
    // ========================================================================

    hina_buffer_desc ubo_desc = {0};
    ubo_desc.flags = static_cast<hina_buffer_flags>(
        HINA_BUFFER_UNIFORM_BIT | HINA_BUFFER_HOST_VISIBLE_BIT | HINA_BUFFER_HOST_COHERENT_BIT);

    ubo_desc.size = sizeof(GBufferUBO);
    for (uint32_t i = 0; i < DeferredApp::FRAMES_IN_FLIGHT; i++) {
        g_app.gbuffer_ubo[i] = hina_make_buffer(&ubo_desc);
        if (!hina_buffer_is_valid(g_app.gbuffer_ubo[i])) {
            EXAMPLE_LOGE("Failed to create G-buffer UBO %u", i);
            return false;
        }
        g_app.gbuffer_ubo_mapped[i] = static_cast<GBufferUBO*>(hina_map_buffer(g_app.gbuffer_ubo[i]));
    }

    ubo_desc.size = sizeof(CompositionUBO);
    for (uint32_t i = 0; i < DeferredApp::FRAMES_IN_FLIGHT; i++) {
        g_app.composition_ubo[i] = hina_make_buffer(&ubo_desc);
        if (!hina_buffer_is_valid(g_app.composition_ubo[i])) {
            EXAMPLE_LOGE("Failed to create composition UBO %u", i);
            return false;
        }
        g_app.composition_ubo_mapped[i] = static_cast<CompositionUBO*>(hina_map_buffer(g_app.composition_ubo[i]));
    }

    ubo_desc.size = sizeof(TransparentUBO);
    for (uint32_t i = 0; i < DeferredApp::FRAMES_IN_FLIGHT; i++) {
        g_app.transparent_ubo[i] = hina_make_buffer(&ubo_desc);
        if (!hina_buffer_is_valid(g_app.transparent_ubo[i])) {
            EXAMPLE_LOGE("Failed to create transparent UBO %u", i);
            return false;
        }
        g_app.transparent_ubo_mapped[i] = static_cast<TransparentUBO*>(hina_map_buffer(g_app.transparent_ubo[i]));
    }

    hina_buffer_desc ssbo_desc = {0};
    ssbo_desc.size = sizeof(Light) * NUM_LIGHTS;
    ssbo_desc.flags = static_cast<hina_buffer_flags>(
        HINA_BUFFER_STORAGE_BIT | HINA_BUFFER_HOST_VISIBLE_BIT | HINA_BUFFER_HOST_COHERENT_BIT);

    g_app.lights_ssbo = hina_make_buffer(&ssbo_desc);
    if (!hina_buffer_is_valid(g_app.lights_ssbo)) {
        EXAMPLE_LOGE("Failed to create lights SSBO");
        return false;
    }
    g_app.lights_mapped = static_cast<Light*>(hina_map_buffer(g_app.lights_ssbo));
    init_lights(g_app.lights_mapped, NUM_LIGHTS);

    // ========================================================================
    // Create G-Buffer
    // ========================================================================

    if (!gbuffer_init(&g_app.gbuffer, app->width, app->height)) {
        EXAMPLE_LOGE("Failed to create G-buffer");
        return false;
    }

    // ========================================================================
    // Create Pipelines
    // ========================================================================

    char* error = nullptr;

    // Define tile pass layout for legacy render pass compatibility
    // This describes the subpass structure so pipelines get the right render pass template
    hina_tile_pass_layout tile_layout = {};
    tile_layout.subpass_count = 2;
    tile_layout.samples = HINA_SAMPLE_COUNT_1_BIT;
    // Subpass 0: G-Buffer (3 color outputs + depth write)
    tile_layout.subpasses[0].color_count = 3;
    tile_layout.subpasses[0].color_formats[0] = HINA_FORMAT_R16G16B16A16_SFLOAT;  // Position
    tile_layout.subpasses[0].color_formats[1] = HINA_FORMAT_R16G16B16A16_SFLOAT;  // Normal
    tile_layout.subpasses[0].color_formats[2] = HINA_FORMAT_R8G8B8A8_UNORM;       // Albedo
    tile_layout.subpasses[0].depth_format = HINA_FORMAT_D32_SFLOAT;
    tile_layout.subpasses[0].depth_read_only = false;  // G-buffer writes depth
    tile_layout.subpasses[0].input_count = 0;
    // Subpass 1: Composition (1 color output, 3 tile inputs, depth read-only)
    tile_layout.subpasses[1].color_count = 1;
    tile_layout.subpasses[1].color_formats[0] = HINA_FORMAT_SWAPCHAIN;
    tile_layout.subpasses[1].depth_format = HINA_FORMAT_D32_SFLOAT;
    tile_layout.subpasses[1].depth_read_only = true;  // Must match runtime for render pass compatibility
    tile_layout.subpasses[1].input_count = 3;  // Position, normal, albedo from subpass 0
    // Tile input mappings: source subpass and attachment index
    tile_layout.subpasses[1].tile_inputs[0] = {0, 0};  // Position from subpass 0, attachment 0
    tile_layout.subpasses[1].tile_inputs[1] = {0, 1};  // Normal from subpass 0, attachment 1
    tile_layout.subpasses[1].tile_inputs[2] = {0, 2};  // Albedo from subpass 0, attachment 2

    // G-Buffer pipeline (MRT output)
    hina_vertex_layout gbuffer_layout = {};
    gbuffer_layout.buffer_count = 1;
    gbuffer_layout.buffer_strides[0] = sizeof(Vertex);
    gbuffer_layout.input_rates[0] = HINA_VERTEX_INPUT_RATE_VERTEX;
    gbuffer_layout.attr_count = 3;
    gbuffer_layout.attrs[0] = { HINA_FORMAT_R32G32B32_SFLOAT, offsetof(Vertex, position), 0, 0 };
    gbuffer_layout.attrs[1] = { HINA_FORMAT_R32G32B32_SFLOAT, offsetof(Vertex, normal), 1, 0 };
    gbuffer_layout.attrs[2] = { HINA_FORMAT_R32G32B32_SFLOAT, offsetof(Vertex, color), 2, 0 };

    char* gbuffer_shader_path = hina_example_shader_path(app, "gbuffer.hina_sl");

    hina_hsl_pipeline_desc gbuffer_pip_desc = hina_hsl_pipeline_desc_default();
    gbuffer_pip_desc.layout = gbuffer_layout;
    gbuffer_pip_desc.front_face = HINA_FRONT_FACE_COUNTER_CLOCKWISE;
    gbuffer_pip_desc.cull_mode = HINA_CULL_MODE_BACK;
    gbuffer_pip_desc.color_formats[0] = HINA_FORMAT_R16G16B16A16_SFLOAT;
    gbuffer_pip_desc.color_formats[1] = HINA_FORMAT_R16G16B16A16_SFLOAT;
    gbuffer_pip_desc.color_formats[2] = HINA_FORMAT_R8G8B8A8_UNORM;
    gbuffer_pip_desc.depth_format = HINA_FORMAT_D32_SFLOAT;
    gbuffer_pip_desc.tile_layout = &tile_layout;  // For tile pass render pass compatibility

    g_app.gbuffer_pipeline = hina_example_make_pipeline_from_hsl(app, gbuffer_shader_path, &gbuffer_pip_desc, &error);
    free(gbuffer_shader_path);
    if (!hina_pipeline_is_valid(g_app.gbuffer_pipeline)) {
        EXAMPLE_LOGE("G-buffer pipeline failed: %s", error ? error : "Unknown");
        if (error) hslc_free_log(error);
        return false;
    }

    g_app.gbuffer_scene_layout = hina_pipeline_get_bind_group_layout(g_app.gbuffer_pipeline, 0);
    if (!hina_bind_group_layout_is_valid(g_app.gbuffer_scene_layout)) {
        EXAMPLE_LOGE("Failed to get G-buffer bind group layout for set 0");
        return false;
    }

    // Composition pipeline (full-screen, no vertex input) - runs in tile pass subpass 1
    hina_vertex_layout empty_layout = {};

    char* comp_shader_path = hina_example_shader_path(app, "composition_tile.hina_sl");

    hina_hsl_pipeline_desc comp_pip_desc = hina_hsl_pipeline_desc_default();
    comp_pip_desc.layout = empty_layout;
    comp_pip_desc.cull_mode = HINA_CULL_MODE_NONE;
    comp_pip_desc.depth.depth_test = false;
    comp_pip_desc.depth.depth_write = false;
    comp_pip_desc.color_formats[0] = HINA_FORMAT_SWAPCHAIN;
    comp_pip_desc.depth_format = HINA_FORMAT_D32_SFLOAT;
    comp_pip_desc.subpass_index = 1;  // Tile pass subpass 1 (composition)
    comp_pip_desc.tile_layout = &tile_layout;  // Required for subpass_index > 0

    g_app.composition_pipeline = hina_example_make_pipeline_from_hsl(app, comp_shader_path, &comp_pip_desc, &error);
    free(comp_shader_path);
    if (!hina_pipeline_is_valid(g_app.composition_pipeline)) {
        EXAMPLE_LOGE("Composition pipeline failed: %s", error ? error : "Unknown");
        if (error) hslc_free_log(error);
        return false;
    }

    g_app.comp_scene_layout = hina_pipeline_get_bind_group_layout(g_app.composition_pipeline, 0);
    if (!hina_bind_group_layout_is_valid(g_app.comp_scene_layout)) {
        EXAMPLE_LOGE("Failed to get composition bind group layout for set 0");
        return false;
    }

    // Get tile input bind group layout (set 1 - GBuffer group with tile inputs)
    g_app.comp_gbuffer_layout = hina_pipeline_get_bind_group_layout(g_app.composition_pipeline, 1);
    if (!hina_bind_group_layout_is_valid(g_app.comp_gbuffer_layout)) {
        EXAMPLE_LOGE("Failed to get composition bind group layout for set 1 (tile inputs)");
        return false;
    }

    // Note: No sampler needed for tile inputs - they read from tile memory directly

    // Transparent pipeline (forward pass with alpha blending)
    hina_vertex_layout trans_layout = {};
    trans_layout.buffer_count = 1;
    trans_layout.buffer_strides[0] = sizeof(TransparentVertex);
    trans_layout.input_rates[0] = HINA_VERTEX_INPUT_RATE_VERTEX;
    trans_layout.attr_count = 4;
    trans_layout.attrs[0] = { HINA_FORMAT_R32G32B32_SFLOAT, offsetof(TransparentVertex, position), 0, 0 };
    trans_layout.attrs[1] = { HINA_FORMAT_R32G32B32_SFLOAT, offsetof(TransparentVertex, normal), 1, 0 };
    trans_layout.attrs[2] = { HINA_FORMAT_R32G32B32A32_SFLOAT, offsetof(TransparentVertex, color), 2, 0 };
    trans_layout.attrs[3] = { HINA_FORMAT_R32G32_SFLOAT, offsetof(TransparentVertex, uv), 3, 0 };

    char* trans_shader_path = hina_example_shader_path(app, "transparent.hina_sl");

    hina_hsl_pipeline_desc trans_pip_desc = hina_hsl_pipeline_desc_default();
    trans_pip_desc.layout = trans_layout;
    trans_pip_desc.front_face = HINA_FRONT_FACE_COUNTER_CLOCKWISE;
    trans_pip_desc.cull_mode = HINA_CULL_MODE_NONE;  // No culling for glass
    trans_pip_desc.depth.depth_test = true;
    trans_pip_desc.depth.depth_write = false;  // Don't write depth for transparent
    trans_pip_desc.blend[0].enable = true;
    trans_pip_desc.blend[0].src_color = HINA_BLEND_FACTOR_SRC_ALPHA;
    trans_pip_desc.blend[0].dst_color = HINA_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA;
    trans_pip_desc.blend[0].src_alpha = HINA_BLEND_FACTOR_ONE;
    trans_pip_desc.blend[0].dst_alpha = HINA_BLEND_FACTOR_ZERO;
    trans_pip_desc.color_formats[0] = HINA_FORMAT_SWAPCHAIN;
    trans_pip_desc.depth_format = HINA_FORMAT_D32_SFLOAT;
    trans_pip_desc.subpass_index = 1;  // Same subpass as composition
    trans_pip_desc.tile_layout = &tile_layout;  // For tile pass render pass compatibility

    g_app.transparent_pipeline = hina_example_make_pipeline_from_hsl(app, trans_shader_path, &trans_pip_desc, &error);
    free(trans_shader_path);
    if (!hina_pipeline_is_valid(g_app.transparent_pipeline)) {
        EXAMPLE_LOGE("Transparent pipeline failed: %s", error ? error : "Unknown");
        if (error) hslc_free_log(error);
        return false;
    }

    g_app.transparent_scene_layout = hina_pipeline_get_bind_group_layout(g_app.transparent_pipeline, 0);
    if (!hina_bind_group_layout_is_valid(g_app.transparent_scene_layout)) {
        EXAMPLE_LOGE("Failed to get transparent bind group layout for set 0");
        return false;
    }

    EXAMPLE_LOGI("All pipelines created successfully");

    // ========================================================================
    // Setup Camera
    // ========================================================================

    g_app.camera.rotation = glm::vec3(35.0f, 45.0f, 0.0f);
    g_app.camera.zoom = -12.0f;

    EXAMPLE_LOGI("Deferred shading with %d dynamic point lights + transparent pass", NUM_LIGHTS);
    return true;
}

// ============================================================================
// Rendering
// ============================================================================

static void example_render(hina_example_app* app) {
    g_app.camera.update(*app);

    hina_swapchain_image swapchain = hina_frame_begin();
    if (swapchain.texture.id == HINA_INVALID_HANDLE) {
        hina_example_try_recover_surface(app);
        hina_frame_end();
        return;
    }

    uint32_t w, h;
    hina_get_texture_size(swapchain.texture, &w, &h);

    if (!gbuffer_resize(&g_app.gbuffer, w, h)) {
        EXAMPLE_LOGE("Failed to resize G-buffer");
        hina_frame_end();
        return;
    }

    float aspect = static_cast<float>(w) / static_cast<float>(h);
    float time = app->elapsed_time;
    uint32_t frame_idx = static_cast<uint32_t>(hina_get_frame_index() % DeferredApp::FRAMES_IN_FLIGHT);

    g_app.gbuffer_ubo_mapped[frame_idx]->projection = glm::perspective(glm::radians(60.0f), aspect, 0.1f, 256.0f);
    g_app.gbuffer_ubo_mapped[frame_idx]->projection[1][1] *= -1.0f;
    g_app.gbuffer_ubo_mapped[frame_idx]->view = g_app.camera.view_matrix();

    glm::vec3 cam_pos = glm::vec3(0.0f, 0.0f, -g_app.camera.zoom);
    cam_pos = glm::mat3(glm::inverse(g_app.camera.view_matrix())) * cam_pos;
    g_app.composition_ubo_mapped[frame_idx]->view_pos = glm::vec4(cam_pos, 1.0f);
    g_app.composition_ubo_mapped[frame_idx]->light_count = NUM_LIGHTS;

    g_app.transparent_ubo_mapped[frame_idx]->projection = g_app.gbuffer_ubo_mapped[frame_idx]->projection;
    g_app.transparent_ubo_mapped[frame_idx]->view = g_app.gbuffer_ubo_mapped[frame_idx]->view;
    g_app.transparent_ubo_mapped[frame_idx]->model = glm::mat4(1.0f);
    g_app.transparent_ubo_mapped[frame_idx]->depth_tex_id = 0;
    g_app.transparent_ubo_mapped[frame_idx]->near_plane = 0.1f;
    g_app.transparent_ubo_mapped[frame_idx]->far_plane = 256.0f;

    // Animate lights - use base height (stored in w) and compute offset from elapsed_time
    for (int i = 0; i < NUM_LIGHTS; i++) {
        float phase = i * 0.1f;
        float base_y = g_app.lights_mapped[i].position.w;
        g_app.lights_mapped[i].position.y = base_y + sinf(time * 2.0f + phase) * 0.3f;
    }

    hina_cmd* cmd = hina_cmd_begin_ex(HINA_QUEUE_GRAPHICS);
    if (!cmd) {
        hina_frame_end();
        return;
    }

    // ====================================================================
    // Tile Pass: G-Buffer + Composition (2 subpasses with tile inputs)
    // ====================================================================
    // Subpass 0: G-Buffer fill (MRT output)
    // Subpass 1: Composition (reads G-buffer via tile inputs) + Transparent
    // ====================================================================

    hina_texture_view pos_view = hina_texture_get_default_view(g_app.gbuffer.position);
    hina_texture_view norm_view = hina_texture_get_default_view(g_app.gbuffer.normal);
    hina_texture_view albedo_view = hina_texture_get_default_view(g_app.gbuffer.albedo);
    hina_texture_view depth_view = hina_texture_get_default_view(g_app.gbuffer.depth);
    hina_texture_view swapchain_view = hina_texture_get_default_view(swapchain.texture);

    hina_tile_pass_desc tile_pass = {};
    tile_pass.label = "deferred_tile_pass";
    tile_pass.subpass_count = 2;

    // Subpass 0: G-Buffer fill
    tile_pass.subpasses[0].label = "gbuffer";
    tile_pass.subpasses[0].color_count = 3;
    tile_pass.subpasses[0].color[0].image = pos_view;
    tile_pass.subpasses[0].color[0].load_op = HINA_LOAD_OP_CLEAR;
    tile_pass.subpasses[0].color[0].store_op = HINA_STORE_OP_STORE;
    tile_pass.subpasses[0].color[0].clear_color[3] = 0.0f;  // Alpha=0 for background detection

    tile_pass.subpasses[0].color[1].image = norm_view;
    tile_pass.subpasses[0].color[1].load_op = HINA_LOAD_OP_CLEAR;
    tile_pass.subpasses[0].color[1].store_op = HINA_STORE_OP_STORE;

    tile_pass.subpasses[0].color[2].image = albedo_view;
    tile_pass.subpasses[0].color[2].load_op = HINA_LOAD_OP_CLEAR;
    tile_pass.subpasses[0].color[2].store_op = HINA_STORE_OP_STORE;

    tile_pass.subpasses[0].has_depth = true;
    tile_pass.subpasses[0].depth.image = depth_view;
    tile_pass.subpasses[0].depth.load_op = HINA_LOAD_OP_CLEAR;
    tile_pass.subpasses[0].depth.store_op = HINA_STORE_OP_STORE;
    tile_pass.subpasses[0].depth.depth_clear = 1.0f;

    // Subpass 1: Composition (tile inputs from subpass 0) + Transparent
    tile_pass.subpasses[1].label = "composition";
    tile_pass.subpasses[1].color_count = 1;
    tile_pass.subpasses[1].color[0].image = swapchain_view;
    tile_pass.subpasses[1].color[0].load_op = HINA_LOAD_OP_CLEAR;
    tile_pass.subpasses[1].color[0].store_op = HINA_STORE_OP_STORE;

    // Tile inputs: read G-buffer from subpass 0
    tile_pass.subpasses[1].tile_input_count = 3;
    tile_pass.subpasses[1].tile_inputs[0] = { 0, 0 };  // position from subpass 0, attachment 0
    tile_pass.subpasses[1].tile_inputs[1] = { 0, 1 };  // normal from subpass 0, attachment 1
    tile_pass.subpasses[1].tile_inputs[2] = { 0, 2 };  // albedo from subpass 0, attachment 2

    // Depth for transparent pass (load from subpass 0)
    tile_pass.subpasses[1].has_depth = true;
    tile_pass.subpasses[1].depth.image = depth_view;
    tile_pass.subpasses[1].depth.load_op = HINA_LOAD_OP_LOAD;  // Preserve depth from G-buffer pass
    tile_pass.subpasses[1].depth.store_op = HINA_STORE_OP_STORE;
    tile_pass.subpasses[1].depth_read_only = true;

    // Begin tile pass
    if (!hina_begin_tile_pass(cmd, &tile_pass)) {
        EXAMPLE_LOGE("Failed to begin tile pass");
        hina_frame_end();
        return;
    }

    // ====================================================================
    // Subpass 0: G-Buffer Fill
    // ====================================================================

    hina_bind_group_entry gbuffer_scene_entry = {};
    gbuffer_scene_entry.binding = 0;
    gbuffer_scene_entry.type = HINA_DESC_TYPE_UNIFORM_BUFFER;
    gbuffer_scene_entry.buffer.buffer = g_app.gbuffer_ubo[frame_idx];
    gbuffer_scene_entry.buffer.offset = 0;
    gbuffer_scene_entry.buffer.size = sizeof(GBufferUBO);

    hina_bind_group_desc gbuffer_scene_desc = {};
    gbuffer_scene_desc.layout = g_app.gbuffer_scene_layout;
    gbuffer_scene_desc.entries = &gbuffer_scene_entry;
    gbuffer_scene_desc.entry_count = 1;
    gbuffer_scene_desc.label = "gbuffer_scene";

    hina_transient_bind_group gbuffer_scene_group = hina_example_make_transient_bind_group(&gbuffer_scene_desc);

    hina_cmd_bind_pipeline(cmd, g_app.gbuffer_pipeline);
    hina_cmd_bind_transient_group(cmd, 0, gbuffer_scene_group);

    // Draw scene with identity model matrix, no color override
    ModelPushConstants model_pc;
    model_pc.model = glm::mat4(1.0f);
    model_pc.color_override = glm::vec4(0.0f);
    hina_cmd_push_constants(cmd, 0, sizeof(ModelPushConstants), &model_pc);

    hina_vertex_input bindings = {};
    bindings.vertex_buffers[0] = g_app.vbo;
    bindings.index_buffer = g_app.ibo;
    bindings.index_type = HINA_INDEX_UINT32;

    hina_cmd_apply_vertex_input(cmd, &bindings);
    hina_cmd_draw_indexed(cmd, static_cast<uint32_t>(g_app.scene.indices.size()), 1, 0, 0, 0);

    // Draw light indicators using push constants for per-light model matrix and color
    hina_vertex_input light_bindings = {};
    light_bindings.vertex_buffers[0] = g_app.light_vbo;
    light_bindings.index_buffer = g_app.light_ibo;
    light_bindings.index_type = HINA_INDEX_UINT32;
    hina_cmd_apply_vertex_input(cmd, &light_bindings);

    for (int i = 0; i < NUM_LIGHTS; i++) {
        model_pc.model = glm::translate(glm::mat4(1.0f),
            glm::vec3(g_app.lights_mapped[i].position));
        model_pc.color_override = glm::vec4(g_app.lights_mapped[i].color.r,
                                             g_app.lights_mapped[i].color.g,
                                             g_app.lights_mapped[i].color.b,
                                             2.0f);
        hina_cmd_push_constants(cmd, 0, sizeof(ModelPushConstants), &model_pc);
        hina_cmd_draw_indexed(cmd, static_cast<uint32_t>(g_app.light_indicator.indices.size()), 1, 0, 0, 0);
    }

    // ====================================================================
    // Transition to Subpass 1: Composition
    // ====================================================================

    hina_tile_pass_next(cmd, &tile_pass);

    // Create tile input bind group (INPUT_ATTACHMENT descriptors for G-buffer)
    hina_bind_group_entry tile_input_entries[3] = {};
    tile_input_entries[0].binding = 0;
    tile_input_entries[0].type = HINA_DESC_TYPE_INPUT_ATTACHMENT;
    tile_input_entries[0].view = pos_view;

    tile_input_entries[1].binding = 1;
    tile_input_entries[1].type = HINA_DESC_TYPE_INPUT_ATTACHMENT;
    tile_input_entries[1].view = norm_view;

    tile_input_entries[2].binding = 2;
    tile_input_entries[2].type = HINA_DESC_TYPE_INPUT_ATTACHMENT;
    tile_input_entries[2].view = albedo_view;

    hina_bind_group_desc tile_input_desc = {};
    tile_input_desc.layout = g_app.comp_gbuffer_layout;
    tile_input_desc.entries = tile_input_entries;
    tile_input_desc.entry_count = 3;
    tile_input_desc.label = "tile_inputs";

    hina_transient_bind_group tile_input_group = hina_example_make_transient_bind_group(&tile_input_desc);

    // Scene bind group (UBO + SSBO)
    hina_bind_group_entry comp_entries[2] = {};
    comp_entries[0].binding = 0;
    comp_entries[0].type = HINA_DESC_TYPE_UNIFORM_BUFFER;
    comp_entries[0].buffer.buffer = g_app.composition_ubo[frame_idx];
    comp_entries[0].buffer.offset = 0;
    comp_entries[0].buffer.size = sizeof(CompositionUBO);

    comp_entries[1].binding = 1;
    comp_entries[1].type = HINA_DESC_TYPE_STORAGE_BUFFER;
    comp_entries[1].buffer.buffer = g_app.lights_ssbo;
    comp_entries[1].buffer.offset = 0;
    comp_entries[1].buffer.size = sizeof(Light) * NUM_LIGHTS;

    hina_bind_group_desc comp_group_desc = {};
    comp_group_desc.layout = g_app.comp_scene_layout;
    comp_group_desc.entries = comp_entries;
    comp_group_desc.entry_count = 2;
    comp_group_desc.label = "composition_scene";

    hina_transient_bind_group comp_scene_group = hina_example_make_transient_bind_group(&comp_group_desc);

    hina_cmd_bind_pipeline(cmd, g_app.composition_pipeline);
    hina_cmd_bind_transient_group(cmd, 0, comp_scene_group);
    hina_cmd_bind_transient_group(cmd, 1, tile_input_group);

    hina_cmd_draw(cmd, 3, 1, 0, 0);

    // ====================================================================
    // Transparent Forward Pass (still in subpass 1, uses same depth)
    // ====================================================================

    hina_cmd_bind_pipeline(cmd, g_app.transparent_pipeline);
    hina_bind_group_entry transparent_entry = {};
    transparent_entry.binding = 0;
    transparent_entry.type = HINA_DESC_TYPE_UNIFORM_BUFFER;
    transparent_entry.buffer.buffer = g_app.transparent_ubo[frame_idx];
    transparent_entry.buffer.offset = 0;
    transparent_entry.buffer.size = sizeof(TransparentUBO);

    hina_bind_group_desc transparent_group_desc = {};
    transparent_group_desc.layout = g_app.transparent_scene_layout;
    transparent_group_desc.entries = &transparent_entry;
    transparent_group_desc.entry_count = 1;
    transparent_group_desc.label = "transparent_scene";

    hina_transient_bind_group transparent_group = hina_example_make_transient_bind_group(&transparent_group_desc);
    hina_cmd_bind_transient_group(cmd, 0, transparent_group);

    hina_vertex_input glass_bindings = {};
    glass_bindings.vertex_buffers[0] = g_app.glass_vbo;
    glass_bindings.index_buffer = g_app.glass_ibo;
    glass_bindings.index_type = HINA_INDEX_UINT32;

    hina_cmd_apply_vertex_input(cmd, &glass_bindings);
    hina_cmd_draw_indexed(cmd, static_cast<uint32_t>(g_app.glass.indices.size()), 1, 0, 0, 0);

    // Present frame (ends pass, renders ImGui, submits, ends frame)
    hina_example_present_frame(app, cmd, swapchain);
}

// ============================================================================
// Cleanup
// ============================================================================

static void example_cleanup(hina_example_app* app) {
    (void)app;
    EXAMPLE_LOGI("Cleaning up Deferred Rendering example...");

    // Destroy pipelines
    if (hina_pipeline_is_valid(g_app.transparent_pipeline))
        hina_destroy_pipeline(g_app.transparent_pipeline);
    if (hina_pipeline_is_valid(g_app.composition_pipeline))
        hina_destroy_pipeline(g_app.composition_pipeline);
    if (hina_pipeline_is_valid(g_app.gbuffer_pipeline))
        hina_destroy_pipeline(g_app.gbuffer_pipeline);

    // Destroy G-buffer
    gbuffer_destroy(&g_app.gbuffer);

    for (uint32_t i = 0; i < DeferredApp::FRAMES_IN_FLIGHT; i++) {
        if (hina_buffer_is_valid(g_app.transparent_ubo[i]))
            hina_destroy_buffer(g_app.transparent_ubo[i]);
        if (hina_buffer_is_valid(g_app.composition_ubo[i]))
            hina_destroy_buffer(g_app.composition_ubo[i]);
        if (hina_buffer_is_valid(g_app.gbuffer_ubo[i]))
            hina_destroy_buffer(g_app.gbuffer_ubo[i]);
    }
    if (hina_buffer_is_valid(g_app.lights_ssbo))
        hina_destroy_buffer(g_app.lights_ssbo);

    // Destroy transparent buffers
    if (hina_buffer_is_valid(g_app.glass_ibo))
        hina_destroy_buffer(g_app.glass_ibo);
    if (hina_buffer_is_valid(g_app.glass_vbo))
        hina_destroy_buffer(g_app.glass_vbo);

    // Destroy light indicator buffers
    if (hina_buffer_is_valid(g_app.light_ibo))
        hina_destroy_buffer(g_app.light_ibo);
    if (hina_buffer_is_valid(g_app.light_vbo))
        hina_destroy_buffer(g_app.light_vbo);

    // Destroy opaque buffers
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

HINA_EXAMPLE_MAIN("HinaVK Deferred Rendering", example_init, example_render, example_cleanup)
