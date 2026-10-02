#include "stage/Transform.h"

#include <cmath>

namespace dmxviz::stage {

glm::mat4 Transform::matrix() const {
    glm::mat4 m = glm::mat4_cast(rotation);
    m[0] *= scale.x;
    m[1] *= scale.y;
    m[2] *= scale.z;
    m[3] = glm::vec4(position, 1.0f);
    return m;
}

Transform Transform::fromMatrix(const glm::mat4& m) {
    Transform t;
    t.position = glm::vec3(m[3]);
    glm::vec3 axes[3] = {glm::vec3(m[0]), glm::vec3(m[1]), glm::vec3(m[2])};
    for (int i = 0; i < 3; ++i) t.scale[i] = glm::length(axes[i]);
    // A mirroring matrix has a negative determinant; put the sign on X.
    if (glm::determinant(glm::mat3(m)) < 0.0f) t.scale.x = -t.scale.x;
    glm::mat3 r(1.0f);
    for (int i = 0; i < 3; ++i) {
        if (std::abs(t.scale[i]) > 1e-12f) r[i] = axes[i] / t.scale[i];
    }
    // Orthonormalise (Gram-Schmidt) so quat_cast gets a clean rotation even
    // when the matrix carried shear.
    r[0] = glm::normalize(r[0]);
    r[1] = glm::normalize(r[1] - glm::dot(r[1], r[0]) * r[0]);
    r[2] = glm::cross(r[0], r[1]);
    if (!std::isfinite(r[0].x) || !std::isfinite(r[1].x)) r = glm::mat3(1.0f);
    t.rotation = glm::normalize(glm::quat_cast(r));
    return t;
}

glm::quat Transform::quatFromEulerDegrees(const glm::vec3& d) {
    const glm::quat qx = glm::angleAxis(degToRad(d.x), glm::vec3(1, 0, 0));
    const glm::quat qy = glm::angleAxis(degToRad(d.y), glm::vec3(0, 1, 0));
    const glm::quat qz = glm::angleAxis(degToRad(d.z), glm::vec3(0, 0, 1));
    return glm::normalize(qz * qy * qx);
}

glm::vec3 Transform::eulerDegreesFromQuat(const glm::quat& q) {
    // R = Rz(c) * Ry(b) * Rx(a). glm matrices are column-major: m[col][row].
    const glm::mat3 m = glm::mat3_cast(glm::normalize(q));
    const float r20 = m[0][2];
    float a = 0.0f, b = 0.0f, c = 0.0f;
    if (std::abs(r20) < 0.99999f) {
        b = std::asin(-r20);
        a = std::atan2(m[1][2], m[2][2]);
        c = std::atan2(m[0][1], m[0][0]);
    } else {
        // Gimbal lock (Y = +-90 deg): only a + c or a - c is defined; put it all on X.
        b = r20 < 0.0f ? kPi * 0.5f : -kPi * 0.5f;
        c = 0.0f;
        a = std::atan2(-m[2][1], m[1][1]);
    }
    return {radToDeg(a), radToDeg(b), radToDeg(c)};
}

glm::vec3 Transform::eulerDegrees() const { return eulerDegreesFromQuat(rotation); }

void Transform::setEulerDegrees(const glm::vec3& degrees) { rotation = quatFromEulerDegrees(degrees); }

Transform Transform::lookAt(const glm::vec3& eye, const glm::vec3& target, const glm::vec3& up) {
    Transform t;
    t.position = eye;
    glm::vec3 forward = target - eye;
    if (glm::length(forward) < 1e-6f) return t;
    forward = glm::normalize(forward);
    glm::vec3 right = glm::cross(forward, up);
    if (glm::length(right) < 1e-6f) right = glm::cross(forward, glm::vec3(0, 0, 1));
    right = glm::normalize(right);
    const glm::vec3 realUp = glm::cross(right, forward);
    glm::mat3 r;
    r[0] = right;
    r[1] = realUp;
    r[2] = -forward;
    t.rotation = glm::normalize(glm::quat_cast(r));
    return t;
}

}  // namespace dmxviz::stage
