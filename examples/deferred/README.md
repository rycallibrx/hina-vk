# Deferred

G-buffer rendering with tile-based composition and 64 dynamic point lights.

**Demonstrates:**
- Multiple color attachments (G-buffer: position, normal, albedo)
- Tile-based deferred shading using `hina_begin_tile_pass()`:
  1. Subpass 0 (G-Buffer): Render opaque geometry to MRT
  2. Subpass 1 (Composition): Read G-buffer as input attachments via `tile_load()`
  3. Subpass 2 (Transparent): Forward render with alpha blending
- Input attachments for efficient on-chip G-buffer reads (no texture sampling)
- Automatic fallback to VkRenderPass subpasses on older Vulkan (pre-1.4)
- Storage buffers (SSBO) for light array
- 64 dynamic point lights with animated positions
- Push constants for per-object model matrices
- HSL shader with `tile_input()` declarations

**Tile Pass API:**

The tile pass combines multiple subpasses that can read each other's outputs via input attachments:

```c
hina_tile_pass_desc desc = {
    .subpasses = {
        { .color = {...}, .color_count = 3, .has_depth = true },           // G-buffer
        { .tile_inputs = {{0,0},{0,1},{0,2}}, .tile_input_count = 3, ... }, // Composition
        { .depth_read_only = true, ... }                                    // Transparent
    },
    .subpass_count = 3
};
hina_begin_tile_pass(cmd, &desc);
// draw G-buffer
hina_tile_pass_next(cmd, &desc);
// draw composition
hina_tile_pass_next(cmd, &desc);
// draw transparent
hina_end_tile_pass(cmd);
```

**Controls:**
- Mouse drag: Rotate camera
- Scroll wheel: Zoom in/out
- ESC: Exit
