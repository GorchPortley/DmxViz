// ============================================================================
// upsample.glsl - adds the reduced-resolution haze image to the full-resolution
// HDR target ("bilateral, depth-aware upsample").
//
// Plain bilinear upsampling would smear beam light across depth edges (a truss
// in front of a beam would get a bright halo). Instead each full-res pixel
// looks at the 3x3 volume texels around it and weights them by distance in
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
uniform sampler2D u_volume;     // reduced-res in-scattered light
uniform sampler2D u_volumeDepth;  // reduced-res min / max distance
uniform sampler2D u_gDistance;  // full-res distance

out vec4 o_color;

void main() {
    ivec2 pixel = ivec2(gl_FragCoord.xy);
    float depth = texelFetch(u_gDistance, pixel, 0).r;

    // Position of this pixel in volume texel units (texel centres at .5); params2.w = 2 (half) or 4 (quarter).
    vec2 volumePos = gl_FragCoord.xy / frame.params2.w;
    ivec2 centre = ivec2(volumePos);
    ivec2 maxTexel = ivec2(frame.volumeViewport.xy) - 1;

    vec3 sum = vec3(0.0);
    float weightSum = 0.0;
    vec3 nearest = vec3(0.0);
    float nearestDiff = 1e30;
    for (int y = -1; y <= 1; ++y) {
        for (int x = -1; x <= 1; ++x) {
            ivec2 texel = clamp(centre + ivec2(x, y), ivec2(0), maxTexel);
            vec3 value = texelFetch(u_volume, texel, 0).rgb;
            float sampleDepth = texelFetch(u_volumeDepth, texel, 0).g;

            vec2 offset = (vec2(texel) + 0.5) - volumePos;
            float spatial = exp(-dot(offset, offset) * 0.9);
            // Relative difference, measured against the NEARER of the two depths so
            // that sky pixels (distance 1e9) do not accept a truss texel as similar.
            float diff = abs(sampleDepth - depth) / max(min(sampleDepth, depth), 0.1);
            float range = 1.0 / (1.0 + diff * diff * 4000.0);  // ~0.5 at 1.6 % difference
            float w = spatial * range;
            sum += value * w;
            weightSum += w;
            if (diff < nearestDiff) {
                nearestDiff = diff;
                nearest = value;
            }
        }
    }
    // If no neighbour has a matching depth (thin geometry inside one volume
    // texel), take the one that matches best instead of a blurred mismatch.
    vec3 result = weightSum > 0.05 ? sum / weightSum : nearest;
    o_color = vec4(result, 0.0);
}
#endif
