#include "stage/MeshBvh.h"

#include <algorithm>
#include <cmath>

namespace dmxviz::stage {
namespace {

constexpr std::uint32_t kLeafSize = 4;
constexpr float kBaryEpsilon = 1e-6f;  // closes hairline cracks between neighbouring triangles

// Slab test. Returns the entry distance in tNear.
bool rayBox(const glm::vec3& origin, const glm::vec3& invDir, const Aabb& box, float tMax, float& tNear) {
    const glm::vec3 t0 = (box.min - origin) * invDir;
    const glm::vec3 t1 = (box.max - origin) * invDir;
    const glm::vec3 lo = glm::min(t0, t1);
    const glm::vec3 hi = glm::max(t0, t1);
    tNear = std::max(std::max(lo.x, lo.y), std::max(lo.z, 0.0f));
    const float tFar = std::min(std::min(hi.x, hi.y), std::min(hi.z, tMax));
    return tNear <= tFar;
}

// 1/d with zero components replaced by a huge finite value (avoids 0 * inf = NaN).
glm::vec3 safeInverse(const glm::vec3& d) {
    glm::vec3 r;
    for (int i = 0; i < 3; ++i) {
        const float c = std::abs(d[i]) < 1e-30f ? (d[i] < 0.0f ? -1e-30f : 1e-30f) : d[i];
        r[i] = 1.0f / c;
    }
    return r;
}

const Aabb kEmptyBox{};

}  // namespace

const Aabb& MeshBvh::bounds() const { return nodes_.empty() ? kEmptyBox : nodes_.front().box; }

void MeshBvh::build(const assets::MeshData& mesh) {
    nodes_.clear();
    triangles_.clear();
    const std::size_t count = mesh.indices.size() / 3;
    triangles_.reserve(count);
    for (std::size_t t = 0; t < count; ++t) {
        const std::uint32_t i0 = mesh.indices[3 * t], i1 = mesh.indices[3 * t + 1], i2 = mesh.indices[3 * t + 2];
        if (i0 >= mesh.vertices.size() || i1 >= mesh.vertices.size() || i2 >= mesh.vertices.size()) continue;
        triangles_.push_back({mesh.vertices[i0].position, mesh.vertices[i1].position, mesh.vertices[i2].position,
                              static_cast<std::uint32_t>(t)});
    }
    if (triangles_.empty()) return;
    nodes_.reserve(2 * triangles_.size() / kLeafSize + 1);
    nodes_.emplace_back();
    buildNode(0, 0, static_cast<std::uint32_t>(triangles_.size()));
}

void MeshBvh::buildNode(std::uint32_t nodeIndex, std::uint32_t begin, std::uint32_t end) {
    auto centroid = [](const Triangle& t) { return (t.a + t.b + t.c) / 3.0f; };
    Aabb box, centreBox;
    for (std::uint32_t i = begin; i < end; ++i) {
        box.expand(triangles_[i].a);
        box.expand(triangles_[i].b);
        box.expand(triangles_[i].c);
        centreBox.expand(centroid(triangles_[i]));
    }
    nodes_[nodeIndex].box = box;
    const glm::vec3 extent = centreBox.size();
    if (end - begin <= kLeafSize || std::max({extent.x, extent.y, extent.z}) <= 0.0f) {
        nodes_[nodeIndex].first = begin;
        nodes_[nodeIndex].count = end - begin;
        return;
    }
    // Median split along the widest axis of the triangle centres.
    const int axis = extent.x >= extent.y && extent.x >= extent.z ? 0 : (extent.y >= extent.z ? 1 : 2);
    const std::uint32_t mid = begin + (end - begin) / 2;
    std::nth_element(triangles_.begin() + begin, triangles_.begin() + mid, triangles_.begin() + end,
                     [&](const Triangle& l, const Triangle& r) { return centroid(l)[axis] < centroid(r)[axis]; });
    // Children are stored next to each other: left at `first`, right at first + 1.
    const auto left = static_cast<std::uint32_t>(nodes_.size());
    nodes_.emplace_back();
    nodes_.emplace_back();
    nodes_[nodeIndex].first = left;
    nodes_[nodeIndex].count = 0;
    buildNode(left, begin, mid);
    buildNode(left + 1, mid, end);
}

bool MeshBvh::intersectTriangle(const glm::vec3& origin, const glm::vec3& dir, const glm::vec3& a,
                                const glm::vec3& b, const glm::vec3& c, float& t) {
    // Moeller-Trumbore, two-sided.
    const glm::vec3 e1 = b - a;
    const glm::vec3 e2 = c - a;
    const glm::vec3 p = glm::cross(dir, e2);
    const float det = glm::dot(e1, p);
    if (det == 0.0f || !std::isfinite(det)) return false;
    const float invDet = 1.0f / det;
    const glm::vec3 s = origin - a;
    const float u = glm::dot(s, p) * invDet;
    if (u < -kBaryEpsilon || u > 1.0f + kBaryEpsilon) return false;
    const glm::vec3 q = glm::cross(s, e1);
    const float v = glm::dot(dir, q) * invDet;
    if (v < -kBaryEpsilon || u + v > 1.0f + kBaryEpsilon) return false;
    t = glm::dot(e2, q) * invDet;
    return t >= 0.0f;
}

bool MeshBvh::intersect(const glm::vec3& origin, const glm::vec3& dir, float tMax, Hit& hit) const {
    if (nodes_.empty()) return false;
    const glm::vec3 invDir = safeInverse(dir);
    float best = tMax;
    bool found = false;
    std::uint32_t stack[64];
    int top = 0;
    float tNear = 0.0f;
    if (!rayBox(origin, invDir, nodes_[0].box, best, tNear)) return false;
    stack[top++] = 0;
    while (top > 0) {
        const Node& node = nodes_[stack[--top]];
        if (!rayBox(origin, invDir, node.box, best, tNear)) continue;
        if (node.count > 0) {
            for (std::uint32_t i = node.first; i < node.first + node.count; ++i) {
                const Triangle& tri = triangles_[i];
                float t = 0.0f;
                if (intersectTriangle(origin, dir, tri.a, tri.b, tri.c, t) && t <= best) {
                    best = t;
                    found = true;
                    hit.t = t;
                    hit.triangle = tri.index;
                    const glm::vec3 n = glm::cross(tri.b - tri.a, tri.c - tri.a);
                    const float len = glm::length(n);
                    hit.normal = len > 0.0f ? n / len : glm::vec3(0, 1, 0);
                }
            }
            continue;
        }
        // Visit the nearer child first so far boxes are culled by `best`.
        float tl = 0.0f, tr = 0.0f;
        const bool hl = rayBox(origin, invDir, nodes_[node.first].box, best, tl);
        const bool hr = rayBox(origin, invDir, nodes_[node.first + 1].box, best, tr);
        if (top + 2 > 64) continue;  // cannot happen for median splits of < 2^60 triangles
        if (hl && hr) {
            if (tl <= tr) {
                stack[top++] = node.first + 1;
                stack[top++] = node.first;
            } else {
                stack[top++] = node.first;
                stack[top++] = node.first + 1;
            }
        } else if (hl) {
            stack[top++] = node.first;
        } else if (hr) {
            stack[top++] = node.first + 1;
        }
    }
    return found;
}

}  // namespace dmxviz::stage
