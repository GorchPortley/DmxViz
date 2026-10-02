#include "render/SurfacePasses.h"

#include "assets/MeshData.h"
#include "render/GpuTypes.h"
#include "render/ShaderProgram.h"

#include <cstddef>
namespace dmxviz::render {
namespace {

// sg_bindings.views[] slots (must match the ShaderProgram declarations below).
enum GBufferViews { kGbFrame = 0 };
enum SpotViews { kSpotFrame = 0, kSpotBeams = 1, kSpotGobos = 2, kSpotAlbedo = 3, kSpotNormal = 4, kSpotDistance = 5 };

}  // namespace

bool SurfacePasses::init() {
    gbufferShader_ = ShaderProgram("gbuffer.glsl").storageBuffer(kGbFrame, 0).build();

    sg_pipeline_desc gp{};
    gp.shader = gbufferShader_;
    // Buffer 0: mesh vertices (assets::Vertex). Buffer 1: one InstanceGpu per instance.
    gp.layout.buffers[0].stride = sizeof(assets::Vertex);
    gp.layout.buffers[1].stride = sizeof(InstanceGpu);
    gp.layout.buffers[1].step_func = SG_VERTEXSTEP_PER_INSTANCE;
    gp.layout.attrs[0] = {0, static_cast<int>(offsetof(assets::Vertex, position)), SG_VERTEXFORMAT_FLOAT3};
    gp.layout.attrs[1] = {0, static_cast<int>(offsetof(assets::Vertex, normal)), SG_VERTEXFORMAT_FLOAT3};
    gp.layout.attrs[2] = {0, static_cast<int>(offsetof(assets::Vertex, uv)), SG_VERTEXFORMAT_FLOAT2};
    for (int i = 0; i < 7; ++i) gp.layout.attrs[3 + i] = {1, i * 16, SG_VERTEXFORMAT_FLOAT4};
    gp.index_type = SG_INDEXTYPE_UINT32;
    gp.cull_mode = SG_CULLMODE_NONE;  // see gbuffer.glsl: two-sided shading instead
    gp.depth.pixel_format = kDepthFormat;
    gp.depth.compare = SG_COMPAREFUNC_LESS_EQUAL;
    gp.depth.write_enabled = true;
    gp.color_count = 4;
    gp.colors[0].pixel_format = kAlbedoFormat;
    gp.colors[1].pixel_format = kNormalFormat;
    gp.colors[2].pixel_format = kDistanceFormat;
    gp.colors[3].pixel_format = kHdrFormat;
    gp.sample_count = 1;
    gp.label = "gbuffer";
    gbufferPipeline_ = sg_make_pipeline(&gp);

    spotShader_ = ShaderProgram("spot_light.glsl")
                      .storageBuffer(kSpotFrame, 0)
                      .storageBuffer(kSpotBeams, 1)
                      .texture(kSpotGobos, 0, "u_gobos", SG_IMAGETYPE_ARRAY)
                      .texture(kSpotAlbedo, 1, "u_gAlbedo", SG_IMAGETYPE_2D, SG_IMAGESAMPLETYPE_FLOAT,
                               SG_SAMPLERTYPE_NONFILTERING)
                      .texture(kSpotNormal, 2, "u_gNormal", SG_IMAGETYPE_2D, SG_IMAGESAMPLETYPE_FLOAT,
                               SG_SAMPLERTYPE_NONFILTERING)
                      .unfilterableTexture(kSpotDistance, 3, "u_gDistance")
                      .build();

    sg_pipeline_desc sp{};
    sp.shader = spotShader_;
    sp.layout.attrs[0].format = SG_VERTEXFORMAT_FLOAT3;
    sp.index_type = SG_INDEXTYPE_UINT16;
    // The image is rendered upside down (see Renderer.cpp), which mirrors the
    // winding: our counter-clockwise front faces arrive clockwise.
    sp.face_winding = SG_FACEWINDING_CW;
    sp.cull_mode = SG_CULLMODE_FRONT;  // keep the far side of each hull
    sp.depth.pixel_format = kDepthFormat;
    sp.depth.compare = SG_COMPAREFUNC_GREATER_EQUAL;  // surface in front of the hull's far side
    sp.depth.write_enabled = false;
    sp.colors[0].pixel_format = kHdrFormat;
    sp.colors[0].blend = additiveBlend();
    sp.sample_count = 1;
    sp.label = "spot-light";
    spotPipeline_ = sg_make_pipeline(&sp);

    return sg_query_pipeline_state(gbufferPipeline_) == SG_RESOURCESTATE_VALID &&
           sg_query_pipeline_state(spotPipeline_) == SG_RESOURCESTATE_VALID;
}

void SurfacePasses::shutdown() {
    for (sg_pipeline p : {gbufferPipeline_, spotPipeline_})
        if (p.id) sg_destroy_pipeline(p);
    for (sg_shader s : {gbufferShader_, spotShader_})
        if (s.id) sg_destroy_shader(s);
    gbufferPipeline_ = spotPipeline_ = {};
    gbufferShader_ = spotShader_ = {};
}

void SurfacePasses::drawGBuffer(PassContext& ctx, const MeshCache& meshes, std::span<const MeshBatch> batches) {
    ViewportTarget::Impl& v = ctx.view;
    const glm::vec3 bg = ctx.scene.environment.background;

    sg_pass pass{};
    pass.action.colors[0] = {SG_LOADACTION_CLEAR, SG_STOREACTION_STORE, {0.0f, 0.0f, 0.0f, 0.0f}};
    pass.action.colors[1] = {SG_LOADACTION_CLEAR, SG_STOREACTION_STORE, {0.0f, 0.0f, 0.0f, 0.0f}};
    pass.action.colors[2] = {SG_LOADACTION_CLEAR, SG_STOREACTION_STORE, {1e9f, 0.0f, 0.0f, 0.0f}};  // "sky"
    pass.action.colors[3] = {SG_LOADACTION_CLEAR, SG_STOREACTION_STORE, {bg.r, bg.g, bg.b, 1.0f}};
    pass.action.depth = {SG_LOADACTION_CLEAR, SG_STOREACTION_STORE, 1.0f};
    pass.attachments.colors[0] = v.gAlbedo.attachment;
    pass.attachments.colors[1] = v.gNormal.attachment;
    pass.attachments.colors[2] = v.gDistance.attachment;
    pass.attachments.colors[3] = v.hdr.attachment;
    pass.attachments.depth_stencil = v.depth.attachment;
    pass.label = "gbuffer";
    sg_begin_pass(&pass);
    if (!batches.empty() && meshes.vertexBuffer().id) {
        sg_apply_pipeline(gbufferPipeline_);
        for (const MeshBatch& b : batches) {
            sg_bindings bind{};
            bind.vertex_buffers[0] = meshes.vertexBuffer();
            bind.vertex_buffer_offsets[0] = b.mesh->vertexOffsetBytes;
            bind.vertex_buffers[1] = v.instances.buffer();
            bind.vertex_buffer_offsets[1] = b.firstInstance * static_cast<int>(sizeof(InstanceGpu));
            bind.index_buffer = meshes.indexBuffer();
            bind.index_buffer_offset = b.mesh->indexOffsetBytes;
            bind.views[kGbFrame] = v.frameConstants.storageView();
            sg_apply_bindings(&bind);
            sg_draw(0, b.mesh->indexCount, b.instanceCount);
            ++ctx.stats.drawCalls;
        }
    }
    sg_end_pass();
}

void SurfacePasses::drawSpotLights(PassContext& ctx, const HullMesh& hull, int beamCount, sg_view goboAtlas) {
    if (beamCount <= 0) return;
    ViewportTarget::Impl& v = ctx.view;
    sg_pass pass{};
    pass.action.colors[0].load_action = SG_LOADACTION_LOAD;
    pass.action.colors[0].store_action = SG_STOREACTION_STORE;
    pass.action.depth = {SG_LOADACTION_LOAD, SG_STOREACTION_STORE, 1.0f};
    pass.attachments.colors[0] = v.hdr.attachment;
    pass.attachments.depth_stencil = v.depth.attachment;
    pass.label = "spot-light";
    sg_begin_pass(&pass);
    sg_apply_pipeline(spotPipeline_);
    sg_bindings bind{};
    bind.vertex_buffers[0] = hull.vertices;
    bind.index_buffer = hull.indices;
    bind.views[kSpotFrame] = v.frameConstants.storageView();
    bind.views[kSpotBeams] = v.beams.storageView();
    bind.views[kSpotGobos] = goboAtlas;
    bind.views[kSpotAlbedo] = v.gAlbedo.texture;
    bind.views[kSpotNormal] = v.gNormal.texture;
    bind.views[kSpotDistance] = v.gDistance.texture;
    bind.samplers[0] = ctx.samplers.gobo;
    bind.samplers[1] = ctx.samplers.nearestClamp;
    bind.samplers[2] = ctx.samplers.nearestClamp;
    bind.samplers[3] = ctx.samplers.nearestClamp;
    sg_apply_bindings(&bind);
    sg_draw(0, hull.indexCount, beamCount);
    ++ctx.stats.drawCalls;
    sg_end_pass();
}

}  // namespace dmxviz::render
