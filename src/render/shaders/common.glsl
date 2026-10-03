// ============================================================================
// common.glsl - declarations shared by every DmxViz render shader.
//
// How the shader files are put together (see ShaderProgram.cpp):
//   * each file holds BOTH stages of one program; the loader compiles it twice
//     with VERTEX_SHADER or FRAGMENT_SHADER defined,
//   * `#include "x.glsl"` pastes another file (once),
//   * the loader writes the `#version 430 core` line itself.
//
// Units: positions in metres, angles in radians, colours linear RGB. Light
// values in the HDR buffers are "HDR units" (1.0 = 250 cd/m^2, see BeamMath.h).
// ============================================================================

#version 430 core

const float PI = 3.14159265359;

// Per-view constants, uploaded once per frame (C++ mirror: FrameGpu in GpuTypes.h).
// A storage buffer rather than plain uniforms so every shader can include the
// same declaration without sokol complaining about uniforms a shader never uses.
layout(std430, binding = 0) readonly buffer FrameConstants {
    mat4 viewProj;         // world -> clip (y is flipped, so the image is stored top row first)
    mat4 invViewProj;      // clip -> world
    vec4 cameraPos;        // xyz = camera position, w = animation time (s)
    vec4 cameraForward;    // xyz = unit view direction, w = focal length in full-res pixels
    vec4 viewport;         // full res: width, height, 1/width, 1/height
    vec4 volumeViewport;   // volumetric target (half or quarter res): width, height, 1/width, 1/height
    vec4 haze;             // x = scattering coefficient (1/m), y = density variation, z = beam brightness, w = phase g
    vec4 ambientExposure;  // rgb = ambient light (HDR units), w = exposure
    vec4 params;           // x = HDR units per nit, y = frame counter, z = min march steps, w = max march steps
    vec4 params2;          // x = clip beams at floor (0/1), y = bloom strength, z = volume-target pixels per step,
                           // w = volume resolution divisor (2 or 4)
} frame;

// Unit direction of the camera ray through a screen position (uv in 0..1 over the target).
vec3 viewRayDirection(vec2 uv) {
    vec4 farPoint = frame.invViewProj * vec4(uv * 2.0 - 1.0, 1.0, 1.0);
    return normalize(farPoint.xyz / farPoint.w - frame.cameraPos.xyz);
}

// World position -> screen uv (0..1) and whether it is in front of the camera.
vec2 worldToUv(vec3 p, out bool inFront) {
    vec4 clip = frame.viewProj * vec4(p, 1.0);
    inFront = clip.w > 1e-4;
    return clip.xy / max(clip.w, 1e-4) * 0.5 + 0.5;
}

// Interleaved gradient noise (Jimenez 2014): a cheap per-pixel value in [0, 1)
// with a blue-noise-like spectrum. Used to jitter ray-march samples and to
// dither the final image so smooth gradients do not band.
float interleavedGradientNoise(vec2 pixel) {
    return fract(52.9829189 * fract(dot(pixel, vec2(0.06711056, 0.00583715))));
}

float luminance(vec3 c) {
    return dot(c, vec3(0.2126, 0.7152, 0.0722));
}

// The 8-bit output image stores sRGB-encoded values (what ImGui expects).
float linearToSrgb(float c) {
    return c <= 0.0031308 ? 12.92 * c : 1.055 * pow(c, 1.0 / 2.4) - 0.055;
}

vec3 linearToSrgb(vec3 c) {
    return vec3(linearToSrgb(c.r), linearToSrgb(c.g), linearToSrgb(c.b));
}

// Octahedral normal encoding: a unit vector in two numbers (Cigolle et al. 2014).
vec2 octWrap(vec2 v) {
    return (1.0 - abs(v.yx)) * vec2(v.x >= 0.0 ? 1.0 : -1.0, v.y >= 0.0 ? 1.0 : -1.0);
}

vec2 encodeNormal(vec3 n) {
    n /= abs(n.x) + abs(n.y) + abs(n.z);
    vec2 e = n.z >= 0.0 ? n.xy : octWrap(n.xy);
    return e;
}

vec3 decodeNormal(vec2 e) {
    vec3 n = vec3(e, 1.0 - abs(e.x) - abs(e.y));
    if (n.z < 0.0) n.xy = octWrap(n.xy);
    return normalize(n);
}

// Full-screen triangle for post passes: draw 3 vertices without any buffer.
// Vertex 0..2 cover clip space [-1,3] so the visible part is the whole screen.
#ifdef VERTEX_SHADER
vec4 fullscreenTrianglePosition() {
    vec2 p = vec2(float((gl_VertexID << 1) & 2), float(gl_VertexID & 2));
    return vec4(p * 2.0 - 1.0, 0.0, 1.0);
}
#endif
