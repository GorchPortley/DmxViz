#include "render/NoiseVolume.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <vector>

namespace dmxviz::render {
namespace {

// Integer hash -> [0, 1). Lattice values for value noise.
float hash3(int x, int y, int z) {
    std::uint32_t h = static_cast<std::uint32_t>(x) * 73856093u ^ static_cast<std::uint32_t>(y) * 19349663u ^
                      static_cast<std::uint32_t>(z) * 83492791u;
    h ^= h >> 13;
    h *= 0x5bd1e995u;
    h ^= h >> 15;
    return static_cast<float>(h & 0xffffu) / 65536.0f;
}

float smooth(float t) { return t * t * (3.0f - 2.0f * t); }

// Value noise with `period` lattice cells across the texture, wrapping so the
// texture tiles seamlessly.
float valueNoise(float x, float y, float z, int period) {
    const int x0 = static_cast<int>(std::floor(x)), y0 = static_cast<int>(std::floor(y)),
              z0 = static_cast<int>(std::floor(z));
    const float tx = smooth(x - x0), ty = smooth(y - y0), tz = smooth(z - z0);
    auto v = [&](int dx, int dy, int dz) {
        return hash3((x0 + dx) % period, (y0 + dy) % period, (z0 + dz) % period);
    };
    const float a = v(0, 0, 0) + (v(1, 0, 0) - v(0, 0, 0)) * tx;
    const float b = v(0, 1, 0) + (v(1, 1, 0) - v(0, 1, 0)) * tx;
    const float c = v(0, 0, 1) + (v(1, 0, 1) - v(0, 0, 1)) * tx;
    const float d = v(0, 1, 1) + (v(1, 1, 1) - v(0, 1, 1)) * tx;
    const float e = a + (b - a) * ty;
    const float f = c + (d - c) * ty;
    return e + (f - e) * tz;
}

}  // namespace

void NoiseVolume::init() {
    constexpr int n = kSize;
    std::vector<std::uint8_t> voxels(static_cast<std::size_t>(n) * n * n);
    const int periods[3] = {4, 8, 16};
    const float weights[3] = {0.55f, 0.3f, 0.15f};
    for (int z = 0; z < n; ++z) {
        for (int y = 0; y < n; ++y) {
            for (int x = 0; x < n; ++x) {
                float value = 0.0f;
                for (int o = 0; o < 3; ++o) {
                    const float s = static_cast<float>(periods[o]) / n;
                    value += weights[o] * valueNoise(x * s, y * s, z * s, periods[o]);
                }
                // Stretch the contrast a little (sums of octaves cluster around 0.5).
                value = std::clamp((value - 0.5f) * 1.6f + 0.5f, 0.0f, 1.0f);
                voxels[(static_cast<std::size_t>(z) * n + y) * n + x] = static_cast<std::uint8_t>(value * 255.0f + 0.5f);
            }
        }
    }
    sg_image_desc d{};
    d.type = SG_IMAGETYPE_3D;
    d.width = n;
    d.height = n;
    d.num_slices = n;
    d.pixel_format = SG_PIXELFORMAT_R8;
    d.data.mip_levels[0] = {voxels.data(), voxels.size()};
    d.label = "haze-noise";
    image_ = sg_make_image(&d);
    sg_view_desc vd{};
    vd.texture.image = image_;
    vd.label = "haze-noise";
    view_ = sg_make_view(&vd);
}

void NoiseVolume::shutdown() {
    if (view_.id) sg_destroy_view(view_);
    if (image_.id) sg_destroy_image(image_);
    view_ = {};
    image_ = {};
}

}  // namespace dmxviz::render
