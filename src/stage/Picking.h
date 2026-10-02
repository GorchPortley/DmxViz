#pragma once
// Viewport picking: ray casts against MeshInstances and marquee (box) selection.
//
// Works on the flat per-frame instance list (stage geometry from
// Scene::collectRenderables plus fixture bodies from the simulation), so
// anything that is drawn can be picked; the hit's node id is the instance's
// pickId. Instances with pickId == kInvalidNode are ignored.

#include "assets/AssetLibrary.h"
#include "core/Math.h"
#include "core/SceneTypes.h"
#include "stage/MeshBvh.h"

#include <array>
#include <cstdint>
#include <functional>
#include <optional>
#include <unordered_map>
#include <vector>

namespace dmxviz::stage {

struct PickHit {
    NodeId node = kInvalidNode;
    float distance = 0.0f;            // along the ray, world units
    glm::vec3 point{0.0f};            // world space
    glm::vec3 normal{0.0f, 1.0f, 0.0f};  // world space, unit, facing the ray origin
    std::size_t instance = 0;         // index in the instance list
    MeshId mesh = kInvalidMesh;
    std::uint32_t triangle = 0;
    std::array<glm::vec3, 3> triangleWorld{};  // corners of the hit triangle (vertex snapping)
};

// Optional extra test, e.g. skip locked nodes: [&](const MeshInstance& i) { return !scene.effectiveLocked(i.pickId); }
using PickFilter = std::function<bool(const MeshInstance&)>;

// Ray caster with a cache of one BVH per mesh. The BVH of a mesh is rebuilt
// when its AssetLibrary revision changes. Keep one Picker per AssetLibrary.
class Picker {
public:
    // Nearest hit along `ray` (direction is normalised internally).
    std::optional<PickHit> pick(const Ray& ray, const std::vector<MeshInstance>& instances,
                                const assets::AssetLibrary& library, const PickFilter& filter = {});

    // Same result without BVHs or culling: tests every triangle (reference for tests).
    static std::optional<PickHit> pickBruteForce(const Ray& ray, const std::vector<MeshInstance>& instances,
                                                 const assets::AssetLibrary& library, const PickFilter& filter = {});

    // The cached BVH of a mesh (built on demand); nullptr for unknown meshes.
    const MeshBvh* bvh(MeshId mesh, const assets::AssetLibrary& library);
    std::size_t cachedMeshCount() const { return cache_.size(); }
    void clear() { cache_.clear(); }

private:
    struct Entry {
        std::uint64_t revision = 0;
        MeshBvh bvh;
    };
    std::unordered_map<MeshId, Entry> cache_;
};

// World-space ray through a point of the viewport. ndc is in [-1, 1] (y up);
// inverseViewProjection uses OpenGL clip conventions (z in [-1, 1]).
Ray rayFromNdc(const glm::mat4& inverseViewProjection, const glm::vec2& ndc);

// Six planes (normal pointing inside: dot(n, p) + w >= 0).
struct Frustum {
    std::array<glm::vec4, 6> planes{};

    static Frustum fromViewProjection(const glm::mat4& viewProjection);
    // The part of the view inside an NDC rectangle (marquee drag).
    static Frustum fromNdcRect(const glm::mat4& viewProjection, const glm::vec2& ndcMin, const glm::vec2& ndcMax);

    bool contains(const glm::vec3& p) const;
    // Conservative: may report boxes near the frustum corners as intersecting.
    bool intersects(const Aabb& box) const;
    bool contains(const Aabb& box) const;
};

enum class MarqueeMode {
    Touching,  // node selected if any of its instances' bounds overlap the marquee
    Enclosed,  // node selected only if all its instances' bounds are inside
};

// Node ids (unique, in instance order) selected by a marquee frustum.
std::vector<NodeId> marqueeSelect(const Frustum& frustum, const std::vector<MeshInstance>& instances,
                                  const assets::AssetLibrary& library, MarqueeMode mode = MarqueeMode::Touching,
                                  const PickFilter& filter = {});

}  // namespace dmxviz::stage
