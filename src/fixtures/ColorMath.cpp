#include "fixtures/ColorMath.h"

#include <cmath>
#include <cstdio>

namespace dmxviz::fixtures {
namespace {

glm::vec3 xyzToLinear(const glm::vec3& xyz) {
    return {3.2404542f * xyz.x - 1.5371385f * xyz.y - 0.4985314f * xyz.z,
            -0.9692660f * xyz.x + 1.8760108f * xyz.y + 0.0415560f * xyz.z,
            0.0556434f * xyz.x - 0.2040259f * xyz.y + 1.0572252f * xyz.z};
}

// Planckian locus chromaticity (Kim et al. 2002 cubic spline fit, 1667..25000 K).
glm::vec2 planckianXy(float t) {
    t = std::clamp(t, 1667.0f, 25000.0f);
    const double T = t;
    const double T2 = T * T;
    const double T3 = T2 * T;
    double x = 0.0;
    if (T <= 4000.0)
        x = -0.2661239e9 / T3 - 0.2343589e6 / T2 + 0.8776956e3 / T + 0.179910;
    else
        x = -3.0258469e9 / T3 + 2.1070379e6 / T2 + 0.2226347e3 / T + 0.240390;
    const double x2 = x * x;
    const double x3 = x2 * x;
    double y = 0.0;
    if (T <= 2222.0)
        y = -1.1063814 * x3 - 1.34811020 * x2 + 2.18555832 * x - 0.20219683;
    else if (T <= 4000.0)
        y = -0.9549476 * x3 - 1.37418593 * x2 + 2.09137015 * x - 0.16748867;
    else
        y = 3.0817580 * x3 - 5.87338670 * x2 + 3.75112997 * x - 0.37001483;
    return {static_cast<float>(x), static_cast<float>(y)};
}

glm::vec3 rawKelvin(float kelvin) {
    const glm::vec2 xy = planckianXy(kelvin);
    return glm::max(xyYToLinear(xy.x, xy.y, 1.0f), glm::vec3(0.0f));
}

int hexDigit(char c) {
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'a' && c <= 'f') return c - 'a' + 10;
    if (c >= 'A' && c <= 'F') return c - 'A' + 10;
    return -1;
}

}  // namespace

float srgbToLinear(float c) {
    return c <= 0.04045f ? c / 12.92f : std::pow((c + 0.055f) / 1.055f, 2.4f);
}

float linearToSrgb(float c) {
    c = std::max(c, 0.0f);
    return c <= 0.0031308f ? c * 12.92f : 1.055f * std::pow(c, 1.0f / 2.4f) - 0.055f;
}

glm::vec3 srgbToLinear(const glm::vec3& c) { return {srgbToLinear(c.r), srgbToLinear(c.g), srgbToLinear(c.b)}; }
glm::vec3 linearToSrgb(const glm::vec3& c) { return {linearToSrgb(c.r), linearToSrgb(c.g), linearToSrgb(c.b)}; }

std::optional<glm::vec3> parseHexColor(std::string_view hex) {
    if (!hex.empty() && hex.front() == '#') hex.remove_prefix(1);
    if (hex.size() != 6) return std::nullopt;
    glm::vec3 c{0.0f};
    for (int i = 0; i < 3; ++i) {
        const int hi = hexDigit(hex[static_cast<std::size_t>(i * 2)]);
        const int lo = hexDigit(hex[static_cast<std::size_t>(i * 2 + 1)]);
        if (hi < 0 || lo < 0) return std::nullopt;
        c[i] = static_cast<float>(hi * 16 + lo) / 255.0f;
    }
    return srgbToLinear(c);
}

std::string formatHexColor(const glm::vec3& linear) {
    const glm::vec3 s = glm::clamp(linearToSrgb(linear), 0.0f, 1.0f);
    char buf[8];
    std::snprintf(buf, sizeof(buf), "#%02x%02x%02x", static_cast<unsigned>(std::lround(s.r * 255.0f)),
                  static_cast<unsigned>(std::lround(s.g * 255.0f)), static_cast<unsigned>(std::lround(s.b * 255.0f)));
    return buf;
}

glm::vec3 xyYToLinear(float x, float y, float Y) {
    if (y <= 1e-6f) return glm::vec3(0.0f);
    const glm::vec3 xyz{x * Y / y, Y, (1.0f - x - y) * Y / y};
    return glm::max(xyzToLinear(xyz), glm::vec3(0.0f));
}

glm::vec3 kelvinToLinear(float kelvin) {
    // Divide by the 6500 K colour so that the renderer's neutral white is 6500 K
    // (the raw Planckian locus at 6500 K is slightly magenta compared to D65).
    static const glm::vec3 reference = rawKelvin(6500.0f);
    return normalizeMax(rawKelvin(kelvin) / reference);
}

glm::vec3 wavelengthToLinear(float nm) {
    // Multi-lobe Gaussian fit of the CIE 1931 colour matching functions
    // (Wyman, Sloan, Shirley 2013).
    auto g = [](float x, float mu, float s1, float s2) {
        const float t = (x - mu) / (x < mu ? s1 : s2);
        return std::exp(-0.5f * t * t);
    };
    const float X = 1.056f * g(nm, 599.8f, 37.9f, 31.0f) + 0.362f * g(nm, 442.0f, 16.0f, 26.7f) -
                    0.065f * g(nm, 501.1f, 20.4f, 26.2f);
    const float Y = 0.821f * g(nm, 568.8f, 46.9f, 40.5f) + 0.286f * g(nm, 530.9f, 16.3f, 31.1f);
    const float Z = 1.217f * g(nm, 437.0f, 11.8f, 36.0f) + 0.681f * g(nm, 459.0f, 26.0f, 13.8f);
    return normalizeMax(glm::max(xyzToLinear({X, Y, Z}), glm::vec3(0.0f)));
}

glm::vec3 normalizeMax(const glm::vec3& c) {
    const float m = maxComponent(c);
    return m > 1e-9f ? c / m : glm::vec3(0.0f);
}

}  // namespace dmxviz::fixtures
