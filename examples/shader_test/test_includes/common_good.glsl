// Common header - shared code that works
#ifndef COMMON_GOOD_GLSL
#define COMMON_GOOD_GLSL

struct SharedData {
    mat4 mvp;
    vec4 color;
};

layout(set = 0, binding = 0) uniform UBO {
    SharedData data;
} ubo;

#endif // COMMON_GOOD_GLSL
