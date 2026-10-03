#pragma once
// Small valid model files (GLB, glTF with embedded buffer, OBJ + MTL, 3DS) as mutation seeds.

#include "Mutate.h"

#include <cstring>
#include <map>
#include <string>

namespace dmxviz::robust::samples {

inline void put16(Bytes& b, std::uint16_t v) {
    b.push_back(static_cast<std::uint8_t>(v & 0xFF));
    b.push_back(static_cast<std::uint8_t>(v >> 8));
}
inline void put32(Bytes& b, std::uint32_t v) {
    for (int i = 0; i < 4; ++i) b.push_back(static_cast<std::uint8_t>((v >> (8 * i)) & 0xFF));
}
inline void putF(Bytes& b, float f) {
    std::uint32_t u;
    std::memcpy(&u, &f, 4);
    put32(b, u);
}
inline void putStr(Bytes& b, const std::string& s) {
    b.insert(b.end(), s.begin(), s.end());
    b.push_back(0);
}

inline std::string base64(const Bytes& data) {
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

// Unit quad: 4 positions (48 bytes) + 6 uint16 indices (12 bytes).
inline Bytes quadBuffer() {
    Bytes b;
    const float corners[4][3] = {{0, 0, 0}, {1, 0, 0}, {1, 1, 0}, {0, 1, 0}};
    for (const auto& c : corners)
        for (float v : c) putF(b, v);
    for (std::uint16_t i : {0, 1, 2, 0, 2, 3}) put16(b, i);
    return b;
}

// glTF JSON for the quad. `uri` empty = GLB binary chunk. `nodes` is the nodes array JSON.
inline std::string quadJson(std::size_t bufferSize, const std::string& uri, const std::string& nodes) {
    std::string buffer = "{\"byteLength\":" + std::to_string(bufferSize);
    if (!uri.empty()) buffer += ",\"uri\":\"" + uri + "\"";
    buffer += "}";
    return R"({"asset":{"version":"2.0"},"scene":0,"scenes":[{"nodes":[0]}],"nodes":)" + nodes +
           R"(,"meshes":[{"name":"quad","primitives":[{"attributes":{"POSITION":0},"indices":1,"material":0}]}],)"
           R"("materials":[{"pbrMetallicRoughness":{"baseColorFactor":[0.5,0.25,0.125,1]},"emissiveFactor":[1,0,0]}],)"
           R"("buffers":[)" +
           buffer +
           R"(],"bufferViews":[{"buffer":0,"byteOffset":0,"byteLength":48},{"buffer":0,"byteOffset":48,"byteLength":12}],)"
           R"("accessors":[{"bufferView":0,"componentType":5126,"count":4,"type":"VEC3","min":[0,0,0],"max":[1,1,0]},)"
           R"({"bufferView":1,"componentType":5123,"count":6,"type":"SCALAR"}]})";
}

inline Bytes makeGlb(std::string json, Bytes bin) {
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

inline Bytes quadGlb() {
    const Bytes bin = quadBuffer();
    return makeGlb(quadJson(bin.size(), "", R"([{"translation":[1,2,3],"children":[1]},{"mesh":0,"scale":[2,2,2]}])"),
                   bin);
}

inline std::string quadGltfText() {
    const Bytes bin = quadBuffer();
    return quadJson(bin.size(), "data:application/octet-stream;base64," + base64(bin), R"([{"mesh":0}])");
}

inline std::string objText() {
    return "# test\nmtllib scene.mtl\n"
           "v 0 0 0\nv 1 0 0\nv 1 1 0\nv 0 1 0\nv 0 0 1\nvt 0 0\nvt 1 0\nvn 0 0 1\n"
           "usemtl red\nf 1 2 3 4\nf 1/1/1 2/2/1 3/1/1\n"
           "usemtl blue\nf -5 -4 -1\n";
}
inline std::string mtlText() {
    return "newmtl red\nKd 1 0 0\nNs 100\nnewmtl blue\nKd 0 0 1\nPr 0.25\nPm 1\n";
}

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

inline Bytes make3ds(std::size_t materialGroups = 1) {
    Chunk3ds verts{0x4110, {}, {}};
    const float vs[4][3] = {{0, 0, 0}, {1, 0, 0}, {1, 1, 0}, {0, 0, 1}};
    put16(verts.payload, 4);
    for (const auto& v : vs)
        for (float c : v) putF(verts.payload, c);
    Chunk3ds faces{0x4120, {}, {}};
    put16(faces.payload, 2);
    for (int i : {0, 1, 2, 0, 1, 0, 3, 0}) put16(faces.payload, static_cast<std::uint16_t>(i));
    for (std::size_t g = 0; g < materialGroups; ++g) {
        Chunk3ds group{0x4130, {}, {}};
        putStr(group.payload, g == 0 ? "Red" : "M" + std::to_string(g));
        put16(group.payload, 1);
        put16(group.payload, static_cast<std::uint16_t>(g % 2));
        faces.children.push_back(group);
    }
    Chunk3ds smooth{0x4150, {}, {}};
    put32(smooth.payload, 1);
    put32(smooth.payload, 2);
    faces.children.push_back(smooth);

    Chunk3ds object{0x4000, {}, {Chunk3ds{0x4100, {}, {verts, faces}}}};
    putStr(object.payload, "Body");
    Chunk3ds color{0x0011, {255, 0, 0}, {}};
    Chunk3ds name{0xA000, {}, {}};
    putStr(name.payload, "Red");
    Chunk3ds material{0xAFFF, {}, {name, Chunk3ds{0xA020, {}, {color}}}};
    return Chunk3ds{0x4D4D, {}, {Chunk3ds{0x3D3D, {}, {material, object}}}}.bytes();
}

}  // namespace dmxviz::robust::samples
