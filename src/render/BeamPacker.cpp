#include "render/BeamPacker.h"

#include "render/GoboAtlas.h"

#include <algorithm>
#include <cmath>

namespace dmxviz::render {
namespace {

using namespace beammath;

// The hull mesh is a polygon around the circle; this is how much bigger its
// corners are than the circle (used for conservative culling).
const float kHullPolygonScale = 1.0f / std::cos(kPi / kHullSegments);

// Smallest hull silhouette (in pixels of the volumetric target) that still gets a haze march.
constexpr float kMinVolumePixels = 1.0f;

// The step cap the cost budget may lower the march to at the very least.
constexpr float kBudgetMinSteps = 3.0f;

glm::vec3 safeNormalize(const glm::vec3& v, const glm::vec3& fallback) {
    const float len = glm::length(v);
    return len > 1e-6f ? v / len : fallback;
}

// Orthonormal frame from the beam's direction and up hint.
glm::vec3 perpendicularUp(const glm::vec3& dir, const glm::vec3& upHint) {
    glm::vec3 up = upHint - dir * glm::dot(upHint, dir);
    if (glm::length(up) < 1e-4f) {
        // Up parallel to the beam: pick any perpendicular vector.
        const glm::vec3 other = std::abs(dir.y) < 0.9f ? glm::vec3(0, 1, 0) : glm::vec3(0, 0, 1);
        up = other - dir * glm::dot(other, dir);
    }
    return glm::normalize(up);
}

}  // namespace

void BeamPacker::pack(std::span<const BeamState> beams, const Frustum& frustum, const glm::vec3& cameraPos,
                      const RenderSettings& settings, GoboAtlas& atlas, const VolumeView& volume) {
    instances_.clear();
    surfaceOnly_.clear();
    glows_.clear();
    cameraPos_ = cameraPos;
    volume_ = volume;
    coveragePixels_ = 0.0f;

    for (const BeamState& b : beams) {
        const float lumens = std::max(b.luminousFlux, 0.0f) * std::max(b.intensity, 0.0f);
        if (lumens < 0.01f || glm::dot(b.color, b.color) <= 0.0f) continue;  // dark: nothing to draw

        Frame f;
        f.dir = safeNormalize(b.direction, glm::vec3(0, -1, 0));
        f.up = perpendicularUp(f.dir, b.up);
        f.right = glm::cross(f.dir, f.up);

        const BeamProfile profile = makeProfile(b);
        const bool rect = b.shape == BeamShape::Rectangle;
        const float solidAngle = rect ? profileSolidAngleRect(profile) : profileSolidAngleRound(profile);
        const float peak = peakIntensity(b.luminousFlux, b.intensity, solidAngle);

        // Pattern data shared by all prism facets of this beam.
        BeamGpu pattern{};
        const float layer0 = static_cast<float>(atlas.layerFor(b.gobos[0].image));
        const float layer1 = static_cast<float>(atlas.layerFor(b.gobos[1].image));
        const float layer2 = static_cast<float>(atlas.layerFor(b.animationWheel.image));
        const float focus = std::clamp(b.focus, 0.0f, 1.0f);
        const float frost = std::clamp(b.frost, 0.0f, 1.0f);
        const float baseBlur = (1.0f - focus) * 0.15f + frost * 0.3f + (b.shape == BeamShape::Wash ? 0.08f : 0.0f);
        const float bladeSoftness = 0.01f + (1.0f - focus) * 0.08f + frost * 0.2f;
        pattern.goboLayers = {layer0, layer1, layer2, bladeSoftness};
        pattern.goboRotation = {b.gobos[0].rotation, b.gobos[1].rotation, b.animationWheel.rotation,
                                std::clamp(b.iris, 0.05f, 1.0f)};
        bool hasBlades = false;
        for (int i = 0; i < kMaxBlades; ++i) {
            pattern.bladeInsertion[i] = std::clamp(b.blades[static_cast<std::size_t>(i)].insertion, 0.0f, 1.0f);
            pattern.bladeAngle[i] = b.blades[static_cast<std::size_t>(i)].angle;
            hasBlades = hasBlades || pattern.bladeInsertion[i] > 0.001f;
        }
        const bool hasGobos = layer0 > 0.0f || layer1 > 0.0f || layer2 > 0.0f;
        pattern.misc = {b.bladeRotation, static_cast<float>((hasGobos ? 1 : 0) | (hasBlades ? 2 : 0)), 0.0f, 0.0f};
        pattern.apex.w = baseBlur;

        const int facets = std::min<int>(b.prism.facetCount, kMaxPrismFacets);
        if (facets == 0) {
            addInstance(b, f, profile, peak, pattern, frustum, settings);
        } else {
            // A prism splits the beam: each facet is a copy of the beam tilted by
            // its offset (rotated with the prism), carrying 1/facets of the light.
            // The whole image plane turns with the prism, so the facet frame does too.
            const float c = std::cos(b.prism.rotation), s = std::sin(b.prism.rotation);
            const glm::vec3 rotRight = c * f.right + s * f.up;
            const glm::vec3 rotUp = -s * f.right + c * f.up;
            for (int i = 0; i < facets; ++i) {
                const glm::vec2 o = b.prism.facets[static_cast<std::size_t>(i)];
                Frame ff;
                ff.dir = glm::normalize(f.dir + rotRight * std::tan(o.x) + rotUp * std::tan(o.y));
                ff.up = perpendicularUp(ff.dir, rotUp);
                ff.right = glm::cross(ff.dir, ff.up);
                addInstance(b, ff, profile, peak / static_cast<float>(facets), pattern, frustum, settings);
            }
        }

        // Lens glow: one sprite per lens, using the unsplit beam. Skip lenses
        // that face away from the camera (lens_glow.glsl shows a little stray
        // light over the whole front hemisphere) or are off screen.
        if (settings.lensGlow) {
            const glm::vec3 toCamera = cameraPos - b.position;
            const float dist = glm::length(toCamera);
            if (dist > 1e-3f) {
                const float cosAngle = glm::dot(f.dir, toCamera / dist);
                const float radius = rect ? 0.5f * glm::length(b.emitterSize) : std::max(b.lensRadius, kMinLensRadius);
                if (cosAngle > 0.0f && !sphereOutsideFrustum(frustum, b.position, radius + 0.5f)) {
                    GlowGpu g;
                    g.posRadius = {b.position, radius};
                    g.dirTanBeam = {f.dir, profile.tanBeam};
                    g.radiance = {glm::max(b.color, glm::vec3(0.0f)) * peak * kHdrPerNit, profile.exponent};
                    const float area = rect ? b.emitterSize.x * b.emitterSize.y : kPi * radius * radius;
                    const float shape = b.shape == BeamShape::Glow ? 2.0f : (rect ? 1.0f : 0.0f);
                    g.misc = {profile.tanCutoff, area, shape, 0.0f};
                    glows_.push_back(g);
                }
            }
        }
    }

    volumetricCount_ = static_cast<int>(instances_.size());
    instances_.insert(instances_.end(), surfaceOnly_.begin(), surfaceOnly_.end());

    // Cost control of the haze pass: its work is about coveragePixels x steps. If that exceeds the budget, lower
    // the cap on the steps, even below minMarchSteps (down to kBudgetMinSteps: noisier haze, but the upsample
    // blurs it). Beyond that only a lower resolution helps, which the automatic quality takes care of.
    const float minSteps = static_cast<float>(std::max(settings.minMarchSteps, 1));
    const float maxSteps = std::max(static_cast<float>(settings.maxMarchSteps), minSteps);
    marchStepCap_ = maxSteps;
    if (settings.volumetricBudget > 0.0f && coveragePixels_ > 1.0f)
        marchStepCap_ = std::clamp(settings.volumetricBudget * 1.0e6f / coveragePixels_,
                                   std::min(minSteps, kBudgetMinSteps), maxSteps);
}

void BeamPacker::addInstance(const BeamState& b, const Frame& f, const BeamProfile& profile, float peakCandela,
                             const BeamGpu& patternTemplate, const Frustum& frustum, const RenderSettings& settings) {
    const bool rect = b.shape == BeamShape::Rectangle;

    // Virtual apex behind the lens: a lens of radius r0 emitting up to the
    // field angle behaves like a point that far back (see BeamMath.h).
    float z0x, z0y, lensRadius;
    BeamHull hull;
    if (rect) {
        const glm::vec2 size = glm::max(b.emitterSize, glm::vec2(2.0f * kMinLensRadius));
        z0x = 0.5f * size.x / profile.tanField;
        z0y = 0.5f * size.y / profile.tanField;
        lensRadius = 0.5f * glm::length(size);
        // A circle around the rectangular spread: corners need sqrt(2) more angle.
        hull.tanHalfAngle = profile.tanCutoff * 1.41421356f;
        hull.apexDistance = std::max(z0x, z0y);
    } else {
        lensRadius = std::max(b.lensRadius, kMinLensRadius);
        z0x = z0y = lensRadius / profile.tanField;
        hull.tanHalfAngle = profile.tanCutoff;
        hull.apexDistance = z0x;
    }
    const float z0 = std::sqrt(z0x * z0y);

    hull.length = std::min(settings.maxBeamLength, lengthByBrightness(peakCandela, z0, settings.minIlluminance));
    if (settings.clipBeamsAtFloor) hull.length = lengthToFloor(b.position, f.dir, hull, hull.length);
    if (hull.length <= 0.01f) return;

    BeamHull cullHull = hull;
    cullHull.tanHalfAngle *= kHullPolygonScale;
    if (hullOutsideFrustum(frustum, b.position, f.dir, cullHull)) return;

    BeamGpu g = patternTemplate;
    g.posLength = {b.position, hull.length};
    g.dirHullTan = {f.dir, hull.tanHalfAngle};
    g.upHullApex = {f.up, hull.apexDistance};
    g.radiance = {glm::max(b.color, glm::vec3(0.0f)) * peakCandela * kHdrPerNit, rect ? 1.0f : 0.0f};
    g.profile = {profile.tanBeam, profile.exponent, profile.tanField, profile.tanCutoff};
    g.apex = {z0x, z0y, lensRadius, patternTemplate.apex.w};

    bool volume = b.castsVolume && b.shape != BeamShape::Glow;
    if (volume) {
        // Beams that cover less than a pixel of the volumetric target cannot be seen in the haze: skip the march.
        const float coverage =
            hullCoveragePixels(hull, b.position, f.dir, cameraPos_, volume_.focalPixels, volume_.pixels);
        volume = coverage >= kMinVolumePixels;
        if (volume) coveragePixels_ += coverage;
    }
    (volume ? instances_ : surfaceOnly_).push_back(g);
}

}  // namespace dmxviz::render
