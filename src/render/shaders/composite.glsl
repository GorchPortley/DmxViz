// ============================================================================
// composite.glsl - final pass into the 8-bit image the UI shows:
//   HDR + bloom -> exposure -> tonemap -> selection outline -> sRGB -> dither.
//
// Tonemapping. A plain per-channel filmic curve pushes every bright colour
// towards white: a deep blue beam with a hot core turns pale. Lighting
// visualisers want saturated colours in the beams, so the default applies the
// curve to the brightest channel only and scales the colour by the result
// (hue and saturation preserved). Only truly extreme values (lens cores, many
// times brighter than anything else) are blended towards white, the way an
// overexposed lamp looks to a camera.
// ============================================================================

#include "common.glsl"

#ifdef VERTEX_SHADER
void main() {
    gl_Position = fullscreenTrianglePosition();
}
#endif

#ifdef FRAGMENT_SHADER
uniform sampler2D u_hdr;      // full-res HDR scene
uniform sampler2D u_bloom;    // bloom level 0 (half res, sum of all levels)
uniform sampler2D u_gNormal;  // w = highlight (0 none, 1 hover, 2 selected)
uniform vec4 u_params;        // x = bloom on (0/1), y = tonemapper (0 hue-preserving, 1 per-channel)

out vec4 o_color;

// Sum of the level weights 1 + s + s^2 + ... + s^5 with s = kScatter (bloom_up.glsl).
const float kBloomWeightSum = (1.0 - 0.075418890625) / (1.0 - 0.65);
const vec3 kSelectedColor = vec3(1.0, 0.55, 0.12);
const vec3 kHoverColor = vec3(0.35, 0.75, 1.0);

// Narkowicz 2015 fit of the ACES reference tonemapper.
float acesFit(float x) {
    x *= 0.6;
    return clamp((x * (2.51 * x + 0.03)) / (x * (2.43 * x + 0.59) + 0.14), 0.0, 1.0);
}

vec3 acesFit(vec3 x) {
    return vec3(acesFit(x.r), acesFit(x.g), acesFit(x.b));
}

vec3 tonemapHuePreserving(vec3 c) {
    float peak = max(c.r, max(c.g, c.b));
    if (peak <= 1e-6) return vec3(0.0);
    float mapped = acesFit(peak);
    vec3 saturated = c * (mapped / peak);
    // Path to white for extreme highlights only.
    float whiten = smoothstep(6.0, 80.0, peak);
    return mix(saturated, vec3(mapped), whiten);
}

float highlightAt(ivec2 p) {
    p = clamp(p, ivec2(0), ivec2(frame.viewport.xy) - 1);
    return texelFetch(u_gNormal, p, 0).w;
}

void main() {
    ivec2 pixel = ivec2(gl_FragCoord.xy);
    vec2 uv = gl_FragCoord.xy * frame.viewport.zw;
    vec3 color = texelFetch(u_hdr, pixel, 0).rgb;

    if (u_params.x > 0.5) {
        // Energy-preserving mix: bloom redistributes light, it does not add any.
        vec3 bloom = texture(u_bloom, uv).rgb / kBloomWeightSum;
        color = mix(color, bloom, frame.params2.y);
    }
    color *= frame.ambientExposure.w;
    color = u_params.y > 0.5 ? acesFit(color) : tonemapHuePreserving(color);

    // Selection / hover outline: a pixel at the border of a highlighted object
    // (a neighbour has a higher highlight value than this pixel) is painted.
    float own = highlightAt(pixel);
    float around = 0.0;
    const ivec2 offsets[8] = ivec2[](ivec2(2, 0), ivec2(-2, 0), ivec2(0, 2), ivec2(0, -2),
                                     ivec2(1, 1), ivec2(-1, 1), ivec2(1, -1), ivec2(-1, -1));
    for (int i = 0; i < 8; ++i) around = max(around, highlightAt(pixel + offsets[i]));
    if (around > own + 0.5) {
        color = around > 1.5 ? kSelectedColor : kHoverColor;
    } else if (own > 1.5) {
        color = mix(color, kSelectedColor, 0.08);
    }

    vec3 srgb = linearToSrgb(clamp(color, 0.0, 1.0));
    // Dither by +-0.5 LSB so dark haze gradients do not band in 8 bits.
    srgb += (interleavedGradientNoise(gl_FragCoord.xy) - 0.5) / 255.0;
    o_color = vec4(srgb, 1.0);
}
#endif
