#include "stage/GeometryUtil.h"

#include "assets/Primitives.h"

#include <glm/gtx/quaternion.hpp>

#include <cmath>
#include <format>

namespace dmxviz::stage::geom {

void addTube(assets::MeshData& m, const glm::vec3& a, const glm::vec3& b, float radius, int sides, bool caps) {
    const glm::vec3 d = b - a;
    const float length = glm::length(d);
    if (length < 1e-6f) return;
    const assets::MeshData unit = assets::makeCylinder(radius, length, sides, caps);
    const glm::vec3 dir = d / length;
    // glm::rotation is undefined for opposite vectors; handle straight down explicitly.
    const glm::quat rot = glm::dot(dir, glm::vec3(0, 1, 0)) < -0.9999f
                              ? glm::angleAxis(kPi, glm::vec3(1, 0, 0))
                              : glm::rotation(glm::vec3(0.0f, 1.0f, 0.0f), dir);
    m.append(unit, glm::translate(glm::mat4(1.0f), (a + b) * 0.5f) * glm::mat4_cast(rot));
}

void addBox(assets::MeshData& m, const glm::vec3& centre, const glm::vec3& size) {
    m.append(assets::makeBox(size), glm::translate(glm::mat4(1.0f), centre));
}

void addBox(assets::MeshData& m, const glm::vec3& centre, const glm::vec3& size, const glm::quat& rotation) {
    m.append(assets::makeBox(size), glm::translate(glm::mat4(1.0f), centre) * glm::mat4_cast(rotation));
}

std::string keyNum(float v) {
    // Round to 0.1 mm so that 3.0 and 2.99999 share a mesh; avoid "-0".
    const long long tenths = std::llround(static_cast<double>(v) * 10000.0);
    return std::format("{}", tenths);
}

}  // namespace dmxviz::stage::geom
