#pragma once
// Interactive camera for a 3D viewport: orbit around a target, pan, dolly,
// and a first-person "fly" mode. The math lives here; ImGui input mapping is
// in handleViewportCameraInput() so the controller itself stays testable.
//
// Mouse/keyboard mapping (common DCC conventions, left button stays free for
// selection and gizmos):
//   middle drag / Alt+left drag ......... orbit
//   Shift+middle drag / Alt+middle drag . pan
//   wheel / Alt+right drag .............. dolly
//   hold right button + W A S D Q E ..... fly (Shift = faster)

#include "core/Math.h"
#include "render/RenderScene.h"

namespace dmxviz::ui {

class ViewportCamera {
public:
    enum class Preset { Front, Side, Top, Audience, Perspective };

    // Orbit by yaw/pitch deltas in radians. Pitch is clamped short of the poles.
    void orbit(float deltaYaw, float deltaPitch);
    // Pan by a mouse delta in pixels, scaled so the target point tracks the cursor.
    void pan(float dxPixels, float dyPixels, float viewportHeightPixels);
    // Positive steps move toward the target (exponential, so it feels the same at any distance).
    void dolly(float steps);
    // Fly: rotate the view direction, then move in camera-local axes (x right, y up, z forward).
    void look(float deltaYaw, float deltaPitch);
    void fly(const glm::vec3& localDirection, float metres);

    // Fit the camera around a box, keeping the current view direction.
    void frame(const Aabb& bounds);
    void setPreset(Preset preset, const Aabb& stageBounds);

    // Builds view/projection for the given aspect ratio (width / height).
    render::Camera camera(float aspect) const;
    // World-space ray through a point given in normalised device coords (-1..1, y up).
    Ray rayThrough(const glm::vec2& ndc, float aspect) const;

    glm::vec3 position() const;
    glm::vec3 forward() const;
    const glm::vec3& target() const { return target_; }
    float distance() const { return distance_; }
    float fovY() const { return fovY_; }
    void setFovY(float radians) { fovY_ = std::clamp(radians, degToRad(10.0f), degToRad(110.0f)); }

private:
    glm::vec3 target_{0.0f, 1.5f, 0.0f};
    float yaw_ = 0.0f;               // radians, 0 = looking toward -Z (from the audience)
    float pitch_ = degToRad(-12.0f); // radians, negative = looking down
    float distance_ = 14.0f;         // metres from target
    float fovY_ = degToRad(50.0f);
    float nearPlane_ = 0.05f;
    float farPlane_ = 1000.0f;
};

// Applies ImGui mouse/keyboard input to the camera while the viewport is
// hovered (or a drag that started there is still active). Returns true when
// the camera consumed input this frame.
bool handleViewportCameraInput(ViewportCamera& camera, bool viewportHovered, float viewportHeightPixels,
                               float deltaTime);

}  // namespace dmxviz::ui
