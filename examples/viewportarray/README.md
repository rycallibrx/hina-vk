# Viewport Array

Multi-viewport rendering with geometry shader instancing for stereo/split-screen effects.

**Demonstrates:**
- Geometry shader with invocations = 2 for duplication
- Multi-viewport rendering in a single draw call
- `gl_ViewportIndex` for viewport selection
- Split-screen stereo-like effect (VR preview)
- Per-invocation view matrix offsets for stereo separation
- Screen-space position offsetting via geometry shader

**Controls:**
- Mouse drag: Rotate camera
- Scroll wheel: Zoom in/out
- ESC: Exit
