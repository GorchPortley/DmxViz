// ============================================================================
// depth_downsample.glsl - builds the half-resolution depth used by the haze
// pass: for every 2x2 block of full-res pixels, the nearest and the farthest
// scene distance (RG32F). The volumetric pass clips beams with the farthest
// value; the upsample uses both to find depth edges.
// ============================================================================

#include "common.glsl"

#ifdef VERTEX_SHADER
void main() {
    gl_Position = fullscreenTrianglePosition();
}
#endif

#ifdef FRAGMENT_SHADER
uniform sampler2D u_gDistance;

out vec4 o_minMax;

void main() {
    ivec2 base = ivec2(gl_FragCoord.xy) * 2;
    ivec2 maxPixel = ivec2(frame.viewport.xy) - 1;
    float d0 = texelFetch(u_gDistance, min(base, maxPixel), 0).r;
    float d1 = texelFetch(u_gDistance, min(base + ivec2(1, 0), maxPixel), 0).r;
    float d2 = texelFetch(u_gDistance, min(base + ivec2(0, 1), maxPixel), 0).r;
    float d3 = texelFetch(u_gDistance, min(base + ivec2(1, 1), maxPixel), 0).r;
    o_minMax = vec4(min(min(d0, d1), min(d2, d3)), max(max(d0, d1), max(d2, d3)), 0.0, 0.0);
}
#endif
