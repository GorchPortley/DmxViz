#pragma once
// CPU mirrors of the data the shaders read. Every struct here is copied to the
// GPU byte for byte, so its layout must match the GLSL declaration named in
// its comment: only vec4/mat4 members (std430 and vertex attributes then agree
// with C++ without padding surprises).

#include "core/Math.h"

#include <cstdint>

namespace dmxviz::render {

// Per-view constants: storage buffer `FrameConstants` in shaders/common.glsl.
struct FrameGpu {
    glm::mat4 viewProj{1.0f};     // world -> clip (y flipped, see Renderer.cpp "Image orientation")
    glm::mat4 invViewProj{1.0f};  // clip -> world
    glm::vec4 cameraPos{0.0f};    // xyz = position (m), w = animation time (s)
    glm::vec4 cameraForward{0.0f};// xyz = unit view direction, w = focal length in full-res pixels
    glm::vec4 viewport{0.0f};     // full res: width, height, 1/width, 1/height
    glm::vec4 halfViewport{0.0f}; // half res: width, height, 1/width, 1/height
    glm::vec4 haze{0.0f};         // x = scattering (1/m), y = density variation, z = beam brightness, w = phase g
    glm::vec4 ambientExposure{0.0f}; // rgb = ambient light (HDR units), w = exposure
    glm::vec4 params{0.0f};       // x = HDR units per nit, y = frame counter, z = min steps, w = max steps
    glm::vec4 params2{0.0f};      // x = clip beams at floor, y = bloom strength, z = pixels per march step, w = 0
};
static_assert(sizeof(FrameGpu) % 16 == 0);

// Per-instance vertex attributes of the G-buffer pass (shaders/gbuffer.glsl).
struct InstanceGpu {
    glm::vec4 world0{1, 0, 0, 0};  // world matrix columns
    glm::vec4 world1{0, 1, 0, 0};
    glm::vec4 world2{0, 0, 1, 0};
    glm::vec4 world3{0, 0, 0, 1};
    glm::vec4 albedoRoughness{0.5f, 0.5f, 0.5f, 0.6f};
    glm::vec4 emissiveMetallic{0.0f};  // rgb = emissive (HDR units), a = metallic
    glm::vec4 extra{0.0f};             // x = highlight (0 none, 1 hover, 2 selected)
};
static_assert(sizeof(InstanceGpu) == 7 * 16);

// One beam (or one prism facet of a beam): storage buffer `Beams` in
// shaders/beam_common.glsl. Built by BeamPacker from BeamState.
struct BeamGpu {
    glm::vec4 posLength{0.0f};   // xyz = lens centre (m), w = hull length along the axis (m)
    glm::vec4 dirHullTan{0.0f};  // xyz = unit axis, w = tan of the hull half angle
    glm::vec4 upHullApex{0.0f};  // xyz = unit up (gobo/blade reference), w = hull apex distance behind the lens
    glm::vec4 radiance{0.0f};    // rgb = colour * peak intensity (HDR-scaled candela), w = shape (0 round, 1 rect)
    glm::vec4 profile{0.0f};     // x = tanBeam, y = exponent, z = tanField, w = tanCutoff
    glm::vec4 apex{0.0f};        // x = z0 (virtual apex, round or rect width), y = z0 rect height,
                                 // z = lens radius (m), w = base gobo blur (gobo uv units)
    glm::vec4 goboLayers{0.0f};  // x, y, z = gobo atlas layers (gobo 1, gobo 2, animation wheel; 0 = open),
                                 // w = blade edge softness (uv units)
    glm::vec4 goboRotation{0.0f};// x, y, z = rotation (rad) of the layers above, w = iris (0..1)
    glm::vec4 bladeInsertion{0.0f}; // blades 0..3 (top, left, bottom, right), 0 = out, 1 = to the centre
    glm::vec4 bladeAngle{0.0f};     // blade edge tilt (rad)
    glm::vec4 misc{0.0f};           // x = blade assembly rotation (rad), y = flags (1 = gobos, 2 = blades), zw = 0
};
static_assert(sizeof(BeamGpu) == 11 * 16);

// One lens glow sprite: storage buffer `Glows` in shaders/lens_glow.glsl.
struct GlowGpu {
    glm::vec4 posRadius{0.0f};   // xyz = lens centre, w = emitter radius (m)
    glm::vec4 dirTanBeam{0.0f};  // xyz = beam axis, w = tanBeam
    glm::vec4 radiance{0.0f};    // rgb = colour * peak intensity (HDR-scaled candela), w = exponent
    glm::vec4 misc{0.0f};        // x = tanCutoff, y = emitter area (m^2), z = shape (0 round, 1 rect, 2 glow), w = 0
};
static_assert(sizeof(GlowGpu) == 4 * 16);

// Debug line vertex (shaders/lines.glsl).
struct LineVertexGpu {
    glm::vec4 position{0.0f};  // xyz, w = 1
    glm::vec4 color{1.0f};     // linear RGBA
};
static_assert(sizeof(LineVertexGpu) == 32);

}  // namespace dmxviz::render
