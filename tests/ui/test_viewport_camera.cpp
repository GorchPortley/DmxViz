#include "ui/ViewportCamera.h"

#include <doctest/doctest.h>

#include <algorithm>
#include <cmath>

using namespace dmxviz;
using namespace dmxviz::ui;

namespace {

// A stage-sized box (the demo show is about this big).
Aabb stageBox() {
    Aabb b;
    b.min = {-6.0f, 0.0f, -3.3f};
    b.max = {6.0f, 7.0f, 3.0f};
    return b;
}

// Largest |x| or |y| of any corner in normalised device coordinates: 1 = touches the edge of the view.
float largestExtent(const ViewportCamera& camera, const Aabb& box, float aspect) {
    const render::Camera cam = camera.camera(aspect);
    float largest = 0.0f;
    for (int corner = 0; corner < 8; ++corner) {
        const glm::vec3 p((corner & 1) ? box.max.x : box.min.x, (corner & 2) ? box.max.y : box.min.y,
                          (corner & 4) ? box.max.z : box.min.z);
        const glm::vec4 clip = cam.projection * cam.view * glm::vec4(p, 1.0f);
        REQUIRE(clip.w > 0.0f);  // in front of the camera
        largest = std::max({largest, std::abs(clip.x / clip.w), std::abs(clip.y / clip.w)});
    }
    return largest;
}

}  // namespace

TEST_CASE("ViewportCamera: every preset frames the whole box, tightly, for any viewport shape") {
    const Aabb box = stageBox();
    const ViewportCamera::Preset presets[] = {ViewportCamera::Preset::Front, ViewportCamera::Preset::Side,
                                              ViewportCamera::Preset::Top, ViewportCamera::Preset::Perspective};
    for (ViewportCamera::Preset preset : presets) {
        for (float aspect : {0.6f, 1.0f, 16.0f / 9.0f, 2.5f}) {
            ViewportCamera camera;
            camera.setPreset(preset, box, aspect);
            INFO("preset " << static_cast<int>(preset) << ", aspect " << aspect);
            const float extent = largestExtent(camera, box, aspect);
            CHECK(extent <= 1.0f);   // nothing is cut off, in the narrower direction either
            CHECK(extent >= 0.85f);  // and it is not framed from far away
        }
    }
}

TEST_CASE("ViewportCamera: a narrow viewport needs more distance than a wide one") {
    const Aabb box = stageBox();
    ViewportCamera wide, narrow;
    wide.setPreset(ViewportCamera::Preset::Front, box, 2.0f);
    narrow.setPreset(ViewportCamera::Preset::Front, box, 0.5f);
    CHECK(narrow.distance() > wide.distance());
}

TEST_CASE("ViewportCamera: the perspective preset is closer than a bounding sphere fit") {
    const Aabb box = stageBox();
    ViewportCamera camera;
    camera.setPreset(ViewportCamera::Preset::Perspective, box);
    const float radius = glm::length(box.size()) * 0.5f;
    const float sphereDistance = radius / std::sin(camera.fovY() * 0.5f) * 1.1f;  // the old framing
    CHECK(camera.distance() < 0.9f * sphereDistance);
    CHECK(camera.target().x == doctest::Approx(box.center().x));
    CHECK(camera.target().y == doctest::Approx(box.center().y));
}

TEST_CASE("ViewportCamera: a tiny box is not framed from inside it") {
    Aabb tiny;
    tiny.min = glm::vec3(1.0f);
    tiny.max = glm::vec3(1.1f);
    ViewportCamera camera;
    camera.frame(tiny);
    CHECK(camera.distance() > 1.0f);
}
