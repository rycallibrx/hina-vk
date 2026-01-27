/**
 * HinaVK Offscreen Rendering Example
 *
 * This example demonstrates offscreen rendering and using the result as a texture:
 * - Render a 3D model to an offscreen framebuffer (reflected)
 * - Use the offscreen texture as a reflection on a mirror plane
 * - Press 'D' to toggle debug view showing the offscreen render target
 *
 * Based on https://github.com/SaschaWillems/Vulkan/blob/master/examples/offscreen/offscreen.cpp
 */

#include <cmath>
#include <vector>

#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>

#include "../hina_example.h"

// ============================================================================
// Configuration
// ============================================================================

// Offscreen framebuffer resolution
constexpr int OFFSCREEN_WIDTH = 512;
constexpr int OFFSCREEN_HEIGHT = 512;

// ============================================================================
// Vertex Data Structures
// ============================================================================

struct Vertex
{
    glm::vec3 position;
    glm::vec3 normal;
    glm::vec3 color;
};

struct MirrorVertex
{
    glm::vec3 position;
    glm::vec3 normal;
    glm::vec2 uv;
};

// Uniform buffer for phong shading
struct PhongUBO
{
    glm::mat4 projection;
    glm::mat4 view;
    glm::mat4 model;
    glm::vec4 light_pos;
};

// Uniform buffer for mirror plane
struct MirrorUBO
{
    glm::mat4 projection;
    glm::mat4 view;
    glm::mat4 model;
    glm::mat4 reflected_vp; // View-projection from reflected camera (for texture lookup)
};

// Uniform buffer for debug display
struct DebugUBO
{
    uint32_t texture_id;
    uint32_t _pad[3];
};

// ============================================================================
// Mesh Generation
// ============================================================================

struct Mesh
{
    std::vector<Vertex> vertices;
    std::vector<uint32_t> indices;
};

struct MirrorMesh
{
    std::vector<MirrorVertex> vertices;
    std::vector<uint32_t> indices;
};

// Generate a torus mesh
static Mesh generate_torus(float outer_radius, float inner_radius, int rings, int sides, glm::vec3 color)
{
    Mesh mesh;

    for (int ring = 0; ring <= rings; ring++)
    {
        float phi = 2.0f * glm::pi<float>() * float(ring) / float(rings);
        float cos_phi = cosf(phi);
        float sin_phi = sinf(phi);

        for (int side = 0; side <= sides; side++)
        {
            float theta = 2.0f * glm::pi<float>() * float(side) / float(sides);
            float cos_theta = cosf(theta);
            float sin_theta = sinf(theta);

            float x = (outer_radius + inner_radius * cos_theta) * cos_phi;
            float y = inner_radius * sin_theta;
            float z = (outer_radius + inner_radius * cos_theta) * sin_phi;

            float nx = cos_theta * cos_phi;
            float ny = sin_theta;
            float nz = cos_theta * sin_phi;

            Vertex v;
            v.position = glm::vec3(x, y, z);
            v.normal = glm::normalize(glm::vec3(nx, ny, nz));
            v.color = color;
            mesh.vertices.push_back(v);
        }
    }

    // CW winding (becomes CCW after Vulkan projection Y-flip)
    for (int ring = 0; ring < rings; ring++)
    {
        for (int side = 0; side < sides; side++)
        {
            uint32_t current = ring * (sides + 1) + side;
            uint32_t next = current + sides + 1;

            // Reversed winding order for CW
            mesh.indices.push_back(current);
            mesh.indices.push_back(current + 1);
            mesh.indices.push_back(next);

            mesh.indices.push_back(current + 1);
            mesh.indices.push_back(next + 1);
            mesh.indices.push_back(next);
        }
    }

    return mesh;
}

// Generate floor/mirror plane (XZ plane facing up)
// Uses CW winding which becomes CCW after Vulkan Y-flip
static MirrorMesh generate_mirror_plane(float size)
{
    MirrorMesh mesh;

    float half = size / 2.0f;
    glm::vec3 normal(0.0f, 1.0f, 0.0f);

    mesh.vertices.push_back({{-half, 0.0f, -half}, normal, {0.0f, 0.0f}});
    mesh.vertices.push_back({{half, 0.0f, -half}, normal, {1.0f, 0.0f}});
    mesh.vertices.push_back({{half, 0.0f, half}, normal, {1.0f, 1.0f}});
    mesh.vertices.push_back({{-half, 0.0f, half}, normal, {0.0f, 1.0f}});

    // CW winding (becomes CCW after projection Y flip)
    mesh.indices = {0, 2, 1, 0, 3, 2};

    return mesh;
}

// ============================================================================
// Offscreen Framebuffer
// ============================================================================

struct OffscreenFramebuffer
{
    hina_texture color; // Resolve target - sampled by mirror
    hina_texture depth; // Non-MSAA path only
    hina_texture msaa_color;
    hina_texture msaa_depth;
    uint32_t width;
    uint32_t height;
    bool use_msaa;
};

static bool offscreen_init(OffscreenFramebuffer *fb, uint32_t width, uint32_t height, bool use_msaa = true)
{
    fb->width = width;
    fb->height = height;
    fb->use_msaa = use_msaa;

    hina_texture_desc color_desc = hina_texture_desc_default();
    color_desc.format = HINA_FORMAT_R8G8B8A8_UNORM;
    color_desc.width = width;
    color_desc.height = height;
    color_desc.usage = static_cast<hina_texture_usage_flags>(
        HINA_TEXTURE_RENDER_TARGET_BIT | HINA_TEXTURE_SAMPLED_BIT);

    fb->color = hina_make_texture(&color_desc);
    if (!hina_texture_is_valid(fb->color))
    {
        EXAMPLE_LOGE("Failed to create offscreen color texture");
        return false;
    }

    if (use_msaa)
    {
        hina_texture_desc msaa_color_desc = hina_texture_desc_default();
        msaa_color_desc.format = HINA_FORMAT_R8G8B8A8_UNORM;
        msaa_color_desc.width = width;
        msaa_color_desc.height = height;
        msaa_color_desc.samples = HINA_SAMPLE_COUNT_4_BIT;
        msaa_color_desc.usage = HINA_TEXTURE_RENDER_TARGET_BIT;

        fb->msaa_color = hina_make_texture(&msaa_color_desc);
        if (!hina_texture_is_valid(fb->msaa_color))
        {
            EXAMPLE_LOGE("Failed to create MSAA color texture");
            hina_destroy_texture(fb->color);
            return false;
        }

        hina_texture_desc msaa_depth_desc = hina_texture_desc_default();
        msaa_depth_desc.format = HINA_FORMAT_D32_SFLOAT;
        msaa_depth_desc.width = width;
        msaa_depth_desc.height = height;
        msaa_depth_desc.samples = HINA_SAMPLE_COUNT_4_BIT;
        msaa_depth_desc.usage = HINA_TEXTURE_RENDER_TARGET_BIT;

        fb->msaa_depth = hina_make_texture(&msaa_depth_desc);
        if (!hina_texture_is_valid(fb->msaa_depth))
        {
            EXAMPLE_LOGE("Failed to create MSAA depth texture");
            hina_destroy_texture(fb->msaa_color);
            hina_destroy_texture(fb->color);
            return false;
        }
        EXAMPLE_LOGI("Created 4x MSAA offscreen buffers: %ux%u", width, height);
    }
    else
    {
        // Non-MSAA path: 1x depth buffer
        hina_texture_desc depth_desc = hina_texture_desc_default();
        depth_desc.format = HINA_FORMAT_D32_SFLOAT;
        depth_desc.width = width;
        depth_desc.height = height;
        depth_desc.usage = static_cast<hina_texture_usage_flags>(
            HINA_TEXTURE_RENDER_TARGET_BIT | HINA_TEXTURE_SAMPLED_BIT);

        fb->depth = hina_make_texture(&depth_desc);
        if (!hina_texture_is_valid(fb->depth))
        {
            EXAMPLE_LOGE("Failed to create offscreen depth texture");
            hina_destroy_texture(fb->color);
            return false;
        }
    }

    return true;
}

static void offscreen_destroy(OffscreenFramebuffer *fb)
{
    if (hina_texture_is_valid(fb->color))
        hina_destroy_texture(fb->color);
    if (hina_texture_is_valid(fb->depth))
        hina_destroy_texture(fb->depth);
    if (hina_texture_is_valid(fb->msaa_color))
        hina_destroy_texture(fb->msaa_color);
    if (hina_texture_is_valid(fb->msaa_depth))
        hina_destroy_texture(fb->msaa_depth);
    *fb = {};
}

// ============================================================================
// Application State
// ============================================================================

struct OffscreenApp
{
    // Mesh data
    Mesh model;
    MirrorMesh mirror;

    // Vulkan resources - Buffers
    hina_buffer model_vbo;
    hina_buffer model_ibo;
    hina_buffer mirror_vbo;
    hina_buffer mirror_ibo;
    hina_buffer offscreen_ubo;
    hina_buffer model_ubo;
    hina_buffer mirror_ubo;
    hina_buffer debug_ubo;

    // Mapped UBO pointers
    PhongUBO *offscreen_ubo_mapped;
    PhongUBO *model_ubo_mapped;
    MirrorUBO *mirror_ubo_mapped;
    DebugUBO *debug_ubo_mapped;

    // Offscreen framebuffer
    OffscreenFramebuffer offscreen;

    // Main depth buffer
    hina_depth_buffer main_depth;

    // Pipelines
    hina_pipeline offscreen_pipeline;    // 1x for non-MSAA path
    hina_pipeline offscreen_pipeline_4x; // 4x MSAA for offscreen rendering
    hina_pipeline model_pipeline;
    hina_pipeline mirror_pipeline;
    hina_pipeline debug_pipeline;

    // Bind group layouts
    hina_bind_group_layout scene_layout;
    hina_bind_group_layout mirror_layout;
    hina_bind_group_layout mirror_tex_layout;
    hina_bind_group_layout debug_tex_layout;

    // Sampler and bind groups
    hina_sampler offscreen_sampler;
    hina_bind_group mirror_tex_bind_group;
    hina_bind_group debug_tex_bind_group;

    // Camera and state
    float model_rotation;
    glm::vec3 model_position;
};

static OffscreenApp g_app = {};

// ============================================================================
// Initialization
// ============================================================================

static bool example_init(hina_example_app *app)
{
    EXAMPLE_LOGI("Initializing Offscreen Rendering example...");

    // Generate Meshes
    g_app.model = generate_torus(0.5f, 0.2f, 32, 16, glm::vec3(0.8f, 0.2f, 0.2f));
    g_app.mirror = generate_mirror_plane(3.0f);
    EXAMPLE_LOGI("Model: %zu vertices, %zu indices", g_app.model.vertices.size(), g_app.model.indices.size());
    EXAMPLE_LOGI("Mirror: %zu vertices, %zu indices", g_app.mirror.vertices.size(), g_app.mirror.indices.size());

    // Create Model VBO/IBO
    hina_buffer_desc model_vbo_desc = {0};
    model_vbo_desc.size = g_app.model.vertices.size() * sizeof(Vertex);
    model_vbo_desc.memory = HINA_BUFFER_CPU;
    model_vbo_desc.usage = HINA_BUFFER_VERTEX;
    model_vbo_desc.initial_data = g_app.model.vertices.data();

    g_app.model_vbo = hina_make_buffer(&model_vbo_desc);
    if (!hina_buffer_is_valid(g_app.model_vbo))
    {
        EXAMPLE_LOGE("Failed to create model VBO");
        return false;
    }

    hina_buffer_desc model_ibo_desc = {0};
    model_ibo_desc.size = g_app.model.indices.size() * sizeof(uint32_t);
    model_ibo_desc.memory = HINA_BUFFER_CPU;
    model_ibo_desc.usage = HINA_BUFFER_INDEX;
    model_ibo_desc.initial_data = g_app.model.indices.data();

    g_app.model_ibo = hina_make_buffer(&model_ibo_desc);
    if (!hina_buffer_is_valid(g_app.model_ibo))
    {
        EXAMPLE_LOGE("Failed to create model IBO");
        return false;
    }

    // Create Mirror VBO/IBO
    hina_buffer_desc mirror_vbo_desc = {0};
    mirror_vbo_desc.size = g_app.mirror.vertices.size() * sizeof(MirrorVertex);
    mirror_vbo_desc.memory = HINA_BUFFER_CPU;
    mirror_vbo_desc.usage = HINA_BUFFER_VERTEX;
    mirror_vbo_desc.initial_data = g_app.mirror.vertices.data();

    g_app.mirror_vbo = hina_make_buffer(&mirror_vbo_desc);
    if (!hina_buffer_is_valid(g_app.mirror_vbo))
    {
        EXAMPLE_LOGE("Failed to create mirror VBO");
        return false;
    }

    hina_buffer_desc mirror_ibo_desc = {0};
    mirror_ibo_desc.size = g_app.mirror.indices.size() * sizeof(uint32_t);
    mirror_ibo_desc.memory = HINA_BUFFER_CPU;
    mirror_ibo_desc.usage = HINA_BUFFER_INDEX;
    mirror_ibo_desc.initial_data = g_app.mirror.indices.data();

    g_app.mirror_ibo = hina_make_buffer(&mirror_ibo_desc);
    if (!hina_buffer_is_valid(g_app.mirror_ibo))
    {
        EXAMPLE_LOGE("Failed to create mirror IBO");
        return false;
    }

    // Create Uniform Buffers
    hina_buffer_desc offscreen_ubo_desc = {0};
    offscreen_ubo_desc.size = sizeof(PhongUBO);
    offscreen_ubo_desc.memory = HINA_BUFFER_CPU;
    offscreen_ubo_desc.usage = HINA_BUFFER_UNIFORM;

    g_app.offscreen_ubo = hina_make_buffer(&offscreen_ubo_desc);
    if (!hina_buffer_is_valid(g_app.offscreen_ubo))
    {
        EXAMPLE_LOGE("Failed to create offscreen UBO");
        return false;
    }
    g_app.offscreen_ubo_mapped = static_cast<PhongUBO *>(hina_mapped_buffer_ptr(g_app.offscreen_ubo));

    hina_buffer_desc model_ubo_desc = {0};
    model_ubo_desc.size = sizeof(PhongUBO);
    model_ubo_desc.memory = HINA_BUFFER_CPU;
    model_ubo_desc.usage = HINA_BUFFER_UNIFORM;

    g_app.model_ubo = hina_make_buffer(&model_ubo_desc);
    if (!hina_buffer_is_valid(g_app.model_ubo))
    {
        EXAMPLE_LOGE("Failed to create model UBO");
        return false;
    }
    g_app.model_ubo_mapped = static_cast<PhongUBO *>(hina_mapped_buffer_ptr(g_app.model_ubo));

    hina_buffer_desc mirror_ubo_desc = {0};
    mirror_ubo_desc.size = sizeof(MirrorUBO);
    mirror_ubo_desc.memory = HINA_BUFFER_CPU;
    mirror_ubo_desc.usage = HINA_BUFFER_UNIFORM;

    g_app.mirror_ubo = hina_make_buffer(&mirror_ubo_desc);
    if (!hina_buffer_is_valid(g_app.mirror_ubo))
    {
        EXAMPLE_LOGE("Failed to create mirror UBO");
        return false;
    }
    g_app.mirror_ubo_mapped = static_cast<MirrorUBO *>(hina_mapped_buffer_ptr(g_app.mirror_ubo));

    hina_buffer_desc debug_ubo_desc = {0};
    debug_ubo_desc.size = sizeof(DebugUBO);
    debug_ubo_desc.memory = HINA_BUFFER_CPU;
    debug_ubo_desc.usage = HINA_BUFFER_UNIFORM;

    g_app.debug_ubo = hina_make_buffer(&debug_ubo_desc);
    if (!hina_buffer_is_valid(g_app.debug_ubo))
    {
        EXAMPLE_LOGE("Failed to create debug UBO");
        return false;
    }
    g_app.debug_ubo_mapped = static_cast<DebugUBO *>(hina_mapped_buffer_ptr(g_app.debug_ubo));

    // Create Offscreen Framebuffer
    if (!offscreen_init(&g_app.offscreen, OFFSCREEN_WIDTH, OFFSCREEN_HEIGHT))
    {
        EXAMPLE_LOGE("Failed to create offscreen framebuffer");
        return false;
    }

    // Create Main Depth Buffer
    if (!hina_depth_buffer_init(&g_app.main_depth, app->width, app->height))
    {
        EXAMPLE_LOGE("Failed to create main depth buffer");
        return false;
    }

    // Create Pipelines
    char *error = nullptr;

    // Phong pipeline vertex layout
    hina_vertex_layout model_layout = {};
    model_layout.buffer_count = 1;
    model_layout.buffer_strides[0] = sizeof(Vertex);
    model_layout.input_rates[0] = HINA_VERTEX_INPUT_RATE_VERTEX;
    model_layout.attr_count = 3;
    model_layout.attrs[0] = {HINA_FORMAT_R32G32B32_SFLOAT, offsetof(Vertex, position), 0, 0};
    model_layout.attrs[1] = {HINA_FORMAT_R32G32B32_SFLOAT, offsetof(Vertex, normal), 1, 0};
    model_layout.attrs[2] = {HINA_FORMAT_R32G32B32_SFLOAT, offsetof(Vertex, color), 2, 0};

    char *phong_shader_path = hina_example_shader_path(app, "phong.hina_sl");

    // Offscreen phong pipeline (front face culling for mirrored/reflected geometry)
    hina_hsl_pipeline_desc offscreen_pip_desc = hina_hsl_pipeline_desc_default();
    offscreen_pip_desc.layout = model_layout;
    offscreen_pip_desc.front_face = HINA_FRONT_FACE_COUNTER_CLOCKWISE;
    offscreen_pip_desc.cull_mode = HINA_CULL_MODE_FRONT;
    offscreen_pip_desc.color_formats[0] = HINA_FORMAT_R8G8B8A8_UNORM;
    offscreen_pip_desc.depth_format = HINA_FORMAT_D32_SFLOAT;

    g_app.offscreen_pipeline = hina_example_make_pipeline_from_hsl(app, phong_shader_path, &offscreen_pip_desc, &error);
    if (!hina_pipeline_is_valid(g_app.offscreen_pipeline))
    {
        EXAMPLE_LOGE("Offscreen pipeline failed: %s", error ? error : "Unknown");
        if (error)
            hslc_free_log(error);
        free(phong_shader_path);
        return false;
    }

    // 4x MSAA offscreen pipeline
    hina_hsl_pipeline_desc offscreen_pip_4x_desc = offscreen_pip_desc;
    offscreen_pip_4x_desc.samples = HINA_SAMPLE_COUNT_4_BIT;

    g_app.offscreen_pipeline_4x = hina_example_make_pipeline_from_hsl(app, phong_shader_path, &offscreen_pip_4x_desc, &error);
    if (!hina_pipeline_is_valid(g_app.offscreen_pipeline_4x))
    {
        EXAMPLE_LOGE("Offscreen 4x MSAA pipeline failed: %s", error ? error : "Unknown");
        if (error)
            hslc_free_log(error);
        free(phong_shader_path);
        return false;
    }

    // Main scene phong pipeline (normal back-face culling)
    hina_hsl_pipeline_desc model_pip_desc = hina_hsl_pipeline_desc_default();
    model_pip_desc.layout = model_layout;
    model_pip_desc.front_face = HINA_FRONT_FACE_COUNTER_CLOCKWISE;
    model_pip_desc.cull_mode = HINA_CULL_MODE_BACK;
    model_pip_desc.color_formats[0] = hina_get_surface_format();
    model_pip_desc.depth_format = HINA_FORMAT_D32_SFLOAT;

    g_app.model_pipeline = hina_example_make_pipeline_from_hsl(app, phong_shader_path, &model_pip_desc, &error);
    free(phong_shader_path);

    if (!hina_pipeline_is_valid(g_app.model_pipeline))
    {
        EXAMPLE_LOGE("Model pipeline failed: %s", error ? error : "Unknown");
        if (error)
            hslc_free_log(error);
        return false;
    }

    // Mirror pipeline
    hina_vertex_layout mirror_layout1 = {};
    mirror_layout1.buffer_count = 1;
    mirror_layout1.buffer_strides[0] = sizeof(MirrorVertex);
    mirror_layout1.input_rates[0] = HINA_VERTEX_INPUT_RATE_VERTEX;
    mirror_layout1.attr_count = 3;
    mirror_layout1.attrs[0] = {HINA_FORMAT_R32G32B32_SFLOAT, offsetof(MirrorVertex, position), 0, 0};
    mirror_layout1.attrs[1] = {HINA_FORMAT_R32G32B32_SFLOAT, offsetof(MirrorVertex, normal), 1, 0};
    mirror_layout1.attrs[2] = {HINA_FORMAT_R32G32_SFLOAT, offsetof(MirrorVertex, uv), 2, 0};

    char *mirror_shader_path = hina_example_shader_path(app, "mirror.hina_sl");

    hina_hsl_pipeline_desc mirror_pip_desc = hina_hsl_pipeline_desc_default();
    mirror_pip_desc.layout = mirror_layout1;
    mirror_pip_desc.front_face = HINA_FRONT_FACE_COUNTER_CLOCKWISE;
    mirror_pip_desc.cull_mode = HINA_CULL_MODE_BACK;
    mirror_pip_desc.color_formats[0] = hina_get_surface_format();
    mirror_pip_desc.depth_format = HINA_FORMAT_D32_SFLOAT;

    g_app.mirror_pipeline = hina_example_make_pipeline_from_hsl(app, mirror_shader_path, &mirror_pip_desc, &error);
    free(mirror_shader_path);

    if (!hina_pipeline_is_valid(g_app.mirror_pipeline))
    {
        EXAMPLE_LOGE("Mirror pipeline failed: %s", error ? error : "Unknown");
        if (error)
            hslc_free_log(error);
        return false;
    }

    // Debug pipeline
    hina_vertex_layout debug_layout = {};

    char *debug_shader_path = hina_example_shader_path(app, "debug.hina_sl");

    hina_hsl_pipeline_desc debug_pip_desc = hina_hsl_pipeline_desc_default();
    debug_pip_desc.layout = debug_layout;
    debug_pip_desc.cull_mode = HINA_CULL_MODE_NONE;
    debug_pip_desc.color_formats[0] = hina_get_surface_format();
    debug_pip_desc.depth.depth_test = false;
    debug_pip_desc.depth.depth_write = false;

    g_app.debug_pipeline = hina_example_make_pipeline_from_hsl(app, debug_shader_path, &debug_pip_desc, &error);
    free(debug_shader_path);

    if (!hina_pipeline_is_valid(g_app.debug_pipeline))
    {
        EXAMPLE_LOGE("Debug pipeline failed: %s", error ? error : "Unknown");
        if (error)
            hslc_free_log(error);
        return false;
    }

    EXAMPLE_LOGI("All pipelines created successfully");

    // Get bind group layouts
    g_app.scene_layout = hina_pipeline_get_bind_group_layout(g_app.offscreen_pipeline, 0);
    g_app.mirror_layout = hina_pipeline_get_bind_group_layout(g_app.mirror_pipeline, 0);
    g_app.mirror_tex_layout = hina_pipeline_get_bind_group_layout(g_app.mirror_pipeline, 1);
    g_app.debug_tex_layout = hina_pipeline_get_bind_group_layout(g_app.debug_pipeline, 0);

    // Create Sampler
    hina_sampler_desc offscreen_sampler_desc = hina_sampler_desc_default();
    offscreen_sampler_desc.min_filter = HINA_FILTER_LINEAR;
    offscreen_sampler_desc.mag_filter = HINA_FILTER_LINEAR;
    offscreen_sampler_desc.address_u = HINA_ADDRESS_CLAMP_TO_EDGE;
    offscreen_sampler_desc.address_v = HINA_ADDRESS_CLAMP_TO_EDGE;

    g_app.offscreen_sampler = hina_make_sampler(&offscreen_sampler_desc);
    if (!hina_sampler_is_valid(g_app.offscreen_sampler))
    {
        EXAMPLE_LOGE("Failed to create offscreen sampler");
        return false;
    }

    // Create Bind Groups
    hina_texture_view offscreen_view = hina_texture_get_default_view(g_app.offscreen.color);

    hina_bind_group_entry mirror_tex_entry = {};
    mirror_tex_entry.binding = 0;
    mirror_tex_entry.type = HINA_DESC_TYPE_COMBINED_IMAGE_SAMPLER;
    mirror_tex_entry.combined.view = offscreen_view;
    mirror_tex_entry.combined.sampler = g_app.offscreen_sampler;

    hina_bind_group_desc mirror_tex_group_desc = {};
    mirror_tex_group_desc.layout = g_app.mirror_tex_layout;
    mirror_tex_group_desc.entries = &mirror_tex_entry;
    mirror_tex_group_desc.entry_count = 1;
    mirror_tex_group_desc.label = "mirror_reflection";

    g_app.mirror_tex_bind_group = hina_create_bind_group(&mirror_tex_group_desc);

    hina_bind_group_entry debug_tex_entry = {};
    debug_tex_entry.binding = 0;
    debug_tex_entry.type = HINA_DESC_TYPE_COMBINED_IMAGE_SAMPLER;
    debug_tex_entry.combined.view = offscreen_view;
    debug_tex_entry.combined.sampler = g_app.offscreen_sampler;

    hina_bind_group_desc debug_tex_group_desc = {};
    debug_tex_group_desc.layout = g_app.debug_tex_layout;
    debug_tex_group_desc.entries = &debug_tex_entry;
    debug_tex_group_desc.entry_count = 1;
    debug_tex_group_desc.label = "debug_texture";

    g_app.debug_tex_bind_group = hina_create_bind_group(&debug_tex_group_desc);

    // Setup Camera and State
    app->camera.rotation = glm::vec3(25.0f, 0.0f, 0.0f);
    app->camera.zoom = -4.5f;

    g_app.model_rotation = 0.0f;
    g_app.model_position = glm::vec3(0.0f, 1.5f, 0.0f);

    EXAMPLE_LOGI("Offscreen rendering example initialized");

    return true;
}

// ============================================================================
// Rendering
// ============================================================================

static void example_render(hina_example_app *app)
{
    g_app.model_rotation += 45.0f * app->delta_time;
    app->camera.update(*app);

    hina_swapchain_image swapchain = hina_frame_begin();
    if (swapchain.texture.id == HINA_INVALID_HANDLE)
    {
        hina_example_try_recover_surface(app);
        hina_frame_end();
        return;
    }

    uint32_t w, h;
    hina_get_texture_size(swapchain.texture, &w, &h);

    if (!hina_depth_buffer_resize(&g_app.main_depth, w, h))
    {
        EXAMPLE_LOGE("Failed to resize depth buffer");
        hina_frame_end();
        return;
    }

    float aspect = static_cast<float>(w) / static_cast<float>(h);

    // Light position
    glm::vec4 light_pos(0.0f, 5.0f, 5.0f, 1.0f);

    // View and projection for main pass
    glm::mat4 view = app->camera.view_matrix();
    glm::mat4 projection = glm::perspective(glm::radians(60.0f), aspect, 0.1f, 256.0f);
    projection[1][1] *= -1.0f; // Vulkan Y-flip

    // Reflection matrix across Y=0 plane
    glm::mat4 reflect_y = glm::scale(glm::mat4(1.0f), glm::vec3(1.0f, -1.0f, 1.0f));
    glm::mat4 reflected_view = view * reflect_y;
    glm::mat4 reflected_vp = projection * reflected_view;

    // Model transform for the actual torus
    glm::mat4 model_matrix = glm::mat4(1.0f);
    model_matrix = glm::translate(model_matrix, g_app.model_position);
    model_matrix = glm::rotate(model_matrix, glm::radians(g_app.model_rotation), glm::vec3(0.0f, 1.0f, 0.0f));

    // Update Uniform Buffers
    g_app.offscreen_ubo_mapped->projection = projection;
    g_app.offscreen_ubo_mapped->view = reflected_view;
    g_app.offscreen_ubo_mapped->model = model_matrix;
    g_app.offscreen_ubo_mapped->light_pos = light_pos;

    g_app.model_ubo_mapped->projection = projection;
    g_app.model_ubo_mapped->view = view;
    g_app.model_ubo_mapped->model = model_matrix;
    g_app.model_ubo_mapped->light_pos = light_pos;

    g_app.mirror_ubo_mapped->projection = projection;
    g_app.mirror_ubo_mapped->view = view;
    g_app.mirror_ubo_mapped->model = glm::mat4(1.0f);
    g_app.mirror_ubo_mapped->reflected_vp = reflected_vp;

    g_app.debug_ubo_mapped->texture_id = 0;

    // Begin Command Buffer
    hina_cmd *cmd = hina_cmd_begin_ex(HINA_QUEUE_GRAPHICS);
    if (!cmd)
    {
        hina_frame_end();
        return;
    }

    // Pass 1: Offscreen Rendering (Reflected Model)
    hina_pass_action offscreen_pass = {};
    if (g_app.offscreen.use_msaa)
    {
        // MSAA path: render to 4x buffer, resolve to 1x color target
        offscreen_pass.colors[0].image = hina_texture_get_default_view(g_app.offscreen.msaa_color);
        offscreen_pass.colors[0].resolve = hina_texture_get_default_view(g_app.offscreen.color);
        offscreen_pass.colors[0].load_op = HINA_LOAD_OP_CLEAR;
        offscreen_pass.colors[0].store_op = HINA_STORE_OP_DONT_CARE;
        offscreen_pass.colors[0].clear_color[3] = 1.0f;
        offscreen_pass.depth.image = hina_texture_get_default_view(g_app.offscreen.msaa_depth);
    }
    else
    {
        offscreen_pass.colors[0].image = hina_texture_get_default_view(g_app.offscreen.color);
        offscreen_pass.colors[0].load_op = HINA_LOAD_OP_CLEAR;
        offscreen_pass.colors[0].store_op = HINA_STORE_OP_STORE;
        offscreen_pass.colors[0].clear_color[3] = 1.0f;
        offscreen_pass.depth.image = hina_texture_get_default_view(g_app.offscreen.depth);
    }
    offscreen_pass.depth.load_op = HINA_LOAD_OP_CLEAR;
    offscreen_pass.depth.store_op = HINA_STORE_OP_DONT_CARE;
    offscreen_pass.depth.depth_clear = 1.0f;
    offscreen_pass.width = OFFSCREEN_WIDTH;
    offscreen_pass.height = OFFSCREEN_HEIGHT;

    hina_bind_group_entry offscreen_entry = {};
    offscreen_entry.binding = 0;
    offscreen_entry.type = HINA_DESC_TYPE_UNIFORM_BUFFER;
    offscreen_entry.buffer.buffer = g_app.offscreen_ubo;
    offscreen_entry.buffer.size = sizeof(PhongUBO);

    hina_bind_group_desc offscreen_group_desc = {};
    offscreen_group_desc.layout = g_app.scene_layout;
    offscreen_group_desc.entries = &offscreen_entry;
    offscreen_group_desc.entry_count = 1;

    hina_transient_bind_group offscreen_group = hina_example_make_transient_bind_group(&offscreen_group_desc);

    hina_cmd_begin_pass(cmd, &offscreen_pass);
    hina_cmd_bind_pipeline(cmd, g_app.offscreen.use_msaa ? g_app.offscreen_pipeline_4x : g_app.offscreen_pipeline);
    hina_cmd_bind_transient_group(cmd, 0, offscreen_group);

    hina_vertex_input model_bindings = {};
    model_bindings.vertex_buffers[0] = g_app.model_vbo;
    model_bindings.index_buffer = g_app.model_ibo;
    model_bindings.index_type = HINA_INDEX_UINT32;

    hina_cmd_apply_vertex_input(cmd, &model_bindings);
    hina_cmd_draw_indexed(cmd, static_cast<uint32_t>(g_app.model.indices.size()), 1, 0, 0, 0);
    hina_cmd_end_pass(cmd);

    hina_cmd_transition_texture(cmd, g_app.offscreen.color, HINA_TEXSTATE_SHADER_READ);

    // Pass 2: Main Scene Rendering
    hina_pass_action main_pass = {};
    main_pass.colors[0].image = hina_texture_get_default_view(swapchain.texture);
    main_pass.colors[0].load_op = HINA_LOAD_OP_CLEAR;
    main_pass.colors[0].store_op = HINA_STORE_OP_STORE;
    main_pass.colors[0].clear_color[0] = 0.1f;
    main_pass.colors[0].clear_color[1] = 0.1f;
    main_pass.colors[0].clear_color[2] = 0.15f;
    main_pass.colors[0].clear_color[3] = 1.0f;
    main_pass.depth.image = hina_texture_get_default_view(g_app.main_depth.texture);
    main_pass.depth.load_op = HINA_LOAD_OP_CLEAR;
    main_pass.depth.store_op = HINA_STORE_OP_DONT_CARE;
    main_pass.depth.depth_clear = 1.0f;

    hina_cmd_begin_pass(cmd, &main_pass);

    // Draw mirror plane
    hina_bind_group_entry mirror_entry = {};
    mirror_entry.binding = 0;
    mirror_entry.type = HINA_DESC_TYPE_UNIFORM_BUFFER;
    mirror_entry.buffer.buffer = g_app.mirror_ubo;
    mirror_entry.buffer.size = sizeof(MirrorUBO);

    hina_bind_group_desc mirror_group_desc = {};
    mirror_group_desc.layout = g_app.mirror_layout;
    mirror_group_desc.entries = &mirror_entry;
    mirror_group_desc.entry_count = 1;

    hina_transient_bind_group mirror_group = hina_example_make_transient_bind_group(&mirror_group_desc);

    hina_cmd_bind_pipeline(cmd, g_app.mirror_pipeline);
    hina_cmd_bind_transient_group(cmd, 0, mirror_group);
    hina_cmd_bind_group(cmd, 1, g_app.mirror_tex_bind_group);

    hina_vertex_input mirror_bindings = {};
    mirror_bindings.vertex_buffers[0] = g_app.mirror_vbo;
    mirror_bindings.index_buffer = g_app.mirror_ibo;
    mirror_bindings.index_type = HINA_INDEX_UINT32;

    hina_cmd_apply_vertex_input(cmd, &mirror_bindings);
    hina_cmd_draw_indexed(cmd, static_cast<uint32_t>(g_app.mirror.indices.size()), 1, 0, 0, 0);

    // Draw model
    hina_bind_group_entry model_entry = {};
    model_entry.binding = 0;
    model_entry.type = HINA_DESC_TYPE_UNIFORM_BUFFER;
    model_entry.buffer.buffer = g_app.model_ubo;
    model_entry.buffer.size = sizeof(PhongUBO);

    hina_bind_group_desc model_group_desc = {};
    model_group_desc.layout = g_app.scene_layout;
    model_group_desc.entries = &model_entry;
    model_group_desc.entry_count = 1;

    hina_transient_bind_group model_group = hina_example_make_transient_bind_group(&model_group_desc);

    hina_cmd_bind_pipeline(cmd, g_app.model_pipeline);
    hina_cmd_bind_transient_group(cmd, 0, model_group);
    hina_cmd_apply_vertex_input(cmd, &model_bindings);
    hina_cmd_draw_indexed(cmd, static_cast<uint32_t>(g_app.model.indices.size()), 1, 0, 0, 0);

    // Present frame (ends pass, renders ImGui, submits, ends frame)
    hina_example_present_frame(app, cmd, swapchain);
}

// ============================================================================
// Cleanup
// ============================================================================

static void example_cleanup(hina_example_app *app)
{
    (void)app;
    EXAMPLE_LOGI("Cleaning up Offscreen Rendering example...");

    if (hina_bind_group_is_valid(g_app.debug_tex_bind_group))
        hina_destroy_bind_group(g_app.debug_tex_bind_group);
    if (hina_bind_group_is_valid(g_app.mirror_tex_bind_group))
        hina_destroy_bind_group(g_app.mirror_tex_bind_group);
    if (hina_sampler_is_valid(g_app.offscreen_sampler))
        hina_destroy_sampler(g_app.offscreen_sampler);
    if (hina_pipeline_is_valid(g_app.debug_pipeline))
        hina_destroy_pipeline(g_app.debug_pipeline);
    if (hina_pipeline_is_valid(g_app.mirror_pipeline))
        hina_destroy_pipeline(g_app.mirror_pipeline);
    if (hina_pipeline_is_valid(g_app.model_pipeline))
        hina_destroy_pipeline(g_app.model_pipeline);
    if (hina_pipeline_is_valid(g_app.offscreen_pipeline))
        hina_destroy_pipeline(g_app.offscreen_pipeline);
    if (hina_pipeline_is_valid(g_app.offscreen_pipeline_4x))
        hina_destroy_pipeline(g_app.offscreen_pipeline_4x);
    hina_depth_buffer_destroy(&g_app.main_depth);
    offscreen_destroy(&g_app.offscreen);
    if (hina_buffer_is_valid(g_app.debug_ubo))
        hina_destroy_buffer(g_app.debug_ubo);
    if (hina_buffer_is_valid(g_app.mirror_ubo))
        hina_destroy_buffer(g_app.mirror_ubo);
    if (hina_buffer_is_valid(g_app.model_ubo))
        hina_destroy_buffer(g_app.model_ubo);
    if (hina_buffer_is_valid(g_app.offscreen_ubo))
        hina_destroy_buffer(g_app.offscreen_ubo);
    if (hina_buffer_is_valid(g_app.mirror_ibo))
        hina_destroy_buffer(g_app.mirror_ibo);
    if (hina_buffer_is_valid(g_app.mirror_vbo))
        hina_destroy_buffer(g_app.mirror_vbo);
    if (hina_buffer_is_valid(g_app.model_ibo))
        hina_destroy_buffer(g_app.model_ibo);
    if (hina_buffer_is_valid(g_app.model_vbo))
        hina_destroy_buffer(g_app.model_vbo);

    g_app = {};
    EXAMPLE_LOGI("Cleanup complete");
}

// ============================================================================
// Entry Point
// ============================================================================

HINA_EXAMPLE_MAIN("HinaVK Offscreen Rendering", example_init, example_render, example_cleanup)
