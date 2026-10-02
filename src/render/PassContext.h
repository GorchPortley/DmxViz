#pragma once
// What every render pass needs for one viewport and frame.

#include "render/GpuResources.h"
#include "render/RenderScene.h"
#include "render/RenderSettings.h"
#include "render/ViewportTargetImpl.h"

namespace dmxviz::render {

struct PassContext {
    ViewportTarget::Impl& view;
    const RenderScene& scene;
    const RenderSettings& settings;
    const Samplers& samplers;
    RenderStats& stats;
};

// Pipeline helpers shared by the pass classes.
inline sg_blend_state additiveBlend() {
    sg_blend_state b{};
    b.enabled = true;
    b.src_factor_rgb = SG_BLENDFACTOR_ONE;
    b.dst_factor_rgb = SG_BLENDFACTOR_ONE;
    b.src_factor_alpha = SG_BLENDFACTOR_ONE;
    b.dst_factor_alpha = SG_BLENDFACTOR_ONE;
    return b;
}

inline sg_blend_state alphaBlend() {
    sg_blend_state b{};
    b.enabled = true;
    b.src_factor_rgb = SG_BLENDFACTOR_SRC_ALPHA;
    b.dst_factor_rgb = SG_BLENDFACTOR_ONE_MINUS_SRC_ALPHA;
    b.src_factor_alpha = SG_BLENDFACTOR_ONE;
    b.dst_factor_alpha = SG_BLENDFACTOR_ONE_MINUS_SRC_ALPHA;
    return b;
}

// A pipeline for a full-screen triangle (no vertex buffers) into one colour target.
inline sg_pipeline_desc fullscreenPipeline(sg_shader shader, sg_pixel_format color, const char* label) {
    sg_pipeline_desc d{};
    d.shader = shader;
    d.colors[0].pixel_format = color;
    d.depth.pixel_format = SG_PIXELFORMAT_NONE;
    d.sample_count = 1;
    d.label = label;
    return d;
}

// Starts an offscreen pass into a single colour target without depth.
inline void beginColorPass(const RenderTarget& target, sg_load_action load, const char* label,
                           sg_color clear = {0.0f, 0.0f, 0.0f, 0.0f}) {
    sg_pass pass{};
    pass.action.colors[0].load_action = load;
    pass.action.colors[0].store_action = SG_STOREACTION_STORE;
    pass.action.colors[0].clear_value = clear;
    pass.attachments.colors[0] = target.attachment;
    pass.label = label;
    sg_begin_pass(&pass);
}

}  // namespace dmxviz::render
