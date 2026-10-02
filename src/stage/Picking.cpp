#include "stage/Picking.h"

#include <algorithm>
#include <cmath>
#include <limits>
#include <unordered_map>

namespace dmxviz::stage {
namespace {

// Ray (unit dir) against a world box; false if it misses or starts beyond maxT.
bool rayHitsBox(const Ray& ray, const Aabb& box, float maxT) {
    float tNear = 0.0f, tFar = maxT;
    for (int i = 0; i < 3; ++i) {
        const float o = ray.origin[i], d = ray.direction[i];
        if (std::abs(d) < 1e-20f) {
            if (o < box.min[i] || o > box.max[i]) return false;
            continue;
        }
        float t0 = (box.min[i] - o) / d;
        float t1 = (box.max[i] - o) / d;
        if (t0 > t1) std::swap(t0, t1);
        tNear = std::max(tNear, t0);
        tFar = std::min(tFar, t1);
        if (tNear > tFar) return false;
    }
    return true;
}

bool acceptable(const MeshInstance& inst, const PickFilter& filter) {
    return inst.pickId != kInvalidNode && (!filter || filter(inst));
}

// Fills the parts of a hit that are common to the BVH and brute-force paths.
PickHit makeHit(const Ray& ray, const MeshInstance& inst, std::size_t index, const assets::MeshData& mesh,
                std::uint32_t triangle, float t, const glm::vec3& localNormal) {
    PickHit hit;
    hit.node = inst.pickId;
    hit.distance = t;
    hit.point = ray.origin + ray.direction * t;
    hit.instance = index;
    hit.mesh = inst.mesh;
    hit.triangle = triangle;
    const glm::mat3 normalMatrix = glm::transpose(glm::inverse(glm::mat3(inst.world)));
    glm::vec3 n = normalMatrix * localNormal;
    const float len = glm::length(n);
    n = len > 0.0f ? n / len : glm::vec3(0, 1, 0);
    hit.normal = glm::dot(n, ray.direction) > 0.0f ? -n : n;
    for (int k = 0; k < 3; ++k) {
        const glm::vec3 p = mesh.vertices[mesh.indices[3 * triangle + static_cast<std::uint32_t>(k)]].position;
        hit.triangleWorld[static_cast<std::size_t>(k)] = glm::vec3(inst.world * glm::vec4(p, 1.0f));
    }
    return hit;
}

bool localRay(const Ray& ray, const glm::mat4& world, glm::vec3& origin, glm::vec3& dir) {
    if (std::abs(glm::determinant(glm::mat3(world))) < 1e-18f) return false;
    const glm::mat4 inv = glm::inverse(world);
    origin = glm::vec3(inv * glm::vec4(ray.origin, 1.0f));
    dir = glm::mat3(inv) * ray.direction;  // not normalised: t stays in world units
    return true;
}

Ray normalised(const Ray& ray) {
    Ray r = ray;
    const float len = glm::length(r.direction);
    r.direction = len > 0.0f ? r.direction / len : glm::vec3(0, 0, -1);
    return r;
}

}  // namespace

const MeshBvh* Picker::bvh(MeshId mesh, const assets::AssetLibrary& library) {
    const assets::MeshData* data = library.mesh(mesh);
    if (!data) return nullptr;
    const std::uint64_t revision = library.meshRevision(mesh);
    Entry& e = cache_[mesh];
    if (e.revision != revision) {
        e.bvh.build(*data);
        e.revision = revision;
    }
    return &e.bvh;
}

std::optional<PickHit> Picker::pick(const Ray& rayIn, const std::vector<MeshInstance>& instances,
                                    const assets::AssetLibrary& library, const PickFilter& filter) {
    const Ray ray = normalised(rayIn);
    std::optional<PickHit> best;
    float bestT = std::numeric_limits<float>::max();
    for (std::size_t i = 0; i < instances.size(); ++i) {
        const MeshInstance& inst = instances[i];
        if (!acceptable(inst, filter)) continue;
        const assets::MeshData* mesh = library.mesh(inst.mesh);
        if (!mesh || mesh->indices.empty()) continue;
        if (!rayHitsBox(ray, mesh->bounds.transformed(inst.world), bestT)) continue;
        glm::vec3 o, d;
        if (!localRay(ray, inst.world, o, d)) continue;
        const MeshBvh* tree = bvh(inst.mesh, library);
        MeshBvh::Hit h;
        if (tree && tree->intersect(o, d, bestT, h)) {
            bestT = h.t;
            best = makeHit(ray, inst, i, *mesh, h.triangle, h.t, h.normal);
        }
    }
    return best;
}

std::optional<PickHit> Picker::pickBruteForce(const Ray& rayIn, const std::vector<MeshInstance>& instances,
                                              const assets::AssetLibrary& library, const PickFilter& filter) {
    const Ray ray = normalised(rayIn);
    std::optional<PickHit> best;
    float bestT = std::numeric_limits<float>::max();
    for (std::size_t i = 0; i < instances.size(); ++i) {
        const MeshInstance& inst = instances[i];
        if (!acceptable(inst, filter)) continue;
        const assets::MeshData* mesh = library.mesh(inst.mesh);
        if (!mesh) continue;
        glm::vec3 o, d;
        if (!localRay(ray, inst.world, o, d)) continue;
        for (std::size_t t = 0; t + 2 < mesh->indices.size(); t += 3) {
            const glm::vec3 a = mesh->vertices[mesh->indices[t]].position;
            const glm::vec3 b = mesh->vertices[mesh->indices[t + 1]].position;
            const glm::vec3 c = mesh->vertices[mesh->indices[t + 2]].position;
            float hitT = 0.0f;
            if (MeshBvh::intersectTriangle(o, d, a, b, c, hitT) && hitT < bestT) {
                bestT = hitT;
                const glm::vec3 n = glm::cross(b - a, c - a);
                best = makeHit(ray, inst, i, *mesh, static_cast<std::uint32_t>(t / 3), hitT,
                               glm::length(n) > 0.0f ? glm::normalize(n) : glm::vec3(0, 1, 0));
            }
        }
    }
    return best;
}

Ray rayFromNdc(const glm::mat4& inverseViewProjection, const glm::vec2& ndc) {
    glm::vec4 nearP = inverseViewProjection * glm::vec4(ndc, -1.0f, 1.0f);
    glm::vec4 farP = inverseViewProjection * glm::vec4(ndc, 1.0f, 1.0f);
    nearP /= nearP.w;
    farP /= farP.w;
    Ray r;
    r.origin = glm::vec3(nearP);
    r.direction = glm::normalize(glm::vec3(farP - nearP));
    return r;
}

// ---- Frustum ---------------------------------------------------------------

namespace {

glm::vec4 row(const glm::mat4& m, int i) { return {m[0][i], m[1][i], m[2][i], m[3][i]}; }

glm::vec4 normalisePlane(const glm::vec4& p) {
    const float len = glm::length(glm::vec3(p));
    return len > 0.0f ? p / len : p;
}

}  // namespace

Frustum Frustum::fromViewProjection(const glm::mat4& m) { return fromNdcRect(m, {-1.0f, -1.0f}, {1.0f, 1.0f}); }

Frustum Frustum::fromNdcRect(const glm::mat4& m, const glm::vec2& ndcMin, const glm::vec2& ndcMax) {
    // A clip-space point (x, y, z, w) is inside when lo * w <= x <= hi * w, etc.
    const glm::vec2 lo = glm::min(ndcMin, ndcMax);
    const glm::vec2 hi = glm::max(ndcMin, ndcMax);
    const glm::vec4 r0 = row(m, 0), r1 = row(m, 1), r2 = row(m, 2), r3 = row(m, 3);
    Frustum f;
    f.planes[0] = normalisePlane(r0 - lo.x * r3);
    f.planes[1] = normalisePlane(hi.x * r3 - r0);
    f.planes[2] = normalisePlane(r1 - lo.y * r3);
    f.planes[3] = normalisePlane(hi.y * r3 - r1);
    f.planes[4] = normalisePlane(r3 + r2);  // near (OpenGL z >= -w)
    f.planes[5] = normalisePlane(r3 - r2);  // far
    return f;
}

bool Frustum::contains(const glm::vec3& p) const {
    for (const glm::vec4& pl : planes) {
        if (glm::dot(glm::vec3(pl), p) + pl.w < 0.0f) return false;
    }
    return true;
}

bool Frustum::intersects(const Aabb& box) const {
    if (box.empty()) return false;
    for (const glm::vec4& pl : planes) {
        // The box corner furthest along the plane normal.
        const glm::vec3 p{pl.x >= 0.0f ? box.max.x : box.min.x, pl.y >= 0.0f ? box.max.y : box.min.y,
                          pl.z >= 0.0f ? box.max.z : box.min.z};
        if (glm::dot(glm::vec3(pl), p) + pl.w < 0.0f) return false;
    }
    return true;
}

bool Frustum::contains(const Aabb& box) const {
    if (box.empty()) return false;
    for (const glm::vec4& pl : planes) {
        // The box corner least along the plane normal.
        const glm::vec3 p{pl.x >= 0.0f ? box.min.x : box.max.x, pl.y >= 0.0f ? box.min.y : box.max.y,
                          pl.z >= 0.0f ? box.min.z : box.max.z};
        if (glm::dot(glm::vec3(pl), p) + pl.w < 0.0f) return false;
    }
    return true;
}

std::vector<NodeId> marqueeSelect(const Frustum& frustum, const std::vector<MeshInstance>& instances,
                                  const assets::AssetLibrary& library, MarqueeMode mode, const PickFilter& filter) {
    std::vector<NodeId> order;
    std::unordered_map<NodeId, bool> verdict;  // node -> still selected
    for (const MeshInstance& inst : instances) {
        if (!acceptable(inst, filter)) continue;
        const assets::MeshData* mesh = library.mesh(inst.mesh);
        if (!mesh) continue;
        const Aabb box = mesh->bounds.transformed(inst.world);
        auto [it, first] = verdict.emplace(inst.pickId, mode == MarqueeMode::Enclosed);
        if (first) order.push_back(inst.pickId);
        if (mode == MarqueeMode::Touching)
            it->second = it->second || frustum.intersects(box);
        else
            it->second = it->second && frustum.contains(box);
    }
    std::vector<NodeId> out;
    for (NodeId id : order) {
        if (verdict[id]) out.push_back(id);
    }
    return out;
}

}  // namespace dmxviz::stage
