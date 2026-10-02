// ============================================================================
// beam_common.glsl - the optical model of one beam, shared by surface lighting
// (spot_light.glsl) and the haze ray-march (volumetric.glsl).
//
// A beam leaves a lens disc. Mathematically it is treated as a point light
// placed at a "virtual apex" a little behind the lens (z0 = lensRadius /
// tanField), so near the lens the light comes from the whole disc and the
// inverse-square falloff stays finite. Directions are described by tangents
// t = (lateral offset) / (distance from the apex): t = tan(angle off axis).
//
//   intensity(t) = I0 * f(t) * pattern(t)        [cd]
//   f(t)         = 2^-((|t| / tanBeam)^p)        0.5 at the beam angle, 0.1 at the field angle
//   pattern(t)   = gobos * animation wheel * framing blades (0..1 per colour)
//   illuminance  = intensity / distance^2        [lux], distance from the apex
//
// C++ side: BeamPacker.cpp fills the struct below, BeamMath.h has the CPU math.
// ============================================================================

#include "common.glsl"

// Mirror of BeamGpu (GpuTypes.h). Everything per beam that a shader needs.
struct Beam {
    vec4 posLength;      // xyz = lens centre, w = hull length along the axis (m)
    vec4 dirHullTan;     // xyz = unit beam axis, w = tan of the hull half angle
    vec4 upHullApex;     // xyz = unit "up" of gobos/blades, w = hull apex distance behind the lens
    vec4 radiance;       // rgb = colour * peak intensity (HDR-scaled cd), w = shape (0 round, 1 rectangle)
    vec4 profile;        // x = tanBeam, y = exponent p, z = tanField, w = tanCutoff
    vec4 apex;           // x = z0 (width axis), y = z0 (height axis), z = lens radius, w = base gobo blur
    vec4 goboLayers;     // xyz = atlas layers of gobo 1, gobo 2, animation wheel (0 = open), w = blade softness
    vec4 goboRotation;   // xyz = rotation of those layers (rad), w = iris (0..1)
    vec4 bladeInsertion; // framing blades top, left, bottom, right: 0 = out, 1 = to the centre
    vec4 bladeAngle;     // tilt of each blade edge (rad)
    vec4 misc;           // x = blade assembly rotation (rad), y = flags (1 = has gobos, 2 = has blades)
};

layout(std430, binding = 1) readonly buffer Beams {
    Beam beams[];
};

// Gobo atlas: 2D array texture, one gobo per layer, layer 0 = open (white).
// RGB = transmission per colour (glass gobos), mipmapped so blur = mip bias.
uniform sampler2DArray u_gobos;

bool isRectangle(Beam b) { return b.radiance.w > 0.5; }

vec3 beamRight(Beam b) { return cross(b.dirHullTan.xyz, b.upHullApex.xyz); }

// Average virtual apex used for falloff and light direction.
vec3 beamApexPosition(Beam b) {
    float z0 = isRectangle(b) ? sqrt(b.apex.x * b.apex.y) : b.apex.x;
    return b.posLength.xyz - b.dirHullTan.xyz * z0;
}

// A world position expressed in the beam's frame.
struct BeamPoint {
    float z;        // distance along the axis from the lens (m); < 0 is behind the fixture
    vec2 t;         // tangent coordinates: x towards "right", y towards "up"
    float dist2;    // squared distance from the virtual apex (inverse-square law)
};

BeamPoint beamLocal(Beam b, vec3 p) {
    vec3 v = p - b.posLength.xyz;
    vec2 lateral = vec2(dot(v, beamRight(b)), dot(v, b.upHullApex.xyz));
    BeamPoint r;
    r.z = dot(v, b.dirHullTan.xyz);
    // Rectangles have a different virtual apex per axis, so the light spreads
    // from a w x h emitter instead of a disc.
    vec2 zApex = max(vec2(r.z) + b.apex.xy, vec2(1e-4));
    r.t = lateral / zApex;
    float za = isRectangle(b) ? sqrt(zApex.x * zApex.y) : zApex.x;
    r.dist2 = za * za + dot(lateral, lateral);
    return r;
}

// Angular profile f(t) in 0..1. Rectangles use a separable profile, which
// gives the soft-cornered rectangle of an LED panel or blinder cell.
float beamProfile(Beam b, vec2 t) {
    float tanBeam = b.profile.x;
    float p = b.profile.y;
    float tanCutoff = b.profile.w;
    float radius;
    float f;
    if (isRectangle(b)) {
        vec2 x = pow(max(abs(t) / tanBeam, vec2(1e-6)), vec2(p));
        f = exp2(-(x.x + x.y));  // 2^-a * 2^-b
        radius = max(abs(t.x), abs(t.y));
    } else {
        radius = length(t);
        f = exp2(-pow(max(radius / tanBeam, 1e-6), p));
    }
    // Fade out just before the cutoff so the edge of the hull geometry never shows.
    return f * (1.0 - smoothstep(0.85 * tanCutoff, tanCutoff, radius));
}

vec2 rotate2d(vec2 v, float angle) {
    float c = cos(angle), s = sin(angle);
    return vec2(c * v.x - s * v.y, s * v.x + c * v.y);
}

// One gobo layer. `uv` is -1..1 across the gobo, +y = beam up. Rotating the
// gobo by `angle` means sampling the image at the inversely rotated position.
vec3 sampleGobo(float layer, float angle, vec2 uv, float lod) {
    vec2 r = rotate2d(uv, -angle);
    vec2 tex = vec2(0.5 + 0.5 * r.x, 0.5 - 0.5 * r.y);  // image row 0 is the top (= up)
    return textureLod(u_gobos, vec3(tex, layer), lod).rgb;
}

// Framing shutters: each blade covers the half plane beyond a line at distance
// (1 - insertion) from the centre, tilted by its angle. `soft` widens the edge.
float bladeMask(Beam b, vec2 uv) {
    float soft = b.goboLayers.w;
    float mask = 1.0;
    for (int i = 0; i < 4; ++i) {
        float insertion = b.bladeInsertion[i];
        if (insertion <= 0.0) continue;
        float baseAngle = b.misc.x + 0.5 * PI * float(i + 1);  // blade 0 comes from the top (+y)
        vec2 n = vec2(cos(baseAngle), sin(baseAngle));       // from the centre towards the blade
        vec2 edgePoint = n * (1.0 - insertion);
        vec2 edgeNormal = rotate2d(n, b.bladeAngle[i]);
        mask *= 1.0 - smoothstep(-soft, soft, dot(uv - edgePoint, edgeNormal));
    }
    return mask;
}

// Transmission of gobos + animation wheel + blades for a direction t.
// extraBlur (gobo uv units) adds blur on top of the beam's focus/frost blur;
// the ray-march uses it for the lens defocus close to the fixture.
vec3 beamPattern(Beam b, vec2 t, float extraBlur) {
    int flags = int(b.misc.y + 0.5);
    if (flags == 0) return vec3(1.0);
    // Gobo coordinates: 1 at the field edge. The iris closes the beam angle but
    // not the gobo, so with a closed iris we see the centre of the gobo enlarged.
    vec2 uv = t / b.profile.z * b.goboRotation.w;
    vec3 transmission = vec3(1.0);
    if ((flags & 1) != 0) {
        float halfSize = 0.5 * float(textureSize(u_gobos, 0).x);  // texels per uv unit
        float lod = log2(max((b.apex.w + extraBlur) * halfSize, 1e-3));
        if (b.goboLayers.x > 0.5) transmission *= sampleGobo(b.goboLayers.x, b.goboRotation.x, uv, lod);
        if (b.goboLayers.y > 0.5) transmission *= sampleGobo(b.goboLayers.y, b.goboRotation.y, uv, lod);
        if (b.goboLayers.z > 0.5) transmission *= sampleGobo(b.goboLayers.z, b.goboRotation.z, uv, lod);
    }
    if ((flags & 2) != 0) transmission *= bladeMask(b, uv);
    return transmission;
}

// The beam hull: a truncated cone around the axis, drawn as a 24-sided prism-like
// mesh (unit template, see Renderer.cpp makeHullMesh). Both the lighting and the
// haze pass draw its BACK faces, so every covered pixel runs the fragment shader
// exactly once per beam, even with the camera inside the beam.
#ifdef VERTEX_SHADER
layout(location = 0) in vec3 a_hull;  // xy = point on the (circumscribed) unit circle, z = 0 lens end, 1 far end

vec3 beamHullVertex(Beam b) {
    vec3 dir = b.dirHullTan.xyz;
    vec3 up = b.upHullApex.xyz;
    vec3 side = cross(up, dir);  // (side, up, dir) is right-handed, so the mesh winding is preserved
    float z = a_hull.z * b.posLength.w;
    float radius = b.dirHullTan.w * (z + b.upHullApex.w);
    return b.posLength.xyz + dir * z + (side * a_hull.x + up * a_hull.y) * radius;
}
#endif
