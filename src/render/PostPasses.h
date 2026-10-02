#pragma once
// Image-space passes after lighting:
//   5. bloom: 6-level dual filter (13-tap down, tent up)   bloom_down.glsl / bloom_up.glsl
//   6. composite into the 8-bit target: tonemap, outline   composite.glsl
//      + floor grid and debug lines on top                 grid.glsl / lines.glsl

#include "render/PassContext.h"

namespace dmxviz::render {

class PostPasses {
public:
    bool init();
    void shutdown();

    void drawBloom(PassContext& ctx);
    // lineVertexCount: vertices uploaded to the view's line buffer (2 per line).
    void drawComposite(PassContext& ctx, int lineVertexCount);

private:
    sg_shader bloomDownShader_{}, bloomUpShader_{}, compositeShader_{}, gridShader_{}, linesShader_{};
    sg_pipeline bloomDownPipeline_{}, bloomUpPipeline_{}, compositePipeline_{}, gridPipeline_{};
    sg_pipeline linesVisiblePipeline_{}, linesHiddenPipeline_{};
};

}  // namespace dmxviz::render
