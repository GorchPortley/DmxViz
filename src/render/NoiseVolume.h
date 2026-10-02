#pragma once
// A small tileable 3D noise texture (R8) for drifting haze. Generated once on
// the CPU at start-up: three octaves of smooth value noise, so one texture
// fetch per ray-march sample gives soft, cloud-like density variation.

#include "sokol_gfx.h"

namespace dmxviz::render {

class NoiseVolume {
public:
    static constexpr int kSize = 32;

    void init();
    void shutdown();
    sg_view textureView() const { return view_; }

private:
    sg_image image_{};
    sg_view view_{};
};

}  // namespace dmxviz::render
