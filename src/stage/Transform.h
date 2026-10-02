#pragma once
// Local transform of a scene node: translation, rotation and scale (TRS).

#include "core/Math.h"

namespace dmxviz::stage {

// Position + rotation + scale relative to the parent node. The matrix is
// T * R * S, so scale is applied first, then rotation, then translation.
//
// Rotation is stored as a quaternion (no gimbal lock, exact round trips). The
// UI usually wants Euler angles in degrees: eulerDegrees()/setEulerDegrees()
// use the order X, then Y, then Z about the fixed parent axes (R = Rz * Ry * Rx),
// which is what most DCC tools show as "Rotation X/Y/Z".
struct Transform {
    glm::vec3 position{0.0f};
    glm::quat rotation{1.0f, 0.0f, 0.0f, 0.0f};  // w, x, y, z
    glm::vec3 scale{1.0f};

    glm::mat4 matrix() const;

    // Splits an affine matrix into TRS. Shear (from non-uniform scale under a
    // rotated parent) cannot be represented and is dropped.
    static Transform fromMatrix(const glm::mat4& m);

    glm::vec3 eulerDegrees() const;
    void setEulerDegrees(const glm::vec3& degrees);
    static glm::quat quatFromEulerDegrees(const glm::vec3& degrees);
    static glm::vec3 eulerDegreesFromQuat(const glm::quat& q);

    // Rotation that makes local -Z look from `eye` toward `target` with local +Y
    // as close to `up` as possible (camera presets).
    static Transform lookAt(const glm::vec3& eye, const glm::vec3& target, const glm::vec3& up = {0, 1, 0});

    bool operator==(const Transform&) const = default;
};

}  // namespace dmxviz::stage
