#include "assets/ModelLoader.h"

#include <doctest/doctest.h>

#include <cstring>
#include <filesystem>
#include <fstream>
#include <map>
#include <string>
#include <vector>

using namespace dmxviz;
using namespace dmxviz::assets;

namespace {

using Bytes = std::vector<std::uint8_t>;

void put16(Bytes& b, std::uint16_t v) {
    b.push_back(static_cast<std::uint8_t>(v & 0xFF));
    b.push_back(static_cast<std::uint8_t>(v >> 8));
}
void put32(Bytes& b, std::uint32_t v) {
    for (int i = 0; i < 4; ++i) b.push_back(static_cast<std::uint8_t>((v >> (8 * i)) & 0xFF));
}
void putF(Bytes& b, float f) {
    std::uint32_t u;
    std::memcpy(&u, &f, 4);
    put32(b, u);
}
void putStr(Bytes& b, const std::string& s) {
    b.insert(b.end(), s.begin(), s.end());
    b.push_back(0);
}
Bytes toBytes(const std::string& s) { return Bytes(s.begin(), s.end()); }

std::string base64(const Bytes& data) {
    static const char* tbl = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
    std::string out;
    std::size_t i = 0;
    for (; i + 2 < data.size(); i += 3) {
        const std::uint32_t n = (data[i] << 16) | (data[i + 1] << 8) | data[i + 2];
        for (int k = 3; k >= 0; --k) out.push_back(tbl[(n >> (6 * k)) & 63]);
    }
    if (i < data.size()) {
        std::uint32_t n = data[i] << 16;
        if (i + 1 < data.size()) n |= data[i + 1] << 8;
        out.push_back(tbl[(n >> 18) & 63]);
        out.push_back(tbl[(n >> 12) & 63]);
        out.push_back(i + 1 < data.size() ? tbl[(n >> 6) & 63] : '=');
        out.push_back('=');
    }
    return out;
}

// Unit quad in the XY plane (normal +Z): 4 float3 positions + 6 indices.
Bytes quadBuffer(int indexBytes) {
    Bytes b;
    for (glm::vec3 p : {glm::vec3(0, 0, 0), glm::vec3(1, 0, 0), glm::vec3(1, 1, 0), glm::vec3(0, 1, 0)}) {
        putF(b, p.x);
        putF(b, p.y);
        putF(b, p.z);
    }
    for (std::uint32_t i : {0u, 1u, 2u, 0u, 2u, 3u}) {
        if (indexBytes == 1) b.push_back(static_cast<std::uint8_t>(i));
        if (indexBytes == 2) put16(b, static_cast<std::uint16_t>(i));
        if (indexBytes == 4) put32(b, i);
    }
    while (b.size() % 4) b.push_back(0);
    return b;
}

int componentType(int indexBytes) { return indexBytes == 1 ? 5121 : indexBytes == 2 ? 5123 : 5125; }

// glTF JSON for the quad; `bufferUri` empty = GLB binary chunk.
std::string quadJson(int indexBytes, std::size_t bufferSize, const std::string& bufferUri, const std::string& nodes) {
    std::string buffer = "{\"byteLength\":" + std::to_string(bufferSize);
    if (!bufferUri.empty()) buffer += ",\"uri\":\"" + bufferUri + "\"";
    buffer += "}";
    return R"({"asset":{"version":"2.0"},"extensionsUsed":["KHR_materials_emissive_strength"],"scene":0,)"
           R"("scenes":[{"nodes":[0]}],"nodes":)" +
           nodes +
           R"(,"meshes":[{"name":"quad","primitives":[{"attributes":{"POSITION":0},"indices":1,"material":0}]}],)"
           R"("materials":[{"pbrMetallicRoughness":{"baseColorFactor":[0.5,0.25,0.125,1],"metallicFactor":0.3,)"
           R"("roughnessFactor":0.7},"emissiveFactor":[1,0,0],)"
           R"("extensions":{"KHR_materials_emissive_strength":{"emissiveStrength":2}}}],)"
           R"("buffers":[)" +
           buffer + R"(],"bufferViews":[{"buffer":0,"byteOffset":0,"byteLength":48},)" +
           R"({"buffer":0,"byteOffset":48,"byteLength":)" + std::to_string(6 * indexBytes) + "}]," +
           R"("accessors":[{"bufferView":0,"componentType":5126,"count":4,"type":"VEC3","min":[0,0,0],"max":[1,1,0]},)" +
           R"({"bufferView":1,"componentType":)" + std::to_string(componentType(indexBytes)) +
           R"(,"count":6,"type":"SCALAR"}]})";
}

const std::string kSimpleNodes = R"([{"mesh":0}])";

Bytes makeGlb(std::string json, Bytes bin) {
    while (json.size() % 4) json.push_back(' ');
    while (bin.size() % 4) bin.push_back(0);
    Bytes b;
    put32(b, 0x46546C67);  // "glTF"
    put32(b, 2);
    put32(b, static_cast<std::uint32_t>(12 + 8 + json.size() + 8 + bin.size()));
    put32(b, static_cast<std::uint32_t>(json.size()));
    put32(b, 0x4E4F534A);  // "JSON"
    b.insert(b.end(), json.begin(), json.end());
    put32(b, static_cast<std::uint32_t>(bin.size()));
    put32(b, 0x004E4942);  // "BIN\0"
    b.insert(b.end(), bin.begin(), bin.end());
    return b;
}

void checkFlatQuad(const ModelPart& part) {
    CHECK(part.mesh.triangleCount() == 2);
    for (const Vertex& v : part.mesh.vertices) {
        CHECK(v.normal.x == doctest::Approx(0.0f));
        CHECK(v.normal.y == doctest::Approx(0.0f));
        CHECK(v.normal.z == doctest::Approx(1.0f));
    }
}

// --- 3DS writer ----------------------------------------------------------
struct Chunk3ds {
    std::uint16_t id;
    Bytes payload;
    std::vector<Chunk3ds> children;

    Bytes bytes() const {
        Bytes body = payload;
        for (const Chunk3ds& c : children) {
            const Bytes cb = c.bytes();
            body.insert(body.end(), cb.begin(), cb.end());
        }
        Bytes b;
        put16(b, id);
        put32(b, static_cast<std::uint32_t>(body.size() + 6));
        b.insert(b.end(), body.begin(), body.end());
        return b;
    }
};

// Two triangles sharing the edge v0-v1 at 90 degrees: face 0 in z=0 (+Z),
// face 1 in y=0 (+Y). With redGroup, face 0 uses material "Red" and face 1
// the default material.
Bytes make3ds(std::uint32_t groupA, std::uint32_t groupB, float scale = 1.0f, bool redGroup = true) {
    Chunk3ds verts{0x4110, {}, {}};
    const glm::vec3 vs[] = {{0, 0, 0}, {1, 0, 0}, {1, 1, 0}, {0, 0, 1}};
    put16(verts.payload, 4);
    for (glm::vec3 v : vs) {
        putF(verts.payload, v.x * scale);
        putF(verts.payload, v.y * scale);
        putF(verts.payload, v.z * scale);
    }
    Chunk3ds faces{0x4120, {}, {}};
    put16(faces.payload, 2);
    for (int i : {0, 1, 2, 0, 1, 0, 3, 0}) put16(faces.payload, static_cast<std::uint16_t>(i));  // a b c flags
    Chunk3ds group{0x4130, {}, {}};
    putStr(group.payload, "Red");
    put16(group.payload, 1);
    put16(group.payload, 0);
    Chunk3ds smooth{0x4150, {}, {}};
    put32(smooth.payload, groupA);
    put32(smooth.payload, groupB);
    faces.children = redGroup ? std::vector<Chunk3ds>{group, smooth} : std::vector<Chunk3ds>{smooth};

    Chunk3ds object{0x4000, {}, {Chunk3ds{0x4100, {}, {verts, faces}}}};
    putStr(object.payload, "Body");

    Chunk3ds color{0x0011, {255, 0, 0}, {}};
    Chunk3ds name{0xA000, {}, {}};
    putStr(name.payload, "Red");
    Chunk3ds material{0xAFFF, {}, {name, Chunk3ds{0xA020, {}, {color}}}};

    return Chunk3ds{0x4D4D, {}, {Chunk3ds{0x3D3D, {}, {material, object}}}}.bytes();
}

struct TempDir {
    std::filesystem::path path;
    TempDir() {
        path = std::filesystem::temp_directory_path() / "dmxviz_model_loader_test";
        std::filesystem::remove_all(path);
        std::filesystem::create_directories(path);
    }
    ~TempDir() {
        std::error_code ec;
        std::filesystem::remove_all(path, ec);
    }
    void write(const std::string& name, const Bytes& data) const {
        std::ofstream out(path / name, std::ios::binary);
        out.write(reinterpret_cast<const char*>(data.data()), static_cast<std::streamsize>(data.size()));
    }
};

std::optional<std::vector<std::uint8_t>> mapResolver(void* user, std::string_view uri) {
    auto* files = static_cast<std::map<std::string, Bytes>*>(user);
    auto it = files->find(std::string(uri));
    if (it == files->end()) return std::nullopt;
    return it->second;
}

}  // namespace

TEST_CASE("GLB: node hierarchy, uint16 indices, flat normals and PBR material") {
    const std::string nodes = R"([{"translation":[1,2,3],"children":[1]},{"mesh":0,"scale":[2,2,2]}])";
    const Bytes bin = quadBuffer(2);
    const Bytes glb = makeGlb(quadJson(2, bin.size(), "", nodes), bin);
    std::string error;
    auto model = loadModelFromMemory(glb, "glb", {}, &error);
    REQUIRE_MESSAGE(model, error);
    REQUIRE(model->parts.size() == 1);
    const ModelPart& part = model->parts[0];
    checkFlatQuad(part);
    CHECK(part.mesh.name == "quad");
    // translate(1,2,3) * scale(2)
    CHECK(model->bounds.min.x == doctest::Approx(1.0f));
    CHECK(model->bounds.min.y == doctest::Approx(2.0f));
    CHECK(model->bounds.max.x == doctest::Approx(3.0f));
    CHECK(model->bounds.max.y == doctest::Approx(4.0f));
    CHECK(model->bounds.max.z == doctest::Approx(3.0f));
    CHECK(part.material.albedo.x == doctest::Approx(0.5f));
    CHECK(part.material.albedo.y == doctest::Approx(0.25f));
    CHECK(part.material.albedo.z == doctest::Approx(0.125f));
    CHECK(part.material.metallic == doctest::Approx(0.3f));
    CHECK(part.material.roughness == doctest::Approx(0.7f));
    CHECK(part.material.emissive.x == doctest::Approx(2.0f));  // factor x KHR_materials_emissive_strength
}

TEST_CASE("glTF: base64 data URI buffer with uint8 and uint32 indices") {
    for (int indexBytes : {1, 4}) {
        const Bytes bin = quadBuffer(indexBytes);
        const std::string uri = "data:application/octet-stream;base64," + base64(bin);
        const std::string json = quadJson(indexBytes, bin.size(), uri, kSimpleNodes);
        std::string error;
        auto model = loadModelFromMemory(toBytes(json), "gltf", {}, &error);
        REQUIRE_MESSAGE(model, error);
        REQUIRE(model->parts.size() == 1);
        checkFlatQuad(model->parts[0]);
    }
}

TEST_CASE("glTF: external buffer through the resolver (percent-decoded URI)") {
    const Bytes bin = quadBuffer(2);
    std::map<std::string, Bytes> files{{"quad data.bin", bin}};
    const std::string json = quadJson(2, bin.size(), "quad%20data.bin", kSimpleNodes);
    std::string error;
    auto model = loadModelFromMemory(toBytes(json), "gltf", {}, &error, &mapResolver, &files);
    REQUIRE_MESSAGE(model, error);
    checkFlatQuad(model->parts[0]);

    // Without a resolver the external file cannot be found.
    auto missing = loadModelFromMemory(toBytes(json), "gltf", {}, &error);
    CHECK_FALSE(missing);
    CHECK(error.find("resolver") != std::string::npos);
}

TEST_CASE("glTF: external buffer next to a file on disk") {
    TempDir dir;
    const Bytes bin = quadBuffer(2);
    dir.write("quad.bin", bin);
    dir.write("quad.gltf", toBytes(quadJson(2, bin.size(), "quad.bin", kSimpleNodes)));
    std::string error;
    auto model = loadModel(dir.path / "quad.gltf", {}, &error);
    REQUIRE_MESSAGE(model, error);
    checkFlatQuad(model->parts[0]);
}

TEST_CASE("glTF: triangle strip and existing normals") {
    Bytes bin;
    // Strip order: (0,0) (1,0) (0,1) (1,1); normals all +Z but not unit length.
    for (glm::vec3 p : {glm::vec3(0, 0, 0), glm::vec3(1, 0, 0), glm::vec3(0, 1, 0), glm::vec3(1, 1, 0)}) {
        putF(bin, p.x);
        putF(bin, p.y);
        putF(bin, p.z);
    }
    for (int i = 0; i < 4; ++i) {
        putF(bin, 0.0f);
        putF(bin, 0.0f);
        putF(bin, 2.0f);
    }
    const std::string json =
        R"({"asset":{"version":"2.0"},"nodes":[{"mesh":0}],"scenes":[{"nodes":[0]}],)"
        R"("meshes":[{"primitives":[{"attributes":{"POSITION":0,"NORMAL":1},"mode":5}]}],)"
        R"("buffers":[{"byteLength":96,"uri":"data:application/octet-stream;base64,)" +
        base64(bin) +
        R"("}],"bufferViews":[{"buffer":0,"byteOffset":0,"byteLength":48},{"buffer":0,"byteOffset":48,"byteLength":48}],)"
        R"("accessors":[{"bufferView":0,"componentType":5126,"count":4,"type":"VEC3","min":[0,0,0],"max":[1,1,0]},)"
        R"({"bufferView":1,"componentType":5126,"count":4,"type":"VEC3"}]})";
    std::string error;
    auto model = loadModelFromMemory(toBytes(json), "gltf", {}, &error);
    REQUIRE_MESSAGE(model, error);
    const MeshData& m = model->parts[0].mesh;
    REQUIRE(m.triangleCount() == 2);
    CHECK(m.vertices.size() == 4);
    for (const Vertex& v : m.vertices) CHECK(v.normal.z == doctest::Approx(1.0f));
    // Both triangles wind counter-clockwise seen from +Z.
    for (std::size_t t = 0; t < 2; ++t) {
        const glm::vec3 a = m.vertices[m.indices[3 * t]].position;
        const glm::vec3 b = m.vertices[m.indices[3 * t + 1]].position;
        const glm::vec3 c = m.vertices[m.indices[3 * t + 2]].position;
        CHECK(glm::cross(b - a, c - a).z > 0.0f);
    }
    // No material: neutral default.
    CHECK(model->parts[0].material.metallic == doctest::Approx(0.0f));
}

TEST_CASE("model load options: Z-up conversion and unit scale") {
    const std::string nodes = R"([{"mesh":0,"translation":[0,0,1000]}])";
    const Bytes bin = quadBuffer(2);
    ModelLoadOptions opts;
    opts.zUp = true;
    opts.unitScale = 0.001f;
    std::string error;
    auto model = loadModelFromMemory(makeGlb(quadJson(2, bin.size(), "", nodes), bin), "glb", opts, &error);
    REQUIRE_MESSAGE(model, error);
    // Source (x, y, z) -> (x, z, -y) * 0.001: the unit quad at z = 1000 lands at y = 1 m.
    CHECK(model->bounds.min.y == doctest::Approx(1.0f));
    CHECK(model->bounds.max.y == doctest::Approx(1.0f));
    CHECK(model->bounds.min.z == doctest::Approx(-0.001f));
    CHECK(model->bounds.max.x == doctest::Approx(0.001f));
    // The quad's +Z normal becomes +Y once the part transform is applied.
    const glm::vec3 n = glm::normalize(glm::mat3(model->parts[0].transform) * model->parts[0].mesh.vertices[0].normal);
    CHECK(n.y == doctest::Approx(1.0f));
}

TEST_CASE("OBJ with MTL: materials split parts, polygons, negative indices, computed normals") {
    const std::string obj =
        "# test\nmtllib scene.mtl\n"
        "v 0 0 0\nv 1 0 0\nv 1 1 0\nv 0 1 0\nv 0 0 1\n"
        "usemtl red\nf 1 2 3 4\n"
        "usemtl blue\nf -5 -4 -1\n";
    const std::string mtl =
        "newmtl red\nKd 1 0 0\nNs 100\n"
        "newmtl blue\nKd 0 0 1\nPr 0.25\nPm 1\n";
    std::map<std::string, Bytes> files{{"scene.mtl", toBytes(mtl)}};
    std::string error;
    auto model = loadModelFromMemory(toBytes(obj), "obj", {}, &error, &mapResolver, &files);
    REQUIRE_MESSAGE(model, error);
    REQUIRE(model->parts.size() == 2);
    const ModelPart& red = model->parts[0];
    const ModelPart& blue = model->parts[1];
    checkFlatQuad(red);
    CHECK(red.material.albedo.x == doctest::Approx(1.0f));
    CHECK(red.material.roughness == doctest::Approx(std::sqrt(2.0f / 102.0f)));
    REQUIRE(blue.mesh.triangleCount() == 1);
    CHECK(blue.mesh.vertices[0].normal.y == doctest::Approx(-1.0f));
    CHECK(blue.material.albedo.z == doctest::Approx(1.0f));
    CHECK(blue.material.roughness == doctest::Approx(0.25f));
    CHECK(blue.material.metallic == doctest::Approx(1.0f));
}

TEST_CASE("OBJ: hard edges of a cube without normals are kept (crease angle)") {
    const std::string obj =
        "v -1 -1 -1\nv 1 -1 -1\nv 1 1 -1\nv -1 1 -1\nv -1 -1 1\nv 1 -1 1\nv 1 1 1\nv -1 1 1\n"
        "f 1 4 3 2\nf 5 6 7 8\nf 1 2 6 5\nf 4 8 7 3\nf 1 5 8 4\nf 2 3 7 6\n";
    std::string error;
    auto model = loadModelFromMemory(toBytes(obj), "obj", {}, &error);
    REQUIRE_MESSAGE(model, error);
    const MeshData& m = model->parts[0].mesh;
    CHECK(m.triangleCount() == 12);
    CHECK(m.vertices.size() == 24);  // each corner split into its 3 face normals
    for (const Vertex& v : m.vertices) CHECK(glm::length(v.normal) == doctest::Approx(1.0f));
}

TEST_CASE("OBJ from disk with mtllib next to it, and with v/vt/vn faces") {
    TempDir dir;
    dir.write("tri.obj", toBytes("mtllib tri.mtl\nv 0 0 0\nv 1 0 0\nv 0 1 0\nvt 0 0\nvt 1 0\nvt 0 1\nvn 0 0 1\n"
                                 "usemtl green\nf 1/1/1 2/2/1 3/3/1\n"));
    dir.write("tri.mtl", toBytes("newmtl green\nKd 0 1 0\n"));
    std::string error;
    auto model = loadModel(dir.path / "tri.obj", {}, &error);
    REQUIRE_MESSAGE(model, error);
    REQUIRE(model->parts.size() == 1);
    CHECK(model->parts[0].material.albedo.y == doctest::Approx(1.0f));
    CHECK(model->parts[0].mesh.vertices[1].uv.x == doctest::Approx(1.0f));
}

TEST_CASE("3DS: objects, material groups and sRGB colour") {
    std::string error;
    auto model = loadModelFromMemory(make3ds(1, 2), "3ds", {}, &error);
    REQUIRE_MESSAGE(model, error);
    REQUIRE(model->parts.size() == 2);  // default-material face + "Red" face
    const ModelPart* red = nullptr;
    for (const ModelPart& p : model->parts) {
        if (p.material.albedo.x > 0.99f && p.material.albedo.y < 0.01f) red = &p;
    }
    REQUIRE(red);
    CHECK(red->mesh.name == "Body");
    CHECK(red->mesh.triangleCount() == 1);
    for (const Vertex& v : red->mesh.vertices) CHECK(v.normal.z == doctest::Approx(1.0f));
    CHECK(model->bounds.max.x == doctest::Approx(1.0f));
    CHECK(model->bounds.max.z == doctest::Approx(1.0f));
}

TEST_CASE("3DS: smoothing groups decide shared or split normals") {
    std::string error;
    auto smooth = loadModelFromMemory(make3ds(1, 1, 1.0f, false), "3ds", {}, &error);
    REQUIRE_MESSAGE(smooth, error);
    REQUIRE(smooth->parts.size() == 1);
    const MeshData& s = smooth->parts[0].mesh;
    CHECK(s.vertices.size() == 4);
    bool foundAveraged = false;
    for (const Vertex& v : s.vertices) {
        if (v.position == glm::vec3(0.0f)) {
            CHECK(v.normal.y == doctest::Approx(0.70710678f));
            CHECK(v.normal.z == doctest::Approx(0.70710678f));
            foundAveraged = true;
        }
    }
    CHECK(foundAveraged);

    auto hard = loadModelFromMemory(make3ds(1, 2, 1.0f, false), "3ds", {}, &error);
    REQUIRE_MESSAGE(hard, error);
    CHECK(hard->parts[0].mesh.vertices.size() == 6);
}

TEST_CASE("3DS: Z-up millimetre source is converted to Y-up metres") {
    std::string error;
    ModelLoadOptions opts;
    opts.zUp = true;
    opts.unitScale = 0.001f;
    auto model = loadModelFromMemory(make3ds(1, 1, 1000.0f, false), "3ds", opts, &error);
    REQUIRE_MESSAGE(model, error);
    // Source (0,0,1000 mm) -> (0, 1, 0) m; (1000,1000,0) -> (1, 0, -1).
    CHECK(model->bounds.max.y == doctest::Approx(1.0f));
    CHECK(model->bounds.min.z == doctest::Approx(-1.0f));
    CHECK(model->bounds.max.x == doctest::Approx(1.0f));
    CHECK(model->bounds.min.y == doctest::Approx(0.0f));
}

TEST_CASE("model loader errors are reported") {
    std::string error;
    CHECK_FALSE(loadModelFromMemory(toBytes("hello world"), "xyz", {}, &error));
    CHECK(error.find("unsupported") != std::string::npos);

    CHECK_FALSE(loadModelFromMemory({}, "obj", {}, &error));

    const Bytes bin = quadBuffer(2);
    Bytes glb = makeGlb(quadJson(2, bin.size(), "", kSimpleNodes), bin);
    glb.resize(glb.size() - 30);
    CHECK_FALSE(loadModelFromMemory(glb, "glb", {}, &error));
    CHECK_FALSE(error.empty());

    CHECK_FALSE(loadModelFromMemory(toBytes("v 0 0 0\nf 1 2 3\n"), "obj", {}, &error));
    CHECK(error.find("line 2") != std::string::npos);

    Bytes bad3ds = make3ds(1, 1);
    bad3ds[8] = 0xFF;  // corrupt the EDIT3DS chunk length
    bad3ds[9] = 0xFF;
    CHECK_FALSE(loadModelFromMemory(bad3ds, "3ds", {}, &error));
    CHECK(error.find("3DS") != std::string::npos);

    CHECK_FALSE(loadModel("/nonexistent/dir/model.glb", {}, &error));
    CHECK(error.find("cannot open") != std::string::npos);
}
