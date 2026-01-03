# Terrain Tessellation

Tessellation shaders for dynamic terrain LOD with heightmap displacement.

**Demonstrates:**
- Tessellation control shader (TCS) for dynamic LOD
- Tessellation evaluation shader (TES) for vertex generation
- Quad patch topology (PATCH_LIST with 4 control points)
- Screen-space adaptive tessellation based on edge size
- Procedural heightmap generation (multi-frequency sine waves)
- Frustum culling in tessellation control shader
- Heightmap texture sampling for displacement

**Configuration:**
- Grid size: 8x8 patches (64 quad patches total)
- Terrain scale: 16x16 world units
- Heightmap resolution: 256x256

**Controls:**
- Mouse drag: Rotate camera
- Scroll wheel: Zoom in/out
- ESC: Exit
