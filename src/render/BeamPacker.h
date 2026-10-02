#pragma once
// Converts the frame's BeamStates into the arrays the beam passes draw from:
//
//   * BeamGpu instances (one per beam, or one per prism facet with the flux
//     split between facets), culled against the view frustum and with zero
//     output beams dropped. Instances that cast a haze volume come first, so
//     the volumetric pass draws [0, volumetricCount) and the surface lighting
//     pass draws everything - one draw call each.
//   * GlowGpu sprites, one per visible lens.
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

class BeamPacker {
public:
    void pack(std::span<const BeamState> beams, const beammath::Frustum& frustum, const glm::vec3& cameraPos,
              const RenderSettings& settings, GoboAtlas& atlas);

    // [0, volumetricCount()) cast haze, the rest only light surfaces.
    const std::vector<BeamGpu>& instances() const { return instances_; }
    int volumetricCount() const { return volumetricCount_; }
    const std::vector<GlowGpu>& glows() const { return glows_; }

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
};

}  // namespace dmxviz::render
