#include "assets/Primitives.h"

#include <glm/gtx/quaternion.hpp>

#include <cmath>

namespace dmxviz::assets {
namespace {

// Adds a quad with corners c +/- u +/- v; counter-clockwise seen from n = u x v.
void addQuad(MeshData& m, const glm::vec3& c, const glm::vec3& u, const glm::vec3& v) {
    const glm::vec3 n = glm::normalize(glm::cross(u, v));
    const auto base = static_cast<std::uint32_t>(m.vertices.size());
    m.vertices.push_back({c - u - v, n, {0, 1}});
    m.vertices.push_back({c + u - v, n, {1, 1}});
    m.vertices.push_back({c + u + v, n, {1, 0}});
    m.vertices.push_back({c - u + v, n, {0, 0}});
    m.indices.insert(m.indices.end(), {base, base + 1, base + 2, base, base + 2, base + 3});
}

glm::vec3 ringPoint(float radius, float angle, float y) {
    return {radius * std::cos(angle), y, radius * std::sin(angle)};
}

// Flat cap at height y. Faces +Y when up is true, -Y otherwise.
void addCap(MeshData& m, float radius, float y, int segments, bool up) {
    const glm::vec3 n{0.0f, up ? 1.0f : -1.0f, 0.0f};
    const auto centre = static_cast<std::uint32_t>(m.vertices.size());
    m.vertices.push_back({{0.0f, y, 0.0f}, n, {0.5f, 0.5f}});
    for (int s = 0; s <= segments; ++s) {
        const float a = 2.0f * kPi * static_cast<float>(s) / static_cast<float>(segments);
        m.vertices.push_back({ringPoint(radius, a, y), n, {0.5f + 0.5f * std::cos(a), 0.5f + 0.5f * std::sin(a)}});
    }
    for (int s = 0; s < segments; ++s) {
        const std::uint32_t p0 = centre + 1 + static_cast<std::uint32_t>(s);
        const std::uint32_t p1 = p0 + 1;
        if (up)
            m.indices.insert(m.indices.end(), {centre, p1, p0});
        else
            m.indices.insert(m.indices.end(), {centre, p0, p1});
    }
}

// Side wall between a top ring and a bottom ring (radii may differ, giving a cone).
void addSide(MeshData& m, float rTop, float yTop, float rBottom, float yBottom, int segments) {
    const float slope = (rBottom - rTop) / (yTop - yBottom);  // radial normal tilt
    const auto base = static_cast<std::uint32_t>(m.vertices.size());
    for (int s = 0; s <= segments; ++s) {
        const float u = static_cast<float>(s) / static_cast<float>(segments);
        const float a = 2.0f * kPi * u;
        const glm::vec3 n = glm::normalize(glm::vec3(std::cos(a), slope, std::sin(a)));
        m.vertices.push_back({ringPoint(rTop, a, yTop), n, {u, 0.0f}});
        m.vertices.push_back({ringPoint(rBottom, a, yBottom), n, {u, 1.0f}});
    }
    for (int s = 0; s < segments; ++s) {
        const std::uint32_t a = base + 2 * static_cast<std::uint32_t>(s);  // top
        const std::uint32_t b = a + 1;                                      // bottom
        const std::uint32_t c = a + 2;                                      // next top
        const std::uint32_t d = a + 3;                                      // next bottom
        m.indices.insert(m.indices.end(), {a, c, b, c, d, b});
    }
}

}  // namespace

MeshData makeBox(const glm::vec3& size) {
    MeshData m;
    m.name = "box";
    const glm::vec3 h = size * 0.5f;
    addQuad(m, {h.x, 0, 0}, {0, 0, -h.z}, {0, h.y, 0});   // +X
    addQuad(m, {-h.x, 0, 0}, {0, 0, h.z}, {0, h.y, 0});   // -X
    addQuad(m, {0, h.y, 0}, {h.x, 0, 0}, {0, 0, -h.z});   // +Y
    addQuad(m, {0, -h.y, 0}, {h.x, 0, 0}, {0, 0, h.z});   // -Y
    addQuad(m, {0, 0, h.z}, {h.x, 0, 0}, {0, h.y, 0});    // +Z
    addQuad(m, {0, 0, -h.z}, {-h.x, 0, 0}, {0, h.y, 0});  // -Z
    m.computeBounds();
    return m;
}

MeshData makePlane(float width, float depth, int subdivisions) {
    MeshData m;
    m.name = "plane";
    const int n = std::max(1, subdivisions);
    for (int j = 0; j <= n; ++j) {
        for (int i = 0; i <= n; ++i) {
            const float u = static_cast<float>(i) / static_cast<float>(n);
            const float v = static_cast<float>(j) / static_cast<float>(n);
            m.vertices.push_back({{(u - 0.5f) * width, 0.0f, (0.5f - v) * depth}, {0, 1, 0}, {u, v}});
        }
    }
    const auto row = static_cast<std::uint32_t>(n + 1);
    for (int j = 0; j < n; ++j) {
        for (int i = 0; i < n; ++i) {
            const std::uint32_t a = static_cast<std::uint32_t>(j) * row + static_cast<std::uint32_t>(i);
            m.indices.insert(m.indices.end(), {a, a + 1, a + 1 + row, a, a + 1 + row, a + row});
        }
    }
    m.computeBounds();
    return m;
}

MeshData makeDisc(float radius, int segments) {
    MeshData m;
    m.name = "disc";
    addCap(m, radius, 0.0f, std::max(3, segments), true);
    m.computeBounds();
    return m;
}

MeshData makeSphere(float radius, int segments, int rings) {
    MeshData m;
    m.name = "sphere";
    segments = std::max(3, segments);
    rings = std::max(2, rings);
    for (int r = 0; r <= rings; ++r) {
        const float v = static_cast<float>(r) / static_cast<float>(rings);
        const float phi = kPi * v;
        for (int s = 0; s <= segments; ++s) {
            const float u = static_cast<float>(s) / static_cast<float>(segments);
            const float theta = 2.0f * kPi * u;
            const glm::vec3 n{std::sin(phi) * std::cos(theta), std::cos(phi), std::sin(phi) * std::sin(theta)};
            m.vertices.push_back({n * radius, n, {u, v}});
        }
    }
    const auto row = static_cast<std::uint32_t>(segments + 1);
    for (int r = 0; r < rings; ++r) {
        for (int s = 0; s < segments; ++s) {
            const std::uint32_t a = static_cast<std::uint32_t>(r) * row + static_cast<std::uint32_t>(s);
            const std::uint32_t b = a + row;
            const std::uint32_t c = a + 1;
            const std::uint32_t d = b + 1;
            m.indices.insert(m.indices.end(), {a, c, b, c, d, b});
        }
    }
    m.computeBounds();
    return m;
}

MeshData makeCylinder(float radius, float height, int segments, bool capped) {
    MeshData m;
    m.name = "cylinder";
    segments = std::max(3, segments);
    const float h = height * 0.5f;
    addSide(m, radius, h, radius, -h, segments);
    if (capped) {
        addCap(m, radius, h, segments, true);
        addCap(m, radius, -h, segments, false);
    }
    m.computeBounds();
    return m;
}

MeshData makeCone(float radius, float height, int segments, bool capped) {
    MeshData m;
    m.name = "cone";
    segments = std::max(3, segments);
    addSide(m, 0.0f, height, radius, 0.0f, segments);
    if (capped) addCap(m, radius, 0.0f, segments, false);
    m.computeBounds();
    return m;
}

MeshData makeTube(const glm::vec3& from, const glm::vec3& to, float radius, int segments) {
    const glm::vec3 d = to - from;
    const float length = glm::length(d);
    MeshData unit = makeCylinder(radius, length, segments, true);
    if (length < 1e-6f) return unit;
    const glm::quat rot = glm::rotation(glm::vec3(0.0f, 1.0f, 0.0f), d / length);
    const glm::mat4 xf = glm::translate(glm::mat4(1.0f), (from + to) * 0.5f) * glm::mat4_cast(rot);
    MeshData m;
    m.name = "tube";
    m.append(unit, xf);
    return m;
}

}  // namespace dmxviz::assets
