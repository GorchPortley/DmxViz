#pragma once
// Converts the frame's BeamStates into the arrays the beam passes draw from:
//
//   * BeamGpu instances (one per beam, or one per prism facet with the flux
//     split between facets), culled against the view frustum and with zero
//     output beams dropped. Instances that cast a haze volume come first, so
//     the volumetric pass draws [0, volumetricCount) and the surface lighting
//     pass draws everything - one draw call each.
//   * GlowGpu sprites, one per visible lens.
//   * A cost estimate of the haze pass (hull coverage x march steps) and from it the march step cap that keeps
//     the pass inside RenderSettings::volumetricBudget. Beams smaller than a pixel only light surfaces.
//
// The output vectors are reused every frame (no allocation in steady state).

#include "core/SceneTypes.h"
#include "render/BeamMath.h"
#include "render/GpuTypes.h"
#include "render/RenderSettings.h"

#include <span>
#include <vector>

namespace dmxviz::render {

class GoboAtlas;

// The volumetric target as seen by the packer, to estimate how many of its pixels a beam covers.
struct VolumeView {
    float focalPixels = 1000.0f;  // camera focal length in pixels of the volumetric target
    float pixels = 1.0e6f;        // pixel count of the volumetric target
};

class BeamPacker {
public:
    void pack(std::span<const BeamState> beams, const beammath::Frustum& frustum, const glm::vec3& cameraPos,
              const RenderSettings& settings, GoboAtlas& atlas, const VolumeView& volume);

    // [0, volumetricCount()) cast haze, the rest only light surfaces.
    const std::vector<BeamGpu>& instances() const { return instances_; }
    int volumetricCount() const { return volumetricCount_; }
    const std::vector<GlowGpu>& glows() const { return glows_; }

    // Upper bound for the march steps of a pixel this frame: RenderSettings::maxMarchSteps, lowered towards
    // minMarchSteps when the estimated haze cost exceeds the budget.
    float marchStepCap() const { return marchStepCap_; }
    // Estimated pixels (of the volumetric target) covered by all haze beams, counting overdraw.
    float volumetricCoveragePixels() const { return coveragePixels_; }

private:
    struct Frame {  // orthonormal beam frame
        glm::vec3 dir, up, right;
    };

    void addInstance(const BeamState& b, const Frame& f, const beammath::BeamProfile& profile, float peakCandela,
                     const BeamGpu& patternTemplate, const beammath::Frustum& frustum, const RenderSettings& settings);

    std::vector<BeamGpu> instances_;
    std::vector<BeamGpu> surfaceOnly_;
    std::vector<GlowGpu> glows_;
    int volumetricCount_ = 0;
    // Per pack() call: the camera and target the coverage estimate refers to.
    glm::vec3 cameraPos_{0.0f};
    VolumeView volume_;
    float coveragePixels_ = 0.0f;
    float marchStepCap_ = 0.0f;
};

}  // namespace dmxviz::render
