# Stencil Buffer

Stencil-based outline rendering with toon shading.

**Demonstrates:**
- Stencil buffer setup with D32_SFLOAT_S8_UINT format
- Two-pass rendering technique:
  1. Toon pass: Render model with cel-shading, write 1 to stencil
  2. Outline pass: Render extruded model only where stencil != 1
- Stencil compare and write operations (ALWAYS, NOT_EQUAL, REPLACE, KEEP)
- Vertex extrusion along normals for outline effect
- Depth/stencil attachment configuration

**Controls:**
- Mouse drag: Rotate camera
- Scroll wheel: Zoom in/out
- ESC: Exit
