#include "core/Math.h"

#include <doctest/doctest.h>

using namespace dmxviz;

TEST_CASE("aabb transform") {
    Aabb box;
    CHECK(box.empty());
    box.expand({-1, -1, -1});
    box.expand({1, 1, 1});
    const Aabb moved = box.transformed(glm::translate(glm::mat4(1.0f), {10, 0, 0}));
    CHECK(moved.center().x == doctest::Approx(10.0f));
    CHECK(moved.size().y == doctest::Approx(2.0f));
}
