/**
 * hina_example.h - Helper header for HinaVK examples
 *
 * Provides cross-platform example framework:
 * - Desktop: SDL window management
 * - Android: NativeActivity via android_native_app_glue
 *
 * Usage:
 *   #include "hina_example.h"
 *
 * On Android, define HINA_EXAMPLE_ANDROID_MAIN in exactly one .cpp file
 * to get the android_main entry point.
 */

#ifndef HINA_EXAMPLE_H
#define HINA_EXAMPLE_H

#include <cstdio>
#include <cstdlib>
#include <cstring>

// ============================================================================
// Platform Detection and Includes
// ============================================================================

#ifdef __ANDROID__
    // Android NativeActivity path
    #include <android/log.h>
    #include <android/native_activity.h>
    #include <android/native_window.h>
    #include <android/asset_manager.h>
    #include <android_native_app_glue.h>
    #include <time.h>  // for nanosleep

    #define EXAMPLE_LOG_TAG "HinaVK"
    #define EXAMPLE_LOGI(...) __android_log_print(ANDROID_LOG_INFO, EXAMPLE_LOG_TAG, __VA_ARGS__)
    #define EXAMPLE_LOGE(...) __android_log_print(ANDROID_LOG_ERROR, EXAMPLE_LOG_TAG, __VA_ARGS__)
#else
    // Desktop SDL path
    #include <SDL.h>
    #include <SDL_syswm.h>

    #define EXAMPLE_LOGI(...) do { fprintf(stdout, __VA_ARGS__); fprintf(stdout, "\n"); } while(0)
    #define EXAMPLE_LOGE(...) do { fprintf(stderr, __VA_ARGS__); fprintf(stderr, "\n"); } while(0)

    #ifdef _WIN32
        #define _CRTDBG_MAP_ALLOC
        #include <cstdlib>
        #include <crtdbg.h>
    #endif
#endif

extern "C" {
#include "hina_vk.h"
}

// ============================================================================
// ImGui Integration (when available)
// ============================================================================

#ifdef HINA_EXAMPLE_HAS_IMGUI
#include "imgui.h"
#include <vector>
#include <cstring>

// ImGui configuration
#ifndef HINA_IMGUI_MAX_VERTEX_COUNT
#define HINA_IMGUI_MAX_VERTEX_COUNT 65536
#endif
#ifndef HINA_IMGUI_MAX_INDEX_COUNT
#define HINA_IMGUI_MAX_INDEX_COUNT (65536 * 3)
#endif
#ifndef HINA_IMGUI_FRAMES_IN_FLIGHT
#define HINA_IMGUI_FRAMES_IN_FLIGHT 3
#endif
#ifndef HINA_IMGUI_MAX_TEXTURES
#define HINA_IMGUI_MAX_TEXTURES 64
#endif

// Forward declarations for ImGui functions (defined later in file)
struct hina_example_app;
struct hina_cmd;
struct ImDrawData;
inline bool hina_example_imgui_init(hina_example_app* app);
inline void hina_example_imgui_shutdown(hina_example_app* app);
inline bool hina_example_begin_ui(hina_example_app* app);
inline void hina_example_draw_imgui_data(hina_example_app* app, hina_cmd* cmd, ImDrawData* draw_data, uint32_t fb_width, uint32_t fb_height);
inline void hina_example_draw_imgui_data(hina_example_app* app, hina_cmd* cmd, ImDrawData* draw_data);  // Legacy overload
inline bool hina_example_ui_want_mouse(hina_example_app* app);
inline void hina_example_imgui_stats_content(hina_example_app* app, bool include_headers);
#ifndef __ANDROID__
inline bool hina_example_imgui_process_event(hina_example_app* app, const SDL_Event* event);
#else
inline bool hina_example_imgui_process_touch(hina_example_app* app, int action, float x, float y);
#endif

#else // !HINA_EXAMPLE_HAS_IMGUI

// Forward declarations for stub functions (defined later in file)
struct hina_example_app;
struct hina_cmd;
inline bool hina_example_imgui_init(hina_example_app* app);
inline void hina_example_imgui_shutdown(hina_example_app* app);
inline bool hina_example_begin_ui(hina_example_app* app);
inline bool hina_example_ui_want_mouse(hina_example_app* app);

#endif // HINA_EXAMPLE_HAS_IMGUI

// ============================================================================
// Scope Exit Macro (C++ RAII cleanup)
// ============================================================================

#define HINA_CONCAT_(a, b) a##b
#define HINA_CONCAT(a, b) HINA_CONCAT_(a, b)

struct hina_scope_exit_t {
    void (*fn)(void*);
    void* ctx;
    ~hina_scope_exit_t() { if (fn) fn(ctx); }
};

#define HINA_SCOPE_EXIT(code) \
    auto HINA_CONCAT(_scope_exit_fn_, __LINE__) = [&]() { code; }; \
    hina_scope_exit_t HINA_CONCAT(_scope_exit_, __LINE__) = { \
        [](void* ctx) { (*static_cast<decltype(&HINA_CONCAT(_scope_exit_fn_, __LINE__))>(ctx))(); }, \
        &HINA_CONCAT(_scope_exit_fn_, __LINE__) \
    }

// ============================================================================
// Example App State (Cross-Platform)
// ============================================================================

// SDL Event callback for custom event processing (e.g., ImGui)
#ifndef __ANDROID__
typedef bool (*hina_example_event_callback)(const SDL_Event* event, void* user_data);
#endif

// ============================================================================
// Camera Helper (requires GLM)
// ============================================================================

#ifdef GLM_VERSION

struct hina_camera {
    glm::vec3 rotation;
    glm::vec3 position;
    float zoom;

    hina_camera() : rotation(0.0f), position(0.0f), zoom(-2.5f) {}

    glm::mat4 view_matrix() const {
        glm::mat4 rotM = glm::mat4(1.0f);
        rotM = glm::rotate(rotM, glm::radians(rotation.x), glm::vec3(1.0f, 0.0f, 0.0f));
        rotM = glm::rotate(rotM, glm::radians(rotation.y), glm::vec3(0.0f, 1.0f, 0.0f));
        rotM = glm::rotate(rotM, glm::radians(rotation.z), glm::vec3(0.0f, 0.0f, 1.0f));

        glm::vec3 translation = position + glm::vec3(0.0f, 0.0f, zoom);
        glm::mat4 transM = glm::translate(glm::mat4(1.0f), translation);

        return transM * rotM;
    }

    void update(const hina_example_app& app, float rotation_speed = 0.5f, float pan_speed = 0.01f, float zoom_speed = 0.25f);
};

#endif // GLM_VERSION

struct hina_example_app {
    // Common state
    int width;
    int height;
    bool running;
    bool paused;
    bool focused;

    // Native window handles (for Vulkan surface)
    void* native_window;
    void* native_display;

    // Input state (touch on Android, mouse on desktop)
    float input_x;
    float input_y;
    float input_delta_x;
    float input_delta_y;
    bool input_down;        // Primary touch/left mouse
    bool input_down_alt;    // Secondary touch/right mouse (desktop only)
    int scroll_delta;       // Mouse wheel (desktop only, 0 on Android)

    // Keyboard state (desktop only)
    bool key_space;         // Space key pressed this frame
    bool key_space_held;    // Space key currently held

    // Timing
    float delta_time;
    float elapsed_time;

#ifdef GLM_VERSION
    hina_camera camera;
#endif

    // Frame limits (for automated testing)
    int frame_count;
    int max_frames;
    float max_seconds;
    int warmup_frames;

    // Executable path for asset loading (desktop only, nullptr on Android)
    const char* exe_path;

#ifdef __ANDROID__
    // Android-specific
    struct android_app* android_state;
    bool surface_ready;
    bool hina_initialized;
    float touch_start_x;
    float touch_start_y;
    // Timing (using clock_gettime)
    uint64_t last_time_ns;
    uint64_t start_time_ns;
#else
    // Desktop SDL-specific
    SDL_Window* window;
    Uint64 last_time;
    Uint64 start_time;
    Uint64 perf_frequency;
    int mouse_last_x;
    int mouse_last_y;

    // Optional event callback (for custom event processing)
    hina_example_event_callback event_callback;
    void* event_callback_user_data;
#endif

#ifdef HINA_EXAMPLE_HAS_IMGUI
    // ImGui state
    bool imgui_initialized;
    bool imgui_visible;           // Toggle UI visibility
    bool imgui_show_settings;     // Show settings panel (toggled by header)
    bool imgui_frame_active;      // True if NewFrame() has been called this frame
    bool imgui_want_mouse;        // Cached: ImGui wants mouse input (from previous frame)
    bool imgui_want_keyboard;     // Cached: ImGui wants keyboard input (from previous frame)
    hina_texture imgui_font_texture;
    hina_sampler imgui_sampler;
    hina_pipeline imgui_pipeline;
    hina_bind_group_layout imgui_bind_group_layout;

    // Texture registry for ImGui (index 0 = invalid, index 1 = font)
    struct imgui_texture_entry {
        hina_texture_view view;
        hina_sampler sampler;
        hina_bind_group bind_group;
        bool is_custom;
    };
    std::vector<imgui_texture_entry>* imgui_textures;

    // Per-frame buffers
    struct imgui_frame_data {
        hina_buffer vertex_buffer;
        hina_buffer index_buffer;
        void* vertex_mapped;
        void* index_mapped;
    } imgui_frames[HINA_IMGUI_FRAMES_IN_FLIGHT];
#endif
};

#ifdef GLM_VERSION

inline void hina_camera::update(const hina_example_app& app, float rotation_speed, float pan_speed, float zoom_speed) {
    if (app.input_down) {
        rotation.x += app.input_delta_y * rotation_speed;
        rotation.y += app.input_delta_x * rotation_speed;
    }
    if (app.input_down_alt) {
        position.x += app.input_delta_x * pan_speed;
        position.y += app.input_delta_y * pan_speed;
    }
    zoom += app.scroll_delta * zoom_speed;
}

#endif // GLM_VERSION

// ============================================================================
// Example Configuration (Desktop only)
// ============================================================================

#ifndef __ANDROID__

struct hina_example_config {
    const char* title;
    int width;
    int height;
    bool validation;
    bool legacy_renderpass;
    bool no_timeline_semaphore;
    bool force_single_queue;
    bool debug_no_sync2;           // Force legacy vkQueueSubmit/vkCmdPipelineBarrier
    bool force_separate_families;  // Force compute to separate queue family (tests ownership)
    bool force_legacy_tile_pass;   // Force legacy multi-subpass tile pass (tests VK 1.0-1.3)
    bool vsync;
    int max_frames;
    float max_seconds;
    int warmup_frames;     // Frames to skip before collecting perf stats (default: 120)
    const char* exe_path;  // argv[0] for asset path resolution (desktop only)
};

// Compile-time defaults
#ifndef HINA_EXAMPLE_DEFAULT_VALIDATION
#define HINA_EXAMPLE_DEFAULT_VALIDATION 1
#endif
#ifndef HINA_EXAMPLE_DEFAULT_VSYNC
#define HINA_EXAMPLE_DEFAULT_VSYNC 0
#endif
#ifndef HINA_EXAMPLE_DEFAULT_LEGACY_RENDERPASS
#define HINA_EXAMPLE_DEFAULT_LEGACY_RENDERPASS 0
#endif
#ifndef HINA_EXAMPLE_DEFAULT_NO_TIMELINE_SEMAPHORE
#define HINA_EXAMPLE_DEFAULT_NO_TIMELINE_SEMAPHORE 0
#endif
#ifndef HINA_EXAMPLE_DEFAULT_FORCE_SINGLE_QUEUE
#define HINA_EXAMPLE_DEFAULT_FORCE_SINGLE_QUEUE 0
#endif
#ifndef HINA_EXAMPLE_DEFAULT_FORCE_LEGACY_TILE_PASS
#define HINA_EXAMPLE_DEFAULT_FORCE_LEGACY_TILE_PASS 0
#endif
#ifndef HINA_EXAMPLE_DEFAULT_WIDTH
#define HINA_EXAMPLE_DEFAULT_WIDTH 1280
#endif
#ifndef HINA_EXAMPLE_DEFAULT_HEIGHT
#define HINA_EXAMPLE_DEFAULT_HEIGHT 720
#endif
#ifndef HINA_EXAMPLE_DEFAULT_MAX_FRAMES
#define HINA_EXAMPLE_DEFAULT_MAX_FRAMES 0
#endif
#ifndef HINA_EXAMPLE_DEFAULT_MAX_SECONDS
#define HINA_EXAMPLE_DEFAULT_MAX_SECONDS 0.0f
#endif
#ifndef HINA_EXAMPLE_DEFAULT_WARMUP_FRAMES
#define HINA_EXAMPLE_DEFAULT_WARMUP_FRAMES 120
#endif

inline hina_example_config hina_example_config_default() {
    hina_example_config cfg = {};
    cfg.title = "HinaVK Example";
    cfg.width = HINA_EXAMPLE_DEFAULT_WIDTH;
    cfg.height = HINA_EXAMPLE_DEFAULT_HEIGHT;
    cfg.validation = (HINA_EXAMPLE_DEFAULT_VALIDATION != 0);
    cfg.legacy_renderpass = (HINA_EXAMPLE_DEFAULT_LEGACY_RENDERPASS != 0);
    cfg.no_timeline_semaphore = (HINA_EXAMPLE_DEFAULT_NO_TIMELINE_SEMAPHORE != 0);
    cfg.force_single_queue = (HINA_EXAMPLE_DEFAULT_FORCE_SINGLE_QUEUE != 0);
    cfg.force_legacy_tile_pass = (HINA_EXAMPLE_DEFAULT_FORCE_LEGACY_TILE_PASS != 0);
    cfg.vsync = (HINA_EXAMPLE_DEFAULT_VSYNC != 0);
    cfg.max_frames = HINA_EXAMPLE_DEFAULT_MAX_FRAMES;
    cfg.max_seconds = HINA_EXAMPLE_DEFAULT_MAX_SECONDS;
    cfg.warmup_frames = HINA_EXAMPLE_DEFAULT_WARMUP_FRAMES;
    return cfg;
}

inline void hina_example_print_usage(const char* prog_name) {
    printf("Usage: %s [options]\n", prog_name);
    printf("\nOptions:\n");
    printf("  --validation          Enable Vulkan validation layers (default)\n");
    printf("  --no-validation       Disable validation layers\n");
    printf("  --legacy-renderpass   Use legacy VkRenderPass\n");
    printf("  --dynamic-rendering   Use dynamic rendering (VK 1.3+ or VK_KHR_dynamic_rendering)\n");
    printf("  --timeline-semaphore  Use timeline semaphores (default)\n");
    printf("  --no-timeline-semaphore  Use fence-based synchronization\n");
    printf("  --single-queue        Force compute/transfer onto graphics queue\n");
    printf("  --no-single-queue     Allow separate compute/transfer queues\n");
    printf("  --vsync / --no-vsync  Enable/disable vsync\n");
    printf("  --frames=N            Exit after N frames\n");
    printf("  --duration=N          Exit after N seconds\n");
    printf("  --warmup=N            Skip first N frames from perf stats (default: 120)\n");
    printf("  --width=N --height=N  Window dimensions\n");
    printf("  --debug-no-sync2      Force legacy vkQueueSubmit/vkCmdPipelineBarrier\n");
    printf("  --separate-families   Force compute to separate queue family (tests ownership)\n");
    printf("  --legacy-tile-pass    Force legacy multi-subpass tile pass\n");
    printf("  --dynamic-tile-pass   Force dynamic rendering tile pass\n");
    printf("  --help                Show this help\n");
}

inline bool hina_example_parse_args(hina_example_config* cfg, int argc, char** argv) {
    for (int i = 1; i < argc; i++) {
        const char* arg = argv[i];
        if (strcmp(arg, "--help") == 0 || strcmp(arg, "-h") == 0) {
            hina_example_print_usage(argv[0]);
            return false;
        }
        else if (strcmp(arg, "--validation") == 0) cfg->validation = true;
        else if (strcmp(arg, "--no-validation") == 0) cfg->validation = false;
        else if (strcmp(arg, "--legacy-renderpass") == 0) cfg->legacy_renderpass = true;
        else if (strcmp(arg, "--dynamic-rendering") == 0) cfg->legacy_renderpass = false;
        else if (strcmp(arg, "--timeline-semaphore") == 0) cfg->no_timeline_semaphore = false;
        else if (strcmp(arg, "--no-timeline-semaphore") == 0) cfg->no_timeline_semaphore = true;
        else if (strcmp(arg, "--single-queue") == 0) cfg->force_single_queue = true;
        else if (strcmp(arg, "--no-single-queue") == 0) cfg->force_single_queue = false;
        else if (strcmp(arg, "--debug-no-sync2") == 0) cfg->debug_no_sync2 = true;
        else if (strcmp(arg, "--separate-families") == 0) cfg->force_separate_families = true;
        else if (strcmp(arg, "--legacy-tile-pass") == 0) cfg->force_legacy_tile_pass = true;
        else if (strcmp(arg, "--dynamic-tile-pass") == 0) cfg->force_legacy_tile_pass = false;
        else if (strcmp(arg, "--vsync") == 0) cfg->vsync = true;
        else if (strcmp(arg, "--no-vsync") == 0) cfg->vsync = false;
        else if (strncmp(arg, "--frames=", 9) == 0) cfg->max_frames = atoi(arg + 9);
        else if (strncmp(arg, "--duration=", 11) == 0) cfg->max_seconds = static_cast<float>(atof(arg + 11));
        else if (strncmp(arg, "--warmup=", 9) == 0) cfg->warmup_frames = atoi(arg + 9);
        else if (strncmp(arg, "--width=", 8) == 0) cfg->width = atoi(arg + 8);
        else if (strncmp(arg, "--height=", 9) == 0) cfg->height = atoi(arg + 9);
        else {
            fprintf(stderr, "Unknown argument: %s\n", arg);
            hina_example_print_usage(argv[0]);
            return false;
        }
    }
    return true;
}

#endif // !__ANDROID__

// ============================================================================
// Logging Callback
// ============================================================================

inline void hina_example_log_callback(const char* msg) {
    EXAMPLE_LOGI("[HinaVK] %s", msg);
}

// ============================================================================
// Asset/File Loading (Cross-Platform)
// ============================================================================

#ifdef __ANDROID__

// Android: Load from APK assets
inline char* hina_example_load_file(hina_example_app* app, const char* path, size_t* out_size = nullptr) {
    if (!app || !app->android_state || !app->android_state->activity) return nullptr;

    AAssetManager* mgr = app->android_state->activity->assetManager;
    EXAMPLE_LOGI("hina_example_load_file: Opening '%s'", path);

    AAsset* asset = AAssetManager_open(mgr, path, AASSET_MODE_BUFFER);
    if (!asset) {
        EXAMPLE_LOGE("Failed to open asset: %s", path);
        return nullptr;
    }

    off_t size = AAsset_getLength(asset);
    if (size <= 0) {
        EXAMPLE_LOGE("Invalid asset size: %s", path);
        AAsset_close(asset);
        return nullptr;
    }

    char* buffer = static_cast<char*>(malloc(static_cast<size_t>(size) + 1));
    if (!buffer) {
        AAsset_close(asset);
        return nullptr;
    }

    int read = AAsset_read(asset, buffer, static_cast<size_t>(size));
    AAsset_close(asset);

    if (read != size) {
        EXAMPLE_LOGE("Failed to read asset: %s (read %d of %ld)", path, read, (long)size);
        free(buffer);
        return nullptr;
    }

    buffer[size] = '\0';
    if (out_size) *out_size = static_cast<size_t>(size);
    EXAMPLE_LOGI("Loaded %ld bytes from '%s'", (long)size, path);
    return buffer;
}

#else

// Desktop: Load from filesystem (relative to executable or source dir)
inline char* hina_example_load_file(hina_example_app* app, const char* path, size_t* out_size = nullptr) {
    (void)app;  // Not used on desktop

    EXAMPLE_LOGI("hina_example_load_file: Opening '%s'", path);
    SDL_RWops* rw = SDL_RWFromFile(path, "rb");
    if (!rw) {
        EXAMPLE_LOGE("Failed to open file: %s (%s)", path, SDL_GetError());
        return nullptr;
    }

    Sint64 size = SDL_RWsize(rw);
    if (size < 0) {
        EXAMPLE_LOGE("Failed to get file size: %s", path);
        SDL_RWclose(rw);
        return nullptr;
    }

    char* buffer = static_cast<char*>(malloc(static_cast<size_t>(size) + 1));
    if (!buffer) {
        SDL_RWclose(rw);
        return nullptr;
    }

    size_t read = SDL_RWread(rw, buffer, 1, static_cast<size_t>(size));
    SDL_RWclose(rw);

    if (read != static_cast<size_t>(size)) {
        EXAMPLE_LOGE("Failed to read file: %s", path);
        free(buffer);
        return nullptr;
    }

    buffer[size] = '\0';
    if (out_size) *out_size = static_cast<size_t>(size);
    return buffer;
}

// Desktop helper: Get path relative to executable
// Alias for backwards compatibility - use hina_example_get_asset_path instead
inline char* hina_example_get_path(const char* argv0, const char* filename);

inline char* hina_example_get_asset_path(const char* argv0, const char* filename) {
#if defined(HINA_EXAMPLE_SOURCE_DIR)
    {
        const char* base = HINA_EXAMPLE_SOURCE_DIR;
        size_t base_len = strlen(base);
        size_t name_len = strlen(filename);
        bool need_sep = base_len > 0 && base[base_len - 1] != '/' && base[base_len - 1] != '\\';
        char* path = static_cast<char*>(malloc(base_len + name_len + (need_sep ? 2 : 1)));
        if (path) {
            memcpy(path, base, base_len);
            size_t offset = base_len;
            if (need_sep) path[offset++] = '/';
            memcpy(path + offset, filename, name_len + 1);
            return path;
        }
    }
#endif
    const char* last_sep = nullptr;
    for (const char* p = argv0; *p; p++) {
        if (*p == '/' || *p == '\\') last_sep = p;
    }
    size_t dir_len = last_sep ? static_cast<size_t>(last_sep - argv0 + 1) : 0;
    size_t name_len = strlen(filename);
    char* path = static_cast<char*>(malloc(dir_len + name_len + 1));
    if (!path) return nullptr;
    if (dir_len > 0) memcpy(path, argv0, dir_len);
    memcpy(path + dir_len, filename, name_len + 1);
    return path;
}

// Alias for backwards compatibility
inline char* hina_example_get_path(const char* argv0, const char* filename) {
    return hina_example_get_asset_path(argv0, filename);
}

#endif // __ANDROID__

// ============================================================================
// Cross-Platform Shader/Asset Path Helper
// ============================================================================

// Get path to a shader or asset file, works on both Android and Desktop.
// On Android: returns heap-allocated "shaders/filename"
// On Desktop: returns heap-allocated path relative to executable
// Always free the result with free()
inline char* hina_example_shader_path(hina_example_app* app, const char* filename) {
#ifdef __ANDROID__
    (void)app;
    const char* prefix = "shaders/";
    size_t prefix_len = strlen(prefix);
    size_t name_len = strlen(filename);
    char* path = static_cast<char*>(malloc(prefix_len + name_len + 1));
    if (path) {
        memcpy(path, prefix, prefix_len);
        memcpy(path + prefix_len, filename, name_len + 1);
    }
    return path;
#else
    return hina_example_get_asset_path(app->exe_path, filename);
#endif
}

// Get path to a texture or other asset file (same as shader path on both platforms)
inline char* hina_example_asset_path(hina_example_app* app, const char* filename) {
#ifdef __ANDROID__
    (void)app;
    size_t name_len = strlen(filename);
    char* path = static_cast<char*>(malloc(name_len + 1));
    if (path) memcpy(path, filename, name_len + 1);
    return path;
#else
    return hina_example_get_asset_path(app->exe_path, filename);
#endif
}

// ============================================================================
// Platform-Specific Implementation
// ============================================================================

#ifdef __ANDROID__

// ----------------------------------------------------------------------------
// Android NativeActivity Implementation
// ----------------------------------------------------------------------------

// Helper: get current time in nanoseconds
inline uint64_t hina_example_get_time_ns() {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return static_cast<uint64_t>(ts.tv_sec) * 1000000000ULL + static_cast<uint64_t>(ts.tv_nsec);
}

// Forward declarations for callbacks
inline void hina_example_handle_cmd(struct android_app* app, int32_t cmd);
inline int32_t hina_example_handle_input(struct android_app* app, AInputEvent* event);

// Initialize Android app (call from android_main)
inline bool hina_example_init_android(hina_example_app* app, struct android_app* state, const char* title) {
    (void)title;  // Android doesn't use window title

    memset(app, 0, sizeof(*app));
    app->android_state = state;
    app->running = true;
    app->focused = true;

    // Initialize timing
    uint64_t now = hina_example_get_time_ns();
    app->last_time_ns = now;
    app->start_time_ns = now;

    // Set up callbacks
    state->userData = app;
    state->onAppCmd = hina_example_handle_cmd;
    state->onInputEvent = hina_example_handle_input;

    EXAMPLE_LOGI("hina_example_init_android: Waiting for window...");
    return true;
}

// Initialize HinaVK when window is ready
inline bool hina_example_init_hina(hina_example_app* app) {
    if (app->hina_initialized) return true;

    ANativeWindow* window = app->android_state->window;
    if (!window) {
        EXAMPLE_LOGE("hina_example_init_hina: No native window!");
        return false;
    }

    app->width = ANativeWindow_getWidth(window);
    app->height = ANativeWindow_getHeight(window);
    app->native_window = window;
    EXAMPLE_LOGI("hina_example_init_hina: Window size %dx%d", app->width, app->height);

    hina_desc init_desc = {};
    init_desc.native_window = window;
    init_desc.native_display = nullptr;
    init_desc.flags = 0;
    init_desc.log_fn = hina_example_log_callback;

    if (!hina_init(&init_desc)) {
        EXAMPLE_LOGE("hina_example_init_hina: hina_init failed!");
        return false;
    }

    // Configure swapchain with vsync (FIFO)
    // NOTE: PREROTATE_BIT not set - compositor handles rotation (avoids Mali flickering bug)
    hina_swapchain_desc swap_desc = {};
    swap_desc.present_mode = HINA_PRESENT_MODE_FIFO;
    hina_configure_swapchain(&swap_desc);

    // Initialize ImGui (if available)
    if (!hina_example_imgui_init(app)) {
        EXAMPLE_LOGE("hina_example_imgui_init failed!");
        // Continue without ImGui - not fatal
    }

    app->hina_initialized = true;
    EXAMPLE_LOGI("hina_example_init_hina: Complete!");
    return true;
}

// Handle Android lifecycle commands
inline void hina_example_handle_cmd(struct android_app* state, int32_t cmd) {
    hina_example_app* app = static_cast<hina_example_app*>(state->userData);

    switch (cmd) {
        case APP_CMD_INIT_WINDOW:
            EXAMPLE_LOGI("APP_CMD_INIT_WINDOW");
            app->native_window = state->window;
            if (state->window) {
                app->width = ANativeWindow_getWidth(state->window);
                app->height = ANativeWindow_getHeight(state->window);

                // If HinaVK is already initialized, we MUST recreate the surface
                // because the old surface was destroyed with the previous window
                if (app->hina_initialized) {
                    EXAMPLE_LOGI("Recreating surface for new window");
                    if (!hina_recreate_surface(state->window, nullptr)) {
                        EXAMPLE_LOGE("Failed to recreate surface!");
                    }
                }
                app->surface_ready = true;
            }
            break;

        case APP_CMD_TERM_WINDOW:
            EXAMPLE_LOGI("APP_CMD_TERM_WINDOW");
            app->surface_ready = false;
            break;

        case APP_CMD_GAINED_FOCUS:
            EXAMPLE_LOGI("APP_CMD_GAINED_FOCUS");
            app->focused = true;
            break;

        case APP_CMD_LOST_FOCUS:
            EXAMPLE_LOGI("APP_CMD_LOST_FOCUS");
            app->focused = false;
            break;

        case APP_CMD_PAUSE:
            EXAMPLE_LOGI("APP_CMD_PAUSE");
            app->paused = true;
            break;

        case APP_CMD_RESUME:
            EXAMPLE_LOGI("APP_CMD_RESUME");
            app->paused = false;
            break;

        case APP_CMD_DESTROY:
            EXAMPLE_LOGI("APP_CMD_DESTROY");
            app->running = false;
            break;

        case APP_CMD_CONFIG_CHANGED:
            EXAMPLE_LOGI("APP_CMD_CONFIG_CHANGED");
            if (state->window) {
                app->width = ANativeWindow_getWidth(state->window);
                app->height = ANativeWindow_getHeight(state->window);
            }
            break;
    }
}

// Handle Android input events
inline int32_t hina_example_handle_input(struct android_app* state, AInputEvent* event) {
    hina_example_app* app = static_cast<hina_example_app*>(state->userData);

    if (AInputEvent_getType(event) == AINPUT_EVENT_TYPE_MOTION) {
        int32_t action = AMotionEvent_getAction(event) & AMOTION_EVENT_ACTION_MASK;
        float x = AMotionEvent_getX(event, 0);
        float y = AMotionEvent_getY(event, 0);

#ifdef HINA_EXAMPLE_HAS_IMGUI
        // Process ImGui touch input first
        bool imgui_consumed = hina_example_imgui_process_touch(app, action, x, y);
#else
        bool imgui_consumed = false;
#endif

        // Only update app input state if ImGui didn't consume the touch
        if (!imgui_consumed) {
            switch (action) {
                case AMOTION_EVENT_ACTION_DOWN:
                    app->input_down = true;
                    app->touch_start_x = x;
                    app->touch_start_y = y;
                    app->input_x = x;
                    app->input_y = y;
                    app->input_delta_x = 0;
                    app->input_delta_y = 0;
                    break;

                case AMOTION_EVENT_ACTION_MOVE:
                    if (app->input_down) {
                        app->input_delta_x = x - app->input_x;
                        app->input_delta_y = y - app->input_y;
                        app->input_x = x;
                        app->input_y = y;
                    }
                    break;

                case AMOTION_EVENT_ACTION_UP:
                case AMOTION_EVENT_ACTION_CANCEL:
                    app->input_down = false;
                    app->input_delta_x = 0;
                    app->input_delta_y = 0;
                    break;
            }
        }
        return 1;
    }
    return 0;
}

// Poll events (Android version)
inline bool hina_example_poll(hina_example_app* app) {
    // Update timing
    uint64_t current_time = hina_example_get_time_ns();
    app->delta_time = static_cast<float>(current_time - app->last_time_ns) / 1e9f;
    app->last_time_ns = current_time;
    app->elapsed_time = static_cast<float>(current_time - app->start_time_ns) / 1e9f;

    // Reset per-frame input deltas
    app->input_delta_x = 0;
    app->input_delta_y = 0;

    // Process pending events
    int events;
    struct android_poll_source* source;

    // Block if paused, poll if active
    int timeout = app->paused ? -1 : 0;
    while (ALooper_pollAll(timeout, nullptr, &events, (void**)&source) >= 0) {
        if (source) {
            source->process(app->android_state, source);
        }
        if (app->android_state->destroyRequested) {
            EXAMPLE_LOGI("hina_example_poll: Destroy requested");
            app->running = false;
            return false;
        }
        // After processing one event, only poll (don't block)
        timeout = 0;
    }

    // Update frame count
    app->frame_count++;

    // Check limits
    if (app->max_frames > 0 && app->frame_count >= app->max_frames) {
        app->running = false;
    }
    if (app->max_seconds > 0.0f && app->elapsed_time >= app->max_seconds) {
        app->running = false;
    }

    return app->running;
}

// Check if we should render this frame
inline bool hina_example_should_render(hina_example_app* app) {
    if (app->paused) return false;
    if (!app->surface_ready) return false;
    if (hina_is_surface_lost()) return false;
    return true;
}

// Try to recover from surface lost
inline bool hina_example_try_recover_surface(hina_example_app* app) {
    if (!hina_is_surface_lost()) return true;
    if (app->paused) return false;

    ANativeWindow* window = app->android_state->window;
    if (!window) return false;

    EXAMPLE_LOGI("Attempting surface recovery...");
    if (hina_recreate_surface(window, nullptr)) {
        EXAMPLE_LOGI("Surface recovery successful!");
        return true;
    }
    EXAMPLE_LOGE("Surface recovery failed");
    return false;
}

// Shutdown (Android version)
inline void hina_example_shutdown(hina_example_app* app) {
    hina_example_imgui_shutdown(app);
    if (app->hina_initialized) {
        hina_shutdown();
        app->hina_initialized = false;
    }
    EXAMPLE_LOGI("hina_example_shutdown: Complete");
}

#else

// ----------------------------------------------------------------------------
// Desktop SDL Implementation
// ----------------------------------------------------------------------------

inline bool hina_example_init(hina_example_app* app, const hina_example_config* cfg) {
#ifdef _WIN32
    // Note: Vulkan validation layers (VK_LAYER_KHRONOS_validation) allocate internal
    // tracking state that persists until layer unload, which occurs AFTER CRT atexit.
    // This causes false-positive leak reports (~7 blocks) in debug builds with validation.
    // HinaVK's own allocator (hina_alloc_dump_stats) shows 0 leaks - this is expected.
    _CrtSetDbgFlag(_CRTDBG_ALLOC_MEM_DF | _CRTDBG_LEAK_CHECK_DF);
#endif

    memset(app, 0, sizeof(*app));
    app->width = cfg->width;
    app->height = cfg->height;
    app->running = true;
    app->focused = true;
    app->perf_frequency = SDL_GetPerformanceFrequency();
    app->last_time = SDL_GetPerformanceCounter();
    app->start_time = app->last_time;
    app->max_frames = cfg->max_frames;
    app->max_seconds = cfg->max_seconds;
    app->warmup_frames = cfg->warmup_frames;
    app->exe_path = cfg->exe_path;

    // SDL Initialization
    if (SDL_Init(SDL_INIT_VIDEO) != 0) {
        EXAMPLE_LOGE("SDL init failed: %s", SDL_GetError());
        return false;
    }

    app->window = SDL_CreateWindow(cfg->title,
        SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED,
        cfg->width, cfg->height,
        SDL_WINDOW_VULKAN | SDL_WINDOW_RESIZABLE | SDL_WINDOW_ALLOW_HIGHDPI);

    if (!app->window) {
        EXAMPLE_LOGE("SDL window creation failed: %s", SDL_GetError());
        SDL_Quit();
        return false;
    }

    // Get native window handle
    SDL_SysWMinfo wm = {};
    SDL_VERSION(&wm.version);
    if (!SDL_GetWindowWMInfo(app->window, &wm)) {
        EXAMPLE_LOGE("SDL_GetWindowWMInfo failed: %s", SDL_GetError());
        SDL_DestroyWindow(app->window);
        SDL_Quit();
        return false;
    }

#if defined(SDL_VIDEO_DRIVER_WINDOWS)
    app->native_window = reinterpret_cast<void*>(wm.info.win.window);
#elif defined(SDL_VIDEO_DRIVER_X11)
    app->native_window = reinterpret_cast<void*>(wm.info.x11.window);
    app->native_display = reinterpret_cast<void*>(wm.info.x11.display);
#elif defined(SDL_VIDEO_DRIVER_WAYLAND)
    app->native_window = reinterpret_cast<void*>(wm.info.wl.surface);
    app->native_display = reinterpret_cast<void*>(wm.info.wl.display);
#elif defined(SDL_VIDEO_DRIVER_COCOA)
    app->native_window = reinterpret_cast<void*>(wm.info.cocoa.window);
#endif

    if (!app->native_window) {
        EXAMPLE_LOGE("Failed to get native window handle");
        SDL_DestroyWindow(app->window);
        SDL_Quit();
        return false;
    }

    // Initialize HinaVK
    uint32_t flags = 0;
    if (cfg->validation) flags |= HINA_INIT_VALIDATION_BIT;
    if (cfg->legacy_renderpass) flags |= HINA_DEBUG_FORCE_LEGACY_RENDERPASS_BIT;
    if (cfg->no_timeline_semaphore) flags |= HINA_DEBUG_NO_TIMELINE_SEMAPHORE_BIT;
    if (cfg->force_single_queue) flags |= HINA_DEBUG_FORCE_SINGLE_QUEUE_BIT;
    if (cfg->debug_no_sync2) flags |= HINA_DEBUG_NO_SYNC2_BIT;
    if (cfg->force_separate_families) flags |= HINA_DEBUG_FORCE_SEPARATE_FAMILIES_BIT;
    if (cfg->force_legacy_tile_pass) flags |= HINA_DEBUG_FORCE_LEGACY_TILE_PASS_BIT;

    hina_desc init_desc = {};
    init_desc.native_window = app->native_window;
    init_desc.native_display = app->native_display;
    init_desc.flags = static_cast<hina_init_flags>(flags);
    init_desc.log_fn = hina_example_log_callback;

    if (!hina_init(&init_desc)) {
        EXAMPLE_LOGE("hina_init failed!");
        SDL_DestroyWindow(app->window);
        SDL_Quit();
        return false;
    }

    {
        hina_swapchain_desc swap_desc = {};
        swap_desc.present_mode = cfg->vsync ? HINA_PRESENT_MODE_FIFO : HINA_PRESENT_MODE_MAILBOX;
        hina_configure_swapchain(&swap_desc);
    }

    // Initialize ImGui (if available)
    if (!hina_example_imgui_init(app)) {
        EXAMPLE_LOGE("hina_example_imgui_init failed!");
        // Continue without ImGui - not fatal
    }

    EXAMPLE_LOGI("hina_example_init: Complete!");
    return true;
}

inline bool hina_example_poll(hina_example_app* app) {
    // Update timing
    Uint64 current_time = SDL_GetPerformanceCounter();
    app->delta_time = static_cast<float>(current_time - app->last_time) / static_cast<float>(app->perf_frequency);
    app->last_time = current_time;
    app->elapsed_time = static_cast<float>(current_time - app->start_time) / static_cast<float>(app->perf_frequency);

    // Reset per-frame input
    app->input_delta_x = 0;
    app->input_delta_y = 0;
    app->scroll_delta = 0;
    app->key_space = false;  // Reset per-frame key state

    // Process events
    SDL_Event event;
    while (SDL_PollEvent(&event)) {
        // Process ImGui events first
#ifdef HINA_EXAMPLE_HAS_IMGUI
        bool imgui_consumed = hina_example_imgui_process_event(app, &event);
#else
        bool imgui_consumed = false;
#endif

        // Call optional event callback (for custom processing)
        bool event_consumed = imgui_consumed;
        if (app->event_callback) {
            event_consumed = app->event_callback(&event, app->event_callback_user_data) || event_consumed;
        }

        switch (event.type) {
            case SDL_QUIT:
                app->running = false;
                break;

            case SDL_KEYDOWN:
                if (event.key.keysym.sym == SDLK_ESCAPE) {
                    app->running = false;
                } else if (event.key.keysym.sym == SDLK_SPACE) {
                    if (!app->key_space_held) {  // Only trigger on initial press
                        app->key_space = true;
                    }
                    app->key_space_held = true;
                }
                break;

            case SDL_KEYUP:
                if (event.key.keysym.sym == SDLK_SPACE) {
                    app->key_space_held = false;
                }
                break;

            case SDL_MOUSEBUTTONDOWN:
                if (event.button.button == SDL_BUTTON_LEFT) {
                    app->input_down = true;
                    app->mouse_last_x = event.button.x;
                    app->mouse_last_y = event.button.y;
                } else if (event.button.button == SDL_BUTTON_RIGHT) {
                    app->input_down_alt = true;
                    app->mouse_last_x = event.button.x;
                    app->mouse_last_y = event.button.y;
                }
                break;

            case SDL_MOUSEBUTTONUP:
                if (event.button.button == SDL_BUTTON_LEFT) {
                    app->input_down = false;
                } else if (event.button.button == SDL_BUTTON_RIGHT) {
                    app->input_down_alt = false;
                }
                break;

            case SDL_MOUSEMOTION:
                app->input_x = static_cast<float>(event.motion.x);
                app->input_y = static_cast<float>(event.motion.y);
                if (app->input_down || app->input_down_alt) {
                    app->input_delta_x = static_cast<float>(event.motion.x - app->mouse_last_x);
                    app->input_delta_y = static_cast<float>(event.motion.y - app->mouse_last_y);
                    app->mouse_last_x = event.motion.x;
                    app->mouse_last_y = event.motion.y;
                }
                break;

            case SDL_MOUSEWHEEL:
                app->scroll_delta = event.wheel.y;
                break;

            case SDL_WINDOWEVENT:
                if (event.window.event == SDL_WINDOWEVENT_FOCUS_GAINED) {
                    app->focused = true;
                } else if (event.window.event == SDL_WINDOWEVENT_FOCUS_LOST) {
                    app->focused = false;
                }
                break;
        }
    }

    // Update window size
    SDL_GetWindowSize(app->window, &app->width, &app->height);

    // Update frame count
    app->frame_count++;

    // Check limits
    if (app->max_frames > 0 && app->frame_count >= app->max_frames) {
        app->running = false;
    }
    if (app->max_seconds > 0.0f && app->elapsed_time >= app->max_seconds) {
        app->running = false;
    }

    return app->running;
}

inline bool hina_example_should_render(hina_example_app* app) {
    (void)app;
    return !hina_is_surface_lost();
}

inline bool hina_example_try_recover_surface(hina_example_app* app) {
    (void)app;
    return true;  // Desktop doesn't usually lose surface
}

inline void hina_example_shutdown(hina_example_app* app) {
    hina_example_imgui_shutdown(app);
    hina_shutdown();
    if (app->window) {
        SDL_DestroyWindow(app->window);
        app->window = nullptr;
    }
    SDL_Quit();
}

// Set an event callback for custom SDL event processing (e.g., ImGui)
inline void hina_example_set_event_callback(hina_example_app* app,
                                            hina_example_event_callback callback,
                                            void* user_data) {
    app->event_callback = callback;
    app->event_callback_user_data = user_data;
}

#endif // __ANDROID__

// ============================================================================
// Common Utilities (Cross-Platform)
// ============================================================================

inline float hina_example_aspect(const hina_example_app* app) {
    return static_cast<float>(app->width) / static_cast<float>(app->height);
}

// Cross-platform sleep (in milliseconds)
inline void hina_example_sleep(uint32_t ms) {
#ifdef __ANDROID__
    struct timespec ts;
    ts.tv_sec = ms / 1000;
    ts.tv_nsec = (ms % 1000) * 1000000;
    nanosleep(&ts, nullptr);
#else
    SDL_Delay(ms);
#endif
}

/**
 * Present frame with automatic ImGui rendering.
 * This is the recommended way to end a frame - it handles:
 * 1. Ending the current render pass
 * 2. Complete ImGui frame lifecycle (NewFrame, widgets, Render, draw)
 * 3. Submitting the command buffer
 * 4. Ending the frame
 *
 * The entire ImGui lifecycle is contained here for robustness:
 * - No coordination needed between poll() and render()
 * - Identical behavior on desktop and Android
 * - First-frame issues are avoided
 *
 * Usage:
 *   hina_cmd_begin_pass(cmd, &pass);
 *   // ... your scene rendering ...
 *   hina_example_present_frame(app, cmd, swapchain);  // replaces end_pass + submit + frame_end
 */
inline void hina_example_present_frame(hina_example_app* app, hina_cmd* cmd, hina_swapchain_image swapchain) {
    // End the scene pass
    hina_cmd_end_pass(cmd);

#ifdef HINA_EXAMPLE_HAS_IMGUI
    if (app->imgui_initialized) {
        if (!app->imgui_frame_active) {
            ImGuiIO& io = ImGui::GetIO();
            io.DisplaySize = ImVec2(static_cast<float>(app->width), static_cast<float>(app->height));
            io.DeltaTime = app->delta_time > 0.0f ? app->delta_time : (1.0f / 60.0f);
            ImGui::NewFrame();
            app->imgui_frame_active = true;
        }

        if (app->imgui_visible) {
            ImGui::SetNextWindowPos(ImVec2(10, 10), ImGuiCond_FirstUseEver);
            ImGui::SetNextWindowBgAlpha(0.5f);

            ImGuiWindowFlags overlay_flags =
                ImGuiWindowFlags_NoDecoration |
                ImGuiWindowFlags_AlwaysAutoResize |
                ImGuiWindowFlags_NoSavedSettings |
                ImGuiWindowFlags_NoFocusOnAppearing |
                ImGuiWindowFlags_NoNav;

            if (ImGui::Begin("##stats_overlay", nullptr, overlay_flags)) {
                // Unified stats content (device, FPS, frame stats, caps, internal paths)
                hina_example_imgui_stats_content(app, true);

                // Settings header for examples to add custom controls
                if (ImGui::CollapsingHeader("Settings", &app->imgui_show_settings)) {
                }
            }
            ImGui::End();
        }

        ImGui::Render();
        app->imgui_frame_active = false;
        ImDrawData* draw_data = ImGui::GetDrawData();

        ImGuiIO& io_post = ImGui::GetIO();
        app->imgui_want_mouse = io_post.WantCaptureMouse;
        app->imgui_want_keyboard = io_post.WantCaptureKeyboard;

        if (draw_data && draw_data->TotalVtxCount > 0) {
            hina_pass_action imgui_pass = {};
            imgui_pass.colors[0].image = hina_texture_get_default_view(swapchain.texture);
            imgui_pass.colors[0].load_op = HINA_LOAD_OP_LOAD;
            imgui_pass.colors[0].store_op = HINA_STORE_OP_STORE;

            hina_cmd_begin_pass(cmd, &imgui_pass);

            // Viewport uses swapchain texture dimensions
            uint32_t fb_w, fb_h;
            hina_get_texture_size(swapchain.texture, &fb_w, &fb_h);

            hina_viewport viewport = {
                0.0f, 0.0f,
                static_cast<float>(fb_w), static_cast<float>(fb_h),
                0.0f, 1.0f
            };
            hina_cmd_set_viewport(cmd, &viewport);
            hina_example_draw_imgui_data(app, cmd, draw_data, fb_w, fb_h);
            hina_cmd_end_pass(cmd);
        }
    }
#else
    (void)app;
    (void)swapchain;
#endif

    hina_frame_submit(cmd);
    hina_frame_end();
}

// ============================================================================
// Depth Buffer Helper
// ============================================================================

struct hina_depth_buffer {
    hina_texture texture;
    uint32_t width;
    uint32_t height;
};

inline bool hina_depth_buffer_init(hina_depth_buffer* db, uint32_t width, uint32_t height) {
    hina_texture_desc desc = {};
    desc.type = HINA_TEX_TYPE_2D;
    desc.format = HINA_FORMAT_D32_SFLOAT;
    desc.width = width;
    desc.height = height;
    desc.layers = 1;
    desc.mip_levels = 1;
    desc.samples = HINA_SAMPLE_COUNT_1_BIT;
    desc.usage = static_cast<hina_texture_usage_flags>(HINA_TEXTURE_RENDER_TARGET_BIT | HINA_TEXTURE_SAMPLED_BIT);

    db->texture = hina_make_texture(&desc);
    db->width = width;
    db->height = height;
    return hina_texture_is_valid(db->texture);
}

inline void hina_depth_buffer_destroy(hina_depth_buffer* db) {
    if (hina_texture_is_valid(db->texture)) {
        hina_destroy_texture(db->texture);
        db->texture = {};
    }
}

inline bool hina_depth_buffer_resize(hina_depth_buffer* db, uint32_t width, uint32_t height) {
    if (db->width == width && db->height == height) return true;
    hina_depth_buffer_destroy(db);
    return hina_depth_buffer_init(db, width, height);
}

// ============================================================================
// MSAA Buffer Helpers
// ============================================================================

struct hina_msaa_buffers {
    hina_texture color;
    hina_texture depth;
    uint32_t width;
    uint32_t height;
    hina_sample_count samples;
};

inline bool hina_msaa_buffers_init(hina_msaa_buffers* msaa, uint32_t width, uint32_t height,
                                    hina_sample_count samples = HINA_SAMPLE_COUNT_4_BIT) {
    hina_texture_desc color_desc = {};
    color_desc.type = HINA_TEX_TYPE_2D;
    color_desc.format = hina_get_surface_format();  // Match swapchain format
    color_desc.width = width;
    color_desc.height = height;
    color_desc.layers = 1;
    color_desc.mip_levels = 1;
    color_desc.samples = samples;
    color_desc.usage = HINA_TEXTURE_RENDER_TARGET_BIT;

    msaa->color = hina_make_texture(&color_desc);
    if (!hina_texture_is_valid(msaa->color))
        return false;

    hina_texture_desc depth_desc = {};
    depth_desc.type = HINA_TEX_TYPE_2D;
    depth_desc.format = HINA_FORMAT_D32_SFLOAT;
    depth_desc.width = width;
    depth_desc.height = height;
    depth_desc.layers = 1;
    depth_desc.mip_levels = 1;
    depth_desc.samples = samples;
    depth_desc.usage = HINA_TEXTURE_RENDER_TARGET_BIT;

    msaa->depth = hina_make_texture(&depth_desc);
    if (!hina_texture_is_valid(msaa->depth)) {
        hina_destroy_texture(msaa->color);
        msaa->color = {};
        return false;
    }

    msaa->width = width;
    msaa->height = height;
    msaa->samples = samples;
    return true;
}

inline void hina_msaa_buffers_destroy(hina_msaa_buffers* msaa) {
    if (hina_texture_is_valid(msaa->depth)) {
        hina_destroy_texture(msaa->depth);
        msaa->depth = {};
    }
    if (hina_texture_is_valid(msaa->color)) {
        hina_destroy_texture(msaa->color);
        msaa->color = {};
    }
}

inline bool hina_msaa_buffers_resize(hina_msaa_buffers* msaa, uint32_t width, uint32_t height) {
    if (msaa->width == width && msaa->height == height) return true;
    hina_sample_count samples = msaa->samples;
    hina_msaa_buffers_destroy(msaa);
    return hina_msaa_buffers_init(msaa, width, height, samples);
}

// ============================================================================
// Uniform Ring Helper
// ============================================================================

#ifndef HINA_EXAMPLE_MAX_FRAMES_IN_FLIGHT
#define HINA_EXAMPLE_MAX_FRAMES_IN_FLIGHT 3
#endif

typedef struct hina_uniform_ring {
    hina_buffer buffers[HINA_EXAMPLE_MAX_FRAMES_IN_FLIGHT];
    void* mapped[HINA_EXAMPLE_MAX_FRAMES_IN_FLIGHT];
    uint64_t size;
    uint64_t offsets[HINA_EXAMPLE_MAX_FRAMES_IN_FLIGHT];
    uint32_t alignment;
} hina_uniform_ring;

static inline uint64_t hina_example_align_u64(uint64_t value, uint64_t align) {
    if (align <= 1) return value;
    return (value + align - 1) & ~(align - 1);
}

static inline uint32_t hina_example_uniform_ring_slot(uint64_t frame_index) {
    return (uint32_t)(frame_index % HINA_EXAMPLE_MAX_FRAMES_IN_FLIGHT);
}

static inline void hina_uniform_ring_shutdown(hina_uniform_ring* ring) {
    if (!ring) return;
    for (uint32_t i = 0; i < HINA_EXAMPLE_MAX_FRAMES_IN_FLIGHT; ++i) {
        if (ring->buffers[i].id != HINA_INVALID_HANDLE) {
            hina_destroy_buffer(ring->buffers[i]);
        }
        ring->buffers[i].id = HINA_INVALID_HANDLE;
        ring->mapped[i] = nullptr;
        ring->offsets[i] = 0;
    }
    ring->size = 0;
    ring->alignment = 0;
}

static inline bool hina_uniform_ring_init(hina_uniform_ring* ring, size_t size_per_frame) {
    if (!ring || size_per_frame == 0) return false;
    memset(ring, 0, sizeof(*ring));
    ring->size = (uint64_t)size_per_frame;

    const hina_device_caps* caps = hina_get_device_caps();
    ring->alignment = caps ? caps->min_uniform_buffer_alignment : 1;
    if (ring->alignment == 0) ring->alignment = 1;

    hina_buffer_desc desc = {0};
    desc.size = size_per_frame;
    desc.memory = HINA_BUFFER_CPU;
    desc.usage = HINA_BUFFER_UNIFORM;

    for (uint32_t i = 0; i < HINA_EXAMPLE_MAX_FRAMES_IN_FLIGHT; ++i) {
        ring->buffers[i] = hina_make_buffer(&desc);
        if (ring->buffers[i].id == HINA_INVALID_HANDLE) {
            hina_uniform_ring_shutdown(ring);
            return false;
        }
        ring->mapped[i] = hina_mapped_buffer_ptr(ring->buffers[i]);
        if (!ring->mapped[i]) {
            hina_uniform_ring_shutdown(ring);
            return false;
        }
    }
    return true;
}

static inline void hina_uniform_ring_begin_frame(hina_uniform_ring* ring) {
    if (!ring) return;
    uint32_t slot = hina_example_uniform_ring_slot(hina_get_frame_index());
    ring->offsets[slot] = 0;
}

static inline void* hina_uniform_ring_alloc(hina_uniform_ring* ring, size_t size,
                                            hina_buffer* out_buffer, uint64_t* out_offset) {
    if (!ring || size == 0 || !out_buffer || !out_offset) return nullptr;
    uint32_t slot = hina_example_uniform_ring_slot(hina_get_frame_index());
    if (ring->buffers[slot].id == HINA_INVALID_HANDLE || !ring->mapped[slot]) return nullptr;

    uint64_t aligned_offset = hina_example_align_u64(ring->offsets[slot], ring->alignment);
    if (aligned_offset + size > ring->size) {
        EXAMPLE_LOGE("hina_uniform_ring_alloc: out of space");
        return nullptr;
    }

    ring->offsets[slot] = aligned_offset + size;
    *out_buffer = ring->buffers[slot];
    *out_offset = aligned_offset;
    return static_cast<uint8_t*>(ring->mapped[slot]) + aligned_offset;
}

static inline bool hina_uniform_ring_write(hina_uniform_ring* ring, const void* data, size_t size,
                                           hina_buffer* out_buffer, uint64_t* out_offset) {
    if (!data) return false;
    void* dst = hina_uniform_ring_alloc(ring, size, out_buffer, out_offset);
    if (!dst) return false;
    memcpy(dst, data, size);
    hina_flush_buffer(*out_buffer, *out_offset, size);
    return true;
}

// ============================================================================
// Pipeline Creation Helper
// ============================================================================

inline hina_pipeline hina_example_make_pipeline_from_hsl(
    hina_example_app* app,
    const char* shader_path,
    const hina_hsl_pipeline_desc* desc,
    char** out_error)
{
    if (!desc) return hina_pipeline{HINA_INVALID_HANDLE};

    // Load shader source
    char* source = hina_example_load_file(app, shader_path, nullptr);
    if (!source) {
        if (out_error) {
            const char* msg = "Failed to load shader file";
            *out_error = static_cast<char*>(malloc(strlen(msg) + 1));
            if (*out_error) strcpy(*out_error, msg);
        }
        return hina_pipeline{HINA_INVALID_HANDLE};
    }

    // Compile HSL
    hina_hsl_module* module = hslc_compile_hsl_source(source, shader_path, out_error);
    free(source);

    if (!module) {
        return hina_pipeline{HINA_INVALID_HANDLE};
    }

    // Create pipeline (errors logged internally via EXAMPLE_LOGE)
    hina_pipeline pipeline = hina_make_pipeline_from_module(module, desc, NULL);
    hslc_hsl_module_free(module);

    return pipeline;
}

// ============================================================================
// Transient Bind Group Helper (examples only)
// ============================================================================

inline hina_transient_bind_group hina_example_make_transient_bind_group(
    const hina_bind_group_desc* desc)
{
    hina_transient_bind_group tbg = {};
    if (!desc || desc->layout.id == HINA_INVALID_HANDLE) return tbg;

    tbg = hina_alloc_transient_bind_group(desc->layout);
    if (!tbg.internal.set) return tbg;

    for (uint32_t i = 0; i < desc->entry_count; ++i) {
        const hina_bind_group_entry& e = desc->entries[i];
        switch (e.type) {
        case HINA_DESC_TYPE_UNIFORM_BUFFER:
        case HINA_DESC_TYPE_STORAGE_BUFFER:
            hina_transient_write_buffer(&tbg, e.binding, e.type,
                                        e.buffer.buffer, e.buffer.offset, e.buffer.size);
            break;
        case HINA_DESC_TYPE_COMBINED_IMAGE_SAMPLER:
            hina_transient_write_combined_image(&tbg, e.binding, e.combined.view, e.combined.sampler);
            break;
        case HINA_DESC_TYPE_STORAGE_IMAGE:
            hina_transient_write_storage_image(&tbg, e.binding, e.view);
            break;
        case HINA_DESC_TYPE_INPUT_ATTACHMENT:
            hina_transient_write_input_attachment(&tbg, e.binding, e.view);
            break;
        default:
            EXAMPLE_LOGE("Example transient bind group: unsupported entry type %u", (unsigned)e.type);
            break;
        }
    }

    return tbg;
}

// ============================================================================
// Performance Statistics (Desktop only)
// ============================================================================

#ifndef __ANDROID__

#ifndef HINA_PERF_HISTORY_SIZE
#define HINA_PERF_HISTORY_SIZE 2048
#endif

struct hina_perf_stats {
    float frame_times[HINA_PERF_HISTORY_SIZE];
    uint32_t history_index;
    uint32_t history_count;
    float min_frame_time;
    float max_frame_time;
    double total_frame_time;
    uint32_t total_frames;
    uint32_t warmup_frames;
    bool warmup_complete;
    float fps_accumulator;
    uint32_t fps_frame_count;
    float current_fps;
};

inline void hina_perf_stats_init(hina_perf_stats* stats, uint32_t warmup = 10) {
    memset(stats, 0, sizeof(*stats));
    stats->min_frame_time = 1e9f;
    stats->warmup_frames = warmup;
    stats->warmup_complete = (warmup == 0);
}

inline void hina_perf_stats_update(hina_perf_stats* stats, float delta_time) {
    if (!stats->warmup_complete) {
        stats->warmup_frames--;
        if (stats->warmup_frames == 0) stats->warmup_complete = true;
        return;
    }
    if (delta_time <= 0.0f || delta_time > 1.0f) return;

    stats->frame_times[stats->history_index] = delta_time;
    stats->history_index = (stats->history_index + 1) % HINA_PERF_HISTORY_SIZE;
    if (stats->history_count < HINA_PERF_HISTORY_SIZE) stats->history_count++;

    stats->total_frame_time += delta_time;
    stats->total_frames++;
    if (delta_time < stats->min_frame_time) stats->min_frame_time = delta_time;
    if (delta_time > stats->max_frame_time) stats->max_frame_time = delta_time;

    stats->fps_accumulator += delta_time;
    stats->fps_frame_count++;
    if (stats->fps_accumulator >= 0.5f) {
        stats->current_fps = stats->fps_frame_count / stats->fps_accumulator;
        stats->fps_accumulator = 0.0f;
        stats->fps_frame_count = 0;
    }
}

inline int hina_perf_compare_float(const void* a, const void* b) {
    float fa = *(const float*)a;
    float fb = *(const float*)b;
    return (fa > fb) - (fa < fb);
}

inline float hina_perf_percentile(const float* sorted, uint32_t count, float percentile) {
    if (count == 0) return 0.0f;
    float index = (percentile / 100.0f) * (count - 1);
    uint32_t lower = (uint32_t)index;
    uint32_t upper = lower + 1;
    if (upper >= count) return sorted[count - 1];
    float frac = index - lower;
    return sorted[lower] * (1.0f - frac) + sorted[upper] * frac;
}

inline void hina_perf_stats_print(hina_perf_stats* stats) {
    if (stats->total_frames == 0) {
        printf("[PERF] No frames recorded\n");
        return;
    }

    float avg_frame_time = (float)(stats->total_frame_time / stats->total_frames);
    float avg_fps = 1.0f / avg_frame_time;
    float min_fps = 1.0f / stats->max_frame_time;
    float max_fps = 1.0f / stats->min_frame_time;

    float p1_frame_time = 0.0f, p01_frame_time = 0.0f;
    float fps_1_low = 0.0f, fps_01_low = 0.0f;

    if (stats->history_count > 0) {
        float* sorted = (float*)malloc(stats->history_count * sizeof(float));
        if (sorted) {
            memcpy(sorted, stats->frame_times, stats->history_count * sizeof(float));
            qsort(sorted, stats->history_count, sizeof(float), hina_perf_compare_float);
            p1_frame_time = hina_perf_percentile(sorted, stats->history_count, 99.0f);
            p01_frame_time = hina_perf_percentile(sorted, stats->history_count, 99.9f);
            fps_1_low = (p1_frame_time > 0.0f) ? (1.0f / p1_frame_time) : 0.0f;
            fps_01_low = (p01_frame_time > 0.0f) ? (1.0f / p01_frame_time) : 0.0f;
            free(sorted);
        }
    }

    printf("\n[PERF] ==================== Performance Summary ====================\n");
    printf("[PERF] Frames: %u | Duration: %.2f seconds\n", stats->total_frames, (float)stats->total_frame_time);
    printf("[PERF] FPS: avg=%.1f min=%.1f max=%.1f 1%%low=%.1f 0.1%%low=%.1f\n",
           avg_fps, min_fps, max_fps, fps_1_low, fps_01_low);
    printf("[PERF] Frame Time: avg=%.2fms min=%.2fms max=%.2fms\n",
           avg_frame_time * 1000.0f, stats->min_frame_time * 1000.0f, stats->max_frame_time * 1000.0f);
    printf("[PERF] ================================================================\n\n");
}

#endif // !__ANDROID__

// ============================================================================
// Print Helpers (Desktop only)
// ============================================================================

#ifndef __ANDROID__
inline void hina_example_print_controls() {
    printf("Controls:\n");
    printf("  Left mouse drag: Rotate view\n");
    printf("  Right mouse drag: Pan view\n");
    printf("  Mouse wheel: Zoom in/out\n");
    printf("  ESC: Exit\n");
}
#endif

// ============================================================================
// KTX Texture Loading (optional, requires HINA_KTX_ENABLED)
// ============================================================================

#ifdef HINA_KTX_ENABLED
#include <ktx.h>

#define HINA_GL_RED          0x1903
#define HINA_GL_RG           0x8227
#define HINA_GL_RGB          0x1907
#define HINA_GL_BGR          0x80E0
#define HINA_GL_RGBA         0x1908
#define HINA_GL_BGRA         0x80E1
#define HINA_GL_UNSIGNED_BYTE 0x1401
#define HINA_GL_SRGB8_ALPHA8 0x8C43

inline hina_format hina_ktx1_get_format(ktxTexture1* ktx1) {
    if (ktx1->glInternalformat == HINA_GL_SRGB8_ALPHA8) return HINA_FORMAT_R8G8B8A8_SRGB;
    if (ktx1->glType == HINA_GL_UNSIGNED_BYTE) {
        switch (ktx1->glFormat) {
            case HINA_GL_RED:  return HINA_FORMAT_R8_UNORM;
            case HINA_GL_RG:   return HINA_FORMAT_R8G8_UNORM;
            case HINA_GL_RGB:  return HINA_FORMAT_R8G8B8_UNORM;
            case HINA_GL_BGR:  return HINA_FORMAT_R8G8B8_UNORM;
            case HINA_GL_RGBA: return HINA_FORMAT_R8G8B8A8_UNORM;
            case HINA_GL_BGRA: return HINA_FORMAT_B8G8R8A8_UNORM;
        }
    }
    return HINA_FORMAT_R8G8B8A8_UNORM;
}

struct hina_ktx_texture {
    hina_texture texture;
    uint32_t width;
    uint32_t height;
    uint32_t mip_levels;
    hina_format format;
};

inline bool hina_ktx_load(hina_example_app* app, hina_ktx_texture* out, const char* filepath) {
    if (!out || !filepath) return false;
    memset(out, 0, sizeof(*out));

    ktxTexture* ktx_texture = nullptr;
    ktxResult result;

#ifdef __ANDROID__
    size_t file_size = 0;
    char* file_data = hina_example_load_file(app, filepath, &file_size);
    if (!file_data) return false;
    result = ktxTexture_CreateFromMemory(
        reinterpret_cast<ktx_uint8_t*>(file_data), file_size,
        KTX_TEXTURE_CREATE_LOAD_IMAGE_DATA_BIT, &ktx_texture);
    free(file_data);
#else
    (void)app;
    result = ktxTexture_CreateFromNamedFile(filepath, KTX_TEXTURE_CREATE_LOAD_IMAGE_DATA_BIT, &ktx_texture);
#endif

    if (result != KTX_SUCCESS) {
        EXAMPLE_LOGE("Failed to load KTX: %s (error: %d)", filepath, result);
        return false;
    }

    out->width = ktx_texture->baseWidth;
    out->height = ktx_texture->baseHeight;
    out->mip_levels = ktx_texture->numLevels;
    out->format = hina_ktx1_get_format(reinterpret_cast<ktxTexture1*>(ktx_texture));

    ktx_uint8_t* ktx_data = ktxTexture_GetData(ktx_texture);
    ktx_size_t ktx_size = ktxTexture_GetDataSize(ktx_texture);

    hina_texture_desc tex_desc = hina_texture_desc_default();
    tex_desc.format = out->format;
    tex_desc.width = out->width;
    tex_desc.height = out->height;
    tex_desc.mip_levels = static_cast<uint16_t>(out->mip_levels);
    tex_desc.usage = static_cast<hina_texture_usage_flags>(HINA_TEXTURE_SAMPLED_BIT | HINA_TEXTURE_TRANSFER_SRC_BIT);

    out->texture = hina_make_texture(&tex_desc);
    if (!hina_texture_is_valid(out->texture)) {
        ktxTexture_Destroy(ktx_texture);
        return false;
    }

    hina_buffer_desc staging_desc = {0};
    staging_desc.size = static_cast<size_t>(ktx_size);
    staging_desc.memory = HINA_BUFFER_CPU;
    staging_desc.usage = HINA_BUFFER_TRANSFER_SRC;
    staging_desc.initial_data = ktx_data;

    hina_buffer staging_buffer = hina_make_buffer(&staging_desc);
    if (!hina_buffer_is_valid(staging_buffer)) {
        hina_destroy_texture(out->texture);
        out->texture = {};
        ktxTexture_Destroy(ktx_texture);
        return false;
    }

    hina_cmd* cmd = hina_cmd_begin_ex(HINA_QUEUE_GRAPHICS);
    if (cmd) {
        for (uint32_t mip = 0; mip < out->mip_levels; mip++) {
            ktx_size_t offset;
            ktxTexture_GetImageOffset(ktx_texture, mip, 0, 0, &offset);
            hina_cmd_copy_buffer_to_texture(cmd, staging_buffer, out->texture, offset, mip, 0);
        }
        hina_cmd_transition_texture(cmd, out->texture, HINA_TEXSTATE_SHADER_READ);
        hina_ticket ticket = hina_submit_immediate(cmd);
        hina_wait_ticket(ticket);
    }

    hina_destroy_buffer(staging_buffer);
    ktxTexture_Destroy(ktx_texture);
    return true;
}

inline void hina_ktx_destroy(hina_ktx_texture* tex) {
    if (tex && hina_texture_is_valid(tex->texture)) {
        hina_destroy_texture(tex->texture);
        memset(tex, 0, sizeof(*tex));
    }
}

#endif // HINA_KTX_ENABLED

// ============================================================================
// ImGui Integration Implementation
// ============================================================================

#ifdef HINA_EXAMPLE_HAS_IMGUI

// Push constant structure (must match shader)
struct hina_imgui_push_constants {
    float scale[2];
    float translate[2];
    float rot_cos;      // cos(prerotation_angle) for Z-axis rotation in NDC
    float rot_sin;      // sin(prerotation_angle)
};

// Helper: Map SDL keycode to ImGui key
#ifndef __ANDROID__
inline ImGuiKey hina_imgui_sdl_keycode_to_imgui(int keycode) {
    switch (keycode) {
        case SDLK_TAB: return ImGuiKey_Tab;
        case SDLK_LEFT: return ImGuiKey_LeftArrow;
        case SDLK_RIGHT: return ImGuiKey_RightArrow;
        case SDLK_UP: return ImGuiKey_UpArrow;
        case SDLK_DOWN: return ImGuiKey_DownArrow;
        case SDLK_PAGEUP: return ImGuiKey_PageUp;
        case SDLK_PAGEDOWN: return ImGuiKey_PageDown;
        case SDLK_HOME: return ImGuiKey_Home;
        case SDLK_END: return ImGuiKey_End;
        case SDLK_INSERT: return ImGuiKey_Insert;
        case SDLK_DELETE: return ImGuiKey_Delete;
        case SDLK_BACKSPACE: return ImGuiKey_Backspace;
        case SDLK_SPACE: return ImGuiKey_Space;
        case SDLK_RETURN: return ImGuiKey_Enter;
        case SDLK_ESCAPE: return ImGuiKey_Escape;
        case SDLK_a: return ImGuiKey_A;
        case SDLK_c: return ImGuiKey_C;
        case SDLK_v: return ImGuiKey_V;
        case SDLK_x: return ImGuiKey_X;
        case SDLK_y: return ImGuiKey_Y;
        case SDLK_z: return ImGuiKey_Z;
        default: return ImGuiKey_None;
    }
}
#endif

// Helper: Get bind group for texture index
inline hina_bind_group hina_example_imgui_get_bind_group(hina_example_app* app, uint32_t texture_index) {
    if (!app->imgui_textures || texture_index >= app->imgui_textures->size()) return {};

    auto& entry = (*app->imgui_textures)[texture_index];

    // Create bind group on demand
    if (!hina_bind_group_is_valid(entry.bind_group)) {
        hina_bind_group_entry bg_entry = {};
        bg_entry.binding = 0;
        bg_entry.type = HINA_DESC_TYPE_COMBINED_IMAGE_SAMPLER;
        bg_entry.combined.view = entry.view;
        bg_entry.combined.sampler = entry.sampler;

        hina_bind_group_desc bg_desc = {};
        bg_desc.layout = app->imgui_bind_group_layout;
        bg_desc.entries = &bg_entry;
        bg_desc.entry_count = 1;
        bg_desc.label = "ImGui Texture";

        entry.bind_group = hina_create_bind_group(&bg_desc);
    }

    return entry.bind_group;
}

// ============================================================================
// CYBERDECK INTERFACE THEME
// A high-contrast dark theme inspired by sci-fi command line interfaces
// Monospace-first | Sharp edges | Keyboard-driven | Zero shadows
// ============================================================================

namespace hina_imgui_theme {
    // Accent: Electric Blue (#007FFF)
    inline constexpr ImVec4 AccentColor     = ImVec4(0.00f, 0.50f, 1.00f, 1.00f);
    inline constexpr ImVec4 AccentHovered   = ImVec4(0.20f, 0.60f, 1.00f, 1.00f);
    inline constexpr ImVec4 AccentActive    = ImVec4(0.00f, 0.40f, 0.80f, 1.00f);
    inline constexpr ImVec4 AccentDim       = ImVec4(0.00f, 0.25f, 0.50f, 1.00f);

    // Backgrounds: Pure black base for maximum contrast
    inline constexpr ImVec4 BgBlack     = ImVec4(0.02f, 0.02f, 0.03f, 1.00f);
    inline constexpr ImVec4 BgDark      = ImVec4(0.05f, 0.05f, 0.06f, 1.00f);
    inline constexpr ImVec4 BgMedium    = ImVec4(0.08f, 0.09f, 0.10f, 1.00f);
    inline constexpr ImVec4 BgLight     = ImVec4(0.12f, 0.13f, 0.14f, 1.00f);
    inline constexpr ImVec4 BgHighlight = ImVec4(0.00f, 0.15f, 0.25f, 1.00f);

    // Text: High contrast for readability
    inline constexpr ImVec4 TextPrimary   = ImVec4(0.90f, 0.92f, 0.95f, 1.00f);
    inline constexpr ImVec4 TextDisabled  = ImVec4(0.30f, 0.32f, 0.35f, 1.00f);

    // Borders: Sharp definition
    inline constexpr ImVec4 BorderColor   = ImVec4(0.20f, 0.22f, 0.25f, 1.00f);
    inline constexpr ImVec4 BorderBright  = ImVec4(0.30f, 0.33f, 0.38f, 1.00f);

    inline void Apply() {
        ImGuiStyle& style = ImGui::GetStyle();
        ImVec4* colors = style.Colors;

        // Scale factor for touch devices (Android needs larger touch targets)
#ifdef __ANDROID__
        constexpr float scale = 2.0f;  // 2x for high-DPI mobile screens
#else
        constexpr float scale = 1.0f;
#endif

        // === SPACING: Compact on desktop, touch-friendly on mobile ===
        style.WindowPadding      = ImVec2(6.0f * scale, 6.0f * scale);
        style.FramePadding       = ImVec2(6.0f * scale, 3.0f * scale);
        style.CellPadding        = ImVec2(4.0f * scale, 2.0f * scale);
        style.ItemSpacing        = ImVec2(6.0f * scale, 4.0f * scale);
        style.ItemInnerSpacing   = ImVec2(4.0f * scale, 4.0f * scale);
        style.IndentSpacing      = 14.0f * scale;
        style.ScrollbarSize      = 12.0f * scale;
        style.GrabMinSize        = 10.0f * scale;
        style.TouchExtraPadding  = ImVec2(0.0f, 0.0f);  // Will be scaled by ImGui if needed

        // === BORDERS: Sharp 1px borders everywhere ===
        style.WindowBorderSize   = 1.0f;
        style.ChildBorderSize    = 1.0f;
        style.PopupBorderSize    = 1.0f;
        style.FrameBorderSize    = 1.0f;
        style.TabBorderSize      = 1.0f;
        style.TabBarBorderSize   = 1.0f;
        style.TabBarOverlineSize = 2.0f;

        // === ROUNDING: Strict 0 for crisp terminal look ===
        style.WindowRounding     = 0.0f;
        style.ChildRounding      = 0.0f;
        style.FrameRounding      = 0.0f;
        style.PopupRounding      = 0.0f;
        style.ScrollbarRounding  = 0.0f;
        style.GrabRounding       = 0.0f;
        style.TabRounding        = 0.0f;

        // === ALIGNMENT ===
        style.WindowTitleAlign   = ImVec2(0.0f, 0.5f);
        style.WindowMenuButtonPosition = ImGuiDir_None;
        style.ButtonTextAlign    = ImVec2(0.5f, 0.5f);
        style.SelectableTextAlign = ImVec2(0.0f, 0.5f);

        // === RENDERING: Crisp pixels ===
        style.AntiAliasedLines   = false;
        style.AntiAliasedFill    = false;

        // === COLORS ===

        // Text
        colors[ImGuiCol_Text]                  = TextPrimary;
        colors[ImGuiCol_TextDisabled]          = TextDisabled;
        colors[ImGuiCol_TextSelectedBg]        = ImVec4(AccentColor.x, AccentColor.y, AccentColor.z, 0.50f);

        // Windows
        colors[ImGuiCol_WindowBg]              = BgBlack;
        colors[ImGuiCol_ChildBg]               = BgBlack;
        colors[ImGuiCol_PopupBg]               = BgDark;

        // Borders
        colors[ImGuiCol_Border]                = BorderColor;
        colors[ImGuiCol_BorderShadow]          = ImVec4(0.0f, 0.0f, 0.0f, 0.0f);

        // Frames (inputs, sliders)
        colors[ImGuiCol_FrameBg]               = BgDark;
        colors[ImGuiCol_FrameBgHovered]        = BgMedium;
        colors[ImGuiCol_FrameBgActive]         = BgHighlight;

        // Title bar
        colors[ImGuiCol_TitleBg]               = BgDark;
        colors[ImGuiCol_TitleBgActive]         = ImVec4(0.02f, 0.08f, 0.15f, 1.00f);
        colors[ImGuiCol_TitleBgCollapsed]      = BgBlack;

        // Menu bar
        colors[ImGuiCol_MenuBarBg]             = BgBlack;

        // Scrollbar
        colors[ImGuiCol_ScrollbarBg]           = BgBlack;
        colors[ImGuiCol_ScrollbarGrab]         = BorderColor;
        colors[ImGuiCol_ScrollbarGrabHovered]  = BorderBright;
        colors[ImGuiCol_ScrollbarGrabActive]   = AccentColor;

        // Checkmark & Slider
        colors[ImGuiCol_CheckMark]             = AccentColor;
        colors[ImGuiCol_SliderGrab]            = AccentDim;
        colors[ImGuiCol_SliderGrabActive]      = AccentColor;

        // Buttons
        colors[ImGuiCol_Button]                = BgMedium;
        colors[ImGuiCol_ButtonHovered]         = BgLight;
        colors[ImGuiCol_ButtonActive]          = BgHighlight;

        // Headers (collapsing, tree nodes)
        colors[ImGuiCol_Header]                = BgMedium;
        colors[ImGuiCol_HeaderHovered]         = BgLight;
        colors[ImGuiCol_HeaderActive]          = BgHighlight;

        // Separators
        colors[ImGuiCol_Separator]             = BorderColor;
        colors[ImGuiCol_SeparatorHovered]      = AccentColor;
        colors[ImGuiCol_SeparatorActive]       = AccentColor;

        // Resize grip
        colors[ImGuiCol_ResizeGrip]            = BorderColor;
        colors[ImGuiCol_ResizeGripHovered]     = AccentHovered;
        colors[ImGuiCol_ResizeGripActive]      = AccentColor;

        // Tabs
        colors[ImGuiCol_Tab]                   = BgDark;
        colors[ImGuiCol_TabHovered]            = BgMedium;
        colors[ImGuiCol_TabSelected]           = BgMedium;
        colors[ImGuiCol_TabSelectedOverline]   = AccentColor;
        colors[ImGuiCol_TabDimmed]             = BgBlack;
        colors[ImGuiCol_TabDimmedSelected]     = BgDark;

        // Docking (only if enabled)
#ifdef ImGuiCol_DockingPreview
        colors[ImGuiCol_DockingPreview]        = ImVec4(AccentColor.x, AccentColor.y, AccentColor.z, 0.40f);
        colors[ImGuiCol_DockingEmptyBg]        = BgBlack;
#endif

        // Plots
        colors[ImGuiCol_PlotLines]             = AccentColor;
        colors[ImGuiCol_PlotLinesHovered]      = AccentHovered;
        colors[ImGuiCol_PlotHistogram]         = AccentColor;
        colors[ImGuiCol_PlotHistogramHovered]  = AccentHovered;

        // Tables
        colors[ImGuiCol_TableHeaderBg]         = BgMedium;
        colors[ImGuiCol_TableBorderStrong]     = BorderColor;
        colors[ImGuiCol_TableBorderLight]      = ImVec4(BorderColor.x, BorderColor.y, BorderColor.z, 0.50f);
        colors[ImGuiCol_TableRowBg]            = BgBlack;
        colors[ImGuiCol_TableRowBgAlt]         = BgDark;

        // Drag/Drop
        colors[ImGuiCol_DragDropTarget]        = AccentColor;

        // Navigation
        colors[ImGuiCol_NavCursor]             = AccentColor;
        colors[ImGuiCol_NavWindowingHighlight] = AccentColor;
        colors[ImGuiCol_NavWindowingDimBg]     = ImVec4(0.0f, 0.0f, 0.0f, 0.75f);

        // Modal
        colors[ImGuiCol_ModalWindowDimBg]      = ImVec4(0.0f, 0.0f, 0.0f, 0.85f);
    }
}

// Initialize ImGui subsystem (called automatically by hina_example_init)
inline bool hina_example_imgui_init(hina_example_app* app) {
    if (app->imgui_initialized) return true;

    // Create ImGui context
    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImGuiIO& io = ImGui::GetIO();

    // Configure ImGui
    io.BackendRendererName = "imgui-hinavk";
    io.BackendFlags |= ImGuiBackendFlags_RendererHasVtxOffset;
    io.ConfigFlags |= ImGuiConfigFlags_NavEnableGamepad;

    // Apply Cyberdeck theme
    hina_imgui_theme::Apply();

    // Build font atlas (using default font for now - DPI scaling disabled for debugging)
    unsigned char* pixels;
    int width, height;
    io.Fonts->GetTexDataAsRGBA32(&pixels, &width, &height);

    // Create font texture
    hina_texture_desc tex_desc = hina_texture_desc_default();
    tex_desc.format = HINA_FORMAT_R8G8B8A8_UNORM;
    tex_desc.width = static_cast<uint32_t>(width);
    tex_desc.height = static_cast<uint32_t>(height);
    tex_desc.initial_data = pixels;
    tex_desc.usage = HINA_TEXTURE_SAMPLED_BIT;

    app->imgui_font_texture = hina_make_texture(&tex_desc);
    if (!hina_texture_is_valid(app->imgui_font_texture)) {
        EXAMPLE_LOGE("[ImGui] Failed to create font texture");
        return false;
    }

    // Create sampler
    hina_sampler_desc sampler_desc = hina_sampler_desc_default();
    sampler_desc.min_filter = HINA_FILTER_LINEAR;
    sampler_desc.mag_filter = HINA_FILTER_LINEAR;
    sampler_desc.address_u = HINA_ADDRESS_CLAMP_TO_EDGE;
    sampler_desc.address_v = HINA_ADDRESS_CLAMP_TO_EDGE;
    app->imgui_sampler = hina_make_sampler(&sampler_desc);
    if (!hina_sampler_is_valid(app->imgui_sampler)) {
        EXAMPLE_LOGE("[ImGui] Failed to create sampler");
        return false;
    }

    // Initialize texture registry
    app->imgui_textures = new std::vector<hina_example_app::imgui_texture_entry>();
    // Index 0: reserved as invalid (ImTextureID=0 is invalid in ImGui 1.92+)
    app->imgui_textures->push_back({{}, {}, {}, false});
    // Index 1: font texture
    hina_texture_view font_view = hina_texture_get_default_view(app->imgui_font_texture);
    app->imgui_textures->push_back({font_view, app->imgui_sampler, {}, false});
    io.Fonts->SetTexID(static_cast<ImTextureID>(1));

    // Create pipeline with embedded shader
    hina_vertex_layout vertex_layout = {};
    vertex_layout.buffer_count = 1;
    vertex_layout.buffer_strides[0] = sizeof(ImDrawVert);
    vertex_layout.input_rates[0] = HINA_VERTEX_INPUT_RATE_VERTEX;
    vertex_layout.attr_count = 3;
    vertex_layout.attrs[0] = { HINA_FORMAT_R32G32_SFLOAT, offsetof(ImDrawVert, pos), 0, 0 };
    vertex_layout.attrs[1] = { HINA_FORMAT_R32G32_SFLOAT, offsetof(ImDrawVert, uv), 1, 0 };
    vertex_layout.attrs[2] = { HINA_FORMAT_R8G8B8A8_UNORM, offsetof(ImDrawVert, col), 2, 0 };

    const char* shader_source = R"(
#hina
group ImGui = 0;

bindings(ImGui, start=0) {
  texture sampler2D u_texture;
}

push_constant PushConstants {
  vec2 scale;
  vec2 translate;
  float rot_cos;      // cos(prerotation_angle) for Z-axis rotation in NDC
  float rot_sin;      // sin(prerotation_angle)
} pc;

struct VertexIn {
  vec2 a_position;
  vec2 a_uv;
  vec4 a_color;
};

struct Varyings {
  vec2 uv;
  vec4 color;
};

struct FragOut {
  vec4 color;
};
#hina_end

#hina_stage vertex entry VSMain
Varyings VSMain(VertexIn in) {
    Varyings out;
    out.uv = in.a_uv;
    out.color = in.a_color;

    // First: standard pixel-to-NDC transform (preserves 1:1 pixel mapping)
    vec2 pos_ndc = in.a_position * pc.scale + pc.translate;

    // Then: rotate in NDC space for prerotation
    // This rotates the entire coordinate system, not individual pixel mappings
    // The display hardware will counter-rotate, making content appear correct
    vec2 rotated_ndc = vec2(
        pos_ndc.x * pc.rot_cos - pos_ndc.y * pc.rot_sin,
        pos_ndc.x * pc.rot_sin + pos_ndc.y * pc.rot_cos
    );

    gl_Position = vec4(rotated_ndc, 0.0, 1.0);
    return out;
}
#hina_end

#hina_stage fragment entry FSMain
FragOut FSMain(Varyings in) {
    FragOut out;
    out.color = in.color * texture(u_texture, in.uv);
    return out;
}
#hina_end
)";

    char* error = nullptr;
    hina_hsl_module* module = hslc_compile_hsl_source(shader_source, "imgui_shader", &error);
    if (!module) {
        EXAMPLE_LOGE("[ImGui] Shader compilation failed: %s", error ? error : "Unknown");
        if (error) hslc_free_log(error);
        return false;
    }

    hina_hsl_pipeline_desc pip_desc = hina_hsl_pipeline_desc_default();
    pip_desc.layout = vertex_layout;
    pip_desc.cull_mode = HINA_CULL_MODE_NONE;
    pip_desc.color_formats[0] = hina_get_surface_format();
    pip_desc.depth.depth_test = false;
    pip_desc.depth.depth_write = false;
    pip_desc.blend[0].enable = true;
    pip_desc.blend[0].src_color = HINA_BLEND_FACTOR_SRC_ALPHA;
    pip_desc.blend[0].dst_color = HINA_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA;
    pip_desc.blend[0].color_op = HINA_BLEND_OP_ADD;
    pip_desc.blend[0].src_alpha = HINA_BLEND_FACTOR_ONE;
    pip_desc.blend[0].dst_alpha = HINA_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA;
    pip_desc.blend[0].alpha_op = HINA_BLEND_OP_ADD;
    // Don't specify depth format - ImGui doesn't use depth testing
    // This avoids compatibility issues on Android where D32_SFLOAT may not be available
    pip_desc.depth_format = HINA_FORMAT_UNDEFINED;

    app->imgui_pipeline = hina_make_pipeline_from_module(module, &pip_desc, NULL);
    hslc_hsl_module_free(module);

    if (!hina_pipeline_is_valid(app->imgui_pipeline)) {
        EXAMPLE_LOGE("[ImGui] Pipeline creation failed");
        return false;
    }

    app->imgui_bind_group_layout = hina_pipeline_get_bind_group_layout(app->imgui_pipeline, 0);

    // Create per-frame buffers
    for (int i = 0; i < HINA_IMGUI_FRAMES_IN_FLIGHT; i++) {
        hina_buffer_desc vb_desc = {0};
        vb_desc.size = HINA_IMGUI_MAX_VERTEX_COUNT * sizeof(ImDrawVert);
        vb_desc.memory = HINA_BUFFER_CPU;
        vb_desc.usage = HINA_BUFFER_VERTEX;
        app->imgui_frames[i].vertex_buffer = hina_make_buffer(&vb_desc);
        app->imgui_frames[i].vertex_mapped = hina_mapped_buffer_ptr(app->imgui_frames[i].vertex_buffer);

        hina_buffer_desc ib_desc = {0};
        ib_desc.size = HINA_IMGUI_MAX_INDEX_COUNT * sizeof(ImDrawIdx);
        ib_desc.memory = HINA_BUFFER_CPU;
        ib_desc.usage = HINA_BUFFER_INDEX;
        app->imgui_frames[i].index_buffer = hina_make_buffer(&ib_desc);
        app->imgui_frames[i].index_mapped = hina_mapped_buffer_ptr(app->imgui_frames[i].index_buffer);

        if (!app->imgui_frames[i].vertex_mapped || !app->imgui_frames[i].index_mapped) {
            EXAMPLE_LOGE("[ImGui] Failed to create/map frame buffers");
            return false;
        }
    }

    app->imgui_initialized = true;
    app->imgui_visible = true;
    app->imgui_show_settings = false;
    EXAMPLE_LOGI("[ImGui] Initialized successfully");
    return true;
}

// Shutdown ImGui subsystem
inline void hina_example_imgui_shutdown(hina_example_app* app) {
    if (!app->imgui_initialized) return;

    // Don't destroy bind groups - let descriptor pool cleanup handle them
    if (app->imgui_textures) {
        delete app->imgui_textures;
        app->imgui_textures = nullptr;
    }

    for (int i = 0; i < HINA_IMGUI_FRAMES_IN_FLIGHT; i++) {
        if (hina_buffer_is_valid(app->imgui_frames[i].vertex_buffer))
            hina_destroy_buffer(app->imgui_frames[i].vertex_buffer);
        if (hina_buffer_is_valid(app->imgui_frames[i].index_buffer))
            hina_destroy_buffer(app->imgui_frames[i].index_buffer);
    }

    if (hina_pipeline_is_valid(app->imgui_pipeline))
        hina_destroy_pipeline(app->imgui_pipeline);
    if (hina_sampler_is_valid(app->imgui_sampler))
        hina_destroy_sampler(app->imgui_sampler);
    if (hina_texture_is_valid(app->imgui_font_texture))
        hina_destroy_texture(app->imgui_font_texture);

    ImGui::DestroyContext();
    app->imgui_initialized = false;
}

// Process SDL event for ImGui (called internally by poll)
#ifndef __ANDROID__
inline bool hina_example_imgui_process_event(hina_example_app* app, const SDL_Event* event) {
    if (!app->imgui_initialized) return false;

    ImGuiIO& io = ImGui::GetIO();

    switch (event->type) {
        case SDL_MOUSEWHEEL:
            io.AddMouseWheelEvent(static_cast<float>(event->wheel.x), static_cast<float>(event->wheel.y));
            return io.WantCaptureMouse;

        case SDL_MOUSEBUTTONDOWN:
        case SDL_MOUSEBUTTONUP: {
            int mouse_button = -1;
            if (event->button.button == SDL_BUTTON_LEFT) mouse_button = 0;
            else if (event->button.button == SDL_BUTTON_RIGHT) mouse_button = 1;
            else if (event->button.button == SDL_BUTTON_MIDDLE) mouse_button = 2;
            if (mouse_button != -1) {
                io.AddMouseButtonEvent(mouse_button, event->type == SDL_MOUSEBUTTONDOWN);
            }
            return io.WantCaptureMouse;
        }

        case SDL_MOUSEMOTION:
            io.AddMousePosEvent(static_cast<float>(event->motion.x), static_cast<float>(event->motion.y));
            return false;

        case SDL_TEXTINPUT:
            io.AddInputCharactersUTF8(event->text.text);
            return io.WantTextInput;

        case SDL_KEYDOWN:
        case SDL_KEYUP: {
            ImGuiKey key = hina_imgui_sdl_keycode_to_imgui(event->key.keysym.sym);
            if (key != ImGuiKey_None) {
                io.AddKeyEvent(key, event->type == SDL_KEYDOWN);
            }
            io.AddKeyEvent(ImGuiMod_Ctrl, (SDL_GetModState() & KMOD_CTRL) != 0);
            io.AddKeyEvent(ImGuiMod_Shift, (SDL_GetModState() & KMOD_SHIFT) != 0);
            io.AddKeyEvent(ImGuiMod_Alt, (SDL_GetModState() & KMOD_ALT) != 0);
            io.AddKeyEvent(ImGuiMod_Super, (SDL_GetModState() & KMOD_GUI) != 0);
            return io.WantCaptureKeyboard;
        }
    }
    return false;
}
#else // __ANDROID__

// Process touch input for ImGui (Android)
inline bool hina_example_imgui_process_touch(hina_example_app* app, int action, float x, float y) {
    if (!app->imgui_initialized) return false;

    ImGuiIO& io = ImGui::GetIO();

    // Map Android touch actions to ImGui mouse events
    // action values: 0=DOWN, 1=UP, 2=MOVE (AMOTION_EVENT_ACTION_*)
    switch (action) {
        case 0: // AMOTION_EVENT_ACTION_DOWN
            io.AddMousePosEvent(x, y);
            io.AddMouseButtonEvent(0, true);  // Left mouse button down
            break;

        case 1: // AMOTION_EVENT_ACTION_UP
        case 3: // AMOTION_EVENT_ACTION_CANCEL
            io.AddMouseButtonEvent(0, false);  // Left mouse button up
            break;

        case 2: // AMOTION_EVENT_ACTION_MOVE
            io.AddMousePosEvent(x, y);
            break;
    }

    return io.WantCaptureMouse;
}
#endif

/**
 * Begin ImGui frame for custom widget rendering.
 * Call this ONLY if you need to render custom ImGui widgets in your example.
 * For examples without custom UI, present_frame() handles everything automatically.
 *
 * Returns true if ImGui is ready for widget calls.
 */
inline bool hina_example_begin_ui(hina_example_app* app) {
    if (!app->imgui_initialized) return false;
    if (app->imgui_frame_active) return true;  // Already active

    // Begin ImGui frame
    ImGuiIO& io = ImGui::GetIO();

#ifdef __ANDROID__
    // With prerotation, framebuffer is now portrait but user sees landscape
    // Tell ImGui the LOGICAL dimensions (what user sees after display rotation)
    float prerotation = hina_get_swapchain_prerotation();
    if (prerotation == 90.0f || prerotation == 270.0f) {
        // Framebuffer is portrait (W×H where W<H), user sees landscape (H×W)
        // ImGui should generate UI for landscape (swap dimensions)
        io.DisplaySize = ImVec2(static_cast<float>(app->height), static_cast<float>(app->width));
    } else {
        io.DisplaySize = ImVec2(static_cast<float>(app->width), static_cast<float>(app->height));
    }
#else
    io.DisplaySize = ImVec2(static_cast<float>(app->width), static_cast<float>(app->height));
#endif

    io.DeltaTime = app->delta_time > 0.0f ? app->delta_time : (1.0f / 60.0f);
    ImGui::NewFrame();
    app->imgui_frame_active = true;

    return true;
}

/**
 * Check if settings panel is open (for examples to add custom controls).
 */
inline bool hina_example_ui_settings_open(hina_example_app* app) {
    return app->imgui_initialized && app->imgui_visible && app->imgui_show_settings;
}

/**
 * Check if ImGui wants to capture mouse input.
 * Uses cached value from previous frame (safe to call anytime).
 */
inline bool hina_example_ui_want_mouse(hina_example_app* app) {
    return app->imgui_initialized && app->imgui_want_mouse;
}

/**
 * Check if ImGui wants to capture keyboard input.
 * Uses cached value from previous frame (safe to call anytime).
 */
inline bool hina_example_ui_want_keyboard(hina_example_app* app) {
    return app->imgui_initialized && app->imgui_want_keyboard;
}

/**
 * Toggle UI visibility (can be bound to a key).
 */
inline void hina_example_ui_toggle(hina_example_app* app) {
    app->imgui_visible = !app->imgui_visible;
}

/**
 * Render standard stats content for ImGui overlay.
 * Call this within your own ImGui window to get unified telemetry display.
 *
 * @param app           The example app context
 * @param include_headers  If true, include collapsible headers for Frame Stats, Device Caps, etc.
 *                         If false, just show minimal FPS + GPU time inline.
 *
 * Example usage in a custom overlay:
 *   if (ImGui::Begin("##my_overlay", ...)) {
 *       hina_example_imgui_stats_content(app, true);  // Full stats with headers
 *       ImGui::Separator();
 *       ImGui::Text("My custom stat: %d", value);     // Add your own stats
 *   }
 *   ImGui::End();
 */
inline void hina_example_imgui_stats_content(hina_example_app* app, bool include_headers) {
    const hina_device_caps* caps = hina_get_device_caps();
    const hina_debug_caps* dbg = hina_get_debug_caps();
    hina_frame_stats stats = hina_get_frame_stats();

    // Device name (always show)
    if (caps && caps->device_name[0]) {
        ImGui::TextColored(ImVec4(0.4f, 0.8f, 1.0f, 1.0f), "%s", caps->device_name);
    }

    // FPS and frame time (always show)
    float fps = app->delta_time > 0.0f ? (1.0f / app->delta_time) : 0.0f;
    float ms = app->delta_time * 1000.0f;
    ImGui::Text("%.1f fps (%.2f ms)", fps, ms);

    // GPU time if available (always show when present)
    if (stats.gpu_time_ms > 0.0) {
        ImGui::Text("GPU: %.2f ms", stats.gpu_time_ms);
    }

    if (!include_headers) return;

    // Collapsible sections for detailed stats
    if (ImGui::CollapsingHeader("Frame Stats")) {
        ImGui::Text("CPU frame: %.2f ms", stats.frame_time_ms);
        if (stats.gpu_time_ms > 0.0) {
            ImGui::Text("GPU time:  %.2f ms", stats.gpu_time_ms);
        } else {
            ImGui::TextColored(ImVec4(0.6f, 0.6f, 0.6f, 1.0f), "GPU time:  N/A");
        }
        ImGui::Separator();
        ImGui::Text("Draw calls:    %u", stats.draw_calls);
        ImGui::Text("Dispatches:    %u", stats.dispatch_calls);
        ImGui::Text("Pipeline binds:%u", stats.pipeline_binds);
        ImGui::Text("Desc writes:   %u", stats.descriptor_writes);
        ImGui::Text("Vertices:      %llu", (unsigned long long)stats.vertices_submitted);
        ImGui::Separator();
        if (stats.gpu_memory_budget > 0) {
            float used_mb = stats.gpu_memory_used / (1024.0f * 1024.0f);
            float budget_mb = stats.gpu_memory_budget / (1024.0f * 1024.0f);
            float pct = (stats.gpu_memory_used * 100.0f) / stats.gpu_memory_budget;
            ImGui::Text("VRAM: %.0f / %.0f MB (%.0f%%)", used_mb, budget_mb, pct);
        } else {
            float used_mb = stats.gpu_memory_used / (1024.0f * 1024.0f);
            ImGui::Text("VRAM: %.0f MB", used_mb);
        }
    }

    if (ImGui::CollapsingHeader("Device Caps")) {
        ImVec4 yes_color = ImVec4(0.4f, 1.0f, 0.4f, 1.0f);
        ImVec4 no_color = ImVec4(0.6f, 0.6f, 0.6f, 1.0f);

        // Vulkan version
        uint32_t ver = caps->api_version;
        uint32_t major = (ver >> 22) & 0x7FU;
        uint32_t minor = (ver >> 12) & 0x3FFU;
        uint32_t patch = ver & 0xFFFU;
        ImGui::Text("Vulkan %u.%u.%u", major, minor, patch);
        ImGui::Separator();

        #define SHOW_CAP(name, field) \
            ImGui::Text(name ":"); ImGui::SameLine(); \
            ImGui::TextColored(caps->field ? yes_color : no_color, caps->field ? "YES" : "no")

        SHOW_CAP("dynamicRendering", has_dynamic_rendering);
        SHOW_CAP("localRead", has_dynamic_rendering_local_read);
        SHOW_CAP("timelineSemaphore", has_timeline_semaphore);
        SHOW_CAP("geometryShader", has_geometry_shader);
        SHOW_CAP("tessellation", has_tessellation_shader);
        SHOW_CAP("anisotropy", has_sampler_anisotropy);
        #undef SHOW_CAP

        ImGui::Separator();
        ImGui::Text("Subgroup: %u (min %u, max %u)",
                    caps->subgroup_size, caps->min_subgroup_size, caps->max_subgroup_size);
        ImGui::Text("UBO align: %u", caps->min_uniform_buffer_alignment);
        ImGui::Text("SSBO align: %u", caps->min_storage_buffer_alignment);
    }

    if (ImGui::CollapsingHeader("Internal Paths")) {
        ImVec4 yes_color = ImVec4(0.4f, 1.0f, 0.4f, 1.0f);
        ImVec4 no_color = ImVec4(0.6f, 0.6f, 0.6f, 1.0f);

        ImGui::Text("Active Paths:");
        #define SHOW_USE(name, field) \
            ImGui::Text("  " name ":"); ImGui::SameLine(); \
            ImGui::TextColored(dbg->field ? yes_color : no_color, dbg->field ? "YES" : "no")

        SHOW_USE("sync2", uses_synchronization2);
        SHOW_USE("dynRender", uses_dynamic_rendering);
        SHOW_USE("timeline", uses_timeline_semaphore);
        SHOW_USE("asyncCompute", has_dedicated_compute);
        #undef SHOW_USE

        ImGui::Separator();
        ImGui::Text("Extensions:");
        #define SHOW_EXT(name, field) \
            ImGui::Text("  " name ":"); ImGui::SameLine(); \
            ImGui::TextColored(dbg->field ? yes_color : no_color, dbg->field ? "YES" : "no")

        SHOW_EXT("deviceFault", has_device_fault);
        SHOW_EXT("memBudget", has_memory_budget);
        SHOW_EXT("maint4", has_maintenance4);
        SHOW_EXT("maint5", has_maintenance5);
        SHOW_EXT("subgroupCtrl", has_subgroup_size_control);
        SHOW_EXT("pipelineFeedback", has_pipeline_creation_feedback);
        #undef SHOW_EXT
    }
}

/**
 * Draw ImGui draw data to command buffer.
 * This is an internal helper called by hina_example_present_frame.
 * The draw data must already have been prepared via ImGui::Render().
 */
inline void hina_example_draw_imgui_data(hina_example_app* app, hina_cmd* cmd, ImDrawData* draw_data, uint32_t fb_width, uint32_t fb_height) {
    if (!app->imgui_initialized || !draw_data) return;

    uint32_t frame_slot = static_cast<uint32_t>(hina_get_frame_index() % HINA_IMGUI_FRAMES_IN_FLIGHT);
    auto& frame = app->imgui_frames[frame_slot];

    if (draw_data->TotalVtxCount > HINA_IMGUI_MAX_VERTEX_COUNT ||
        draw_data->TotalIdxCount > HINA_IMGUI_MAX_INDEX_COUNT) {
        EXAMPLE_LOGE("[ImGui] Draw data exceeds buffer limits");
        return;
    }

#ifdef __ANDROID__
    float prerotation = hina_get_swapchain_prerotation();
    float display_w = draw_data->DisplaySize.x;  // Logical landscape width (e.g., 2264)
    float display_h = draw_data->DisplaySize.y;  // Logical landscape height (e.g., 1080)
#endif

    // Upload vertex/index data
    ImDrawVert* vtx_dst = static_cast<ImDrawVert*>(frame.vertex_mapped);
    ImDrawIdx* idx_dst = static_cast<ImDrawIdx*>(frame.index_mapped);

    for (int n = 0; n < draw_data->CmdListsCount; n++) {
        const ImDrawList* cmd_list = draw_data->CmdLists[n];

#ifdef __ANDROID__
        // With portrait framebuffer (prerotation), transform landscape coords to portrait
        if (prerotation == 90.0f) {
            // 90° CW display rotation: fb(x,y) → display(y, fb_w-x)
            // To get landscape(lx,ly) at display(lx,ly): fb_x = fb_w - ly, fb_y = lx
            // fb_width = display_h (portrait width = landscape height)
            for (int i = 0; i < cmd_list->VtxBuffer.Size; i++) {
                const ImDrawVert& src = cmd_list->VtxBuffer.Data[i];
                vtx_dst[i].pos.x = display_h - src.pos.y;  // fb_x = fb_width - landscape_y
                vtx_dst[i].pos.y = src.pos.x;              // fb_y = landscape_x
                vtx_dst[i].uv = src.uv;
                vtx_dst[i].col = src.col;
            }
        } else if (prerotation == 270.0f) {
            // 270° CW display rotation: fb(x,y) → display(fb_h-y, x)
            // To get landscape(lx,ly) at display(lx,ly): fb_x = ly, fb_y = fb_h - lx
            // fb_height = display_w (portrait height = landscape width)
            for (int i = 0; i < cmd_list->VtxBuffer.Size; i++) {
                const ImDrawVert& src = cmd_list->VtxBuffer.Data[i];
                vtx_dst[i].pos.x = src.pos.y;              // fb_x = landscape_y
                vtx_dst[i].pos.y = display_w - src.pos.x;  // fb_y = fb_height - landscape_x
                vtx_dst[i].uv = src.uv;
                vtx_dst[i].col = src.col;
            }
        } else if (prerotation == 180.0f) {
            for (int i = 0; i < cmd_list->VtxBuffer.Size; i++) {
                const ImDrawVert& src = cmd_list->VtxBuffer.Data[i];
                vtx_dst[i].pos.x = display_w - src.pos.x;
                vtx_dst[i].pos.y = display_h - src.pos.y;
                vtx_dst[i].uv = src.uv;
                vtx_dst[i].col = src.col;
            }
        } else {
            memcpy(vtx_dst, cmd_list->VtxBuffer.Data, cmd_list->VtxBuffer.Size * sizeof(ImDrawVert));
        }
#else
        memcpy(vtx_dst, cmd_list->VtxBuffer.Data, cmd_list->VtxBuffer.Size * sizeof(ImDrawVert));
#endif

        memcpy(idx_dst, cmd_list->IdxBuffer.Data, cmd_list->IdxBuffer.Size * sizeof(ImDrawIdx));
        vtx_dst += cmd_list->VtxBuffer.Size;
        idx_dst += cmd_list->IdxBuffer.Size;
    }

    // Push constants: use FRAMEBUFFER dimensions for scale (not DisplaySize)
    // After coord transform, portrait coords [0,H]×[0,W] map to portrait fb [0,fb_w]×[0,fb_h]
    // This gives 1:1 pixel mapping!
    hina_imgui_push_constants pc;
    pc.scale[0] = 2.0f / static_cast<float>(fb_width);
    pc.scale[1] = 2.0f / static_cast<float>(fb_height);
    pc.translate[0] = -1.0f;
    pc.translate[1] = -1.0f;
    pc.rot_cos = 1.0f;
    pc.rot_sin = 0.0f;

    hina_cmd_bind_pipeline(cmd, app->imgui_pipeline);
    hina_cmd_push_constants(cmd, 0, sizeof(pc), &pc);

    hina_vertex_input bindings = {};
    bindings.vertex_buffers[0] = frame.vertex_buffer;
    bindings.vertex_offsets[0] = 0;
    bindings.index_buffer = frame.index_buffer;
    bindings.index_type = sizeof(ImDrawIdx) == 2 ? HINA_INDEX_UINT16 : HINA_INDEX_UINT32;
    hina_cmd_apply_vertex_input(cmd, &bindings);

    int global_vtx_offset = 0;
    int global_idx_offset = 0;
    ImVec2 clip_off = draw_data->DisplayPos;
    ImTextureID last_texture_id = ImTextureID_Invalid;

    for (int n = 0; n < draw_data->CmdListsCount; n++) {
        const ImDrawList* cmd_list = draw_data->CmdLists[n];

        for (int cmd_i = 0; cmd_i < cmd_list->CmdBuffer.Size; cmd_i++) {
            const ImDrawCmd* pcmd = &cmd_list->CmdBuffer[cmd_i];

            if (pcmd->UserCallback) {
                pcmd->UserCallback(cmd_list, pcmd);
            } else {
                ImVec2 clip_min(pcmd->ClipRect.x - clip_off.x, pcmd->ClipRect.y - clip_off.y);
                ImVec2 clip_max(pcmd->ClipRect.z - clip_off.x, pcmd->ClipRect.w - clip_off.y);
                if (clip_max.x <= clip_min.x || clip_max.y <= clip_min.y) continue;

                // Calculate scissor rectangle
                hina_scissor scissor;
#ifdef __ANDROID__
                // Transform scissor rectangle for prerotation
                // Uses same transform as vertices: landscape→portrait
                if (prerotation == 90.0f) {
                    // 90° CW: fb_x = display_h - landscape_y, fb_y = landscape_x
                    scissor.x = static_cast<int32_t>(display_h - clip_max.y);
                    scissor.y = static_cast<int32_t>(clip_min.x > 0 ? clip_min.x : 0);
                    scissor.width = static_cast<uint32_t>(clip_max.y - clip_min.y);
                    scissor.height = static_cast<uint32_t>(clip_max.x - clip_min.x);
                } else if (prerotation == 270.0f) {
                    // 270° CW: fb_x = landscape_y, fb_y = display_w - landscape_x
                    scissor.x = static_cast<int32_t>(clip_min.y > 0 ? clip_min.y : 0);
                    scissor.y = static_cast<int32_t>(display_w - clip_max.x);
                    scissor.width = static_cast<uint32_t>(clip_max.y - clip_min.y);
                    scissor.height = static_cast<uint32_t>(clip_max.x - clip_min.x);
                } else if (prerotation == 180.0f) {
                    scissor.x = static_cast<int32_t>(display_w - clip_max.x);
                    scissor.y = static_cast<int32_t>(display_h - clip_max.y);
                    scissor.width = static_cast<uint32_t>(clip_max.x - clip_min.x);
                    scissor.height = static_cast<uint32_t>(clip_max.y - clip_min.y);
                } else {
                    scissor.x = static_cast<int32_t>(clip_min.x > 0 ? clip_min.x : 0);
                    scissor.y = static_cast<int32_t>(clip_min.y > 0 ? clip_min.y : 0);
                    scissor.width = static_cast<uint32_t>(clip_max.x - clip_min.x);
                    scissor.height = static_cast<uint32_t>(clip_max.y - clip_min.y);
                }
#else
                scissor.x = static_cast<int32_t>(clip_min.x > 0 ? clip_min.x : 0);
                scissor.y = static_cast<int32_t>(clip_min.y > 0 ? clip_min.y : 0);
                scissor.width = static_cast<uint32_t>(clip_max.x - clip_min.x);
                scissor.height = static_cast<uint32_t>(clip_max.y - clip_min.y);
#endif
                hina_cmd_set_scissor(cmd, &scissor);

                ImTextureID tex_id = pcmd->GetTexID();
                if (tex_id != last_texture_id) {
                    uint32_t tex_index = static_cast<uint32_t>(tex_id);
                    hina_bind_group bg = hina_example_imgui_get_bind_group(app, tex_index);
                    if (hina_bind_group_is_valid(bg)) {
                        hina_cmd_bind_group(cmd, 0, bg);
                    }
                    last_texture_id = tex_id;
                }

                hina_cmd_draw_indexed(cmd, pcmd->ElemCount, 1,
                    pcmd->IdxOffset + global_idx_offset,
                    pcmd->VtxOffset + global_vtx_offset, 0);
            }
        }
        global_vtx_offset += cmd_list->VtxBuffer.Size;
        global_idx_offset += cmd_list->IdxBuffer.Size;
    }
}

/**
 * Legacy overload - derives framebuffer dimensions from draw_data.
 */
inline void hina_example_draw_imgui_data(hina_example_app* app, hina_cmd* cmd, ImDrawData* draw_data) {
    hina_example_draw_imgui_data(app, cmd, draw_data,
        static_cast<uint32_t>(draw_data->DisplaySize.x),
        static_cast<uint32_t>(draw_data->DisplaySize.y));
}

/**
 * Get ImTextureID for a hina_texture (for use with ImGui::Image).
 */
inline ImTextureID hina_example_imgui_texture_id(hina_example_app* app, hina_texture tex) {
    if (!app->imgui_initialized || !app->imgui_textures) return static_cast<ImTextureID>(0);

    hina_texture_view view = hina_texture_get_default_view(tex);

    // Check if already registered
    for (size_t i = 0; i < app->imgui_textures->size(); i++) {
        auto& entry = (*app->imgui_textures)[i];
        if (entry.view.id == view.id && entry.sampler.id == app->imgui_sampler.id) {
            return static_cast<ImTextureID>(i);
        }
    }

    // Register new texture
    uint32_t index = static_cast<uint32_t>(app->imgui_textures->size());
    app->imgui_textures->push_back({view, app->imgui_sampler, {}, true});
    return static_cast<ImTextureID>(index);
}

#else // !HINA_EXAMPLE_HAS_IMGUI

// Stub functions when ImGui is not available
inline bool hina_example_imgui_init(hina_example_app*) { return true; }
inline void hina_example_imgui_shutdown(hina_example_app*) {}
inline bool hina_example_begin_ui(hina_example_app*) { return false; }
inline bool hina_example_ui_settings_open(hina_example_app*) { return false; }
inline bool hina_example_ui_want_mouse(hina_example_app*) { return false; }
inline bool hina_example_ui_want_keyboard(hina_example_app*) { return false; }
inline void hina_example_ui_toggle(hina_example_app*) {}

#endif // HINA_EXAMPLE_HAS_IMGUI

// ============================================================================
// Unified Entry Point
// ============================================================================
//
// Usage: Define three callback functions and invoke the run function:
//
//   bool my_init(hina_example_app* app) { return true; }
//   void my_render(hina_example_app* app) { }
//   void my_cleanup(hina_example_app* app) { }
//
//   // At file scope:
//   HINA_EXAMPLE_MAIN("My Example", my_init, my_render, my_cleanup)

typedef bool (*hina_example_init_fn)(hina_example_app*);
typedef void (*hina_example_render_fn)(hina_example_app*);
typedef void (*hina_example_cleanup_fn)(hina_example_app*);

#ifdef __ANDROID__

inline void hina_example_run_android(
    struct android_app* state,
    const char* title,
    hina_example_init_fn init_fn,
    hina_example_render_fn render_fn,
    hina_example_cleanup_fn cleanup_fn)
{
    EXAMPLE_LOGI("android_main: Starting %s", title);

    hina_example_app app;
    hina_example_init_android(&app, state, title);

    bool resources_created = false;

    while (hina_example_poll(&app)) {
        if (app.surface_ready && !app.hina_initialized) {
            if (!hina_example_init_hina(&app)) {
                EXAMPLE_LOGE("Failed to initialize HinaVK");
                continue;
            }
            if (!init_fn(&app)) {
                EXAMPLE_LOGE("Failed to initialize resources");
                hina_example_shutdown(&app);
                return;
            }
            resources_created = true;
            // Reset timing so elapsed_time starts from 0 after init completes
            uint64_t now = hina_example_get_time_ns();
            app.start_time_ns = now;
            app.last_time_ns = now;
            EXAMPLE_LOGI("Resources initialized");
        }

        if (hina_example_should_render(&app) && resources_created) {
            render_fn(&app);
        }
    }

    if (resources_created) {
        cleanup_fn(&app);
    }
    hina_example_shutdown(&app);
    EXAMPLE_LOGI("android_main: Exit");
}

#define HINA_EXAMPLE_MAIN(title, init_fn, render_fn, cleanup_fn) \
    void android_main(struct android_app* state) { \
        hina_example_run_android(state, title, init_fn, render_fn, cleanup_fn); \
    }

#else // Desktop

inline int hina_example_run_desktop(
    int argc, char** argv,
    const char* title,
    hina_example_init_fn init_fn,
    hina_example_render_fn render_fn,
    hina_example_cleanup_fn cleanup_fn)
{
    hina_example_config cfg = hina_example_config_default();
    cfg.title = title;
    cfg.exe_path = argv[0];

    if (!hina_example_parse_args(&cfg, argc, argv)) {
        return 0;
    }

    hina_example_app app;
    if (!hina_example_init(&app, &cfg)) {
        fprintf(stderr, "[ERROR] Failed to initialize\n");
        return 1;
    }

    if (!init_fn(&app)) {
        fprintf(stderr, "[ERROR] Failed to initialize resources\n");
        hina_example_shutdown(&app);
        return 1;
    }

    printf("[INFO] Entering render loop...\n");

    // Performance tracking for console output
    double cpu_time_accum = 0.0;
    double gpu_time_accum = 0.0;
    uint32_t frame_count = 0;
    Uint64 perf_freq = SDL_GetPerformanceFrequency();
    Uint64 last_report_time = SDL_GetPerformanceCounter();

    while (hina_example_poll(&app)) {
        if (hina_example_should_render(&app)) {
            render_fn(&app);

            // Accumulate frame stats
            hina_frame_stats stats = hina_get_frame_stats();
            cpu_time_accum += stats.frame_time_ms;
            gpu_time_accum += stats.gpu_time_ms;
            frame_count++;

            // Print every second
            Uint64 now = SDL_GetPerformanceCounter();
            double elapsed_sec = (double)(now - last_report_time) / (double)perf_freq;
            if (elapsed_sec >= 1.0 && frame_count > 0) {
                double avg_cpu = cpu_time_accum / frame_count;
                double avg_gpu = gpu_time_accum / frame_count;
                printf("[PERF] CPU: %.3f ms | GPU: %.3f ms | FPS: %.1f\n",
                       avg_cpu, avg_gpu, 1000.0 / avg_cpu);
                cpu_time_accum = 0.0;
                gpu_time_accum = 0.0;
                frame_count = 0;
                last_report_time = now;
            }
        }
    }

    // Print final stats
    if (frame_count > 0) {
        double avg_cpu = cpu_time_accum / frame_count;
        double avg_gpu = gpu_time_accum / frame_count;
        printf("[PERF] Final - CPU: %.3f ms | GPU: %.3f ms\n", avg_cpu, avg_gpu);
    }

    cleanup_fn(&app);
    hina_example_shutdown(&app);
    printf("[INFO] Done.\n");
    return 0;
}

#define HINA_EXAMPLE_MAIN(title, init_fn, render_fn, cleanup_fn) \
    int main(int argc, char** argv) { \
        return hina_example_run_desktop(argc, argv, title, init_fn, render_fn, cleanup_fn); \
    }

#endif // __ANDROID__

#endif // HINA_EXAMPLE_H
