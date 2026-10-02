#include "ProceduralGobos.h"

#include <algorithm>
#include <cmath>
#include <cstdint>

namespace dmxviz::sandbox {
namespace {

constexpr float kAperture = 0.95f;  // radius of the gobo's open area (1 = image edge)

float hash2(int x, int y) {
    std::uint32_t h = static_cast<std::uint32_t>(x) * 374761393u + static_cast<std::uint32_t>(y) * 668265263u;
    h = (h ^ (h >> 13)) * 1274126177u;
    return static_cast<float>((h ^ (h >> 16)) & 0xffffu) / 65535.0f;
}

float valueNoise(float x, float y) {
    const float fx = std::floor(x), fy = std::floor(y);
    const int ix = static_cast<int>(fx), iy = static_cast<int>(fy);
    float tx = x - fx, ty = y - fy;
    tx = tx * tx * (3.0f - 2.0f * tx);
    ty = ty * ty * (3.0f - 2.0f * ty);
    const float a = hash2(ix, iy) + (hash2(ix + 1, iy) - hash2(ix, iy)) * tx;
    const float b = hash2(ix, iy + 1) + (hash2(ix + 1, iy + 1) - hash2(ix, iy + 1)) * tx;
    return a + (b - a) * ty;
}

float fbm(float x, float y) {
    return 0.55f * valueNoise(x, y) + 0.3f * valueNoise(2.1f * x + 5.2f, 2.1f * y + 1.3f) +
           0.15f * valueNoise(4.3f * x + 9.1f, 4.3f * y + 3.7f);
}

// Transmission of a point (x, y in -1..1, y up) for each pattern.
glm::vec3 transmission(GoboPattern pattern, float x, float y) {
    const float r = std::sqrt(x * x + y * y);
    if (r > kAperture) return glm::vec3(0.0f);
    switch (pattern) {
        case GoboPattern::Dots: {
            auto hole = [&](float cx, float cy, float radius) {
                return (x - cx) * (x - cx) + (y - cy) * (y - cy) < radius * radius;
            };
            if (hole(0.0f, 0.0f, 0.2f)) return glm::vec3(1.0f);
            for (int i = 0; i < 6; ++i) {
                const float a = static_cast<float>(i) * 1.0471976f + 0.3f;
                if (hole(0.58f * std::cos(a), 0.58f * std::sin(a), 0.21f)) return glm::vec3(1.0f);
            }
            return glm::vec3(0.0f);
        }
        case GoboPattern::Breakup: {
            const float n = fbm(x * 3.2f + 11.0f, y * 3.2f + 7.0f);
            return glm::vec3(n > 0.52f ? 1.0f : 0.0f);
        }
        case GoboPattern::Star: {
            const float a = std::atan2(y, x) + 1.5707963f;  // one point straight up
            const float sector = std::fmod(std::abs(a) * 5.0f / 6.2831853f, 1.0f);
            const float tri = std::abs(sector - 0.5f) * 2.0f;  // 1 at the points, 0 between
            const float radius = 0.38f + 0.52f * tri * tri;
            return glm::vec3(r < radius ? 1.0f : 0.0f);
        }
        case GoboPattern::Lines: {
            const float f = std::abs(std::fmod(x * 3.5f + 10.0f, 1.0f) - 0.5f);
            return glm::vec3(f < 0.2f ? 1.0f : 0.0f);
        }
        case GoboPattern::Ring: {
            const bool open = r < 0.16f || (r > 0.36f && r < 0.5f) || (r > 0.72f && r < 0.86f);
            return glm::vec3(open ? 1.0f : 0.0f);
        }
        case GoboPattern::Glass: {
            // Three coloured segments with soft-edged breakup: a dichroic glass gobo.
            const float a = std::atan2(y, x);
            const int segment = static_cast<int>(std::floor((a + 3.1415927f) / 2.0943951f)) % 3;
            const glm::vec3 colors[3] = {{1.0f, 0.15f, 0.1f}, {0.1f, 0.35f, 1.0f}, {0.2f, 1.0f, 0.3f}};
            const float n = fbm(x * 2.5f + 3.0f, y * 2.5f + 1.0f);
            return n > 0.45f ? colors[segment] : glm::vec3(0.0f);
        }
    }
    return glm::vec3(1.0f);
}

}  // namespace

assets::ImageData makeGobo(GoboPattern pattern, int size) {
    assets::ImageData img;
    img.width = size;
    img.height = size;
    img.channels = pattern == GoboPattern::Glass ? 4 : 1;
    img.pixels.resize(static_cast<std::size_t>(size) * size * img.channels);
    constexpr int kSub = 3;  // supersampling for anti-aliased edges
    for (int py = 0; py < size; ++py) {
        for (int px = 0; px < size; ++px) {
            glm::vec3 sum(0.0f);
            for (int sy = 0; sy < kSub; ++sy) {
                for (int sx = 0; sx < kSub; ++sx) {
                    const float x = ((px + (sx + 0.5f) / kSub) / size) * 2.0f - 1.0f;
                    const float y = 1.0f - ((py + (sy + 0.5f) / kSub) / size) * 2.0f;  // row 0 = top
                    sum += transmission(pattern, x, y);
                }
            }
            const glm::vec3 c = sum / float(kSub * kSub);
            const std::size_t i = (static_cast<std::size_t>(py) * size + px) * img.channels;
            if (img.channels == 1) {
                img.pixels[i] = static_cast<std::uint8_t>(c.r * 255.0f + 0.5f);
            } else {
                img.pixels[i + 0] = static_cast<std::uint8_t>(c.r * 255.0f + 0.5f);
                img.pixels[i + 1] = static_cast<std::uint8_t>(c.g * 255.0f + 0.5f);
                img.pixels[i + 2] = static_cast<std::uint8_t>(c.b * 255.0f + 0.5f);
                img.pixels[i + 3] = 255;
            }
        }
    }
    return img;
}

}  // namespace dmxviz::sandbox
