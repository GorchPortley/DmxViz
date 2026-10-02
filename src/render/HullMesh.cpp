#include "render/HullMesh.h"

#include "render/BeamMath.h"

#include <cmath>
#include <cstdint>
#include <vector>

namespace dmxviz::render {

void HullMesh::init() {
    constexpr int n = beammath::kHullSegments;
    const float scale = 1.0f / std::cos(kPi / n);  // circumscribe the circle

    std::vector<float> v;  // xyz triples
    auto add = [&v](float x, float y, float z) { v.insert(v.end(), {x, y, z}); };
    for (int ring = 0; ring < 2; ++ring) {
        for (int i = 0; i < n; ++i) {
            const float a = 2.0f * kPi * static_cast<float>(i) / n;
            add(scale * std::cos(a), scale * std::sin(a), static_cast<float>(ring));
        }
    }
    const auto centre0 = static_cast<std::uint16_t>(2 * n);
    const auto centre1 = static_cast<std::uint16_t>(2 * n + 1);
    add(0.0f, 0.0f, 0.0f);
    add(0.0f, 0.0f, 1.0f);

    std::vector<std::uint16_t> idx;
    for (int i = 0; i < n; ++i) {
        const auto a = static_cast<std::uint16_t>(i);                // lens ring, this angle
        const auto b = static_cast<std::uint16_t>((i + 1) % n);      // lens ring, next angle
        const auto c = static_cast<std::uint16_t>(n + (i + 1) % n);  // far ring, next angle
        const auto d = static_cast<std::uint16_t>(n + i);            // far ring, this angle
        idx.insert(idx.end(), {a, b, c, a, c, d});                   // side, normal points outwards
        idx.insert(idx.end(), {centre0, b, a});                      // lens cap, normal -z
        idx.insert(idx.end(), {centre1, d, c});                      // far cap, normal +z
    }

    sg_buffer_desc vd{};
    vd.usage.vertex_buffer = true;
    vd.data = {v.data(), v.size() * sizeof(float)};
    vd.label = "beam-hull-vertices";
    vertices = sg_make_buffer(&vd);

    sg_buffer_desc id{};
    id.usage.index_buffer = true;
    id.data = {idx.data(), idx.size() * sizeof(std::uint16_t)};
    id.label = "beam-hull-indices";
    indices = sg_make_buffer(&id);
    indexCount = static_cast<int>(idx.size());
}

void HullMesh::shutdown() {
    if (vertices.id) sg_destroy_buffer(vertices);
    if (indices.id) sg_destroy_buffer(indices);
    vertices = {};
    indices = {};
    indexCount = 0;
}

}  // namespace dmxviz::render
