# Compute Particles

GPU particle simulation using compute shaders with 1M+ particles.

**Demonstrates:**
- Compute pipeline creation and dispatch
- Storage buffers used as both compute output and vertex input
- Ping-pong double buffering for particle state
- Compute-to-graphics memory barriers for synchronization
- Point rendering with additive blending
- Attractor-based particle physics simulation
- Toggle between touch-controlled and automatic animation

**Configuration:**
- Desktop: 1,048,576 particles (256K * 4)
- Android: 262,144 particles (256K)
- Workgroup size: 256

**Controls:**
- SPACE or TAP: Toggle attractor mode (touch vs automatic)
- Touch/Mouse: Move attractor point (when in touch mode)
- ESC: Exit
