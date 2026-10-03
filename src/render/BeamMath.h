#pragma once
// Beam optics on the CPU: angular intensity profile, flux -> intensity,
// beam volume extent and frustum culling. The shaders (beam_common.glsl)
// evaluate the same profile per pixel; this file prepares their inputs.
//
// Header-only and free of GPU dependencies so tests/render/ can test it.
//
// The model, in short:
//   * Angles are handled as tangents ("t = tan(angle from the beam axis)"), which
//     is what you get when you intersect the beam with a plane across the axis.
//   * Profile f(t) = 2^-((t / tanBeam)^p): f = 1 on the axis, 0.5 at the beam
//     angle, 0.1 at the field angle (p is solved for that), a super-Gaussian
//     that is soft for washes (field >> beam) and hard for spots (field ~ beam).
//   * A beam starts as the lens disc, not as a point: it behaves like a point
//     source placed a distance z0 = lensRadius / tanField *behind* the lens
//     (the "virtual apex"). Inverse-square falloff is measured from there, which
//     keeps the brightness finite at the lens and conserves flux along the beam.
//   * Peak intensity I0 [cd] = flux [lm] / solid angle of the profile [sr], so a
//     narrow 20k lm spot is far brighter on axis than a wide 500 lm LED pixel.

#include "core/SceneTypes.h"

#include <algorithm>
#include <array>
#include <cmath>

namespace dmxviz::render::beammath {

// ---------------------------------------------------------------------------
// Photometric calibration
// ---------------------------------------------------------------------------

// The HDR buffers store luminance in "HDR units": 1.0 = kNitsPerHdrUnit cd/m^2.
// With exposure 1 the tonemapper maps ~1.0 to a bright, not yet clipped value,
// i.e. a dark stage floor (albedo 0.1) lit by ~8000 lux reads as "bright".
constexpr float kNitsPerHdrUnit = 250.0f;
constexpr float kHdrPerNit = 1.0f / kNitsPerHdrUnit;

// Haze scattering coefficient (1/m) at Environment::hazeDensity = 1. Real stage
// haze spans roughly 0.01/m (barely visible) to 0.3/m (thick, short visibility).
constexpr float kHazeScatteringAtFullDensity = 0.12f;

// Profile value where a beam volume is cut off (1 % of the axis intensity).
constexpr float kProfileCutoff = 0.01f;
constexpr float kMinHalfAngle = degToRad(0.1f);
constexpr float kMaxHalfAngle = degToRad(80.0f);  // keeps tan() finite for very wide emitters
constexpr float kMinLensRadius = 0.001f;           // m
constexpr int kHullSegments = 24;                  // sides of the polygonal beam hull (see Renderer)

// ---------------------------------------------------------------------------
// Angular profile
// ---------------------------------------------------------------------------

struct BeamProfile {
    float tanBeam = 0.1f;    // tan(half beam angle): f = 0.5 here
    float tanField = 0.12f;  // tan(half field angle): gobo/iris/blade coordinates reach 1 here
    float exponent = 2.0f;   // p in f(t) = 2^-((t / tanBeam)^p)
    float tanCutoff = 0.2f;  // f = kProfileCutoff here; the beam volume ends at this angle
};

inline float profileValue(float t, const BeamProfile& p) {
    return std::exp2(-std::pow(std::max(t, 0.0f) / p.tanBeam, p.exponent));
}

// Exponent p so that f(tanBeam) = 0.5 and f(tanField) = 0.1:
//   2^-((tf/tb)^p) = 0.1  <=>  (tf/tb)^p = log2(10)  <=>  p = ln(log2 10) / ln(tf/tb)
inline float profileExponent(float tanBeam, float tanField) {
    const float ratio = std::max(tanField / tanBeam, 1.0001f);
    return std::clamp(std::log(std::log2(10.0f)) / std::log(ratio), 1.0f, 48.0f);
}

// Tangent where the profile has fallen to `level` (0 < level < 1).
inline float profileTanAt(float level, float tanBeam, float exponent) {
    return tanBeam * std::pow(std::log2(1.0f / level), 1.0f / exponent);
}

// Builds the profile for a beam, including the softening effects of focus and
// frost. Frost diffuses the light: slightly wider and towards a Gaussian (p = 2).
// An unfocused profile spot has soft edges, so focus caps how hard the edge gets.
inline BeamProfile makeProfile(const BeamState& b) {
    const float frost = std::clamp(b.frost, 0.0f, 1.0f);
    const float focus = std::clamp(b.focus, 0.0f, 1.0f);
    const float widen = 1.0f + 0.3f * frost;

    const float halfBeam = std::clamp(0.5f * b.beamAngle * widen, kMinHalfAngle, kMaxHalfAngle);
    const float halfField =
        std::clamp(0.5f * std::max(b.fieldAngle, b.beamAngle) * widen, halfBeam, kMaxHalfAngle);

    BeamProfile p;
    p.tanBeam = std::tan(halfBeam);
    p.tanField = std::max(std::tan(halfField), p.tanBeam * 1.0001f);
    float exponent = profileExponent(p.tanBeam, p.tanField);
    if (b.shape == BeamShape::Beam) exponent = std::max(exponent, 8.0f);  // tight, crisp shaft
    if (b.shape != BeamShape::Wash && b.shape != BeamShape::Glow) {
        const float maxExponent = 2.5f + (48.0f - 2.5f) * focus * focus;
        exponent = std::min(exponent, maxExponent);
    }
    exponent = exponent + (std::min(exponent, 2.0f) - exponent) * frost;  // frost only ever softens
    p.exponent = std::clamp(exponent, 1.0f, 48.0f);
    p.tanCutoff = std::min(profileTanAt(kProfileCutoff, p.tanBeam, p.exponent), std::tan(kMaxHalfAngle));
    p.tanCutoff = std::max(p.tanCutoff, p.tanField);
    return p;
}

// Solid angle "seen" by the profile, i.e. the integral of f over the plane
// across the beam at unit distance:  Omega = 2*pi * Int f(t) t dt
//                                          = pi tb^2 Gamma(1 + 2/p) / (ln 2)^(2/p).
// Dividing the flux by it gives the peak (axis) intensity.
inline float profileSolidAngleRound(const BeamProfile& p) {
    const float ln2 = 0.69314718f;
    return kPi * p.tanBeam * p.tanBeam * std::tgamma(1.0f + 2.0f / p.exponent) /
           std::pow(ln2, 2.0f / p.exponent);
}

// Rectangular emitters use a separable profile f(tx) * f(ty); the solid angle
// is the square of the 1-D integral  Int f(t) dt = 2 tb Gamma(1 + 1/p) / (ln 2)^(1/p).
inline float profileSolidAngleRect(const BeamProfile& p) {
    const float ln2 = 0.69314718f;
    const float width = 2.0f * p.tanBeam * std::tgamma(1.0f + 1.0f / p.exponent) / std::pow(ln2, 1.0f / p.exponent);
    return width * width;
}

// Peak luminous intensity in candela for a given total flux.
inline float peakIntensity(float luminousFlux, float intensity, float solidAngle) {
    return std::max(luminousFlux, 0.0f) * std::max(intensity, 0.0f) / std::max(solidAngle, 1e-6f);
}

// ---------------------------------------------------------------------------
// Beam volume (the "hull" drawn for surface lighting and haze)
// ---------------------------------------------------------------------------

// The hull is a truncated cone around the beam axis. Its apex sits apexDistance
// behind the lens and it opens with tanHalfAngle; it is cut at the lens plane
// (z = 0) and at z = length.
struct BeamHull {
    float tanHalfAngle = 0.2f;
    float apexDistance = 0.1f;  // m behind the lens
    float length = 10.0f;       // m along the axis, from the lens
};

// Axial length after which every point of the hull lies below the floor (y = 0),
// or `fallback` when part of the hull never reaches the floor.
inline float lengthToFloor(const glm::vec3& lensPos, const glm::vec3& dir, const BeamHull& hull, float fallback) {
    if (lensPos.y <= 0.0f) return fallback;
    const float apexY = lensPos.y - dir.y * hull.apexDistance;
    if (apexY <= 0.0f) return fallback;
    // Angle between the axis and straight down, and the shallowest ray in the hull.
    const float beta = std::acos(std::clamp(-dir.y, -1.0f, 1.0f));
    const float halfAngle = std::atan(hull.tanHalfAngle);
    const float shallowest = beta + halfAngle;
    if (shallowest >= degToRad(89.0f)) return fallback;
    // That ray hits the floor after apexY / cos(shallowest); project it onto the axis.
    const float slant = apexY / std::cos(shallowest);
    const float axial = slant * std::cos(halfAngle) - hull.apexDistance;
    return std::clamp(axial + 0.05f, 0.0f, fallback);
}

// Rough number of target pixels the hull covers (its silhouette), for the cost estimate of the haze pass:
// the shader shades every pixel of the hull, so coverage x march steps is the work a beam causes.
// `focalPixels` is the camera focal length in pixels of the target being drawn. A beam seen from the side shows
// the trapezoid between its two end discs, one pointing at the camera shows the far disc; the estimate blends the
// two by the viewing angle. Capped at `maxPixels` (a hull cannot cover more than the whole target, however near).
inline float hullCoveragePixels(const BeamHull& h, const glm::vec3& lensPos, const glm::vec3& dir,
                                const glm::vec3& cameraPos, float focalPixels, float maxPixels) {
    const float r0 = h.tanHalfAngle * h.apexDistance;
    const float r1 = h.tanHalfAngle * (h.apexDistance + h.length);
    const glm::vec3 toCamera = cameraPos - (lensPos + dir * (0.5f * h.length));
    const float distance = std::max(glm::length(toCamera), 0.1f);
    const float cosView = std::min(std::abs(glm::dot(dir, toCamera)) / distance, 1.0f);  // 1 = along the axis
    const float sinView = std::sqrt(1.0f - cosView * cosView);
    const float area = h.length * (r0 + r1) * sinView + kPi * r1 * r1 * cosView;  // m^2
    const float pixelsPerMetre = focalPixels / distance;
    return std::min(area * pixelsPerMetre * pixelsPerMetre, maxPixels);
}

// Axial distance where the on-axis illuminance I0 / (z + z0)^2 drops below minLux.
inline float lengthByBrightness(float peakCandela, float apexDistance, float minLux) {
    if (minLux <= 0.0f) return 1e6f;
    return std::max(std::sqrt(peakCandela / minLux) - apexDistance, 0.0f);
}

// ---------------------------------------------------------------------------
// Frustum culling
// ---------------------------------------------------------------------------

struct Plane {
    glm::vec3 normal{0.0f, 1.0f, 0.0f};  // points into the frustum
    float d = 0.0f;                      // dot(normal, p) + d >= 0 inside
};

using Frustum = std::array<Plane, 6>;

// Gribb/Hartmann plane extraction for an OpenGL-style view-projection matrix.
inline Frustum frustumFromViewProj(const glm::mat4& m) {
    const glm::vec4 r0{m[0][0], m[1][0], m[2][0], m[3][0]};
    const glm::vec4 r1{m[0][1], m[1][1], m[2][1], m[3][1]};
    const glm::vec4 r2{m[0][2], m[1][2], m[2][2], m[3][2]};
    const glm::vec4 r3{m[0][3], m[1][3], m[2][3], m[3][3]};
    const glm::vec4 eq[6] = {r3 + r0, r3 - r0, r3 + r1, r3 - r1, r3 + r2, r3 - r2};
    Frustum f;
    for (int i = 0; i < 6; ++i) {
        const float len = glm::length(glm::vec3(eq[i]));
        f[static_cast<std::size_t>(i)] = {glm::vec3(eq[i]) / len, eq[i].w / len};
    }
    return f;
}

// A disc (centre, unit normal, radius) lies completely on the outside of a plane.
inline bool discOutside(const Plane& pl, const glm::vec3& centre, const glm::vec3& normal, float radius) {
    const float c = glm::dot(pl.normal, normal);
    const float extent = radius * std::sqrt(std::max(0.0f, 1.0f - c * c));
    return glm::dot(pl.normal, centre) + pl.d + extent < 0.0f;
}

// The truncated cone is the convex hull of its two end discs: it is outside the
// frustum if both discs are outside the same plane.
inline bool hullOutsideFrustum(const Frustum& f, const glm::vec3& lensPos, const glm::vec3& dir, const BeamHull& h) {
    const float r0 = h.tanHalfAngle * h.apexDistance;
    const float r1 = h.tanHalfAngle * (h.apexDistance + h.length);
    const glm::vec3 end = lensPos + dir * h.length;
    for (const Plane& pl : f) {
        if (discOutside(pl, lensPos, dir, r0) && discOutside(pl, end, dir, r1)) return true;
    }
    return false;
}

// Box outside if, for some plane, even its corner furthest along the plane normal is outside.
inline bool aabbOutsideFrustum(const Frustum& f, const Aabb& box) {
    if (box.empty()) return true;
    for (const Plane& pl : f) {
        const glm::vec3 corner{pl.normal.x >= 0.0f ? box.max.x : box.min.x, pl.normal.y >= 0.0f ? box.max.y : box.min.y,
                            pl.normal.z >= 0.0f ? box.max.z : box.min.z};
        if (glm::dot(pl.normal, corner) + pl.d < 0.0f) return true;
    }
    return false;
}

inline bool sphereOutsideFrustum(const Frustum& f, const glm::vec3& centre, float radius) {
    for (const Plane& pl : f) {
        if (glm::dot(pl.normal, centre) + pl.d < -radius) return true;
    }
    return false;
}

}  // namespace dmxviz::render::beammath
