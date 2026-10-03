#pragma once
// ColorWidgets: colour editing for linear-RGB values.
//
// The renderer works in linear RGB, but people think in the sRGB colours a
// colour picker shows. These helpers convert at the widget boundary, so the
// stored value is only touched when the user actually changes the colour.

#include "core/Math.h"

#include "imgui.h"

namespace dmxviz::ui {

float srgbToLinear(float s);
float linearToSrgb(float l);
glm::vec3 srgbToLinear(const glm::vec3& s);
glm::vec3 linearToSrgb(const glm::vec3& l);

// ColorEdit3 that shows `linear` as sRGB. Returns true when the user changed it.
bool colorEditLinear(const char* label, glm::vec3& linear, ImGuiColorEditFlags flags = 0);

}  // namespace dmxviz::ui
