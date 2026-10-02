// ============================================================================
// spot_light.glsl - pass 2: every beam lights the surfaces it hits.
//
// One instanced draw for all beams: instance i draws the hull of beams[i].
// Only back faces are rasterised and the depth test is GREATER_EQUAL against
// the scene depth, so a pixel runs this shader only when the visible surface
// lies in front of the hull's far side - i.e. possibly inside the beam.
// The fragment then reconstructs the surface position from the G-buffer
// distance, evaluates the beam (profile, gobos, blades, inverse square) and
// shades it with Lambert diffuse + GGX specular. Output is added (blend ONE,
// ONE) to the HDR target.
// ============================================================================

#include "common.glsl"
#include "beam_common.glsl"

#ifdef VERTEX_SHADER
flat out int v_beam;

void main() {
    v_beam = gl_InstanceID;
    gl_Position = frame.viewProj * vec4(beamHullVertex(beams[gl_InstanceID]), 1.0);
}
#endif

#ifdef FRAGMENT_SHADER
flat in int v_beam;

uniform sampler2D u_gAlbedo;    // rgb = albedo, a = roughness
uniform sampler2D u_gNormal;    // xy = octahedral normal, z = metallic
uniform sampler2D u_gDistance;  // r = distance from the camera

out vec4 o_color;

// GGX / Trowbridge-Reitz microfacet specular with Smith-Schlick visibility and
// Schlick Fresnel, plus energy-conserving Lambert diffuse. `lightSize` is the
// lens radius divided by the distance: like a sphere light (Karis 2013) it
// widens the highlight so a big lens does not give a pin-point reflection.
vec3 shadeSurface(vec3 albedo, float roughness, float metallic, vec3 n, vec3 v, vec3 l, float lightSize) {
    float nDotL = max(dot(n, l), 0.0);
    float nDotV = max(dot(n, v), 1e-3);
    vec3 h = normalize(v + l);
    float nDotH = max(dot(n, h), 0.0);
    float vDotH = max(dot(v, h), 0.0);

    float a = max(roughness * roughness, 0.002);
    float aWide = min(a + 0.5 * lightSize, 1.0);
    float energy = (a * a) / (aWide * aWide);  // keep the widened lobe's energy constant
    float a2 = aWide * aWide;
    float d = nDotH * nDotH * (a2 - 1.0) + 1.0;
    float distribution = a2 / (PI * d * d);

    float k = (roughness + 1.0) * (roughness + 1.0) / 8.0;
    float visibility = 1.0 / ((nDotL * (1.0 - k) + k) * (nDotV * (1.0 - k) + k) * 4.0);

    vec3 f0 = mix(vec3(0.04), albedo, metallic);
    vec3 fresnel = f0 + (1.0 - f0) * pow(1.0 - vDotH, 5.0);

    vec3 specular = distribution * visibility * fresnel * energy;
    vec3 diffuse = (1.0 - fresnel) * (1.0 - metallic) * albedo / PI;
    return (diffuse + specular) * nDotL;
}

void main() {
    ivec2 pixel = ivec2(gl_FragCoord.xy);
    float distance = texelFetch(u_gDistance, pixel, 0).r;

    vec3 viewDir = viewRayDirection(gl_FragCoord.xy * frame.viewport.zw);
    vec3 surface = frame.cameraPos.xyz + viewDir * distance;

    Beam b = beams[v_beam];
    BeamPoint bp = beamLocal(b, surface);
    if (bp.z < 0.0 || bp.z > b.posLength.w) discard;  // behind the lens or past the beam's end
    float f = beamProfile(b, bp.t);
    if (f < 1e-4) discard;

    vec3 toLight = beamApexPosition(b) - surface;
    vec3 l = toLight * inversesqrt(bp.dist2);
    vec4 nrm = texelFetch(u_gNormal, pixel, 0);
    vec3 n = decodeNormal(nrm.xy);
    if (dot(n, l) <= 0.0) discard;

    // Close to the lens a gobo is out of focus: every point of the lens disc
    // projects its own copy. The blur (in gobo units) is the lens radius seen
    // from the surface, divided by the field half-angle.
    float lensBlur = b.apex.z / (sqrt(bp.dist2) * b.profile.z);
    vec3 pattern = beamPattern(b, bp.t, 0.5 * lensBlur);

    vec3 illuminance = b.radiance.rgb * (f / bp.dist2) * pattern;  // HDR-scaled lux
    vec4 albedoRoughness = texelFetch(u_gAlbedo, pixel, 0);
    vec3 brdf = shadeSurface(albedoRoughness.rgb, albedoRoughness.a, nrm.z, n, -viewDir, l,
                             b.apex.z * inversesqrt(bp.dist2));
    o_color = vec4(brdf * illuminance, 0.0);
}
#endif
