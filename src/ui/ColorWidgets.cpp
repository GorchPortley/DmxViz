#include "ui/ColorWidgets.h"

#include <cmath>

namespace dmxviz::ui {

float srgbToLinear(float s) {
    s = std::fmax(s, 0.0f);
    return s <= 0.04045f ? s / 12.92f : std::pow((s + 0.055f) / 1.055f, 2.4f);
}

float linearToSrgb(float l) {
    l = std::fmax(l, 0.0f);
    return l <= 0.0031308f ? l * 12.92f : 1.055f * std::pow(l, 1.0f / 2.4f) - 0.055f;
}

glm::vec3 srgbToLinear(const glm::vec3& s) {
    return {srgbToLinear(s.x), srgbToLinear(s.y), srgbToLinear(s.z)};
}

glm::vec3 linearToSrgb(const glm::vec3& l) {
    return {linearToSrgb(l.x), linearToSrgb(l.y), linearToSrgb(l.z)};
}

bool colorEditLinear(const char* label, glm::vec3& linear, ImGuiColorEditFlags flags) {
    glm::vec3 shown = linearToSrgb(linear);
    if (!ImGui::ColorEdit3(label, &shown.x, flags)) return false;
    linear = srgbToLinear(shown);
    return true;
}

}  // namespace dmxviz::ui
