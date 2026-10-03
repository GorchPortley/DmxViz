#include "ui/ViewportCamera.h"

#include "imgui.h"

#include <cmath>

namespace dmxviz::ui {
namespace {

constexpr float kMaxPitch = degToRad(89.0f);
constexpr float kMinDistance = 0.2f;
constexpr float kMaxDistance = 500.0f;

glm::vec3 directionFromAngles(float yaw, float pitch) {
    // yaw 0 looks toward -Z; positive yaw turns toward -X (counter-clockwise seen from above).
    return {-std::sin(yaw) * std::cos(pitch), std::sin(pitch), -std::cos(yaw) * std::cos(pitch)};
}

}  // namespace

glm::vec3 ViewportCamera::forward() const { return directionFromAngles(yaw_, pitch_); }

glm::vec3 ViewportCamera::position() const { return target_ - forward() * distance_; }

void ViewportCamera::orbit(float deltaYaw, float deltaPitch) {
    yaw_ += deltaYaw;
    pitch_ = std::clamp(pitch_ + deltaPitch, -kMaxPitch, kMaxPitch);
}

void ViewportCamera::pan(float dxPixels, float dyPixels, float viewportHeightPixels) {
    if (viewportHeightPixels <= 0.0f) return;
    // World size of one pixel at the target's depth.
    const float metresPerPixel = 2.0f * distance_ * std::tan(fovY_ * 0.5f) / viewportHeightPixels;
    const glm::vec3 f = forward();
    const glm::vec3 right = glm::normalize(glm::cross(f, glm::vec3(0, 1, 0)));
    const glm::vec3 up = glm::cross(right, f);
    target_ += (-dxPixels * right + dyPixels * up) * metresPerPixel;
}

void ViewportCamera::dolly(float steps) {
    distance_ = std::clamp(distance_ * std::pow(0.88f, steps), kMinDistance, kMaxDistance);
}

void ViewportCamera::look(float deltaYaw, float deltaPitch) {
    // Rotate around the eye instead of the target: keep the position fixed.
    const glm::vec3 eye = position();
    orbit(deltaYaw, deltaPitch);
    target_ = eye + forward() * distance_;
}

void ViewportCamera::fly(const glm::vec3& localDirection, float metres) {
    const glm::vec3 f = forward();
    const glm::vec3 right = glm::normalize(glm::cross(f, glm::vec3(0, 1, 0)));
    const glm::vec3 delta = (right * localDirection.x + glm::vec3(0, 1, 0) * localDirection.y + f * localDirection.z);
    if (glm::dot(delta, delta) < 1e-12f) return;
    target_ += glm::normalize(delta) * metres;
}

void ViewportCamera::frame(const Aabb& bounds) {
    if (bounds.empty()) return;
    target_ = bounds.center();
    const float radius = std::max(0.5f, glm::length(bounds.size()) * 0.5f);
    distance_ = std::clamp(radius / std::sin(fovY_ * 0.5f) * 1.1f, kMinDistance, kMaxDistance);
}

void ViewportCamera::lookFromTo(const glm::vec3& eye, const glm::vec3& target) {
    const glm::vec3 toTarget = target - eye;
    const float length = glm::length(toTarget);
    if (length < 1e-4f) return;
    const glm::vec3 f = toTarget / length;
    target_ = target;
    distance_ = std::clamp(length, kMinDistance, kMaxDistance);
    pitch_ = std::clamp(std::asin(std::clamp(f.y, -1.0f, 1.0f)), -kMaxPitch, kMaxPitch);
    // Inverse of directionFromAngles(); keep the old heading when looking straight up or down.
    if (std::abs(f.x) + std::abs(f.z) > 1e-5f) yaw_ = std::atan2(-f.x, -f.z);
}

void ViewportCamera::setPreset(Preset preset, const Aabb& stageBounds) {
    switch (preset) {
        case Preset::Front: yaw_ = 0.0f; pitch_ = 0.0f; break;
        case Preset::Side: yaw_ = degToRad(90.0f); pitch_ = 0.0f; break;
        case Preset::Top: yaw_ = 0.0f; pitch_ = -kMaxPitch; break;
        case Preset::Audience: yaw_ = 0.0f; pitch_ = degToRad(-8.0f); break;
        case Preset::Perspective: yaw_ = degToRad(-30.0f); pitch_ = degToRad(-20.0f); break;
    }
    frame(stageBounds);
    if (preset == Preset::Audience) {
        // Eye height of a standing audience member, a few rows back.
        target_.y = std::max(1.7f, target_.y);
    }
}

render::Camera ViewportCamera::camera(float aspect) const {
    render::Camera c;
    c.position = position();
    c.fovY = fovY_;
    c.nearPlane = nearPlane_;
    c.farPlane = farPlane_;
    c.view = glm::lookAt(c.position, target_, glm::vec3(0, 1, 0));
    c.projection = glm::perspective(fovY_, std::max(aspect, 1e-3f), nearPlane_, farPlane_);
    return c;
}

Ray ViewportCamera::rayThrough(const glm::vec2& ndc, float aspect) const {
    const render::Camera c = camera(aspect);
    const glm::mat4 inv = glm::inverse(c.projection * c.view);
    glm::vec4 nearPt = inv * glm::vec4(ndc, -1.0f, 1.0f);
    glm::vec4 farPt = inv * glm::vec4(ndc, 1.0f, 1.0f);
    nearPt /= nearPt.w;
    farPt /= farPt.w;
    Ray r;
    r.origin = glm::vec3(nearPt);
    r.direction = glm::normalize(glm::vec3(farPt - nearPt));
    return r;
}

bool handleViewportCameraInput(ViewportCamera& camera, bool viewportHovered, float viewportHeightPixels,
                               float deltaTime) {
    ImGuiIO& io = ImGui::GetIO();
    // A drag keeps control even when the cursor leaves the viewport.
    static bool dragging = false;
    const bool anyButton = ImGui::IsMouseDown(ImGuiMouseButton_Middle) || ImGui::IsMouseDown(ImGuiMouseButton_Right) ||
                           (io.KeyAlt && ImGui::IsMouseDown(ImGuiMouseButton_Left));
    if (!anyButton) dragging = false;
    if (!viewportHovered && !dragging) return false;

    bool used = false;
    const ImVec2 d = io.MouseDelta;
    constexpr float kOrbitSpeed = 0.006f;  // radians per pixel

    const bool mmb = ImGui::IsMouseDown(ImGuiMouseButton_Middle);
    const bool lmb = ImGui::IsMouseDown(ImGuiMouseButton_Left);
    const bool rmb = ImGui::IsMouseDown(ImGuiMouseButton_Right);

    if ((mmb && (io.KeyShift || io.KeyAlt))) {
        camera.pan(d.x, d.y, viewportHeightPixels);
        used = true;
    } else if (mmb || (io.KeyAlt && lmb)) {
        camera.orbit(-d.x * kOrbitSpeed, -d.y * kOrbitSpeed);
        used = true;
    } else if (io.KeyAlt && rmb) {
        camera.dolly(-d.y * 0.05f);
        used = true;
    } else if (rmb) {
        // Fly mode: mouse looks around, WASDQE moves.
        camera.look(-d.x * kOrbitSpeed * 0.7f, -d.y * kOrbitSpeed * 0.7f);
        glm::vec3 move{0.0f};
        if (ImGui::IsKeyDown(ImGuiKey_W)) move.z += 1.0f;
        if (ImGui::IsKeyDown(ImGuiKey_S)) move.z -= 1.0f;
        if (ImGui::IsKeyDown(ImGuiKey_D)) move.x += 1.0f;
        if (ImGui::IsKeyDown(ImGuiKey_A)) move.x -= 1.0f;
        if (ImGui::IsKeyDown(ImGuiKey_E)) move.y += 1.0f;
        if (ImGui::IsKeyDown(ImGuiKey_Q)) move.y -= 1.0f;
        const float speed = (io.KeyShift ? 12.0f : 4.0f) * deltaTime;  // metres per second
        camera.fly(move, speed);
        used = true;
    }
    if (viewportHovered && io.MouseWheel != 0.0f) {
        camera.dolly(io.MouseWheel);
        used = true;
    }
    if (used && anyButton) dragging = true;
    return used;
}

}  // namespace dmxviz::ui
