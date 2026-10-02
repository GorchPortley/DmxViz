#include "assets/MeshData.h"

namespace dmxviz::assets {

void MeshData::computeBounds() {
    bounds = Aabb{};
    for (const Vertex& v : vertices) bounds.expand(v.position);
}

void MeshData::computeNormals() {
    for (Vertex& v : vertices) v.normal = glm::vec3(0.0f);
    for (std::size_t i = 0; i + 2 < indices.size(); i += 3) {
        Vertex& a = vertices[indices[i]];
        Vertex& b = vertices[indices[i + 1]];
        Vertex& c = vertices[indices[i + 2]];
        // Area-weighted face normal.
        const glm::vec3 n = glm::cross(b.position - a.position, c.position - a.position);
        a.normal += n;
        b.normal += n;
        c.normal += n;
    }
    for (Vertex& v : vertices) {
        const float len = glm::length(v.normal);
        v.normal = len > 1e-12f ? v.normal / len : glm::vec3(0.0f, 1.0f, 0.0f);
    }
}

void MeshData::append(const MeshData& other, const glm::mat4& transform) {
    const auto base = static_cast<std::uint32_t>(vertices.size());
    const glm::mat3 normalMatrix = glm::transpose(glm::inverse(glm::mat3(transform)));
    // A mirroring transform flips triangle winding; keep faces pointing outward.
    const bool flip = glm::determinant(glm::mat3(transform)) < 0.0f;
    vertices.reserve(vertices.size() + other.vertices.size());
    for (const Vertex& v : other.vertices) {
        Vertex t = v;
        t.position = glm::vec3(transform * glm::vec4(v.position, 1.0f));
        t.normal = glm::normalize(normalMatrix * v.normal);
        vertices.push_back(t);
        bounds.expand(t.position);
    }
    indices.reserve(indices.size() + other.indices.size());
    for (std::size_t i = 0; i + 2 < other.indices.size(); i += 3) {
        indices.push_back(base + other.indices[i]);
        indices.push_back(base + other.indices[flip ? i + 2 : i + 1]);
        indices.push_back(base + other.indices[flip ? i + 1 : i + 2]);
    }
}

}  // namespace dmxviz::assets
