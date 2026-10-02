// ============================================================================
// lens_glow.glsl - pass 4: the glare of a lens you look into.
//
// One camera-facing sprite per lens (instanced, 6 vertices built from
// gl_VertexID, no vertex buffer). Two parts:
//   * core: the lens disc itself. Its brightness is energy-conserving: the
//     illuminance the lens produces at the camera, spread over the disc's
//     on-screen size (at least one pixel, so distant fixtures still sparkle);
//   * halo: a soft glare ring, a small fraction of the same energy.
// Both scale with the beam profile at the viewing angle, so a lens glows when
// the beam points at the camera and is dark from the side or behind.
// Occlusion is tested in the vertex shader against the scene distance around
// the lens (a few taps -> soft fade instead of popping).
// ============================================================================

#include "common.glsl"

// Mirror of GlowGpu (GpuTypes.h).
struct Glow {
    vec4 posRadius;   // xyz = lens centre, w = emitter radius (m)
    vec4 dirTanBeam;  // xyz = beam axis, w = tanBeam
    vec4 radiance;    // rgb = colour * peak intensity (HDR-scaled cd), w = exponent
    vec4 misc;        // x = tanCutoff, y = emitter area (m^2), z = shape (0 round, 1 rect, 2 glow)
};

layout(std430, binding = 2) readonly buffer Glows {
    Glow glows[];
};

// Fraction of the lens energy that goes into the halo, and the halo size
// relative to the core (in pixels: max(coreRadius * scale, minimum)).
const float kHaloEnergy = 0.06;
const float kHaloScale = 3.0;
const float kHaloMinPixels = 3.0;
const float kHaloExtent = 5.0;  // sprite half size in halo radii (the falloff is < 0.3 % there)
const float kMaxCoreRadiance = 20000.0;  // HDR units; keeps fp16 targets far from overflow

#ifdef VERTEX_SHADER
uniform sampler2D u_gDistance;

out vec2 v_offsetPx;         // pixel offset from the lens centre
flat out vec3 v_core;        // core radiance (HDR units)
flat out vec3 v_halo;        // halo peak radiance (HDR units)
flat out vec2 v_radiiPx;     // x = core radius, y = halo scale (pixels)

// Fraction of a few distance-buffer taps around the lens that are not in front of it.
float lensVisibility(vec2 uv, float lensDistance, float radiusPx, float bias) {
    vec2 size = frame.viewport.xy;
    float visible = 0.0;
    const vec2 taps[5] = vec2[](vec2(0.0), vec2(1.0, 0.0), vec2(-1.0, 0.0), vec2(0.0, 1.0), vec2(0.0, -1.0));
    for (int i = 0; i < 5; ++i) {
        ivec2 p = ivec2(uv * size + taps[i] * radiusPx);
        if (any(lessThan(p, ivec2(0))) || any(greaterThanEqual(p, ivec2(size)))) {
            visible += 1.0;  // off screen: assume visible
            continue;
        }
        float sceneDistance = texelFetch(u_gDistance, p, 0).r;
        visible += sceneDistance + bias >= lensDistance ? 1.0 : 0.0;
    }
    return visible / 5.0;
}

void main() {
    Glow g = glows[gl_InstanceID];
    vec3 toCamera = frame.cameraPos.xyz - g.posRadius.xyz;
    float dist = length(toCamera);
    vec3 v = toCamera / dist;

    // Beam profile in the direction of the camera.
    float cosAngle = dot(g.dirTanBeam.xyz, v);
    float t = cosAngle > 1e-3 ? sqrt(max(1.0 - cosAngle * cosAngle, 0.0)) / cosAngle : 1e6;
    float f = t < g.misc.x ? exp2(-pow(max(t / g.dirTanBeam.w, 1e-6), g.radiance.w)) : 0.0;

    bool inFront;
    vec2 uv = worldToUv(g.posRadius.xyz, inFront);
    float focalPx = frame.cameraForward.w;
    float coreRadiusPx = max(focalPx * g.posRadius.w / dist, 0.75);

    float bias = 2.0 * g.posRadius.w + 0.02 * dist + 0.05;
    float visibility = (inFront && f > 1e-4) ? lensVisibility(uv, dist, coreRadiusPx + 1.0, bias) : 0.0;

    // Illuminance at the camera, spread over the solid angle of the core disc.
    vec3 illuminance = g.radiance.rgb * f / (dist * dist) * visibility;
    float pixelAngle = 1.0 / focalPx;
    float coreSolidAngle = PI * coreRadiusPx * coreRadiusPx * pixelAngle * pixelAngle;
    vec3 core = illuminance / coreSolidAngle;
    float peak = max(core.r, max(core.g, core.b));
    if (peak > kMaxCoreRadiance) core *= kMaxCoreRadiance / peak;

    float haloPx = max(coreRadiusPx * kHaloScale, kHaloMinPixels);
    float haloSolidAngle = PI * haloPx * haloPx * pixelAngle * pixelAngle;  // integral of the falloff below
    v_halo = illuminance * kHaloEnergy / haloSolidAngle;
    v_core = core * (1.0 - kHaloEnergy);
    v_radiiPx = vec2(coreRadiusPx, haloPx);

    // Corner of the quad (two triangles).
    const vec2 corners[6] = vec2[](vec2(-1, -1), vec2(1, -1), vec2(1, 1), vec2(-1, -1), vec2(1, 1), vec2(-1, 1));
    vec2 corner = corners[gl_VertexID];
    float halfSizePx = max(haloPx * kHaloExtent, coreRadiusPx + 2.0);
    v_offsetPx = corner * halfSizePx;

    vec2 clipXY = (uv + corner * halfSizePx * frame.viewport.zw) * 2.0 - 1.0;
    // Invisible sprites collapse to a degenerate quad (nothing rasterised).
    gl_Position = (visibility > 0.0) ? vec4(clipXY, 0.0, 1.0) : vec4(0.0, 0.0, 0.0, 1.0);
}
#endif

#ifdef FRAGMENT_SHADER
in vec2 v_offsetPx;
flat in vec3 v_core;
flat in vec3 v_halo;
flat in vec2 v_radiiPx;

out vec4 o_color;

void main() {
    float r = length(v_offsetPx);
    // Anti-aliased disc for the core.
    float core = 1.0 - smoothstep(v_radiiPx.x - 0.5, v_radiiPx.x + 0.5, r);
    // Halo: 1 / (1 + x^2)^2 integrates to pi * scale^2, matching haloSolidAngle.
    float x = r / v_radiiPx.y;
    float halo = 1.0 / ((1.0 + x * x) * (1.0 + x * x));
    o_color = vec4(v_core * core + v_halo * halo, 0.0);
}
#endif
