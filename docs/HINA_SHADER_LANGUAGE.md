# Hina Shader Language (HSL)

---

## 0. Status, Scope, and Reading Guide

HSL is a GLSL 450 superset with WGSL-inspired syntax for defining shader modules.
The key principle is: **compiler owns the pipeline interface, author writes shader logic**.

Scope and backend:
- Current implementation in `src/hina_vk.c`
- GLSL 450 source generated, compiled with glslang.
- SPIR-V output targets Vulkan 1.0 / SPIR-V 1.0.

Reading guide:
- If you want to write a shader now: read sections 1, 4.1-4.4, and 5.2.
- If you want to use a specific feature: read section 4 and 5.
- If you are debugging output or compiler behavior: read sections 7 and 8.

---

## 1. 5-Minute Tutorial

If you are new to HSL, follow this order:
1. Start a single `#hina ... #hina_end` header block.
2. Declare bind groups (`group`) and resources (`bindings` or `binding`).
3. Declare the IO structs with the exact names `VertexIn`, `Varyings`, `FragOut`.
4. Optionally add custom structs or `snippet` blocks for helper functions.
5. Add one or more `#hina_stage ... #hina_end` blocks with your GLSL code.
6. Compile and fix GLSL errors (stage bodies are regular GLSL).

Minimal template:
```glsl
#hina
group Scene = 0;
bindings(Scene, start=0) {
  uniform(std140) UBO { mat4 mvp; } ubo;
}
struct VertexIn { vec3 a_position; };
struct Varyings { vec2 uv; };
struct FragOut  { vec4 color; };
snippet Common {
  vec2 pack_uv(vec3 p) { return p.xy; }
}
#hina_end

#hina_stage vertex entry VSMain use Common
Varyings VSMain(VertexIn in) {
  Varyings out;
  gl_Position = ubo.mvp * vec4(in.a_position, 1.0);
  out.uv = pack_uv(in.a_position);
  return out;
}
#hina_end

#hina_stage fragment entry FSMain use Common
FragOut FSMain(Varyings in) {
  FragOut out;
  out.color = vec4(in.uv, 0.0, 1.0);
  return out;
}
#hina_end
```

Checklist:
- Graphics modules require both vertex and fragment stages.
- Compute modules must not include graphics stages.
- `VertexIn`, `Varyings`, `FragOut` names are fixed.

---

## 2. Mental Model

An HSL file is:
- One header region with declarations — either an explicit `#hina ... #hina_end` block, or an **implicit header** (everything before the first `#hina_stage`).
- One or more stage blocks (`#hina_stage ... #hina_end`) with GLSL bodies.

Compiler responsibilities:
- Assign IO locations based on struct field order.
- Assign bindings in `bindings(...)` blocks.
- Generate interface variables and `main()` wrappers.
- Expand `#include` and inject `#line` for error mapping.
- Rewrite entry parameter names (`in`/`out`) and `tile_load(...)` calls.

Author responsibilities:
- Declare IO structs, resources, and entry signatures correctly.
- Write valid GLSL inside stage bodies.

---

## 3. File Layout and Preprocessing

### 3.1 Directive Scanning

- Directives are recognized only at the start of a line (after whitespace).
- Directives are ignored inside comments or string/char literals.
- Only the first `#hina` header is recognized; later headers are ignored.
- **Implicit header mode**: if no `#hina` block is present but `#hina_stage` blocks exist, everything from the start of the file to the first `#hina_stage` line is treated as the header. This allows `#include` directives at the top of the file (the natural position for includes).
- Stage blocks are delimited by `#hina_stage` and `#hina_end`; nested stages are not recognized.

### 3.2 Include Expansion

`#include "path"` is expanded in both the `#hina` header block and stage bodies. Header-block includes are expanded before HSL parsing (enabling shared struct, group, binding, and snippet definitions across files). Stage-body includes are expanded during per-stage GLSL compilation.

Details (as implemented by the compiler):
- Only the quoted form is recognized (`#include "path"`).
- Paths are resolved relative to the including file by the default loader.
- Includes are expanded recursively with `#line` directives for accurate filename/line reporting.
- Circular includes cause a **compile error** with message: "Circular include detected: <filename>".
- Include depth is capped at 32; exceeding this causes a **compile error** with message: "Maximum include depth exceeded".
- The include pass does not track comments or strings; it just looks for a line starting with `#include`.
- `#pragma once` is supported: if the first non-blank line of an included file is `#pragma once`, the file is only included once per compilation. Subsequent `#include` directives for the same resolved path are silently skipped. Traditional `#ifndef` include guards also work (handled by glslang's preprocessor). Without either mechanism, double-including a file causes GLSL redefinition errors.

#### When to use includes

Includes are for **shared declarations across separate `.hina_sl` files** — things like common UBO layouts, utility functions, or shared constants. With implicit header mode, `#include` can go at the top of the file (the natural position), and the included content can contain HSL declarations (groups, bindings, structs, snippets).

Implicit header example — `#include` at the top, no `#hina` block needed:
```glsl
// shaders/simple.hina_sl
#include "shared_types.glsl"

struct VertexIn { vec3 a_position; vec2 a_uv; };
struct Varyings { vec2 uv; };
struct FragOut  { vec4 color; };

#hina_stage vertex entry VSMain
Varyings VSMain(VertexIn in) {
    Varyings out;
    gl_Position = ubo.data.mvp * vec4(in.a_position, 1.0);
    out.uv = in.a_uv;
    return out;
}
#hina_end

#hina_stage fragment entry FSMain
FragOut FSMain(Varyings in) {
    FragOut out;
    out.color = ubo.data.color;
    return out;
}
#hina_end
```

Includes also expand inside stage bodies for raw GLSL sharing (not HSL declarations).

Common patterns:
- Shared struct/UBO definitions used by multiple shader files
- GLSL utility functions (noise, tonemapping, BRDF) used across materials
- Engine-wide constants or `#define` macros

Worked example — sharing a UBO layout across shaders:

```glsl
// common/scene_ubo.glsl
#pragma once

struct SceneData {
    mat4 viewProj;
    vec4 cameraPos;
    vec4 lightDir;
};

layout(set = 0, binding = 0) uniform SceneUBO {
    SceneData scene;
} ubo;
```

```glsl
// shaders/lit.hina_sl
#hina
struct VertexIn { vec3 a_pos; vec3 a_normal; };
struct Varyings { vec3 normal; };
struct FragOut  { vec4 color; };
#hina_end

#hina_stage vertex entry VSMain
#include "common/scene_ubo.glsl"
Varyings VSMain(VertexIn in) {
    Varyings out;
    out.normal = in.a_normal;
    gl_Position = ubo.scene.viewProj * vec4(in.a_pos, 1.0);
    return out;
}
#hina_end

#hina_stage fragment entry FSMain
#include "common/scene_ubo.glsl"
FragOut FSMain(Varyings in) {
    FragOut out;
    float NdotL = max(dot(normalize(in.normal), ubo.scene.lightDir.xyz), 0.0);
    out.color = vec4(vec3(NdotL), 1.0);
    return out;
}
#hina_end
```

Note that the same file is included in both stages. Each stage is compiled independently, so both need the UBO declaration. The `#pragma once` directive ensures the file is only expanded once per stage compilation, preventing redefinition errors if included through multiple paths.

**Custom include loader:** The low-level `hslc_compile()` API accepts a `load_include_fn` callback for custom path resolution (e.g., loading from archives or virtual filesystems). When `NULL`, the default loader resolves paths relative to the including file using standard file I/O.

### 3.3 Opaque GLSL Regions

- Stage bodies are copied as-is and are not parsed by HSL.
- Resource block bodies are copied as raw GLSL; they are not parsed by HSL.
- Snippet bodies and block bodies use brace matching that skips comments/strings.

---

## 4. Header Block Reference (`#hina ... #hina_end` or implicit)

### 4.1 Bind Groups (Descriptor Sets)

Define bind groups (descriptor sets):

```glsl
group Scene = 0;
group Material = 1;
```

Rules:
- Names and set indices must be unique.
- Max groups: 4 (Vulkan 1.0 minimum).
- Set index must be in `[0, 3]`.

### 4.2 Binding Syntax

Auto-assign bindings within a group:

```glsl
bindings(Scene, start=0) {
  uniform(std140) SceneUBO { mat4 viewProj; } scene;
  texture sampler2D albedo;
}
```

Or explicit binding:

```glsl
binding(Material, 2) uniform(std140) MatUBO { ... } mat;
```

Rules:
- `start` and explicit binding indices must be in `[0, 31]`.
- Auto-binding stops with an error if bindings exceed per-group limit (32).
- Binding indices must be unique per set.
- Resource names must be unique across the module.

### 4.3 Resource Kinds

Syntax:
- `uniform(std140|scalar) Block { members } name;` - Uniform buffer
- `buffer(readonly|readwrite[, coherent][, volatile][, restrict]) Block { members } name;` - Storage buffer (always std430 layout)
- `texture <SamplerType> name;` - Combined image sampler (sampler2D, samplerCube, etc.)
- `sampler name;` - Sampler only (for use with separate textures)
- `image(format) <ImageType> name;` - Storage image
- `tile_input(N) name;` - Input attachment for tile-based rendering (fragment only)

Rules and notes:
- `uniform(scalar)` enables `GL_EXT_scalar_block_layout` extension automatically. Requires device support for `VK_EXT_scalar_block_layout`.
- `buffer` defaults to `readonly` when no access qualifier is provided.
- `buffer` always uses `std430` layout (hardcoded by the compiler).
- `tile_input` resources are only emitted for fragment stages; using `tile_load()` in other stages will cause a GLSL compile error.
- `tile_input(N)` index must be in range `[0, 3]`.
- Resource block bodies are copied as raw GLSL; custom structs can be referenced inside them.
- Custom structs must be declared **before** the resource blocks that reference them (the compiler emits them in declaration order).

Sampler-only usage (separated samplers):
```glsl
bindings(Scene, start=0) {
  texture texture2D myTex;     // Texture without sampler
  sampler mySampler;           // Sampler only
}

// In shader code, combine them:
vec4 col = texture(sampler2D(myTex, mySampler), uv);
```

Storage image usage:
```glsl
bindings(Compute, start=0) {
  image(rgba8) image2D outputImg;   // Format goes in parentheses
  image(r32f) image2D depthImg;
}

// In shader code:
imageStore(outputImg, ivec2(gl_GlobalInvocationID.xy), vec4(1.0));
float d = imageLoad(depthImg, coord).r;
```

Valid image formats include: `rgba8`, `rgba16f`, `rgba32f`, `r32f`, `r32i`, `r32ui`, etc. (standard GLSL image format qualifiers).
If no format is specified, the compiler emits the image without a format qualifier (may require `shaderStorageImageReadWithoutFormat` capability).

Tile input (worked example):
```glsl
// Declaration - N is the input_attachment_index
bindings(GBuffer, start=0) {
  tile_input(0) gPosition;
  tile_input(1) gNormal;
  tile_input(2) gAlbedo;
}

// Usage in fragment shader
vec4 pos = tile_load(gPosition);
vec4 norm = tile_load(gNormal);
```

Generated GLSL:
- `tile_input(N) name;` -> `layout(input_attachment_index = N, set = S, binding = B) uniform subpassInput name;`
- `tile_load(name)` -> `subpassLoad(name)`

The `input_attachment_index` (N) must match the index in the runtime's `tile_inputs[]` array when setting up the tile pass. For example, if `tile_input(0) gPosition` is declared, then `tile_inputs[0]` at runtime must reference the attachment that gPosition should read from.

<!-- TODO: Consider auto-assigning input_attachment_index by declaration order (like bindings) to simplify syntax to just `tile_input gPosition;` -->

### 4.4 IO Structs and Location Assignment

Vertex inputs, varyings, and fragment outputs are declared as structs:

```glsl
struct VertexIn {
  vec3 a_position;
  vec2 a_uv;
  @flat uint instance_id;  // interpolation qualifier
};

struct Varyings {
  vec2 uv;
  vec3 world_pos;
  @flat uint material_id;
};

struct FragOut {
  vec4 color;
  vec4 normal;  // Multiple outputs supported
};
```

Interpolation qualifiers:
- `@flat` - Disable interpolation; required for integer types
- `@noperspective` - Linear interpolation in screen space (no perspective correction)
- `@centroid` - Sample at centroid of covered pixels (helps with MSAA edge artifacts)
- `@sample` - Per-sample interpolation (enables per-sample shading)

Rules:
- `@flat` cannot be combined with other interpolation qualifiers.
- Other qualifiers can be combined (e.g. `@noperspective @centroid`).
- Locations are assigned automatically in field declaration order.

Allowed types:
- `float`, `int`, `uint`, `bool`
- `vec2/3/4`, `ivec2/3/4`, `uvec2/3/4`, `bvec2/3/4`
- `mat2`, `mat3`, `mat4`, `mat2x3`, `mat3x2`, `mat2x4`, `mat4x2`, `mat3x4`, `mat4x3`
- Fixed-size arrays: `vec4[4]`, `mat4[2]`

**Note:** Double-precision types (`double`, `dvec*`, `dmat*`) are not validated by the slot counter and may fail during GLSL compilation if unsupported.

Slot counts:
- scalar, vec2, vec3, vec4: 1 slot
- mat2: 2 slots, mat3: 3 slots, mat4: 4 slots
- mat2x3/mat3x2: 2/3 slots respectively (column count)
- mat2x4/mat4x2: 2/4 slots respectively
- mat3x4/mat4x3: 3/4 slots respectively
- T[N]: slots(T) * N

Max locations: 16 (Vulkan 1.0 minimum)

Beginner notes:
- The names `VertexIn`, `Varyings`, and `FragOut` are fixed and are used by the compiler to generate interface variables.
- `VertexIn` is optional: omit it for procedural vertex shaders that use only `gl_VertexIndex` (e.g., fullscreen triangles).
- `Varyings` is optional: omit it for shaders that don't need inter-stage communication (e.g., depth-only or shadow passes).
- Any other struct name is treated as a custom data struct and can be used in resource blocks or stage code.
- **Tessellation/geometry modules:** Do not declare `VertexIn`, `Varyings`, or `FragOut` when tessellation or geometry stages are present. Use raw GLSL `layout(location = N) in/out` declarations in each stage body instead. See §5.2.1.

Worked example (slot counting):
```glsl
struct Varyings {
  vec2 uv;         // slot 0
  mat4 basis;      // slots 1-4
  vec4 weights[2]; // slots 5-6
};
// Total slots = 7 (must be <= 16)
```

### 4.5 Push Constants

```glsl
push_constant Push { mat4 model; vec4 tint; } pc;
```

Rules:
- Only one push constant block per file.
- Max size: 128 bytes.

### 4.6 Specialization Constants

```glsl
spec_const(0) uint MAX_LIGHTS = 16;
spec_const(1) bool ENABLE_SHADOWS = true;
```

Max count: 16

### 4.7 Snippets (Shared Code)

```glsl
snippet Lighting {
  vec3 calc_light(vec3 n, vec3 l) { return max(dot(n, l), 0.0) * vec3(1.0); }
}
```

Notes:
- Snippets are stored in the header and only emitted into stages that list them via `use`.
- You can list multiple snippets: `use Common, Lighting, Noise`.
- **`use *`** includes all defined snippets in definition order. This is convenient when most stages need everything and you don't want to maintain an explicit list.
- Snippets are emitted in the order they appear in the `use` list (or definition order for `use *`), **not** topologically sorted.
  - If snippet A calls functions from snippet B, list B before A: `use B, A`. With `use *`, definition order is used automatically.
- Limits: 16 snippets per file, and up to 8 snippets used per stage (no limit for `use *`).

#### When to use snippets

Snippets are for **GLSL helper functions selectively injected into stages via `use`**. Snippets live in the `#hina` header block and each stage only pays for the code it actually uses. Snippet definitions can be shared across files via `#include` in the `#hina` block (see §3.2).

Common patterns:
- Math utilities (remap, saturate, pack/unpack) used by both VS and FS in the same file
- Lighting functions shared between a forward pass and a debug visualization pass
- Noise or procedural functions used in multiple stages of the same shader

Worked example — layered snippets with dependencies:

```glsl
#hina
group Scene = 0;
bindings(Scene, start=0) {
  uniform(std140) UBO { vec4 lightDir; vec4 cameraPos; } ubo;
}
struct VertexIn { vec3 a_pos; vec3 a_normal; vec2 a_uv; };
struct Varyings { vec3 normal; vec3 worldPos; vec2 uv; };
struct FragOut  { vec4 color; };

snippet Math {
  float saturate(float x) { return clamp(x, 0.0, 1.0); }
  float remap(float v, float lo, float hi) { return lo + v * (hi - lo); }
}

snippet Lighting {
  // Depends on Math — list Math before Lighting in `use`
  vec3 blinn_phong(vec3 N, vec3 L, vec3 V, float shininess) {
    float diff = saturate(dot(N, L));
    vec3 H = normalize(L + V);
    float spec = pow(saturate(dot(N, H)), shininess);
    return vec3(diff + spec);
  }
}
#hina_end

#hina_stage vertex entry VSMain
Varyings VSMain(VertexIn in) {
    Varyings out;
    out.normal = in.a_normal;
    out.worldPos = in.a_pos;
    out.uv = in.a_uv;
    gl_Position = vec4(in.a_pos, 1.0);
    return out;
}
#hina_end

#hina_stage fragment entry FSMain use Math, Lighting
FragOut FSMain(Varyings in) {
    FragOut out;
    vec3 N = normalize(in.normal);
    vec3 L = normalize(ubo.lightDir.xyz);
    vec3 V = normalize(ubo.cameraPos.xyz - in.worldPos);
    out.color = vec4(blinn_phong(N, L, V, 64.0), 1.0);
    return out;
}
#hina_end
```

Key points in this example:
- `Math` is listed before `Lighting` in the `use` clause because `Lighting` calls `saturate()` from `Math`.
- The vertex stage doesn't `use` either snippet — it has no need for lighting math, so neither snippet's code is compiled into the VS.
- Snippet bodies can reference resources declared in the header (like `ubo`) since they're injected into the stage's GLSL before compilation.

#### Snippets vs includes: when to use which

| | Snippets | Includes |
|---|---|---|
| **Scope** | Selective per-stage injection | Cross-file text sharing |
| **Where declared** | Inside the `#hina` header block | External `.glsl` files |
| **Where expanded** | Into stages that list them via `use` | Into `#hina` blocks or stage bodies via `#include` |
| **Selective injection** | Yes — only stages with `use` get the code | No — every `#include` always expands |
| **Can reference HSL resources** | Yes — snippets see `ubo`, `pc`, etc. | In header: must use HSL syntax. In stage bodies: raw GLSL |
| **Include guards needed** | No | Yes (`#pragma once` or `#ifndef`) if included multiple times |
| **Use case** | Helper functions for specific stages | Shared UBO layouts, engine-wide utilities, cross-file snippet libraries |

Snippets and includes **compose**: you can `#include` a file containing snippet definitions inside the `#hina` block, then `use` those snippets in individual stages. This enables cross-file snippet libraries:

```glsl
// lighting_lib.glsl
#pragma once
snippet Lighting {
    vec3 calc_diffuse(vec3 N, vec3 L, vec3 color) {
        return max(dot(N, L), 0.0) * color;
    }
}
```

```glsl
// my_shader.hina_sl
#hina
#include "lighting_lib.glsl"
struct VertexIn { vec3 a_position; vec3 a_normal; };
struct Varyings { vec3 normal; };
struct FragOut { vec4 color; };
#hina_end

#hina_stage fragment entry FSMain use Lighting
FragOut FSMain(Varyings in) {
    FragOut out;
    out.color = vec4(calc_diffuse(in.normal, vec3(0,1,0), vec3(1)), 1.0);
    return out;
}
#hina_end
```

### 4.8 Shared Memory (Compute Only)

```glsl
shared float data[256];
shared vec4 tile[32][32];
```

Rules:
- Only allowed when a compute stage is present.
- Only scalar/vector/matrix types and arrays of them (no custom structs).
- Total shared memory usage must be <= 16384 bytes.
- Double-precision types are not supported.

Validation size assumptions:
- float/int/uint/bool: 4 bytes
- vec2/ivec2/uvec2/bvec2: 8 bytes
- vec3/ivec3/uvec3/bvec3: 12 bytes, arrays use 16-byte stride
- vec4/ivec4/uvec4/bvec4: 16 bytes
- matNxM: N columns of 16 bytes (mat2=32, mat3=48, mat4=64)

---

## 5. Stage Blocks

### 5.1 Stage Header Syntax

```glsl
#hina_stage <kind> entry <Name> [early_fragment_tests] [depth_less|depth_greater|depth_any|depth_unchanged] [use <SnippetList>]
    // GLSL body (opaque, not parsed by HSL)
#hina_end
```

Supported stages:
- `vertex`
- `tess_control` (alias: `tesc`)
- `tess_eval` (alias: `tese`)
- `geometry` (alias: `geom`)
- `fragment` (alias: `frag`)
- `compute` (alias: `comp`)

Notes:
- Fragment qualifiers are fragment-only.
- Qualifiers can appear in any order but must come before `use`.

### 5.2 Entry Function Signatures

Vertex (with vertex input):
```glsl
Varyings VSMain(VertexIn in) {
  Varyings out;
  gl_Position = ...;  // Write gl_Position in body
  out.uv = in.a_uv;
  return out;
}
```

Vertex (procedural - no vertex input):
```glsl
// Omit VertexIn struct declaration entirely
Varyings VSMain() {
  Varyings out;
  // Use gl_VertexIndex for procedural geometry (e.g., fullscreen triangles)
  out.uv = vec2((gl_VertexIndex << 1) & 2, gl_VertexIndex & 2);
  gl_Position = vec4(out.uv * 2.0 - 1.0, 0.0, 1.0);
  return out;
}
```

Vertex (no varyings - for depth/shadow passes):
```glsl
// Omit Varyings struct declaration entirely
void VSMain(VertexIn in) {
  gl_Position = ubo.mvp * vec4(in.a_position, 1.0);
}
```

Fragment (with varyings input):
```glsl
FragOut FSMain(Varyings in) {
  FragOut out;
  out.color = vec4(in.uv, 0.0, 1.0);
  return out;
}
```

Fragment (no varyings - for depth/shadow passes):
```glsl
// Omit Varyings struct declaration entirely
FragOut FSMain() {
  FragOut out;
  out.color = vec4(1.0);  // Solid color output (e.g., for shadow maps)
  return out;
}
```

### 5.2.2 Vertex-Only Modules (Depth Pre-Pass)

HSL supports vertex-only modules for depth pre-pass and similar use cases where no fragment shader is needed:

```glsl
#hina
group Scene = 0;
bindings(Scene, start=0) {
  uniform(std140) UBO { mat4 mvp; } ubo;
}
struct VertexIn { vec3 a_position; };
#hina_end

#hina_stage vertex entry VSMain
void VSMain(VertexIn in) {
    gl_Position = ubo.mvp * vec4(in.a_position, 1.0);
}
#hina_end
```

When creating the pipeline, set `color_attachment_count = 0` and configure only depth/stencil attachments.

Compute:
```glsl
#hina_stage compute entry CSMain
layout(local_size_x = 8, local_size_y = 8, local_size_z = 1) in;

void CSMain() {
  uvec3 id = gl_GlobalInvocationID;
  // Compute shader code
}
#hina_end
```

**Important:** HSL does not have special syntax for compute workgroup size. Declare it in the stage body using standard GLSL `layout(local_size_x=..., ...)` syntax.

### 5.2.1 Tessellation and Geometry Stages

Tessellation and geometry stages use raw GLSL builtins rather than the struct-based IO used by vertex/fragment. Varyings from the vertex stage are available as arrays with the generated names (e.g., `_hina_v2f_in_uv[]`).

**Tessellation control** (patch subdivision control):
```glsl
#hina_stage tess_control entry TCSMain
layout(vertices = 3) out;  // Output 3 control points per patch

// Per-vertex outputs (arrayed)
out vec3 tcs_worldPos[];
out vec2 tcs_uv[];

void TCSMain() {
  uint id = gl_InvocationID;
  
  // Pass through vertex data to tessellation evaluation
  tcs_worldPos[id] = _hina_v2f_in_worldPos[id];
  tcs_uv[id] = _hina_v2f_in_uv[id];
  
  // Set tessellation levels (only needs to run once per patch)
  if (id == 0) {
    float tessLevel = 4.0;
    gl_TessLevelOuter[0] = tessLevel;
    gl_TessLevelOuter[1] = tessLevel;
    gl_TessLevelOuter[2] = tessLevel;
    gl_TessLevelInner[0] = tessLevel;
  }
}
#hina_end
```

**Tessellation evaluation** (vertex generation):
```glsl
#hina_stage tess_eval entry TESMain
layout(triangles, equal_spacing, ccw) in;

// Inputs from tess control (arrayed)
in vec3 tcs_worldPos[];
in vec2 tcs_uv[];

// Outputs to fragment (or geometry)
out vec3 tes_worldPos;
out vec2 tes_uv;

void TESMain() {
  vec3 bary = gl_TessCoord;  // Barycentric coordinates
  
  // Interpolate using barycentric coordinates
  tes_worldPos = bary.x * tcs_worldPos[0] + 
                 bary.y * tcs_worldPos[1] + 
                 bary.z * tcs_worldPos[2];
  tes_uv = bary.x * tcs_uv[0] + 
           bary.y * tcs_uv[1] + 
           bary.z * tcs_uv[2];
  
  gl_Position = ubo.viewProj * vec4(tes_worldPos, 1.0);
}
#hina_end
```

**Geometry** (primitive processing):
```glsl
#hina_stage geometry entry GSMain
layout(triangles) in;
layout(triangle_strip, max_vertices = 3) out;

// Inputs from vertex/tess_eval (arrayed, 3 vertices for triangles)
in vec3 gs_in_worldPos[];
in vec2 gs_in_uv[];

// Outputs to fragment
out vec3 gs_worldPos;
out vec2 gs_uv;
out vec3 gs_normal;

void GSMain() {
  // Compute face normal
  vec3 edge1 = gs_in_worldPos[1] - gs_in_worldPos[0];
  vec3 edge2 = gs_in_worldPos[2] - gs_in_worldPos[0];
  vec3 faceNormal = normalize(cross(edge1, edge2));
  
  // Emit triangle with computed normal
  for (int i = 0; i < 3; i++) {
    gs_worldPos = gs_in_worldPos[i];
    gs_uv = gs_in_uv[i];
    gs_normal = faceNormal;
    gl_Position = gl_in[i].gl_Position;
    EmitVertex();
  }
  EndPrimitive();
}
#hina_end
```

**Important:** When using tessellation or geometry stages, you **must** define your own input/output interface variables using raw GLSL `layout(location = N) in/out` declarations. Do not declare `VertexIn`, `Varyings`, or `FragOut` structs in the `#hina` header — the codegen emits non-arrayed output variables, but GLSL requires TCS per-vertex outputs to be unsized arrays (e.g., `out vec3 pos[]`). Using struct-based IO with tessellation stages will cause a GLSL compile error.

### 5.3 Fragment Qualifiers

```glsl
#hina_stage fragment entry FSMain early_fragment_tests depth_greater
```

Supported qualifiers:
- `early_fragment_tests`
- `depth_less`
- `depth_greater`
- `depth_any`
- `depth_unchanged`

### 5.4 Snippet Usage in Stages

Named snippets:
```glsl
#hina_stage fragment entry FSMain use Lighting, Noise
FragOut FSMain(Varyings in) {
  FragOut out;
  vec3 normal = normalize(in.world_pos);  // Example usage
  out.color = vec4(calc_light(normal, vec3(0, 1, 0)), 1.0);
  return out;
}
#hina_end
```

Wildcard (`use *`) — includes all snippets in definition order:
```glsl
#hina_stage vertex entry VSMain use *
Varyings VSMain(VertexIn in) {
  Varyings out;
  out.color = shade(vec3(in.a_pos, 0.0));  // All snippets available
  gl_Position = vec4(in.a_pos, 0.0, 1.0);
  return out;
}
#hina_end
```

---

## 6. Validation Rules and Limits

Stage composition rules:
- Graphics modules require both vertex and fragment stages.
- Cannot mix compute with graphics stages.
- Stage blocks cannot be nested.

Resource limits (Vulkan 1.0 minimum):
- Max groups (sets): 4
- Max bindings per group: 32
- Max total bindings: 128
- Max UBOs per stage: 12
- Max SSBOs per stage: 4
- Max sampled images per stage: 16
- Max storage images per stage: 4
- Max push constant size: 128 bytes
- Max location slots: 16

Compiler limits (HSL parser):
- Max structs per file: 8 (includes IO and custom structs)
- Max fields per struct: 32
- Max snippets per file: 16
- Max snippets used per stage: 8
- Max shared memory declarations: 32
- Max shared array dimensions: 4
- Max total shared memory: 16384 bytes
- Max identifier length: 64 characters
- Max type name length: 32 characters
- Max UBO/SSBO block body length: 1024 characters

---

## 7. Code Rewriting and Generated Output

### 7.1 Rewrites

The compiler performs limited text rewrites inside stage bodies:
- Entry parameter names `in` and `out` are rewritten to `_in` and `_out`.
- `tile_load(...)` calls are rewritten to `subpassLoad(...)`.

### 7.2 Compilation Output

The HSL compiler generates:
- GLSL 450 with `#version 450` preamble
- `#extension GL_EXT_scalar_block_layout : require` (when any uniform uses `scalar` layout)
- SPIR-V via glslang (Vulkan 1.0 client, SPIR-V 1.0)

SPIRV-Reflect is used to extract:
- Descriptor bindings
- Specialization constants
- Vertex inputs
- Push constant ranges

### 7.3 Generated Variable Names

For debugging and stable reflection:
- Vertex input vars: `_hina_vi_<field>[_<col>]`
- Varyings out: `_hina_v2f_out_<field>[_<col>]`
- Varyings in: `_hina_v2f_in_<field>[_<col>]`
- Fragment out: `_hina_fo_<field>`

The `_<col>` suffix appears for matrix types (one variable per column).

---

## 8. Troubleshooting and Gotchas

- Directives must start at the beginning of a line (after whitespace).
- `#hina`/`#hina_end` is optional. If omitted, everything before the first `#hina_stage` is treated as the header (implicit header mode). Explicit `#hina` blocks remain fully supported.
- `#include` works in header blocks (explicit or implicit) and stage bodies. Included files used in the header must contain valid HSL declarations (structs, groups, bindings, snippets) — not raw GLSL `layout(...)` qualifiers.
- `#include` expansion does not track comments or strings; avoid commenting out includes by prefixing with `//` on the same line.
- Use `#pragma once` or `#ifndef` include guards to prevent double-inclusion errors.
- Stage bodies and resource block bodies are copied as raw GLSL; GLSL errors come from those bodies, not the HSL parser.
- IO struct type names are not validated beyond slot counting; invalid types will fail during GLSL compilation.
- `use` must appear after any stage qualifiers. `use *` includes all snippets; it cannot be combined with named snippets.
- Shared declarations require a compute stage.
- Tessellation control/eval pairing is enforced at pipeline creation (HSL compilation does not check this).
- Using `VertexIn`/`Varyings`/`FragOut` structs with tessellation or geometry stages will cause GLSL compile errors due to non-arrayed TCS output generation. Use raw GLSL IO declarations instead (see §5.2.1).

---

## 9. Quick Reference

```glsl
#hina
// Groups
group <Name> = <set>;

// Bindings block
bindings(<GroupName>, start=<N>) {
  uniform(std140|scalar) Block { ... } name;
  buffer(readonly|readwrite[, coherent][, volatile][, restrict]) Block { ... } name;
  texture <samplerType> name;
  sampler name;
  image(format) <imageType> name;
  tile_input(<index>) name;  // fragment only, use tile_load(name)
}

// Standalone resource
binding(<Group>, <binding>) uniform(std140|scalar) Block { ... } name;

// IO structs (all optional depending on use case)
struct VertexIn { ... };  // Optional: omit for fullscreen triangles (uses gl_VertexIndex)
struct Varyings { ... };  // Optional: omit for depth/shadow passes (no inter-stage data)
struct FragOut { ... };   // Required for fragment output

// Shared memory (compute only)
shared <type> <name>[N]...;

// Push constants
push_constant Block { ... } name;

// Spec constants
spec_const(<id>) <type> NAME = default;

// Snippets
snippet <Name> { ... }
#hina_end

// Stages
#hina_stage <kind> entry <Name> [early_fragment_tests] [depth_*] [use <Snippets>]
<ReturnType> <Name>(<ParamType> in) { ... }
#hina_end
```
