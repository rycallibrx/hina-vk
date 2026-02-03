// Shared types header WITHOUT include guard - double include will cause redefinition

struct SharedData {
    mat4 mvp;
    vec4 color;
};

layout(set = 0, binding = 0) uniform UBO {
    SharedData data;
} ubo;
