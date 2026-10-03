// ============================================================================
// depth_downsample.glsl - builds the reduced-resolution depth used by the haze
// pass: for every NxN block of full-res pixels (N = 2 for half-res haze, 4 for
// quarter-res, frame.params2.w), the nearest and the farthest scene distance (RG32F). The volumetric pass clips beams with the farthest
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
    int block = int(frame.params2.w + 0.5);
    ivec2 base = ivec2(gl_FragCoord.xy) * block;
    ivec2 maxPixel = ivec2(frame.viewport.xy) - 1;
    float nearest = 1e30;
    float farthest = 0.0;
    for (int y = 0; y < block; ++y) {
        for (int x = 0; x < block; ++x) {
            float d = texelFetch(u_gDistance, min(base + ivec2(x, y), maxPixel), 0).r;
            nearest = min(nearest, d);
            farthest = max(farthest, d);
        }
    }
    o_minMax = vec4(nearest, farthest, 0.0, 0.0);
}
#endif
