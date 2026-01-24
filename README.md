<p align="center">
  <img src="images/banner.png" alt="HinaVK" width="600"/>
</p>

<h1 align="center">HinaVK</h1>

<p align="center">
  <b>A lightweight Vulkan abstraction layer in C</b>
</p>

<p align="center">
  <img src="https://img.shields.io/badge/Vulkan-1.0%2B-red?logo=vulkan" alt="Vulkan"/>
  <img src="https://img.shields.io/badge/C11-00599C?logo=c" alt="C11"/>
  <img src="https://img.shields.io/badge/license-MIT-blue" alt="License"/>
  <img src="https://img.shields.io/badge/platforms-Windows%20|%20Android-lightgrey" alt="Platforms"/>
</p>

---

## What is this?

HinaVK is a heavily opinionated Vulkan abstraction layer designed to make common tasks easier while hopefully still delivering high performance and low overhead. I didn't want to write 1.0 Vulkan, but I needed it to work on android devices. So I wrote this to make the experience slightly less painful.

- Mostly 1.3 styled API but has fallbacks for older Vulkan versions (1.0+)
- Uses bind groups instead of descriptor sets (borrowed the idea from WebGPU)
- Has its own shader format and compiler (HSL) so you can keep vertex + fragment in one file
- Handles resource creation and destruction for you and manages them for you internally, so you only have to manage handles.
- Entirely internal memory management for both VMA and internal tracking. 
- Works both for both single threaded and multi-threaded recording
- 1 .c file, 1 .h file, and one .cpp file for volk/VMA implementation

In the spirit of write-once-use-everywhere code, I don't expose platform-specific features like mesh shaders. If you need those, escape hatches to raw Vulkan exist.

---

## Quick Start

```c
hina_init(&(hina_desc){ .native_window = hwnd });

while (running) {
    hina_swapchain_image swap = hina_frame_begin();
    hina_cmd* cmd = hina_cmd_begin();
    hina_cmd_begin_pass(cmd, &pass);
    hina_cmd_draw(cmd, 3, 1, 0, 0);
    hina_cmd_end_pass(cmd);
    hina_frame_submit(cmd);
    hina_frame_end();
}

hina_shutdown();
```

---

## API Overview

### Frame Loop

The render loop looks like this:

```c
while (running) {
    hina_swapchain_image swap = hina_frame_begin();

    hina_cmd* cmd = hina_cmd_begin();
    hina_cmd_begin_pass(cmd, &pass);
    // ... draw calls ...
    hina_cmd_end_pass(cmd);

    hina_frame_submit(cmd);
    hina_frame_end();
}
```

If you need compute, sync points handle the cross-queue wait:

```c
// Compute pass
hina_cmd* compute_cmd = hina_cmd_begin_ex(HINA_QUEUE_COMPUTE);
hina_cmd_dispatch(compute_cmd, 64, 64, 1);
hina_sync_point compute_done = hina_frame_submit(compute_cmd);

// Graphics waits for compute
hina_frame_wait(HINA_QUEUE_GRAPHICS, compute_done);
hina_cmd* gfx_cmd = hina_cmd_begin();
// ... render using compute results ...
```

### Resources

Buffers, textures, and samplers:

```c
// Vertex buffer with initial data (uploaded via staging)
hina_buffer vbo = hina_make_buffer(&(hina_buffer_desc){
    .size = sizeof(vertices),
    .flags = HINA_BUFFER_VERTEX_BIT | HINA_BUFFER_TRANSFER_DST_BIT,
    .initial_data = vertices,
    .label = "vertex_buffer"
});

// Texture with mipmap generation
hina_texture tex = hina_make_texture(&(hina_texture_desc){
    .width = 512, .height = 512,
    .format = HINA_FORMAT_R8G8B8A8_SRGB,
    .flags = HINA_TEXTURE_SAMPLED_BIT | HINA_TEXTURE_TRANSFER_DST_BIT,
    .initial_data = pixels,
    .generate_mips = true,
    .label = "diffuse"
});

// Sampler
hina_sampler sampler = hina_make_sampler(&(hina_sampler_desc){
    .min_filter = HINA_FILTER_LINEAR,
    .mag_filter = HINA_FILTER_LINEAR,
    .mipmap_mode = HINA_MIPMAP_MODE_LINEAR,
    .max_anisotropy = 16.0f
});
```

For per-frame dynamic data, use host-visible buffers:

```c
hina_buffer ubo = hina_make_buffer(&(hina_buffer_desc){
    .size = sizeof(Uniforms),
    .flags = HINA_BUFFER_UNIFORM_BIT | HINA_BUFFER_HOST_VISIBLE_BIT,
});
Uniforms* ptr = hina_map_buffer(ubo);  // Persistent mapping, don't unmap
ptr->mvp = mvp_matrix;  // Just write to it each frame
```

Resource destruction is deferred internally—call destroy whenever you want and the library waits for the GPU:

```c
hina_destroy_buffer(vbo);
hina_destroy_texture(tex);
```

### Shaders (HSL)

HSL lets you write vertex + fragment in one file with some convenience syntax:

```glsl
// triangle.hina_sl
#hina
group Scene = 0;
bindings(Scene, start=0) {
  uniform(std140) UBO { mat4 mvp; } ubo;
}
struct VertexIn  { vec3 pos; vec3 color; };
struct Varyings  { vec3 color; };
struct FragOut   { vec4 color; };
#hina_end

#hina_stage vertex entry VSMain
Varyings VSMain(VertexIn in) {
    Varyings out;
    gl_Position = ubo.mvp * vec4(in.pos, 1.0);
    out.color = in.color;
    return out;
}
#hina_end

#hina_stage fragment entry FSMain
FragOut FSMain(Varyings in) {
    FragOut out;
    out.color = vec4(in.color, 1.0);
    return out;
}
#hina_end
```

The `#hina` block handles bind groups and IO structs so you don't write location annotations by hand. Full syntax is in [the HSL docs](docs/HINA_SHADER_LANGUAGE.md).

The compiler generates SPIR-V and extracts reflection data. Pipelines are created from compiled modules:

```c
hina_hsl_module* module = hslc_compile_hsl_source(source, "cube.hina_sl", &error);
hina_pipeline pipeline = hina_make_pipeline_from_module(module, &pip_desc, NULL);
```

### Pipelines

The easiest path is HSL modules, which include reflection data and validate bind group layouts for you in debug builds. Anything prefixed with hslc is part of the shader parsers and compiler, and can be disabled if you write the compiled module to disk and load it at runtime.

```c
hina_hsl_module* module = hslc_compile_hsl("shader.hina_sl", &error);
hina_pipeline pipeline = hina_make_pipeline_from_module(module, &(hina_hsl_pipeline_desc){
    .color_formats = { HINA_FORMAT_B8G8R8A8_SRGB },
    .depth_format = HINA_FORMAT_D32_SFLOAT,
    .cull_mode = HINA_CULL_BACK,
}, NULL);

// Layouts come from the module
hina_bind_group_layout scene_layout = hina_pipeline_get_bind_group_layout(pipeline, 0);
```

If you have pre-compiled SPIR-V or need more control:

```c
hina_shader vs = hina_make_shader(vs_spirv, vs_size);
hina_shader fs = hina_make_shader(fs_spirv, fs_size);

hina_pipeline pipeline = hina_make_pipeline_ex(&(hina_pipeline_desc_any){
    .kind = HINA_PIPELINE_KIND_GRAPHICS,
    .desc.graphics = {
        .vs = vs, .fs = fs,
        .layout = { ... },
        .color_formats = { HINA_FORMAT_B8G8R8A8_SRGB },
    }
});
```

### Render Passes

For simple rendering without tile-local reads, use regular passes:

```c
hina_cmd_begin_pass(cmd, &(hina_pass_action){
    .colors[0] = {
        .image = swapchain_view,
        .load_op = HINA_LOAD_OP_CLEAR,
        .store_op = HINA_STORE_OP_STORE,
        .clear_color = { 0.1f, 0.1f, 0.1f, 1.0f }
    },
    .depth = {
        .image = depth_view,
        .load_op = HINA_LOAD_OP_CLEAR,
        .store_op = HINA_STORE_OP_DONT_CARE,
        .depth_clear = 1.0f
    },
});

hina_cmd_bind_pipeline(cmd, pipeline);
hina_cmd_apply_vertex_input(cmd, &vertex_input);
hina_cmd_bind_group(cmd, 0, scene_bind_group);
hina_cmd_draw_indexed(cmd, index_count, 1, 0, 0, 0);

hina_cmd_end_pass(cmd);
```

Under the hood this uses dynamic rendering on 1.3+, or hashes render passes on older versions.

### Commands

The usual draw/dispatch commands:

```c
hina_cmd_draw(cmd, vertex_count, instance_count, first_vertex, first_instance);
hina_cmd_draw_indexed(cmd, index_count, instance_count, first_index, vertex_offset, first_instance);
hina_cmd_draw_indexed_indirect(cmd, indirect_buffer, offset, draw_count, stride);

hina_cmd_dispatch(cmd, groups_x, groups_y, groups_z);
hina_cmd_dispatch_indirect(cmd, indirect_buffer, offset);

hina_cmd_push_constants(cmd, offset, size, &data);
```

Copies and blits:

```c
hina_cmd_copy_buffer(cmd, src, dst, src_offset, dst_offset, size);
hina_cmd_copy_buffer_to_texture(cmd, staging, texture, buffer_offset, mip, layer);
hina_cmd_blit_texture(cmd, src, dst, src_mip, dst_mip, HINA_FILTER_LINEAR);
```

### Bind Groups

Instead of descriptor sets, you work with bind groups (similar to WebGPU):

```c
// Create a layout (done once)
hina_bind_group_layout material_layout = hina_pipeline_get_bind_group_layout(pipeline, 1);

// Create an immutable bind group (long-lived resources)
hina_bind_group_entry entry = {
    .binding = 0,
    .type = HINA_DESC_TYPE_COMBINED_IMAGE_SAMPLER,
    .combined = { .view = texture_view, .sampler = sampler }
};
hina_bind_group material = hina_create_bind_group(&(hina_bind_group_desc){
    .layout = material_layout,
    .entries = &entry,
    .entry_count = 1
});

// Or allocate a transient bind group (per-frame, O(1) bump allocation)
hina_transient_bind_group transforms = hina_alloc_transient_bind_group(transforms_layout);
hina_transient_write_buffer(&transforms, 0, HINA_DESC_TYPE_UNIFORM_BUFFER, ubo, 0, sizeof(UBO));

// Bind during rendering
hina_cmd_bind_group(cmd, 1, material);
hina_cmd_bind_transient_group(cmd, 0, transforms);
```

### Tile Passes (Deferred Rendering)

For tile-based deferred rendering, use tile passes instead of regular passes. Tile passes let you read previous subpass outputs as input attachments without round-tripping through memory—on mobile GPUs this keeps data in tile memory.

I don't love exposing subpasses directly, so this is a simplified abstraction. If your GPU supports `VK_KHR_dynamic_rendering_local_read` (Supported in Vulkan 1.4), it uses that under the hood. Otherwise, it falls back to traditional render passes with subpass dependencies.

```c
hina_tile_pass_desc tile_pass = {
    .label = "deferred",
    // Subpass count is derived automatically (first empty subpass terminates)

    // Subpass 0: G-Buffer fill
    .subpasses[0] = {
        .label = "gbuffer",
        .color = {
            { .image = position_view, .load_op = HINA_LOAD_OP_CLEAR, .store_op = HINA_STORE_OP_DONT_CARE },
            { .image = normal_view,   .load_op = HINA_LOAD_OP_CLEAR, .store_op = HINA_STORE_OP_DONT_CARE },
            { .image = albedo_view,   .load_op = HINA_LOAD_OP_CLEAR, .store_op = HINA_STORE_OP_DONT_CARE },
        },
        .color_count = 3,
        .has_depth = true,
        .depth = { .image = depth_view, .load_op = HINA_LOAD_OP_CLEAR, .store_op = HINA_STORE_OP_DONT_CARE },
    },
    
    // Subpass 1: Composition (reads G-buffer as tile inputs)
    .subpasses[1] = {
        .label = "composition",
        .color = { { .image = swapchain_view, .load_op = HINA_LOAD_OP_CLEAR, .store_op = HINA_STORE_OP_STORE } },
        .color_count = 1,
        .tile_inputs = {
            { .subpass_output_index = 0, .attachment_index = 0 },  // position
            { .subpass_output_index = 0, .attachment_index = 1 },  // normal
            { .subpass_output_index = 0, .attachment_index = 2 },  // albedo
        },
        .tile_input_count = 3,
    },
};

hina_begin_tile_pass(cmd, &tile_pass);
// ... draw G-buffer geometry ...
hina_tile_pass_next(cmd, &tile_pass);
// ... draw fullscreen quad ...
hina_end_tile_pass(cmd);
```

In shaders, use `tile_input` and `tile_load()`:

```glsl
bindings(GBuffer, start=0) {
  tile_input(0) gPosition;
  tile_input(1) gNormal;
  tile_input(2) gAlbedo;
}

// In fragment stage
vec3 pos = tile_load(gPosition).xyz;
vec3 norm = tile_load(gNormal).xyz;
```

The `tile_input(N)` index must match the order in `tile_inputs[]` at runtime. See [the HSL docs](docs/HINA_SHADER_LANGUAGE.md) for full syntax.

### Barriers and Synchronization

**What the library handles automatically:**
- Render pass attachments: layout transitions happen inside `hina_cmd_begin_pass` / `hina_cmd_end_pass`
- Texture creation with `initial_data`: uploaded via staging and transitioned to SHADER_READ
- Mip generation (`hina_generate_mips`): transitions between mip levels during blit chain
- Swapchain acquire/present: handled by `hina_frame_begin` / `hina_frame_submit`
- Descriptor binding: no barriers needed when binding textures/buffers to shaders
- Upload synchronization: `hina_create_bind_group()`, `hina_cmd_apply_vertex_input()`, and indirect draw/dispatch
  automatically wait for pending uploads (see `@section upload_sync` in hina_vk.h for details)

**When you need manual barriers:**
- Compute shader output → graphics read (same queue, different stages)
- Render-to-texture → sample as input (if not using tile passes)
- Any cross-queue resource sharing

```c
// Texture state transition (e.g., after rendering to a texture, before sampling it)
hina_cmd_transition_texture(cmd, texture, HINA_TEXSTATE_SHADER_READ);

// Memory barrier: compute writes an SSBO, then vertex shader reads it
hina_cmd_buffer_barrier(cmd, ssbo,
    HINA_PIPELINE_STAGE_COMPUTE_SHADER, HINA_ACCESS_SHADER_WRITE,
    HINA_PIPELINE_STAGE_VERTEX_SHADER, HINA_ACCESS_SHADER_READ);

// Global memory barrier (all buffers): compute dispatch → indirect draw
hina_cmd_memory_barrier(cmd, 
    HINA_PIPELINE_STAGE_COMPUTE_SHADER, HINA_ACCESS_SHADER_WRITE,
    HINA_PIPELINE_STAGE_DRAW_INDIRECT, HINA_ACCESS_INDIRECT_READ);
```

For cross-queue work (e.g., async compute produces data that graphics consumes), use queue ownership transfers:

```c
// On compute queue: release to graphics
hina_cmd* compute_cmd = hina_cmd_begin_ex(HINA_QUEUE_COMPUTE);
hina_cmd_release_buffer(compute_cmd, ssbo, HINA_QUEUE_GRAPHICS);
hina_sync_point sync = hina_frame_submit(compute_cmd);

// On graphics queue: wait for compute, then acquire
hina_frame_wait(HINA_QUEUE_GRAPHICS, sync);
hina_cmd* gfx_cmd = hina_cmd_begin();
hina_cmd_acquire_buffer(gfx_cmd, ssbo, HINA_QUEUE_COMPUTE);
```

If both queues map to the same hardware queue family (common on some GPUs), release/acquire are no-ops.

### Queries

Timestamp queries for GPU profiling:

```c
hina_query_pool timestamps = hina_make_query_pool(&(hina_query_pool_desc){
    .type = HINA_QUERY_TYPE_TIMESTAMP,
    .count = 16
});

hina_cmd_reset_query_pool(cmd, timestamps, 0, 2);
hina_cmd_write_timestamp(cmd, timestamps, 0, HINA_PIPE_STAGE_TOP_OF_PIPE);
// ... render ...
hina_cmd_write_timestamp(cmd, timestamps, 1, HINA_PIPE_STAGE_BOTTOM_OF_PIPE);

// Read results next frame (don't stall)
uint64_t results[2];
if (hina_get_query_results(timestamps, 0, 2, results, false)) {
    double ms = (results[1] - results[0]) * timestamp_period / 1e6;
}
```

Occlusion queries work similarly with `hina_cmd_begin_query` / `hina_cmd_end_query`.

### Debug Labels

Labels show up in RenderDoc and validation layer messages:

```c
hina_cmd_begin_label(cmd, "Shadow Pass", (float[]){1, 0.5f, 0, 1});
// ... shadow rendering ...
hina_cmd_end_label(cmd);

hina_cmd_insert_label(cmd, "Marker", (float[]){0, 1, 0, 1});
```

Resource labels are set via the `.label` field in creation descriptors.

### Vulkan Escape Hatches

When you need to drop down to raw Vulkan (mesh shaders, vendor extensions, etc.):

```c
hina_vulkan_handles handles;
hina_get_vulkan_handles(&handles);
VkDevice device = handles.device;
VkQueue queue = handles.graphics_queue;

// Get raw handles from HinaVK objects
VkBuffer vk_buf = hina_get_vk_buffer(buffer);
VkImage vk_img = hina_get_vk_image(texture);
VkPipeline vk_pip = hina_get_vk_pipeline(pipeline);
VkCommandBuffer vk_cmd = hina_get_vk_command_buffer(cmd);
```

### Stats

There's a simple stats query if you need it:

```c
hina_frame_stats stats = hina_get_frame_stats();
printf("Frame: %.2f ms, Draw calls: %u, GPU mem: %llu MB\n",
       stats.frame_time_ms, stats.draw_calls, stats.gpu_memory_used / (1024*1024));
```

---

## Building

```bash
cmake -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build --config Release
```

### CMake Options

| Option | Default | Description |
|--------|---------|-------------|
| `HINA_VK_BUILD_EXAMPLES` | ON | Build examples |
| `HINA_VK_ENABLE_TRACY` | ON | Tracy profiler integration |
| `HINA_VK_ENABLE_SHADER_COMPILER` | ON | Runtime HSL/GLSL→SPIR-V (disable to ship pre-compiled) |

### Android

The `android/` directory contains a Gradle project with a subset of examples ported to Android:

- `triangle` - Basic indexed triangle
- `pipelines` - Multiple pipeline variants
- `dynamicuniformbuffer` - Dynamic UBO offsets
- `computeparticles` - GPU particle simulation
- 'deferred' - G-buffer with tile-based composition

Open `android/` in Android Studio to build and run. 

Each example links hina_example.h, which is just C++ using `native_app_glue`. Shaders (`.hina_sl` files) are copied to the APK's assets and compiled at runtime.

The build targets `arm64-v8a` by default (modern 64-bit devices). Edit `android/build.gradle` to change:

```groovy
abiFilters = ["arm64-v8a"]  // Add "armeabi-v7a" for older 32-bit devices
```

**Requirements:** Android Studio, NDK r25+, API 24+ (Vulkan minimum)

### Using in Your Project

The library is just a few files:

```
include/hina_vk.h      # public API
src/hina_vk.c          # implementation
src/hina_vk_impl.cpp   # volk/VMA instantiation
```

**Option 1: Full repo** (clone with submodules)

```cmake
add_subdirectory(hina-vk)
target_link_libraries(my_app PRIVATE hina-vk)
```

**Option 2: Embedded** (just the core files)

If you already have volk, VMA, etc. in your project, copy the core files plus `spirv_reflect.c` into `src/`:

```
your_project/
├── hina-vk/
│   ├── CMakeLists.txt
│   ├── include/hina_vk.h
│   └── src/
│       ├── hina_vk.c
│       ├── hina_vk_impl.cpp
│       └── spirv_reflect.c  # copy from SPIRV-Reflect
```

HinaVK auto-detects whether `third_party/` exists. If not, it assumes your project provides the necessary headers (volk, VMA, Vulkan-Headers) through normal include paths. Just make sure the targets exist before including HinaVK:

```cmake
# Your project already has these from vcpkg, conan, or add_subdirectory
find_package(volk CONFIG REQUIRED)
find_package(VulkanMemoryAllocator CONFIG REQUIRED)
# ... or however you get them

add_subdirectory(hina-vk)
target_link_libraries(my_app PRIVATE hina-vk)
```

If you want runtime shader compilation, provide the mentioned dependencies. Otherwise, set `HINA_VK_ENABLE_SHADER_COMPILER=OFF`.

---

## Examples

Most of these are ports of [Sascha Willems' Vulkan examples](https://github.com/SaschaWillems/Vulkan):

| Example | Description |
|---------|-------------|
| `triangle` | Indexed triangle with MVP transform |
| `texture` | KTX texture loading, mipmaps |
| `descriptorsets` | Bind groups, per-object uniforms, 4x MSAA |
| `pushconstants` | Push constant usage |
| `dynamicuniformbuffer` | Dynamic UBO offsets |
| `specializationconstants` | Shader specialization |
| `pipelines` | Multiple pipeline variants (phong, toon, wireframe) |
| `offscreen` | Render-to-texture, MSAA resolve |
| `stencilbuffer` | Stencil-based outline rendering |
| `deferred` | G-buffer, tile-based composition with input attachments |
| `computeparticles` | GPU particle simulation |
| `terraintessellation` | Tessellation shaders |
| `viewportarray` | Multi-viewport rendering |
| `gltf_renderer` | glTF 2.0 PBR renderer with GPU culling |

---

## Initialization

```c
hina_desc desc = hina_desc_default();
desc.native_window = hwnd;
desc.flags = HINA_INIT_VALIDATION_BIT;  // Enable validation layers
hina_init(&desc);
```

| Flag | Description |
|------|-------------|
| `HINA_INIT_VALIDATION_BIT` | Enable Vulkan validation layers (debug builds) |
| `HINA_INIT_TERMINATE_ON_ERROR_BIT` | Abort on Vulkan errors instead of returning failure |
| `HINA_INIT_PREFER_INTEGRATED_BIT` | Prefer integrated GPU over discrete (power savings) |

### Pipeline Cache

You can save/load the pipeline cache to speed up startup:

```c
// Save on shutdown
size_t size;
void* data = hina_get_pipeline_cache_data(&size);
save_to_disk("pipeline.cache", data, size);
free(data);

// Load on init
hina_desc desc = hina_desc_default();
desc.pipeline_cache.data = load_from_disk("pipeline.cache", &desc.pipeline_cache.size);
hina_init(&desc);
```

---

## Platform Support

| Platform | Status |
|----------|--------|
| Windows | ✓ Primary development |
| Android | ✓ Tested on Xiaomi Redmi Note 8 Pro |
| Linux | Not yet tested |
| macOS | Not yet tested |

### Vulkan Version

Works with Vulkan 1.0 and doesn't require any extensions. Advanced features are used when available:

- **Dynamic rendering** (VK 1.3+): Skip render pass/framebuffer boilerplate
- **Dynamic rendering local read** (VK 1.4+ or extension): Tile-based deferred rendering via `tile_input`/`tile_load` in shaders

On older Vulkan versions, equivalent functionality is provided through render passes and subpasses.

You can cap the API version for testing:

```c
desc.max_api_version = HINA_VK_VERSION_1_0;  // Force Vulkan 1.0 path
```

### Tested Toolchains

| Toolchain | Version | Notes |
|-----------|---------|-------|
| MSVC | 2022 (v143) | Primary development |
| GCC | 13+ (MinGW-w64) | Windows cross-compile |
| Clang | NDK r26+ | Android builds |

**Requirements:** C11, C++17 (for volk/VMA implementation files)

---

## Dependencies

All included as git submodules in `third_party/`.

### Core Library

| Dependency | Usage |
|------------|-------|
| [Vulkan-Headers](https://github.com/KhronosGroup/Vulkan-Headers) | API definitions (headers only) |
| [volk](https://github.com/zeux/volk) | Vulkan loader (compiled into library) |
| [VMA](https://github.com/GPUOpen-LibrariesAndSDKs/VulkanMemoryAllocator) | Memory allocation (headers only) |

### Core Library (Optional)

| Dependency | Usage | CMake Flag |
|------------|-------|------------|
| [glslang](https://github.com/KhronosGroup/glslang) | Runtime GLSL→SPIR-V | `HINA_VK_ENABLE_SHADER_COMPILER=ON` |
| [SPIRV-Tools](https://github.com/KhronosGroup/SPIRV-Tools) | SPIR-V optimizer (for glslang) | `HINA_VK_ENABLE_SHADER_COMPILER=ON` |
| [SPIRV-Reflect](https://github.com/KhronosGroup/SPIRV-Reflect) | Shader reflection | `HINA_VK_ENABLE_SHADER_COMPILER=ON` |
| [Tracy](https://github.com/wolfpld/tracy) | GPU/CPU profiling | `HINA_VK_ENABLE_TRACY=ON` |

### Examples Only

| Dependency | Usage |
|------------|-------|
| [SDL2](https://github.com/libsdl-org/SDL) | Windowing, input |
| [glm](https://github.com/g-truc/glm) | Math |
| [tinygltf](https://github.com/syoyo/tinygltf) | glTF loading |
| [Dear ImGui](https://github.com/ocornut/imgui) | Debug UI overlay |
| [KTX](https://github.com/KhronosGroup/KTX-Software) | Texture loading (minimal subset) |

---

## License

MIT License. See [LICENSE](LICENSE).
