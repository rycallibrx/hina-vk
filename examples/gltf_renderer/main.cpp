#include <algorithm>
#include <atomic>
#include <chrono>
#include <cmath>
#include <condition_variable>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <limits>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

#define TINYGLTF_IMPLEMENTATION
#define STB_IMAGE_IMPLEMENTATION
#define STB_IMAGE_WRITE_IMPLEMENTATION
#include <tiny_gltf.h>

#define GLM_ENABLE_EXPERIMENTAL
#include <glm/glm.hpp>
#include <glm/gtc/matrix_inverse.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/quaternion.hpp>
#include <glm/gtc/type_ptr.hpp>
#include <glm/gtx/quaternion.hpp>

#include "../hina_example.h"

// ============================================================================
// Configuration
// ============================================================================

constexpr int WINDOW_WIDTH = 1280;
constexpr int WINDOW_HEIGHT = 720;
constexpr const char* WINDOW_TITLE = "HinaVK GLTF Renderer";
// No default model shipped - user must provide --gltf=<path>
constexpr uint32_t SHADOW_MAP_SIZE = 2048;
constexpr uint32_t CULL_WORKGROUP_SIZE = 64;

// GPU Timestamp query indices (per frame)
constexpr uint32_t QUERY_SHADOW_START = 0;
constexpr uint32_t QUERY_SHADOW_END   = 1;
constexpr uint32_t QUERY_MAIN_START   = 2;
constexpr uint32_t QUERY_MAIN_END     = 3;
constexpr uint32_t QUERIES_PER_FRAME  = 4;
constexpr uint32_t TIMING_FRAME_DELAY = 2;  // Read results from N frames ago

// ============================================================================
// Data Structures
// ============================================================================

struct GltfVertex {
    glm::vec3 position;
    glm::vec3 normal;
    glm::vec4 tangent;
    glm::vec2 uv;
};

struct SceneUBO {
    glm::mat4 view;
    glm::mat4 proj;
    glm::mat4 view_proj;
    glm::mat4 light_view_proj;
    glm::vec4 camera_pos;
    glm::vec4 light_dir;
    glm::vec4 light_color;
    glm::vec4 ambient_color;
};

struct MaterialUBO {
    glm::vec4 base_color_factor;
    glm::vec4 emissive_factor;
    glm::vec4 mrno;        // x=metallic, y=roughness, z=normal_scale, w=occlusion_strength
    glm::vec4 alpha_flags; // x=alpha_cutoff, y=alpha_mode, z=double_sided
};

struct ObjectData {
    glm::mat4 model;
    glm::mat4 normal;
};

struct ObjectBounds {
    glm::vec4 center_radius;
};

struct DrawCommand {
    uint32_t index_count;
    uint32_t instance_count;
    uint32_t first_index;
    int32_t vertex_offset;
    uint32_t first_instance;
};

static_assert(sizeof(DrawCommand) == 20, "DrawCommand size mismatch");

struct CullingParams {
    glm::vec4 frustum_planes[6];
    uint32_t draw_count;
    glm::vec3 pad0;
};

struct DrawItem {
    uint32_t index_count = 0;
    uint32_t first_index = 0;
    int32_t vertex_offset = 0;
    uint32_t material_index = 0;
    glm::mat4 model = glm::mat4(1.0f);
    glm::mat4 normal = glm::mat4(1.0f);
    glm::vec4 bounds = glm::vec4(0.0f);
};

struct MaterialDesc {
    glm::vec4 base_color_factor = glm::vec4(1.0f);
    glm::vec4 emissive_factor = glm::vec4(0.0f);
    float metallic_factor = 1.0f;
    float roughness_factor = 1.0f;
    float normal_scale = 1.0f;
    float occlusion_strength = 1.0f;
    float alpha_cutoff = 0.5f;
    uint32_t alpha_mode = 0; // 0=opaque, 1=mask, 2=blend (treated as opaque)
    uint32_t double_sided = 0;
    int base_color_tex = -1;
    int metallic_roughness_tex = -1;
    int normal_tex = -1;
    int occlusion_tex = -1;
    int emissive_tex = -1;
};

struct ImageData {
    int width = 0;
    int height = 0;
    std::vector<uint8_t> rgba;
};

struct TextureDesc {
    int image_index = -1;
    int sampler_index = -1;
};

struct SamplerDesc {
    int min_filter = -1;
    int mag_filter = -1;
    int wrap_s = TINYGLTF_TEXTURE_WRAP_REPEAT;
    int wrap_t = TINYGLTF_TEXTURE_WRAP_REPEAT;
};

struct LoadedScene {
    std::vector<GltfVertex> vertices;
    std::vector<uint32_t> indices;
    std::vector<DrawItem> draw_items;
    std::vector<MaterialDesc> materials;
    std::vector<ImageData> images;
    std::vector<TextureDesc> textures;
    std::vector<SamplerDesc> samplers;
    glm::vec3 scene_center = glm::vec3(0.0f);
    float scene_radius = 1.0f;
};

struct DrawRange {
    uint32_t offset = 0;
    uint32_t count = 0;
};

struct MaterialGpu {
    hina_bind_group bind_group = {HINA_INVALID_HANDLE};
    hina_buffer ubo_buffer = {HINA_INVALID_HANDLE};
};

struct PassTiming {
    double shadow_ms = 0.0;
    double main_ms = 0.0;
    bool valid = false;
};

struct FrameResources {
    hina_pass_action shadow_pass = {};
    hina_pass_action main_pass = {};
    hina_pipeline shadow_pipeline = {HINA_INVALID_HANDLE};
    hina_pipeline pbr_pipeline = {HINA_INVALID_HANDLE};
    hina_bind_group shadow_scene_group = {HINA_INVALID_HANDLE};
    hina_bind_group shadow_object_group = {HINA_INVALID_HANDLE};
    hina_bind_group pbr_scene_group = {HINA_INVALID_HANDLE};
    hina_bind_group pbr_object_group = {HINA_INVALID_HANDLE};
    const MaterialGpu* materials = nullptr;
    const DrawRange* material_ranges = nullptr;
    uint32_t material_count = 0;
    hina_buffer vertex_buffer = {HINA_INVALID_HANDLE};
    hina_buffer index_buffer = {HINA_INVALID_HANDLE};
    hina_buffer shadow_commands = {HINA_INVALID_HANDLE};
    hina_buffer draw_commands = {HINA_INVALID_HANDLE};
    uint32_t draw_count = 0;
    hina_texture shadow_color = {HINA_INVALID_HANDLE};
    hina_texture shadow_depth = {HINA_INVALID_HANDLE};
    // GPU timing
    hina_query_pool query_pool = {HINA_INVALID_HANDLE};
    uint32_t query_base = 0;  // Base query index for this frame
};

enum class WorkerRole {
    Shadow,
    Main
};

struct WorkerState {
    hina_context* ctx = nullptr;
    WorkerRole role = WorkerRole::Main;
    std::thread thread;
    std::mutex mutex;
    std::condition_variable cv;
    bool has_job = false;
    bool done = false;
    bool exit = false;
    FrameResources* frame = nullptr;
    hina_cmd* cmd = nullptr;
};

// ============================================================================
// Helpers
// ============================================================================

static bool ends_with(const std::string& value, const char* suffix) {
    size_t len = strlen(suffix);
    if (value.size() < len) return false;
    return value.compare(value.size() - len, len, suffix) == 0;
}

static glm::mat4 node_local_matrix(const tinygltf::Node& node) {
    glm::mat4 local(1.0f);
    if (node.matrix.size() == 16) {
        local = glm::make_mat4(node.matrix.data());
    } else {
        glm::vec3 t(0.0f);
        glm::vec3 s(1.0f);
        glm::quat r(1.0f, 0.0f, 0.0f, 0.0f);

        if (node.translation.size() == 3) {
            t = glm::vec3(node.translation[0], node.translation[1], node.translation[2]);
        }
        if (node.scale.size() == 3) {
            s = glm::vec3(node.scale[0], node.scale[1], node.scale[2]);
        }
        if (node.rotation.size() == 4) {
            r = glm::quat(static_cast<float>(node.rotation[3]),
                          static_cast<float>(node.rotation[0]),
                          static_cast<float>(node.rotation[1]),
                          static_cast<float>(node.rotation[2]));
        }

        local = glm::translate(glm::mat4(1.0f), t) * glm::mat4_cast(r) * glm::scale(glm::mat4(1.0f), s);
    }
    return local;
}

static void build_world_matrices(const tinygltf::Model& model, int node_index,
                                 const glm::mat4& parent, std::vector<glm::mat4>& world) {
    const tinygltf::Node& node = model.nodes[node_index];
    glm::mat4 local = node_local_matrix(node);
    glm::mat4 global = parent * local;
    world[node_index] = global;

    for (int child : node.children) {
        build_world_matrices(model, child, global, world);
    }
}

static float read_component(const unsigned char* data, int component_type, bool normalized) {
    switch (component_type) {
        case TINYGLTF_COMPONENT_TYPE_FLOAT: {
            float value = 0.0f;
            memcpy(&value, data, sizeof(float));
            return value;
        }
        case TINYGLTF_COMPONENT_TYPE_UNSIGNED_BYTE: {
            uint8_t value = *data;
            if (normalized) return static_cast<float>(value) / 255.0f;
            return static_cast<float>(value);
        }
        case TINYGLTF_COMPONENT_TYPE_BYTE: {
            int8_t value = *reinterpret_cast<const int8_t*>(data);
            if (normalized) return std::max(-1.0f, static_cast<float>(value) / 127.0f);
            return static_cast<float>(value);
        }
        case TINYGLTF_COMPONENT_TYPE_UNSIGNED_SHORT: {
            uint16_t value = 0;
            memcpy(&value, data, sizeof(uint16_t));
            if (normalized) return static_cast<float>(value) / 65535.0f;
            return static_cast<float>(value);
        }
        case TINYGLTF_COMPONENT_TYPE_SHORT: {
            int16_t value = 0;
            memcpy(&value, data, sizeof(int16_t));
            if (normalized) return std::max(-1.0f, static_cast<float>(value) / 32767.0f);
            return static_cast<float>(value);
        }
        default:
            return 0.0f;
    }
}

static bool read_accessor_floats(const tinygltf::Model& model, const tinygltf::Accessor& accessor,
                                 int expected_type, int components, std::vector<float>& out) {
    if (accessor.type != expected_type) return false;
    if (accessor.componentType != TINYGLTF_COMPONENT_TYPE_FLOAT &&
        accessor.componentType != TINYGLTF_COMPONENT_TYPE_UNSIGNED_BYTE &&
        accessor.componentType != TINYGLTF_COMPONENT_TYPE_BYTE &&
        accessor.componentType != TINYGLTF_COMPONENT_TYPE_UNSIGNED_SHORT &&
        accessor.componentType != TINYGLTF_COMPONENT_TYPE_SHORT) {
        return false;
    }

    const tinygltf::BufferView& buffer_view = model.bufferViews[accessor.bufferView];
    const tinygltf::Buffer& buffer = model.buffers[buffer_view.buffer];
    const unsigned char* data = buffer.data.data() + buffer_view.byteOffset + accessor.byteOffset;

    size_t stride = accessor.ByteStride(buffer_view);
    size_t component_size = tinygltf::GetComponentSizeInBytes(accessor.componentType);
    if (stride == 0) {
        stride = component_size * components;
    }

    out.resize(accessor.count * components);
    for (size_t i = 0; i < accessor.count; ++i) {
        const unsigned char* ptr = data + i * stride;
        for (int c = 0; c < components; ++c) {
            out[i * components + c] = read_component(ptr + c * component_size, accessor.componentType,
                                                     accessor.normalized);
        }
    }
    return true;
}

static bool read_accessor_vec2(const tinygltf::Model& model, const tinygltf::Accessor& accessor,
                               std::vector<glm::vec2>& out) {
    std::vector<float> data;
    if (!read_accessor_floats(model, accessor, TINYGLTF_TYPE_VEC2, 2, data)) return false;
    out.resize(accessor.count);
    for (size_t i = 0; i < accessor.count; ++i) {
        out[i] = glm::vec2(data[i * 2 + 0], data[i * 2 + 1]);
    }
    return true;
}

static bool read_accessor_vec3(const tinygltf::Model& model, const tinygltf::Accessor& accessor,
                               std::vector<glm::vec3>& out) {
    std::vector<float> data;
    if (!read_accessor_floats(model, accessor, TINYGLTF_TYPE_VEC3, 3, data)) return false;
    out.resize(accessor.count);
    for (size_t i = 0; i < accessor.count; ++i) {
        out[i] = glm::vec3(data[i * 3 + 0], data[i * 3 + 1], data[i * 3 + 2]);
    }
    return true;
}

static bool read_accessor_vec4(const tinygltf::Model& model, const tinygltf::Accessor& accessor,
                               std::vector<glm::vec4>& out) {
    std::vector<float> data;
    if (!read_accessor_floats(model, accessor, TINYGLTF_TYPE_VEC4, 4, data)) return false;
    out.resize(accessor.count);
    for (size_t i = 0; i < accessor.count; ++i) {
        out[i] = glm::vec4(data[i * 4 + 0], data[i * 4 + 1], data[i * 4 + 2], data[i * 4 + 3]);
    }
    return true;
}

static bool read_indices(const tinygltf::Model& model, const tinygltf::Accessor& accessor,
                         std::vector<uint32_t>& out) {
    if (accessor.type != TINYGLTF_TYPE_SCALAR) return false;

    const tinygltf::BufferView& buffer_view = model.bufferViews[accessor.bufferView];
    const tinygltf::Buffer& buffer = model.buffers[buffer_view.buffer];
    const unsigned char* data = buffer.data.data() + buffer_view.byteOffset + accessor.byteOffset;

    size_t stride = accessor.ByteStride(buffer_view);
    size_t component_size = tinygltf::GetComponentSizeInBytes(accessor.componentType);
    if (stride == 0) stride = component_size;

    out.resize(accessor.count);
    for (size_t i = 0; i < accessor.count; ++i) {
        const unsigned char* ptr = data + i * stride;
        switch (accessor.componentType) {
            case TINYGLTF_COMPONENT_TYPE_UNSIGNED_BYTE:
                out[i] = *ptr;
                break;
            case TINYGLTF_COMPONENT_TYPE_UNSIGNED_SHORT: {
                uint16_t value = 0;
                memcpy(&value, ptr, sizeof(uint16_t));
                out[i] = value;
                break;
            }
            case TINYGLTF_COMPONENT_TYPE_UNSIGNED_INT: {
                uint32_t value = 0;
                memcpy(&value, ptr, sizeof(uint32_t));
                out[i] = value;
                break;
            }
            default:
                return false;
        }
    }
    return true;
}

static void compute_normals(const std::vector<glm::vec3>& positions,
                            const std::vector<uint32_t>& indices,
                            std::vector<glm::vec3>& normals) {
    normals.assign(positions.size(), glm::vec3(0.0f));
    for (size_t i = 0; i + 2 < indices.size(); i += 3) {
        uint32_t i0 = indices[i + 0];
        uint32_t i1 = indices[i + 1];
        uint32_t i2 = indices[i + 2];
        const glm::vec3& v0 = positions[i0];
        const glm::vec3& v1 = positions[i1];
        const glm::vec3& v2 = positions[i2];
        glm::vec3 n = glm::normalize(glm::cross(v1 - v0, v2 - v0));
        normals[i0] += n;
        normals[i1] += n;
        normals[i2] += n;
    }
    for (glm::vec3& n : normals) {
        if (glm::dot(n, n) > 0.0f) {
            n = glm::normalize(n);
        } else {
            n = glm::vec3(0.0f, 1.0f, 0.0f);
        }
    }
}

static void compute_tangents(const std::vector<glm::vec3>& positions,
                             const std::vector<glm::vec3>& normals,
                             const std::vector<glm::vec2>& uvs,
                             const std::vector<uint32_t>& indices,
                             std::vector<glm::vec4>& tangents) {
    std::vector<glm::vec3> tan1(positions.size(), glm::vec3(0.0f));
    std::vector<glm::vec3> tan2(positions.size(), glm::vec3(0.0f));

    for (size_t i = 0; i + 2 < indices.size(); i += 3) {
        uint32_t i0 = indices[i + 0];
        uint32_t i1 = indices[i + 1];
        uint32_t i2 = indices[i + 2];

        const glm::vec3& v0 = positions[i0];
        const glm::vec3& v1 = positions[i1];
        const glm::vec3& v2 = positions[i2];

        const glm::vec2& w0 = uvs[i0];
        const glm::vec2& w1 = uvs[i1];
        const glm::vec2& w2 = uvs[i2];

        float x1 = v1.x - v0.x;
        float x2 = v2.x - v0.x;
        float y1 = v1.y - v0.y;
        float y2 = v2.y - v0.y;
        float z1 = v1.z - v0.z;
        float z2 = v2.z - v0.z;

        float s1 = w1.x - w0.x;
        float s2 = w2.x - w0.x;
        float t1 = w1.y - w0.y;
        float t2 = w2.y - w0.y;

        float r = (s1 * t2 - s2 * t1);
        if (fabsf(r) < 1e-8f) continue;
        float inv_r = 1.0f / r;

        glm::vec3 sdir((t2 * x1 - t1 * x2) * inv_r,
                       (t2 * y1 - t1 * y2) * inv_r,
                       (t2 * z1 - t1 * z2) * inv_r);
        glm::vec3 tdir((s1 * x2 - s2 * x1) * inv_r,
                       (s1 * y2 - s2 * y1) * inv_r,
                       (s1 * z2 - s2 * z1) * inv_r);

        tan1[i0] += sdir;
        tan1[i1] += sdir;
        tan1[i2] += sdir;

        tan2[i0] += tdir;
        tan2[i1] += tdir;
        tan2[i2] += tdir;
    }

    tangents.resize(positions.size());
    for (size_t i = 0; i < positions.size(); ++i) {
        const glm::vec3& n = normals[i];
        const glm::vec3& t = tan1[i];

        glm::vec3 tangent = t - n * glm::dot(n, t);
        float len2 = glm::dot(tangent, tangent);
        float w = 1.0f;
        if (len2 > 1e-6f) {
            tangent *= (1.0f / sqrtf(len2));
            w = (glm::dot(glm::cross(n, tangent), tan2[i]) < 0.0f) ? -1.0f : 1.0f;
        } else {
            tangent = glm::vec3(1.0f, 0.0f, 0.0f);
            w = 1.0f;
        }
        tangents[i] = glm::vec4(tangent, w);
    }
}

static glm::vec4 compute_bounds(const std::vector<glm::vec3>& positions) {
    glm::vec3 min_v(std::numeric_limits<float>::max());
    glm::vec3 max_v(std::numeric_limits<float>::lowest());
    for (const glm::vec3& p : positions) {
        min_v = glm::min(min_v, p);
        max_v = glm::max(max_v, p);
    }
    glm::vec3 center = (min_v + max_v) * 0.5f;
    float radius = glm::length(max_v - center);
    return glm::vec4(center, radius);
}

static bool convert_image_rgba8(const tinygltf::Image& image, ImageData& out) {
    if (image.width <= 0 || image.height <= 0 || image.image.empty()) return false;
    if (image.pixel_type != TINYGLTF_COMPONENT_TYPE_UNSIGNED_BYTE &&
        image.pixel_type != TINYGLTF_COMPONENT_TYPE_UNSIGNED_SHORT) {
        return false;
    }

    const int width = image.width;
    const int height = image.height;
    const int comp = image.component;
    const size_t pixel_count = static_cast<size_t>(width) * static_cast<size_t>(height);
    const bool is_u16 = image.pixel_type == TINYGLTF_COMPONENT_TYPE_UNSIGNED_SHORT;
    const size_t src_stride = static_cast<size_t>(comp) * (is_u16 ? 2 : 1);

    out.width = width;
    out.height = height;
    out.rgba.resize(pixel_count * 4);

    for (size_t i = 0; i < pixel_count; ++i) {
        uint8_t r = 0;
        uint8_t g = 0;
        uint8_t b = 0;
        uint8_t a = 255;

        const unsigned char* src = image.image.data() + i * src_stride;
        auto read_u8 = [&](int channel) {
            if (is_u16) {
                uint16_t value = 0;
                memcpy(&value, src + channel * 2, sizeof(uint16_t));
                return static_cast<uint8_t>(value >> 8);
            }
            return src[channel];
        };

        if (comp == 1) {
            r = g = b = read_u8(0);
        } else if (comp == 2) {
            r = g = b = read_u8(0);
            a = read_u8(1);
        } else if (comp == 3) {
            r = read_u8(0);
            g = read_u8(1);
            b = read_u8(2);
        } else if (comp == 4) {
            r = read_u8(0);
            g = read_u8(1);
            b = read_u8(2);
            a = read_u8(3);
        } else {
            return false;
        }

        out.rgba[i * 4 + 0] = r;
        out.rgba[i * 4 + 1] = g;
        out.rgba[i * 4 + 2] = b;
        out.rgba[i * 4 + 3] = a;
    }

    return true;
}

static void extract_frustum_planes(const glm::mat4& vp, glm::vec4 planes[6]) {
    glm::mat4 m = glm::transpose(vp);
    planes[0] = m[3] + m[0];
    planes[1] = m[3] - m[0];
    planes[2] = m[3] + m[1];
    planes[3] = m[3] - m[1];
    planes[4] = m[3] + m[2];
    planes[5] = m[3] - m[2];

    for (int i = 0; i < 6; ++i) {
        float len = glm::length(glm::vec3(planes[i]));
        if (len > 0.0f) {
            planes[i] /= len;
        }
    }
}

static bool load_gltf_scene(const std::string& path, LoadedScene& out_scene) {
    tinygltf::TinyGLTF loader;
    tinygltf::Model model;
    std::string err;
    std::string warn;
    bool ok = false;

    if (ends_with(path, ".glb")) {
        ok = loader.LoadBinaryFromFile(&model, &err, &warn, path.c_str());
    } else {
        ok = loader.LoadASCIIFromFile(&model, &err, &warn, path.c_str());
    }

    if (!warn.empty()) {
        EXAMPLE_LOGI("GLTF warning: %s", warn.c_str());
    }
    if (!ok) {
        EXAMPLE_LOGE("Failed to load GLTF: %s", err.empty() ? "unknown" : err.c_str());
        return false;
    }

    out_scene.samplers.reserve(model.samplers.size());
    for (const tinygltf::Sampler& sampler : model.samplers) {
        SamplerDesc desc;
        desc.min_filter = sampler.minFilter;
        desc.mag_filter = sampler.magFilter;
        desc.wrap_s = sampler.wrapS;
        desc.wrap_t = sampler.wrapT;
        out_scene.samplers.push_back(desc);
    }

    out_scene.images.reserve(model.images.size());
    for (const tinygltf::Image& image : model.images) {
        ImageData data;
        if (!convert_image_rgba8(image, data)) {
            EXAMPLE_LOGI("Image conversion failed, using 1x1 fallback");
            data.width = 1;
            data.height = 1;
            data.rgba = {255, 255, 255, 255};
        }
        out_scene.images.push_back(std::move(data));
    }

    out_scene.textures.reserve(model.textures.size());
    for (const tinygltf::Texture& tex : model.textures) {
        TextureDesc desc;
        desc.image_index = tex.source;
        desc.sampler_index = tex.sampler;
        out_scene.textures.push_back(desc);
    }

    out_scene.materials.reserve(model.materials.size());
    for (const tinygltf::Material& mat : model.materials) {
        MaterialDesc desc;
        const auto& pbr = mat.pbrMetallicRoughness;

        if (pbr.baseColorFactor.size() == 4) {
            desc.base_color_factor = glm::vec4(
                static_cast<float>(pbr.baseColorFactor[0]),
                static_cast<float>(pbr.baseColorFactor[1]),
                static_cast<float>(pbr.baseColorFactor[2]),
                static_cast<float>(pbr.baseColorFactor[3]));
        }
        if (mat.emissiveFactor.size() == 3) {
            desc.emissive_factor = glm::vec4(
                static_cast<float>(mat.emissiveFactor[0]),
                static_cast<float>(mat.emissiveFactor[1]),
                static_cast<float>(mat.emissiveFactor[2]),
                0.0f);
        }

        desc.metallic_factor = static_cast<float>(pbr.metallicFactor);
        desc.roughness_factor = static_cast<float>(pbr.roughnessFactor);
        desc.normal_scale = static_cast<float>(mat.normalTexture.scale);
        desc.occlusion_strength = static_cast<float>(mat.occlusionTexture.strength);
        desc.alpha_cutoff = static_cast<float>(mat.alphaCutoff);
        desc.double_sided = mat.doubleSided ? 1u : 0u;

        if (mat.alphaMode == "MASK") {
            desc.alpha_mode = 1;
        } else if (mat.alphaMode == "BLEND") {
            desc.alpha_mode = 2;
        } else {
            desc.alpha_mode = 0;
        }

        desc.base_color_tex = pbr.baseColorTexture.index;
        desc.metallic_roughness_tex = pbr.metallicRoughnessTexture.index;
        desc.normal_tex = mat.normalTexture.index;
        desc.occlusion_tex = mat.occlusionTexture.index;
        desc.emissive_tex = mat.emissiveTexture.index;

        out_scene.materials.push_back(desc);
    }

    if (out_scene.materials.empty()) {
        out_scene.materials.emplace_back();
    }

    int scene_index = model.defaultScene >= 0 ? model.defaultScene : 0;
    if (scene_index < 0 || scene_index >= static_cast<int>(model.scenes.size())) {
        EXAMPLE_LOGE("GLTF has no valid scene");
        return false;
    }

    std::vector<glm::mat4> world_matrices(model.nodes.size(), glm::mat4(1.0f));
    const tinygltf::Scene& scene = model.scenes[scene_index];
    for (int node_index : scene.nodes) {
        build_world_matrices(model, node_index, glm::mat4(1.0f), world_matrices);
    }

    for (size_t node_index = 0; node_index < model.nodes.size(); ++node_index) {
        const tinygltf::Node& node = model.nodes[node_index];
        if (node.mesh < 0 || node.mesh >= static_cast<int>(model.meshes.size())) continue;

        const tinygltf::Mesh& mesh = model.meshes[node.mesh];
        for (const tinygltf::Primitive& prim : mesh.primitives) {
            if (prim.mode != TINYGLTF_MODE_TRIANGLES) continue;

            auto it_pos = prim.attributes.find("POSITION");
            if (it_pos == prim.attributes.end()) continue;

            const tinygltf::Accessor& pos_accessor = model.accessors[it_pos->second];
            std::vector<glm::vec3> positions;
            if (!read_accessor_vec3(model, pos_accessor, positions)) continue;

            std::vector<glm::vec3> normals;
            std::vector<glm::vec4> tangents;
            std::vector<glm::vec2> uvs;

            auto it_norm = prim.attributes.find("NORMAL");
            if (it_norm != prim.attributes.end()) {
                read_accessor_vec3(model, model.accessors[it_norm->second], normals);
            }

            auto it_uv = prim.attributes.find("TEXCOORD_0");
            if (it_uv != prim.attributes.end()) {
                read_accessor_vec2(model, model.accessors[it_uv->second], uvs);
            }

            auto it_tan = prim.attributes.find("TANGENT");
            if (it_tan != prim.attributes.end()) {
                read_accessor_vec4(model, model.accessors[it_tan->second], tangents);
            }

            std::vector<uint32_t> indices;
            if (prim.indices >= 0) {
                if (!read_indices(model, model.accessors[prim.indices], indices)) continue;
            } else {
                indices.resize(positions.size());
                for (uint32_t i = 0; i < static_cast<uint32_t>(positions.size()); ++i) {
                    indices[i] = i;
                }
            }

            if (normals.empty()) {
                compute_normals(positions, indices, normals);
            }

            if (uvs.empty()) {
                uvs.resize(positions.size(), glm::vec2(0.0f));
            }

            if (tangents.empty()) {
                compute_tangents(positions, normals, uvs, indices, tangents);
            }

            uint32_t base_vertex = static_cast<uint32_t>(out_scene.vertices.size());
            for (size_t i = 0; i < positions.size(); ++i) {
                GltfVertex v;
                v.position = positions[i];
                v.normal = normals[i];
                v.tangent = tangents[i];
                v.uv = uvs[i];
                out_scene.vertices.push_back(v);
            }

            uint32_t first_index = static_cast<uint32_t>(out_scene.indices.size());
            for (uint32_t idx : indices) {
                out_scene.indices.push_back(base_vertex + idx);
            }

            DrawItem item;
            item.first_index = first_index;
            item.index_count = static_cast<uint32_t>(indices.size());
            item.vertex_offset = 0;
            item.material_index = prim.material >= 0 ? static_cast<uint32_t>(prim.material) : 0;
            if (item.material_index >= out_scene.materials.size()) {
                item.material_index = 0;
            }
            item.model = world_matrices[node_index];
            item.normal = glm::mat4(glm::transpose(glm::inverse(glm::mat3(item.model))));
            item.bounds = compute_bounds(positions);
            out_scene.draw_items.push_back(item);
        }
    }

    if (out_scene.draw_items.empty()) {
        EXAMPLE_LOGE("No drawable primitives found");
        return false;
    }

    // Sort draw items by material for efficient binding.
    std::vector<size_t> order(out_scene.draw_items.size());
    for (size_t i = 0; i < order.size(); ++i) order[i] = i;
    std::sort(order.begin(), order.end(), [&](size_t a, size_t b) {
        return out_scene.draw_items[a].material_index < out_scene.draw_items[b].material_index;
    });

    std::vector<DrawItem> sorted_items;
    sorted_items.reserve(out_scene.draw_items.size());
    for (size_t idx : order) {
        sorted_items.push_back(out_scene.draw_items[idx]);
    }
    out_scene.draw_items.swap(sorted_items);

    glm::vec3 scene_min(std::numeric_limits<float>::max());
    glm::vec3 scene_max(std::numeric_limits<float>::lowest());
    for (const DrawItem& item : out_scene.draw_items) {
        glm::vec4 local = item.bounds;
        glm::vec4 world_center = item.model * glm::vec4(local.x, local.y, local.z, 1.0f);
        glm::vec3 sx = glm::vec3(item.model[0]);
        glm::vec3 sy = glm::vec3(item.model[1]);
        glm::vec3 sz = glm::vec3(item.model[2]);
        float scale = std::max(glm::length(sx), std::max(glm::length(sy), glm::length(sz)));
        float radius = local.w * scale;
        scene_min = glm::min(scene_min, glm::vec3(world_center) - glm::vec3(radius));
        scene_max = glm::max(scene_max, glm::vec3(world_center) + glm::vec3(radius));
    }

    out_scene.scene_center = (scene_min + scene_max) * 0.5f;
    out_scene.scene_radius = glm::length(scene_max - out_scene.scene_center);
    if (out_scene.scene_radius < 0.1f) {
        out_scene.scene_radius = 0.1f;
    }

    return true;
}

static hina_sampler make_sampler_from_desc(const SamplerDesc& desc, const hina_device_caps* caps) {
    hina_sampler_desc samp = hina_sampler_desc_default();

    auto map_filter = [](int filter) {
        return filter == TINYGLTF_TEXTURE_FILTER_NEAREST ? HINA_FILTER_NEAREST : HINA_FILTER_LINEAR;
    };

    if (desc.mag_filter != -1) {
        samp.mag_filter = map_filter(desc.mag_filter);
    }

    if (desc.min_filter == TINYGLTF_TEXTURE_FILTER_NEAREST ||
        desc.min_filter == TINYGLTF_TEXTURE_FILTER_NEAREST_MIPMAP_NEAREST ||
        desc.min_filter == TINYGLTF_TEXTURE_FILTER_NEAREST_MIPMAP_LINEAR) {
        samp.min_filter = HINA_FILTER_NEAREST;
    }

    if (desc.min_filter == TINYGLTF_TEXTURE_FILTER_LINEAR ||
        desc.min_filter == TINYGLTF_TEXTURE_FILTER_LINEAR_MIPMAP_NEAREST ||
        desc.min_filter == TINYGLTF_TEXTURE_FILTER_LINEAR_MIPMAP_LINEAR) {
        samp.min_filter = HINA_FILTER_LINEAR;
    }

    if (desc.min_filter == TINYGLTF_TEXTURE_FILTER_NEAREST_MIPMAP_NEAREST ||
        desc.min_filter == TINYGLTF_TEXTURE_FILTER_LINEAR_MIPMAP_NEAREST) {
        samp.mipmap_filter = HINA_FILTER_NEAREST;
    }

    if (desc.min_filter == TINYGLTF_TEXTURE_FILTER_NEAREST_MIPMAP_LINEAR ||
        desc.min_filter == TINYGLTF_TEXTURE_FILTER_LINEAR_MIPMAP_LINEAR) {
        samp.mipmap_filter = HINA_FILTER_LINEAR;
    }

    auto map_wrap = [](int wrap) {
        switch (wrap) {
            case TINYGLTF_TEXTURE_WRAP_CLAMP_TO_EDGE:
                return HINA_ADDRESS_CLAMP_TO_EDGE;
            case TINYGLTF_TEXTURE_WRAP_MIRRORED_REPEAT:
                return HINA_ADDRESS_MIRRORED_REPEAT;
            case TINYGLTF_TEXTURE_WRAP_REPEAT:
            default:
                return HINA_ADDRESS_REPEAT;
        }
    };

    samp.address_u = map_wrap(desc.wrap_s);
    samp.address_v = map_wrap(desc.wrap_t);
    samp.address_w = HINA_ADDRESS_REPEAT;

    if (caps && caps->has_sampler_anisotropy) {
        samp.flags = static_cast<hina_sampler_flags>(samp.flags | HINA_SAMPLER_ANISOTROPY_ENABLE_BIT);
        samp.max_anisotropy = caps->max_sampler_anisotropy;
    }

    return hina_make_sampler(&samp);
}

static void draw_indexed_indirect_range(hina_cmd* cmd, hina_buffer indirect,
                                        uint64_t base_offset, uint32_t count,
                                        uint32_t stride) {
    for (uint32_t i = 0; i < count; ++i) {
        hina_cmd_draw_indexed_indirect(cmd, indirect,
                                       base_offset + (uint64_t)i * stride,
                                       1, stride);
    }
}

static hina_cmd* record_shadow_pass(hina_context* ctx, FrameResources* frame) {
    hina_cmd* cmd = hina_ctx_cmd_begin_ex(ctx, HINA_QUEUE_GRAPHICS);
    if (!cmd) return nullptr;

    // Note: shadow_commands is created with initial_owner=GRAPHICS, no acquire needed

    hina_cmd_transition_texture(cmd, frame->shadow_color, HINA_TEXSTATE_COLOR_ATTACHMENT);
    hina_cmd_transition_texture(cmd, frame->shadow_depth, HINA_TEXSTATE_DEPTH_ATTACHMENT);

    // Timestamp: shadow pass start
    if (hina_query_pool_is_valid(frame->query_pool)) {
        hina_cmd_write_timestamp(cmd, frame->query_pool,
            frame->query_base + QUERY_SHADOW_START, HINA_PIPELINE_STAGE_TOP);
    }

    hina_cmd_begin_pass(cmd, &frame->shadow_pass);
    hina_cmd_bind_pipeline(cmd, frame->shadow_pipeline);
    hina_cmd_bind_group(cmd, 0, frame->shadow_scene_group);
    hina_cmd_bind_group(cmd, 1, frame->shadow_object_group);

    hina_vertex_input bindings = {};
    bindings.vertex_buffers[0] = frame->vertex_buffer;
    bindings.vertex_offsets[0] = 0;
    bindings.index_buffer = frame->index_buffer;
    bindings.index_type = HINA_INDEX_UINT32;
    hina_cmd_apply_vertex_input(cmd, &bindings);

    draw_indexed_indirect_range(cmd, frame->shadow_commands, 0,
                                frame->draw_count, sizeof(DrawCommand));
    hina_cmd_end_pass(cmd);

    // Timestamp: shadow pass end
    if (hina_query_pool_is_valid(frame->query_pool)) {
        hina_cmd_write_timestamp(cmd, frame->query_pool,
            frame->query_base + QUERY_SHADOW_END, HINA_PIPELINE_STAGE_BOTTOM);
    }

    hina_cmd_transition_texture(cmd, frame->shadow_depth, HINA_TEXSTATE_SHADER_READ);

    return cmd;
}

static hina_cmd* record_main_pass(hina_context* ctx, FrameResources* frame) {
    hina_cmd* cmd = hina_ctx_cmd_begin_ex(ctx, HINA_QUEUE_GRAPHICS);
    if (!cmd) return nullptr;

    // Acquire draw commands from compute queue (exclusive sharing mode)
    hina_cmd_acquire_buffer(cmd, frame->draw_commands, HINA_QUEUE_COMPUTE);

    // Timestamp: main pass start
    if (hina_query_pool_is_valid(frame->query_pool)) {
        hina_cmd_write_timestamp(cmd, frame->query_pool,
            frame->query_base + QUERY_MAIN_START, HINA_PIPELINE_STAGE_TOP);
    }

    hina_cmd_begin_pass(cmd, &frame->main_pass);
    hina_cmd_bind_pipeline(cmd, frame->pbr_pipeline);
    hina_cmd_bind_group(cmd, 0, frame->pbr_scene_group);
    hina_cmd_bind_group(cmd, 2, frame->pbr_object_group);

    hina_vertex_input bindings = {};
    bindings.vertex_buffers[0] = frame->vertex_buffer;
    bindings.vertex_offsets[0] = 0;
    bindings.index_buffer = frame->index_buffer;
    bindings.index_type = HINA_INDEX_UINT32;
    hina_cmd_apply_vertex_input(cmd, &bindings);

    for (uint32_t i = 0; i < frame->material_count; ++i) {
        const DrawRange& range = frame->material_ranges[i];
        if (range.count == 0) continue;
        hina_cmd_bind_group(cmd, 1, frame->materials[i].bind_group);
        draw_indexed_indirect_range(cmd, frame->draw_commands,
                                    (uint64_t)range.offset * sizeof(DrawCommand),
                                    range.count, sizeof(DrawCommand));
    }

    hina_cmd_end_pass(cmd);

    // Timestamp: main pass end
    if (hina_query_pool_is_valid(frame->query_pool)) {
        hina_cmd_write_timestamp(cmd, frame->query_pool,
            frame->query_base + QUERY_MAIN_END, HINA_PIPELINE_STAGE_BOTTOM);
    }

    // Release draw commands back to compute queue for next frame (bidirectional transfer)
    hina_cmd_release_buffer(cmd, frame->draw_commands, HINA_QUEUE_COMPUTE);

    return cmd;
}

static void worker_thread(WorkerState* worker) {
    while (true) {
        std::unique_lock<std::mutex> lock(worker->mutex);
        worker->cv.wait(lock, [&]() { return worker->has_job || worker->exit; });
        if (worker->exit) {
            return;
        }
        FrameResources* frame = worker->frame;
        worker->has_job = false;
        lock.unlock();

        if (worker->role == WorkerRole::Shadow) {
            worker->cmd = record_shadow_pass(worker->ctx, frame);
        } else {
            worker->cmd = record_main_pass(worker->ctx, frame);
        }

        lock.lock();
        worker->done = true;
        lock.unlock();
        worker->cv.notify_all();
    }
}

static void submit_worker_job(WorkerState& worker, FrameResources* frame) {
    std::lock_guard<std::mutex> lock(worker.mutex);
    worker.frame = frame;
    worker.done = false;
    worker.cmd = nullptr;
    worker.has_job = true;
    worker.cv.notify_all();
}

static hina_cmd* wait_worker_job(WorkerState& worker) {
    std::unique_lock<std::mutex> lock(worker.mutex);
    worker.cv.wait(lock, [&]() { return worker.done; });
    return worker.cmd;
}

// ============================================================================
// Main
// ============================================================================

int main(int argc, char** argv) {
    hina_example_config cfg = hina_example_config_default();
    cfg.title = WINDOW_TITLE;
    cfg.width = WINDOW_WIDTH;
    cfg.height = WINDOW_HEIGHT;

    std::string gltf_path;
    std::vector<char*> filtered_args;
    filtered_args.push_back(argv[0]);

    for (int i = 1; i < argc; ++i) {
        if (strncmp(argv[i], "--gltf=", 7) == 0) {
            gltf_path = argv[i] + 7;
        } else if (strcmp(argv[i], "--gltf") == 0 && i + 1 < argc) {
            gltf_path = argv[++i];
        } else {
            filtered_args.push_back(argv[i]);
        }
    }

    if (!hina_example_parse_args(&cfg, static_cast<int>(filtered_args.size()), filtered_args.data())) {
        return 0;
    }

    if (gltf_path.empty()) {
        // Default for running from VS without command line args
        gltf_path = "ABeautifulGame.glb";
    }

    hina_example_app app;
    if (!hina_example_init(&app, &cfg)) {
        return 1;
    }
    HINA_SCOPE_EXIT(hina_example_shutdown(&app));

    if (!hslc_init(nullptr)) {
        fprintf(stderr, "Failed to initialize HSL compiler\n");
        return 1;
    }
    HINA_SCOPE_EXIT(hslc_shutdown());

    EXAMPLE_LOGI("Loading GLTF: %s", gltf_path.c_str());
    LoadedScene scene;
    if (!load_gltf_scene(gltf_path, scene)) {
        return 1;
    }

    const hina_device_caps* caps = hina_get_device_caps();

    // ========================================================================
    // Create GPU resources
    // ========================================================================

    hina_buffer_desc vbo_desc = {0};
    vbo_desc.size = scene.vertices.size() * sizeof(GltfVertex);
    vbo_desc.memory = HINA_BUFFER_GPU;
    vbo_desc.usage = static_cast<hina_buffer_usage>(HINA_BUFFER_VERTEX | HINA_BUFFER_TRANSFER_DST);
    vbo_desc.initial_data = scene.vertices.data();

    hina_buffer vertex_buffer = hina_make_buffer(&vbo_desc);
    if (!hina_buffer_is_valid(vertex_buffer)) {
        EXAMPLE_LOGE("Failed to create vertex buffer");
        return 1;
    }
    HINA_SCOPE_EXIT(hina_destroy_buffer(vertex_buffer));

    hina_buffer_desc ibo_desc = {0};
    ibo_desc.size = scene.indices.size() * sizeof(uint32_t);
    ibo_desc.memory = HINA_BUFFER_GPU;
    ibo_desc.usage = static_cast<hina_buffer_usage>(HINA_BUFFER_INDEX | HINA_BUFFER_TRANSFER_DST);
    ibo_desc.initial_data = scene.indices.data();

    hina_buffer index_buffer = hina_make_buffer(&ibo_desc);
    if (!hina_buffer_is_valid(index_buffer)) {
        EXAMPLE_LOGE("Failed to create index buffer");
        return 1;
    }
    HINA_SCOPE_EXIT(hina_destroy_buffer(index_buffer));

    std::vector<ObjectData> objects;
    std::vector<ObjectBounds> bounds;
    std::vector<DrawCommand> draw_commands;

    objects.reserve(scene.draw_items.size());
    bounds.reserve(scene.draw_items.size());
    draw_commands.reserve(scene.draw_items.size());

    for (size_t i = 0; i < scene.draw_items.size(); ++i) {
        const DrawItem& item = scene.draw_items[i];
        ObjectData obj;
        obj.model = item.model;
        obj.normal = item.normal;
        objects.push_back(obj);

        ObjectBounds b;
        b.center_radius = item.bounds;
        bounds.push_back(b);

        DrawCommand cmd = {};
        cmd.index_count = item.index_count;
        cmd.instance_count = 1;
        cmd.first_index = item.first_index;
        cmd.vertex_offset = item.vertex_offset;
        cmd.first_instance = static_cast<uint32_t>(i);
        draw_commands.push_back(cmd);
    }

    hina_buffer_desc obj_desc = {0};
    obj_desc.size = objects.size() * sizeof(ObjectData);
    obj_desc.memory = HINA_BUFFER_GPU;
    obj_desc.usage = static_cast<hina_buffer_usage>(HINA_BUFFER_STORAGE | HINA_BUFFER_TRANSFER_DST);
    obj_desc.initial_data = objects.data();

    hina_buffer object_buffer = hina_make_buffer(&obj_desc);
    if (!hina_buffer_is_valid(object_buffer)) {
        EXAMPLE_LOGE("Failed to create object buffer");
        return 1;
    }
    HINA_SCOPE_EXIT(hina_destroy_buffer(object_buffer));

    hina_buffer_desc bounds_desc = {0};
    bounds_desc.size = bounds.size() * sizeof(ObjectBounds);
    bounds_desc.memory = HINA_BUFFER_GPU;
    bounds_desc.usage = static_cast<hina_buffer_usage>(HINA_BUFFER_STORAGE | HINA_BUFFER_TRANSFER_DST);
    bounds_desc.initial_data = bounds.data();

    hina_buffer bounds_buffer = hina_make_buffer(&bounds_desc);
    if (!hina_buffer_is_valid(bounds_buffer)) {
        EXAMPLE_LOGE("Failed to create bounds buffer");
        return 1;
    }
    HINA_SCOPE_EXIT(hina_destroy_buffer(bounds_buffer));

    hina_buffer_desc draw_desc = {0};
    draw_desc.size = draw_commands.size() * sizeof(DrawCommand);
    draw_desc.memory = HINA_BUFFER_GPU;
    draw_desc.usage = static_cast<hina_buffer_usage>(
        HINA_BUFFER_STORAGE | HINA_BUFFER_INDIRECT | HINA_BUFFER_TRANSFER_DST);
    draw_desc.initial_data = draw_commands.data();
    draw_desc.initial_owner = HINA_QUEUE_COMPUTE;  // Compute uses it first; acquire from graphics on subsequent frames

    hina_buffer draw_buffer = hina_make_buffer(&draw_desc);
    if (!hina_buffer_is_valid(draw_buffer)) {
        EXAMPLE_LOGE("Failed to create draw command buffer");
        return 1;
    }
    HINA_SCOPE_EXIT(hina_destroy_buffer(draw_buffer));

    // Shadow draw buffer uses graphics queue (not compute-culled)
    hina_buffer_desc shadow_draw_desc = draw_desc;
    shadow_draw_desc.initial_owner = HINA_QUEUE_GRAPHICS;
    hina_buffer shadow_draw_buffer = hina_make_buffer(&shadow_draw_desc);
    if (!hina_buffer_is_valid(shadow_draw_buffer)) {
        EXAMPLE_LOGE("Failed to create shadow draw command buffer");
        return 1;
    }
    HINA_SCOPE_EXIT(hina_destroy_buffer(shadow_draw_buffer));

    hina_buffer_desc scene_ubo_desc = {0};
    scene_ubo_desc.size = sizeof(SceneUBO);
    scene_ubo_desc.memory = HINA_BUFFER_CPU;
    scene_ubo_desc.usage = HINA_BUFFER_UNIFORM;

    hina_buffer scene_ubo_buffer = hina_make_buffer(&scene_ubo_desc);
    if (!hina_buffer_is_valid(scene_ubo_buffer)) {
        EXAMPLE_LOGE("Failed to create scene UBO buffer");
        return 1;
    }
    HINA_SCOPE_EXIT(hina_destroy_buffer(scene_ubo_buffer));

    SceneUBO* scene_ubo = static_cast<SceneUBO*>(hina_mapped_buffer_ptr(scene_ubo_buffer));
    if (!scene_ubo) {
        EXAMPLE_LOGE("Failed to map scene UBO buffer");
        return 1;
    }

    hina_buffer_desc cull_params_desc = {0};
    cull_params_desc.size = sizeof(CullingParams);
    cull_params_desc.memory = HINA_BUFFER_CPU;
    cull_params_desc.usage = HINA_BUFFER_UNIFORM;

    hina_buffer cull_params_buffer = hina_make_buffer(&cull_params_desc);
    if (!hina_buffer_is_valid(cull_params_buffer)) {
        EXAMPLE_LOGE("Failed to create culling params buffer");
        return 1;
    }
    HINA_SCOPE_EXIT(hina_destroy_buffer(cull_params_buffer));

    CullingParams* cull_params = static_cast<CullingParams*>(hina_mapped_buffer_ptr(cull_params_buffer));
    if (!cull_params) {
        EXAMPLE_LOGE("Failed to map culling params buffer");
        return 1;
    }

    // ========================================================================
    // Textures and Samplers
    // ========================================================================

    std::vector<hina_texture> textures;
    std::vector<hina_texture_view> texture_views;
    textures.reserve(scene.images.size());
    texture_views.reserve(scene.images.size());

    for (const ImageData& img : scene.images) {
        hina_texture_desc tex_desc = hina_texture_desc_default();
        tex_desc.format = HINA_FORMAT_R8G8B8A8_UNORM;
        tex_desc.width = static_cast<uint32_t>(img.width);
        tex_desc.height = static_cast<uint32_t>(img.height);
        tex_desc.mip_levels = HINA_MIP_LEVELS_AUTO;
        tex_desc.usage = HINA_TEXTURE_SAMPLED_BIT;
        tex_desc.initial_data = img.rgba.data();
        tex_desc.initial_stride = static_cast<size_t>(img.width) * 4;

        hina_texture tex = hina_make_texture(&tex_desc);
        if (!hina_texture_is_valid(tex)) {
            EXAMPLE_LOGE("Failed to create texture");
            return 1;
        }
        textures.push_back(tex);
        texture_views.push_back(hina_texture_get_default_view(tex));
    }
    HINA_SCOPE_EXIT({
        for (hina_texture tex : textures) {
            if (hina_texture_is_valid(tex)) hina_destroy_texture(tex);
        }
    });

    std::vector<hina_sampler> samplers;
    samplers.reserve(scene.samplers.size());
    for (const SamplerDesc& sdesc : scene.samplers) {
        hina_sampler sampler = make_sampler_from_desc(sdesc, caps);
        samplers.push_back(sampler);
    }
    HINA_SCOPE_EXIT({
        for (hina_sampler sampler : samplers) {
            if (hina_sampler_is_valid(sampler)) hina_destroy_sampler(sampler);
        }
    });

    // Default textures
    auto make_solid_texture = [](uint8_t r, uint8_t g, uint8_t b, uint8_t a) {
        uint8_t pixels[4] = {r, g, b, a};
        hina_texture_desc desc = hina_texture_desc_default();
        desc.format = HINA_FORMAT_R8G8B8A8_UNORM;
        desc.width = 1;
        desc.height = 1;
        desc.mip_levels = 1;
        desc.usage = static_cast<hina_texture_usage_flags>(HINA_TEXTURE_SAMPLED_BIT);
        desc.initial_data = pixels;
        desc.initial_stride = 4;
        return hina_make_texture(&desc);
    };

    hina_texture tex_white = make_solid_texture(255, 255, 255, 255);
    hina_texture tex_black = make_solid_texture(0, 0, 0, 255);
    hina_texture tex_normal = make_solid_texture(128, 128, 255, 255);
    HINA_SCOPE_EXIT({
        if (hina_texture_is_valid(tex_white)) hina_destroy_texture(tex_white);
        if (hina_texture_is_valid(tex_black)) hina_destroy_texture(tex_black);
        if (hina_texture_is_valid(tex_normal)) hina_destroy_texture(tex_normal);
    });

    hina_texture_view tex_white_view = hina_texture_get_default_view(tex_white);
    hina_texture_view tex_black_view = hina_texture_get_default_view(tex_black);
    hina_texture_view tex_normal_view = hina_texture_get_default_view(tex_normal);

    hina_sampler_desc default_samp_desc = hina_sampler_desc_default();
    hina_sampler default_sampler = hina_make_sampler(&default_samp_desc);
    HINA_SCOPE_EXIT(if (hina_sampler_is_valid(default_sampler)) hina_destroy_sampler(default_sampler));

    // Map GLTF textures to Hina views/samplers
    struct TextureRef { hina_texture_view view; hina_sampler sampler; };
    std::vector<TextureRef> texture_refs;
    texture_refs.reserve(scene.textures.size());

    for (const TextureDesc& tex : scene.textures) {
        TextureRef ref = { tex_white_view, default_sampler };
        if (tex.image_index >= 0 && tex.image_index < static_cast<int>(texture_views.size())) {
            ref.view = texture_views[tex.image_index];
        }
        if (tex.sampler_index >= 0 && tex.sampler_index < static_cast<int>(samplers.size())) {
            ref.sampler = samplers[tex.sampler_index];
        }
        texture_refs.push_back(ref);
    }

    // ========================================================================
    // Shadow Map
    // ========================================================================

    hina_texture_desc shadow_color_desc = hina_texture_desc_default();
    shadow_color_desc.format = HINA_FORMAT_R8_UNORM;
    shadow_color_desc.width = SHADOW_MAP_SIZE;
    shadow_color_desc.height = SHADOW_MAP_SIZE;
    shadow_color_desc.usage = static_cast<hina_texture_usage_flags>(
        HINA_TEXTURE_RENDER_TARGET_BIT | HINA_TEXTURE_SAMPLED_BIT);

    hina_texture shadow_color = hina_make_texture(&shadow_color_desc);
    if (!hina_texture_is_valid(shadow_color)) {
        EXAMPLE_LOGE("Failed to create shadow color texture");
        return 1;
    }
    HINA_SCOPE_EXIT(hina_destroy_texture(shadow_color));

    hina_texture_desc shadow_depth_desc = hina_texture_desc_default();
    shadow_depth_desc.format = HINA_FORMAT_D32_SFLOAT;
    shadow_depth_desc.width = SHADOW_MAP_SIZE;
    shadow_depth_desc.height = SHADOW_MAP_SIZE;
    shadow_depth_desc.usage = static_cast<hina_texture_usage_flags>(
        HINA_TEXTURE_RENDER_TARGET_BIT | HINA_TEXTURE_SAMPLED_BIT);

    hina_texture shadow_depth = hina_make_texture(&shadow_depth_desc);
    if (!hina_texture_is_valid(shadow_depth)) {
        EXAMPLE_LOGE("Failed to create shadow depth texture");
        return 1;
    }
    HINA_SCOPE_EXIT(hina_destroy_texture(shadow_depth));

    hina_sampler_desc shadow_samp_desc = hina_sampler_desc_default();
    shadow_samp_desc.flags = static_cast<hina_sampler_flags>(shadow_samp_desc.flags | HINA_SAMPLER_COMPARE_ENABLE_BIT);
    shadow_samp_desc.compare_op = HINA_COMPARE_OP_LESS_OR_EQUAL;
    shadow_samp_desc.address_u = HINA_ADDRESS_CLAMP_TO_EDGE;
    shadow_samp_desc.address_v = HINA_ADDRESS_CLAMP_TO_EDGE;
    shadow_samp_desc.address_w = HINA_ADDRESS_CLAMP_TO_EDGE;
    hina_sampler shadow_sampler = hina_make_sampler(&shadow_samp_desc);
    if (!hina_sampler_is_valid(shadow_sampler)) {
        EXAMPLE_LOGE("Failed to create shadow sampler");
        return 1;
    }
    HINA_SCOPE_EXIT(hina_destroy_sampler(shadow_sampler));

    // ========================================================================
    // Pipelines
    // ========================================================================

    char* pbr_path = hina_example_shader_path(&app, "pbr.hina_sl");
    char* shadow_path = hina_example_shader_path(&app, "shadow.hina_sl");
    char* cull_path = hina_example_shader_path(&app, "cull.hina_sl");
    HINA_SCOPE_EXIT({ if (pbr_path) free(pbr_path); if (shadow_path) free(shadow_path); if (cull_path) free(cull_path); });

    hina_vertex_layout vertex_layout = {};
    vertex_layout.buffer_count = 1;
    vertex_layout.buffer_strides[0] = sizeof(GltfVertex);
    vertex_layout.input_rates[0] = HINA_VERTEX_INPUT_RATE_VERTEX;
    vertex_layout.attr_count = 4;
    vertex_layout.attrs[0] = { HINA_FORMAT_R32G32B32_SFLOAT, offsetof(GltfVertex, position), 0, 0 };
    vertex_layout.attrs[1] = { HINA_FORMAT_R32G32B32_SFLOAT, offsetof(GltfVertex, normal), 1, 0 };
    vertex_layout.attrs[2] = { HINA_FORMAT_R32G32B32A32_SFLOAT, offsetof(GltfVertex, tangent), 2, 0 };
    vertex_layout.attrs[3] = { HINA_FORMAT_R32G32_SFLOAT, offsetof(GltfVertex, uv), 3, 0 };

    char* error = nullptr;

    hina_hsl_pipeline_desc pbr_desc = hina_hsl_pipeline_desc_default();
    pbr_desc.layout = vertex_layout;
    pbr_desc.color_formats[0] = hina_get_surface_format();
    pbr_desc.depth_format = HINA_FORMAT_D32_SFLOAT;
    pbr_desc.cull_mode = HINA_CULL_MODE_BACK;

    error = nullptr;
    hina_pipeline pbr_pipeline = hina_example_make_pipeline_from_hsl(&app, pbr_path, &pbr_desc, &error);
    if (!hina_pipeline_is_valid(pbr_pipeline)) {
        EXAMPLE_LOGE("PBR pipeline creation failed: %s", error ? error : "unknown");
        if (error) hslc_free_log(error);
        return 1;
    }
    if (error) hslc_free_log(error);
    HINA_SCOPE_EXIT(hina_destroy_pipeline(pbr_pipeline));

    hina_hsl_pipeline_desc shadow_desc = hina_hsl_pipeline_desc_default();
    shadow_desc.layout = vertex_layout;
    shadow_desc.color_formats[0] = HINA_FORMAT_R8_UNORM;
    shadow_desc.depth_format = HINA_FORMAT_D32_SFLOAT;
    shadow_desc.cull_mode = HINA_CULL_MODE_BACK;

    error = nullptr;
    hina_pipeline shadow_pipeline = hina_example_make_pipeline_from_hsl(&app, shadow_path, &shadow_desc, &error);
    if (!hina_pipeline_is_valid(shadow_pipeline)) {
        EXAMPLE_LOGE("Shadow pipeline creation failed: %s", error ? error : "unknown");
        if (error) hslc_free_log(error);
        return 1;
    }
    if (error) hslc_free_log(error);
    HINA_SCOPE_EXIT(hina_destroy_pipeline(shadow_pipeline));

    hina_hsl_pipeline_desc cull_desc = hina_hsl_pipeline_desc_default();
    error = nullptr;
    hina_pipeline cull_pipeline = hina_example_make_pipeline_from_hsl(&app, cull_path, &cull_desc, &error);
    if (!hina_pipeline_is_valid(cull_pipeline)) {
        EXAMPLE_LOGE("Culling pipeline creation failed: %s", error ? error : "unknown");
        if (error) hslc_free_log(error);
        return 1;
    }
    if (error) hslc_free_log(error);
    HINA_SCOPE_EXIT(hina_destroy_pipeline(cull_pipeline));

    // Bind group layouts
    hina_bind_group_layout pbr_scene_layout = hina_pipeline_get_bind_group_layout(pbr_pipeline, 0);
    hina_bind_group_layout pbr_material_layout = hina_pipeline_get_bind_group_layout(pbr_pipeline, 1);
    hina_bind_group_layout pbr_object_layout = hina_pipeline_get_bind_group_layout(pbr_pipeline, 2);

    hina_bind_group_layout shadow_scene_layout = hina_pipeline_get_bind_group_layout(shadow_pipeline, 0);
    hina_bind_group_layout shadow_object_layout = hina_pipeline_get_bind_group_layout(shadow_pipeline, 1);

    hina_bind_group_layout cull_layout = hina_pipeline_get_bind_group_layout(cull_pipeline, 0);

    // Scene bind groups
    hina_bind_group_entry scene_entries[2] = {};
    scene_entries[0].binding = 0;
    scene_entries[0].type = HINA_DESC_TYPE_UNIFORM_BUFFER;
    scene_entries[0].buffer.buffer = scene_ubo_buffer;
    scene_entries[0].buffer.offset = 0;
    scene_entries[0].buffer.size = sizeof(SceneUBO);

    scene_entries[1].binding = 1;
    scene_entries[1].type = HINA_DESC_TYPE_COMBINED_IMAGE_SAMPLER;
    scene_entries[1].combined.view = hina_texture_get_default_view(shadow_depth);
    scene_entries[1].combined.sampler = shadow_sampler;

    hina_bind_group_desc scene_group_desc = {};
    scene_group_desc.entries = scene_entries;
    scene_group_desc.entry_count = 2;

    scene_group_desc.layout = pbr_scene_layout;
    scene_group_desc.label = "pbr_scene";
    hina_bind_group pbr_scene_group = hina_create_bind_group(&scene_group_desc);
    if (!hina_bind_group_is_valid(pbr_scene_group)) {
        EXAMPLE_LOGE("Failed to create PBR scene bind group");
        return 1;
    }
    HINA_SCOPE_EXIT(hina_destroy_bind_group(pbr_scene_group));

    scene_group_desc.layout = shadow_scene_layout;
    scene_group_desc.label = "shadow_scene";
    hina_bind_group_entry shadow_scene_entries[1] = {};
    shadow_scene_entries[0] = scene_entries[0];
    scene_group_desc.entries = shadow_scene_entries;
    scene_group_desc.entry_count = 1;
    hina_bind_group shadow_scene_group = hina_create_bind_group(&scene_group_desc);
    if (!hina_bind_group_is_valid(shadow_scene_group)) {
        EXAMPLE_LOGE("Failed to create shadow scene bind group");
        return 1;
    }
    HINA_SCOPE_EXIT(hina_destroy_bind_group(shadow_scene_group));

    scene_group_desc.entries = scene_entries;
    scene_group_desc.entry_count = 2;

    // Object bind groups
    hina_bind_group_entry object_entry = {};
    object_entry.binding = 0;
    object_entry.type = HINA_DESC_TYPE_STORAGE_BUFFER;
    object_entry.buffer.buffer = object_buffer;
    object_entry.buffer.offset = 0;
    object_entry.buffer.size = objects.size() * sizeof(ObjectData);

    hina_bind_group_desc object_group_desc = {};
    object_group_desc.entries = &object_entry;
    object_group_desc.entry_count = 1;

    object_group_desc.layout = pbr_object_layout;
    object_group_desc.label = "pbr_object";
    hina_bind_group pbr_object_group = hina_create_bind_group(&object_group_desc);
    if (!hina_bind_group_is_valid(pbr_object_group)) {
        EXAMPLE_LOGE("Failed to create PBR object bind group");
        return 1;
    }
    HINA_SCOPE_EXIT(hina_destroy_bind_group(pbr_object_group));

    object_group_desc.layout = shadow_object_layout;
    object_group_desc.label = "shadow_object";
    hina_bind_group shadow_object_group = hina_create_bind_group(&object_group_desc);
    if (!hina_bind_group_is_valid(shadow_object_group)) {
        EXAMPLE_LOGE("Failed to create shadow object bind group");
        return 1;
    }
    HINA_SCOPE_EXIT(hina_destroy_bind_group(shadow_object_group));

    // Culling bind group
    hina_bind_group_entry cull_entries[4] = {};
    cull_entries[0].binding = 0;
    cull_entries[0].type = HINA_DESC_TYPE_STORAGE_BUFFER;
    cull_entries[0].buffer.buffer = object_buffer;
    cull_entries[0].buffer.offset = 0;
    cull_entries[0].buffer.size = objects.size() * sizeof(ObjectData);

    cull_entries[1].binding = 1;
    cull_entries[1].type = HINA_DESC_TYPE_STORAGE_BUFFER;
    cull_entries[1].buffer.buffer = bounds_buffer;
    cull_entries[1].buffer.offset = 0;
    cull_entries[1].buffer.size = bounds.size() * sizeof(ObjectBounds);

    cull_entries[2].binding = 2;
    cull_entries[2].type = HINA_DESC_TYPE_STORAGE_BUFFER;
    cull_entries[2].buffer.buffer = draw_buffer;
    cull_entries[2].buffer.offset = 0;
    cull_entries[2].buffer.size = draw_commands.size() * sizeof(DrawCommand);

    cull_entries[3].binding = 3;
    cull_entries[3].type = HINA_DESC_TYPE_UNIFORM_BUFFER;
    cull_entries[3].buffer.buffer = cull_params_buffer;
    cull_entries[3].buffer.offset = 0;
    cull_entries[3].buffer.size = sizeof(CullingParams);

    hina_bind_group_desc cull_group_desc = {};
    cull_group_desc.layout = cull_layout;
    cull_group_desc.entries = cull_entries;
    cull_group_desc.entry_count = 4;
    cull_group_desc.label = "cull_group";

    hina_bind_group cull_group = hina_create_bind_group(&cull_group_desc);
    if (!hina_bind_group_is_valid(cull_group)) {
        EXAMPLE_LOGE("Failed to create culling bind group");
        return 1;
    }
    HINA_SCOPE_EXIT(hina_destroy_bind_group(cull_group));

    // Material bind groups
    std::vector<MaterialGpu> materials(scene.materials.size());
    for (size_t i = 0; i < scene.materials.size(); ++i) {
        const MaterialDesc& mat = scene.materials[i];

        auto resolve_tex = [&](int index, hina_texture_view fallback) {
            if (index >= 0 && index < static_cast<int>(texture_refs.size())) {
                return texture_refs[index].view;
            }
            return fallback;
        };

        auto resolve_sampler = [&](int index) {
            if (index >= 0 && index < static_cast<int>(texture_refs.size())) {
                return texture_refs[index].sampler;
            }
            return default_sampler;
        };

        MaterialUBO ubo = {};
        ubo.base_color_factor = mat.base_color_factor;
        ubo.emissive_factor = mat.emissive_factor;
        ubo.mrno = glm::vec4(mat.metallic_factor, mat.roughness_factor,
                             mat.normal_scale, mat.occlusion_strength);
        ubo.alpha_flags = glm::vec4(mat.alpha_cutoff,
                                    static_cast<float>(mat.alpha_mode),
                                    static_cast<float>(mat.double_sided),
                                    0.0f);

        hina_buffer_desc mat_desc = {0};
        mat_desc.size = sizeof(MaterialUBO);
        mat_desc.memory = HINA_BUFFER_CPU;
        mat_desc.usage = HINA_BUFFER_UNIFORM;
        mat_desc.initial_data = &ubo;

        materials[i].ubo_buffer = hina_make_buffer(&mat_desc);
        if (!hina_buffer_is_valid(materials[i].ubo_buffer)) {
            EXAMPLE_LOGE("Failed to create material UBO buffer");
            return 1;
        }

        hina_bind_group_entry entries[6] = {};
        entries[0].binding = 0;
        entries[0].type = HINA_DESC_TYPE_COMBINED_IMAGE_SAMPLER;
        entries[0].combined.view = resolve_tex(mat.base_color_tex, tex_white_view);
        entries[0].combined.sampler = resolve_sampler(mat.base_color_tex);

        entries[1].binding = 1;
        entries[1].type = HINA_DESC_TYPE_COMBINED_IMAGE_SAMPLER;
        entries[1].combined.view = resolve_tex(mat.normal_tex, tex_normal_view);
        entries[1].combined.sampler = resolve_sampler(mat.normal_tex);

        entries[2].binding = 2;
        entries[2].type = HINA_DESC_TYPE_COMBINED_IMAGE_SAMPLER;
        entries[2].combined.view = resolve_tex(mat.metallic_roughness_tex, tex_white_view);
        entries[2].combined.sampler = resolve_sampler(mat.metallic_roughness_tex);

        entries[3].binding = 3;
        entries[3].type = HINA_DESC_TYPE_COMBINED_IMAGE_SAMPLER;
        entries[3].combined.view = resolve_tex(mat.occlusion_tex, tex_white_view);
        entries[3].combined.sampler = resolve_sampler(mat.occlusion_tex);

        entries[4].binding = 4;
        entries[4].type = HINA_DESC_TYPE_COMBINED_IMAGE_SAMPLER;
        entries[4].combined.view = resolve_tex(mat.emissive_tex, tex_black_view);
        entries[4].combined.sampler = resolve_sampler(mat.emissive_tex);

        entries[5].binding = 5;
        entries[5].type = HINA_DESC_TYPE_UNIFORM_BUFFER;
        entries[5].buffer.buffer = materials[i].ubo_buffer;
        entries[5].buffer.offset = 0;
        entries[5].buffer.size = sizeof(MaterialUBO);

        hina_bind_group_desc mat_group_desc = {};
        mat_group_desc.layout = pbr_material_layout;
        mat_group_desc.entries = entries;
        mat_group_desc.entry_count = 6;
        mat_group_desc.label = "material";

        materials[i].bind_group = hina_create_bind_group(&mat_group_desc);
        if (!hina_bind_group_is_valid(materials[i].bind_group)) {
            EXAMPLE_LOGE("Failed to create material bind group");
            return 1;
        }
    }
    HINA_SCOPE_EXIT({
        for (MaterialGpu& mat : materials) {
            if (hina_bind_group_is_valid(mat.bind_group)) hina_destroy_bind_group(mat.bind_group);
            if (hina_buffer_is_valid(mat.ubo_buffer)) hina_destroy_buffer(mat.ubo_buffer);
        }
    });

    // Build material ranges
    std::vector<DrawRange> material_ranges(materials.size());
    uint32_t current_mat = 0;
    uint32_t start = 0;
    for (uint32_t i = 0; i < static_cast<uint32_t>(scene.draw_items.size()); ++i) {
        uint32_t mat = scene.draw_items[i].material_index;
        if (i == 0) {
            current_mat = mat;
            start = 0;
        } else if (mat != current_mat) {
            material_ranges[current_mat] = {start, i - start};
            start = i;
            current_mat = mat;
        }
    }
    material_ranges[current_mat] = {start, static_cast<uint32_t>(scene.draw_items.size()) - start};

    // ========================================================================
    // Depth buffer for main pass
    // ========================================================================

    hina_depth_buffer depth_buffer = {};
    if (!hina_depth_buffer_init(&depth_buffer, WINDOW_WIDTH, WINDOW_HEIGHT)) {
        EXAMPLE_LOGE("Failed to create depth buffer");
        return 1;
    }
    HINA_SCOPE_EXIT(hina_depth_buffer_destroy(&depth_buffer));

    // ========================================================================
    // Thread contexts
    // ========================================================================

    hina_context* shadow_ctx = hina_create_thread_context();
    hina_context* main_ctx = hina_create_thread_context();
    if (!shadow_ctx || !main_ctx) {
        EXAMPLE_LOGE("Failed to create thread contexts");
        return 1;
    }
    HINA_SCOPE_EXIT({
        hina_destroy_thread_context(shadow_ctx);
        hina_destroy_thread_context(main_ctx);
    });

    WorkerState shadow_worker;
    shadow_worker.ctx = shadow_ctx;
    shadow_worker.role = WorkerRole::Shadow;
    shadow_worker.thread = std::thread(worker_thread, &shadow_worker);

    WorkerState main_worker;
    main_worker.ctx = main_ctx;
    main_worker.role = WorkerRole::Main;
    main_worker.thread = std::thread(worker_thread, &main_worker);

    HINA_SCOPE_EXIT({
        {
            std::lock_guard<std::mutex> lock(shadow_worker.mutex);
            shadow_worker.exit = true;
            shadow_worker.cv.notify_all();
        }
        {
            std::lock_guard<std::mutex> lock(main_worker.mutex);
            main_worker.exit = true;
            main_worker.cv.notify_all();
        }
        shadow_worker.thread.join();
        main_worker.thread.join();
    });

    // ========================================================================
    // Main loop
    // ========================================================================

    app.camera.zoom = -scene.scene_radius * 2.0f;

    glm::vec3 light_dir = glm::normalize(glm::vec3(-0.6f, -1.0f, -0.4f));
    glm::vec3 light_color = glm::vec3(1.0f, 0.95f, 0.9f);
    float light_intensity = 5.0f;
    glm::vec3 ambient_color = glm::vec3(0.03f);

    uint32_t frame_count = 0;
    auto last_status_time = std::chrono::high_resolution_clock::now();

    // GPU timestamp query pool for per-pass timing
    // Need enough queries for TIMING_FRAME_DELAY frames to handle latency
    hina_query_pool_desc query_desc = {};
    query_desc.type = HINA_QUERY_TYPE_TIMESTAMP;
    query_desc.count = QUERIES_PER_FRAME * (TIMING_FRAME_DELAY + 1);
    hina_query_pool timing_pool = hina_make_query_pool(&query_desc);
    HINA_SCOPE_EXIT(if (hina_query_pool_is_valid(timing_pool)) hina_destroy_query_pool(timing_pool));

    PassTiming pass_timing = {};  // Stores timing results from previous frame

    // Performance statistics tracking
    hina_perf_stats perf;
    hina_perf_stats_init(&perf);

    while (hina_example_poll(&app)) {
        // Update perf stats with frame time from previous frame
        hina_perf_stats_update(&perf, app.delta_time);
        auto frame_start = std::chrono::high_resolution_clock::now();

        // Only update camera if ImGui doesn't want the mouse
        if (!hina_example_ui_want_mouse(&app)) {
            app.camera.update(app);
        }

        // hina_frame_begin() auto-manages registered child contexts (shadow_ctx, main_ctx)
        hina_swapchain_image swapchain = hina_frame_begin();
        if (swapchain.texture.id == HINA_INVALID_HANDLE) {
            hina_frame_end();
            SDL_Delay(10);
            continue;
        }

        uint32_t w = 0, h = 0;
        hina_get_texture_size(swapchain.texture, &w, &h);
        if (!hina_depth_buffer_resize(&depth_buffer, w, h)) {
            EXAMPLE_LOGE("Failed to resize depth buffer");
            hina_frame_end();
            break;
        }

        glm::mat4 view = app.camera.view_matrix();
        glm::mat4 proj = glm::perspective(glm::radians(60.0f),
                                          h > 0 ? (float)w / (float)h : 1.0f,
                                          0.1f, scene.scene_radius * 10.0f);
        proj[1][1] *= -1.0f;
        glm::mat4 view_proj = proj * view;

        glm::vec3 light_pos = scene.scene_center - light_dir * (scene.scene_radius * 2.0f);
        glm::mat4 light_view = glm::lookAt(light_pos, scene.scene_center, glm::vec3(0.0f, 1.0f, 0.0f));
        float ortho = scene.scene_radius * 1.5f;
        glm::mat4 light_proj = glm::ortho(-ortho, ortho, -ortho, ortho, 0.1f, scene.scene_radius * 4.0f);
        light_proj[1][1] *= -1.0f;
        glm::mat4 light_view_proj = light_proj * light_view;

        scene_ubo->view = view;
        scene_ubo->proj = proj;
        scene_ubo->view_proj = view_proj;
        scene_ubo->light_view_proj = light_view_proj;
        glm::vec3 cam_pos = glm::vec3(glm::inverse(view)[3]);
        scene_ubo->camera_pos = glm::vec4(cam_pos, 1.0f);
        scene_ubo->light_dir = glm::vec4(light_dir, 0.0f);
        scene_ubo->light_color = glm::vec4(light_color, light_intensity);
        scene_ubo->ambient_color = glm::vec4(ambient_color, 1.0f);

        extract_frustum_planes(view_proj, cull_params->frustum_planes);
        cull_params->draw_count = static_cast<uint32_t>(draw_commands.size());
        cull_params->pad0 = glm::vec3(0.0f);

        hina_pass_action shadow_pass = {};
        shadow_pass.colors[0].image = hina_texture_get_default_view(shadow_color);
        shadow_pass.colors[0].load_op = HINA_LOAD_OP_CLEAR;
        shadow_pass.colors[0].store_op = HINA_STORE_OP_DONT_CARE;
        shadow_pass.colors[0].clear_color[0] = 1.0f;
        shadow_pass.colors[0].clear_color[1] = 1.0f;
        shadow_pass.colors[0].clear_color[2] = 1.0f;
        shadow_pass.colors[0].clear_color[3] = 1.0f;
        shadow_pass.depth.image = hina_texture_get_default_view(shadow_depth);
        shadow_pass.depth.load_op = HINA_LOAD_OP_CLEAR;
        shadow_pass.depth.store_op = HINA_STORE_OP_STORE;
        shadow_pass.depth.depth_clear = 1.0f;
        shadow_pass.width = SHADOW_MAP_SIZE;
        shadow_pass.height = SHADOW_MAP_SIZE;

        hina_pass_action main_pass = {};
        main_pass.colors[0].image = hina_texture_get_default_view(swapchain.texture);
        main_pass.colors[0].load_op = HINA_LOAD_OP_CLEAR;
        main_pass.colors[0].store_op = HINA_STORE_OP_STORE;
        main_pass.colors[0].clear_color[0] = 0.02f;
        main_pass.colors[0].clear_color[1] = 0.02f;
        main_pass.colors[0].clear_color[2] = 0.03f;
        main_pass.colors[0].clear_color[3] = 1.0f;
        main_pass.depth.image = hina_texture_get_default_view(depth_buffer.texture);
        main_pass.depth.load_op = HINA_LOAD_OP_CLEAR;
        main_pass.depth.store_op = HINA_STORE_OP_STORE;
        main_pass.depth.depth_clear = 1.0f;
        main_pass.width = w;
        main_pass.height = h;

        FrameResources frame = {};
        frame.shadow_pass = shadow_pass;
        frame.main_pass = main_pass;
        frame.shadow_pipeline = shadow_pipeline;
        frame.pbr_pipeline = pbr_pipeline;
        frame.shadow_scene_group = shadow_scene_group;
        frame.shadow_object_group = shadow_object_group;
        frame.pbr_scene_group = pbr_scene_group;
        frame.pbr_object_group = pbr_object_group;
        frame.materials = materials.data();
        frame.material_ranges = material_ranges.data();
        frame.material_count = static_cast<uint32_t>(materials.size());
        frame.vertex_buffer = vertex_buffer;
        frame.index_buffer = index_buffer;
        frame.shadow_commands = shadow_draw_buffer;
        frame.draw_commands = draw_buffer;
        frame.draw_count = static_cast<uint32_t>(draw_commands.size());
        frame.shadow_color = shadow_color;
        frame.shadow_depth = shadow_depth;

        // GPU timing: set up query pool for this frame
        if (hina_query_pool_is_valid(timing_pool)) {
            uint32_t ring_index = frame_count % (TIMING_FRAME_DELAY + 1);
            frame.query_pool = timing_pool;
            frame.query_base = ring_index * QUERIES_PER_FRAME;

            // Read timing results from N frames ago (if available)
            if (frame_count >= TIMING_FRAME_DELAY) {
                uint32_t read_ring = (frame_count - TIMING_FRAME_DELAY) % (TIMING_FRAME_DELAY + 1);
                uint32_t read_base = read_ring * QUERIES_PER_FRAME;
                uint64_t timestamps[QUERIES_PER_FRAME];
                if (hina_get_query_results(timing_pool, read_base, QUERIES_PER_FRAME, timestamps, false)) {
                    pass_timing.shadow_ms = hina_timestamp_to_ns(timestamps[QUERY_SHADOW_END] - timestamps[QUERY_SHADOW_START]) / 1e6;
                    pass_timing.main_ms = hina_timestamp_to_ns(timestamps[QUERY_MAIN_END] - timestamps[QUERY_MAIN_START]) / 1e6;
                    pass_timing.valid = true;
                }
            }

            // Reset queries for this frame (must be done before writing)
            hina_cmd* reset_cmd = hina_cmd_begin_ex(HINA_QUEUE_GRAPHICS);
            if (reset_cmd) {
                hina_cmd_reset_query_pool(reset_cmd, timing_pool, frame.query_base, QUERIES_PER_FRAME);
                hina_frame_submit(reset_cmd);
            }
        }

        submit_worker_job(shadow_worker, &frame);
        submit_worker_job(main_worker, &frame);

        // Record compute culling pass
        hina_cmd* cull_cmd = hina_cmd_begin_ex(HINA_QUEUE_COMPUTE);
        if (cull_cmd) {
            // Acquire draw buffer from graphics queue (bidirectional ownership transfer)
            // Skip on first frame since buffer was created with COMPUTE ownership
            if (frame_count > 0) {
                hina_cmd_acquire_buffer(cull_cmd, draw_buffer, HINA_QUEUE_GRAPHICS);
            }

            hina_cmd_bind_pipeline(cull_cmd, cull_pipeline);
            hina_cmd_bind_group(cull_cmd, 0, cull_group);
            uint32_t group_count = (frame.draw_count + CULL_WORKGROUP_SIZE - 1) / CULL_WORKGROUP_SIZE;
            hina_cmd_dispatch(cull_cmd, group_count, 1, 1);

            // Release draw buffer to graphics queue (exclusive sharing mode)
            hina_cmd_release_buffer(cull_cmd, draw_buffer, HINA_QUEUE_GRAPHICS);
        }

        // Wait for workers to finish recording
        hina_cmd* shadow_cmd = wait_worker_job(shadow_worker);
        hina_cmd* main_cmd = wait_worker_job(main_worker);

        // Frame submission - no tickets, no timeline branching
        hina_sync_point shadow_done = HINA_SYNC_POINT_INVALID;
        hina_sync_point cull_done = HINA_SYNC_POINT_INVALID;

        if (shadow_cmd) {
            shadow_done = hina_frame_submit(shadow_cmd);
        }
        if (cull_cmd) {
            cull_done = hina_frame_submit(cull_cmd);
        }

        // Main pass waits on shadow and cull
        hina_sync_point main_done = HINA_SYNC_POINT_INVALID;
        if (main_cmd) {
            hina_frame_wait(HINA_QUEUE_GRAPHICS, shadow_done);
            hina_frame_wait(HINA_QUEUE_GRAPHICS, cull_done);
            main_done = hina_frame_submit(main_cmd);
        }

        // ====================================================================
        // ImGui Rendering (separate command buffer, after main pass)
        // ====================================================================
#ifdef HINA_EXAMPLE_HAS_IMGUI
        if (app.imgui_initialized) {
            // Start ImGui frame
            ImGuiIO& io = ImGui::GetIO();
            io.DisplaySize = ImVec2(static_cast<float>(w), static_cast<float>(h));
            io.DeltaTime = app.delta_time > 0.0f ? app.delta_time : (1.0f / 60.0f);
            ImGui::NewFrame();

            // Build stats overlay
            if (app.imgui_visible) {
                ImGui::SetNextWindowPos(ImVec2(10, 10), ImGuiCond_FirstUseEver);
                ImGui::SetNextWindowBgAlpha(0.5f);

                ImGuiWindowFlags overlay_flags =
                    ImGuiWindowFlags_NoDecoration |
                    ImGuiWindowFlags_AlwaysAutoResize |
                    ImGuiWindowFlags_NoSavedSettings |
                    ImGuiWindowFlags_NoFocusOnAppearing |
                    ImGuiWindowFlags_NoNav;

                if (ImGui::Begin("##gltf_stats", nullptr, overlay_flags)) {
                    // Unified telemetry from hina_example (device, FPS, GPU time, etc.)
                    hina_example_imgui_stats_content(&app, true);

                    // Scene-specific stats header
                    if (ImGui::CollapsingHeader("Scene", ImGuiTreeNodeFlags_DefaultOpen)) {
                        ImGui::Text("Meshes:     %zu", scene.draw_items.size());
                        ImGui::Text("Draw calls: %zu", draw_commands.size());
                        ImGui::Text("Materials:  %zu", scene.materials.size());
                        ImGui::Text("Textures:   %zu", scene.textures.size());
                        ImGui::Separator();
                        ImGui::Text("Vertices:   %zu", scene.vertices.size());
                        ImGui::Text("Triangles:  %zu", scene.indices.size() / 3);
                        ImGui::Separator();
                        ImGui::Text("Shadow map: %ux%u", SHADOW_MAP_SIZE, SHADOW_MAP_SIZE);
                    }

                    // GPU pass timing
                    if (pass_timing.valid && ImGui::CollapsingHeader("GPU Pass Timing")) {
                        ImGui::Text("Shadow: %.2f ms", pass_timing.shadow_ms);
                        ImGui::Text("Main:   %.2f ms", pass_timing.main_ms);
                        ImGui::Separator();
                        ImGui::Text("Total:  %.2f ms", pass_timing.shadow_ms + pass_timing.main_ms);
                    }
                }
                ImGui::End();
            }

            // Render ImGui
            ImGui::Render();
            ImDrawData* draw_data = ImGui::GetDrawData();

            // Cache input capture state
            ImGuiIO& io_post = ImGui::GetIO();
            app.imgui_want_mouse = io_post.WantCaptureMouse;
            app.imgui_want_keyboard = io_post.WantCaptureKeyboard;

            // Draw ImGui in a separate pass
            if (draw_data && draw_data->TotalVtxCount > 0) {
                hina_cmd* imgui_cmd = hina_cmd_begin_ex(HINA_QUEUE_GRAPHICS);
                if (imgui_cmd) {
                    // Wait for main pass to complete
                    hina_frame_wait(HINA_QUEUE_GRAPHICS, main_done);

                    // ImGui pass preserves scene content
                    hina_pass_action imgui_pass = {};
                    imgui_pass.colors[0].image = hina_texture_get_default_view(swapchain.texture);
                    imgui_pass.colors[0].load_op = HINA_LOAD_OP_LOAD;
                    imgui_pass.colors[0].store_op = HINA_STORE_OP_STORE;

                    hina_cmd_begin_pass(imgui_cmd, &imgui_pass);

                    hina_viewport viewport = {0.0f, 0.0f, draw_data->DisplaySize.x, draw_data->DisplaySize.y, 0.0f, 1.0f};
                    hina_cmd_set_viewport(imgui_cmd, &viewport);

                    hina_example_draw_imgui_data(&app, imgui_cmd, draw_data);

                    hina_cmd_end_pass(imgui_cmd);
                    hina_frame_submit(imgui_cmd);
                }
            }
        }
#endif

        // hina_frame_end() auto-manages registered child contexts
        hina_frame_end();

        // Frame timing
        frame_count++;
        auto frame_end_time = std::chrono::high_resolution_clock::now();
        double frame_ms = std::chrono::duration<double, std::milli>(frame_end_time - frame_start).count();

        // Stall detection: warn if frame took > 100ms (indicates blocking)
        if (frame_ms > 100.0) {
            printf("WARNING: Frame %u took %.2f ms - possible stall!\n", frame_count, frame_ms);
        }
    }

    // Print final performance summary
    hina_perf_stats_print(&perf);

    return 0;
}

