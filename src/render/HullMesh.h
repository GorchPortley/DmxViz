#pragma once
// The unit beam hull: a 24-sided truncated cone template. Vertex = (x, y, z)
// with (x, y) on a circle slightly larger than the unit circle (so the polygon
// encloses the true circle) and z = 0 at the lens, 1 at the far end. The vertex
// shader (beam_common.glsl, beamHullVertex) scales it per beam instance.
// Triangles wind counter-clockwise seen from outside.

#include "sokol_gfx.h"

namespace dmxviz::render {

struct HullMesh {
    sg_buffer vertices{};
    sg_buffer indices{};  // uint16
    int indexCount = 0;

    void init();
    void shutdown();
};

}  // namespace dmxviz::render
