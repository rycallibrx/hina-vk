/**
 * HinaVK Compute Particles Example - Cross-Platform
 *
 * This example demonstrates GPU compute-based particle simulation with:
 * - Compute shader for particle physics simulation
 * - Storage buffers used as both compute output and vertex input
 * - Memory barriers for compute-to-graphics synchronization
 * - Point rendering with additive blending
 * - Toggle between touch-controlled attractor and automatic animation
 *
 * Based on https://github.com/SaschaWillems/Vulkan/blob/master/examples/computeparticles/computeparticles.cpp
 *
 * Works on both Desktop (SDL) and Android (NativeActivity).
 */

#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <cmath>
#include <random>
#include <vector>

#include "../hina_example.h"

// ============================================================================
// Configuration
// ============================================================================

#ifdef __ANDROID__
constexpr uint32_t PARTICLE_COUNT = 256 * 1024;  // Lower count for mobile
#else
constexpr uint32_t PARTICLE_COUNT = 256 * 1024 * 4;
#endif

constexpr uint32_t WORKGROUP_SIZE = 256;

// ============================================================================
// Data Structures (must match shader)
// ============================================================================

struct Particle {
    glm::vec2 pos;
    glm::vec2 vel;
    glm::vec4 gradientPos;  // x = gradient value for color animation
};

struct ComputeUBO {
    float deltaT;
    float destX;
    float destY;
    int particleCount;
};

// ============================================================================
// Particle Initialization
// ============================================================================

static void initParticles(std::vector<Particle>& particles) {
    particles.resize(PARTICLE_COUNT);

    std::random_device rd;
    std::mt19937 gen(rd());
    std::uniform_real_distribution<float> rndDist(-1.0f, 1.0f);

    for (uint32_t i = 0; i < PARTICLE_COUNT; i++) {
        // Random position in [-1, 1] range (matching original demo)
        particles[i].pos = glm::vec2(rndDist(gen), rndDist(gen));

        // Zero initial velocity (matching original demo)
        particles[i].vel = glm::vec2(0.0f);

        // Random hue for each particle (stored in gradientPos.x)
        std::uniform_real_distribution<float> hueDist(0.0f, 1.0f);
        particles[i].gradientPos = glm::vec4(hueDist(gen), 0.0f, 0.0f, 0.0f);
    }
}

// ============================================================================
// Application State
// ============================================================================

struct ComputeParticlesApp {
    // Vulkan resources
    hina_buffer ssbo_in;
    hina_buffer ssbo_out;
    hina_buffer compute_ubo;
    hina_pipeline compute_pipeline;
    hina_pipeline graphics_pipeline;
    hina_bind_group_layout compute_layout;

    // Mapped buffer pointers
    ComputeUBO* ubo;

    // Ping-pong state
    bool pingPong;

    // Attractor state
    bool attachToCursor;  // false = automatic animation
    float animTimer;
    float animStart;
};

static ComputeParticlesApp g_app = {};

// ============================================================================
// Initialization
// ============================================================================

static bool example_init(hina_example_app* app) {
    EXAMPLE_LOGI("Initializing Compute Particles example...");

    // Initialize shader compiler
    if (!hslc_init(nullptr)) {
        EXAMPLE_LOGE("Failed to initialize shader compiler");
        return false;
    }

    // Create Particle Storage Buffers (double-buffered)
    std::vector<Particle> initialParticles;
    initParticles(initialParticles);

    // Storage buffers that can also be used as vertex buffers
    hina_buffer_desc ssbo_desc = {0};
    ssbo_desc.size = sizeof(Particle) * PARTICLE_COUNT;
    ssbo_desc.flags = static_cast<hina_buffer_flags>(
        HINA_BUFFER_STORAGE_BIT | HINA_BUFFER_VERTEX_BIT |
        HINA_BUFFER_TRANSFER_DST_BIT | HINA_BUFFER_DEVICE_LOCAL_BIT);
    ssbo_desc.initial_data = initialParticles.data();

    g_app.ssbo_in = hina_make_buffer(&ssbo_desc);
    if (!hina_buffer_is_valid(g_app.ssbo_in)) {
        EXAMPLE_LOGE("Failed to create input storage buffer");
        return false;
    }

    g_app.ssbo_out = hina_make_buffer(&ssbo_desc);
    if (!hina_buffer_is_valid(g_app.ssbo_out)) {
        EXAMPLE_LOGE("Failed to create output storage buffer");
        return false;
    }

    // Create Compute UBO
    hina_buffer_desc ubo_desc = {0};
    ubo_desc.size = sizeof(ComputeUBO);
    ubo_desc.flags = static_cast<hina_buffer_flags>(
        HINA_BUFFER_UNIFORM_BIT | HINA_BUFFER_HOST_VISIBLE_BIT | HINA_BUFFER_HOST_COHERENT_BIT);

    g_app.compute_ubo = hina_make_buffer(&ubo_desc);
    if (!hina_buffer_is_valid(g_app.compute_ubo)) {
        EXAMPLE_LOGE("Failed to create compute UBO");
        return false;
    }

    g_app.ubo = static_cast<ComputeUBO*>(hina_map_buffer(g_app.compute_ubo));
    if (!g_app.ubo) {
        EXAMPLE_LOGE("Failed to map compute UBO");
        return false;
    }

    // Create Compute Pipeline (HSL module)
    char* comp_shader_path = hina_example_shader_path(app, "particle_compute.hina_sl");
    char* comp_error = nullptr;

    hina_hsl_pipeline_desc comp_pip_desc = hina_hsl_pipeline_desc_default();

    g_app.compute_pipeline = hina_example_make_pipeline_from_hsl(app, comp_shader_path, &comp_pip_desc, &comp_error);
    free(comp_shader_path);

    if (!hina_pipeline_is_valid(g_app.compute_pipeline)) {
        EXAMPLE_LOGE("Compute pipeline creation failed: %s", comp_error ? comp_error : "Unknown");
        if (comp_error) hslc_free_log(comp_error);
        return false;
    }

    g_app.compute_layout = hina_pipeline_get_bind_group_layout(g_app.compute_pipeline, 0);
    if (!hina_bind_group_layout_is_valid(g_app.compute_layout)) {
        EXAMPLE_LOGE("Failed to get compute bind group layout");
        return false;
    }

    // Create Graphics Pipeline
    char* gfx_shader_path = hina_example_shader_path(app, "particle.hina_sl");
    char* gfx_error = nullptr;

    // Vertex layout matches Particle struct (pos, vel, hue used by vertex shader)
    hina_vertex_layout vertex_layout = {};
    vertex_layout.buffer_count = 1;
    vertex_layout.buffer_strides[0] = sizeof(Particle);
    vertex_layout.input_rates[0] = HINA_VERTEX_INPUT_RATE_VERTEX;
    vertex_layout.attr_count = 3;
    vertex_layout.attrs[0] = { HINA_FORMAT_R32G32_SFLOAT, offsetof(Particle, pos), 0, 0 };
    vertex_layout.attrs[1] = { HINA_FORMAT_R32G32_SFLOAT, offsetof(Particle, vel), 1, 0 };
    vertex_layout.attrs[2] = { HINA_FORMAT_R32G32B32A32_SFLOAT, offsetof(Particle, gradientPos), 2, 0 };

    hina_hsl_pipeline_desc gfx_pip_desc = hina_hsl_pipeline_desc_default();
    gfx_pip_desc.primitive_topology = HINA_PRIMITIVE_TOPOLOGY_POINT_LIST;
    gfx_pip_desc.cull_mode = HINA_CULL_MODE_NONE;
    gfx_pip_desc.layout = vertex_layout;

    // Additive blending for glowing particles
    gfx_pip_desc.blend[0].enable = true;
    gfx_pip_desc.blend[0].src_color = HINA_BLEND_FACTOR_SRC_ALPHA;
    gfx_pip_desc.blend[0].dst_color = HINA_BLEND_FACTOR_ONE;  // Additive
    gfx_pip_desc.blend[0].src_alpha = HINA_BLEND_FACTOR_ONE;
    gfx_pip_desc.blend[0].dst_alpha = HINA_BLEND_FACTOR_ONE;

    // No depth testing for particles
    gfx_pip_desc.depth.depth_test = false;
    gfx_pip_desc.depth.depth_write = false;

    g_app.graphics_pipeline = hina_example_make_pipeline_from_hsl(app, gfx_shader_path, &gfx_pip_desc, &gfx_error);
    free(gfx_shader_path);

    if (!hina_pipeline_is_valid(g_app.graphics_pipeline)) {
        EXAMPLE_LOGE("Graphics pipeline creation failed: %s", gfx_error ? gfx_error : "Unknown");
        if (gfx_error) hslc_free_log(gfx_error);
        return false;
    }

    // Initialize state
    g_app.pingPong = false;
    g_app.attachToCursor = false;  // Start with automatic animation
    g_app.animTimer = 0.0f;
    g_app.animStart = 20.0f;  // Delay before animation starts (matching original)

    EXAMPLE_LOGI("Compute Particles example initialized");
    EXAMPLE_LOGI("Rendering %u particles", PARTICLE_COUNT);
    EXAMPLE_LOGI("Controls:");
    EXAMPLE_LOGI("  SPACE/TAP: Toggle attractor mode (touch vs automatic)");
    EXAMPLE_LOGI("  Touch: Move attractor point (when in touch mode)");

    return true;
}

// ============================================================================
// Rendering
// ============================================================================

static void example_render(hina_example_app* app) {
    // Check for space key to toggle attractor mode
    if (app->key_space) {
        g_app.attachToCursor = !g_app.attachToCursor;
        EXAMPLE_LOGI("Attractor mode: %s", g_app.attachToCursor ? "TOUCH" : "AUTOMATIC");
    }

    // Begin frame (acquires swapchain image)
    hina_swapchain_image swapchain = hina_frame_begin();
    if (swapchain.texture.id == HINA_INVALID_HANDLE) {
        hina_frame_end();
        return;
    }

    // Get actual swapchain dimensions
    uint32_t w, h;
    hina_get_texture_size(swapchain.texture, &w, &h);

    // Calculate attractor position
    float destX, destY;
    if (g_app.attachToCursor) {
        // Touch/mouse-attached mode: center screen at origin, scale to [-1, 1]
        destX = (app->input_x - static_cast<float>(w / 2)) / static_cast<float>(w / 2);
        destY = (app->input_y - static_cast<float>(h / 2)) / static_cast<float>(h / 2);
    } else {
        // Automatic animation: sine wave motion (matching original demo)
        if (g_app.animStart > 0.0f) {
            g_app.animStart -= app->delta_time;
            destX = 0.0f;
            destY = 0.0f;
        } else {
            g_app.animTimer += app->delta_time * 0.04f;
            if (g_app.animTimer > 1.0f) g_app.animTimer = 0.0f;
            destX = sin(glm::radians(g_app.animTimer * 360.0f)) * 0.75f;
            destY = 0.0f;
        }
    }

    // Update compute UBO
    g_app.ubo->deltaT = app->delta_time * 2.5f;
    g_app.ubo->destX = destX;
    g_app.ubo->destY = destY;
    g_app.ubo->particleCount = PARTICLE_COUNT;

    // Select ping-pong buffers
    hina_buffer currentIn = g_app.pingPong ? g_app.ssbo_out : g_app.ssbo_in;
    hina_buffer currentOut = g_app.pingPong ? g_app.ssbo_in : g_app.ssbo_out;

    // Begin command buffer
    hina_cmd* cmd = hina_cmd_begin_ex(HINA_QUEUE_GRAPHICS);
    if (!cmd) {
        hina_frame_end();
        return;
    }

    // ====================================================================
    // Compute Pass - Update particles
    // ====================================================================

    hina_cmd_bind_pipeline(cmd, g_app.compute_pipeline);

    hina_bind_group_entry compute_entries[3] = {};
    compute_entries[0].binding = 0;
    compute_entries[0].type = HINA_DESC_TYPE_STORAGE_BUFFER;
    compute_entries[0].buffer.buffer = currentIn;
    compute_entries[0].buffer.offset = 0;
    compute_entries[0].buffer.size = sizeof(Particle) * PARTICLE_COUNT;

    compute_entries[1].binding = 1;
    compute_entries[1].type = HINA_DESC_TYPE_STORAGE_BUFFER;
    compute_entries[1].buffer.buffer = currentOut;
    compute_entries[1].buffer.offset = 0;
    compute_entries[1].buffer.size = sizeof(Particle) * PARTICLE_COUNT;

    compute_entries[2].binding = 2;
    compute_entries[2].type = HINA_DESC_TYPE_UNIFORM_BUFFER;
    compute_entries[2].buffer.buffer = g_app.compute_ubo;
    compute_entries[2].buffer.offset = 0;
    compute_entries[2].buffer.size = sizeof(ComputeUBO);

    hina_bind_group_desc compute_group_desc = {};
    compute_group_desc.layout = g_app.compute_layout;
    compute_group_desc.entries = compute_entries;
    compute_group_desc.entry_count = 3;
    compute_group_desc.label = "compute";

    hina_transient_bind_group compute_group = hina_example_make_transient_bind_group(&compute_group_desc);
    hina_cmd_bind_transient_group(cmd, 0, compute_group);

    hina_cmd_dispatch(cmd, PARTICLE_COUNT / WORKGROUP_SIZE, 1, 1);

    // Memory barrier: compute write -> vertex read
    hina_cmd_buffer_barrier(cmd, currentOut,
        HINA_PIPELINE_STAGE_COMPUTE_SHADER, HINA_ACCESS_SHADER_WRITE,
        HINA_PIPELINE_STAGE_VERTEX_INPUT, HINA_ACCESS_VERTEX_READ);

    // ====================================================================
    // Graphics Pass - Render particles
    // ====================================================================

    hina_pass_action pass = {};
    pass.colors[0].image = hina_texture_get_default_view(swapchain.texture);
    pass.colors[0].load_op = HINA_LOAD_OP_CLEAR;
    pass.colors[0].store_op = HINA_STORE_OP_STORE;
    pass.colors[0].clear_color[0] = 0.0f;
    pass.colors[0].clear_color[1] = 0.0f;
    pass.colors[0].clear_color[2] = 0.05f;  // Dark background
    pass.colors[0].clear_color[3] = 1.0f;

    hina_cmd_begin_pass(cmd, &pass);
    hina_cmd_bind_pipeline(cmd, g_app.graphics_pipeline);

    hina_vertex_input bindings = {};
    bindings.vertex_buffers[0] = currentOut;  // Use compute output as vertex input
    bindings.vertex_offsets[0] = 0;
    hina_cmd_apply_vertex_input(cmd, &bindings);

    hina_cmd_draw(cmd, PARTICLE_COUNT, 1, 0, 0);

    // Present frame (renders ImGui in separate pass, submits, ends frame)
    hina_example_present_frame(app, cmd, swapchain);

    // Swap ping-pong buffers for next frame
    g_app.pingPong = !g_app.pingPong;
}

// ============================================================================
// Cleanup
// ============================================================================

static void example_cleanup(hina_example_app* app) {
    (void)app;
    EXAMPLE_LOGI("Cleaning up Compute Particles example...");

    if (hina_pipeline_is_valid(g_app.graphics_pipeline))
        hina_destroy_pipeline(g_app.graphics_pipeline);
    if (hina_pipeline_is_valid(g_app.compute_pipeline))
        hina_destroy_pipeline(g_app.compute_pipeline);

    if (hina_buffer_is_valid(g_app.compute_ubo))
        hina_destroy_buffer(g_app.compute_ubo);
    if (hina_buffer_is_valid(g_app.ssbo_out))
        hina_destroy_buffer(g_app.ssbo_out);
    if (hina_buffer_is_valid(g_app.ssbo_in))
        hina_destroy_buffer(g_app.ssbo_in);

    hslc_shutdown();

    g_app = {};
    EXAMPLE_LOGI("Cleanup complete");
}

// ============================================================================
// Entry Point
// ============================================================================

HINA_EXAMPLE_MAIN("HinaVK Compute Particles", example_init, example_render, example_cleanup)
