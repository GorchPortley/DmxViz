#pragma once
// Colour conversions used by fixture import and the fixture runtime.
// Everything returned here is *linear* RGB with sRGB/Rec.709 primaries (D65).

#include "core/Math.h"

#include <optional>
#include <string>
#include <string_view>

namespace dmxviz::fixtures {

float srgbToLinear(float c);
float linearToSrgb(float c);
glm::vec3 srgbToLinear(const glm::vec3& c);
glm::vec3 linearToSrgb(const glm::vec3& c);

// "#rrggbb" (sRGB, as written by humans and OFL) -> linear RGB.
std::optional<glm::vec3> parseHexColor(std::string_view hex);
// Linear RGB -> "#rrggbb" (sRGB, clamped).
std::string formatHexColor(const glm::vec3& linear);

// CIE 1931 xyY -> linear sRGB. Y is relative luminance (1 = white). Negative
// (out of gamut) components are clamped to 0.
glm::vec3 xyYToLinear(float x, float y, float Y);

// Colour of a black body / white light source at the given correlated colour
// temperature (1000..25000 K), normalised so the largest component is 1.
// 6500 K maps to exactly (1, 1, 1): the renderer's neutral white.
glm::vec3 kelvinToLinear(float kelvin);

// Approximate colour of monochromatic light (380..780 nm), normalised to max 1.
glm::vec3 wavelengthToLinear(float nanometres);

// Scales a colour so its largest component is 1 (black stays black).
glm::vec3 normalizeMax(const glm::vec3& c);
inline float maxComponent(const glm::vec3& c) { return std::max(c.r, std::max(c.g, c.b)); }

}  // namespace dmxviz::fixtures
