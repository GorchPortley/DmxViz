#include "assets/AssetLibrary.h"
#include "assets/Primitives.h"

#include <doctest/doctest.h>

using namespace dmxviz;
using namespace dmxviz::assets;

namespace {

// Signed volume via the divergence theorem: positive iff triangles wind
// counter-clockwise seen from outside (outward-facing).
float signedVolume(const MeshData& m) {
    double v = 0.0;
    for (std::size_t i = 0; i + 2 < m.indices.size(); i += 3) {
        const glm::vec3 a = m.vertices[m.indices[i]].position;
        const glm::vec3 b = m.vertices[m.indices[i + 1]].position;
        const glm::vec3 c = m.vertices[m.indices[i + 2]].position;
        v += glm::dot(a, glm::cross(b, c)) / 6.0;
    }
    return static_cast<float>(v);
}

}  // namespace

TEST_CASE("closed primitives are outward-facing with the expected volume") {
    CHECK(signedVolume(makeBox({1, 2, 3})) == doctest::Approx(6.0f).epsilon(1e-4));
    CHECK(signedVolume(makeSphere(1.0f, 64, 32)) == doctest::Approx(4.18879f).epsilon(0.01));
    CHECK(signedVolume(makeCylinder(1.0f, 2.0f, 128)) == doctest::Approx(2.0f * kPi).epsilon(0.01));
    CHECK(signedVolume(makeCone(1.0f, 3.0f, 128)) == doctest::Approx(kPi).epsilon(0.01));
}

TEST_CASE("open primitives face +Y") {
    for (const MeshData& m : {makePlane(2, 2, 3), makeDisc(1.0f)}) {
        const auto& v = m.vertices;
        for (std::size_t i = 0; i + 2 < m.indices.size(); i += 3) {
            const glm::vec3 n = glm::cross(v[m.indices[i + 1]].position - v[m.indices[i]].position,
                                           v[m.indices[i + 2]].position - v[m.indices[i]].position);
            CHECK(n.y > 0.0f);
        }
    }
}

TEST_CASE("tube spans its endpoints") {
    const MeshData t = makeTube({0, 0, 0}, {0, 0, 2}, 0.1f);
    CHECK(t.bounds.min.z == doctest::Approx(0.0f).epsilon(1e-4));
    CHECK(t.bounds.max.z == doctest::Approx(2.0f).epsilon(1e-4));
    CHECK(signedVolume(t) > 0.0f);
}

TEST_CASE("asset library keys replace in place and bump revisions") {
    AssetLibrary lib;
    CHECK(lib.mesh(lib.builtin(BuiltinMesh::Cube)) != nullptr);
    const MeshId a = lib.addMesh(makeBox({1, 1, 1}), "test:box");
    const auto rev = lib.meshRevision(a);
    const MeshId b = lib.addMesh(makeBox({2, 2, 2}), "test:box");
    CHECK(a == b);
    CHECK(lib.meshRevision(a) > rev);
    CHECK(lib.mesh(a)->bounds.size().x == doctest::Approx(2.0f));
    CHECK(lib.findMesh("missing") == kInvalidMesh);
}
