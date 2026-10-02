#include "render/PostPasses.h"

#include "render/GpuTypes.h"
#include "render/ShaderProgram.h"

namespace dmxviz::render {
namespace {

enum CompositeViews { kCompFrame = 0, kCompHdr = 1, kCompBloom = 2, kCompNormal = 3 };
enum GridViews { kGridFrame = 0, kGridDistance = 1 };
enum LineViews { kLineFrame = 0 };

// Uniform block with a single vec4 (bloom, composite, lines).
struct Vec4Params {
    glm::vec4 params{0.0f};
};

}  // namespace

bool PostPasses::init() {
    bloomDownShader_ = ShaderProgram("bloom_down.glsl")
                           .uniforms(0, sizeof(Vec4Params), {{"u_params", SG_UNIFORMTYPE_FLOAT4}})
                           .texture(0, 0, "u_source")
                           .build();
    sg_pipeline_desc dd = fullscreenPipeline(bloomDownShader_, kBloomFormat, "bloom-down");
    bloomDownPipeline_ = sg_make_pipeline(&dd);

    bloomUpShader_ = ShaderProgram("bloom_up.glsl")
                         .uniforms(0, sizeof(Vec4Params), {{"u_params", SG_UNIFORMTYPE_FLOAT4}})
                         .texture(0, 0, "u_source")
                         .build();
    sg_pipeline_desc ud = fullscreenPipeline(bloomUpShader_, kBloomFormat, "bloom-up");
    ud.colors[0].blend = additiveBlend();
    bloomUpPipeline_ = sg_make_pipeline(&ud);

    // The composite pass has the scene depth attached (for the debug lines), so
    // every pipeline in it declares the depth format, even if it ignores depth.
    compositeShader_ = ShaderProgram("composite.glsl")
                           .uniforms(0, sizeof(Vec4Params), {{"u_params", SG_UNIFORMTYPE_FLOAT4}})
                           .storageBuffer(kCompFrame, 0)
                           .texture(kCompHdr, 0, "u_hdr", SG_IMAGETYPE_2D, SG_IMAGESAMPLETYPE_FLOAT,
                                    SG_SAMPLERTYPE_NONFILTERING)
                           .texture(kCompBloom, 1, "u_bloom")
                           .texture(kCompNormal, 2, "u_gNormal", SG_IMAGETYPE_2D, SG_IMAGESAMPLETYPE_FLOAT,
                                    SG_SAMPLERTYPE_NONFILTERING)
                           .build();
    sg_pipeline_desc cd = fullscreenPipeline(compositeShader_, kLdrFormat, "composite");
    cd.depth.pixel_format = kDepthFormat;
    compositePipeline_ = sg_make_pipeline(&cd);

    gridShader_ = ShaderProgram("grid.glsl")
                      .storageBuffer(kGridFrame, 0)
                      .unfilterableTexture(kGridDistance, 0, "u_gDistance")
                      .build();
    sg_pipeline_desc gd = fullscreenPipeline(gridShader_, kLdrFormat, "grid");
    gd.depth.pixel_format = kDepthFormat;
    gd.colors[0].blend = alphaBlend();
    gridPipeline_ = sg_make_pipeline(&gd);

    linesShader_ = ShaderProgram("lines.glsl")
                       .uniforms(0, sizeof(Vec4Params), {{"u_params", SG_UNIFORMTYPE_FLOAT4}})
                       .storageBuffer(kLineFrame, 0)
                       .build();
    sg_pipeline_desc ld{};
    ld.shader = linesShader_;
    ld.layout.attrs[0].format = SG_VERTEXFORMAT_FLOAT4;
    ld.layout.attrs[1].format = SG_VERTEXFORMAT_FLOAT4;
    ld.primitive_type = SG_PRIMITIVETYPE_LINES;
    ld.depth.pixel_format = kDepthFormat;
    ld.depth.compare = SG_COMPAREFUNC_LESS_EQUAL;
    ld.colors[0].pixel_format = kLdrFormat;
    ld.colors[0].blend = alphaBlend();
    ld.sample_count = 1;
    ld.label = "lines-visible";
    linesVisiblePipeline_ = sg_make_pipeline(&ld);
    ld.depth.compare = SG_COMPAREFUNC_GREATER;
    ld.label = "lines-hidden";
    linesHiddenPipeline_ = sg_make_pipeline(&ld);

    for (sg_pipeline p : {bloomDownPipeline_, bloomUpPipeline_, compositePipeline_, gridPipeline_,
                          linesVisiblePipeline_, linesHiddenPipeline_})
        if (sg_query_pipeline_state(p) != SG_RESOURCESTATE_VALID) return false;
    return true;
}

void PostPasses::shutdown() {
    for (sg_pipeline p : {bloomDownPipeline_, bloomUpPipeline_, compositePipeline_, gridPipeline_,
                          linesVisiblePipeline_, linesHiddenPipeline_})
        if (p.id) sg_destroy_pipeline(p);
    for (sg_shader s : {bloomDownShader_, bloomUpShader_, compositeShader_, gridShader_, linesShader_})
        if (s.id) sg_destroy_shader(s);
    bloomDownPipeline_ = bloomUpPipeline_ = compositePipeline_ = gridPipeline_ = {};
    linesVisiblePipeline_ = linesHiddenPipeline_ = {};
    bloomDownShader_ = bloomUpShader_ = compositeShader_ = gridShader_ = linesShader_ = {};
}

void PostPasses::drawBloom(PassContext& ctx) {
    ViewportTarget::Impl& v = ctx.view;

    // Down: HDR -> level 0 -> level 1 ... -> level 5.
    for (int level = 0; level < kBloomLevels; ++level) {
        const RenderTarget& src = level == 0 ? v.hdr : v.bloom[static_cast<std::size_t>(level - 1)];
        const RenderTarget& dst = v.bloom[static_cast<std::size_t>(level)];
        beginColorPass(dst, SG_LOADACTION_DONTCARE, "bloom-down");
        sg_apply_pipeline(bloomDownPipeline_);
        sg_bindings bind{};
        bind.views[0] = src.texture;
        bind.samplers[0] = ctx.samplers.linearClamp;
        sg_apply_bindings(&bind);
        const Vec4Params p{{1.0f / src.width, 1.0f / src.height, level == 0 ? 1.0f : 0.0f, 0.0f}};
        sg_apply_uniforms(0, SG_RANGE_REF(p));
        sg_draw(0, 3, 1);
        sg_end_pass();
    }
    // Up: level 5 is blurred into level 4 (added), then 4 into 3, ... into 0.
    for (int level = kBloomLevels - 2; level >= 0; --level) {
        const RenderTarget& src = v.bloom[static_cast<std::size_t>(level + 1)];
        const RenderTarget& dst = v.bloom[static_cast<std::size_t>(level)];
        beginColorPass(dst, SG_LOADACTION_LOAD, "bloom-up");
        sg_apply_pipeline(bloomUpPipeline_);
        sg_bindings bind{};
        bind.views[0] = src.texture;
        bind.samplers[0] = ctx.samplers.linearClamp;
        sg_apply_bindings(&bind);
        const Vec4Params p{{1.0f / src.width, 1.0f / src.height, 1.0f / dst.width, 1.0f / dst.height}};
        sg_apply_uniforms(0, SG_RANGE_REF(p));
        sg_draw(0, 3, 1);
        sg_end_pass();
    }
    ctx.stats.drawCalls += 2 * kBloomLevels - 1;
}

void PostPasses::drawComposite(PassContext& ctx, int lineVertexCount) {
    ViewportTarget::Impl& v = ctx.view;
    sg_pass pass{};
    pass.action.colors[0].load_action = SG_LOADACTION_DONTCARE;
    pass.action.colors[0].store_action = SG_STOREACTION_STORE;
    pass.action.depth = {SG_LOADACTION_LOAD, SG_STOREACTION_DONTCARE, 1.0f};
    pass.attachments.colors[0] = v.ldr.attachment;
    pass.attachments.depth_stencil = v.depth.attachment;
    pass.label = "composite";
    sg_begin_pass(&pass);

    sg_apply_pipeline(compositePipeline_);
    {
        sg_bindings bind{};
        bind.views[kCompFrame] = v.frameConstants.storageView();
        bind.views[kCompHdr] = v.hdr.texture;
        bind.views[kCompBloom] = v.bloom[0].texture;
        bind.views[kCompNormal] = v.gNormal.texture;
        bind.samplers[0] = ctx.samplers.nearestClamp;
        bind.samplers[1] = ctx.samplers.linearClamp;
        bind.samplers[2] = ctx.samplers.nearestClamp;
        sg_apply_bindings(&bind);
        const float tonemapper = ctx.settings.tonemapper == Tonemapper::AcesPerChannel ? 1.0f : 0.0f;
        const Vec4Params p{{ctx.settings.bloom ? 1.0f : 0.0f, tonemapper, 0.0f, 0.0f}};
        sg_apply_uniforms(0, SG_RANGE_REF(p));
        sg_draw(0, 3, 1);
        ++ctx.stats.drawCalls;
    }

    if (ctx.scene.environment.showGrid) {
        sg_apply_pipeline(gridPipeline_);
        sg_bindings bind{};
        bind.views[kGridFrame] = v.frameConstants.storageView();
        bind.views[kGridDistance] = v.gDistance.texture;
        bind.samplers[0] = ctx.samplers.nearestClamp;
        sg_apply_bindings(&bind);
        sg_draw(0, 3, 1);
        ++ctx.stats.drawCalls;
    }

    if (lineVertexCount > 0) {
        // Hidden parts first (faint), then the visible parts on top.
        const float alpha[2] = {0.25f, 1.0f};
        const sg_pipeline pipelines[2] = {linesHiddenPipeline_, linesVisiblePipeline_};
        for (int i = 0; i < 2; ++i) {
            sg_apply_pipeline(pipelines[i]);
            sg_bindings bind{};
            bind.vertex_buffers[0] = v.lines.buffer();
            bind.views[kLineFrame] = v.frameConstants.storageView();
            sg_apply_bindings(&bind);
            const Vec4Params p{{alpha[i], 0.0f, 0.0f, 0.0f}};
            sg_apply_uniforms(0, SG_RANGE_REF(p));
            sg_draw(0, lineVertexCount, 1);
            ++ctx.stats.drawCalls;
        }
    }
    sg_end_pass();
}

}  // namespace dmxviz::render
