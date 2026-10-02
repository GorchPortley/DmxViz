#pragma once
// Bounding volume hierarchy over the triangles of one mesh, for ray picking.

#include "assets/MeshData.h"

#include <cstdint>
#include <vector>

namespace dmxviz::stage {

// Binary tree of axis-aligned boxes in mesh space; leaves hold a few
// triangles. Built once per mesh (the Picker caches it per MeshId) and then
// answers "first triangle hit by this ray" in roughly O(log n).
class MeshBvh {
public:
    struct Hit {
        float t = 0.0f;               // ray parameter: point = origin + t * dir
        std::uint32_t triangle = 0;   // index of the triangle in the mesh (indices / 3)
        glm::vec3 normal{0, 1, 0};    // geometric normal (unit, mesh space, winding side)
    };

    MeshBvh() = default;
    explicit MeshBvh(const assets::MeshData& mesh) { build(mesh); }
    void build(const assets::MeshData& mesh);

    // Nearest hit with 0 <= t <= tMax. `dir` need not be unit length (picking
    // passes a world ray transformed into mesh space, so t stays in world
    // units). Triangles are hit from both sides.
    bool intersect(const glm::vec3& origin, const glm::vec3& dir, float tMax, Hit& hit) const;

    std::size_t nodeCount() const { return nodes_.size(); }
    std::size_t triangleCount() const { return triangles_.size(); }
    const Aabb& bounds() const;

    // Ray against one triangle (two-sided). Shared with the brute-force reference.
    static bool intersectTriangle(const glm::vec3& origin, const glm::vec3& dir, const glm::vec3& a,
                                  const glm::vec3& b, const glm::vec3& c, float& t);

private:
    struct Node {
        Aabb box;
        std::uint32_t first = 0;  // interior: index of the left child (right = first + 1); leaf: first triangle slot
        std::uint32_t count = 0;  // 0 for interior nodes
    };
    struct Triangle {
        glm::vec3 a, b, c;
        std::uint32_t index;  // original triangle index
    };

    void buildNode(std::uint32_t nodeIndex, std::uint32_t begin, std::uint32_t end);

    std::vector<Node> nodes_;
    std::vector<Triangle> triangles_;
};

}  // namespace dmxviz::stage
