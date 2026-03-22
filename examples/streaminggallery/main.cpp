#include <algorithm>
#include <array>
#include <cctype>
#include <atomic>
#include <cmath>
#include <condition_variable>
#include <cstdint>
#include <deque>
#include <filesystem>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

#define STB_IMAGE_IMPLEMENTATION
#define STBI_FAILURE_USERMSG
#include "../../third_party/tinygltf/stb_image.h"

#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>

#include "../hina_example.h"

namespace
{

constexpr uint32_t DEFAULT_POSTER_COLS = 12;
constexpr uint32_t DEFAULT_RESIDENT_BUDGET = 14;
constexpr uint32_t DEFAULT_QUEUE_BUDGET = 12;
constexpr uint32_t RESIDENT_HEADROOM = 4;
constexpr uint32_t MAX_QUEUE_BUDGET = 24;
constexpr float GALLERY_RADIUS = 7.8f;
constexpr float CAMERA_RADIUS = 4.3f;
constexpr float PANEL_WIDTH = 1.58f;
constexpr float PANEL_HEIGHT = 0.90f;
constexpr float PANEL_ROW_SPACING = 1.20f;
constexpr float ORBIT_SPEED = 0.22f;
constexpr float CAMERA_LEAD = 0.22f;
constexpr float CAMERA_FOV_Y_DEGREES = 56.0f;
constexpr float PREFETCH_ANGLE_MARGIN = glm::radians(28.0f);
constexpr float PANEL_BOUND_RADIUS = 0.92f;
constexpr uint32_t INITIAL_PREFETCH_TIMEOUT_MS = 5000;
constexpr uint32_t PLACEHOLDER_WIDTH = 128;
constexpr uint32_t PLACEHOLDER_HEIGHT = 72;

enum class PosterState : uint8_t
{
    Cleared,
    Queued,
    Resident,
    Failed
};

struct Vertex
{
    glm::vec3 position;
    glm::vec3 normal;
    glm::vec2 uv;
};

struct SceneUBO
{
    glm::mat4 projection;
    glm::mat4 view;
    glm::vec4 light_pos;
    glm::vec4 eye_pos;
};

struct PosterUBO
{
    glm::mat4 model;
    glm::vec4 tint;
    glm::vec4 params;
};

struct Mesh
{
    std::vector<Vertex> vertices;
    std::vector<uint32_t> indices;
};

struct PosterAsset
{
    std::string file_name;
    std::string disk_path;
    uint64_t file_size = 0;
    float angle = 0.0f;
    float y = 0.0f;
    uint32_t width = 0;
    uint32_t height = 0;
    uint64_t gpu_bytes = 0;
    uint64_t request_id = 0;
    uint32_t load_count = 0;
    uint32_t evict_count = 0;
    bool wanted = false;
    bool on_screen = false;
    PosterState state = PosterState::Cleared;
    hina_texture texture = {};
    hina_texture_view view = {};
};

struct LoadJob
{
    uint32_t poster_index = 0;
    uint64_t request_id = 0;
};

struct LoadResult
{
    uint32_t poster_index = 0;
    uint64_t request_id = 0;
    uint64_t disk_bytes = 0;
    uint32_t width = 0;
    uint32_t height = 0;
    uint64_t gpu_bytes = 0;
    bool success = false;
    hina_texture texture = {};
    hina_texture_view view = {};
    std::string error;
};

struct GalleryApp
{
    Mesh mesh;
    hina_buffer vbo = {};
    hina_buffer ibo = {};
    hina_buffer scene_buffer = {};
    hina_buffer poster_buffer = {};
    SceneUBO* scene_mapped = nullptr;
    uint8_t* poster_mapped = nullptr;
    uint64_t poster_stride = 0;

    hina_pipeline pipeline = {};
    hina_bind_group_layout scene_layout = {};
    hina_bind_group_layout surface_layout = {};
    hina_bind_group scene_group = {};
    hina_sampler sampler = {};
    hina_depth_buffer depth = {};

    hina_texture placeholder_texture = {};
    hina_texture_view placeholder_view = {};

    std::filesystem::path asset_dir;
    std::vector<PosterAsset> posters;
    uint32_t poster_cols = DEFAULT_POSTER_COLS;
    uint32_t poster_rows = 1;
    uint32_t resident_budget = DEFAULT_RESIDENT_BUDGET;
    uint32_t queue_budget = DEFAULT_QUEUE_BUDGET;
    bool pause_orbit = false;

    hina_context* stream_ctx = nullptr;
    std::thread stream_thread;
    std::mutex stream_mutex;
    std::condition_variable stream_cv;
    std::deque<LoadJob> load_queue;
    std::deque<LoadResult> ready_queue;
    std::atomic<bool> stream_exit { false };

    uint64_t loads_completed = 0;
    uint64_t evictions_completed = 0;
    uint64_t failed_loads = 0;
    uint64_t bytes_read_total = 0;
    uint64_t peak_resident_gpu_bytes = 0;
    uint32_t current_on_screen_count = 0;
    uint32_t current_on_screen_placeholders = 0;
    uint32_t peak_on_screen_placeholders = 0;
    float camera_angle = 0.0f;
};

GalleryApp g_app = {};

static uint64_t align_u64(uint64_t value, uint64_t alignment)
{
    if (alignment <= 1u) return value;
    return (value + alignment - 1u) & ~(alignment - 1u);
}

static float wrap_angle(float angle)
{
    while (angle > glm::pi<float>()) angle -= glm::two_pi<float>();
    while (angle < -glm::pi<float>()) angle += glm::two_pi<float>();
    return angle;
}

static float angular_distance(float a, float b)
{
    return std::fabs(wrap_angle(a - b));
}

static glm::vec3 gallery_eye(float camera_angle, float time_seconds)
{
    return glm::vec3(std::sin(camera_angle) * CAMERA_RADIUS,
                     0.30f + std::sin(time_seconds * 0.5f) * 0.12f,
                     std::cos(camera_angle) * CAMERA_RADIUS);
}

static glm::vec3 gallery_target(float focus_angle)
{
    return glm::vec3(std::sin(focus_angle) * GALLERY_RADIUS, 0.0f, std::cos(focus_angle) * GALLERY_RADIUS);
}

static glm::vec3 poster_center_world(const PosterAsset& poster)
{
    return glm::vec3(std::sin(poster.angle) * GALLERY_RADIUS, poster.y, std::cos(poster.angle) * GALLERY_RADIUS);
}

static float gallery_vertical_half_fov()
{
    return glm::radians(CAMERA_FOV_Y_DEGREES * 0.5f);
}

static float gallery_horizontal_half_fov(float aspect)
{
    return std::atan(std::tan(gallery_vertical_half_fov()) * aspect);
}

static bool poster_is_on_screen(const PosterAsset& poster,
                                const glm::vec3& eye,
                                const glm::vec3& forward,
                                const glm::vec3& right,
                                const glm::vec3& up,
                                float horiz_half_fov,
                                float vert_half_fov)
{
    glm::vec3 to_poster = poster_center_world(poster) - eye;
    float z = glm::dot(to_poster, forward);
    if (z <= 0.05f) return false;

    float x = glm::dot(to_poster, right);
    float y = glm::dot(to_poster, up);
    float pad = std::atan(PANEL_BOUND_RADIUS / z);
    float horiz = std::fabs(std::atan2(x, z));
    float vert = std::fabs(std::atan2(y, z));
    return horiz <= (horiz_half_fov + pad) && vert <= (vert_half_fov + pad);
}

static glm::vec3 hsv_to_rgb(float h, float s, float v)
{
    h = h - std::floor(h);
    float c = v * s;
    float x = c * (1.0f - std::fabs(std::fmod(h * 6.0f, 2.0f) - 1.0f));
    glm::vec3 rgb(0.0f);

    if (h < 1.0f / 6.0f) rgb = glm::vec3(c, x, 0.0f);
    else if (h < 2.0f / 6.0f) rgb = glm::vec3(x, c, 0.0f);
    else if (h < 3.0f / 6.0f) rgb = glm::vec3(0.0f, c, x);
    else if (h < 4.0f / 6.0f) rgb = glm::vec3(0.0f, x, c);
    else if (h < 5.0f / 6.0f) rgb = glm::vec3(x, 0.0f, c);
    else rgb = glm::vec3(c, 0.0f, x);

    return rgb + glm::vec3(v - c);
}

static const char* poster_state_name(PosterState state)
{
    switch (state)
    {
    case PosterState::Cleared: return "Cleared";
    case PosterState::Queued: return "Queued";
    case PosterState::Resident: return "Resident";
    case PosterState::Failed: return "Failed";
    }
    return "Unknown";
}

static Mesh make_panel_mesh()
{
    Mesh mesh;
    mesh.vertices = {
        {{-0.5f, -0.5f, 0.0f}, {0.0f, 0.0f, 1.0f}, {0.0f, 1.0f}},
        {{ 0.5f, -0.5f, 0.0f}, {0.0f, 0.0f, 1.0f}, {1.0f, 1.0f}},
        {{ 0.5f,  0.5f, 0.0f}, {0.0f, 0.0f, 1.0f}, {1.0f, 0.0f}},
        {{-0.5f,  0.5f, 0.0f}, {0.0f, 0.0f, 1.0f}, {0.0f, 0.0f}},
    };
    mesh.indices = {0, 1, 2, 0, 2, 3};
    return mesh;
}

static std::vector<uint8_t> make_placeholder_pixels()
{
    std::vector<uint8_t> pixels(PLACEHOLDER_WIDTH * PLACEHOLDER_HEIGHT * 4u);
    for (uint32_t y = 0; y < PLACEHOLDER_HEIGHT; ++y)
    {
        float v = float(y) / float(PLACEHOLDER_HEIGHT - 1u);
        for (uint32_t x = 0; x < PLACEHOLDER_WIDTH; ++x)
        {
            float u = float(x) / float(PLACEHOLDER_WIDTH - 1u);
            bool checker = ((x / 16u) + (y / 16u)) % 2u == 0u;
            glm::vec3 base = checker ? glm::vec3(0.18f, 0.20f, 0.24f) : glm::vec3(0.11f, 0.13f, 0.17f);
            glm::vec3 accent = hsv_to_rgb(0.58f + 0.07f * u, 0.30f, 0.92f);
            float sweep = 0.5f + 0.5f * std::sin((u * 10.0f + v * 3.0f) * glm::pi<float>());
            glm::vec3 color = glm::mix(base, accent, 0.18f * sweep);
            if (x % 24u == 0u || y % 24u == 0u)
            {
                color += glm::vec3(0.05f, 0.05f, 0.06f);
            }

            uint32_t idx = (y * PLACEHOLDER_WIDTH + x) * 4u;
            pixels[idx + 0] = static_cast<uint8_t>(glm::clamp(color.r, 0.0f, 1.0f) * 255.0f);
            pixels[idx + 1] = static_cast<uint8_t>(glm::clamp(color.g, 0.0f, 1.0f) * 255.0f);
            pixels[idx + 2] = static_cast<uint8_t>(glm::clamp(color.b, 0.0f, 1.0f) * 255.0f);
            pixels[idx + 3] = 255;
        }
    }
    return pixels;
}

static hina_texture make_texture(hina_context* ctx, const uint8_t* data, uint32_t width, uint32_t height)
{
    hina_texture_desc desc = hina_texture_desc_default();
    desc.format = HINA_FORMAT_R8G8B8A8_UNORM;
    desc.width = width;
    desc.height = height;
    desc.initial_data = data;
    return ctx ? hina_ctx_make_texture(ctx, &desc) : hina_make_texture(&desc);
}

static PosterUBO* poster_ubo_at(uint32_t index)
{
    return reinterpret_cast<PosterUBO*>(g_app.poster_mapped + g_app.poster_stride * index);
}

static uint64_t resident_gpu_bytes()
{
    uint64_t total = 0;
    for (const PosterAsset& poster : g_app.posters)
    {
        if (poster.state == PosterState::Resident)
        {
            total += poster.gpu_bytes;
        }
    }
    return total;
}

static uint32_t resident_poster_count()
{
    uint32_t count = 0;
    for (const PosterAsset& poster : g_app.posters)
    {
        if (poster.state == PosterState::Resident) ++count;
    }
    return count;
}

static uint32_t queued_poster_count()
{
    uint32_t count = 0;
    for (const PosterAsset& poster : g_app.posters)
    {
        if (poster.state == PosterState::Queued) ++count;
    }
    return count;
}

static void update_peak_resident_bytes()
{
    g_app.peak_resident_gpu_bytes = std::max(g_app.peak_resident_gpu_bytes, resident_gpu_bytes());
}

static bool discover_posters(hina_example_app* app)
{
    char* asset_path = hina_example_asset_path(app, "assets/posters");
    if (!asset_path)
    {
        EXAMPLE_LOGE("Failed to resolve poster asset directory");
        return false;
    }

    g_app.asset_dir = asset_path;
    free(asset_path);

    if (!std::filesystem::exists(g_app.asset_dir))
    {
        EXAMPLE_LOGE("Poster asset directory missing: %s", g_app.asset_dir.string().c_str());
        EXAMPLE_LOGE("Run examples/streaminggallery/prepare_assets.ps1 first");
        return false;
    }

    std::vector<std::filesystem::path> poster_files;
    for (const auto& entry : std::filesystem::directory_iterator(g_app.asset_dir))
    {
        if (!entry.is_regular_file()) continue;
        std::string ext = entry.path().extension().string();
        std::transform(ext.begin(), ext.end(), ext.begin(), [](unsigned char c) { return char(std::tolower(c)); });
        if (ext == ".jpg" || ext == ".jpeg" || ext == ".png")
        {
            poster_files.push_back(entry.path());
        }
    }

    std::sort(poster_files.begin(), poster_files.end());
    if (poster_files.empty())
    {
        EXAMPLE_LOGE("No poster images found in %s", g_app.asset_dir.string().c_str());
        return false;
    }

    g_app.poster_cols = std::min<uint32_t>(DEFAULT_POSTER_COLS, static_cast<uint32_t>(poster_files.size()));
    g_app.poster_rows = (static_cast<uint32_t>(poster_files.size()) + g_app.poster_cols - 1u) / g_app.poster_cols;
    g_app.posters.clear();
    g_app.posters.reserve(poster_files.size());

    for (uint32_t i = 0; i < poster_files.size(); ++i)
    {
        const auto& path = poster_files[i];
        uint32_t col = i % g_app.poster_cols;
        uint32_t row = i / g_app.poster_cols;
        float col_t = (float(col) + 0.5f) / float(g_app.poster_cols);
        float angle = (col_t - 0.5f) * glm::two_pi<float>();
        float y = (float(row) - float(g_app.poster_rows - 1u) * 0.5f) * PANEL_ROW_SPACING;

        PosterAsset poster = {};
        poster.file_name = path.filename().string();
        poster.disk_path = path.string();
        poster.file_size = std::filesystem::file_size(path);
        poster.angle = angle;
        poster.y = y;
        g_app.posters.push_back(std::move(poster));
    }

    return true;
}

static bool create_buffers()
{
    g_app.mesh = make_panel_mesh();

    hina_buffer_desc vbo_desc = {};
    vbo_desc.size = g_app.mesh.vertices.size() * sizeof(Vertex);
    vbo_desc.memory = HINA_BUFFER_CPU;
    vbo_desc.usage = HINA_BUFFER_VERTEX;
    vbo_desc.initial_data = g_app.mesh.vertices.data();
    g_app.vbo = hina_make_buffer(&vbo_desc);
    if (!hina_buffer_is_valid(g_app.vbo))
    {
        EXAMPLE_LOGE("Failed to create panel vertex buffer");
        return false;
    }

    hina_buffer_desc ibo_desc = {};
    ibo_desc.size = g_app.mesh.indices.size() * sizeof(uint32_t);
    ibo_desc.memory = HINA_BUFFER_CPU;
    ibo_desc.usage = HINA_BUFFER_INDEX;
    ibo_desc.initial_data = g_app.mesh.indices.data();
    g_app.ibo = hina_make_buffer(&ibo_desc);
    if (!hina_buffer_is_valid(g_app.ibo))
    {
        EXAMPLE_LOGE("Failed to create panel index buffer");
        return false;
    }

    hina_buffer_desc scene_desc = {};
    scene_desc.size = sizeof(SceneUBO);
    scene_desc.memory = HINA_BUFFER_CPU;
    scene_desc.usage = HINA_BUFFER_UNIFORM;
    g_app.scene_buffer = hina_make_buffer(&scene_desc);
    if (!hina_buffer_is_valid(g_app.scene_buffer))
    {
        EXAMPLE_LOGE("Failed to create scene buffer");
        return false;
    }
    g_app.scene_mapped = static_cast<SceneUBO*>(hina_mapped_buffer_ptr(g_app.scene_buffer));
    if (!g_app.scene_mapped)
    {
        EXAMPLE_LOGE("Failed to map scene buffer");
        return false;
    }

    const hina_device_caps* caps = hina_get_device_caps();
    uint32_t alignment = caps ? caps->min_uniform_buffer_alignment : 1u;
    if (alignment == 0u) alignment = 1u;
    g_app.poster_stride = align_u64(sizeof(PosterUBO), alignment);

    hina_buffer_desc poster_desc = {};
    poster_desc.size = g_app.poster_stride * g_app.posters.size();
    poster_desc.memory = HINA_BUFFER_CPU;
    poster_desc.usage = HINA_BUFFER_UNIFORM;
    g_app.poster_buffer = hina_make_buffer(&poster_desc);
    if (!hina_buffer_is_valid(g_app.poster_buffer))
    {
        EXAMPLE_LOGE("Failed to create poster uniform buffer");
        return false;
    }
    g_app.poster_mapped = static_cast<uint8_t*>(hina_mapped_buffer_ptr(g_app.poster_buffer));
    if (!g_app.poster_mapped)
    {
        EXAMPLE_LOGE("Failed to map poster uniform buffer");
        return false;
    }

    return true;
}

static bool create_placeholder_texture()
{
    std::vector<uint8_t> pixels = make_placeholder_pixels();
    g_app.placeholder_texture = make_texture(nullptr, pixels.data(), PLACEHOLDER_WIDTH, PLACEHOLDER_HEIGHT);
    if (!hina_texture_is_valid(g_app.placeholder_texture))
    {
        EXAMPLE_LOGE("Failed to create placeholder texture");
        return false;
    }

    hina_ticket ticket = hina_flush_uploads();
    if (ticket) hina_wait_ticket(ticket);

    if (!hina_texture_is_ready(g_app.placeholder_texture))
    {
        EXAMPLE_LOGE("Placeholder texture did not become ready");
        return false;
    }

    g_app.placeholder_view = hina_texture_get_default_view(g_app.placeholder_texture);
    return true;
}

static bool create_pipeline(hina_example_app* app)
{
    hina_vertex_layout vertex_layout = {};
    vertex_layout.buffer_count = 1;
    vertex_layout.buffer_strides[0] = sizeof(Vertex);
    vertex_layout.input_rates[0] = HINA_VERTEX_INPUT_RATE_VERTEX;
    vertex_layout.attr_count = 3;
    vertex_layout.attrs[0] = { HINA_FORMAT_R32G32B32_SFLOAT, offsetof(Vertex, position), 0, 0 };
    vertex_layout.attrs[1] = { HINA_FORMAT_R32G32B32_SFLOAT, offsetof(Vertex, normal), 1, 0 };
    vertex_layout.attrs[2] = { HINA_FORMAT_R32G32_SFLOAT, offsetof(Vertex, uv), 2, 0 };

    char* shader_path = hina_example_shader_path(app, "gallery.hina_sl");
    if (!shader_path)
    {
        EXAMPLE_LOGE("Failed to resolve gallery shader path");
        return false;
    }

    char* error = nullptr;
    hina_hsl_pipeline_desc pipeline_desc = hina_hsl_pipeline_desc_default();
    pipeline_desc.layout = vertex_layout;
    pipeline_desc.color_formats[0] = hina_get_surface_format();
    pipeline_desc.depth_format = HINA_FORMAT_D32_SFLOAT;
    pipeline_desc.front_face = HINA_FRONT_FACE_COUNTER_CLOCKWISE;

    g_app.pipeline = hina_example_make_pipeline_from_hsl(app, shader_path, &pipeline_desc, &error);
    free(shader_path);

    if (!hina_pipeline_is_valid(g_app.pipeline))
    {
        EXAMPLE_LOGE("Gallery pipeline creation failed: %s", error ? error : "unknown error");
        if (error) hslc_free_log(error);
        return false;
    }
    if (error) hslc_free_log(error);

    g_app.scene_layout = hina_pipeline_get_bind_group_layout(g_app.pipeline, 0);
    g_app.surface_layout = hina_pipeline_get_bind_group_layout(g_app.pipeline, 1);
    if (!hina_bind_group_layout_is_valid(g_app.scene_layout) ||
        !hina_bind_group_layout_is_valid(g_app.surface_layout))
    {
        EXAMPLE_LOGE("Failed to get gallery bind group layouts");
        return false;
    }

    hina_bind_group_entry scene_entry = {};
    scene_entry.binding = 0;
    scene_entry.type = HINA_DESC_TYPE_UNIFORM_BUFFER;
    scene_entry.buffer.buffer = g_app.scene_buffer;
    scene_entry.buffer.offset = 0;
    scene_entry.buffer.size = sizeof(SceneUBO);

    hina_bind_group_desc scene_group_desc = {};
    scene_group_desc.layout = g_app.scene_layout;
    scene_group_desc.entries = &scene_entry;
    scene_group_desc.entry_count = 1;
    scene_group_desc.label = "streaming_gallery_scene";

    g_app.scene_group = hina_create_bind_group(&scene_group_desc);
    if (!hina_bind_group_is_valid(g_app.scene_group))
    {
        EXAMPLE_LOGE("Failed to create scene bind group");
        return false;
    }

    return true;
}

static void enqueue_load(uint32_t index)
{
    PosterAsset& poster = g_app.posters[index];
    if (poster.state != PosterState::Cleared) return;

    poster.state = PosterState::Queued;
    poster.request_id++;

    {
        std::lock_guard<std::mutex> lock(g_app.stream_mutex);
        g_app.load_queue.push_back({index, poster.request_id});
    }
    g_app.stream_cv.notify_all();
}

static void evict_poster(uint32_t index)
{
    PosterAsset& poster = g_app.posters[index];
    if (poster.state != PosterState::Resident) return;

    if (hina_texture_is_valid(poster.texture))
    {
        hina_destroy_texture(poster.texture);
    }

    poster.texture = {};
    poster.view = {};
    poster.width = 0;
    poster.height = 0;
    poster.gpu_bytes = 0;
    poster.state = PosterState::Cleared;
    poster.evict_count++;
    g_app.evictions_completed++;
}

static void stream_worker_main()
{
    while (!g_app.stream_exit.load(std::memory_order_relaxed))
    {
        LoadJob job = {};
        {
            std::unique_lock<std::mutex> lock(g_app.stream_mutex);
            g_app.stream_cv.wait(lock, [] {
                return g_app.stream_exit.load(std::memory_order_relaxed) || !g_app.load_queue.empty();
            });
            if (g_app.stream_exit.load(std::memory_order_relaxed)) break;
            job = g_app.load_queue.front();
            g_app.load_queue.pop_front();
        }

        const PosterAsset& poster = g_app.posters[job.poster_index];
        LoadResult result = {};
        result.poster_index = job.poster_index;
        result.request_id = job.request_id;
        result.disk_bytes = poster.file_size;

        int width = 0;
        int height = 0;
        int comp = 0;
        stbi_uc* pixels = stbi_load(poster.disk_path.c_str(), &width, &height, &comp, 4);
        if (!pixels)
        {
            result.error = stbi_failure_reason() ? stbi_failure_reason() : "stbi_load failed";
        }
        else
        {
            hina_texture texture = make_texture(g_app.stream_ctx, pixels, uint32_t(width), uint32_t(height));
            stbi_image_free(pixels);

            if (!hina_texture_is_valid(texture))
            {
                result.error = "hina_ctx_make_texture failed";
            }
            else
            {
                hina_ticket ticket = hina_ctx_flush_uploads(g_app.stream_ctx);
                if (ticket) hina_ctx_wait_ticket(g_app.stream_ctx, ticket);

                result.success = true;
                result.texture = texture;
                result.view = hina_texture_get_default_view(texture);
                result.width = uint32_t(width);
                result.height = uint32_t(height);
                result.gpu_bytes = uint64_t(width) * uint64_t(height) * 4u;
            }
        }

        {
            std::lock_guard<std::mutex> lock(g_app.stream_mutex);
            g_app.ready_queue.push_back(std::move(result));
        }
    }
}

static bool start_streamer()
{
    g_app.stream_ctx = hina_create_transfer_context();
    if (!g_app.stream_ctx)
    {
        EXAMPLE_LOGE("Failed to create streaming thread context");
        return false;
    }

    try
    {
        g_app.stream_thread = std::thread(stream_worker_main);
    }
    catch (...)
    {
        EXAMPLE_LOGE("Failed to launch streaming worker thread");
        hina_destroy_transfer_context(g_app.stream_ctx);
        g_app.stream_ctx = nullptr;
        return false;
    }

    return true;
}

static void stop_streamer()
{
    g_app.stream_exit.store(true, std::memory_order_relaxed);
    g_app.stream_cv.notify_all();
    if (g_app.stream_thread.joinable())
    {
        g_app.stream_thread.join();
    }

    {
        std::lock_guard<std::mutex> lock(g_app.stream_mutex);
        while (!g_app.ready_queue.empty())
        {
            LoadResult result = std::move(g_app.ready_queue.front());
            g_app.ready_queue.pop_front();
            if (hina_texture_is_valid(result.texture))
            {
                hina_destroy_texture(result.texture);
            }
        }
        g_app.load_queue.clear();
    }

    if (g_app.stream_ctx)
    {
        hina_destroy_transfer_context(g_app.stream_ctx);
        g_app.stream_ctx = nullptr;
    }
}

static void process_load_results()
{
    std::deque<LoadResult> ready;
    {
        std::lock_guard<std::mutex> lock(g_app.stream_mutex);
        ready.swap(g_app.ready_queue);
    }

    while (!ready.empty())
    {
        LoadResult result = std::move(ready.front());
        ready.pop_front();

        if (result.poster_index >= g_app.posters.size())
        {
            if (hina_texture_is_valid(result.texture))
            {
                hina_destroy_texture(result.texture);
            }
            continue;
        }

        PosterAsset& poster = g_app.posters[result.poster_index];
        if (result.request_id != poster.request_id || !poster.wanted)
        {
            if (hina_texture_is_valid(result.texture))
            {
                hina_destroy_texture(result.texture);
            }
            if (poster.state == PosterState::Queued)
            {
                poster.state = PosterState::Cleared;
            }
            continue;
        }

        if (!result.success)
        {
            poster.state = PosterState::Failed;
            g_app.failed_loads++;
            EXAMPLE_LOGE("Poster load failed for %s: %s", poster.file_name.c_str(), result.error.c_str());
            continue;
        }

        if (hina_texture_is_valid(poster.texture))
        {
            hina_destroy_texture(poster.texture);
        }

        poster.texture = result.texture;
        poster.view = result.view;
        poster.width = result.width;
        poster.height = result.height;
        poster.gpu_bytes = result.gpu_bytes;
        poster.state = PosterState::Resident;
        poster.load_count++;

        g_app.loads_completed++;
        g_app.bytes_read_total += result.disk_bytes;
        update_peak_resident_bytes();
    }
}

static bool initial_prefetch_complete(void)
{
    for (const PosterAsset& poster : g_app.posters)
    {
        if (!poster.wanted) continue;
        if (poster.state != PosterState::Resident && poster.state != PosterState::Failed) return false;
    }
    return true;
}

static void update_streaming_targets(float focus_angle, const glm::vec3& eye, const glm::vec3& target, float aspect)
{
    glm::vec3 world_up(0.0f, 1.0f, 0.0f);
    glm::vec3 forward = glm::normalize(target - eye);
    glm::vec3 right = glm::normalize(glm::cross(forward, world_up));
    glm::vec3 up = glm::normalize(glm::cross(right, forward));
    float horiz_half_fov = gallery_horizontal_half_fov(aspect);
    float vert_half_fov = gallery_vertical_half_fov();
    float preload_horizon = horiz_half_fov + PREFETCH_ANGLE_MARGIN;
    uint32_t on_screen_count = 0;

    for (PosterAsset& poster : g_app.posters)
    {
        poster.wanted = false;
        poster.on_screen = poster_is_on_screen(poster, eye, forward, right, up, horiz_half_fov, vert_half_fov);
        if (poster.on_screen) ++on_screen_count;
    }
    g_app.current_on_screen_count = on_screen_count;

    std::vector<std::pair<float, uint32_t>> ranking;
    ranking.reserve(g_app.posters.size());
    for (uint32_t i = 0; i < g_app.posters.size(); ++i)
    {
        const PosterAsset& poster = g_app.posters[i];
        float score = angular_distance(poster.angle, focus_angle) + std::fabs(poster.y) * 0.02f;
        ranking.emplace_back(score, i);
        if (poster.on_screen || angular_distance(poster.angle, focus_angle) <= preload_horizon)
        {
            g_app.posters[i].wanted = true;
        }
    }

    std::sort(ranking.begin(), ranking.end(), [](const auto& a, const auto& b) {
        return a.first < b.first;
    });

    uint32_t wanted_count = 0;
    for (const PosterAsset& poster : g_app.posters)
    {
        if (poster.wanted) ++wanted_count;
    }

    uint32_t effective_resident_budget = std::min<uint32_t>(
        static_cast<uint32_t>(g_app.posters.size()),
        std::max(g_app.resident_budget, on_screen_count + RESIDENT_HEADROOM));

    for (uint32_t i = 0; i < ranking.size() && wanted_count < effective_resident_budget; ++i)
    {
        PosterAsset& poster = g_app.posters[ranking[i].second];
        if (poster.wanted) continue;
        poster.wanted = true;
        ++wanted_count;
    }

    for (uint32_t i = 0; i < g_app.posters.size(); ++i)
    {
        PosterAsset& poster = g_app.posters[i];
        if (!poster.wanted && poster.state == PosterState::Resident)
        {
            evict_poster(i);
        }
    }

    uint32_t effective_queue_budget = std::min<uint32_t>(MAX_QUEUE_BUDGET, std::max(g_app.queue_budget, effective_resident_budget));
    for (uint32_t i = 0; i < ranking.size(); ++i)
    {
        PosterAsset& poster = g_app.posters[ranking[i].second];
        if (!poster.wanted) continue;
        if (poster.state == PosterState::Cleared && queued_poster_count() < effective_queue_budget)
        {
            enqueue_load(ranking[i].second);
        }
    }

    uint32_t on_screen_placeholders = 0;
    for (const PosterAsset& poster : g_app.posters)
    {
        if (!poster.on_screen) continue;
        if (poster.state != PosterState::Resident && poster.state != PosterState::Failed) ++on_screen_placeholders;
    }
    g_app.current_on_screen_placeholders = on_screen_placeholders;
    g_app.peak_on_screen_placeholders = std::max(g_app.peak_on_screen_placeholders, on_screen_placeholders);
}

static bool bootstrap_initial_residency(uint32_t width, uint32_t height)
{
    float aspect = height > 0u ? float(width) / float(height) : 1.0f;
    float focus_angle = g_app.camera_angle + CAMERA_LEAD;
    glm::vec3 eye = gallery_eye(g_app.camera_angle, 0.0f);
    glm::vec3 target = gallery_target(focus_angle);

    for (uint32_t elapsed_ms = 0; elapsed_ms < INITIAL_PREFETCH_TIMEOUT_MS; ++elapsed_ms)
    {
        process_load_results();
        update_streaming_targets(focus_angle, eye, target, aspect);
        if (initial_prefetch_complete()) return true;
        hina_example_sleep(1);
    }

    process_load_results();
    update_streaming_targets(focus_angle, eye, target, aspect);
    return initial_prefetch_complete();
}

static void update_poster_ubos(float time_seconds)
{
    for (uint32_t i = 0; i < g_app.posters.size(); ++i)
    {
        PosterAsset& poster = g_app.posters[i];
        PosterUBO* ubo = poster_ubo_at(i);

        float angle_deg = glm::degrees(poster.angle);
        float sway = std::sin(time_seconds * 0.65f + poster.angle * 2.3f + poster.y) * 2.0f;
        glm::mat4 model(1.0f);
        model = glm::translate(model, glm::vec3(std::sin(poster.angle) * GALLERY_RADIUS, poster.y,
                                                std::cos(poster.angle) * GALLERY_RADIUS));
        model = glm::rotate(model, glm::radians(180.0f + angle_deg), glm::vec3(0.0f, 1.0f, 0.0f));
        model = glm::rotate(model, glm::radians(sway), glm::vec3(0.0f, 0.0f, 1.0f));
        model = glm::scale(model, glm::vec3(PANEL_WIDTH, PANEL_HEIGHT, 1.0f));

        glm::vec3 tint = glm::vec3(1.0f);
        float emissive = 0.18f;
        if (poster.state == PosterState::Queued)
        {
            tint = glm::vec3(0.92f, 0.82f, 0.62f);
            emissive = 0.28f;
        }
        else if (poster.state == PosterState::Cleared)
        {
            tint = poster.on_screen ? glm::vec3(0.68f, 0.72f, 0.80f) : glm::vec3(0.52f, 0.56f, 0.62f);
            emissive = 0.10f;
        }
        else if (poster.state == PosterState::Failed)
        {
            tint = glm::vec3(0.95f, 0.42f, 0.38f);
            emissive = 0.22f;
        }
        else if (!poster.on_screen)
        {
            tint = glm::vec3(0.86f, 0.90f, 0.97f);
        }

        ubo->model = model;
        ubo->tint = glm::vec4(tint, 1.0f);
        ubo->params = glm::vec4(0.0f, emissive, poster.on_screen ? 1.0f : 0.0f, 0.0f);
    }
}

static void draw_benchmark_ui(hina_example_app* app)
{
#ifdef HINA_EXAMPLE_HAS_IMGUI
    ImGui::SetNextWindowPos(ImVec2(10.0f, 110.0f), ImGuiCond_FirstUseEver);
    ImGui::SetNextWindowSize(ImVec2(520.0f, 0.0f), ImGuiCond_FirstUseEver);
    ImGui::SetNextWindowBgAlpha(0.78f);

    if (ImGui::Begin("Streaming Residency", nullptr, ImGuiWindowFlags_AlwaysAutoResize))
    {
        hina_frame_stats stats = hina_get_frame_stats();
        uint64_t resident_bytes = resident_gpu_bytes();
        uint32_t resident_count = resident_poster_count();
        uint32_t queued_count = queued_poster_count();
        uint32_t cleared_count = static_cast<uint32_t>(g_app.posters.size()) - resident_count - queued_count;

        ImGui::Text("Disk-backed poster streaming from %s", g_app.asset_dir.string().c_str());
        ImGui::Separator();
        ImGui::Text("Camera angle: %.2f rad", g_app.camera_angle);
        ImGui::Text("Resident: %u  Queued: %u  Cleared: %u", resident_count, queued_count, cleared_count);
        ImGui::Text("On-screen: %u  On-screen placeholders: %u  Peak on-screen placeholders: %u",
                    g_app.current_on_screen_count,
                    g_app.current_on_screen_placeholders,
                    g_app.peak_on_screen_placeholders);
        ImGui::Text("Loads: %llu  Evictions: %llu  Failures: %llu",
                    static_cast<unsigned long long>(g_app.loads_completed),
                    static_cast<unsigned long long>(g_app.evictions_completed),
                    static_cast<unsigned long long>(g_app.failed_loads));
        ImGui::Text("Disk read: %.2f MiB  Resident GPU: %.2f MiB  Peak GPU: %.2f MiB",
                    double(g_app.bytes_read_total) / (1024.0 * 1024.0),
                    double(resident_bytes) / (1024.0 * 1024.0),
                    double(g_app.peak_resident_gpu_bytes) / (1024.0 * 1024.0));
        ImGui::Text("Draws: %u  CPU: %.3f ms  GPU: %.3f ms",
                    stats.draw_calls, stats.frame_time_ms, stats.gpu_time_ms);
        ImGui::Separator();

        ImGui::Checkbox("Pause orbit", &g_app.pause_orbit);
        int resident_budget = static_cast<int>(g_app.resident_budget);
        int queue_budget = static_cast<int>(g_app.queue_budget);
        int budget_max = static_cast<int>(g_app.posters.size());
        if (ImGui::SliderInt("Resident target", &resident_budget, 4, budget_max))
        {
            g_app.resident_budget = static_cast<uint32_t>(resident_budget);
        }
        if (ImGui::SliderInt("Queue target", &queue_budget, 1, std::min<int>(budget_max, MAX_QUEUE_BUDGET)))
        {
            g_app.queue_budget = static_cast<uint32_t>(queue_budget);
        }

        ImGui::Separator();
        if (ImGui::BeginTable("poster_residency", 6, ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg | ImGuiTableFlags_ScrollY,
                              ImVec2(500.0f, 320.0f)))
        {
            ImGui::TableSetupColumn("ID");
            ImGui::TableSetupColumn("State");
            ImGui::TableSetupColumn("On screen");
            ImGui::TableSetupColumn("GPU MiB");
            ImGui::TableSetupColumn("Disk KB");
            ImGui::TableSetupColumn("File");
            ImGui::TableHeadersRow();

            for (uint32_t i = 0; i < g_app.posters.size(); ++i)
            {
                const PosterAsset& poster = g_app.posters[i];
                ImGui::TableNextRow();
                ImGui::TableSetColumnIndex(0);
                ImGui::Text("%02u", i);
                ImGui::TableSetColumnIndex(1);
                ImGui::TextUnformatted(poster_state_name(poster.state));
                ImGui::TableSetColumnIndex(2);
                ImGui::TextUnformatted(poster.on_screen ? "yes" : "no");
                ImGui::TableSetColumnIndex(3);
                ImGui::Text("%.2f", double(poster.gpu_bytes) / (1024.0 * 1024.0));
                ImGui::TableSetColumnIndex(4);
                ImGui::Text("%.1f", double(poster.file_size) / 1024.0);
                ImGui::TableSetColumnIndex(5);
                ImGui::TextUnformatted(poster.file_name.c_str());
            }
            ImGui::EndTable();
        }

        ImGui::TextWrapped("The resident target is a soft floor. The sample will keep anything currently on screen plus a small look-ahead margin resident, even if that exceeds the slider target.");
        ImGui::TextWrapped("For comparable measurements, use HINA_INTERNAL_PROFILE=1 and a duration-bounded run so the camera crosses multiple residency windows.");
    }
    ImGui::End();
#else
    (void)app;
#endif
}

static bool example_init(hina_example_app* app)
{
    EXAMPLE_LOGI("Initializing Streaming Gallery example...");

    if (!discover_posters(app)) return false;
    if (!create_buffers()) return false;

    hina_sampler_desc sampler_desc = hina_sampler_desc_default();
    sampler_desc.min_filter = HINA_FILTER_LINEAR;
    sampler_desc.mag_filter = HINA_FILTER_LINEAR;
    sampler_desc.address_u = HINA_ADDRESS_CLAMP_TO_EDGE;
    sampler_desc.address_v = HINA_ADDRESS_CLAMP_TO_EDGE;
    g_app.sampler = hina_make_sampler(&sampler_desc);
    if (!hina_sampler_is_valid(g_app.sampler))
    {
        EXAMPLE_LOGE("Failed to create gallery sampler");
        return false;
    }

    if (!hina_depth_buffer_init(&g_app.depth, app->width, app->height))
    {
        EXAMPLE_LOGE("Failed to create gallery depth buffer");
        return false;
    }

    if (!create_placeholder_texture()) return false;
    if (!create_pipeline(app)) return false;
    if (!start_streamer()) return false;
    if (!bootstrap_initial_residency(app->width, app->height))
    {
        EXAMPLE_LOGE("Initial poster prefetch did not complete within %u ms", INITIAL_PREFETCH_TIMEOUT_MS);
        return false;
    }
    g_app.current_on_screen_placeholders = 0;
    g_app.peak_on_screen_placeholders = 0;

    EXAMPLE_LOGI("Streaming Gallery initialized with %u disk-backed posters from %s",
                 static_cast<uint32_t>(g_app.posters.size()), g_app.asset_dir.string().c_str());
    EXAMPLE_LOGI("Use HINA_INTERNAL_PROFILE=1 with --duration=N to profile disk-backed streaming");
    return true;
}

static void example_render(hina_example_app* app)
{
    hina_example_begin_ui(app);
    draw_benchmark_ui(app);

    if (!g_app.pause_orbit)
    {
        g_app.camera_angle = app->elapsed_time * ORBIT_SPEED;
    }

    hina_swapchain_image swapchain = hina_frame_begin();
    if (swapchain.texture.id == HINA_INVALID_HANDLE)
    {
        hina_example_sleep(10);
        hina_frame_end();
        return;
    }

    uint32_t width = 0;
    uint32_t height = 0;
    hina_get_texture_size(swapchain.texture, &width, &height);
    if (!hina_depth_buffer_resize(&g_app.depth, width, height))
    {
        EXAMPLE_LOGE("Failed to resize gallery depth buffer");
        hina_frame_end();
        return;
    }

    float aspect = height > 0u ? float(width) / float(height) : 1.0f;
    float focus_angle = g_app.camera_angle + CAMERA_LEAD;
    glm::vec3 eye = gallery_eye(g_app.camera_angle, app->elapsed_time);
    glm::vec3 target = gallery_target(focus_angle);

    process_load_results();
    update_streaming_targets(focus_angle, eye, target, aspect);

    glm::mat4 projection = glm::perspective(glm::radians(CAMERA_FOV_Y_DEGREES), aspect, 0.1f, 64.0f);
    projection[1][1] *= -1.0f;
    g_app.scene_mapped->projection = projection;
    g_app.scene_mapped->view = glm::lookAt(eye, target, glm::vec3(0.0f, 1.0f, 0.0f));
    g_app.scene_mapped->light_pos = glm::vec4(0.0f, 2.4f, 0.0f, 1.0f);
    g_app.scene_mapped->eye_pos = glm::vec4(eye, 1.0f);

    update_poster_ubos(app->elapsed_time);
    update_peak_resident_bytes();

    hina_cmd* cmd = hina_cmd_begin_ex(HINA_QUEUE_GRAPHICS);
    if (!cmd)
    {
        hina_frame_end();
        return;
    }

    hina_pass_action pass = {};
    pass.colors[0].image = hina_texture_get_default_view(swapchain.texture);
    pass.colors[0].load_op = HINA_LOAD_OP_CLEAR;
    pass.colors[0].store_op = HINA_STORE_OP_STORE;
    pass.colors[0].clear_color[0] = 0.040f;
    pass.colors[0].clear_color[1] = 0.050f;
    pass.colors[0].clear_color[2] = 0.080f;
    pass.colors[0].clear_color[3] = 1.0f;
    pass.depth.image = hina_texture_get_default_view(g_app.depth.texture);
    pass.depth.load_op = HINA_LOAD_OP_CLEAR;
    pass.depth.store_op = HINA_STORE_OP_DONT_CARE;
    pass.depth.depth_clear = 1.0f;

    hina_cmd_begin_pass(cmd, &pass);
    hina_cmd_bind_pipeline(cmd, g_app.pipeline);
    hina_cmd_bind_group(cmd, 0, g_app.scene_group);

    hina_vertex_input bindings = {};
    bindings.vertex_buffers[0] = g_app.vbo;
    bindings.vertex_offsets[0] = 0;
    bindings.index_buffer = g_app.ibo;
    bindings.index_type = HINA_INDEX_UINT32;
    hina_cmd_apply_vertex_input(cmd, &bindings);

    for (uint32_t i = 0; i < g_app.posters.size(); ++i)
    {
        const PosterAsset& poster = g_app.posters[i];
        hina_bind_group_entry entries[2] = {};
        entries[0].binding = 0;
        entries[0].type = HINA_DESC_TYPE_UNIFORM_BUFFER;
        entries[0].buffer.buffer = g_app.poster_buffer;
        entries[0].buffer.offset = g_app.poster_stride * i;
        entries[0].buffer.size = sizeof(PosterUBO);
        entries[1].binding = 1;
        entries[1].type = HINA_DESC_TYPE_COMBINED_IMAGE_SAMPLER;
        entries[1].combined.view = poster.state == PosterState::Resident ? poster.view : g_app.placeholder_view;
        entries[1].combined.sampler = g_app.sampler;

        hina_bind_group_desc group_desc = {};
        group_desc.layout = g_app.surface_layout;
        group_desc.entries = entries;
        group_desc.entry_count = 2;
        group_desc.label = "streaming_gallery_surface";

        hina_transient_bind_group surface = hina_example_make_transient_bind_group(&group_desc);
        hina_cmd_bind_transient_group(cmd, 1, surface);
        hina_cmd_draw_indexed(cmd, static_cast<uint32_t>(g_app.mesh.indices.size()), 1, 0, 0, 0);
    }

    hina_example_present_frame(app, cmd, swapchain);
}

static void example_cleanup(hina_example_app* app)
{
    (void)app;

    stop_streamer();

    if (hina_bind_group_is_valid(g_app.scene_group))
    {
        hina_destroy_bind_group(g_app.scene_group);
        g_app.scene_group = {};
    }

    if (hina_pipeline_is_valid(g_app.pipeline))
    {
        hina_destroy_pipeline(g_app.pipeline);
        g_app.pipeline = {};
    }

    hina_depth_buffer_destroy(&g_app.depth);

    if (hina_sampler_is_valid(g_app.sampler))
    {
        hina_destroy_sampler(g_app.sampler);
        g_app.sampler = {};
    }

    if (hina_texture_is_valid(g_app.placeholder_texture))
    {
        hina_destroy_texture(g_app.placeholder_texture);
        g_app.placeholder_texture = {};
        g_app.placeholder_view = {};
    }

    for (PosterAsset& poster : g_app.posters)
    {
        if (hina_texture_is_valid(poster.texture))
        {
            hina_destroy_texture(poster.texture);
            poster.texture = {};
            poster.view = {};
        }
    }

    if (hina_buffer_is_valid(g_app.poster_buffer))
    {
        hina_destroy_buffer(g_app.poster_buffer);
        g_app.poster_buffer = {};
    }
    if (hina_buffer_is_valid(g_app.scene_buffer))
    {
        hina_destroy_buffer(g_app.scene_buffer);
        g_app.scene_buffer = {};
    }
    if (hina_buffer_is_valid(g_app.ibo))
    {
        hina_destroy_buffer(g_app.ibo);
        g_app.ibo = {};
    }
    if (hina_buffer_is_valid(g_app.vbo))
    {
        hina_destroy_buffer(g_app.vbo);
        g_app.vbo = {};
    }

    printf("[STREAM] posters=%u resident_budget=%u queue_budget=%u loads=%llu evictions=%llu failures=%llu disk_mib=%.2f peak_gpu_mib=%.2f peak_screen_placeholders=%u\n",
           static_cast<unsigned>(g_app.posters.size()),
           static_cast<unsigned>(g_app.resident_budget),
           static_cast<unsigned>(g_app.queue_budget),
           static_cast<unsigned long long>(g_app.loads_completed),
           static_cast<unsigned long long>(g_app.evictions_completed),
           static_cast<unsigned long long>(g_app.failed_loads),
           double(g_app.bytes_read_total) / (1024.0 * 1024.0),
           double(g_app.peak_resident_gpu_bytes) / (1024.0 * 1024.0),
           static_cast<unsigned>(g_app.peak_on_screen_placeholders));
}

} // namespace

HINA_EXAMPLE_MAIN("HinaVK Streaming Gallery", example_init, example_render, example_cleanup)
