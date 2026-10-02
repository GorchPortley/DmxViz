#include "render/BeamPasses.h"

#include "render/ShaderProgram.h"

namespace dmxviz::render {
namespace {

enum DownsampleViews { kDsFrame = 0, kDsDistance = 1 };
enum VolumeViews { kVolFrame = 0, kVolBeams = 1, kVolGobos = 2, kVolHalfDepth = 3, kVolNoise = 4 };
enum UpsampleViews { kUpFrame = 0, kUpVolume = 1, kUpHalfDepth = 2, kUpDistance = 3 };
enum GlowViews { kGlowFrame = 0, kGlowSprites = 1, kGlowDistance = 2 };

}  // namespace

bool BeamPasses::init() {
    downsampleShader_ = ShaderProgram("depth_downsample.glsl")
                            .storageBuffer(kDsFrame, 0)
                            .unfilterableTexture(kDsDistance, 0, "u_gDistance")
                            .build();
    sg_pipeline_desc dp = fullscreenPipeline(downsampleShader_, kHalfDepthFormat, "depth-downsample");
    downsamplePipeline_ = sg_make_pipeline(&dp);

    volumeShader_ = ShaderProgram("volumetric.glsl")
                        .storageBuffer(kVolFrame, 0)
                        .storageBuffer(kVolBeams, 1)
                        .texture(kVolGobos, 0, "u_gobos", SG_IMAGETYPE_ARRAY)
                        .unfilterableTexture(kVolHalfDepth, 1, "u_halfDepth")
                        .texture(kVolNoise, 2, "u_noise", SG_IMAGETYPE_3D)
                        .build();
    sg_pipeline_desc vp{};
    vp.shader = volumeShader_;
    vp.layout.attrs[0].format = SG_VERTEXFORMAT_FLOAT3;
    vp.index_type = SG_INDEXTYPE_UINT16;
    vp.face_winding = SG_FACEWINDING_CW;  // mirrored image, see SurfacePasses
    vp.cull_mode = SG_CULLMODE_FRONT;     // back faces: one fragment per pixel and beam
    vp.depth.pixel_format = SG_PIXELFORMAT_NONE;
    vp.colors[0].pixel_format = kVolumeFormat;
    vp.colors[0].blend = additiveBlend();
    vp.sample_count = 1;
    vp.label = "volumetric";
    volumePipeline_ = sg_make_pipeline(&vp);

    upsampleShader_ = ShaderProgram("upsample.glsl")
                          .storageBuffer(kUpFrame, 0)
                          .texture(kUpVolume, 0, "u_volume", SG_IMAGETYPE_2D, SG_IMAGESAMPLETYPE_FLOAT,
                                   SG_SAMPLERTYPE_NONFILTERING)
                          .unfilterableTexture(kUpHalfDepth, 1, "u_halfDepth")
                          .unfilterableTexture(kUpDistance, 2, "u_gDistance")
                          .build();
    sg_pipeline_desc up = fullscreenPipeline(upsampleShader_, kHdrFormat, "volumetric-upsample");
    up.colors[0].blend = additiveBlend();
    upsamplePipeline_ = sg_make_pipeline(&up);

    glowShader_ = ShaderProgram("lens_glow.glsl")
                      .storageBuffer(kGlowFrame, 0)
                      .storageBuffer(kGlowSprites, 2)
                      .unfilterableTexture(kGlowDistance, 0, "u_gDistance")
                      .inVertexStage()
                      .build();
    sg_pipeline_desc gp = fullscreenPipeline(glowShader_, kHdrFormat, "lens-glow");
    gp.colors[0].blend = additiveBlend();
    glowPipeline_ = sg_make_pipeline(&gp);

    for (sg_pipeline p : {downsamplePipeline_, volumePipeline_, upsamplePipeline_, glowPipeline_})
        if (sg_query_pipeline_state(p) != SG_RESOURCESTATE_VALID) return false;
    return true;
}

void BeamPasses::shutdown() {
    for (sg_pipeline p : {downsamplePipeline_, volumePipeline_, upsamplePipeline_, glowPipeline_})
        if (p.id) sg_destroy_pipeline(p);
    for (sg_shader s : {downsampleShader_, volumeShader_, upsampleShader_, glowShader_})
        if (s.id) sg_destroy_shader(s);
    downsamplePipeline_ = volumePipeline_ = upsamplePipeline_ = glowPipeline_ = {};
    downsampleShader_ = volumeShader_ = upsampleShader_ = glowShader_ = {};
}

void BeamPasses::drawVolumetrics(PassContext& ctx, const HullMesh& hull, int volumetricBeams, sg_view goboAtlas,
                                 sg_view noise) {
    if (volumetricBeams <= 0) return;
    ViewportTarget::Impl& v = ctx.view;

    // 3a. Half-res min/max distance.
    beginColorPass(v.halfDepth, SG_LOADACTION_DONTCARE, "depth-downsample");
    sg_apply_pipeline(downsamplePipeline_);
    {
        sg_bindings bind{};
        bind.views[kDsFrame] = v.frameConstants.storageView();
        bind.views[kDsDistance] = v.gDistance.texture;
        bind.samplers[0] = ctx.samplers.nearestClamp;
        sg_apply_bindings(&bind);
    }
    sg_draw(0, 3, 1);
    sg_end_pass();

    // 3b. Ray-march every haze-casting beam into the half-res volume target.
    beginColorPass(v.volume, SG_LOADACTION_CLEAR, "volumetric");
    sg_apply_pipeline(volumePipeline_);
    {
        sg_bindings bind{};
        bind.vertex_buffers[0] = hull.vertices;
        bind.index_buffer = hull.indices;
        bind.views[kVolFrame] = v.frameConstants.storageView();
        bind.views[kVolBeams] = v.beams.storageView();
        bind.views[kVolGobos] = goboAtlas;
        bind.views[kVolHalfDepth] = v.halfDepth.texture;
        bind.views[kVolNoise] = noise;
        bind.samplers[0] = ctx.samplers.gobo;
        bind.samplers[1] = ctx.samplers.nearestClamp;
        bind.samplers[2] = ctx.samplers.noise;
        sg_apply_bindings(&bind);
    }
    sg_draw(0, hull.indexCount, volumetricBeams);
    sg_end_pass();

    // 3c. Depth-aware upsample, added to the HDR image.
    beginColorPass(v.hdr, SG_LOADACTION_LOAD, "volumetric-upsample");
    sg_apply_pipeline(upsamplePipeline_);
    {
        sg_bindings bind{};
        bind.views[kUpFrame] = v.frameConstants.storageView();
        bind.views[kUpVolume] = v.volume.texture;
        bind.views[kUpHalfDepth] = v.halfDepth.texture;
        bind.views[kUpDistance] = v.gDistance.texture;
        bind.samplers[0] = ctx.samplers.nearestClamp;
        bind.samplers[1] = ctx.samplers.nearestClamp;
        bind.samplers[2] = ctx.samplers.nearestClamp;
        sg_apply_bindings(&bind);
    }
    sg_draw(0, 3, 1);
    sg_end_pass();
    ctx.stats.drawCalls += 3;
}

void BeamPasses::drawLensGlow(PassContext& ctx, int glowCount) {
    if (glowCount <= 0) return;
    ViewportTarget::Impl& v = ctx.view;
    beginColorPass(v.hdr, SG_LOADACTION_LOAD, "lens-glow");
    sg_apply_pipeline(glowPipeline_);
    sg_bindings bind{};
    bind.views[kGlowFrame] = v.frameConstants.storageView();
    bind.views[kGlowSprites] = v.glows.storageView();
    bind.views[kGlowDistance] = v.gDistance.texture;
    bind.samplers[0] = ctx.samplers.nearestClamp;
    sg_apply_bindings(&bind);
    sg_draw(0, 6, glowCount);
    sg_end_pass();
    ++ctx.stats.drawCalls;
}

}  // namespace dmxviz::render
