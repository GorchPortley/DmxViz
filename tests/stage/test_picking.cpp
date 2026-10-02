#include "assets/AssetLibrary.h"
#include "assets/Primitives.h"
#include "stage/Picking.h"
#include "stage/TrussBuilder.h"

#include <doctest/doctest.h>

#include <random>

using namespace dmxviz;
using namespace dmxviz::stage;
using assets::AssetLibrary;
using assets::BuiltinMesh;

namespace {

MeshInstance instance(MeshId mesh, const glm::mat4& world, NodeId id) {
    MeshInstance i;
    i.mesh = mesh;
    i.world = world;
    i.pickId = id;
    return i;
}

glm::mat4 at(float x, float y, float z) { return glm::translate(glm::mat4(1.0f), {x, y, z}); }

Ray ray(glm::vec3 o, glm::vec3 d) { return Ray{o, glm::normalize(d)}; }

}  // namespace

TEST_CASE("pick: nearest hit with distance, point and normal") {
    AssetLibrary lib;
    const MeshId cube = lib.builtin(BuiltinMesh::Cube);
    std::vector<MeshInstance> inst = {instance(cube, at(0, 0, -10), 1), instance(cube, at(0, 0, -5), 2)};
    Picker picker;
    auto hit = picker.pick(ray({0, 0, 0}, {0, 0, -1}), inst, lib);
    REQUIRE(hit);
    CHECK(hit->node == 2);
    CHECK(hit->instance == 1);
    CHECK(hit->distance == doctest::Approx(4.5f));
    CHECK(hit->point.z == doctest::Approx(-4.5f));
    CHECK(hit->normal.z == doctest::Approx(1.0f));
    // The hit triangle lies on the front face.
    for (const glm::vec3& p : hit->triangleWorld) CHECK(p.z == doctest::Approx(-4.5f));

    CHECK_FALSE(picker.pick(ray({0, 0, 0}, {0, 0, 1}), inst, lib));  // behind the ray
    CHECK_FALSE(picker.pick(ray({3, 0, 0}, {0, 0, -1}), inst, lib));  // beside the boxes
}

TEST_CASE("pick: rotated and non-uniformly scaled instances") {
    AssetLibrary lib;
    const MeshId cube = lib.builtin(BuiltinMesh::Cube);
    // 2 m long along local X, rotated 90 deg about Y: 2 m deep along world Z.
    const glm::mat4 world = at(0, 0, -5) * glm::rotate(glm::mat4(1.0f), kPi * 0.5f, glm::vec3(0, 1, 0)) *
                            glm::scale(glm::mat4(1.0f), {2.0f, 1.0f, 1.0f});
    std::vector<MeshInstance> inst = {instance(cube, world, 7)};
    Picker picker;
    auto front = picker.pick(ray({0, 0, 0}, {0, 0, -1}), inst, lib);
    REQUIRE(front);
    CHECK(front->distance == doctest::Approx(4.0f));
    CHECK(front->normal.z == doctest::Approx(1.0f));
    auto side = picker.pick(ray({5, 0, -5.5f}, {-1, 0, 0}), inst, lib);
    REQUIRE(side);
    CHECK(side->distance == doctest::Approx(4.5f));
    CHECK(side->normal.x == doctest::Approx(1.0f));
    CHECK(std::abs(side->normal.y) < 1e-5f);
}

TEST_CASE("pick: ignores pickId 0, honours filters and hits faces from inside") {
    AssetLibrary lib;
    const MeshId cube = lib.builtin(BuiltinMesh::Cube);
    std::vector<MeshInstance> inst = {instance(cube, at(0, 0, -5), kInvalidNode), instance(cube, at(0, 0, -8), 3)};
    Picker picker;
    auto hit = picker.pick(ray({0, 0, 0}, {0, 0, -1}), inst, lib);
    REQUIRE(hit);
    CHECK(hit->node == 3);
    CHECK_FALSE(picker.pick(ray({0, 0, 0}, {0, 0, -1}), inst, lib, [](const MeshInstance& i) { return i.pickId != 3; }));

    std::vector<MeshInstance> inside = {instance(cube, glm::mat4(1.0f), 4)};
    auto back = picker.pick(ray({0, 0, 0}, {1, 0, 0}), inside, lib);
    REQUIRE(back);
    CHECK(back->distance == doctest::Approx(0.5f));
    CHECK(back->normal.x == doctest::Approx(-1.0f));  // faces the ray origin
}

TEST_CASE("pick: BVH agrees with brute force on random rays") {
    AssetLibrary lib;
    TrussProfile f34;
    const MeshId truss = truss::straightMesh(lib, f34, 3.0f);
    const MeshId sphere = lib.builtin(BuiltinMesh::Sphere);
    const MeshId cone = lib.builtin(BuiltinMesh::Cone);
    std::mt19937 rng(1234);
    std::uniform_real_distribution<float> u(-1.0f, 1.0f);
    std::vector<MeshInstance> inst;
    NodeId id = 1;
    for (int i = 0; i < 12; ++i) {
        const MeshId mesh = i % 3 == 0 ? truss : (i % 3 == 1 ? sphere : cone);
        const glm::mat4 world = at(u(rng) * 3.0f, u(rng) * 3.0f, u(rng) * 3.0f) *
                                glm::rotate(glm::mat4(1.0f), u(rng) * kPi, glm::normalize(glm::vec3(u(rng), 1, u(rng)))) *
                                glm::scale(glm::mat4(1.0f), glm::vec3(1.0f + 0.5f * u(rng), 1.0f, 1.0f + 0.3f * u(rng)));
        inst.push_back(instance(mesh, world, id++));
    }
    Picker picker;
    int hits = 0;
    for (int r = 0; r < 400; ++r) {
        const glm::vec3 origin = glm::vec3(u(rng), u(rng), u(rng)) * 8.0f;
        // Aim near a random instance so most rays hit something.
        const glm::vec3 centre = glm::vec3(inst[static_cast<std::size_t>(r) % inst.size()].world[3]);
        const glm::vec3 target = centre + glm::vec3(u(rng), u(rng), u(rng)) * 0.5f;
        const Ray rr = ray(origin, target - origin);
        auto fast = picker.pick(rr, inst, lib);
        auto slow = Picker::pickBruteForce(rr, inst, lib);
        REQUIRE(fast.has_value() == slow.has_value());
        if (!fast) continue;
        ++hits;
        CHECK(fast->distance == doctest::Approx(slow->distance).epsilon(1e-4));
        if (std::abs(fast->distance - slow->distance) > 1e-4f) continue;
        CHECK(fast->node == slow->node);
    }
    CHECK(hits > 200);
    CHECK(picker.cachedMeshCount() == 3);
}

TEST_CASE("pick: BVH is rebuilt when the mesh is replaced") {
    AssetLibrary lib;
    const MeshId mesh = lib.addMesh(assets::makeBox({1, 1, 1}), "test:box");
    std::vector<MeshInstance> inst = {instance(mesh, glm::mat4(1.0f), 1)};
    Picker picker;
    auto a = picker.pick(ray({0, 0, 5}, {0, 0, -1}), inst, lib);
    REQUIRE(a);
    CHECK(a->distance == doctest::Approx(4.5f));
    lib.addMesh(assets::makeBox({4, 4, 4}), "test:box");  // same key: replaced in place, new revision
    auto b = picker.pick(ray({0, 0, 5}, {0, 0, -1}), inst, lib);
    REQUIRE(b);
    CHECK(b->distance == doctest::Approx(3.0f));
}

TEST_CASE("rayFromNdc and marquee selection") {
    AssetLibrary lib;
    const MeshId cube = lib.builtin(BuiltinMesh::Cube);
    const glm::mat4 view = glm::lookAt(glm::vec3(0, 0, 10), glm::vec3(0), glm::vec3(0, 1, 0));
    const glm::mat4 proj = glm::perspective(degToRad(60.0f), 1.0f, 0.1f, 100.0f);
    const glm::mat4 vp = proj * view;

    const Ray centre = rayFromNdc(glm::inverse(vp), {0.0f, 0.0f});
    CHECK(centre.direction.z == doctest::Approx(-1.0f));
    CHECK(centre.origin.z == doctest::Approx(9.9f));

    std::vector<MeshInstance> inst = {instance(cube, at(0, 0, 0), 1), instance(cube, at(4, 0, 0), 2),
                                      instance(cube, at(0, 0, 20), 3)};  // behind the camera
    // Marquee covering the middle of the screen: only the centred cube.
    const Frustum middle = Frustum::fromNdcRect(vp, {-0.3f, -0.3f}, {0.3f, 0.3f});
    CHECK(marqueeSelect(middle, inst, lib) == std::vector<NodeId>{1});
    // Whole screen: both visible cubes, not the one behind the camera.
    const Frustum all = Frustum::fromViewProjection(vp);
    CHECK(marqueeSelect(all, inst, lib) == std::vector<NodeId>{1, 2});

    // A marquee that cuts cube 2 in half selects it when touching, not when enclosing.
    const glm::vec4 c2 = vp * glm::vec4(4, 0, 0, 1);
    const float x2 = c2.x / c2.w;
    const Frustum half = Frustum::fromNdcRect(vp, {-0.9f, -0.9f}, {x2, 0.9f});
    CHECK(marqueeSelect(half, inst, lib, MarqueeMode::Touching) == std::vector<NodeId>{1, 2});
    CHECK(marqueeSelect(half, inst, lib, MarqueeMode::Enclosed) == std::vector<NodeId>{1});
}
