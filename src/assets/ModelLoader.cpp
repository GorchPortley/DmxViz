#include "assets/ModelLoader.h"

#include "core/Limits.h"

// The cgltf implementation is compiled once elsewhere (a non-strict third
// party target); this file only uses the API.
#include "cgltf.h"

#include <algorithm>
#include <array>
#include <bit>
#include <cctype>
#include <charconv>
#include <cmath>
#include <cstdlib>
#include <cstring>
#include <exception>
#include <format>
#include <fstream>
#include <filesystem>
#include <functional>
#include <iterator>
#include <unordered_map>
#include <utility>

// Supported formats and limits:
//   glTF 2.0 (.gltf / .glb): triangle, strip and fan primitives of all meshes
//     reachable from the default scene, with node transforms baked into
//     ModelPart::transform. Indices of any type. Buffers: GLB binary chunk,
//     base64 data URIs, external files (next to the file for path loads,
//     through the FileResolver for memory loads). Material: PBR base colour /
//     metallic / roughness / emissive factors (+ KHR_materials_emissive_strength,
//     KHR_materials_pbrSpecularGlossiness). Textures, skins, morph targets,
//     Draco and meshopt compression are not supported.
//   Wavefront OBJ: v / vt / vn / f (polygons are fan-triangulated, negative
//     indices allowed), usemtl + mtllib (Kd, Ke, Ns, Pr, Pm). One part per material.
//   3DS: MAIN3DS > EDIT3DS > EDIT_OBJECT > OBJ_TRIMESH with vertices, faces,
//     texture coordinates, material groups and smoothing groups; materials with
//     diffuse colour and shininess. Vertices are in the file's world space.
// Missing normals are computed: flat for glTF (as the spec requires), with a
// 45 degree crease angle for OBJ, from smoothing groups for 3DS.

namespace dmxviz::assets {
namespace {

using ReadFileFn = std::function<std::optional<std::vector<std::uint8_t>>(std::string_view uri)>;

constexpr float kObjCreaseCos = 0.70710678f;  // 45 degrees
constexpr float kFlatCreaseCos = 0.9999f;

std::string lower(std::string_view s) {
    std::string r(s);
    for (char& c : r) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    return r;
}

std::filesystem::path pathFromUtf8(std::string_view utf8) {
    return std::filesystem::path(std::u8string(reinterpret_cast<const char8_t*>(utf8.data()), utf8.size()));
}

std::optional<std::vector<std::uint8_t>> readWholeFile(const std::filesystem::path& path) {
    std::error_code sizeError;
    const std::uintmax_t size = std::filesystem::file_size(path, sizeError);
    if (!sizeError && size > limits::kMaxFileBytes) return std::nullopt;  // refuse absurd files up front
    std::ifstream in(path, std::ios::binary);
    if (!in) return std::nullopt;
    return std::vector<std::uint8_t>((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
}

// Linear RGB from an 8-bit/float sRGB-encoded value.
float srgbToLinear(float c) {
    c = std::clamp(c, 0.0f, 1.0f);
    return c <= 0.04045f ? c / 12.92f : std::pow((c + 0.055f) / 1.055f, 2.4f);
}

// Scale and Z-up -> Y-up conversion: (x, y, z) -> (x, z, -y) * scale. Built
// from exact columns so a pure axis swap has no rounding noise.
glm::mat4 conversionMatrix(const ModelLoadOptions& opts) {
    const float s = (opts.unitScale > 0.0f && std::isfinite(opts.unitScale)) ? opts.unitScale : 1.0f;
    glm::mat4 m(1.0f);
    if (opts.zUp) {
        m[1] = glm::vec4(0.0f, 0.0f, -1.0f, 0.0f);  // source +Y -> -Z
        m[2] = glm::vec4(0.0f, 1.0f, 0.0f, 0.0f);   // source +Z -> +Y
    }
    m[0] *= s;
    m[1] *= s;
    m[2] *= s;
    return m;
}

// Applies a rotation + uniform scale to vertices in place (no winding change).
void transformInPlace(MeshData& m, const glm::mat4& c) {
    if (c == glm::mat4(1.0f)) return;
    const glm::mat3 r = glm::mat3(c);
    for (Vertex& v : m.vertices) {
        v.position = glm::vec3(c * glm::vec4(v.position, 1.0f));
        const glm::vec3 n = r * v.normal;
        const float len = glm::length(n);
        v.normal = len > 1e-12f ? n / len : glm::vec3(0.0f, 1.0f, 0.0f);
    }
}

std::uint32_t floatKey(float f) { return f == 0.0f ? 0u : std::bit_cast<std::uint32_t>(f); }

struct Vec3Key {
    std::uint32_t x, y, z;
    bool operator==(const Vec3Key&) const = default;
};
struct Vec3KeyHash {
    std::size_t operator()(const Vec3Key& k) const {
        return (static_cast<std::size_t>(k.x) * 73856093u) ^ (static_cast<std::size_t>(k.y) * 19349663u) ^
               (static_cast<std::size_t>(k.z) * 83492791u);
    }
};
Vec3Key keyOf(const glm::vec3& v) { return {floatKey(v.x), floatKey(v.y), floatKey(v.z)}; }

struct CornerKey {
    std::uint32_t vertex;
    Vec3Key normal;
    bool operator==(const CornerKey&) const = default;
};
struct CornerKeyHash {
    std::size_t operator()(const CornerKey& k) const {
        return Vec3KeyHash{}(k.normal) ^ (static_cast<std::size_t>(k.vertex) * 2654435761u);
    }
};

// Recomputes normals per triangle corner. Two triangles sharing a position
// are smoothed together when they share a smoothing group (3DS) or, without
// groups, when the angle between them is below the crease angle. Vertices are
// split where a position needs several normals.
void generateNormals(MeshData& m, float creaseCos, const std::vector<std::uint32_t>* groups = nullptr) {
    const std::size_t triCount = m.indices.size() / 3;
    // Weld by position so smoothing works across UV seams.
    std::unordered_map<Vec3Key, std::uint32_t, Vec3KeyHash> posIds;
    std::vector<std::uint32_t> vertexPos(m.vertices.size());
    for (std::size_t i = 0; i < m.vertices.size(); ++i) {
        const auto it = posIds.emplace(keyOf(m.vertices[i].position), static_cast<std::uint32_t>(posIds.size())).first;
        vertexPos[i] = it->second;
    }
    std::vector<glm::vec3> faceNormal(triCount), faceUnit(triCount);
    for (std::size_t t = 0; t < triCount; ++t) {
        const glm::vec3 a = m.vertices[m.indices[3 * t]].position;
        const glm::vec3 b = m.vertices[m.indices[3 * t + 1]].position;
        const glm::vec3 c = m.vertices[m.indices[3 * t + 2]].position;
        faceNormal[t] = glm::cross(b - a, c - a);
        const float len = glm::length(faceNormal[t]);
        faceUnit[t] = len > 1e-20f ? faceNormal[t] / len : glm::vec3(0.0f);
    }
    // Triangles touching each position (compressed adjacency lists).
    std::vector<std::uint32_t> start(posIds.size() + 1, 0);
    for (std::uint32_t idx : m.indices) ++start[vertexPos[idx] + 1];
    for (std::size_t i = 1; i < start.size(); ++i) start[i] += start[i - 1];
    std::vector<std::uint32_t> fill(start.begin(), start.end() - 1);
    std::vector<std::uint32_t> adjacency(m.indices.size());
    for (std::size_t i = 0; i < m.indices.size(); ++i)
        adjacency[fill[vertexPos[m.indices[i]]]++] = static_cast<std::uint32_t>(i / 3);

    MeshData out;
    out.name = m.name;
    out.vertices.reserve(m.vertices.size());
    out.indices.reserve(m.indices.size());
    std::unordered_map<CornerKey, std::uint32_t, CornerKeyHash> remap;
    for (std::size_t i = 0; i < m.indices.size(); ++i) {
        const std::size_t t = i / 3;
        const std::uint32_t v = m.indices[i];
        const std::uint32_t p = vertexPos[v];
        glm::vec3 n(0.0f);
        for (std::uint32_t k = start[p]; k < start[p + 1]; ++k) {
            const std::uint32_t g = adjacency[k];
            const bool smooth = g == t || (groups ? (((*groups)[t] & (*groups)[g]) != 0)
                                                  : glm::dot(faceUnit[t], faceUnit[g]) >= creaseCos);
            if (smooth) n += faceNormal[g];
        }
        const float len = glm::length(n);
        if (len > 1e-20f)
            n /= len;
        else
            n = glm::length(faceUnit[t]) > 0.5f ? faceUnit[t] : glm::vec3(0.0f, 1.0f, 0.0f);
        auto [it, inserted] = remap.emplace(CornerKey{v, keyOf(n)}, static_cast<std::uint32_t>(out.vertices.size()));
        if (inserted) out.vertices.push_back({m.vertices[v].position, n, m.vertices[v].uv});
        out.indices.push_back(it->second);
    }
    m = std::move(out);
}

void normalizeNormals(MeshData& m) {
    for (Vertex& v : m.vertices) {
        const float len = glm::length(v.normal);
        v.normal = len > 1e-12f ? v.normal / len : glm::vec3(0.0f, 1.0f, 0.0f);
    }
}

Material defaultMaterial() { return Material{glm::vec3(0.8f), 0.6f, 0.0f, glm::vec3(0.0f)}; }

void finishModel(ModelData& model) {
    model.bounds = Aabb{};
    for (ModelPart& p : model.parts) {
        p.mesh.computeBounds();
        model.bounds.expand(p.mesh.bounds.transformed(p.transform));
    }
}

// Running totals for one model. Checked while parts are added so a hostile file (a few instanced
// nodes of a huge mesh, millions of materials) stops early with an error instead of exhausting memory.
struct ModelBudget {
    std::size_t triangles = 0;
    std::size_t vertices = 0;
    std::size_t parts = 0;

    bool add(const MeshData& mesh, std::string& error) {
        triangles += mesh.indices.size() / 3;
        vertices += mesh.vertices.size();
        if (++parts > limits::kMaxModelParts || triangles > limits::kMaxTriangles ||
            vertices > limits::kMaxVertices) {
            error = std::format("the model is too large (more than {} million triangles or {} parts)",
                                limits::kMaxTriangles / 1'000'000, limits::kMaxModelParts);
            return false;
        }
        return true;
    }
};

// =============================================================================
// glTF 2.0
// =============================================================================

struct GltfFiles {
    const ReadFileFn* readFile = nullptr;
    std::string error;
};

cgltf_result gltfReadFile(const cgltf_memory_options*, const cgltf_file_options* fileOptions, const char* path,
                          cgltf_size* size, void** data) {
    auto* files = static_cast<GltfFiles*>(fileOptions->user_data);
    if (!files->readFile || !*files->readFile) {
        files->error = std::format("external file '{}' is referenced but no file resolver was given", path);
        return cgltf_result_file_not_found;
    }
    std::optional<std::vector<std::uint8_t>> bytes = (*files->readFile)(path);
    if (!bytes) {
        files->error = std::format("cannot read external file '{}'", path);
        return cgltf_result_file_not_found;
    }
    if (size && *size > bytes->size()) {
        files->error = std::format("external file '{}' is shorter than its declared size", path);
        return cgltf_result_data_too_short;
    }
    void* mem = std::malloc(std::max<std::size_t>(bytes->size(), 1));
    if (!mem) return cgltf_result_out_of_memory;
    if (!bytes->empty()) std::memcpy(mem, bytes->data(), bytes->size());
    if (size) *size = bytes->size();
    *data = mem;
    return cgltf_result_success;
}

void gltfReleaseFile(const cgltf_memory_options*, const cgltf_file_options*, void* data) { std::free(data); }

const char* gltfResultText(cgltf_result r) {
    switch (r) {
        case cgltf_result_success: return "success";
        case cgltf_result_data_too_short: return "data too short / truncated buffer";
        case cgltf_result_unknown_format: return "unknown format";
        case cgltf_result_invalid_json: return "invalid JSON";
        case cgltf_result_invalid_gltf: return "invalid glTF";
        case cgltf_result_invalid_options: return "invalid options";
        case cgltf_result_file_not_found: return "file not found";
        case cgltf_result_io_error: return "I/O error";
        case cgltf_result_out_of_memory: return "out of memory";
        case cgltf_result_legacy_gltf: return "glTF 1.0 is not supported";
        default: return "error";
    }
}

struct GltfGuard {
    cgltf_data* data = nullptr;
    ~GltfGuard() {
        if (data) cgltf_free(data);
    }
};

Material gltfMaterial(const cgltf_material* mat) {
    if (!mat) return defaultMaterial();
    Material m;
    if (mat->has_pbr_specular_glossiness && !mat->has_pbr_metallic_roughness) {
        const auto& sg = mat->pbr_specular_glossiness;
        m.albedo = glm::vec3(sg.diffuse_factor[0], sg.diffuse_factor[1], sg.diffuse_factor[2]);
        m.roughness = 1.0f - sg.glossiness_factor;
        m.metallic = 0.0f;
    } else {
        // cgltf fills the spec defaults when the block is absent.
        const auto& pbr = mat->pbr_metallic_roughness;
        m.albedo = glm::vec3(pbr.base_color_factor[0], pbr.base_color_factor[1], pbr.base_color_factor[2]);
        m.roughness = pbr.roughness_factor;
        m.metallic = pbr.metallic_factor;
    }
    m.emissive = glm::vec3(mat->emissive_factor[0], mat->emissive_factor[1], mat->emissive_factor[2]);
    if (mat->has_emissive_strength) m.emissive *= mat->emissive_strength.emissive_strength;
    m.roughness = std::clamp(m.roughness, 0.0f, 1.0f);
    m.metallic = std::clamp(m.metallic, 0.0f, 1.0f);
    return m;
}

// Reads `count` elements of `components` floats each; false on failure.
bool unpackFloats(const cgltf_accessor* acc, std::size_t components, std::vector<float>& out) {
    if (cgltf_num_components(acc->type) != components) return false;
    out.resize(acc->count * components);
    return cgltf_accessor_unpack_floats(acc, out.data(), out.size()) == out.size();
}

// Converts one primitive. An empty mesh means "nothing to draw" (points, lines).
bool gltfPrimitive(const cgltf_primitive& prim, MeshData& mesh, std::string& error) {
    if (prim.has_draco_mesh_compression) {
        error = "Draco-compressed meshes are not supported";
        return false;
    }
    if (prim.type != cgltf_primitive_type_triangles && prim.type != cgltf_primitive_type_triangle_strip &&
        prim.type != cgltf_primitive_type_triangle_fan)
        return true;
    const cgltf_accessor* position = nullptr;
    const cgltf_accessor* normal = nullptr;
    const cgltf_accessor* texcoord = nullptr;
    for (cgltf_size i = 0; i < prim.attributes_count; ++i) {
        const cgltf_attribute& a = prim.attributes[i];
        if (a.type == cgltf_attribute_type_position && a.index == 0) position = a.data;
        if (a.type == cgltf_attribute_type_normal && a.index == 0) normal = a.data;
        if (a.type == cgltf_attribute_type_texcoord && a.index == 0) texcoord = a.data;
    }
    if (!position || position->count == 0) return true;
    // An accessor without a buffer view claims any count; check before allocating for it.
    if (position->count > limits::kMaxVertices || (prim.indices && prim.indices->count > 3 * limits::kMaxTriangles)) {
        error = std::format("mesh with {} vertices is too large", position->count);
        return false;
    }
    const std::size_t count = position->count;
    std::vector<float> pos, nrm, uv;
    if (!unpackFloats(position, 3, pos)) {
        error = "cannot read vertex positions";
        return false;
    }
    const bool hasNormals = normal && normal->count == count && unpackFloats(normal, 3, nrm);
    const bool hasUv = texcoord && texcoord->count == count && unpackFloats(texcoord, 2, uv);

    mesh.vertices.resize(count);
    for (std::size_t i = 0; i < count; ++i) {
        Vertex& v = mesh.vertices[i];
        v.position = {pos[3 * i], pos[3 * i + 1], pos[3 * i + 2]};
        if (hasNormals) v.normal = {nrm[3 * i], nrm[3 * i + 1], nrm[3 * i + 2]};
        if (hasUv) v.uv = {uv[2 * i], uv[2 * i + 1]};
    }

    std::vector<std::uint32_t> order;
    if (prim.indices) {
        order.resize(prim.indices->count);
        for (cgltf_size i = 0; i < prim.indices->count; ++i) {
            const cgltf_size idx = cgltf_accessor_read_index(prim.indices, i);
            if (idx >= count) {
                error = std::format("vertex index {} out of range ({} vertices)", idx, count);
                return false;
            }
            order[i] = static_cast<std::uint32_t>(idx);
        }
    } else {
        order.resize(count);
        for (std::size_t i = 0; i < count; ++i) order[i] = static_cast<std::uint32_t>(i);
    }

    if (prim.type == cgltf_primitive_type_triangles) {
        order.resize(order.size() - order.size() % 3);
        mesh.indices = std::move(order);
    } else {
        for (std::size_t i = 2; i < order.size(); ++i) {
            if (prim.type == cgltf_primitive_type_triangle_fan) {
                mesh.indices.insert(mesh.indices.end(), {order[0], order[i - 1], order[i]});
            } else if (i % 2 == 0) {
                mesh.indices.insert(mesh.indices.end(), {order[i - 2], order[i - 1], order[i]});
            } else {
                mesh.indices.insert(mesh.indices.end(), {order[i - 1], order[i - 2], order[i]});
            }
        }
    }
    if (hasNormals)
        normalizeNormals(mesh);
    else
        generateNormals(mesh, kFlatCreaseCos);
    return true;
}

bool gltfNode(const cgltf_node* node, const glm::mat4& conversion, ModelData& model, ModelBudget& budget,
              std::size_t& visits, std::string& error, int depth) {
    if (depth > 256) {
        error = "node hierarchy too deep";
        return false;
    }
    // A node may be listed as child several times, so a small file can describe billions of visits.
    if (++visits > limits::kMaxModelNodeVisits) {
        error = "glTF scene has too many node instances";
        return false;
    }
    if (node->mesh) {
        float m[16];
        cgltf_node_transform_world(node, m);
        const glm::mat4 world = conversion * glm::make_mat4(m);
        const cgltf_mesh& mesh = *node->mesh;
        for (cgltf_size i = 0; i < mesh.primitives_count; ++i) {
            ModelPart part;
            if (!gltfPrimitive(mesh.primitives[i], part.mesh, error)) {
                error = std::format("mesh '{}': {}", mesh.name ? mesh.name : "", error);
                return false;
            }
            if (part.mesh.indices.empty()) continue;
            if (!budget.add(part.mesh, error)) return false;
            part.mesh.name = mesh.name ? mesh.name : (node->name ? node->name : "");
            part.material = gltfMaterial(mesh.primitives[i].material);
            part.transform = world;
            model.parts.push_back(std::move(part));
        }
    }
    for (cgltf_size i = 0; i < node->children_count; ++i) {
        if (!gltfNode(node->children[i], conversion, model, budget, visits, error, depth + 1)) return false;
    }
    return true;
}

bool loadGltf(std::span<const std::uint8_t> bytes, const ModelLoadOptions& opts, const ReadFileFn& readFile,
              ModelData& model, std::string& error) {
    GltfFiles files;
    files.readFile = &readFile;
    cgltf_options options{};
    options.file.read = &gltfReadFile;
    options.file.release = &gltfReleaseFile;
    options.file.user_data = &files;

    // cgltf skips unknown JSON (extras, extensions) recursively: refuse deeply nested documents first.
    // For a GLB only the JSON chunk is scanned (the binary chunk is not text).
    std::span<const std::uint8_t> jsonText = bytes;
    if (bytes.size() >= 20 && std::memcmp(bytes.data(), "glTF", 4) == 0) {
        std::uint32_t chunkLength = 0;
        std::memcpy(&chunkLength, bytes.data() + 12, 4);  // little endian on every supported platform
        jsonText = bytes.subspan(20, std::min<std::size_t>(chunkLength, bytes.size() - 20));
    }
    if (limits::jsonNestedTooDeeply(std::string_view(reinterpret_cast<const char*>(jsonText.data()), jsonText.size()),
                                    64)) {
        error = "glTF: JSON is nested too deeply";
        return false;
    }

    GltfGuard guard;
    cgltf_result r = cgltf_parse(&options, bytes.data(), bytes.size(), &guard.data);
    if (r != cgltf_result_success) {
        error = std::format("glTF: {}", gltfResultText(r));
        return false;
    }
    // A base path without a directory makes cgltf hand relative URIs to our
    // reader unchanged (percent-decoded).
    r = cgltf_load_buffers(&options, guard.data, "model.gltf");
    if (r != cgltf_result_success) {
        error = std::format("glTF buffers: {}", files.error.empty() ? gltfResultText(r) : files.error);
        return false;
    }
    r = cgltf_validate(guard.data);
    if (r != cgltf_result_success) {
        error = std::format("glTF validation: {}", gltfResultText(r));
        return false;
    }
    const cgltf_data* data = guard.data;
    for (cgltf_size i = 0; i < data->buffer_views_count; ++i) {
        if (data->buffer_views[i].has_meshopt_compression) {
            error = "glTF: meshopt-compressed buffers are not supported";
            return false;
        }
    }

    const glm::mat4 conversion = conversionMatrix(opts);
    ModelBudget budget;
    std::size_t visits = 0;
    const cgltf_scene* scene = data->scene ? data->scene : (data->scenes_count > 0 ? &data->scenes[0] : nullptr);
    if (scene) {
        for (cgltf_size i = 0; i < scene->nodes_count; ++i) {
            if (!gltfNode(scene->nodes[i], conversion, model, budget, visits, error, 0)) return false;
        }
    } else if (data->nodes_count > 0) {
        for (cgltf_size i = 0; i < data->nodes_count; ++i) {
            if (!data->nodes[i].parent &&
                !gltfNode(&data->nodes[i], conversion, model, budget, visits, error, 0))
                return false;
        }
    } else {
        // No node hierarchy at all: show every mesh at the origin.
        for (cgltf_size i = 0; i < data->meshes_count; ++i) {
            for (cgltf_size j = 0; j < data->meshes[i].primitives_count; ++j) {
                ModelPart part;
                if (!gltfPrimitive(data->meshes[i].primitives[j], part.mesh, error)) return false;
                if (part.mesh.indices.empty()) continue;
                if (!budget.add(part.mesh, error)) return false;
                part.material = gltfMaterial(data->meshes[i].primitives[j].material);
                part.transform = conversion;
                model.parts.push_back(std::move(part));
            }
        }
    }
    return true;
}

// =============================================================================
// Text helpers (OBJ / MTL)
// =============================================================================

bool isSpace(char c) { return c == ' ' || c == '\t' || c == '\r' || c == '\v' || c == '\f'; }

std::string_view trim(std::string_view s) {
    while (!s.empty() && isSpace(s.front())) s.remove_prefix(1);
    while (!s.empty() && isSpace(s.back())) s.remove_suffix(1);
    return s;
}

// Splits off the next whitespace-separated token.
std::string_view nextToken(std::string_view& rest) {
    std::size_t i = 0;
    while (i < rest.size() && isSpace(rest[i])) ++i;
    std::size_t j = i;
    while (j < rest.size() && !isSpace(rest[j])) ++j;
    const std::string_view tok = rest.substr(i, j - i);
    rest.remove_prefix(j);
    return tok;
}

bool parseFloat(std::string_view tok, float& out) {
    if (!tok.empty() && tok.front() == '+') tok.remove_prefix(1);
    if (tok.empty()) return false;
    const auto r = std::from_chars(tok.data(), tok.data() + tok.size(), out);
    return r.ec == std::errc() && r.ptr == tok.data() + tok.size();
}

bool parseInt(std::string_view tok, long& out) {
    if (!tok.empty() && tok.front() == '+') tok.remove_prefix(1);
    if (tok.empty()) return false;
    const auto r = std::from_chars(tok.data(), tok.data() + tok.size(), out);
    return r.ec == std::errc() && r.ptr == tok.data() + tok.size();
}

bool parseFloats(std::string_view rest, float* out, int n) {
    for (int i = 0; i < n; ++i) {
        if (!parseFloat(nextToken(rest), out[i])) return false;
    }
    return true;
}

// Calls fn(lineNumber, line) for each line with comments and whitespace trimmed.
template <typename Fn>
bool forEachLine(std::string_view text, Fn&& fn) {
    std::size_t pos = 0;
    int lineNo = 0;
    while (pos < text.size()) {
        std::size_t end = text.find('\n', pos);
        if (end == std::string_view::npos) end = text.size();
        std::string_view line = text.substr(pos, end - pos);
        pos = end + 1;
        ++lineNo;
        if (const std::size_t hash = line.find('#'); hash != std::string_view::npos) line = line.substr(0, hash);
        line = trim(line);
        if (line.empty()) continue;
        if (!fn(lineNo, line)) return false;
    }
    return true;
}

// =============================================================================
// Wavefront MTL
// =============================================================================

void parseMtl(std::string_view text, std::unordered_map<std::string, Material>& materials) {
    std::string current;
    std::unordered_map<std::string, bool> hasRoughness;
    forEachLine(text, [&](int, std::string_view line) {
        std::string_view rest = line;
        const std::string_view key = nextToken(rest);
        float v[3];
        if (key == "newmtl") {
            current = std::string(trim(rest));
            materials[current] = defaultMaterial();
        } else if (current.empty()) {
            return true;
        } else if (key == "Kd" && parseFloats(rest, v, 3)) {
            materials[current].albedo = glm::clamp(glm::vec3(v[0], v[1], v[2]), 0.0f, 1.0f);
        } else if (key == "Ke" && parseFloats(rest, v, 3)) {
            materials[current].emissive = glm::max(glm::vec3(v[0], v[1], v[2]), 0.0f);
        } else if (key == "Pr" && parseFloats(rest, v, 1)) {
            materials[current].roughness = std::clamp(v[0], 0.0f, 1.0f);
            hasRoughness[current] = true;
        } else if (key == "Pm" && parseFloats(rest, v, 1)) {
            materials[current].metallic = std::clamp(v[0], 0.0f, 1.0f);
        } else if (key == "Ns" && parseFloats(rest, v, 1) && !hasRoughness[current]) {
            // Phong exponent to roughness (Blinn-Phong <-> GGX approximation).
            materials[current].roughness = std::clamp(std::sqrt(2.0f / (std::max(v[0], 0.0f) + 2.0f)), 0.02f, 1.0f);
        }
        return true;
    });
}

// =============================================================================
// Wavefront OBJ
// =============================================================================

struct ObjIndexKey {
    long v, vt, vn;
    bool operator==(const ObjIndexKey&) const = default;
};
struct ObjIndexKeyHash {
    std::size_t operator()(const ObjIndexKey& k) const {
        return static_cast<std::size_t>(k.v) * 73856093u ^ static_cast<std::size_t>(k.vt) * 19349663u ^
               static_cast<std::size_t>(k.vn) * 83492791u;
    }
};

struct ObjPart {
    std::string material;
    MeshData mesh;
    std::unordered_map<ObjIndexKey, std::uint32_t, ObjIndexKeyHash> vertexOf;
    bool missingNormals = false;
};

// Resolves an OBJ index (1-based, negative = relative to the end) to 0-based.
bool objIndex(std::string_view tok, std::size_t count, long& out) {
    long i = 0;
    if (!parseInt(tok, i) || i == 0) return false;
    out = i > 0 ? i - 1 : static_cast<long>(count) + i;
    return out >= 0 && static_cast<std::size_t>(out) < count;
}

bool loadObj(std::span<const std::uint8_t> bytes, const ModelLoadOptions& opts, const ReadFileFn& readFile,
             ModelData& model, std::string& error) {
    const std::string_view text(reinterpret_cast<const char*>(bytes.data()), bytes.size());
    std::vector<glm::vec3> positions, normals;
    std::vector<glm::vec2> uvs;
    std::unordered_map<std::string, Material> materials;
    std::vector<ObjPart> parts;
    std::unordered_map<std::string, std::size_t> partOf;
    std::string currentMaterial;
    ObjPart* part = nullptr;
    std::size_t triangleTotal = 0;
    const std::string tooLarge = std::format("OBJ: more than {} million triangles or vertices", limits::kMaxTriangles / 1'000'000);

    auto selectPart = [&](const std::string& name) {
        auto [it, inserted] = partOf.emplace(name, parts.size());
        if (inserted) {
            parts.emplace_back();
            parts.back().material = name;
        }
        part = &parts[it->second];
    };

    const bool ok = forEachLine(text, [&](int lineNo, std::string_view line) {
        std::string_view rest = line;
        const std::string_view key = nextToken(rest);
        float f[3] = {0, 0, 0};
        if ((key == "v" || key == "vn" || key == "vt") &&
            positions.size() + normals.size() + uvs.size() >= limits::kMaxVertices) {
            error = tooLarge;
            return false;
        }
        if (key == "v") {
            if (!parseFloats(rest, f, 3)) {
                error = std::format("OBJ line {}: bad vertex", lineNo);
                return false;
            }
            positions.emplace_back(f[0], f[1], f[2]);
        } else if (key == "vn") {
            if (!parseFloats(rest, f, 3)) {
                error = std::format("OBJ line {}: bad normal", lineNo);
                return false;
            }
            normals.emplace_back(f[0], f[1], f[2]);
        } else if (key == "vt") {
            if (!parseFloat(nextToken(rest), f[0])) {
                error = std::format("OBJ line {}: bad texture coordinate", lineNo);
                return false;
            }
            parseFloat(nextToken(rest), f[1]);  // v is optional
            uvs.emplace_back(f[0], f[1]);
        } else if (key == "f") {
            if (!part) selectPart(currentMaterial);
            std::vector<std::uint32_t> corners;
            for (std::string_view tok = nextToken(rest); !tok.empty(); tok = nextToken(rest)) {
                ObjIndexKey k{-1, -1, -1};
                const std::size_t s1 = tok.find('/');
                const std::string_view vTok = tok.substr(0, s1);
                std::string_view vtTok, vnTok;
                if (s1 != std::string_view::npos) {
                    const std::string_view after = tok.substr(s1 + 1);
                    const std::size_t s2 = after.find('/');
                    vtTok = after.substr(0, s2);
                    if (s2 != std::string_view::npos) vnTok = after.substr(s2 + 1);
                }
                if (!objIndex(vTok, positions.size(), k.v) || (!vtTok.empty() && !objIndex(vtTok, uvs.size(), k.vt)) ||
                    (!vnTok.empty() && !objIndex(vnTok, normals.size(), k.vn))) {
                    error = std::format("OBJ line {}: bad face index '{}'", lineNo, tok);
                    return false;
                }
                auto [it, inserted] =
                    part->vertexOf.emplace(k, static_cast<std::uint32_t>(part->mesh.vertices.size()));
                if (inserted) {
                    Vertex v;
                    v.position = positions[static_cast<std::size_t>(k.v)];
                    if (k.vt >= 0) v.uv = uvs[static_cast<std::size_t>(k.vt)];
                    if (k.vn >= 0)
                        v.normal = normals[static_cast<std::size_t>(k.vn)];
                    else
                        part->missingNormals = true;
                    part->mesh.vertices.push_back(v);
                }
                corners.push_back(it->second);
            }
            if (corners.size() < 3) {
                error = std::format("OBJ line {}: face with fewer than 3 vertices", lineNo);
                return false;
            }
            triangleTotal += corners.size() - 2;
            if (triangleTotal > limits::kMaxTriangles) {
                error = tooLarge;
                return false;
            }
            for (std::size_t i = 2; i < corners.size(); ++i)
                part->mesh.indices.insert(part->mesh.indices.end(), {corners[0], corners[i - 1], corners[i]});
        } else if (key == "usemtl") {
            currentMaterial = std::string(trim(rest));
            selectPart(currentMaterial);
        } else if (key == "mtllib") {
            const std::string_view all = trim(rest);
            std::optional<std::vector<std::uint8_t>> mtl = readFile ? readFile(all) : std::nullopt;
            if (!mtl) {
                // Several libraries may be listed on one line.
                for (std::string_view name = nextToken(rest); !name.empty() && !mtl; name = nextToken(rest)) {
                    if (readFile && name != all) mtl = readFile(name);
                }
            }
            if (mtl) parseMtl(std::string_view(reinterpret_cast<const char*>(mtl->data()), mtl->size()), materials);
        }
        // o, g, s, l, p and unknown statements are ignored.
        return true;
    });
    if (!ok) return false;

    const glm::mat4 conversion = conversionMatrix(opts);
    for (ObjPart& p : parts) {
        if (p.mesh.indices.empty()) continue;
        if (p.missingNormals)
            generateNormals(p.mesh, kObjCreaseCos);
        else
            normalizeNormals(p.mesh);
        transformInPlace(p.mesh, conversion);
        ModelPart out;
        out.mesh = std::move(p.mesh);
        out.mesh.name = p.material;
        auto it = materials.find(p.material);
        out.material = it != materials.end() ? it->second : defaultMaterial();
        model.parts.push_back(std::move(out));
    }
    return true;
}

// =============================================================================
// Autodesk 3DS
// =============================================================================

namespace c3ds {
constexpr std::uint16_t kMain = 0x4D4D;
constexpr std::uint16_t kEditor = 0x3D3D;
constexpr std::uint16_t kObject = 0x4000;
constexpr std::uint16_t kTriMesh = 0x4100;
constexpr std::uint16_t kVertices = 0x4110;
constexpr std::uint16_t kFaces = 0x4120;
constexpr std::uint16_t kFaceMaterial = 0x4130;
constexpr std::uint16_t kTexCoords = 0x4140;
constexpr std::uint16_t kSmoothing = 0x4150;
constexpr std::uint16_t kMaterial = 0xAFFF;
constexpr std::uint16_t kMatName = 0xA000;
constexpr std::uint16_t kMatDiffuse = 0xA020;
constexpr std::uint16_t kMatShininess = 0xA040;
constexpr std::uint16_t kColorF = 0x0010;
constexpr std::uint16_t kColor24 = 0x0011;
constexpr std::uint16_t kLinColor24 = 0x0012;
constexpr std::uint16_t kLinColorF = 0x0013;
constexpr std::uint16_t kIntPercent = 0x0030;
constexpr std::uint16_t kFloatPercent = 0x0031;
}  // namespace c3ds

// Bounds-checked little-endian reader over the file.
class ByteReader {
public:
    explicit ByteReader(std::span<const std::uint8_t> data) : data_(data) {}
    std::size_t size() const { return data_.size(); }

    bool u16(std::size_t at, std::uint16_t& out) const {
        if (at + 2 > data_.size()) return false;
        out = static_cast<std::uint16_t>(data_[at] | (data_[at + 1] << 8));
        return true;
    }
    bool u32(std::size_t at, std::uint32_t& out) const {
        if (at + 4 > data_.size()) return false;
        out = static_cast<std::uint32_t>(data_[at]) | (static_cast<std::uint32_t>(data_[at + 1]) << 8) |
              (static_cast<std::uint32_t>(data_[at + 2]) << 16) | (static_cast<std::uint32_t>(data_[at + 3]) << 24);
        return true;
    }
    bool f32(std::size_t at, float& out) const {
        std::uint32_t bits = 0;
        if (!u32(at, bits)) return false;
        out = std::bit_cast<float>(bits);
        return true;
    }
    std::uint8_t byte(std::size_t at) const { return at < data_.size() ? data_[at] : 0; }
    // Zero-terminated string starting at `at`, not past `end`. Returns the
    // position after the terminator, or 0 on failure.
    std::size_t cstring(std::size_t at, std::size_t end, std::string& out) const {
        out.clear();
        for (std::size_t i = at; i < end && i < data_.size(); ++i) {
            if (data_[i] == 0) return i + 1;
            out.push_back(static_cast<char>(data_[i]));
        }
        return 0;
    }

private:
    std::span<const std::uint8_t> data_;
};

constexpr std::size_t kMax3dsMaterialGroups = 1024;  // real objects have a handful

struct ThreeDsObject {
    std::string name;
    std::vector<glm::vec3> vertices;
    std::vector<glm::vec2> uvs;
    std::vector<std::array<std::uint16_t, 3>> faces;
    std::vector<std::uint32_t> smoothing;
    std::vector<std::pair<std::string, std::vector<std::uint16_t>>> materialFaces;
};

class ThreeDsParser {
public:
    explicit ThreeDsParser(std::span<const std::uint8_t> bytes) : r_(bytes) {}

    bool parse(std::string& error) {
        std::uint16_t id = 0;
        std::uint32_t len = 0;
        if (!r_.u16(0, id) || !r_.u32(2, len) || id != c3ds::kMain) {
            error = "3DS: not a 3DS file (missing MAIN3DS chunk)";
            return false;
        }
        // Some exporters write a main chunk length that does not match the file.
        const std::size_t end = std::min<std::size_t>(len, r_.size());
        return chunks(6, end, error, [&](std::uint16_t cid, std::size_t b, std::size_t e) {
            return cid == c3ds::kEditor ? editor(b, e, error) : true;
        });
    }

    std::vector<ThreeDsObject> objects;
    std::unordered_map<std::string, Material> materials;

private:
    template <typename Fn>
    bool chunks(std::size_t begin, std::size_t end, std::string& error, Fn&& fn) {
        std::size_t p = begin;
        while (p + 6 <= end) {
            std::uint16_t id = 0;
            std::uint32_t len = 0;
            r_.u16(p, id);
            r_.u32(p + 2, len);
            if (len < 6 || p + len > end) {
                error = std::format("3DS: chunk 0x{:04X} at offset {} has an invalid length", id, p);
                return false;
            }
            if (!fn(id, p + 6, p + len)) return false;
            p += len;
        }
        return true;
    }

    bool editor(std::size_t begin, std::size_t end, std::string& error) {
        return chunks(begin, end, error, [&](std::uint16_t id, std::size_t b, std::size_t e) {
            if (id == c3ds::kMaterial) return material(b, e, error);
            if (id == c3ds::kObject) return object(b, e, error);
            return true;
        });
    }

    bool material(std::size_t begin, std::size_t end, std::string& error) {
        std::string name;
        Material m = defaultMaterial();
        bool ok = chunks(begin, end, error, [&](std::uint16_t id, std::size_t b, std::size_t e) {
            if (id == c3ds::kMatName) {
                r_.cstring(b, e, name);
            } else if (id == c3ds::kMatDiffuse) {
                glm::vec3 c;
                if (colour(b, e, c, error)) m.albedo = c;
            } else if (id == c3ds::kMatShininess) {
                float s = 0.0f;
                if (percentage(b, e, s, error)) m.roughness = std::clamp(1.0f - 0.9f * s, 0.05f, 1.0f);
            }
            return true;
        });
        if (ok) materials[name] = m;
        return ok;
    }

    // Prefers the linear colour sub-chunk when both are present.
    bool colour(std::size_t begin, std::size_t end, glm::vec3& out, std::string& error) {
        bool found = false, linear = false;
        chunks(begin, end, error, [&](std::uint16_t id, std::size_t b, std::size_t) {
            glm::vec3 c;
            bool isLinear = id == c3ds::kLinColor24 || id == c3ds::kLinColorF;
            if (id == c3ds::kColorF || id == c3ds::kLinColorF) {
                if (!r_.f32(b, c.x) || !r_.f32(b + 4, c.y) || !r_.f32(b + 8, c.z)) return true;
            } else if (id == c3ds::kColor24 || id == c3ds::kLinColor24) {
                c = glm::vec3(r_.byte(b), r_.byte(b + 1), r_.byte(b + 2)) / 255.0f;
            } else {
                return true;
            }
            if (!isLinear) c = {srgbToLinear(c.x), srgbToLinear(c.y), srgbToLinear(c.z)};
            if (!found || (isLinear && !linear)) {
                out = glm::clamp(c, 0.0f, 1.0f);
                found = true;
                linear = isLinear;
            }
            return true;
        });
        return found;
    }

    bool percentage(std::size_t begin, std::size_t end, float& out, std::string& error) {
        bool found = false;
        chunks(begin, end, error, [&](std::uint16_t id, std::size_t b, std::size_t) {
            std::uint16_t i = 0;
            if (id == c3ds::kIntPercent && r_.u16(b, i)) {
                out = static_cast<float>(i) / 100.0f;
                found = true;
            } else if (id == c3ds::kFloatPercent && r_.f32(b, out)) {
                out /= 100.0f;
                found = true;
            }
            return true;
        });
        out = std::clamp(out, 0.0f, 1.0f);
        return found;
    }

    bool object(std::size_t begin, std::size_t end, std::string& error) {
        ThreeDsObject obj;
        const std::size_t after = r_.cstring(begin, end, obj.name);
        if (after == 0) {
            error = "3DS: unterminated object name";
            return false;
        }
        bool hasMesh = false;
        const bool ok = chunks(after, end, error, [&](std::uint16_t id, std::size_t b, std::size_t e) {
            if (id != c3ds::kTriMesh) return true;
            hasMesh = true;
            return trimesh(b, e, obj, error);
        });
        if (!ok) return false;
        if (hasMesh) objects.push_back(std::move(obj));
        return true;
    }

    bool trimesh(std::size_t begin, std::size_t end, ThreeDsObject& obj, std::string& error) {
        return chunks(begin, end, error, [&](std::uint16_t id, std::size_t b, std::size_t e) {
            std::uint16_t count = 0;
            if (id == c3ds::kVertices) {
                if (!r_.u16(b, count) || b + 2 + std::size_t{count} * 12 > e) {
                    error = "3DS: vertex list overruns its chunk";
                    return false;
                }
                obj.vertices.resize(count);
                for (std::size_t i = 0; i < count; ++i) {
                    const std::size_t at = b + 2 + i * 12;
                    r_.f32(at, obj.vertices[i].x);
                    r_.f32(at + 4, obj.vertices[i].y);
                    r_.f32(at + 8, obj.vertices[i].z);
                }
            } else if (id == c3ds::kTexCoords) {
                if (!r_.u16(b, count) || b + 2 + std::size_t{count} * 8 > e) {
                    error = "3DS: texture coordinates overrun their chunk";
                    return false;
                }
                obj.uvs.resize(count);
                for (std::size_t i = 0; i < count; ++i) {
                    r_.f32(b + 2 + i * 8, obj.uvs[i].x);
                    r_.f32(b + 6 + i * 8, obj.uvs[i].y);
                }
            } else if (id == c3ds::kFaces) {
                return faces(b, e, obj, error);
            }
            return true;
        });
    }

    bool faces(std::size_t begin, std::size_t end, ThreeDsObject& obj, std::string& error) {
        std::uint16_t count = 0;
        if (!r_.u16(begin, count) || begin + 2 + std::size_t{count} * 8 > end) {
            error = "3DS: face list overruns its chunk";
            return false;
        }
        obj.faces.resize(count);
        for (std::size_t i = 0; i < count; ++i) {
            const std::size_t at = begin + 2 + i * 8;
            for (std::size_t k = 0; k < 3; ++k) {
                r_.u16(at + 2 * k, obj.faces[i][k]);
                if (obj.faces[i][k] >= obj.vertices.size()) {
                    error = std::format("3DS: object '{}' face {} uses vertex {} of {}", obj.name, i, obj.faces[i][k],
                                        obj.vertices.size());
                    return false;
                }
            }
        }
        // Sub-chunks follow the face list.
        return chunks(begin + 2 + std::size_t{count} * 8, end, error,
                      [&](std::uint16_t id, std::size_t b, std::size_t e) {
                          if (id == c3ds::kFaceMaterial) {
                              std::string name;
                              const std::size_t after = r_.cstring(b, e, name);
                              std::uint16_t n = 0;
                              if (after == 0 || !r_.u16(after, n) || after + 2 + std::size_t{n} * 2 > e) {
                                  error = "3DS: bad face material group";
                                  return false;
                              }
                              if (obj.materialFaces.size() >= kMax3dsMaterialGroups) {
                                  error = "3DS: too many material groups in one object";
                                  return false;
                              }
                              std::vector<std::uint16_t> list(n);
                              for (std::size_t i = 0; i < n; ++i) r_.u16(after + 2 + i * 2, list[i]);
                              obj.materialFaces.emplace_back(std::move(name), std::move(list));
                          } else if (id == c3ds::kSmoothing) {
                              if (b + obj.faces.size() * 4 > e) return true;  // malformed: ignore smoothing
                              obj.smoothing.resize(obj.faces.size());
                              for (std::size_t i = 0; i < obj.faces.size(); ++i) r_.u32(b + i * 4, obj.smoothing[i]);
                          }
                          return true;
                      });
    }

    ByteReader r_;
};

bool load3ds(std::span<const std::uint8_t> bytes, const ModelLoadOptions& opts, ModelData& model, std::string& error) {
    ThreeDsParser parser(bytes);
    if (!parser.parse(error)) return false;
    const glm::mat4 conversion = conversionMatrix(opts);
    ModelBudget budget;
    for (const ThreeDsObject& obj : parser.objects) {
        // Material index per face; -1 = the default material.
        std::vector<int> faceMaterial(obj.faces.size(), -1);
        for (std::size_t m = 0; m < obj.materialFaces.size(); ++m) {
            for (std::uint16_t f : obj.materialFaces[m].second) {
                if (f < faceMaterial.size()) faceMaterial[f] = static_cast<int>(m);
            }
        }
        // Faces per material, collected in one pass (a loop over all faces per material is quadratic).
        std::vector<std::vector<std::size_t>> facesOf(obj.materialFaces.size() + 1);
        for (std::size_t f = 0; f < obj.faces.size(); ++f) facesOf[static_cast<std::size_t>(faceMaterial[f] + 1)].push_back(f);
        for (int m = -1; m < static_cast<int>(obj.materialFaces.size()); ++m) {
            const std::vector<std::size_t>& faceList = facesOf[static_cast<std::size_t>(m + 1)];
            if (faceList.empty()) continue;
            ModelPart part;
            part.mesh.name = obj.name;
            std::vector<std::uint32_t> groups;
            std::vector<std::uint32_t> vertexOf(obj.vertices.size(), UINT32_MAX);
            for (std::size_t f : faceList) {
                for (std::uint16_t vi : obj.faces[f]) {
                    if (vertexOf[vi] == UINT32_MAX) {
                        vertexOf[vi] = static_cast<std::uint32_t>(part.mesh.vertices.size());
                        Vertex v;
                        v.position = obj.vertices[vi];
                        if (vi < obj.uvs.size()) v.uv = obj.uvs[vi];
                        part.mesh.vertices.push_back(v);
                    }
                    part.mesh.indices.push_back(vertexOf[vi]);
                }
                if (!obj.smoothing.empty()) groups.push_back(obj.smoothing[f]);
            }
            if (part.mesh.indices.empty()) continue;
            if (!budget.add(part.mesh, error)) return false;
            if (obj.smoothing.empty())
                generateNormals(part.mesh, kObjCreaseCos);
            else
                generateNormals(part.mesh, 0.0f, &groups);
            transformInPlace(part.mesh, conversion);
            if (m >= 0) {
                auto it = parser.materials.find(obj.materialFaces[static_cast<std::size_t>(m)].first);
                part.material = it != parser.materials.end() ? it->second : defaultMaterial();
            } else {
                part.material = defaultMaterial();
            }
            model.parts.push_back(std::move(part));
        }
    }
    return true;
}

// =============================================================================
// Dispatch
// =============================================================================

enum class Format { Gltf, Obj, ThreeDs, Unknown };

Format detectFormat(std::span<const std::uint8_t> bytes, std::string_view hint) {
    std::string h = lower(hint);
    if (!h.empty() && h.front() == '.') h.erase(0, 1);
    if (h == "gltf" || h == "glb") return Format::Gltf;
    if (h == "obj") return Format::Obj;
    if (h == "3ds") return Format::ThreeDs;
    if (bytes.size() >= 4 && std::memcmp(bytes.data(), "glTF", 4) == 0) return Format::Gltf;
    if (bytes.size() >= 2 && bytes[0] == 0x4D && bytes[1] == 0x4D) return Format::ThreeDs;
    for (std::uint8_t b : bytes) {
        if (isSpace(static_cast<char>(b)) || b == '\n') continue;
        if (b == '{') return Format::Gltf;
        break;
    }
    return Format::Unknown;
}

std::optional<ModelData> loadImpl(std::span<const std::uint8_t> bytes, std::string_view formatHint,
                                  const ModelLoadOptions& opts, std::string* error, const ReadFileFn& readFile) {
    auto fail = [&](std::string msg) -> std::optional<ModelData> {
        if (error) *error = std::move(msg);
        return std::nullopt;
    };
    if (bytes.empty()) return fail("empty model data");
    if (bytes.size() > limits::kMaxFileBytes) return fail("model file is too large");
    ModelData model;
    std::string err;
    bool ok = false;
    switch (detectFormat(bytes, formatHint)) {
        case Format::Gltf: ok = loadGltf(bytes, opts, readFile, model, err); break;
        case Format::Obj: ok = loadObj(bytes, opts, readFile, model, err); break;
        case Format::ThreeDs: ok = load3ds(bytes, opts, model, err); break;
        case Format::Unknown:
            return fail(std::format("unsupported model format '{}' (expected gltf, glb, obj or 3ds)", formatHint));
    }
    if (!ok) return fail(err);
    if (model.parts.empty()) return fail("the model contains no triangle meshes");
    finishModel(model);
    return model;
}

// Backstop: whatever the loaders miss (std::bad_alloc, length_error) must not cross the module boundary.
std::optional<ModelData> loadGuarded(std::span<const std::uint8_t> bytes, std::string_view formatHint,
                                     const ModelLoadOptions& opts, std::string* error, const ReadFileFn& readFile) {
    try {
        return loadImpl(bytes, formatHint, opts, error, readFile);
    } catch (const std::exception& e) {
        if (error) *error = std::string("cannot load model: ") + e.what();
        return std::nullopt;
    }
}

}  // namespace

std::optional<ModelData> loadModel(const std::filesystem::path& path, const ModelLoadOptions& opts,
                                   std::string* error) {
    std::optional<std::vector<std::uint8_t>> bytes = readWholeFile(path);
    if (!bytes) {
        if (error) *error = "cannot open " + path.string();
        return std::nullopt;
    }
    const std::filesystem::path dir = path.parent_path();
    const ReadFileFn readFile = [&dir](std::string_view uri) { return readWholeFile(dir / pathFromUtf8(uri)); };
    std::string ext = path.extension().string();
    std::optional<ModelData> model = loadGuarded(*bytes, ext, opts, error, readFile);
    if (!model && error) *error = path.filename().string() + ": " + *error;
    return model;
}

std::optional<ModelData> loadModelFromMemory(std::span<const std::uint8_t> bytes, std::string_view formatHint,
                                             const ModelLoadOptions& opts, std::string* error,
                                             FileResolver resolveFile, void* resolverUser) {
    ReadFileFn readFile;
    if (resolveFile) {
        readFile = [resolveFile, resolverUser](std::string_view uri) { return resolveFile(resolverUser, uri); };
    }
    return loadGuarded(bytes, formatHint, opts, error, readFile);
}

}  // namespace dmxviz::assets
