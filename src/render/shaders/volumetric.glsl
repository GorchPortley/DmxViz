// ============================================================================
// volumetric.glsl - pass 3: light scattered towards the camera by haze inside
// each beam. Rendered at HALF resolution into an RGBA16F target with additive
// blending; upsample.glsl later adds it to the full-resolution HDR image.
//
// Per pixel and beam (one instanced draw, back faces of the beam hulls):
//   1. intersect the view ray with the beam's cone analytically -> [s0, s1]
//   2. clip the segment by the scene (half-res depth) and by the floor
//   3. march a few jittered samples through it and sum the in-scattered light
//
// Single scattering along the ray:
//   L = sigma_s * Int phase(theta) * I(t(s)) * density(s) / d(s)^2 ds
// d(s) is the distance from the beam's virtual apex. The 1/d^2 term varies
// enormously near the lens, so the samples are not spaced evenly but
// "equiangularly" (Kulla & Fajardo 2012): uniform in the angle seen from the
// apex. That places more samples where the beam is bright and cancels the
// 1/d^2 term exactly, which removes most of the noise with only ~6-20 samples.
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

uniform sampler2D u_halfDepth;  // rg = min / max scene distance of each 2x2 full-res block
uniform sampler3D u_noise;      // tileable 3D noise for drifting haze

out vec4 o_color;

// Ray (ro, rd) against the beam hull: a cone whose apex lies `apexDist` behind
// the lens, opening with tangent k, cut by the lens plane (z = 0) and by z = L.
// Returns the ray interval [s0, s1] that lies inside.
bool intersectBeamHull(Beam b, vec3 ro, vec3 rd, out float s0, out float s1) {
    vec3 axis = b.dirHullTan.xyz;
    float k = b.dirHullTan.w;
    float apexDist = b.upHullApex.w;
    float len = b.posLength.w;

    // 1) The slab 0 <= z <= L along the axis.
    float zo = dot(ro - b.posLength.xyz, axis);
    float dz = dot(rd, axis);
    float lo = -1e9, hi = 1e9;
    if (abs(dz) > 1e-6) {
        float a = -zo / dz, c = (len - zo) / dz;
        lo = min(a, c);
        hi = max(a, c);
    } else if (zo < 0.0 || zo > len) {
        return false;
    }

    // 2) The infinite cone. With h = distance along the axis from the apex, a
    //    point is inside when h >= cos(angle) * |X - apex|, i.e.
    //    g(s) = h^2 - cos^2 * |X - apex|^2 >= 0 (and h > 0, which the slab
    //    already guarantees). g is a quadratic in s: qa s^2 + qb s + qc.
    vec3 co = ro - (b.posLength.xyz - axis * apexDist);
    float cos2 = 1.0 / (1.0 + k * k);
    float hco = dot(co, axis);
    float qa = dz * dz - cos2;
    float qb = 2.0 * (dz * hco - cos2 * dot(rd, co));
    float qc = hco * hco - cos2 * dot(co, co);

    if (abs(qa) < 1e-7) {
        // Ray parallel to the cone's side: g is linear.
        if (abs(qb) > 1e-9) {
            float r = -qc / qb;
            if (qb > 0.0) lo = max(lo, r); else hi = min(hi, r);
        } else if (qc < 0.0) {
            return false;
        }
    } else {
        float disc = qb * qb - 4.0 * qa * qc;
        if (qa < 0.0) {
            // Ray steeper than the cone: inside between the two roots.
            if (disc < 0.0) return false;
            float sq = sqrt(disc);
            float r1 = (-qb + sq) / (2.0 * qa);
            float r2 = (-qb - sq) / (2.0 * qa);
            lo = max(lo, min(r1, r2));
            hi = min(hi, max(r1, r2));
        } else if (disc > 0.0) {
            // Ray within the cone's angle: it enters the forward cone once and
            // stays inside (towards +axis), the other root is on the mirrored cone.
            float sq = sqrt(disc);
            float r1 = (-qb - sq) / (2.0 * qa);
            float r2 = (-qb + sq) / (2.0 * qa);
            if (dz > 0.0) lo = max(lo, r2); else hi = min(hi, r1);
        }
    }
    s0 = lo;
    s1 = hi;
    return hi > lo;
}

// Henyey-Greenstein phase function: how much light is scattered by an angle
// with cosine `cosTheta`. g > 0 favours forward scattering, which is why a beam
// pointing at the camera looks so much brighter than one seen from the side.
float henyeyGreenstein(float cosTheta, float g) {
    float denom = 1.0 + g * g - 2.0 * g * cosTheta;
    return (1.0 - g * g) / (4.0 * PI * denom * sqrt(denom));
}

// Haze density multiplier around 1. The noise drifts slowly so the haze looks
// alive; hazeVariation (frame.haze.y) controls how patchy it is.
float hazeDensity(vec3 p) {
    float time = frame.cameraPos.w;
    vec3 q = p * 0.11 + vec3(0.013, 0.004, 0.009) * time;
    float n = texture(u_noise, q).r;  // mean ~0.5
    return max(mix(1.0, 2.0 * n, frame.haze.y), 0.0);
}

void main() {
    ivec2 pixel = ivec2(gl_FragCoord.xy);
    // The farthest depth of the 2x2 block: beams continue behind thin objects;
    // the depth-aware upsample sorts out the pixels that are actually in front.
    float sceneDistance = texelFetch(u_halfDepth, pixel, 0).g;

    vec3 ro = frame.cameraPos.xyz;
    vec3 rd = viewRayDirection(gl_FragCoord.xy * frame.halfViewport.zw);
    Beam b = beams[v_beam];

    float s0, s1;
    if (!intersectBeamHull(b, ro, rd, s0, s1)) discard;
    s0 = max(s0, 0.0);
    s1 = min(s1, sceneDistance);

    // Cheap stand-in for shadows: light from a fixture above the floor never
    // reaches below y = 0.
    if (frame.params2.x > 0.5 && b.posLength.y > 0.0) {
        if (abs(rd.y) > 1e-6) {
            float sFloor = -ro.y / rd.y;
            if (ro.y >= 0.0 && rd.y < 0.0) s1 = min(s1, sFloor);
            else if (ro.y < 0.0) s0 = rd.y > 0.0 ? max(s0, sFloor) : s1;
        } else if (ro.y < 0.0) {
            discard;
        }
    }
    if (s1 <= s0) discard;

    // Equiangular sampling around the virtual apex: the closest approach of the
    // ray to the apex is at s = delta, at distance D; a sample at angle theta
    // sits at s = delta + D * tan(theta).
    vec3 apex = beamApexPosition(b);
    float delta = dot(apex - ro, rd);
    float closest = max(length(ro + rd * delta - apex), 1e-4);
    float thetaA = atan(s0 - delta, closest);
    float thetaB = atan(s1 - delta, closest);

    // Fewer samples for beams that are short on screen.
    bool front0, front1;
    vec2 uv0 = worldToUv(ro + rd * s0, front0);
    vec2 uv1 = worldToUv(ro + rd * s1, front1);
    float screenLength = (front0 && front1) ? length((uv1 - uv0) * frame.halfViewport.xy) : 1e4;
    int steps = int(clamp(screenLength / frame.params2.z, frame.params.z, frame.params.w));

    float jitter = interleavedGradientNoise(gl_FragCoord.xy);
    float g = frame.haze.w;
    vec3 sum = vec3(0.0);
    for (int i = 0; i < steps; ++i) {
        float theta = mix(thetaA, thetaB, (float(i) + jitter) / float(steps));
        vec3 p = ro + rd * (delta + closest * tan(theta));

        BeamPoint bp = beamLocal(b, p);
        float f = beamProfile(b, bp.t);
        if (f < 1e-4) continue;

        // The gobo is imaged sharply far from the lens but blurred close to it.
        float lensBlur = b.apex.z / (sqrt(bp.dist2) * b.profile.z);
        vec3 pattern = beamPattern(b, bp.t, lensBlur);

        vec3 lightDir = (p - apex) * inversesqrt(bp.dist2);
        float phase = henyeyGreenstein(dot(lightDir, -rd), g);
        sum += pattern * (f * phase * hazeDensity(p));
    }

    // Each equiangular sample stands for (thetaB - thetaA) / steps of angle,
    // i.e. Int ds / d^2 = (thetaB - thetaA) / closest.
    float weight = (thetaB - thetaA) / (closest * float(steps));
    vec3 radiance = sum * weight * b.radiance.rgb * frame.haze.x * frame.haze.z;
    o_color = vec4(radiance, 0.0);
}
#endif
