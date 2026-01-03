# glTF Renderer

Full-featured PBR renderer with GPU culling for glTF 2.0 models.

**Demonstrates:**
- glTF 2.0 loading (via tinygltf)
- Physically-based rendering (PBR) with metallic-roughness workflow
- GPU frustum culling with compute shaders
- Shadow mapping (2048x2048 shadow map)
- Indirect draw commands (`vkCmdDrawIndexedIndirect`)
- Material system with base color, normal, metallic-roughness, and emissive maps
- Node hierarchy traversal and transform computation
- Alpha cutoff and double-sided material support

**Usage:**
```bash
gltf_renderer --gltf=path/to/model.glb
```

Download sample models from: https://github.com/KhronosGroup/glTF-Sample-Assets

**Controls:**
- WASD: Move camera
- Mouse: Look around
- ESC: Exit
