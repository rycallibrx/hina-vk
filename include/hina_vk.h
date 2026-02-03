#ifndef HINA_VK_H
#define HINA_VK_H
/* https://www.pixiv.net/en/artworks/96356970
                                                           :==:=+==                                     ::
                                                        --     =----+-                             .=#*+
                               .                      :-       : -=-=+-:::=+=:                   :****=
                         =*+=+*#**#*=:                +:---:. .:  :-          -==.             :#****:
                                 -*****#=              ==-----.+=:                =++-:      .*******
                                    =*****%:       .=+-.   ::---                     :=-=*- -#*****#.
                                     :#*****%: -*=.          ::       .----=-.          := =*******#
                                      *********              .       .--:--------.            =*#**+
                                      .#*******            .          :           .-:           =*#=
                                       %****-             .             ::           :            =+
                             -#%#:   ==***+                               .:                        =:
                           .##***%=-=  **.                                  .                        .=
                          :%#*****#-   +                                                               =:
                         -%********* .*                                                                 .=
                         ******#******:                         ::                                        =
                          *%*********=               :         :  .                          ::            -:
                          *::********               :         -     :                          =            :-
                         :*    -##%#:              -        ::        -:                        +            .-
                         =:     *%%*              :::      :.           :=                       =            :=
                         *     .%%%=              =:::.   :-               =.                    .+            ::
                        =-     *%%%              +  -:::..:                  :=                    :            =:
                        *:    .%%%+             ::    -:::                      =:-                -             =
                        *.    =%%%-   :         =:::    =          .:             .=-++:            -            .=
                       .*     %%%%:  :.         =:-:::. .        ==                  ::  .::--:     :             +:
                       :*    :%%%%   =          =  .::::.    :==.                       ::           .          - :=
                       :=    ***#*  :-        .::::   :-:==:.                              ::        -          -. +
                       :.    #****  =.       :::: :::.=                              :-=+*******+=:  -          -- -:
                       -    :%#%%=  =        :: -  :. :      ..:::--:              .:*****#####**=:. :          -+ :-
                       -    =%+::=  =        :: = .:.+=**######*#**=-.                    :::---::  .+          -+- =
                       :    .   ::  =         :--::::+-*##****+:                           .:::::   .*          =-+.-
                       =        =:  -        :::- ::-+***+-::::                             ::::::  .+.         =:-=
                      +:        *: .-        :. :-.:- :. .::::                               :::::. .*.         + ::
                     = :       *+= .-        :: .=::=    :::::             -===+=             ::::-  *.        :- +
                    -:        :*-= .=         ::::: =   .::::.            =======+.           :::::  +         + .:
                    :         *--+ .=            :+ =   :::::.           ==========.           :::: :=        =:
                   -         ==--=  +             -+=   :::::.           +=========-           :::.:*:       =:
                   =        -=----. =              +*.  .::::.          -==--------+           :::=*-       =-
                  -.       -=--:---.=-              +*:  ::::.          :=---------=           -*=--.     .+:
                  -       .=--------:=             ::=**=:::::           :=-------=        .=*=----:     .+.
                  :       =----------=.              :=--=+**++:            ::::      .=#%%=-------.    .:=
                  :      :=-----------=               :=--=-----*%%%%#*+==-    .::-#%%%#%##%%**=--:    .  =
                  :      :=-----------=:             . :=--++*%%%###%#%#%%%=----=+%%%#%##%##%+----: :     -
                  :     .:=------------+              . :=-=**%%%%%#%%%#***%%#==*#%#***%%%%#%%=---:-.      =
                  -     : +-------------+              . .+*%%#%#%%%**********=*****#*****#%%%#*---:        -:
                  :       -=-------------+                 +=-%%%***********+***************#%=----.          -
                  :        +--------------*:                :*=**********************-=+******=---:             -
                   =        +=-------------==                 -********#-+=*********=*    -+=:**---              :.
                   ..  :     -=--------------+-:                **-:-++.  *+*+********=      =+*--:         =     .-
                    :: :+:     ==--------------+=--        .     *:       =+*+*+*******      =***--        ...=    -
                     .= =.==     .===------------         :    .:++=      =*****+*****+:    -****+=.      ::  .+   ::
                     -=-==  :=+======----------:          ::::--=**::     +*****=+******    :****=+-    :-     +   =
                    -=   :-=.   :------------=:    -      :--=+****+=     =******+******.   *****==-. -:      ==:-:
                    =.   :     .-------------=    :::      =********=*    .**#******+***:  .*****+==:     :--:
                    -    .     .--:::::::::---=  =:  .=-:   .+*******:-    *%####****++=:  =-..:=
                                               .-:-      .:=====-:::==:                                                                                                                                                                                                                                                    .
*/
/**
 *
 * @file hina_vk.h
 * @brief HinaVK - A lightweight, multi-threaded Vulkan abstraction layer.
 *
 * HinaVK provides a simplified C API over Vulkan 1.0+, handling the complexity of:
 * - Memory management (VMA integration)
 * - Descriptor set management (Automatic pooling)
 * - Synchronization (Timeline semaphores with fallback)
 * - Dynamic Rendering (No render passes/framebuffers needed)
 *
 * @section usage Usage Model
 * 1. Initialize with `hina_init()`.
 * 2. Create resources (Buffers, Textures) using `hina_make_*` functions.
 * 3. Each frame:
 *    a. Acquire swapchain: `swap = hina_frame_begin()`
 *    b. Record commands into `hina_cmd` buffers
 *    c. Submit commands: `hina_frame_submit(cmd)` and `hina_frame_wait(...)`
 *    d. Call `hina_frame_end()`
 *
 * @section threading Thread Safety
 *
 * HinaVK is designed for **single-threaded rendering** with the global context.
 * All public API functions use the global context `g_hina_ctx` internally.
 *
 * ### Thread-Safe Functions (can be called from any thread)
 * These functions only access thread-safe global pools or make thread-safe Vulkan calls:
 * - **Resource Creation**: `hina_make_buffer`, `hina_make_texture`, `hina_make_sampler`,
 *   `hina_make_pipeline_ex`, `hina_make_query_pool`, `hina_create_bind_group_layout`
 * - **Buffer Memory**: `hina_mapped_buffer_ptr`, `hina_flush_buffer`
 *   (thread-safe for different buffers; same buffer requires external sync)
 *
 * ### NOT Thread-Safe (main thread only)
 * These functions access per-context frame state, command pools, or staging buffers:
 * - **Frame Lifecycle**: `hina_frame_begin`, `hina_frame_end`, `hina_frame_submit`
 * - **Command Recording**: `hina_cmd_begin`, all `hina_cmd_*` functions
 * - **Resource Destruction**: `hina_destroy_buffer`, `hina_destroy_texture`, etc.
 *   (uses deferred destruction: slot is invalidated immediately, but Vulkan object
 *   destruction is deferred until in-flight GPU work completes. Callers must not
 *   destroy resources that will be referenced by future descriptor updates or
 *   command recordings. For resize workflows, create replacements first, then
 *   destroy old resources after the frame completes.)
 * - **Immediate Submission**: `hina_submit_immediate`, `hina_wait_ticket`
 * - **Staging/Upload**: `hina_flush_uploads`, `hina_generate_mips`, `hina_download_texture`
 * - **Bind Groups**: `hina_create_bind_group`, `hina_destroy_bind_group`,
 *   `hina_alloc_transient_bind_group`
 *
 * ### Multi-Threaded Command Recording
 * For parallel command recording, create child contexts via `hina_create_thread_context()`.
 * Each thread context has its own command pools, staging buffers, and can record independently.
 * Submit all command buffers from the main thread.
 *
 * ### Multi-Threaded Staging/Uploads
 * Each context (`hina_context`) has its own staging command buffer and retired page list.
 * When using thread contexts for parallel asset loading:
 * - Call `hina_ctx_flush_uploads(thread_ctx)` to flush that context's staged uploads
 * - The global staging page pool is shared and thread-safe
 * - Ready-check APIs (`hina_buffer_is_ready`, etc.) are thread-safe for different resources
 *
 * ### Vulkan Synchronization Notes
 * - Resource creation (`vkCreate*`) is generally thread-safe in Vulkan
 * - Resource destruction (`vkDestroy*`) requires external sync on the object handle
 * - Descriptor pools require external sync (library handles this internally)
 * - VMA allocation access requires external sync per-allocation (same buffer = sync needed)
 *
 * @section upload_sync Automatic Upload Synchronization
 *
 * When you create a buffer or texture with `initial_data`, HinaVK stages the upload
 * asynchronously. **You do not need to wait for uploads to complete**—the library
 * automatically ensures data is ready before GPU access in most paths.
 *
 * ### Automatic Sync Points
 * The library automatically waits for pending uploads at these locations:
 * - **Persistent bind groups**: `hina_create_bind_group()` waits for all referenced
 *   buffers and textures before writing descriptors.
 * - **Vertex/Index binding**: `hina_cmd_apply_vertex_input()` waits for vertex and
 *   index buffers' uploads to complete.
 * - **Indirect commands**: `hina_cmd_draw_indexed_indirect()` and
 *   `hina_cmd_dispatch_indirect()` wait for the indirect buffer's upload.
 *
 * ### Paths Without Auto-Wait
 * These paths do NOT auto-wait (resources must already be ready):
 * - **Transient bind groups**: `hina_transient_write_buffer()`, `hina_transient_write_combined_image()`,
 *   and similar functions write descriptors immediately without checking upload status.
 * - **Slot-based bindings**: The demo path (`hina_cmd_bind_buffer()`, `hina_cmd_bind_storage_image()`)
 *   only validates in debug builds.
 *
 * For these paths, either ensure resources are ready via `hina_wait_buffer()`/`hina_wait_texture()`,
 * or create resources early enough that uploads complete before use.
 *
 * ### Performance
 * The auto-wait uses a fast path: a per-resource `UPLOAD_READY` bit is checked first
 * (no atomics, no locks). Only resources with pending uploads incur the wait cost,
 * and only once—subsequent uses hit the fast path.
 *
 * ### When to Use Explicit Waits
 * Use `hina_wait_buffer()` or `hina_wait_texture()` for:
 * - **Transient bind groups**: When binding newly-created resources to transient descriptors
 * - **CPU readback**: Reading back from a buffer after GPU write (staging download)
 * - **Polling without blocking**: Use `hina_buffer_is_ready()` to check without waiting
 * - **Explicit sync points**: When you need deterministic timing for profiling
 */
#ifdef __cplusplus
extern "C" {
#endif
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>
//  Symbol Visibility
#ifndef HINA_API
#if defined(HINA_VK_SHARED_EXPORTS)
#if defined(_WIN32)
#define HINA_API __declspec(dllexport)
#elif defined(__GNUC__) || defined(__clang__)
#define HINA_API __attribute__((visibility("default")))
#else
#define HINA_API
#endif
#elif defined(HINA_VK_SHARED_IMPORTS)
#if defined(_WIN32)
#define HINA_API __declspec(dllimport)
#else
#define HINA_API
#endif
#else
#define HINA_API
#endif
#endif
//  Configuration & Constants
#define HINA_INVALID_HANDLE ((uint32_t)0)
// Flag bit helper - makes bit positions explicit and auditable
#define HINA_FLAG_BIT(n) (1u << (n))
// Resource Limits (Vulkan 1.0 core minimum guarantees for maximum portability)
#define HINA_MAX_COLOR_ATTACHMENTS 4u       // maxColorAttachments (Vk1.0 min: 4)
#define HINA_MAX_VERTEX_BUFFERS 8           // maxVertexInputBindings (Vk1.0 min: 16, we use 8)
#define HINA_MAX_VERTEX_ATTRS 16            // maxVertexInputAttributes (Vk1.0 min: 16)
#define HINA_MAX_PUSH_CONSTANT_SIZE 128     // maxPushConstantsSize (Vk1.0 min: 128)
#define HINA_MAX_SPECIALIZATION_CONSTANTS 16
#define HINA_MAX_DESCRIPTOR_SETS 4u         // maxBoundDescriptorSets (Vk1.0 min: 4)
#define HINA_MAX_TILE_INPUTS 4u             // Max input attachments per subpass
#define HINA_MAX_TILE_SUBPASSES 4u          // Max subpasses in a tile pass
// Shader Stages (Bit flags)
#define HINA_STAGE_VERTEX        HINA_FLAG_BIT(0)
#define HINA_STAGE_TESS_CONTROL  HINA_FLAG_BIT(1)
#define HINA_STAGE_TESS_EVAL     HINA_FLAG_BIT(2)
#define HINA_STAGE_GEOMETRY      HINA_FLAG_BIT(3)
#define HINA_STAGE_FRAGMENT      HINA_FLAG_BIT(4)
#define HINA_STAGE_COMPUTE       HINA_FLAG_BIT(5)
#define HINA_STAGE_ALL_GRAPHICS (HINA_STAGE_VERTEX | HINA_STAGE_TESS_CONTROL | HINA_STAGE_TESS_EVAL | \
                                 HINA_STAGE_GEOMETRY | HINA_STAGE_FRAGMENT)
#define HINA_STAGE_ALL (HINA_STAGE_ALL_GRAPHICS | HINA_STAGE_COMPUTE)
//  Logging
/**
 * @brief Log levels used by HinaVK and the shader module.
 */
typedef enum hina_log_level
{
  HINA_LOG_INFO = 0,
  HINA_LOG_WARN = 1,
  HINA_LOG_ERROR = 2
} hina_log_level;

//  Handle Definitions
/**
 * @brief Opaque resource handles.
 * HinaVK uses 32-bit IDs/Handles instead of pointers for resources.
 * These are safe to copy and lightweight.
 */
typedef struct { uint32_t id; } hina_buffer;
typedef struct { uint32_t id; } hina_texture;
typedef struct { uint32_t id; } hina_texture_view;
typedef struct { uint32_t id; } hina_sampler;
typedef struct { uint32_t id; } hina_pipeline;

/**
 * @brief Shader module for raw SPIR-V pipelines.
 *
 * Contains SPIR-V bytecode and a lazily-cached VkShaderModule.
 * The VkShaderModule is created on first pipeline use and cached for reuse.
 *
 * Ownership:
 * - SPIR-V data: Caller-owned if owns_spirv=false, hina-owned if owns_spirv=true.
 *   WARNING: Do NOT free spirv_data until after hina_destroy_shader()!
 * - VkShaderModule: hina-owned, created lazily, destroyed by hina_destroy_shader().
 */
typedef struct hina_shader
{
  const void* spirv_data; // SPIR-V bytecode
  size_t spirv_size; // Size in bytes
  void* module; // Cached VkShaderModule (NULL until first use)
  bool owns_spirv; // If true, hina_destroy_shader() frees spirv_data
} hina_shader;

/**
 * @brief Pipeline kind classification.
 */
typedef enum hina_pipeline_kind
{
  HINA_PIPELINE_KIND_GRAPHICS = 0,
  HINA_PIPELINE_KIND_COMPUTE = 1,
} hina_pipeline_kind;
typedef struct { uint32_t id; } hina_query_pool;
typedef struct { uint32_t id; } hina_bind_group_layout;
typedef struct { uint32_t id; } hina_bind_group;

/**
 * @brief Pass layout key - encodes render target configuration.
 * 64-bit key packing color formats, depth format, and sample count.
 * Used for pipeline compatibility validation with render passes.
 */
typedef struct { uint64_t key; } hina_pass_layout;

// Resource Validity - validates handle generation against pool (catches use-after-free)
HINA_API bool hina_buffer_is_valid(hina_buffer b);

HINA_API bool hina_texture_is_valid(hina_texture t);

HINA_API bool hina_sampler_is_valid(hina_sampler s);

HINA_API bool hina_shader_is_valid(const hina_shader* sh);

HINA_API bool hina_pipeline_is_valid(hina_pipeline p);

HINA_API bool hina_query_pool_is_valid(hina_query_pool p);

HINA_API bool hina_bind_group_layout_is_valid(hina_bind_group_layout layout);

HINA_API bool hina_bind_group_is_valid(hina_bind_group group);

// ---------------------------------------------------------------------------
// Resource Upload Readiness
// ---------------------------------------------------------------------------
/**
 * @brief Check if a buffer's initial upload has completed.
 *
 * Buffers created with initial_data are uploaded asynchronously via the staging
 * system. This function returns true once the upload has been submitted and the
 * GPU has completed (or will complete before any use).
 *
 * @note Most users don't need this - HinaVK automatically waits for uploads
 *       when you bind or use a resource. This API is for advanced scenarios
 *       where you want to poll upload status without blocking.
 *
 * @param b The buffer to check
 * @return true if ready for GPU use, false if upload still pending
 */
HINA_API bool hina_buffer_is_ready(hina_buffer b);

/**
 * @brief Check if a texture's initial upload has completed.
 * @see hina_buffer_is_ready for details
 */
HINA_API bool hina_texture_is_ready(hina_texture t);

/**
 * @brief Block until a buffer's initial upload has completed.
 *
 * If the buffer has pending upload work, this flushes staged uploads and waits
 * for the GPU to complete. Returns immediately if already ready.
 *
 * @note Most users don't need this - HinaVK automatically waits for uploads
 *       when you bind or use a resource. This API is for advanced scenarios
 *       where you need explicit synchronization before accessing buffer data.
 *
 * @param b The buffer to wait for
 */
HINA_API void hina_wait_buffer(hina_buffer b);

/**
 * @brief Block until a texture's initial upload has completed.
 * @see hina_wait_buffer for details
 */
HINA_API void hina_wait_texture(hina_texture t);

/**
 * @brief Synchronization ticket for immediate/threadsafe submissions.
 *
 * Returned by `hina_submit_threadsafe()` for GPU work submitted outside the frame lifecycle.
 * Use `hina_wait_ticket()` to block until the GPU completes the associated work.
 *
 * @note For frame-based submission, use `hina_frame_submit()` which returns `hina_sync_point`
 *       instead. Sync points are frame-local and used with `hina_frame_wait()` for intra-frame
 *       dependencies between queues.
 *
 * @see hina_submit_threadsafe, hina_wait_ticket, hina_sync_point
 */
typedef uint64_t hina_ticket;
typedef struct hina_context hina_context;
typedef struct hina_cmd hina_cmd; // Opaque command buffer wrapper

/**
 * @brief Flush all staged uploads to the GPU.
 *
 * Resources created with initial_data are staged but not immediately submitted.
 * Call this to explicitly submit all pending uploads to the GPU. Returns a ticket
 * that can be waited on with hina_wait_ticket().
 *
 * @note Normally you don't need to call this - uploads are automatically flushed
 *       at frame_end() or on-demand when resources are used. This API is for
 *       power users who need explicit control over upload timing.
 *
 * @return Ticket for the flushed uploads, or 0 if nothing was staged
 */
HINA_API hina_ticket hina_flush_uploads(void);

/**
 * @brief Flush staged uploads for a specific context.
 *
 * Each context has its own staging command buffer and pending upload list.
 * Use this when doing multi-threaded asset loading where each thread has
 * its own context created via `hina_create_thread_context()`.
 *
 * @note The staging page pool is shared across all contexts (thread-safe),
 *       but each context's staging command buffer is context-local.
 *
 * @param ctx Context whose staged uploads should be flushed
 * @return Ticket for the flushed uploads, or 0 if nothing was staged
 * @see hina_flush_uploads, hina_create_thread_context
 */
HINA_API hina_ticket hina_ctx_flush_uploads(hina_context* ctx);

/**
 * @brief Queue type for command submission.
 * Selects which GPU queue family to use for command buffer recording and submission.
 */
typedef enum hina_queue
{
  HINA_QUEUE_GRAPHICS = 0, // Graphics + compute + transfer (most flexible)
  HINA_QUEUE_COMPUTE = 1, // Async compute (may be same as graphics on some GPUs)
  HINA_QUEUE_TRANSFER = 2, // Dedicated transfer/DMA queue (if available)
} hina_queue;

/**
 * @brief Swapchain image handle.
 * Returned by hina_frame_begin(), presented at hina_frame_end().
 * Contains the texture and internal synchronization state.
 */
typedef struct hina_swapchain_image
{
  hina_texture texture; // The swapchain backbuffer texture
  uint32_t image_index; // Vulkan swapchain image index
  uint32_t sem_index; // Internal semaphore index for sync
} hina_swapchain_image;

//  Enums (Vulkan Abstractions)
typedef enum
{
  HINA_INDEX_UINT16 = 0,
  HINA_INDEX_UINT32 = 1
} hina_index_type;

/**
 * @brief Texture and render target formats.
 *
 * @section special_values Special Values
 * - **HINA_FORMAT_UNDEFINED (0)**: Indicates no attachment or unused slot.
 *   In `color_formats[]` arrays, the first UNDEFINED terminates the list.
 *   Sparse attachments (UNDEFINED between valid formats) are NOT supported.
 *
 * @section format_mapping Vulkan Mapping
 * All other values map 1:1 to VkFormat via a lookup table. The enum values
 * are stable and can be serialized. Adding new formats appends to the enum.
 */
typedef enum
{
  HINA_FORMAT_UNDEFINED = 0,  /**< No attachment / unused slot (terminates color_formats array) */
  HINA_FORMAT_R8_UNORM,
  HINA_FORMAT_R8_SNORM,
  HINA_FORMAT_R8_UINT,
  HINA_FORMAT_R8_SINT,
  HINA_FORMAT_R8G8_UNORM,
  HINA_FORMAT_R8G8_SNORM,
  HINA_FORMAT_R8G8_UINT,
  HINA_FORMAT_R8G8_SINT,
  HINA_FORMAT_R8G8B8_UNORM,
  HINA_FORMAT_R8G8B8_SNORM,
  HINA_FORMAT_R8G8B8_UINT,
  HINA_FORMAT_R8G8B8_SINT,
  HINA_FORMAT_R8G8B8A8_UNORM,
  HINA_FORMAT_R8G8B8A8_SNORM,
  HINA_FORMAT_R8G8B8A8_UINT,
  HINA_FORMAT_R8G8B8A8_SINT,
  HINA_FORMAT_R8G8B8A8_SRGB,
  HINA_FORMAT_B8G8R8A8_UNORM,
  HINA_FORMAT_B8G8R8A8_SRGB,
  HINA_FORMAT_R16_UNORM,
  HINA_FORMAT_R16_SNORM,
  HINA_FORMAT_R16_UINT,
  HINA_FORMAT_R16_SINT,
  HINA_FORMAT_R16_SFLOAT,
  HINA_FORMAT_R16G16_UNORM,
  HINA_FORMAT_R16G16_SNORM,
  HINA_FORMAT_R16G16_UINT,
  HINA_FORMAT_R16G16_SINT,
  HINA_FORMAT_R16G16_SFLOAT,
  HINA_FORMAT_R16G16B16_UNORM,
  HINA_FORMAT_R16G16B16_SNORM,
  HINA_FORMAT_R16G16B16_UINT,
  HINA_FORMAT_R16G16B16_SINT,
  HINA_FORMAT_R16G16B16_SFLOAT,
  HINA_FORMAT_R16G16B16A16_UNORM,
  HINA_FORMAT_R16G16B16A16_SNORM,
  HINA_FORMAT_R16G16B16A16_UINT,
  HINA_FORMAT_R16G16B16A16_SINT,
  HINA_FORMAT_R16G16B16A16_SFLOAT,
  HINA_FORMAT_R32_UINT,
  HINA_FORMAT_R32_SINT,
  HINA_FORMAT_R32_SFLOAT,
  HINA_FORMAT_R32G32_UINT,
  HINA_FORMAT_R32G32_SINT,
  HINA_FORMAT_R32G32_SFLOAT,
  HINA_FORMAT_R32G32B32_UINT,
  HINA_FORMAT_R32G32B32_SINT,
  HINA_FORMAT_R32G32B32_SFLOAT,
  HINA_FORMAT_R32G32B32A32_UINT,
  HINA_FORMAT_R32G32B32A32_SINT,
  HINA_FORMAT_R32G32B32A32_SFLOAT,
  HINA_FORMAT_D16_UNORM,
  HINA_FORMAT_X8_D24_UNORM_PACK32,
  HINA_FORMAT_D32_SFLOAT,
  HINA_FORMAT_S8_UINT,
  HINA_FORMAT_D16_UNORM_S8_UINT,
  HINA_FORMAT_D24_UNORM_S8_UINT,
  HINA_FORMAT_D32_SFLOAT_S8_UINT,
  HINA_FORMAT_BC1_RGB_UNORM_BLOCK,
  HINA_FORMAT_BC1_RGB_SRGB_BLOCK,
  HINA_FORMAT_BC1_RGBA_UNORM_BLOCK,
  HINA_FORMAT_BC1_RGBA_SRGB_BLOCK,
  HINA_FORMAT_BC2_UNORM_BLOCK,
  HINA_FORMAT_BC2_SRGB_BLOCK,
  HINA_FORMAT_BC3_UNORM_BLOCK,
  HINA_FORMAT_BC3_SRGB_BLOCK,
  HINA_FORMAT_BC4_UNORM_BLOCK,
  HINA_FORMAT_BC4_SNORM_BLOCK,
  HINA_FORMAT_BC5_UNORM_BLOCK,
  HINA_FORMAT_BC5_SNORM_BLOCK,
  HINA_FORMAT_BC6H_UFLOAT_BLOCK,
  HINA_FORMAT_BC6H_SFLOAT_BLOCK,
  HINA_FORMAT_BC7_UNORM_BLOCK,
  HINA_FORMAT_BC7_SRGB_BLOCK,
  // ETC2 formats
  HINA_FORMAT_ETC2_R8G8B8_UNORM_BLOCK,
  HINA_FORMAT_ETC2_R8G8B8_SRGB_BLOCK,
  HINA_FORMAT_ETC2_R8G8B8A1_UNORM_BLOCK,
  HINA_FORMAT_ETC2_R8G8B8A1_SRGB_BLOCK,
  HINA_FORMAT_ETC2_R8G8B8A8_UNORM_BLOCK,
  HINA_FORMAT_ETC2_R8G8B8A8_SRGB_BLOCK,
  // EAC formats
  HINA_FORMAT_EAC_R11_UNORM_BLOCK,
  HINA_FORMAT_EAC_R11_SNORM_BLOCK,
  HINA_FORMAT_EAC_R11G11_UNORM_BLOCK,
  HINA_FORMAT_EAC_R11G11_SNORM_BLOCK,
  // ASTC LDR formats
  HINA_FORMAT_ASTC_4x4_UNORM_BLOCK,
  HINA_FORMAT_ASTC_4x4_SRGB_BLOCK,
  HINA_FORMAT_ASTC_5x4_UNORM_BLOCK,
  HINA_FORMAT_ASTC_5x4_SRGB_BLOCK,
  HINA_FORMAT_ASTC_5x5_UNORM_BLOCK,
  HINA_FORMAT_ASTC_5x5_SRGB_BLOCK,
  HINA_FORMAT_ASTC_6x5_UNORM_BLOCK,
  HINA_FORMAT_ASTC_6x5_SRGB_BLOCK,
  HINA_FORMAT_ASTC_6x6_UNORM_BLOCK,
  HINA_FORMAT_ASTC_6x6_SRGB_BLOCK,
  HINA_FORMAT_ASTC_8x5_UNORM_BLOCK,
  HINA_FORMAT_ASTC_8x5_SRGB_BLOCK,
  HINA_FORMAT_ASTC_8x6_UNORM_BLOCK,
  HINA_FORMAT_ASTC_8x6_SRGB_BLOCK,
  HINA_FORMAT_ASTC_8x8_UNORM_BLOCK,
  HINA_FORMAT_ASTC_8x8_SRGB_BLOCK,
  HINA_FORMAT_ASTC_10x5_UNORM_BLOCK,
  HINA_FORMAT_ASTC_10x5_SRGB_BLOCK,
  HINA_FORMAT_ASTC_10x6_UNORM_BLOCK,
  HINA_FORMAT_ASTC_10x6_SRGB_BLOCK,
  HINA_FORMAT_ASTC_10x8_UNORM_BLOCK,
  HINA_FORMAT_ASTC_10x8_SRGB_BLOCK,
  HINA_FORMAT_ASTC_10x10_UNORM_BLOCK,
  HINA_FORMAT_ASTC_10x10_SRGB_BLOCK,
  HINA_FORMAT_ASTC_12x10_UNORM_BLOCK,
  HINA_FORMAT_ASTC_12x10_SRGB_BLOCK,
  HINA_FORMAT_ASTC_12x12_UNORM_BLOCK,
  HINA_FORMAT_ASTC_12x12_SRGB_BLOCK,
  HINA_FORMAT_MAX_ENUM = 0x7FFFFFFF
} hina_format;

typedef enum
{
  HINA_PRIMITIVE_TOPOLOGY_POINT_LIST = 0,
  HINA_PRIMITIVE_TOPOLOGY_LINE_LIST = 1,
  HINA_PRIMITIVE_TOPOLOGY_LINE_STRIP = 2,
  HINA_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST = 3,
  HINA_PRIMITIVE_TOPOLOGY_TRIANGLE_STRIP = 4,
  HINA_PRIMITIVE_TOPOLOGY_TRIANGLE_FAN = 5,
  HINA_PRIMITIVE_TOPOLOGY_LINE_LIST_WITH_ADJACENCY = 6,
  HINA_PRIMITIVE_TOPOLOGY_LINE_STRIP_WITH_ADJACENCY = 7,
  HINA_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST_WITH_ADJACENCY = 8,
  HINA_PRIMITIVE_TOPOLOGY_TRIANGLE_STRIP_WITH_ADJACENCY = 9,
  HINA_PRIMITIVE_TOPOLOGY_PATCH_LIST = 10
} hina_primitive_topology;

typedef enum
{
  HINA_POLYGON_MODE_FILL = 0,
  HINA_POLYGON_MODE_LINE = 1,
  HINA_POLYGON_MODE_POINT = 2
} hina_polygon_mode;

typedef enum
{
  HINA_CULL_MODE_NONE           = 0,
  HINA_CULL_MODE_FRONT          = HINA_FLAG_BIT(0),
  HINA_CULL_MODE_BACK           = HINA_FLAG_BIT(1),
  HINA_CULL_MODE_FRONT_AND_BACK = HINA_CULL_MODE_FRONT | HINA_CULL_MODE_BACK
} hina_cull_mode;

typedef enum
{
  HINA_FRONT_FACE_COUNTER_CLOCKWISE = 0,
  HINA_FRONT_FACE_CLOCKWISE = 1
} hina_front_face;

typedef enum
{
  HINA_COMPARE_OP_NEVER = 0,
  HINA_COMPARE_OP_LESS = 1,
  HINA_COMPARE_OP_EQUAL = 2,
  HINA_COMPARE_OP_LESS_OR_EQUAL = 3,
  HINA_COMPARE_OP_GREATER = 4,
  HINA_COMPARE_OP_NOT_EQUAL = 5,
  HINA_COMPARE_OP_GREATER_OR_EQUAL = 6,
  HINA_COMPARE_OP_ALWAYS = 7
} hina_compare_op;

typedef enum
{
  HINA_BLEND_FACTOR_ZERO = 0,
  HINA_BLEND_FACTOR_ONE = 1,
  HINA_BLEND_FACTOR_SRC_COLOR = 2,
  HINA_BLEND_FACTOR_ONE_MINUS_SRC_COLOR = 3,
  HINA_BLEND_FACTOR_DST_COLOR = 4,
  HINA_BLEND_FACTOR_ONE_MINUS_DST_COLOR = 5,
  HINA_BLEND_FACTOR_SRC_ALPHA = 6,
  HINA_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA = 7,
  HINA_BLEND_FACTOR_DST_ALPHA = 8,
  HINA_BLEND_FACTOR_ONE_MINUS_DST_ALPHA = 9,
  HINA_BLEND_FACTOR_CONSTANT_COLOR = 10,
  HINA_BLEND_FACTOR_ONE_MINUS_CONSTANT_COLOR = 11,
  HINA_BLEND_FACTOR_CONSTANT_ALPHA = 12,
  HINA_BLEND_FACTOR_ONE_MINUS_CONSTANT_ALPHA = 13,
  HINA_BLEND_FACTOR_SRC_ALPHA_SATURATE = 14,
  HINA_BLEND_FACTOR_SRC1_COLOR = 15,
  HINA_BLEND_FACTOR_ONE_MINUS_SRC1_COLOR = 16,
  HINA_BLEND_FACTOR_SRC1_ALPHA = 17,
  HINA_BLEND_FACTOR_ONE_MINUS_SRC1_ALPHA = 18
} hina_blend_factor;

typedef enum
{
  HINA_BLEND_OP_ADD = 0,
  HINA_BLEND_OP_SUBTRACT = 1,
  HINA_BLEND_OP_REVERSE_SUBTRACT = 2,
  HINA_BLEND_OP_MIN = 3,
  HINA_BLEND_OP_MAX = 4
} hina_blend_op;

typedef enum
{
  HINA_COLOR_COMPONENT_R_BIT = HINA_FLAG_BIT(0),
  HINA_COLOR_COMPONENT_G_BIT = HINA_FLAG_BIT(1),
  HINA_COLOR_COMPONENT_B_BIT = HINA_FLAG_BIT(2),
  HINA_COLOR_COMPONENT_A_BIT = HINA_FLAG_BIT(3),
  HINA_COLOR_COMPONENT_ALL = HINA_COLOR_COMPONENT_R_BIT | HINA_COLOR_COMPONENT_G_BIT | HINA_COLOR_COMPONENT_B_BIT |
  HINA_COLOR_COMPONENT_A_BIT
} hina_color_component_flags;

typedef enum
{
  HINA_LOGIC_OP_CLEAR = 0,
  HINA_LOGIC_OP_AND = 1,
  HINA_LOGIC_OP_AND_REVERSE = 2,
  HINA_LOGIC_OP_COPY = 3,
  HINA_LOGIC_OP_AND_INVERTED = 4,
  HINA_LOGIC_OP_NO_OP = 5,
  HINA_LOGIC_OP_XOR = 6,
  HINA_LOGIC_OP_OR = 7,
  HINA_LOGIC_OP_NOR = 8,
  HINA_LOGIC_OP_EQUIVALENT = 9,
  HINA_LOGIC_OP_INVERT = 10,
  HINA_LOGIC_OP_OR_REVERSE = 11,
  HINA_LOGIC_OP_COPY_INVERTED = 12,
  HINA_LOGIC_OP_OR_INVERTED = 13,
  HINA_LOGIC_OP_NAND = 14,
  HINA_LOGIC_OP_SET = 15
} hina_logic_op;

typedef enum
{
  HINA_VERTEX_INPUT_RATE_VERTEX = 0,
  HINA_VERTEX_INPUT_RATE_INSTANCE = 1
} hina_vertex_input_rate;

typedef enum
{
  HINA_SAMPLE_COUNT_1_BIT  = HINA_FLAG_BIT(0),
  HINA_SAMPLE_COUNT_2_BIT  = HINA_FLAG_BIT(1),
  HINA_SAMPLE_COUNT_4_BIT  = HINA_FLAG_BIT(2),
  HINA_SAMPLE_COUNT_8_BIT  = HINA_FLAG_BIT(3),
  HINA_SAMPLE_COUNT_16_BIT = HINA_FLAG_BIT(4),
  HINA_SAMPLE_COUNT_32_BIT = HINA_FLAG_BIT(5),
  HINA_SAMPLE_COUNT_64_BIT = HINA_FLAG_BIT(6)
} hina_sample_count;

//  Initialization & Lifecycle
// Logging & Allocator Callbacks
typedef void (*hina_log_fn)(const char* msg);

/**
 * @brief Initialization flags for hina_init().
 * Uses bits 0-7 (low byte).
 */
typedef enum
{
  // TERMINATE_ON_ERROR: Call abort() on any Vulkan error instead of returning
  // failure codes. Useful during development to catch errors immediately.
  HINA_INIT_TERMINATE_ON_ERROR_BIT = HINA_FLAG_BIT(0),
  // VALIDATION: Enable Vulkan validation layers (VK_LAYER_KHRONOS_validation).
  // Catches API misuse, memory errors, and sync issues. Enable in debug builds.
  // Significant performance overhead - disable in release builds.
  HINA_INIT_VALIDATION_BIT         = HINA_FLAG_BIT(1),

  // PREFER_INTEGRATED: Select integrated GPU over discrete if both are present.
  // Useful for laptops to save power, or when discrete GPU is overkill.
  HINA_INIT_PREFER_INTEGRATED_BIT  = HINA_FLAG_BIT(2),
  // CREATE_SWAPCHAIN: Create swapchain during hina_init() if a surface exists.
  // Non-fatal if creation fails (e.g., hidden/minimized window).
  HINA_INIT_CREATE_SWAPCHAIN_BIT   = HINA_FLAG_BIT(3),
  // bits 4-7 reserved for future public flags
} hina_init_flags;

/**
 * @brief Internal debug flags for testing Vulkan fallback paths.
 * Uses bits 8-15 (second byte). These are for testing/debugging only.
 */
typedef enum
{
  // Force VkRenderPass/VkFramebuffer path instead of dynamic rendering.
  // Tests compatibility with Vulkan 1.0/1.1 devices.
  HINA_DEBUG_FORCE_LEGACY_RENDERPASS_BIT = HINA_FLAG_BIT(8),
  // Use per-submission VkFence instead of timeline semaphores.
  // Tests the Vulkan 1.1 fallback path.
  HINA_DEBUG_NO_TIMELINE_SEMAPHORE_BIT   = HINA_FLAG_BIT(9),

  // Run compute/transfer work on the graphics queue instead of dedicated queues.
  // Tests single-queue device behavior.
  HINA_DEBUG_FORCE_SINGLE_QUEUE_BIT      = HINA_FLAG_BIT(10),

  // Enable VK_EXT_debug_utils object naming for RenderDoc/debugging.
  // Gives meaningful names to Vulkan objects in capture tools.
  HINA_DEBUG_NAMES_BIT                   = HINA_FLAG_BIT(11),

  // Force legacy vkQueueSubmit/vkCmdPipelineBarrier instead of Sync2 extensions.
  // Tests the Vulkan 1.2 fallback path.
  HINA_DEBUG_NO_SYNC2_BIT                = HINA_FLAG_BIT(12),

  // Force compute to use a separate queue family (even if same as graphics).
  // Tests queue ownership transfer validation.
  HINA_DEBUG_FORCE_SEPARATE_FAMILIES_BIT = HINA_FLAG_BIT(13),
  // Force legacy multi-subpass VkRenderPass for tile passes instead of
  // VK_KHR_dynamic_rendering_local_read. Tests Vulkan 1.0-1.3 compatibility.
  HINA_DEBUG_FORCE_LEGACY_TILE_PASS_BIT  = HINA_FLAG_BIT(14),
  // bits 15-31 reserved for future debug flags
} hina_debug_flags;

/**
 * @brief Vulkan version enum for capability queries and version limiting.
 * Used in hina_device_caps.vk_version and hina_desc.max_api_version.
 */
typedef enum
{
  HINA_VK_VERSION_AUTO = 0, // For max_api_version: use highest supported
  HINA_VK_VERSION_1_0 = 1,
  HINA_VK_VERSION_1_1 = 2,
  HINA_VK_VERSION_1_2 = 3,
  HINA_VK_VERSION_1_3 = 4,
  HINA_VK_VERSION_1_4 = 5
} hina_vk_version;

/**
 * @brief Initialization descriptor for `hina_init`.
 * Memory layout optimized: 8-byte fields first, then 4-byte fields.
 */
typedef struct hina_desc
{
  // Platform Handles (8B each)
  void* native_window; // e.g. HWND, NSWindow, xcb_window_t (Optional: Headless if NULL)
  void* native_display; // e.g. Display*, Wayland display (Optional)
  // Extensions - pointers (8B each)
  const char* const* instance_exts;
  const char* const* device_exts;
  // Pipeline Cache - blob from hina_get_pipeline_cache_data() loaded from disk.
  // Validated internally by hina_init(); silently ignored if stale or corrupt.
  // Safe to free immediately after hina_init() returns.
  struct { const void* data; size_t size; } pipeline_cache;

  // Callbacks (8B each)
  hina_log_fn log_fn;
  // Settings (8B)
  uint64_t staging_buffer_size; // Default: 16MB. Used for internal upload buffers.
  // 4-byte fields grouped together (no padding between them)
  uint32_t instance_ext_count;
  uint32_t device_ext_count;
  uint32_t flags; // Combination of hina_init_flags (and hina_debug_flags for testing)
  // Vulkan API version limit (HINA_VK_VERSION_AUTO = highest supported).
  // Use this to force a lower API version for compatibility testing or to target
  // specific Vulkan versions. When set, the library will not use features from
  // higher versions even if the device supports them.
  hina_vk_version max_api_version;
} hina_desc;

// Defaults: .staging_buffer_size = 16 * 1024 * 1024 (16MB)
HINA_API hina_desc hina_desc_default(void);

/**
 * @brief Initializes the HinaVK library.
 * Creates Vulkan Instance, Device, and global Context.
 */
HINA_API bool hina_init(const hina_desc* desc);

/**
 * @brief Shuts down HinaVK and releases all resources.
 */
HINA_API void hina_shutdown(void);

// Thread Context Creation
// HinaVK allows creating child contexts for multi-threaded command recording.
HINA_API hina_context* hina_create_thread_context(void);

HINA_API void hina_destroy_thread_context(hina_context* ctx);

//  Platform & Window Management
typedef struct hina_vulkan_handles
{
  // 8-byte pointers
  void* instance;
  void* physical_device;
  void* device;
  void* surface;
  void* graphics_queue;
  // 4-byte fields (4B trailing padding for 8B struct alignment)
  uint32_t graphics_queue_family;
  uint32_t pad_;
} hina_vulkan_handles;

// Retrieve raw Vulkan handles
HINA_API bool hina_get_vulkan_handles(hina_vulkan_handles* out_handles);

/**
 * @brief Device capabilities structure.
 * Query with hina_get_device_caps() after initialization.
 */
typedef struct hina_device_caps
{
  // Device info
  char device_name[256]; // GPU device name (e.g., "NVIDIA GeForce RTX 3080")
  // Version info
  uint32_t api_version; // Raw Vulkan API version (VK_MAKE_API_VERSION)
  hina_vk_version vk_version; // Simplified version enum
  // Feature flags - capabilities detected at init
  bool has_dynamic_rendering; // VK_KHR_dynamic_rendering or 1.3 core
  bool has_dynamic_rendering_local_read; // VK_KHR_dynamic_rendering_local_read for tile pass
  bool has_dynamic_rendering_local_read_depth_stencil; // VK 1.4 property: depth/stencil input attachments
  bool has_dynamic_rendering_unused_attachments; // VK_EXT_dynamic_rendering_unused_attachments
  bool has_timeline_semaphore; // VK_KHR_timeline_semaphore or 1.2+ core
  bool has_dynamic_state2; // VK_EXT_extended_dynamic_state2
  bool has_fill_mode_non_solid; // Wireframe rendering support
  bool has_wide_lines; // Wide line support
  bool has_geometry_shader; // Geometry shader support
  bool has_tessellation_shader; // Tessellation shader support
  bool has_sampler_anisotropy; // Anisotropic filtering support
  bool has_independent_blend; // Per-attachment blend states (needed for tile pass)
  // Limits
  uint32_t min_uniform_buffer_alignment; // Required UBO alignment
  uint32_t min_storage_buffer_alignment; // Required SSBO alignment
  uint64_t max_uniform_buffer_range; // Max UBO range (bytes)
  uint64_t max_storage_buffer_range; // Max SSBO range (bytes)
  float max_sampler_anisotropy; // Max anisotropy level (1.0 if unsupported)
  // Subgroup (wavefront) properties - useful for compute shader optimization
  uint32_t subgroup_size; // Default subgroup size (e.g., 32 for NVIDIA, 64 for AMD)
  uint32_t min_subgroup_size; // Minimum supported (if VK_EXT_subgroup_size_control)
  uint32_t max_subgroup_size; // Maximum supported (if VK_EXT_subgroup_size_control)
  // Timing
  double timestamp_period_ns; // Timestamp query period in nanoseconds
} hina_device_caps;

/**
 * @brief Query device capabilities.
 * @return Pointer to internal caps struct, valid until hina_shutdown().
 */
HINA_API const hina_device_caps* hina_get_device_caps(void);

// ---------------------------------------------------------------------------
// Debug Capabilities (Internal Extension Status)
// ---------------------------------------------------------------------------
//
// For development/debugging only. These represent internal optimizations that
// hina-vk applies transparently - applications should NOT branch on these.
// Exposed only to help diagnose performance and verify extension paths.
//
/**
 * @brief Internal extension status for debugging.
 * These flags indicate which Vulkan extensions/features are being used
 * internally by hina-vk. Applications should NOT make decisions based on these.
 */
typedef struct hina_debug_caps
{
  // Device support flags (set during device creation from physical device query)
  bool has_synchronization2; // Device supports VK_KHR_synchronization2 or 1.3+
  bool has_device_fault; // Device supports VK_EXT_device_fault
  bool has_pipeline_creation_feedback; // VK_EXT_pipeline_creation_feedback
  bool has_subgroup_size_control; // VK_EXT_subgroup_size_control
  bool has_memory_budget; // VK_EXT_memory_budget (accurate GPU memory tracking)
  bool has_maintenance4; // VK_KHR_maintenance4 or 1.3+ (query mem reqs without object)
  bool has_maintenance5; // VK_KHR_maintenance5 or 1.4+ (inline shader modules)
  // Active usage flags (set after volkLoadDevice from function pointers)
  bool uses_synchronization2; // Using vkQueueSubmit2/vkCmdPipelineBarrier2
  bool uses_dynamic_rendering; // Using VK_KHR_dynamic_rendering (vs legacy render passes)
  bool uses_timeline_semaphore; // Using timeline semaphores for sync
  // Queue topology
  bool has_dedicated_compute; // Compute queue is separate from graphics (async compute)
} hina_debug_caps;

/**
 * @brief Query internal debug capabilities.
 * For development/debugging only - shows which internal optimizations are active.
 * @return Pointer to debug caps struct, valid until hina_shutdown().
 */
HINA_API const hina_debug_caps* hina_get_debug_caps(void);

// ---------------------------------------------------------------------------
// Frame Statistics
// ---------------------------------------------------------------------------
/**
 * @brief Per-frame statistics for performance monitoring.
 * Counters are always accumulated (no branch overhead in hot paths).
 * Call hina_get_frame_stats() to retrieve stats for the last completed frame.
 */
typedef struct hina_frame_stats
{
  double frame_time_ms; ///< CPU frame time (frame_end - frame_begin)
  double gpu_time_ms; ///< GPU time (requires timestamp queries, 0 if unavailable)
  uint32_t draw_calls; ///< Number of draw/draw_indexed calls
  uint32_t dispatch_calls; ///< Number of compute dispatch calls
  uint32_t pipeline_binds; ///< Number of pipeline binds
  uint32_t descriptor_writes; ///< Number of bind group binds
  uint64_t vertices_submitted; ///< Total vertices submitted
  uint64_t gpu_memory_used; ///< GPU memory used (bytes, from VMA)
  uint64_t gpu_memory_budget; ///< GPU memory budget (bytes, from VMA)
} hina_frame_stats;

/**
 * @brief Get statistics for the last completed frame.
 * Always returns valid data (zeroed if no frames completed yet).
 */
HINA_API hina_frame_stats hina_get_frame_stats(void);

HINA_API hina_frame_stats hina_ctx_get_frame_stats(hina_context* ctx);

/**
 * @brief Binds a native window to the context if not provided during init.
 * Can be called multiple times to change the window (e.g., Android lifecycle).
 * Pass NULL to switch to headless mode.
 */
HINA_API bool hina_set_native_window(void* window, void* display);

/**
 * @brief Notify the library that the surface has been lost.
 *
 * Call this when the platform indicates the surface is no longer valid
 * (e.g., Android Activity paused, window minimized). This puts the library
 * into a "surface lost" state where frame operations become no-ops until
 * the surface is recreated.
 *
 * @note On Android, call this in response to APP_CMD_TERM_WINDOW or when
 *       SDL_WINDOWEVENT_MINIMIZED is received.
 *
 * @see hina_is_surface_lost(), hina_recreate_surface()
 */
HINA_API void hina_surface_lost(void);

//  Frame Submission
//
// The frame is the submission builder. Record anywhere, submit on the main thread.
//
// INVARIANTS:
// 1. NO INFERENCE - Only hina_frame_wait*() and swapchain wiring create
//    dependencies. The library never infers ordering from resources or layouts.
// 2. WAITS ATTACH TO SUBMITS - A wait affects the next hina_frame_submit() on the
//    destination queue and anything after, not retroactively.
// 3. SYNC POINT = SUBMIT BOUNDARY - hina_sync_point{q,i} means "after the i-th
//    submitted item on logical queue q completes on the GPU".
// 4. ALIASED QUEUES = NO GPU SYNC - If graphics and compute map to the same
//    physical VkQueue, cross-queue waits compile to pure ordering (no semaphores).
typedef uint64_t hina_stage_mask;
#define HINA_PIPE_STAGE_NONE                    0ULL
#define HINA_PIPE_STAGE_TOP_OF_PIPE             (1ULL << 0)
#define HINA_PIPE_STAGE_DRAW_INDIRECT           (1ULL << 1)
#define HINA_PIPE_STAGE_VERTEX_INPUT            (1ULL << 2)
#define HINA_PIPE_STAGE_VERTEX_SHADER           (1ULL << 3)
#define HINA_PIPE_STAGE_FRAGMENT_SHADER         (1ULL << 4)
#define HINA_PIPE_STAGE_EARLY_FRAGMENT_TESTS    (1ULL << 5)
#define HINA_PIPE_STAGE_LATE_FRAGMENT_TESTS     (1ULL << 6)
#define HINA_PIPE_STAGE_COLOR_ATTACHMENT_OUTPUT (1ULL << 7)
#define HINA_PIPE_STAGE_COMPUTE_SHADER          (1ULL << 8)
#define HINA_PIPE_STAGE_TRANSFER                (1ULL << 9)
#define HINA_PIPE_STAGE_BOTTOM_OF_PIPE          (1ULL << 10)
#define HINA_PIPE_STAGE_HOST                    (1ULL << 11)
#define HINA_PIPE_STAGE_ALL_GRAPHICS            (1ULL << 12)
#define HINA_PIPE_STAGE_ALL_COMMANDS            (1ULL << 13)
/**
 * @brief Sync point representing a submit boundary (frame-local).
 *
 * Returned by `hina_frame_submit()` to identify a submission within the current frame.
 * Use with `hina_frame_wait()` to declare dependencies between queues (e.g., compute→graphics).
 *
 * Sync points are only valid within the frame they were created. After `hina_frame_end()`,
 * all sync points from that frame become invalid.
 *
 * @note For submissions outside the frame lifecycle, use `hina_submit_threadsafe()` which
 *       returns a `hina_ticket` instead.
 *
 * @see hina_frame_submit, hina_frame_wait, hina_ticket
 */
typedef struct hina_sync_point
{
  uint16_t queue; // Logical queue (HINA_QUEUE_GRAPHICS/COMPUTE)
  uint16_t index; // Boundary index within that queue's stream
} hina_sync_point;
#ifdef __cplusplus
#define HINA_SYNC_POINT_INVALID hina_sync_point{0xFFFF, 0xFFFF}
#else
#define HINA_SYNC_POINT_INVALID ((hina_sync_point){0xFFFF, 0xFFFF})
#endif
typedef enum hina_frame_flags
{
  HINA_FRAME_DEFAULT    = 0,
  HINA_FRAME_NO_PRESENT = 1 << 0
} hina_frame_flags;

typedef enum hina_submit_flags
{
  HINA_SUBMIT_DEFAULT       = 0,
  HINA_SUBMIT_NEEDS_ACQUIRE = 1 << 0
} hina_submit_flags;

// Frame lifecycle (main-thread only)
HINA_API hina_swapchain_image hina_frame_begin(void);

HINA_API hina_swapchain_image hina_frame_begin_ex(hina_frame_flags flags);

HINA_API void hina_frame_end(void);

// Submission within frame (main-thread only)
/**
 * @brief Submit a command buffer within the current frame.
 *
 * Queues the command buffer for execution as part of the current frame's GPU work.
 * Returns a sync point that can be used with hina_frame_wait() to declare dependencies.
 *
 * @param cmd Command buffer to submit (will be ended if still recording)
 * @return Sync point identifying this submission, or HINA_SYNC_POINT_INVALID on failure
 */
HINA_API hina_sync_point hina_frame_submit(hina_cmd* cmd);

HINA_API hina_sync_point hina_frame_submit_ex(hina_cmd* cmd, hina_submit_flags flags);

// Dependency declaration (main-thread only)
/**
 * @brief Declare a cross-queue dependency within the current frame.
 *
 * The next submission on `dst` queue will wait for `src` sync point to complete on the GPU.
 * This enables patterns like: compute shader → graphics rendering.
 *
 * @param dst  Destination queue that will wait (HINA_QUEUE_GRAPHICS, COMPUTE, or TRANSFER)
 * @param src  Sync point from a prior hina_frame_submit() call
 */
HINA_API void hina_frame_wait(hina_queue dst, hina_sync_point src);

/**
 * @brief Declare a cross-queue dependency with specific pipeline stage.
 *
 * Like hina_frame_wait(), but allows specifying which pipeline stage to wait at.
 * This can reduce stalls by allowing earlier stages to proceed before the wait.
 *
 * @param dst   Destination queue that will wait
 * @param src   Sync point from a prior hina_frame_submit() call
 * @param stage Pipeline stage mask (e.g., HINA_PIPE_STAGE_FRAGMENT_SHADER)
 */
HINA_API void hina_frame_wait_at(hina_queue dst, hina_sync_point src, hina_stage_mask stage);

// ---------------------------------------------------------------------------
// Immediate GPU Submission (Outside Frame Loop)
// ---------------------------------------------------------------------------
//
// Use these for one-shot GPU work that must complete independently of rendering:
//
//   hina_cmd* cmd = hina_cmd_begin();
//   hina_cmd_copy_buffer_to_texture(cmd, staging, texture, 0, 0, 0);
//   hina_cmd_transition_texture(cmd, texture, HINA_TEXSTATE_SHADER_READ);
//   hina_ticket ticket = hina_submit_immediate(cmd);
//   hina_wait_ticket(ticket);  // Block until GPU completes
//
// When to use:
//   - Asset loading (textures, buffers) outside the frame loop
//   - One-time setup work before rendering begins
//   - Background thread GPU uploads
//   - Compute work that doesn't need frame synchronization
//
// When NOT to use:
//   - Rendering to swapchain (use hina_frame_submit instead)
//   - Work that should synchronize with frame presentation
//
// Note: Higher-level functions like hina_make_texture(.initial_data) and
// hina_generate_mips() use this internally - prefer those when applicable.
/**
 * @brief Submit a command buffer immediately, bypassing the frame loop.
 *
 * Unlike hina_frame_submit(), this does not participate in swapchain presentation
 * or frame pacing. The work is submitted directly to the GPU and can be waited on
 * via hina_wait_ticket().
 *
 * @param cmd Command buffer to submit (will be ended if still recording)
 * @return Ticket for waiting, or 0 on failure
 */
HINA_API hina_ticket hina_submit_immediate(hina_cmd* cmd);

HINA_API hina_ticket hina_ctx_submit_immediate(hina_context* ctx, hina_cmd* cmd);

/**
 * @brief Block until the GPU completes work associated with a ticket.
 *
 * @param ticket Ticket returned by hina_submit_immediate()
 */
HINA_API void hina_wait_ticket(hina_ticket ticket);

HINA_API void hina_ctx_wait_ticket(hina_context* ctx, hina_ticket ticket);

//  Swapchain & Presentation
typedef enum
{
  HINA_PRESENT_MODE_FIFO = 0, // Vsync, always available (default)
  HINA_PRESENT_MODE_MAILBOX, // No vsync, no tearing (falls back to FIFO)
  HINA_PRESENT_MODE_IMMEDIATE, // Lowest latency, may tear (falls back to MAILBOX→FIFO)
} hina_present_mode;

typedef enum
{
  HINA_SWAPCHAIN_TRANSPARENT_BIT = HINA_FLAG_BIT(0), // Alpha compositing with desktop
  HINA_SWAPCHAIN_PREROTATE_BIT   = HINA_FLAG_BIT(1), // Use surface preTransform (power saving on Android)
} hina_swapchain_flags;

typedef enum
{
  HINA_COLOR_SPACE_SRGB_NONLINEAR = 0,
  HINA_COLOR_SPACE_DISPLAY_P3_NONLINEAR = 1000104001,
  HINA_COLOR_SPACE_EXTENDED_SRGB_LINEAR = 1000104002,
  HINA_COLOR_SPACE_DISPLAY_P3_LINEAR = 1000104003,
  HINA_COLOR_SPACE_DCI_P3_NONLINEAR = 1000104004,
  HINA_COLOR_SPACE_BT709_LINEAR = 1000104005,
  HINA_COLOR_SPACE_BT709_NONLINEAR = 1000104006,
  HINA_COLOR_SPACE_BT2020_LINEAR = 1000104007,
  HINA_COLOR_SPACE_HDR10_ST2084 = 1000104008,
  HINA_COLOR_SPACE_DOLBYVISION = 1000104009,
  HINA_COLOR_SPACE_HDR10_HLG = 1000104010,
  HINA_COLOR_SPACE_ADOBERGB_LINEAR = 1000104011,
  HINA_COLOR_SPACE_ADOBERGB_NONLINEAR = 1000104012,
  HINA_COLOR_SPACE_PASS_THROUGH = 1000104013,
  HINA_COLOR_SPACE_EXTENDED_SRGB_NONLINEAR = 1000104014
} hina_color_space;

typedef struct hina_swapchain_desc
{
  hina_swapchain_flags flags;
  hina_present_mode present_mode; // 0 = FIFO (vsync, default)
  hina_format preferred_format; // HINA_FORMAT_UNDEFINED = auto
  hina_color_space preferred_color_space; // 0 = SRGB_NONLINEAR (default)
} hina_swapchain_desc;

// Defaults: present_mode=FIFO, format/color_space=auto
HINA_API hina_swapchain_desc hina_swapchain_desc_default(void);

/**
 * @brief Configure swapchain presentation settings.
 *
 * Call before the first hina_frame_begin(), or between frames to reconfigure.
 * Changes take effect on the next swapchain recreation (resize, surface lost, etc.).
 *
 * @param desc Swapchain configuration. Use hina_swapchain_desc_default() as base.
 *
 * Example:
 * @code
 *   hina_swapchain_desc sc = hina_swapchain_desc_default();
 *   sc.present_mode = HINA_PRESENT_MODE_MAILBOX;  // Uncapped FPS, no tearing
 *   sc.flags = HINA_SWAPCHAIN_PREROTATE_BIT;      // Android power optimization
 *   hina_configure_swapchain(&sc);
 * @endcode
 *
 * @note For HDR output, set preferred_format to a 10-bit or 16-bit format
 *       (e.g., HINA_FORMAT_A2B10G10R10_UNORM) and preferred_color_space to
 *       HINA_COLOR_SPACE_HDR10_ST2084 or similar. Falls back gracefully if unsupported.
 */
HINA_API void hina_configure_swapchain(const hina_swapchain_desc* desc);

// Get swapchain prerotation angle in degrees (0, 90, 180, or 270).
// Returns 0 unless HINA_SWAPCHAIN_PREROTATE_BIT was set.
// When non-zero, apply rotation to projection matrix: proj = rotate_z(radians(angle)) * proj
HINA_API float hina_get_swapchain_prerotation(void);

/**
 * @brief Get the preferred surface format for swapchain-compatible rendering.
 *
 * Returns the format that will be used for the swapchain. This is determined by
 * querying surface capabilities when the surface is created.
 *
 * Use this to create intermediate textures (e.g., render targets, post-process buffers)
 * that need to match the swapchain format for efficient blitting or compositing.
 *
 * @return The swapchain format, or HINA_FORMAT_UNDEFINED if swapchain doesn't exist yet.
 *
 * @note On Android, returns UNDEFINED until native window is provided and swapchain created.
 *
 * Example:
 * @code
 *   hina_format fmt = hina_get_surface_format();
 *   if (fmt != HINA_FORMAT_UNDEFINED) {
 *       texture_desc.format = fmt;  // Create texture matching swapchain
 *       pipeline_desc.color_formats[0] = fmt;  // Pipeline for that texture
 *   }
 * @endcode
 */
HINA_API hina_format hina_get_surface_format(void);

/**
 * @brief Get the format of a texture.
 *
 * @param tex The texture handle
 * @return The texture's format, or HINA_FORMAT_UNDEFINED if invalid handle.
 */
HINA_API hina_format hina_get_texture_format(hina_texture tex);

/**
 * @brief Check if the surface is in a lost state.
 *
 * Returns true if the surface was lost and needs recreation via hina_recreate_surface().
 * When true, hina_frame_begin() will return an invalid swapchain image and frame
 * operations become no-ops until the surface is recreated.
 *
 * @return true if surface is lost, false if surface is valid.
 *
 * @see hina_surface_lost(), hina_recreate_surface()
 */
HINA_API bool hina_is_surface_lost(void);

/**
 * @brief Recreate Vulkan surface after it was lost.
 *
 * Call this when the platform provides a new native window after a surface loss
 * (e.g., Android app resumed, window restored). This recreates the Vulkan surface
 * and swapchain, allowing rendering to continue.
 *
 * @param native_window Platform-specific window handle (HWND, ANativeWindow*, etc.)
 * @param native_display Platform-specific display handle (NULL on Windows/Android)
 * @return true on success, false on failure.
 *
 * Android lifecycle example (SDL):
 * @code
 *   void handle_event(SDL_Event* e) {
 *       if (e->type == SDL_APP_DIDENTERFOREGROUND) {
 *           SDL_Window* window = SDL_GetWindowFromID(e->window.windowID);
 *           SDL_SysWMinfo info;
 *           SDL_VERSION(&info.version);
 *           SDL_GetWindowWMInfo(window, &info);
 *           hina_recreate_surface(info.info.android.window, NULL);
 *       } else if (e->type == SDL_APP_WILLENTERBACKGROUND) {
 *           hina_surface_lost();
 *       }
 *   }
 * @endcode
 *
 * @see hina_surface_lost(), hina_is_surface_lost()
 */
HINA_API bool hina_recreate_surface(void* native_window, void* native_display);

//  Frame Index Queries
/**
 * @brief Get the current frame index (monotonically increasing).
 *
 * This is a 64-bit counter that never wraps. Useful for:
 * - Resource versioning (e.g., validating transient bind groups)
 * - Frame timing statistics
 * - Debugging
 *
 * @return Current frame index (0 = before first hina_frame_begin).
 */
HINA_API uint64_t hina_get_frame_index(void);

/**
 * @brief Get the last completed frame index (GPU finished).
 *
 * Returns the frame index of the most recently completed GPU frame.
 * Useful for knowing when to reclaim per-frame resources.
 *
 * @return Last completed frame index, or 0 if no frames have completed.
 */
HINA_API uint64_t hina_get_completed_frame_index(void);

//  Buffers

// Memory placement - where the buffer lives (enum, not combinable)
typedef enum
{
  // GPU-optimal memory. Not intended to be mapped by CPU.
  // On UMA/ReBAR systems, VMA may still return mappable memory - this is fine,
  // but the API contract is: upload via staging or initial_data, don't assume mappability.
  // Good for: static vertex/index data, GPU-only storage buffers.
  HINA_BUFFER_GPU = 0,

  // CPU-accessible memory, persistently mapped at creation.
  // GUARANTEED COHERENT: Writes are immediately visible to GPU, no flush needed.
  // VMA picks the best available coherent memory (ReBAR if beneficial).
  // Good for: uniform buffers, dynamic vertex data - the common case.
  HINA_BUFFER_CPU,

  // CPU-accessible memory with explicit synchronization.
  // May be non-coherent: user MUST call hina_flush_buffer() after CPU writes
  // and hina_invalidate_buffer() before CPU reads after GPU writes.
  // Only use this if you need fine-grained control (e.g., mobile optimization).
  // Good for: advanced users who want to batch flushes for performance.
  HINA_BUFFER_CPU_EXPLICIT,
} hina_buffer_memory;

// Usage flags - what the buffer will be used for (combinable bits)
typedef enum
{
  HINA_BUFFER_VERTEX       = HINA_FLAG_BIT(0),  // Vertex buffer
  HINA_BUFFER_INDEX        = HINA_FLAG_BIT(1),  // Index buffer
  HINA_BUFFER_UNIFORM      = HINA_FLAG_BIT(2),  // Uniform buffer (UBO)
  HINA_BUFFER_STORAGE      = HINA_FLAG_BIT(3),  // Storage buffer (SSBO)
  HINA_BUFFER_INDIRECT     = HINA_FLAG_BIT(4),  // Indirect draw/dispatch commands
  HINA_BUFFER_TRANSFER_SRC = HINA_FLAG_BIT(5),  // Source for copy operations
  HINA_BUFFER_TRANSFER_DST = HINA_FLAG_BIT(6),  // Destination for copy operations
} hina_buffer_usage;

typedef struct hina_buffer_desc
{
  size_t size;
  const void* initial_data;      // If non-NULL, data is uploaded via staging buffer
  const char* label;             // Optional debug label (shows in RenderDoc, validation layers)
  hina_buffer_memory memory;     // Where the buffer lives (default: HINA_BUFFER_GPU)
  hina_buffer_usage usage;       // What the buffer is used for (combinable flags)
  hina_queue initial_owner;      // Queue that initially owns this buffer (default: HINA_QUEUE_GRAPHICS)
} hina_buffer_desc;

// Zero-initialize hina_buffer_desc; all fields default to 0/NULL.
HINA_API hina_buffer_desc hina_buffer_desc_default(void);

// Global API
HINA_API hina_buffer hina_make_buffer(const hina_buffer_desc* desc);

HINA_API void hina_destroy_buffer(hina_buffer buf);

// Returns persistent mapped pointer for HINA_BUFFER_CPU or HINA_BUFFER_CPU_EXPLICIT buffers.
HINA_API void* hina_mapped_buffer_ptr(hina_buffer buf);

// Flush CPU writes to make them visible to GPU.
// Required for HINA_BUFFER_CPU_EXPLICIT after CPU writes, before GPU reads.
// No-op for HINA_BUFFER_CPU (always coherent). Asserts on HINA_BUFFER_GPU.
HINA_API void hina_flush_buffer(hina_buffer buf, size_t offset, size_t size);

// Invalidate CPU cache to see GPU writes.
// Required for HINA_BUFFER_CPU_EXPLICIT before CPU reads, after GPU writes.
// No-op for HINA_BUFFER_CPU (always coherent). Asserts on HINA_BUFFER_GPU.
HINA_API void hina_invalidate_buffer(hina_buffer buf, size_t offset, size_t size);

// One-shot GPU→CPU readback. Handles staging buffer and sync internally.
// Source buffer must have HINA_BUFFER_TRANSFER_SRC usage.
HINA_API void hina_download_buffer(hina_buffer src, size_t offset, size_t size, void* dst);

HINA_API hina_ticket hina_flush_staging(void); // Flush internal staging buffers
// Context API (for functions requiring per-context frame state)
HINA_API hina_buffer hina_ctx_make_buffer(hina_context* ctx, const hina_buffer_desc* desc);

HINA_API void hina_ctx_destroy_buffer(hina_context* ctx, hina_buffer buf);

HINA_API hina_ticket hina_ctx_flush_staging(hina_context* ctx);

//  Textures & Views
typedef enum
{
  HINA_TEX_TYPE_2D,
  HINA_TEX_TYPE_2D_ARRAY,
  HINA_TEX_TYPE_CUBE,
  HINA_TEX_TYPE_3D
} hina_texture_type;

typedef enum
{
  // SAMPLED: Texture can be read in shaders via sampler. This is the most
  // common usage for textures (diffuse maps, normal maps, etc).
  HINA_TEXTURE_SAMPLED_BIT       = HINA_FLAG_BIT(0),

  // STORAGE: Texture can be written to in compute shaders (imageStore).
  // Cannot be combined with compressed formats.
  HINA_TEXTURE_STORAGE_BIT       = HINA_FLAG_BIT(1),

  // RENDER_TARGET: Texture can be used as a color or depth attachment.
  // Required for offscreen rendering, shadow maps, G-buffers, etc.
  HINA_TEXTURE_RENDER_TARGET_BIT = HINA_FLAG_BIT(2),
  // TRANSFER_SRC: Texture can be source of copy/blit operations.
  // Required for mipmap generation (each level is blit source for the next).
  HINA_TEXTURE_TRANSFER_SRC_BIT  = HINA_FLAG_BIT(3),

  // TRANSIENT: Contents don't need to survive the frame. On mobile (tile-based
  // GPUs), this allows the texture to live entirely in on-chip memory, avoiding
  // costly VRAM writes. Ideal for intermediate render targets like G-buffers
  // or MSAA color buffers that are resolved before frame end.
  HINA_TEXTURE_TRANSIENT_BIT     = HINA_FLAG_BIT(4),

  // INPUT_ATTACHMENT: Texture can be read as a tile input (subpassInput) in
  // tile passes. Required when using tile_input/tile_load in shaders. This
  // enables the VK_IMAGE_USAGE_INPUT_ATTACHMENT_BIT on the underlying image.
  // Note: Value matches VK_IMAGE_USAGE_INPUT_ATTACHMENT_BIT (0x80) for easy checking.
  HINA_TEXTURE_INPUT_ATTACHMENT_BIT = 0x80
} hina_texture_usage_flags;

typedef struct hina_texture_desc
{
  hina_texture_type type;
  hina_format format;
  uint32_t width;
  uint32_t height;
  uint32_t depth; // For HINA_TEX_TYPE_3D, otherwise must be 1
  uint16_t layers; // For arrays/cubes, ignored for 3D
  uint16_t mip_levels; // Use HINA_MIP_LEVELS_AUTO to auto-calc
  hina_sample_count samples;
  hina_texture_usage_flags usage;
  const void* initial_data; // Mip 0 data (generates mips if mip_levels > 1)
  size_t initial_stride; // Row stride in bytes (0 = tight packing)
  hina_queue initial_owner; // Queue that initially owns this texture (default: HINA_QUEUE_GRAPHICS)
  const char* label; // Optional debug label (shows in RenderDoc, validation layers)
} hina_texture_desc;

// Sentinel to auto-calculate mip count from texture dimensions.
// When used, the library will allocate full mip chain and generate mips
// after initial_data upload (if provided). Not valid with TRANSIENT usage.
#define HINA_MIP_LEVELS_AUTO ((uint16_t)0xFFFF)
// Defaults: .type = HINA_TEX_TYPE_2D, .depth = 1, .layers = 1,
//           .mip_levels = 1, .samples = HINA_SAMPLE_COUNT_1_BIT,
//           .usage = HINA_TEXTURE_SAMPLED_BIT
HINA_API hina_texture_desc hina_texture_desc_default(void);

// Global API
HINA_API hina_texture hina_make_texture(const hina_texture_desc* desc);

HINA_API void hina_destroy_texture(hina_texture tex);

HINA_API hina_ticket hina_generate_mips(hina_texture tex);

// Context API
HINA_API hina_texture hina_ctx_make_texture(hina_context* ctx, const hina_texture_desc* desc);

HINA_API void hina_ctx_destroy_texture(hina_context* ctx, hina_texture tex);

HINA_API hina_ticket hina_ctx_generate_mips(hina_context* ctx, hina_texture tex);

// ---------------------------------------------------------------------------
//  Texture Views
// ---------------------------------------------------------------------------
/**
 * @brief Texture aspect for view creation.
 */
typedef enum hina_texture_aspect
{
  HINA_TEXTURE_ASPECT_ALL,
  HINA_TEXTURE_ASPECT_DEPTH_ONLY,
  HINA_TEXTURE_ASPECT_STENCIL_ONLY,
} hina_texture_aspect;

/**
 * @brief Texture view description (separate from texture).
 *
 * Texture views provide a non-owning view over an existing texture, allowing:
 * - Access to a subset of mip levels or array layers
 * - Format reinterpretation (e.g., UNORM vs SRGB aliasing)
 *
 * Views can be used anywhere a texture is expected via bind groups.
 * Destroying a view does NOT destroy the parent texture.
 * The parent texture is passed to hina_create_texture_view().
 */
typedef struct hina_texture_view_desc
{
  hina_format format; // HINA_FORMAT_UNDEFINED = inherit from parent
  uint32_t base_mip;
  uint32_t mip_count; // 0 = all remaining
  uint32_t base_layer;
  uint32_t layer_count; // 0 = all remaining
  hina_texture_aspect aspect;
  const char* label;
} hina_texture_view_desc;

/**
 * @brief Get the default view for a texture.
 *
 * Every texture has an associated default view (full resource, inherited format).
 * The default view is owned by the texture - user never destroys it separately.
 * It becomes invalid when the parent texture is destroyed.
 */
HINA_API hina_texture_view hina_texture_get_default_view(hina_texture tex);

/**
 * @brief Create a texture view.
 *
 * Creates a view into a texture with the specified subresource range.
 * The parent texture must remain valid for the lifetime of the view.
 */
HINA_API hina_texture_view hina_create_texture_view(hina_texture tex, const hina_texture_view_desc* desc);

/**
 * @brief Destroy a texture view.
 *
 * Only user-created views can be destroyed. Default views are owned by
 * their parent texture and will be ignored.
 */
HINA_API void hina_destroy_texture_view(hina_texture_view view);

/**
 * @brief Check if a texture view is valid.
 */
HINA_API bool hina_texture_view_is_valid(hina_texture_view view);

// ---------------------------------------------------------------------------
//  Texture Download
// ---------------------------------------------------------------------------
/**
 * @brief Downloads texture data from GPU to CPU memory.
 *
 * This is a synchronous operation that:
 * 1. Uses a persistent internal staging buffer (grown as needed)
 * 2. Transitions the texture to TRANSFER_SRC layout
 * 3. Copies the specified mip/layer to the staging buffer
 * 4. Waits for GPU completion
 * 5. Copies data to the user-provided destination
 *
 * The texture must have been created with HINA_TEXTURE_TRANSFER_SRC_BIT usage.
 * Asserts on invalid parameters (null dst, invalid handle, out-of-bounds mip/layer, insufficient dst_size).
 *
 * @param src       Source texture to download from
 * @param mip       Mip level to download (0 = base level)
 * @param layer     Array layer to download (0 = first layer)
 * @param dst       Destination buffer (must be large enough for the mip level)
 * @param dst_size  Size of destination buffer in bytes
 */
HINA_API void hina_download_texture(hina_texture src, uint32_t mip, uint32_t layer, void* dst, size_t dst_size);

HINA_API void hina_ctx_download_texture(hina_context* ctx, hina_texture src, uint32_t mip, uint32_t layer, void* dst,
                                        size_t dst_size);

HINA_API void hina_download_texture_3d(hina_texture src, uint32_t mip, uint32_t z_offset, uint32_t depth, void* dst,
                                       size_t dst_size);

HINA_API void hina_ctx_download_texture_3d(hina_context* ctx, hina_texture src, uint32_t mip, uint32_t z_offset,
                                           uint32_t depth, void* dst, size_t dst_size);

/**
 * @brief Calculates the size in bytes needed to download a texture mip level.
 * For 3D textures, this includes the full mip depth.
 * Asserts on invalid texture handle, out-of-bounds mip level, or unsupported format.
 *
 * @param tex   The texture to query
 * @param mip   The mip level to query
 * @return Size in bytes
 */
HINA_API size_t hina_texture_mip_size(hina_texture tex, uint32_t mip);

//  Samplers
typedef enum
{
  HINA_FILTER_NEAREST,
  HINA_FILTER_LINEAR
} hina_filter;

typedef enum
{
  HINA_ADDRESS_REPEAT,
  HINA_ADDRESS_MIRRORED_REPEAT,
  HINA_ADDRESS_CLAMP_TO_EDGE,
  HINA_ADDRESS_CLAMP_TO_BORDER
} hina_address_mode;

typedef enum
{
  // ANISOTROPY_ENABLE: Enable anisotropic filtering. Improves texture quality
  // at oblique viewing angles (e.g., floors, roads). Set max_anisotropy to
  // control quality/performance tradeoff (typically 4-16). Requires hardware
  // support (check device caps).
  HINA_SAMPLER_ANISOTROPY_ENABLE_BIT = HINA_FLAG_BIT(0),
  // COMPARE_ENABLE: Enable depth comparison for shadow mapping. The sampler
  // returns 0.0 or 1.0 based on comparing sampled depth against a reference
  // value (set via compare_op). Used with samplerShadow in GLSL.
  HINA_SAMPLER_COMPARE_ENABLE_BIT = HINA_FLAG_BIT(1)
} hina_sampler_flags;

typedef struct hina_sampler_desc
{
  hina_filter min_filter;
  hina_filter mag_filter;
  hina_filter mipmap_filter;
  hina_address_mode address_u;
  hina_address_mode address_v;
  hina_address_mode address_w;
  float mip_lod_bias;
  float min_lod;
  float max_lod;
  hina_sampler_flags flags;
  float max_anisotropy;
  hina_compare_op compare_op;
  const char* label; // Optional debug label (shows in RenderDoc, validation layers)
} hina_sampler_desc;

// Defaults: .min_filter = HINA_FILTER_LINEAR, .mag_filter = HINA_FILTER_LINEAR,
//           .mipmap_filter = HINA_FILTER_LINEAR,
//           .address_u/v/w = HINA_ADDRESS_REPEAT, .max_lod = 1000.0f
HINA_API hina_sampler_desc hina_sampler_desc_default(void);

HINA_API hina_sampler hina_make_sampler(const hina_sampler_desc* desc);

HINA_API void hina_destroy_sampler(hina_sampler samp);

HINA_API void hina_ctx_destroy_sampler(hina_context* ctx, hina_sampler samp);

//  Pass Layout
// ---------------------------------------------------------------------------
/**
 * @brief Create a pass layout from attachment formats.
 *
 * @param color_formats Array of color attachment formats
 * @param color_count   Number of color attachments (0-4)
 * @param depth_format  Depth/stencil format (HINA_FORMAT_UNDEFINED if none)
 * @param samples       Sample count (1, 2, 4, 8)
 * @return Pass layout key
 */
HINA_API hina_pass_layout hina_pass_layout_make(const hina_format* color_formats, uint32_t color_count,
                                                hina_format depth_format, uint32_t samples);

/**
 * @brief Get the color format at the specified index from a pass layout.
 */
HINA_API hina_format hina_pass_layout_color_format(hina_pass_layout layout, uint32_t index);

/**
 * @brief Get the depth/stencil format from a pass layout.
 */
HINA_API hina_format hina_pass_layout_depth_format(hina_pass_layout layout);

/**
 * @brief Get the sample count from a pass layout.
 */
HINA_API uint32_t hina_pass_layout_samples(hina_pass_layout layout);

/**
 * @brief Get the number of color attachments in a pass layout.
 */
HINA_API uint32_t hina_pass_layout_color_count(hina_pass_layout layout);

//  Bind Groups (WebGPU-style descriptor management)
//
// Bind groups provide explicit, immutable descriptor sets following WebGPU conventions.
// This is the recommended production API for resource binding.
//
// Two paths are available:
// 1. Production path: Create layouts explicitly, then bind groups against them
// 2. Demo path: Use slot-based hina_cmd_bind_buffer/image (auto-creates descriptors)
//    Slot-based bindings are only supported for raw SPIR-V pipelines.
//
// Usage:
//   hina_bind_group_layout layout = hina_create_bind_group_layout_from_hsl_module(module, 0);
//   hina_bind_group group = hina_create_bind_group(&(hina_bind_group_desc){
//       .layout = layout,
//       .entries = (hina_bind_group_entry[]){
//           { .binding = 0, .type = HINA_DESC_TYPE_UNIFORM_BUFFER,
//             .buffer = { ubo, 0, sizeof(UBO) } },
//           { .binding = 1, .type = HINA_DESC_TYPE_COMBINED_IMAGE_SAMPLER,
//             .combined = { texture_view, sampler } },
//       },
//       .entry_count = 2,
//   });
//   hina_cmd_bind_group(cmd, 0, group);
//
/**
 * @brief Descriptor type enum for bind group entries.
 * Maps to Vulkan descriptor types. Uses Vulkan IMAGE terminology.
 */
typedef enum hina_desc_type
{
  HINA_DESC_TYPE_NONE = 0, // Invalid/uninitialized
  HINA_DESC_TYPE_UNIFORM_BUFFER, // VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER
  HINA_DESC_TYPE_STORAGE_BUFFER, // VK_DESCRIPTOR_TYPE_STORAGE_BUFFER
  HINA_DESC_TYPE_SAMPLED_IMAGE, // VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE
  HINA_DESC_TYPE_STORAGE_IMAGE, // VK_DESCRIPTOR_TYPE_STORAGE_IMAGE
  HINA_DESC_TYPE_SAMPLER, // VK_DESCRIPTOR_TYPE_SAMPLER
  HINA_DESC_TYPE_COMBINED_IMAGE_SAMPLER, // VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER
  HINA_DESC_TYPE_INPUT_ATTACHMENT // VK_DESCRIPTOR_TYPE_INPUT_ATTACHMENT (tile pass)
} hina_desc_type;

/**
 * @brief Binding flags for bind group layout entries.
 */
typedef enum hina_binding_flags
{
  HINA_BINDING_FLAG_NONE = 0,
  HINA_BINDING_FLAG_DYNAMIC_OFFSET = 1 << 0, // Allow dynamic buffer offsets at bind time
} hina_binding_flags;

/**
 * @brief Bind group layout entry - describes a single binding slot.
 */
typedef struct hina_bind_group_layout_entry
{
  uint32_t binding; // Binding index within the set
  hina_desc_type type; // Type of resource bound here
  uint32_t stage_flags; // HINA_STAGE_VERTEX | HINA_STAGE_FRAGMENT etc.
  uint32_t count; // Array size (1 for non-arrays)
  hina_binding_flags flags; // Optional flags (e.g., dynamic offset)
} hina_bind_group_layout_entry;

/**
 * @brief Descriptor for creating a bind group layout.
 */
typedef struct hina_bind_group_layout_desc
{
  const hina_bind_group_layout_entry* entries;
  uint32_t entry_count; // Max 12 entries per layout
  const char* label; // Optional debug label
} hina_bind_group_layout_desc;

/**
 * @brief Bind group entry - binds a resource to a specific binding slot.
 */
typedef struct hina_bind_group_entry
{
  uint32_t binding; // Binding index (must match layout)
  hina_desc_type type; // Type (must match layout)
  union
  {
    struct
    {
      hina_buffer buffer;
      uint64_t offset;
      uint64_t size;
    } buffer;

    hina_texture_view view; // For SAMPLED_IMAGE, STORAGE_IMAGE
    hina_sampler sampler; // For SAMPLER
    struct
    {
      hina_texture_view view;
      hina_sampler sampler;
    } combined;
  };
} hina_bind_group_entry;

/**
 * @brief Descriptor for creating a bind group.
 */
typedef struct hina_bind_group_desc
{
  // 8-byte aligned fields first
  const hina_bind_group_entry* entries;
  const char* label; // Optional debug label
  // 4-byte fields grouped together
  hina_bind_group_layout layout; // Layout this group conforms to
  uint32_t entry_count; // Must match layout's entry count
} hina_bind_group_desc;

// ---------------------------------------------------------------------------
// Bind Group Layout Creation
// ---------------------------------------------------------------------------
// Forward declaration for HSL module (defined in Shader Compilation section)
struct hina_hsl_module;
/**
 * @brief Create a bind group layout from explicit entries.
 * Use this when you want full control over the layout.
 */
HINA_API hina_bind_group_layout hina_create_bind_group_layout(const hina_bind_group_layout_desc* desc);

/**
 * @brief Create a bind group layout from an HSL module.
 * Extracts bindings for the specified set from the module's SPIR-V reflection.
 * Uses the fragment shader's bindings (which typically has all bindings visible).
 *
 * @param module Compiled HSL module from hslc_compile_hsl()
 * @param set_index Which descriptor set (0-3) to create a layout for
 */
HINA_API hina_bind_group_layout hina_create_bind_group_layout_from_hsl_module(
  const struct hina_hsl_module* module, uint32_t set_index);

/**
 * @brief Destroy a bind group layout.
 * Safe to call with invalid handle.
 */
HINA_API void hina_destroy_bind_group_layout(hina_bind_group_layout layout);

// Context-aware version (for deferred destruction)
HINA_API void hina_ctx_destroy_bind_group_layout(hina_context* ctx, hina_bind_group_layout layout);

// ---------------------------------------------------------------------------
// Bind Group Creation
// ---------------------------------------------------------------------------
/**
 * @brief Create a persistent bind group.
 * The bind group is allocated from a persistent pool and must be explicitly destroyed.
 * Resources referenced by the bind group must remain valid for its lifetime.
 */
HINA_API hina_bind_group hina_create_bind_group(const hina_bind_group_desc* desc);

/**
 * @brief Destroy a persistent bind group.
 * Safe to call with invalid handle. Do NOT call on temporary bind groups.
 */
HINA_API void hina_destroy_bind_group(hina_bind_group group);

// Context-aware versions
HINA_API hina_bind_group hina_ctx_create_bind_group(hina_context* ctx, const hina_bind_group_desc* desc);

HINA_API void hina_ctx_destroy_bind_group(hina_context* ctx, hina_bind_group group);

// ---------------------------------------------------------------------------
// Bind Group Usage Guide: Persistent vs Transient
// ---------------------------------------------------------------------------
//
// HinaVK provides two bind group types optimized for different use cases:
//
// PERSISTENT BIND GROUPS (hina_create_bind_group / hina_cmd_bind_group)
// ---------------------------------------------------------------------
// - Allocated from a managed pool, returned as a handle (hina_bind_group)
// - Must be explicitly destroyed with hina_destroy_bind_group()
// - Ideal for: materials, global uniforms, textures that persist across frames
// - Resources must remain valid for the bind group's lifetime
// - Deferred destruction: safe to destroy while GPU may still be using it
//
// TRANSIENT BIND GROUPS (hina_alloc_transient_bind_group / hina_cmd_bind_transient_group)
// --------------------------------------------------------------------------------------
// - Allocated from per-frame linear allocator, returned by value
// - Automatically freed at frame end - no manual destruction
// - Ideal for: per-draw transforms, ImGui textures, any per-frame data
// - O(1) allocation, no locking, extremely fast
// - ONLY valid for the frame in which allocated (debug builds catch violations)
//
// BINDING PRECEDENCE (Edge Cases)
// -------------------------------
// When both transient and persistent are bound to the same set, the MOST
// RECENTLY BOUND wins:
//
//   hina_cmd_bind_group(cmd, 0, persistent);        // Set 0 = persistent
//   hina_cmd_bind_transient_group(cmd, 0, trans);   // Set 0 = transient (wins)
//   hina_cmd_draw(...);                             // Uses transient
//
//   hina_cmd_bind_transient_group(cmd, 0, trans);   // Set 0 = transient
//   hina_cmd_bind_group(cmd, 0, persistent);        // Set 0 = persistent (wins)
//   hina_cmd_draw(...);                             // Uses persistent
//
// This allows mixing patterns safely - just bind what you need before each draw.
//
// COMMON PATTERNS
// ---------------
// Pattern 1: Global + Per-Draw
//   hina_cmd_bind_group(cmd, 0, global_uniforms);   // Set 0: persistent globals
//   for (object : objects) {
//     auto tbg = hina_alloc_transient_bind_group(per_object_layout);
//     hina_transient_write_buffer(&tbg, 0, ...);
//     hina_cmd_bind_transient_group(cmd, 1, tbg);   // Set 1: transient per-object
//     hina_cmd_draw(...);
//   }
//
// Pattern 2: Material System
//   hina_cmd_bind_group(cmd, 0, scene_uniforms);    // Persistent scene data
//   hina_cmd_bind_group(cmd, 1, material->group);   // Persistent material
//   hina_cmd_draw(...);
//
// Pattern 3: ImGui / Debug UI (transient textures)
//   auto tbg = hina_alloc_transient_bind_group(imgui_layout);
//   hina_transient_write_combined_image(&tbg, 0, font_view, sampler);
//   hina_cmd_bind_transient_group(cmd, 0, tbg);
//   // Previous persistent at set 0 is overridden for this draw only
//

// ---------------------------------------------------------------------------
// Bind Group Validation Settings
// ---------------------------------------------------------------------------
/**
 * @brief Validation level enum for bind group binding.
 * Controls how mismatches between bind groups and pipeline reflection are handled.
 */
typedef enum hina_validation_level
{
  HINA_VALIDATION_NONE = 0, ///< No validation (release builds, zero overhead)
  HINA_VALIDATION_WARN = 1, ///< Log warnings but continue execution
  HINA_VALIDATION_ERROR = 2, ///< Log errors and skip draw if mismatch
  HINA_VALIDATION_ASSERT = 3, ///< Assert on mismatch (dev builds)
} hina_validation_level;

/**
 * @brief Set the bind group validation level.
 * Controls runtime validation of bind groups against HSL pipeline reflection.
 *
 * Validation checks include:
 * - Set index is valid for the pipeline
 * - Binding types match between group and pipeline
 * - All required bindings are present
 *
 * @note Only affects HSL pipelines with reflection data.
 *       Raw SPIRV pipelines skip validation automatically.
 * @note In release builds, validation is compiled out by default.
 *       Use HINA_ENABLE_RELEASE_VALIDATION to enable.
 *
 * @param level The desired validation level
 */
HINA_API void hina_set_validation_level(hina_validation_level level);

/**
 * @brief Get the current bind group validation level.
 * @return The current validation level
 */
HINA_API hina_validation_level hina_get_validation_level(void);

// ---------------------------------------------------------------------------
// Command Recording - Bind Groups
// ---------------------------------------------------------------------------
/**
 * @brief Bind a persistent bind group to a descriptor set slot.
 * Replaces any slot-based bindings for this set.
 *
 * @note Overrides any transient bind group at the same set for subsequent draws.
 *       The most recently bound (transient or persistent) wins.
 *       See "Bind Group Usage Guide" section for details and common patterns.
 *
 * @param cmd Command buffer
 * @param set Descriptor set index (0-3)
 * @param group The bind group to bind
 */
HINA_API void hina_cmd_bind_group(hina_cmd* cmd, uint32_t set, hina_bind_group group);

/**
 * @brief Bind a bind group with dynamic buffer offsets.
 * Use for UBOs/SSBOs with HINA_BINDING_FLAG_DYNAMIC_OFFSET.
 *
 * @param offsets Array of byte offsets for dynamic bindings
 * @param offset_count Number of offsets (must match layout's dynamic binding count)
 */
HINA_API void hina_cmd_bind_group_with_offsets(hina_cmd* cmd, uint32_t set, hina_bind_group group,
                                               const uint32_t* offsets, uint32_t offset_count);

// ---------------------------------------------------------------------------
// Transient Bind Groups (Per-Frame, Lock-Free)
// ---------------------------------------------------------------------------
//
// Transient bind groups are allocated from a per-frame linear allocator and
// automatically freed at frame end. They are designed for high-frequency,
// per-draw data that changes every frame (e.g., per-object transforms).
//
// Key differences from persistent bind groups:
// - VALUE TYPE: No handle overhead, returned by value
// - LINEAR ALLOCATION: O(1) bump allocator, no locking
// - FRAME SCOPED: Automatically reclaimed on hina_frame_begin()
// - NO DESTRUCTION: No hina_destroy_* needed (or possible)
//
// Usage pattern:
//   hina_transient_bind_group tbg = hina_alloc_transient_bind_group(layout);
//   hina_transient_write_buffer(&tbg, 0, ubo, offset, size);
//   hina_transient_write_combined_image(&tbg, 1, texture_view, sampler);
//   hina_cmd_bind_transient_group(cmd, 0, tbg);
//
// IMPORTANT: Transient bind groups are only valid for the frame in which they
// were allocated. Using a transient bind group after hina_frame_end() is
// undefined behavior (caught in debug builds via frame_index validation).
//
/**
 * @brief Transient bind group - value type, no handle overhead.
 *
 * Contains internal VkDescriptorSet and frame validation data.
 * Valid only for the frame in which it was allocated.
 */
typedef struct hina_transient_bind_group
{
  struct
  {
    void* set; // VkDescriptorSet (8B, cast to void* for header compat)
    uint64_t frame_index; // Frame in which this was allocated (for debug validation)
  } internal;
} hina_transient_bind_group;

/**
 * @brief Allocate a transient bind group from the per-frame linear allocator.
 *
 * This is an O(1) bump allocation with no locking. The descriptor set is
 * automatically reclaimed when the frame slot is reused (after MAX_FRAMES_IN_FLIGHT).
 *
 * @param layout The bind group layout this group conforms to
 * @return Transient bind group, or zeroed struct on failure (check .internal.set != NULL)
 */
HINA_API hina_transient_bind_group hina_alloc_transient_bind_group(hina_bind_group_layout layout);

HINA_API hina_transient_bind_group hina_ctx_alloc_transient_bind_group(hina_context* ctx, hina_bind_group_layout layout);

/**
 * @brief Write a buffer binding to a transient bind group.
 *
 * @param tbg    Pointer to transient bind group
 * @param binding Binding index (must match layout)
 * @param type   Descriptor type (UNIFORM_BUFFER or STORAGE_BUFFER)
 * @param buf    Buffer to bind
 * @param offset Offset into buffer (bytes)
 * @param size   Size of binding (bytes), or 0 for VK_WHOLE_SIZE
 */
HINA_API void hina_transient_write_buffer(hina_transient_bind_group* tbg, uint32_t binding, hina_desc_type type,
                                          hina_buffer buf, uint64_t offset, uint64_t size);

/**
 * @brief Write a combined image sampler to a transient bind group.
 *
 * @param tbg     Pointer to transient bind group
 * @param binding Binding index (must match layout)
 * @param view    Texture view to bind
 * @param sampler Sampler to bind
 */
HINA_API void hina_transient_write_combined_image(hina_transient_bind_group* tbg, uint32_t binding,
                                                  hina_texture_view view, hina_sampler sampler);

/**
 * @brief Write a storage image to a transient bind group.
 *
 * @param tbg     Pointer to transient bind group
 * @param binding Binding index (must match layout)
 * @param view    Texture view to bind (must be STORAGE compatible)
 */
HINA_API void hina_transient_write_storage_image(hina_transient_bind_group* tbg, uint32_t binding,
                                                 hina_texture_view view);

/**
 * @brief Write an input attachment to a transient bind group.
 *
 * @param tbg     Pointer to transient bind group
 * @param binding Binding index (must match layout)
 * @param view    Texture view to bind (must be INPUT_ATTACHMENT compatible)
 */
HINA_API void hina_transient_write_input_attachment(hina_transient_bind_group* tbg, uint32_t binding,
                                                    hina_texture_view view);

/**
 * @brief Bind a transient bind group to a descriptor set slot.
 *
 * In debug builds, validates that the transient bind group was allocated
 * in the current frame (catches stale usage across frame boundaries).
 *
 * @note Overrides any persistent bind group at the same set for subsequent draws.
 *       The most recently bound (transient or persistent) wins.
 *       See "Bind Group Usage Guide" section for details and common patterns.
 *
 * @param cmd Command buffer
 * @param set Descriptor set index (0-3)
 * @param tbg Transient bind group (passed by value, it's only 16 bytes)
 */
HINA_API void hina_cmd_bind_transient_group(hina_cmd* cmd, uint32_t set, hina_transient_bind_group tbg);

//  HSL Compiler Module (hslc)
//
// The shader module is a standalone compilation library that can be used
// independently of the Vulkan rendering module. It handles:
// Threading: Shader compilation is not thread-safe. Call hslc_compile* from one
// thread at a time.
// - HSL (Hina Shader Language) preprocessing
// - GLSL to SPIR-V compilation via glslang
// - SPIR-V reflection via spirv-reflect
//
// Initialize with hslc_init() before use, shutdown with hslc_shutdown().
// The Vulkan module (hina_init) does NOT automatically initialize the shader module.
//
// Define HINA_NO_SHADER_COMPILER to exclude the shader compilation module.
// This removes dependencies on glslang and SPIRV-Tools, but you can still:
// - Load pre-compiled SPIR-V with hina_make_shader()
// - Load serialized HSL modules with hina_hsl_module_deserialize()
//
#ifndef HINA_NO_SHADER_COMPILER
// ---------------------------------------------------------------------------
// Shader Module Logging
// ---------------------------------------------------------------------------
/**
 * @brief Logging callback for the shader module.
 * @param level   Log level (HINA_LOG_INFO, HINA_LOG_WARN, HINA_LOG_ERROR)
 * @param message The log message (null-terminated)
 * @param user_data User data passed to hslc_init()
 */
typedef void (*hslc_log_fn)(hina_log_level level, const char* message, void* user_data);

/**
 * @brief Configuration for shader module initialization.
 */
typedef struct hslc_config
{
  hslc_log_fn log_fn; // Optional logging callback (NULL = no logging)
  void* log_user_data; // User data passed to log_fn
} hslc_config;

/**
 * @brief Initialize the shader compilation module.
 * Must be called before any shader compilation functions.
 *
 * @param config Optional configuration (NULL = no logging)
 * @return true on success, false on failure
 */
HINA_API bool hslc_init(const hslc_config* config);

/**
 * @brief Shutdown the shader compilation module.
 * Frees internal resources (glslang). Safe to call multiple times.
 */
HINA_API void hslc_shutdown(void);

/**
 * @brief Check if shader module is initialized.
 */
HINA_API bool hslc_is_initialized(void);
#endif // HINA_NO_SHADER_COMPILER
// ---------------------------------------------------------------------------
// Reflection Data Types
// ---------------------------------------------------------------------------
/**
 * @brief Reflected specialization constant info from SPIR-V.
 */
typedef struct hina_reflected_spec_constant
{
  uint32_t constant_id; // The constant_id from layout(constant_id = N)
  uint32_t size; // Size in bytes (4 for 32-bit types, 8 for 64-bit)
  const char* name; // Name of the constant (may be NULL if stripped)
} hina_reflected_spec_constant;

/**
 * @brief Reflected descriptor binding info from SPIR-V (public API).
 */
typedef struct hina_hsl_binding
{
  uint32_t set; // Descriptor set number
  uint32_t binding; // Binding number within set
  uint32_t descriptor_type; // VkDescriptorType equivalent
  uint32_t count; // Array size (1 for non-arrays, 0 for runtime arrays)
  uint32_t stage_flags; // Which shader stages access this (VkShaderStageFlags)
  const char* name; // Variable name from SPIR-V (may be NULL)
} hina_hsl_binding;

/**
 * @brief Reflected vertex input attribute from SPIR-V.
 * Populated from vertex shader input variables.
 */
typedef struct hina_reflected_vertex_input
{
  uint32_t location; // Vertex attribute location
  hina_format format; // Inferred Vulkan format from SPIR-V type
  const char* name; // Variable name (e.g., "a_position")
} hina_reflected_vertex_input;

/**
 * @brief Reflected push constant block info from SPIR-V.
 */
typedef struct hina_reflected_push_constant
{
  uint32_t offset; // Offset in bytes
  uint32_t size; // Size in bytes
  uint32_t stage_flags; // Which stages use this block (VkShaderStageFlags)
} hina_reflected_push_constant;

// ---------------------------------------------------------------------------
// Shader Stage Data
// ---------------------------------------------------------------------------
/**
 * @brief Per-stage compiled shader data with reflection.
 */
typedef struct hina_shader_stage_data
{
  // SPIR-V bytecode
  const void* spirv_data;
  size_t spirv_size;
  // Reflected data
  hina_reflected_spec_constant* spec_constants;
  uint32_t spec_constant_count;
  hina_hsl_binding* bindings;
  uint32_t binding_count;
  // Compute shader workgroup size (0 = unknown/spec-constant-controlled)
  uint16_t local_size_x;
  uint16_t local_size_y;
  uint16_t local_size_z;
} hina_shader_stage_data;

// ---------------------------------------------------------------------------
// HSL Module (Compiled Shader Unit)
// ---------------------------------------------------------------------------
/**
 * @brief Compiled HSL shader module.
 *
 * A reusable compilation unit containing SPIR-V and reflection data for either:
 * - Graphics (vertex + fragment, optional tessellation/geometry), or
 * - Compute (single compute stage)
 *
 * Created with hslc_compile_hsl(), freed with hslc_hsl_module_free().
 */
typedef struct hina_hsl_module
{
  // Shader stages (only populated stages have non-NULL spirv)
  hina_shader_stage_data vs; // Vertex shader
  hina_shader_stage_data tcs; // Tessellation control shader
  hina_shader_stage_data tes; // Tessellation evaluation shader
  hina_shader_stage_data gs; // Geometry shader
  hina_shader_stage_data fs; // Fragment shader
  hina_shader_stage_data cs; // Compute shader
  // Consider using a union for graphics vs compute modules, or separate types.
  // Vertex inputs (graphics modules only)
  hina_reflected_vertex_input* vertex_inputs;
  uint32_t vertex_input_count;
  hina_reflected_push_constant* push_constants;
  uint32_t push_constant_count;
  // Source filename for debugging/error messages
  const char* source_name;
  // Internal: persistent allocation backing deserialized modules (NULL for compiled modules)
  void* _module_alloc;
} hina_hsl_module;

// ---------------------------------------------------------------------------
// HSL Shader Module Cache
// ---------------------------------------------------------------------------
/**
 * @brief Cache for VkShaderModules created from HSL modules.
 *
 * When passed to hina_make_pipeline_from_module(), stores the VkShaderModules
 * so subsequent pipeline creations reuse them instead of recreating.
 *
 * Lifecycle:
 *   hina_hsl_cache cache = {0};  // Zero-init
 *   hina_pipeline p1 = hina_make_pipeline_from_module(mod, &desc1, &cache);  // Creates modules
 *   hina_pipeline p2 = hina_make_pipeline_from_module(mod, &desc2, &cache);  // Reuses modules
 *   hina_destroy_hsl_cache(&cache);  // Destroys VkShaderModules
 *
 * @note If using multiple HSL modules, use separate caches for each module.
 */
typedef struct hina_hsl_cache
{
  void* vs; // VkShaderModule for vertex stage (NULL until first use)
  void* tcs; // VkShaderModule for tessellation control
  void* tes; // VkShaderModule for tessellation evaluation
  void* gs; // VkShaderModule for geometry
  void* fs; // VkShaderModule for fragment
  void* cs; // VkShaderModule for compute
} hina_hsl_cache;

/**
 * @brief Destroy cached VkShaderModules in an HSL cache.
 * @param cache Cache to clean up (NULL is safe, fields are zeroed after)
 */
HINA_API void hina_destroy_hsl_cache(hina_hsl_cache* cache);
#ifndef HINA_NO_SHADER_COMPILER
/**
 * @brief Compile HSL source file into a reusable module.
 *
 * Compiles HSL to SPIR-V and reflects shader metadata for validation.
 *
 * @param filepath Path to .hina_sl file
 * @param out_error Optional error message (free with hslc_free_log)
 * @return Module pointer on success, NULL on failure
 */
HINA_API hina_hsl_module* hslc_compile_hsl(const char* filepath, char** out_error);

/**
 * @brief Compile HSL from source string (not file).
 *
 * @param source HSL source code
 * @param source_name Name for error messages (e.g., "inline_shader")
 * @param out_error Optional error message
 * @return Module pointer on success, NULL on failure
 */
HINA_API hina_hsl_module* hslc_compile_hsl_source(const char* source, const char* source_name, char** out_error);

/**
 * @brief Free a compiled HSL module and all its data.
 */
HINA_API void hslc_hsl_module_free(hina_hsl_module* module);

// ---------------------------------------------------------------------------
// HSL Module Serialization (for offline compilation workflows)
// ---------------------------------------------------------------------------
/**
 * @brief Serialize an HSL module to a binary buffer.
 *
 * Use this to save compiled modules for later loading without recompilation.
 * The serialization function is in hina_shader.c (build-time tool).
 *
 * Example workflow:
 *   // Build time (with hina_shader.c linked):
 *   hina_hsl_module* module = hslc_compile_hsl("shader.hina_sl", NULL);
 *   void* data; size_t size;
 *   hslc_hsl_module_serialize(module, &data, &size);
 *   // Write data to file using your platform's file API
 *   my_write_file("shader.hina_mod", data, size);
 *   hslc_hsl_module_free_serialized(data);
 *   hslc_hsl_module_free(module);
 *
 * @param module The compiled module to serialize
 * @param out_data Output pointer to allocated buffer (free with hslc_hsl_module_free_serialized)
 * @param out_size Output size of the buffer in bytes
 * @return true on success, false on failure
 */
HINA_API bool hslc_hsl_module_serialize(const hina_hsl_module* module, void** out_data, size_t* out_size);

/**
 * @brief Free a buffer allocated by hslc_hsl_module_serialize.
 *
 * On Windows, memory must be freed by the same module that allocated it to avoid
 * CRT heap mismatches. Always use this function to free serialized data.
 *
 * @param data Pointer returned by hslc_hsl_module_serialize
 */
HINA_API void hslc_hsl_module_free_serialized(void* data);
#endif // HINA_NO_SHADER_COMPILER
// ---------------------------------------------------------------------------
// HSL Module Deserialization (runtime, no compiler needed)
// ---------------------------------------------------------------------------
/**
 * @brief Deserialize an HSL module from a binary buffer.
 *
 * Uses a single persistent allocation sized to the serialized data. The module
 * and all its data live in that allocation and are reclaimed when you call
 * hina_hsl_module_free_deserialized().
 *
 * Example workflow:
 *   // Runtime (hina_vk.c only, no shader compiler needed):
 *   void* data; size_t size;
 *   my_read_file("shader.hina_mod", &data, &size);  // Your platform file API
 *   hina_hsl_module* module = hina_hsl_module_deserialize(data, size);
 *   my_free(data);  // Original buffer can be freed after deserialize
 *   hina_pipeline pip = hina_make_pipeline_from_module(module, &desc, NULL);
 *   hina_hsl_module_free_deserialized(module);  // Reclaims module allocation
 *
 * @param data Pointer to serialized data (from hslc_hsl_module_serialize)
 * @param size Size of the data in bytes
 * @return Module pointer on success (free with hina_hsl_module_free_deserialized), NULL on failure
 */
HINA_API hina_hsl_module* hina_hsl_module_deserialize(const void* data, size_t size);

/**
 * @brief Free an HSL module that was created by hina_hsl_module_deserialize.
 *
 * Releases the allocation used by the module, reclaiming all memory.
 * The module pointer becomes invalid after this call.
 *
 * Use hslc_hsl_module_free() for modules from hslc_compile_hsl*().
 * Use hina_hsl_module_free_deserialized() for modules from hina_hsl_module_deserialize().
 *
 * @param module Module returned by hina_hsl_module_deserialize
 */
HINA_API void hina_hsl_module_free_deserialized(hina_hsl_module* module);

/**
 * @brief Shader stage enumeration.
 * Covers all Vulkan shader stages including modern extensions.
 */
typedef enum
{
  HINA_SHADER_STAGE_VERTEX,
  HINA_SHADER_STAGE_TESS_CONTROL,
  HINA_SHADER_STAGE_TESS_EVAL,
  HINA_SHADER_STAGE_GEOMETRY,
  HINA_SHADER_STAGE_FRAGMENT,
  HINA_SHADER_STAGE_COMPUTE,
  // Modern stages (require extensions)
  HINA_SHADER_STAGE_TASK,
  HINA_SHADER_STAGE_MESH,
  HINA_SHADER_STAGE_RAYGEN,
  HINA_SHADER_STAGE_ANY_HIT,
  HINA_SHADER_STAGE_CLOSEST_HIT,
  HINA_SHADER_STAGE_MISS,
  HINA_SHADER_STAGE_INTERSECTION,
  HINA_SHADER_STAGE_CALLABLE
} hina_shader_stage;

/**
 * @brief Compile-time define for shader preprocessing.
 * Injected as #define name value (or #define name 1 if value is NULL).
 * @note Only used when HINA_NO_SHADER_COMPILER is not defined.
 */
typedef struct hslc_compile_define
{
  const char* name; // Define name (e.g., "ENABLE_FOG")
  const char* value; // Define value (e.g., "1"), or NULL for "1"
} hslc_compile_define;

/**
 * @brief Callback for custom include loading.
 * Returns heap-allocated source string that HinaVK will free.
 * Return NULL on failure.
 * @note Only used when HINA_NO_SHADER_COMPILER is not defined.
 */
typedef char* (*hslc_load_include_fn)(const char* path, const char* including_file, void* user_data);
#ifndef HINA_NO_SHADER_COMPILER
/**
 * @brief Full shader compilation descriptor.
 * Supports include handling, preprocessor defines, and HSL injection.
 */
typedef struct hslc_compile_desc
{
  const char* filename; // Main file path (for errors + relative includes)
  const char* source; // Optional: if non-null, use this instead of reading filename
  hina_shader_stage stage;
  const hslc_compile_define* defines;
  uint32_t define_count;
  hslc_load_include_fn load_include_fn; // May be NULL for default file loading
  void* user_data; // User data for load_include_fn
} hslc_compile_desc;

/**
 * @brief Module compilation descriptor for extended control.
 *
 * Use with hslc_compile_hsl_module_ex() when you need custom include loading
 * (e.g., from asset archives, virtual filesystems, or memory).
 */
typedef struct hslc_hsl_module_desc
{
  const char* source;                   // HSL source code (required)
  const char* source_name;              // Optional name for diagnostics (e.g., "shader.hina_sl"); NULL => "<inline>"
  hslc_load_include_fn load_include_fn; // Custom include loader, or NULL for default file loading
  void* user_data;                      // User data passed to load_include_fn
} hslc_hsl_module_desc;

/**
 * @brief Compile HSL source with extended options.
 *
 * Unlike hslc_compile_hsl_source(), this function allows custom include loading
 * for use cases where includes can't be resolved from the filesystem
 * (virtual filesystems, asset archives, in-memory shaders).
 *
 * @param desc Module compilation descriptor
 * @param out_error Optional error message (free with hslc_free_log)
 * @return Module pointer on success, NULL on failure
 */
HINA_API hina_hsl_module* hslc_compile_hsl_module_ex(const hslc_hsl_module_desc* desc, char** out_error);

/**
 * @brief Compiled shader output from hslc_compile.
 * Contains SPIR-V bytecode and reflected specialization constants.
 */
typedef struct hslc_shader_desc
{
  const void* spirv_data; // SPIR-V bytecode (free with hslc_free_spirv)
  size_t spirv_size; // Size in bytes
  hina_reflected_spec_constant* spec_constants; // Reflected spec constants (free with hslc_free_desc)
  uint32_t spec_constant_count;
} hslc_shader_desc;

/**
 * @brief Compile an HSL v1.0 shader to SPIR-V.
 *
 * This is the full shader module compilation API that:
 * - Expands #include directives
 * - Injects version/extensions
 * - Processes HSL v1.0 directives (#hina_stage, #hina_bindings, #hina_varyings, etc.)
 * - Injects user defines
 * - Compiles to SPIR-V
 *
 * @note Raw GLSL (no HSL directives) must use hslc_compile_glsl instead.
 *
 * @param desc Compilation descriptor
 * @param out_shader Output shader descriptor (spirv_data must be freed with hslc_free_spirv)
 * @param out_log Optional output for error log (heap-allocated, free with hslc_free_log)
 * @return true on success, false on failure
 */
HINA_API bool hslc_compile(const hslc_compile_desc* desc, hslc_shader_desc* out_shader, char** out_log);

/**
 * @brief Free SPIR-V data returned by hslc_compile.
 */
HINA_API void hslc_free_spirv(const void* spirv_data);

/**
 * @brief Free all data in a shader_desc returned by hslc_compile.
 * This frees spirv_data and spec_constants (including names).
 */
HINA_API void hslc_free_desc(hslc_shader_desc* desc);

/**
 * @brief Free error log returned by hslc_compile.
 */
HINA_API void hslc_free_log(char* log);

// Legacy Shader Compilation (GLSL -> SPIRV) - Deprecated, use hslc_compile instead
HINA_API bool hslc_compile_glsl(const char* source, size_t length, hina_shader_stage stage, const char* entry_point,
                                uint32_t** out_words, size_t* out_word_count);

HINA_API void hslc_free_spirv_words(uint32_t* words);

#endif // HINA_NO_SHADER_COMPILER
// ---------------------------------------------------------------------------
// Shader Modules (runtime, no compiler needed)
// ---------------------------------------------------------------------------
/**
 * @brief Create a shader from raw SPIR-V bytecode.
 *
 * The returned hina_shader holds a pointer to the SPIR-V data but does NOT
 * take ownership. Caller must keep spirv_data alive until hina_destroy_shader().
 *
 * @param spirv_data SPIR-V bytecode (caller-owned, must stay valid)
 * @param spirv_size Size of SPIR-V data in bytes
 * @return Initialized shader struct (module=NULL until first pipeline use)
 */
HINA_API hina_shader hina_make_shader(const void* spirv_data, size_t spirv_size);

/**
 * @brief Create a shader and take ownership of the SPIR-V buffer.
 *
 * hina takes ownership of spirv_data and will free it when hina_destroy_shader() is called.
 * The buffer must have been allocated with malloc/calloc/realloc.
 *
 * @param spirv_data SPIR-V bytecode (ownership transferred to hina)
 * @param spirv_size Size of SPIR-V data in bytes
 * @return Initialized shader struct (module=NULL until first pipeline use)
 */
HINA_API hina_shader hina_make_shader_take(void* spirv_data, size_t spirv_size);

/**
 * @brief Destroy a shader and its cached VkShaderModule.
 *
 * If owns_spirv is true, also frees the SPIR-V data buffer.
 *
 * @param shd Pointer to shader to destroy (module set to NULL after)
 */
HINA_API void hina_destroy_shader(hina_shader* shd);

// Pipeline Descriptors
// Layout optimized: 4B -> 2B -> 1B fields (no internal padding)
typedef struct hina_vertex_attr
{
  hina_format format; // 4B
  uint16_t offset; // 2B
  uint8_t location; // 1B
  uint8_t buffer_index; // 1B
} hina_vertex_attr;

typedef struct hina_vertex_layout
{
  uint8_t buffer_count;
  uint32_t buffer_strides[HINA_MAX_VERTEX_BUFFERS];
  uint32_t attr_count;
  hina_vertex_attr attrs[HINA_MAX_VERTEX_ATTRS];
  hina_vertex_input_rate input_rates[HINA_MAX_VERTEX_BUFFERS];
} hina_vertex_layout;

// Layout optimized: floats first, then 4B enums, then bools grouped
typedef struct hina_blend_state
{
  float constants[4]; // 16B
  hina_blend_factor src_color; // 4B
  hina_blend_factor dst_color; // 4B
  hina_blend_op color_op; // 4B
  hina_blend_factor src_alpha; // 4B
  hina_blend_factor dst_alpha; // 4B
  hina_blend_op alpha_op; // 4B
  hina_color_component_flags color_write_mask; // 4B
  bool enable; // 1B
  uint8_t pad_[3]; // 3B (explicit padding to 48B)
} hina_blend_state;

// Defaults: .enable = false, .color_write_mask = HINA_COLOR_COMPONENT_ALL,
//           .src_color = HINA_BLEND_FACTOR_SRC_ALPHA,
//           .dst_color = HINA_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA,
//           .color_op = HINA_BLEND_OP_ADD,
//           .src_alpha = HINA_BLEND_FACTOR_ONE,
//           .dst_alpha = HINA_BLEND_FACTOR_ZERO, .alpha_op = HINA_BLEND_OP_ADD
HINA_API hina_blend_state hina_blend_state_default(void);

// Layout optimized: 4B enum first, then bools grouped (8B total)
typedef struct hina_depth_stencil_state
{
  hina_compare_op depth_compare; // 4B
  bool depth_test; // 1B
  bool depth_write; // 1B
  bool stencil_test; // 1B
  uint8_t pad_; // 1B
} hina_depth_stencil_state;

// Defaults: .depth_test = true, .depth_write = true,
//           .depth_compare = HINA_COMPARE_OP_LESS_OR_EQUAL, .stencil_test = false
HINA_API hina_depth_stencil_state hina_depth_stencil_state_default(void);

typedef enum
{
  HINA_STENCIL_KEEP,
  HINA_STENCIL_ZERO,
  HINA_STENCIL_REPLACE,
  HINA_STENCIL_INC_CLAMP,
  HINA_STENCIL_DEC_CLAMP,
  HINA_STENCIL_INVERT,
  HINA_STENCIL_INC_WRAP,
  HINA_STENCIL_DEC_WRAP
} hina_stencil_op;

// Layout optimized: floats first, then bools grouped (16B total)
typedef struct hina_depth_bias_state
{
  float constant; // 4B
  float slope; // 4B
  float clamp; // 4B
  bool enable; // 1B
  uint8_t pad_[3]; // 3B
} hina_depth_bias_state;

typedef struct hina_stencil_face_state
{
  hina_stencil_op fail_op;
  hina_stencil_op pass_op;
  hina_stencil_op depth_fail_op;
  hina_compare_op compare_op;
  uint32_t compare_mask;
  uint32_t write_mask;
  uint32_t reference;
} hina_stencil_face_state;

typedef struct hina_push_constant_range
{
  uint8_t stage_flags;
  uint16_t offset;
  uint16_t size;
} hina_push_constant_range;

/**
 * @brief Specialization constant entry for pipeline creation.
 *
 * Specialization constants allow setting shader constants at pipeline creation
 * time, enabling shader variants without recompilation. Use with GLSL syntax:
 *   layout(constant_id = 0) const int MY_CONST = 0;
 *   layout(constant_id = 1) const float MY_FLOAT = 1.0;
 */
typedef struct hina_specialization_constant
{
  uint32_t constant_id; // Matches constant_id in shader
  uint32_t size; // sizeof(int), sizeof(float), sizeof(bool), etc.
  union
  {
    int32_t i32;
    uint32_t u32;
    float f32;
    int32_t b32; // VkBool32 is 32-bit
  } value;
} hina_specialization_constant;

// Forward declaration for tile pass layout (full definition below hina_tile_pass_desc)
typedef struct hina_tile_pass_layout hina_tile_pass_layout;
/**
 * @brief Complete pipeline state descriptor.
 *
 * HinaVK combines all state into a single immutable pipeline object.
 * Descriptor set layouts are automatically inferred from SPIR-V reflection.
 *
 * @section blend_state Per-Attachment Blend States
 * The `blend[]` array provides independent blend states for each color attachment.
 * - **blend[i]** corresponds to **color_formats[i]** (1:1 mapping)
 * - If device supports `VkPhysicalDeviceFeatures::independentBlend`, each attachment
 *   uses its own blend state from `blend[i]`
 * - If device lacks independent blend, **blend[0] is used for ALL attachments**
 *
 * @section color_formats Color Attachment Formats
 * The `color_formats[]` array specifies render target formats:
 * - First HINA_FORMAT_UNDEFINED terminates the list (count derived automatically)
 * - Use hina_get_surface_format() to get the swapchain format for swapchain rendering
 * - Sparse attachments (UNDEFINED between valid formats) trigger debug assertion
 * - Default: all color_formats = HINA_FORMAT_UNDEFINED (must be set explicitly)
 */
typedef struct hina_pipeline_desc
{
  hina_shader vs;
  hina_shader tcs;
  hina_shader tes;
  hina_shader gs;
  hina_shader fs;
  hina_vertex_layout layout;
  hina_blend_state blend[HINA_MAX_COLOR_ATTACHMENTS];
  hina_depth_stencil_state depth;
  hina_depth_bias_state depth_bias;
  hina_primitive_topology primitive_topology;
  hina_polygon_mode polygon_mode;
  hina_cull_mode cull_mode;
  hina_front_face front_face;
  hina_stencil_face_state stencil_front, stencil_back;

  /**
   * Render target formats for dynamic rendering.
   * - HINA_FORMAT_UNDEFINED: No attachment (first UNDEFINED terminates list)
   * - Use hina_get_surface_format() for swapchain-compatible pipelines
   * - Count derived automatically; sparse attachments are not allowed
   */
  hina_format color_formats[HINA_MAX_COLOR_ATTACHMENTS];
  hina_format depth_format;
  hina_format stencil_format;
  hina_sample_count samples; // MSAA sample count (0 or 1 = no MSAA)
  uint32_t patch_control_points; // Tessellation patch control points (0 = unused)
  uint32_t subpass_index; // For tile pass pipelines (default 0)
  const hina_tile_pass_layout* tile_layout; // Required when subpass_index > 0 (subpass_index < subpass_count)
  // Push Constants
  uint32_t push_constant_range_count;
  const hina_push_constant_range* push_constant_ranges;
  // Specialization Constants (optional, per-stage)
  const hina_specialization_constant* vs_specializations;
  uint32_t vs_specialization_count;
  const hina_specialization_constant* tcs_specializations;
  uint32_t tcs_specialization_count;
  const hina_specialization_constant* tes_specializations;
  uint32_t tes_specialization_count;
  const hina_specialization_constant* gs_specializations;
  uint32_t gs_specialization_count;
  const hina_specialization_constant* fs_specializations;
  uint32_t fs_specialization_count;

  /**
   * Explicit bind group layouts (optional, production path).
   * Count derived from array - first invalid handle terminates (zero-init = use reflection).
   * When valid layouts provided, uses these instead of auto-generating from shader reflection.
   * Debug builds validate that explicit layouts match shader reflection.
   */
  hina_bind_group_layout bind_group_layouts[HINA_MAX_DESCRIPTOR_SETS];
  const char* label; // Optional debug label (shows in RenderDoc, validation layers)
} hina_pipeline_desc;

// Defaults: .primitive_topology = HINA_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST,
//           .polygon_mode = HINA_POLYGON_MODE_FILL,
//           .cull_mode = HINA_CULL_MODE_BACK,
//           .front_face = HINA_FRONT_FACE_COUNTER_CLOCKWISE,
//           .samples = HINA_SAMPLE_COUNT_1_BIT,
//           .blend[0..3] = hina_blend_state_default() (per-attachment, 1:1 with color_formats)
//           .depth = (see hina_depth_stencil_state defaults),
//           .color_formats[0..3] = HINA_FORMAT_UNDEFINED (must set explicitly)
HINA_API hina_pipeline_desc hina_pipeline_desc_default(void);

typedef struct hina_compute_pipeline_desc
{
  hina_shader cs;
  uint32_t push_constant_range_count;
  const hina_push_constant_range* push_constant_ranges;
  /**
   * Explicit bind group layouts (optional).
   * Count derived from array - first invalid handle terminates (zero-init = use reflection).
   */
  hina_bind_group_layout bind_group_layouts[HINA_MAX_DESCRIPTOR_SETS];
  const char* label; // Optional debug label (shows in RenderDoc, validation layers)
} hina_compute_pipeline_desc;

// Zero-initialize hina_compute_pipeline_desc; set cs shader before use.
/**
 * @brief Unified pipeline descriptor (graphics/compute).
 *
 * Use kind to select which union member is valid.
 * RT pipelines are reserved for future support.
 */
typedef struct hina_pipeline_desc_any
{
  hina_pipeline_kind kind;

  union
  {
    hina_pipeline_desc graphics;
    hina_compute_pipeline_desc compute;
  } desc;
} hina_pipeline_desc_any;

// Pipeline Creation
HINA_API hina_pipeline hina_make_pipeline_ex(const hina_pipeline_desc_any* desc);

HINA_API void hina_destroy_pipeline(hina_pipeline pip);

HINA_API void hina_ctx_destroy_pipeline(hina_context* ctx, hina_pipeline pip);

// ---------------------------------------------------------------------------
// HSL Pipeline API - Create pipelines from compiled HSL modules
// ---------------------------------------------------------------------------
/**
 * @brief HSL Pipeline Descriptor.
 *
 * Creates a pipeline from a compiled HSL module. See hina_pipeline_desc for
 * detailed documentation on blend states and color formats.
 *
 * @section blend_state Per-Attachment Blend States
 * Same behavior as hina_pipeline_desc:
 * - blend[i] corresponds to color_formats[i]
 * - Falls back to blend[0] for all if device lacks independentBlend feature
 *
 * @section color_formats Color Attachment Formats
 * Same behavior as hina_pipeline_desc:
 * - First UNDEFINED terminates the list
 * - Use hina_get_surface_format() for swapchain-compatible pipelines
 * - Sparse attachments trigger debug assertion
 *
 * @note The library no longer compiles HSL at runtime. Use hina_shader
 *       to compile HSL into a module, then create a pipeline from it.
 */
typedef struct hina_hsl_pipeline_desc
{
  // Pipeline state (same fields as hina_pipeline_desc, minus shader handles)
  hina_vertex_layout layout;

  /**
   * Per-attachment blend states (1:1 correspondence with color_formats).
   * Falls back to blend[0] for all if device lacks independentBlend feature.
   */
  hina_blend_state blend[HINA_MAX_COLOR_ATTACHMENTS];
  hina_depth_stencil_state depth;
  hina_depth_bias_state depth_bias;
  hina_primitive_topology primitive_topology;
  hina_polygon_mode polygon_mode;
  hina_cull_mode cull_mode;
  hina_front_face front_face;
  hina_stencil_face_state stencil_front, stencil_back;

  /**
   * Render target formats for dynamic rendering.
   * - First UNDEFINED terminates list (count derived automatically)
   * - Use hina_get_surface_format() for swapchain-compatible pipelines
   * - Sparse attachments trigger debug assertion
   */
  hina_format color_formats[HINA_MAX_COLOR_ATTACHMENTS];
  hina_format depth_format;
  hina_format stencil_format;
  hina_sample_count samples; // MSAA sample count (0 or 1 = no MSAA)
  uint32_t patch_control_points; // Tessellation patch control points (0 = unused)
  uint32_t subpass_index; // For tile pass pipelines (default 0)
  const hina_tile_pass_layout* tile_layout; // Required when subpass_index > 0 (subpass_index < subpass_count)
  uint32_t push_constant_range_count;
  const hina_push_constant_range* push_constant_ranges;
  // Specialization Constants (optional, per-stage)
  const hina_specialization_constant* vs_specializations;
  uint32_t vs_specialization_count;
  const hina_specialization_constant* tcs_specializations;
  uint32_t tcs_specialization_count;
  const hina_specialization_constant* tes_specializations;
  uint32_t tes_specialization_count;
  const hina_specialization_constant* gs_specializations;
  uint32_t gs_specialization_count;
  const hina_specialization_constant* fs_specializations;
  uint32_t fs_specialization_count;
  /**
   * Explicit bind group layouts (optional, production path).
   * Count derived from array - first invalid handle terminates (zero-init = use reflection).
   * When valid layouts provided, uses these instead of auto-generating from shader reflection.
   * Debug builds validate that module bindings match these layouts.
   */
  hina_bind_group_layout bind_group_layouts[HINA_MAX_DESCRIPTOR_SETS];
  const char* label; // Optional debug label (shows in RenderDoc, validation layers)
} hina_hsl_pipeline_desc;

// Defaults: Same as hina_pipeline_desc_default(), with .module = NULL
HINA_API hina_hsl_pipeline_desc hina_hsl_pipeline_desc_default(void);

/**
 * @brief Create a pipeline from a pre-compiled HSL module.
 *
 * Use this when creating multiple pipelines from the same shader with different:
 * - Specialization constants (different lighting models, features, etc.)
 * - Pipeline state (blend modes, depth modes, etc.)
 *
 * The module contains reflection data that validates specialization constants
 * and can auto-generate vertex layouts.
 *
 * @note For compute modules, graphics state fields are ignored and
 *       desc->vs_specializations are applied to the compute stage.
 *
 * @param module Compiled HSL module from hslc_compile_hsl()
 * @param desc Pipeline state (shader source fields are ignored, uses module's SPIR-V)
 * @param cache Optional shader module cache (NULL = create temp modules, destroyed after use)
 * @return Pipeline handle, or invalid handle on failure (errors logged via HINA_LOGE)
 */
HINA_API hina_pipeline hina_make_pipeline_from_module(const hina_hsl_module* module, const hina_hsl_pipeline_desc* desc,
                                                      hina_hsl_cache* cache);

/**
 * @brief Get the bind group layout for a specific set from a pipeline.
 * Returns the pipeline's internal layout which has correct stage flags.
 * The returned layout is owned by the pipeline - do NOT destroy it.
 *
 * @param pipeline The graphics or compute pipeline
 * @param set_index Which descriptor set (0-3) to get the layout for
 * @return Bind group layout, or invalid handle if set has no bindings
 */
HINA_API hina_bind_group_layout hina_pipeline_get_bind_group_layout(hina_pipeline pipeline, uint32_t set_index);

// Pipeline Cache Serialization
// Based on: https://zeux.io/2019/07/17/serializing-pipeline-cache/
//
// Usage:
//   SAVE (after rendering, before shutdown):
//     void* blob; size_t size;
//     if (hina_get_pipeline_cache_data(&blob, &size)) {
//       write_file("pipeline.cache", blob, size);
//       hina_free_pipeline_cache_data(blob);
//     }
//
//   LOAD (before hina_init):
//     void* blob = read_file("pipeline.cache", &size);  // NULL if file doesn't exist
//     hina_desc desc = hina_desc_default();
//     desc.pipeline_cache.data = blob;  // NULL is fine, will start fresh
//     desc.pipeline_cache.size = size;
//     hina_init(&desc);
//     free(blob);  // Safe immediately after hina_init returns
//
// The blob contains a validation header (device fingerprint + integrity hash).
// On load, hina_init() validates and silently ignores stale/corrupt data.
HINA_API bool hina_get_pipeline_cache_data(void** out_data, size_t* out_size);

HINA_API void hina_free_pipeline_cache_data(void* data);

//  Command Recording
// Begin/End Recording (convenience - defaults to GRAPHICS queue)
HINA_API hina_cmd* hina_cmd_begin(void);

HINA_API hina_cmd* hina_ctx_cmd_begin(hina_context* ctx);

// Begin/End Recording (explicit queue selection)
HINA_API hina_cmd* hina_cmd_begin_ex(hina_queue queue);

HINA_API hina_cmd* hina_ctx_cmd_begin_ex(hina_context* ctx, hina_queue queue);

HINA_API void hina_cmd_end(hina_cmd* cmd);

// Load operation: what happens to attachment contents at pass start
typedef enum
{
  HINA_LOAD_OP_LOAD, // Preserve previous contents (costs bandwidth)
  HINA_LOAD_OP_CLEAR, // Clear to clear_color/depth_clear value
  HINA_LOAD_OP_DONT_CARE // Contents undefined (fastest, use when you'll write every pixel)
} hina_load_op;

// Store operation: what happens to attachment contents at pass end
typedef enum
{
  HINA_STORE_OP_STORE, // Write results to memory (required if you need the data later)
  HINA_STORE_OP_DONT_CARE // Discard results (use for MSAA color before resolve, or depth you won't read)
} hina_store_op;

typedef enum
{
  HINA_PASS_FLAG_NONE = 0,
  // TRANSIENT_COLOR: Hint that color attachments are intermediate results that
  // won't be read after the pass. On mobile, allows keeping data in tile memory.
  HINA_PASS_TRANSIENT_COLOR_BIT = 1 << 0,
  // TRANSIENT_DEPTH: Hint that depth/stencil is only needed within this pass.
  // Common for shadow map rendering or when depth is just for z-testing.
  HINA_PASS_TRANSIENT_DEPTH_BIT = 1 << 1
} hina_pass_flags;

typedef struct hina_color_attachment
{
  hina_texture_view image;
  hina_texture_view resolve; // MSAA resolve target (invalid = no resolve)
  hina_load_op load_op;
  hina_store_op store_op;
  float clear_color[4];
} hina_color_attachment;

typedef struct hina_depth_attachment
{
  hina_texture_view image;
  hina_load_op load_op;
  hina_store_op store_op;
  float depth_clear;
  uint8_t stencil_clear;
} hina_depth_attachment;

/**
 * @brief Defines a dynamic render pass.
 * HinaVK uses dynamic rendering, so pass objects are transient structs, not created resources.
 *
 * @section color_attachments Color Attachments
 * - First invalid handle (`.id == HINA_INVALID_HANDLE`) in colors[] terminates the list
 * - Dynamic rendering backend (vkCmdBeginRendering): supports all HINA_MAX_COLOR_ATTACHMENTS
 * - Legacy render pass backend: **only uses colors[0]** (colors[1..N] silently ignored)
 *   Check hina_device_caps.has_dynamic_rendering to detect which backend is active.
 */
typedef struct hina_pass_action
{
  hina_color_attachment colors[HINA_MAX_COLOR_ATTACHMENTS];
  hina_depth_attachment depth;
  uint32_t width;
  uint32_t height;
  uint32_t flags; // hina_pass_flags
} hina_pass_action;

/**
 * @brief Begin a dynamic render pass.
 *
 * Starts rendering to the color/depth attachments specified in the action.
 * Must be paired with hina_cmd_end_pass(). Cannot be nested.
 *
 * @param cmd   Command buffer (must be recording, not inside a pass)
 * @param action Pass configuration (attachments, dimensions, clear values)
 */
HINA_API void hina_cmd_begin_pass(hina_cmd* cmd, const hina_pass_action* action);

HINA_API void hina_cmd_end_pass(hina_cmd* cmd);

/**
 * @brief Tile Pass System
 *
 * Tile passes enable deferred rendering with input attachments (subpassInput/subpassLoad)
 * instead of texture sampling. Two backend implementations:
 * - DYNAMIC_LOCAL_READ: VK_KHR_dynamic_rendering_local_read (Vulkan 1.4+ or extension)
 * - LEGACY_SUBPASS: Multi-subpass VkRenderPass (Vulkan 1.0-1.3 fallback)
 *
 * Use hina_device_caps.has_dynamic_rendering_local_read to check which backend is active.
 * Set HINA_DEBUG_FORCE_LEGACY_TILE_PASS_BIT to test legacy path on modern hardware.
 * @note Tile pass counts and indices must respect the HINA_MAX_* limits.
 * @note Tile pass attachments must share the same sample count.
 * @note MSAA resolves are only supported in the last subpass; resolve targets
 *       must be single-sampled and match color formats.
 */
/** @brief Describes a tile input (input attachment) reference
 *
 * @note subpass_output_index must refer to an earlier subpass.
 * @note attachment_index must be < color_count of that subpass (and < HINA_MAX_COLOR_ATTACHMENTS).
 */
typedef struct hina_tile_input
{
  uint32_t subpass_output_index; // Source subpass that wrote this attachment
  uint32_t attachment_index; // Color attachment index in source subpass
} hina_tile_input;

/** @brief Describes a single subpass within a tile pass */
typedef struct hina_tile_subpass
{
  const char* label; // Debug label
  hina_color_attachment color[HINA_MAX_COLOR_ATTACHMENTS]; // Color outputs
  uint32_t color_count; // Must be <= HINA_MAX_COLOR_ATTACHMENTS
  hina_tile_input tile_inputs[HINA_MAX_TILE_INPUTS]; // Input attachments to read
  uint32_t tile_input_count; // Must be <= HINA_MAX_TILE_INPUTS
  hina_depth_attachment depth;
  bool has_depth;
  bool depth_read_only; // Preserve depth from previous subpass (read-only)
  bool depth_input;     // Read depth from prior subpass as input attachment
} hina_tile_subpass;

/**
 * @brief Describes a complete tile pass with multiple subpasses.
 *
 * Subpass count is derived automatically - first empty subpass terminates.
 * An empty subpass has: color_count == 0 && tile_input_count == 0 && has_depth == false.
 * Zero-init produces no subpasses; at least one subpass with attachments is required.
 */
typedef struct hina_tile_pass_desc
{
  const char* label;
  hina_tile_subpass subpasses[HINA_MAX_TILE_SUBPASSES];
  uint32_t width; // Render area width (0 = use first attachment size)
  uint32_t height; // Render area height (0 = use first attachment size)
} hina_tile_pass_desc;

HINA_API bool hina_begin_tile_pass(hina_cmd* cmd, const hina_tile_pass_desc* desc);

HINA_API void hina_tile_pass_next(hina_cmd* cmd, const hina_tile_pass_desc* desc);

HINA_API void hina_end_tile_pass(hina_cmd* cmd);

/**
 * @brief Tile Pass Layout (for pipeline creation)
 *
 * When creating pipelines for tile passes in legacy render pass mode, Vulkan requires
 * a compatible VkRenderPass at pipeline creation time. This struct describes the
 * subpass structure (formats only, no texture views) so that a template render pass
 * can be generated.
 *
 * @note For the current subpass (pipeline_desc.subpass_index), formats are derived
 *       from pipeline_desc.color_formats/depth_format. You only need to fill in
 *       color_formats/depth_format for OTHER subpasses in the tile pass.
 *
 * @note For MSAA tile passes, set samples to the MSAA count and define
 *       resolve_formats for the last subpass outputs that will be resolved.
 *
 * Required when: subpass_index > 0, or input_attachment_count > 0
 */
typedef struct hina_tile_subpass_layout
{
  hina_format color_formats[HINA_MAX_COLOR_ATTACHMENTS]; // [0..color_count) for other subpasses
  hina_format resolve_formats[HINA_MAX_COLOR_ATTACHMENTS]; // HINA_FORMAT_UNDEFINED = no resolve (last subpass only)
  uint32_t color_count; // Must be <= HINA_MAX_COLOR_ATTACHMENTS
  hina_format depth_format; // HINA_FORMAT_UNDEFINED = no depth (for other subpasses)
  bool depth_read_only; // Must match runtime for render pass compatibility
  bool depth_input;     // Read depth from prior subpass as input attachment
  hina_tile_input tile_inputs[HINA_MAX_TILE_INPUTS]; // Input attachment mappings
  uint32_t input_count; // Must be <= HINA_MAX_TILE_INPUTS
} hina_tile_subpass_layout;

/**
 * @brief Layout for tile passes (used in pipeline creation).
 *
 * Subpass count is derived automatically - first empty subpass terminates.
 * An empty subpass has: color_count == 0 && input_count == 0 && depth_format == UNDEFINED.
 * Zero-init produces no subpasses; pipelines using tile_layout require at least one subpass.
 */
typedef struct hina_tile_pass_layout
{
  hina_tile_subpass_layout subpasses[HINA_MAX_TILE_SUBPASSES];
  hina_sample_count samples; // Must be 0 or a single HINA_SAMPLE_COUNT_* value
} hina_tile_pass_layout;

// Vertex Input
typedef struct hina_vertex_input
{
  hina_buffer vertex_buffers[HINA_MAX_VERTEX_BUFFERS];
  uint64_t vertex_offsets[HINA_MAX_VERTEX_BUFFERS];
  hina_buffer index_buffer;
  hina_index_type index_type;
} hina_vertex_input;

HINA_API void hina_cmd_bind_pipeline(hina_cmd* cmd, hina_pipeline pip);

HINA_API void hina_cmd_push_constants(hina_cmd* cmd, uint32_t offset, uint32_t size, const void* data);

HINA_API void hina_cmd_apply_vertex_input(hina_cmd* cmd, const hina_vertex_input* b);

// Slot-based Resource Binding (raw SPIRV escape hatch)
// For use with raw SPIRV shaders that bypass HSL reflection.
// Slot encoding: slot = (set << 8) | binding (up to 256 bindings per set)
// Bindings are automatically flushed to Vulkan descriptor sets before draw/dispatch.
HINA_API void hina_cmd_bind_buffer(hina_cmd* cmd, uint32_t slot, hina_buffer buf, size_t offset, size_t size);

HINA_API void hina_cmd_bind_storage_image(hina_cmd* cmd, uint32_t slot, hina_texture_view view);

// Draw Calls
HINA_API void hina_cmd_draw(hina_cmd* cmd, uint32_t vtx_count, uint32_t instance_count, uint32_t first_vtx,
                            uint32_t first_instance);

HINA_API void hina_cmd_draw_indexed(hina_cmd* cmd, uint32_t idx_count, uint32_t instance_count, uint32_t first_index,
                                    int32_t vertex_offset, uint32_t first_instance);

HINA_API void hina_cmd_draw_indexed_indirect(hina_cmd* cmd, hina_buffer indirect_buf, uint64_t offset,
                                             uint32_t draw_count, uint32_t stride);

// Dynamic State
HINA_API void hina_cmd_set_depth_bias(hina_cmd* cmd, float constant, float slope, float clamp);

HINA_API void hina_cmd_set_blend_color(hina_cmd* cmd, const float color[4]);

/**
 * @brief Viewport definition for dynamic viewport state.
 * Coordinates follow Vulkan conventions (Y down, depth 0-1).
 */
typedef struct hina_viewport
{
  float x;
  float y;
  float width;
  float height;
  float min_depth;
  float max_depth;
} hina_viewport;

/**
 * @brief Scissor rectangle for dynamic scissor state.
 */
typedef struct hina_scissor
{
  int32_t x;
  int32_t y;
  uint32_t width;
  uint32_t height;
} hina_scissor;

/**
 * @brief Set the viewport dynamically.
 * Overrides the viewport set by hina_cmd_begin_pass.
 * Must be called after hina_cmd_begin_pass and before draw calls.
 */
HINA_API void hina_cmd_set_viewport(hina_cmd* cmd, const hina_viewport* viewport);

/**
 * @brief Set the scissor rectangle dynamically.
 * Overrides the scissor set by hina_cmd_begin_pass.
 * Must be called after hina_cmd_begin_pass and before draw calls.
 */
HINA_API void hina_cmd_set_scissor(hina_cmd* cmd, const hina_scissor* scissor);

/**
 * @brief Set the line width dynamically.
 * Only effective when rendering lines (polygon mode LINE or line primitives).
 * Requires wideLines feature for values other than 1.0f.
 */
HINA_API void hina_cmd_set_line_width(hina_cmd* cmd, float width);

// Compute Dispatch
HINA_API void hina_cmd_dispatch(hina_cmd* cmd, uint32_t group_count_x, uint32_t group_count_y, uint32_t group_count_z);

HINA_API void hina_cmd_dispatch_indirect(hina_cmd* cmd, hina_buffer indirect_buf, uint64_t offset);

/**
 * @brief Dispatch compute work by thread count (auto-calculates group count).
 *
 * Divides thread_count by workgroup size to determine group count:
 *   group_count = (thread_count + local_size - 1) / local_size
 *
 * @note Only works with HSL module pipelines that have known workgroup sizes.
 *       For raw SPIR-V pipelines or spec-constant workgroup sizes, use hina_cmd_dispatch().
 *       Asserts if workgroup size is unknown (local_size == 0).
 */
HINA_API void hina_cmd_dispatch_threads(hina_cmd* cmd, uint32_t thread_count_x, uint32_t thread_count_y,
                                        uint32_t thread_count_z);

// Transfer Operations
HINA_API void hina_cmd_copy_buffer(hina_cmd* cmd, hina_buffer src, hina_buffer dst, uint64_t src_offset,
                                   uint64_t dst_offset, uint64_t size);

HINA_API void hina_cmd_copy_buffer_to_texture(hina_cmd* cmd, hina_buffer src, hina_texture dst, uint64_t buffer_offset,
                                              uint32_t mip_level, uint32_t array_layer);

HINA_API void hina_cmd_copy_texture_to_buffer(hina_cmd* cmd, hina_texture src, hina_buffer dst, uint32_t mip_level,
                                              uint32_t array_layer, uint64_t buffer_offset);

HINA_API void hina_cmd_copy_buffer_to_texture_3d(hina_cmd* cmd, hina_buffer src, hina_texture dst,
                                                 uint64_t buffer_offset, uint32_t mip_level, uint32_t z_offset,
                                                 uint32_t depth);

HINA_API void hina_cmd_copy_texture_to_buffer_3d(hina_cmd* cmd, hina_texture src, hina_buffer dst, uint32_t mip_level,
                                                 uint32_t z_offset, uint32_t depth, uint64_t buffer_offset);

HINA_API void hina_cmd_blit_texture(hina_cmd* cmd, hina_texture src, hina_texture dst, uint32_t src_mip,
                                    uint32_t dst_mip, hina_filter filter);

// Texture Barriers
typedef enum
{
  HINA_TEXSTATE_SHADER_READ,
  HINA_TEXSTATE_COLOR_ATTACHMENT,
  HINA_TEXSTATE_DEPTH_ATTACHMENT,
  HINA_TEXSTATE_TRANSFER_SRC,
  HINA_TEXSTATE_TRANSFER_DST,
  HINA_TEXSTATE_PRESENT // For swapchain images before presentation
} hina_texture_state_hint;

HINA_API void hina_cmd_transition_texture(hina_cmd* cmd, hina_texture tex, hina_texture_state_hint new_state);

// Memory Barriers (for compute-graphics synchronization)
typedef enum
{
  HINA_ACCESS_NONE           = 0,
  HINA_ACCESS_VERTEX_READ    = HINA_FLAG_BIT(0),
  HINA_ACCESS_INDEX_READ     = HINA_FLAG_BIT(1),
  HINA_ACCESS_UNIFORM_READ   = HINA_FLAG_BIT(2),
  HINA_ACCESS_SHADER_READ    = HINA_FLAG_BIT(3),
  HINA_ACCESS_SHADER_WRITE   = HINA_FLAG_BIT(4),
  HINA_ACCESS_TRANSFER_READ  = HINA_FLAG_BIT(5),
  HINA_ACCESS_TRANSFER_WRITE = HINA_FLAG_BIT(6),
  HINA_ACCESS_HOST_READ      = HINA_FLAG_BIT(7),
  HINA_ACCESS_HOST_WRITE     = HINA_FLAG_BIT(8),
  HINA_ACCESS_INDIRECT_READ  = HINA_FLAG_BIT(9)
} hina_access_flags;

typedef enum
{
  HINA_PIPELINE_STAGE_TOP             = HINA_FLAG_BIT(0),
  HINA_PIPELINE_STAGE_DRAW_INDIRECT   = HINA_FLAG_BIT(1),
  HINA_PIPELINE_STAGE_VERTEX_INPUT    = HINA_FLAG_BIT(2),
  HINA_PIPELINE_STAGE_VERTEX_SHADER   = HINA_FLAG_BIT(3),
  HINA_PIPELINE_STAGE_FRAGMENT_SHADER = HINA_FLAG_BIT(4),
  HINA_PIPELINE_STAGE_COMPUTE_SHADER  = HINA_FLAG_BIT(5),
  HINA_PIPELINE_STAGE_TRANSFER        = HINA_FLAG_BIT(6),
  HINA_PIPELINE_STAGE_BOTTOM          = HINA_FLAG_BIT(7),
  HINA_PIPELINE_STAGE_HOST            = HINA_FLAG_BIT(8),
  HINA_PIPELINE_STAGE_ALL_GRAPHICS    = HINA_FLAG_BIT(9),
  HINA_PIPELINE_STAGE_ALL_COMMANDS    = HINA_FLAG_BIT(10)
} hina_pipeline_stage_flags;

/**
 * @brief Insert a memory barrier for buffer synchronization.
 * Use this to synchronize compute shader writes with graphics reads, etc.
 */
HINA_API void hina_cmd_buffer_barrier(hina_cmd* cmd, hina_buffer buf, hina_pipeline_stage_flags src_stage,
                                      hina_access_flags src_access, hina_pipeline_stage_flags dst_stage,
                                      hina_access_flags dst_access);

/**
 * @brief Insert a global memory barrier (not buffer-specific).
 * Synchronizes all memory access between pipeline stages.
 */
HINA_API void hina_cmd_memory_barrier(hina_cmd* cmd, hina_pipeline_stage_flags src_stage, hina_access_flags src_access,
                                      hina_pipeline_stage_flags dst_stage, hina_access_flags dst_access);

// ---------------------------------------------------------------------------
// Queue Ownership Transfer (for cross-queue resource usage)
// ---------------------------------------------------------------------------
// Resources use exclusive sharing mode by default. When a resource is used on
// a different queue than its current owner, explicit ownership transfer is required.
//
// Usage pattern:
//   1. Source queue: hina_cmd_release_*(cmd, resource, dst_queue)
//   2. Submit source command, get sync_point
//   3. Dest queue: hina_frame_wait(dst_queue, sync_point)
//   4. Dest queue: hina_cmd_acquire_*(cmd, resource, src_queue, new_state)
//
// If src and dst queues map to the same queue family, these are no-ops.
/**
 * @brief Release resource ownership to another queue.
 * Call this on the source queue before submitting. The semaphore signal from
 * frame_submit() will synchronize the transfer.
 */
HINA_API void hina_cmd_release_texture(hina_cmd* cmd, hina_texture tex, hina_queue dst_queue);

HINA_API void hina_cmd_release_buffer(hina_cmd* cmd, hina_buffer buf, hina_queue dst_queue);

/**
 * @brief Acquire resource ownership from another queue.
 * Call this on the destination queue after waiting on the source's sync_point.
 */
HINA_API void hina_cmd_acquire_texture(hina_cmd* cmd, hina_texture tex, hina_queue src_queue,
                                       hina_texture_state_hint new_state);

HINA_API void hina_cmd_acquire_buffer(hina_cmd* cmd, hina_buffer buf, hina_queue src_queue);

//  Query Pools & Profiling
typedef enum
{
  HINA_QUERY_TYPE_OCCLUSION,
  HINA_QUERY_TYPE_PIPELINE_STATISTICS,
  HINA_QUERY_TYPE_TIMESTAMP
} hina_query_type;

typedef struct hina_query_pool_desc
{
  hina_query_type type;
  uint32_t count;
} hina_query_pool_desc;

HINA_API hina_query_pool hina_make_query_pool(const hina_query_pool_desc* desc);

HINA_API void hina_destroy_query_pool(hina_query_pool pool);

HINA_API void hina_ctx_destroy_query_pool(hina_context* ctx, hina_query_pool pool);

HINA_API void hina_cmd_reset_query_pool(hina_cmd* cmd, hina_query_pool pool, uint32_t first_query, uint32_t count);

HINA_API void hina_cmd_write_timestamp(hina_cmd* cmd, hina_query_pool pool, uint32_t query_index, uint32_t stage_flags);

HINA_API void hina_cmd_begin_query(hina_cmd* cmd, hina_query_pool pool, uint32_t query_index);

HINA_API void hina_cmd_end_query(hina_cmd* cmd, hina_query_pool pool, uint32_t query_index);

HINA_API bool hina_get_query_results(hina_query_pool pool, uint32_t first_query, uint32_t count, uint64_t* results,
                                     bool wait);

double hina_timestamp_to_ns(uint64_t timestamp_delta);

//  Diagnostics
typedef struct hina_gpu_memory_stats
{
  uint64_t total_bytes;
  uint64_t usage_bytes;
} hina_gpu_memory_stats;

/**
 * @brief Query device-local GPU memory usage.
 *
 * Returns false if no device is initialized.
 */
HINA_API bool hina_get_gpu_memory_stats(hina_gpu_memory_stats* out_stats);

//  Debug Utilities
// Note: Use the 'label' field in resource descriptors (hina_buffer_desc, hina_texture_desc,
// hina_sampler_desc, hina_pipeline_desc) to set debug names at creation time.
// Texture Query
HINA_API void hina_get_texture_size(hina_texture tex, uint32_t* width, uint32_t* height);

HINA_API void hina_get_texture_extent(hina_texture tex, uint32_t* width, uint32_t* height, uint32_t* depth);

// Raw Access
HINA_API void* hina_get_vk_buffer(hina_buffer buf);

HINA_API void* hina_get_vk_image(hina_texture tex);

HINA_API void* hina_get_vk_image_view(hina_texture tex);

HINA_API void* hina_get_vk_pipeline(hina_pipeline pip);

HINA_API void* hina_get_vk_command_buffer(hina_cmd* cmd);

// Debug Labels (Markers)
HINA_API void hina_cmd_begin_label(hina_cmd* cmd, const char* name, float color[4]);

HINA_API void hina_cmd_end_label(hina_cmd* cmd);

HINA_API void hina_cmd_insert_label(hina_cmd* cmd, const char* name, float color[4]);

// Pre-defined label colors
#define HINA_LABEL_COLOR_RED     (float[4]){1.0f, 0.0f, 0.0f, 1.0f}
#define HINA_LABEL_COLOR_GREEN   (float[4]){0.0f, 1.0f, 0.0f, 1.0f}
#define HINA_LABEL_COLOR_BLUE    (float[4]){0.0f, 0.5f, 1.0f, 1.0f}
#define HINA_LABEL_COLOR_YELLOW  (float[4]){1.0f, 1.0f, 0.0f, 1.0f}
#define HINA_LABEL_COLOR_CYAN    (float[4]){0.0f, 1.0f, 1.0f, 1.0f}
#define HINA_LABEL_COLOR_MAGENTA (float[4]){1.0f, 0.0f, 1.0f, 1.0f}
#define HINA_LABEL_COLOR_ORANGE  (float[4]){1.0f, 0.5f, 0.0f, 1.0f}
#define HINA_LABEL_COLOR_PURPLE  (float[4]){0.6f, 0.0f, 1.0f, 1.0f}
// Macros
#define HINA_LABEL_SCOPE(cmd, name, color) \
    for (int _hina_label_once = (hina_cmd_begin_label(cmd, name, color), 1); \
         _hina_label_once; \
         _hina_label_once = 0, hina_cmd_end_label(cmd))
#define HINA_LABEL(cmd, name) \
    hina_cmd_begin_label(cmd, name, HINA_LABEL_COLOR_BLUE)
#define HINA_LABEL_END(cmd) \
    hina_cmd_end_label(cmd)
#define HINA_LABEL_MARKER(cmd, name) \
    hina_cmd_insert_label(cmd, name, HINA_LABEL_COLOR_CYAN)
#ifdef __cplusplus
}
#endif
#endif // HINA_VK_H
