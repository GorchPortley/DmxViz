#pragma once
// Common math types. DmxViz uses glm everywhere.
//
// World conventions (ADR 0002):
//   * Right-handed, Y up, units are metres and radians.
//   * Stage floor is the plane y = 0. The audience is toward +Z, upstage is -Z.
//   * A fixture's beam leaves its Beam geometry along local -Y; local +Z is the
//     "up" reference used to orient gobos, prisms and framing shutters.

#include <glm/glm.hpp>
#include <glm/gtc/constants.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/quaternion.hpp>
#include <glm/gtc/type_ptr.hpp>

#include <algorithm>
#include <limits>

namespace dmxviz {

constexpr float kPi = glm::pi<float>();

constexpr float degToRad(float deg) { return deg * (kPi / 180.0f); }
constexpr float radToDeg(float rad) { return rad * (180.0f / kPi); }

// Axis-aligned bounding box. An empty box has min > max.
struct Aabb {
    glm::vec3 min{std::numeric_limits<float>::max()};
    glm::vec3 max{std::numeric_limits<float>::lowest()};

    bool empty() const { return min.x > max.x; }
    void expand(const glm::vec3& p) {
        min = glm::min(min, p);
        max = glm::max(max, p);
    }
    void expand(const Aabb& o) {
        if (o.empty()) return;
        expand(o.min);
        expand(o.max);
    }
    glm::vec3 center() const { return (min + max) * 0.5f; }
    glm::vec3 size() const { return empty() ? glm::vec3(0.0f) : max - min; }

    // Bounds of this box after an affine transform.
    Aabb transformed(const glm::mat4& m) const {
        Aabb r;
        if (empty()) return r;
        for (int i = 0; i < 8; ++i) {
            glm::vec3 c{(i & 1) ? max.x : min.x, (i & 2) ? max.y : min.y, (i & 4) ? max.z : min.z};
            r.expand(glm::vec3(m * glm::vec4(c, 1.0f)));
        }
        return r;
    }
};

struct Ray {
    glm::vec3 origin{0.0f};
    glm::vec3 direction{0.0f, 0.0f, -1.0f};  // unit length
};

}  // namespace dmxviz
