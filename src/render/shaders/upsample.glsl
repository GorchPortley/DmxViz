// ============================================================================
// upsample.glsl - adds the half-resolution haze image to the full-resolution
// HDR target ("bilateral, depth-aware upsample").
//
// Plain bilinear upsampling would smear beam light across depth edges (a truss
// in front of a beam would get a bright halo). Instead each full-res pixel
// looks at the 3x3 half-res texels around it and weights them by distance in
// the image AND by how similar their scene depth is to its own. The small blur
// also removes the remaining ray-march noise.
// ============================================================================

#include "common.glsl"

#ifdef VERTEX_SHADER
void main() {
    gl_Position = fullscreenTrianglePosition();
}
#endif

#ifdef FRAGMENT_SHADER
uniform sampler2D u_volume;     // half-res in-scattered light
uniform sampler2D u_halfDepth;  // half-res min / max distance
uniform sampler2D u_gDistance;  // full-res distance

out vec4 o_color;

void main() {
    ivec2 pixel = ivec2(gl_FragCoord.xy);
    float depth = texelFetch(u_gDistance, pixel, 0).r;

    // Position of this pixel in half-res texel units (texel centres at .5).
    vec2 halfPos = gl_FragCoord.xy * 0.5;
    ivec2 centre = ivec2(halfPos);
    ivec2 maxTexel = ivec2(frame.halfViewport.xy) - 1;

    // Depth tolerance grows with distance (relative precision) plus a little slack.
    float tolerance = 0.03 * depth + 0.05;

    vec3 sum = vec3(0.0);
    float weightSum = 0.0;
    vec3 nearest = vec3(0.0);
    float nearestDiff = 1e30;
    for (int y = -1; y <= 1; ++y) {
        for (int x = -1; x <= 1; ++x) {
            ivec2 texel = clamp(centre + ivec2(x, y), ivec2(0), maxTexel);
            vec3 value = texelFetch(u_volume, texel, 0).rgb;
            float sampleDepth = texelFetch(u_halfDepth, texel, 0).g;

            vec2 offset = (vec2(texel) + 0.5) - halfPos;
            float spatial = exp(-dot(offset, offset) * 0.9);
            float diff = abs(sampleDepth - depth);
            float range = 1.0 / (1.0 + (diff / tolerance) * (diff / tolerance) * 4.0);
            float w = spatial * range;
            sum += value * w;
            weightSum += w;
            if (diff < nearestDiff) {
                nearestDiff = diff;
                nearest = value;
            }
        }
    }
    // If no neighbour has a matching depth, take the one that matches best.
    vec3 result = weightSum > 1e-3 ? sum / weightSum : nearest;
    o_color = vec4(result, 0.0);
}
#endif
