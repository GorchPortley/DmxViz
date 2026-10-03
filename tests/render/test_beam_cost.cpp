#include "render/BeamMath.h"

#include <doctest/doctest.h>

using namespace dmxviz;
using namespace dmxviz::render::beammath;
using doctest::Approx;

namespace {

BeamHull makeHull(float tanHalfAngle, float apexDistance, float length) {
    BeamHull h;
    h.tanHalfAngle = tanHalfAngle;
    h.apexDistance = apexDistance;
    h.length = length;
    return h;
}

}  // namespace

TEST_CASE("hull coverage: side view is the trapezoid between the end discs") {
    // 10 m long, r0 = 0.01 m, r1 = 1.01 m: silhouette (0.01 + 1.01) * 10 = 10.2 m^2.
    const BeamHull h = makeHull(0.1f, 0.1f, 10.0f);
    // The beam lies along +x from the origin; the camera is 10 m above its middle, 500 px focal length:
    // 50 px per metre, seen exactly from the side.
    const float px = hullCoveragePixels(h, {0, 0, 0}, {1, 0, 0}, {5.0f, 10.0f, 0.0f}, 500.0f, 1.0e9f);
    CHECK(px == Approx(10.2f * 50.0f * 50.0f).epsilon(0.02));
}

TEST_CASE("hull coverage: looking down the beam shows the far disc") {
    const BeamHull h = makeHull(0.1f, 0.1f, 10.0f);
    // Camera on the axis, 20 m behind the lens end looking along -axis: distance to the middle is 25 m.
    const float px = hullCoveragePixels(h, {0, 0, 0}, {0, 0, -1}, {0, 0, 20.0f}, 500.0f, 1.0e9f);
    const float disc = kPi * 1.01f * 1.01f * (500.0f / 25.0f) * (500.0f / 25.0f);
    CHECK(px == Approx(disc).epsilon(0.02));
}

TEST_CASE("hull coverage: grows with the square of 1/distance and is capped by the target size") {
    const BeamHull h = makeHull(0.2f, 0.1f, 8.0f);
    const glm::vec3 lens{0, 5, 0}, dir{0, -1, 0};
    const float far = hullCoveragePixels(h, lens, dir, {20.0f, 1.0f, 0.0f}, 800.0f, 1.0e9f);
    const float near = hullCoveragePixels(h, lens, dir, {10.0f, 1.0f, 0.0f}, 800.0f, 1.0e9f);
    CHECK(near > far * 3.0f);  // twice as close: about four times the pixels
    CHECK(hullCoveragePixels(h, lens, dir, {0.5f, 4.0f, 0.0f}, 800.0f, 2.0e5f) == Approx(2.0e5f));
    // A camera exactly in the middle of the hull must not divide by zero.
    CHECK(std::isfinite(hullCoveragePixels(h, lens, dir, lens + dir * 4.0f, 800.0f, 1.0e6f)));
}
