#pragma once
// Passes that draw the light in the air:
//   3a. reduced-res depth (min/max of each block)        depth_downsample.glsl
//   3b. volumetric beams, half/quarter res, one draw     volumetric.glsl
//   3c. depth-aware upsample into the HDR target          upsample.glsl
//   4.  lens glow sprites, one instanced draw              lens_glow.glsl

#include "render/HullMesh.h"
#include "render/PassContext.h"

namespace dmxviz::render {

class BeamPasses {
public:
    bool init();
    void shutdown();

    // 3a-3c. Does nothing when there are no haze-casting beams.
    void drawVolumetrics(PassContext& ctx, const HullMesh& hull, int volumetricBeams, sg_view goboAtlas,
                         sg_view noise);
    // 4.
    void drawLensGlow(PassContext& ctx, int glowCount);

private:
    sg_shader downsampleShader_{}, volumeShader_{}, upsampleShader_{}, glowShader_{};
    sg_pipeline downsamplePipeline_{}, volumePipeline_{}, upsamplePipeline_{}, glowPipeline_{};
};

}  // namespace dmxviz::render
