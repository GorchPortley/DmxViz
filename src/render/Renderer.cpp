#include "render/Renderer.h"

#include "assets/AssetLibrary.h"
#include "core/Log.h"
#include "render/BeamMath.h"
#include "render/BeamPacker.h"
#include "render/BeamPasses.h"
#include "render/GoboAtlas.h"
#include "render/GpuTypes.h"
#include "render/HullMesh.h"
#include "render/MeshCache.h"
#include "render/NoiseVolume.h"
#include "render/PostPasses.h"
#include "render/SurfacePasses.h"
#include "render/ViewportTargetImpl.h"

#include "sokol_app.h"
#include "sokol_gfx.h"
#include "imgui.h"
#include "sokol_imgui.h"

#include <algorithm>
#include <cmath>
#include <vector>

// Frame structure (docs/ARCHITECTURE.md section 7, details in src/render/README.md):
//
//   prepare   cull + sort mesh instances, pack beams (prism facets, culling),
//             upload per-view buffers, bake new gobos
//   1 gbuffer        full res   meshes -> albedo, normal, distance, HDR (ambient + emissive)
//   2 spot lights    full res   all beams light the surfaces           (1 draw)
//   3 volumetrics    half/quarter res   depth min/max, haze ray-march, upsample  (1 draw for all beams)
//   4 lens glow      full res   one sprite per lens                     (1 draw)
//   5 bloom          6 levels   13-tap down / tent up
//   6 composite      full res   tonemap, outline, grid, debug lines -> RGBA8
//
// Image orientation: OpenGL stores render targets bottom row first, but ImGui
// (and every image viewer) expects the top row first. Instead of flipping at
// the end, the projection is flipped vertically (clip y = -y) for every 3D
// pass, so the whole frame is rendered upside down in memory, which is the
// right way up for ImGui::Image. The flip mirrors triangle winding, hence the
// SG_FACEWINDING_CW in the beam pipelines.

namespace dmxviz::render {

// ---------------------------------------------------------------------------
// ViewportTarget
// ---------------------------------------------------------------------------

void ViewportTarget::Impl::createTargets(int w, int h) {
    destroyTargets();
    width = w;
    height = h;
    gAlbedo.create(w, h, kAlbedoFormat, "gbuffer-albedo");
    gNormal.create(w, h, kNormalFormat, "gbuffer-normal");
    gDistance.create(w, h, kDistanceFormat, "gbuffer-distance");
    hdr.create(w, h, kHdrFormat, "hdr");
    depth.create(w, h, "depth");
    ldr.create(w, h, kLdrFormat, "viewport-color");
    createVolumeTargets();
    // The bloom chain always starts at half res, whatever the haze resolution is.
    int bw = (w + 1) / 2, bh = (h + 1) / 2;
    for (RenderTarget& level : bloom) {
        level.create(bw, bh, kBloomFormat, "bloom");
        bw = std::max(bw / 2, 1);
        bh = std::max(bh / 2, 1);
    }
}

void ViewportTarget::Impl::createVolumeTargets() {
    const int vw = (width + volumeDivisor - 1) / volumeDivisor, vh = (height + volumeDivisor - 1) / volumeDivisor;
    volumeDepth.create(vw, vh, kVolumeDepthFormat, "volume-depth");
    volume.create(vw, vh, kVolumeFormat, "volumetric");
}

void ViewportTarget::Impl::setVolumeDivisor(int divisor) {
    divisor = divisor >= 4 ? 4 : 2;
    if (divisor == volumeDivisor) return;
    volumeDivisor = divisor;
    if (!volume.image.id) return;  // no targets yet: createTargets() will use the new divisor
    volumeDepth.destroy();
    volume.destroy();
    createVolumeTargets();
}

void ViewportTarget::Impl::destroyTargets() {
    for (RenderTarget* t : {&gAlbedo, &gNormal, &gDistance, &hdr, &ldr, &volumeDepth, &volume}) t->destroy();
    for (RenderTarget& t : bloom) t.destroy();
    depth.destroy();
}

void ViewportTarget::Impl::destroyAll() {
    destroyTargets();
    frameConstants.destroy();
    instances.destroy();
    beams.destroy();
    glows.destroy();
    lines.destroy();
    if (displaySampler.id) sg_destroy_sampler(displaySampler);
    displaySampler = {};
}

ViewportTarget::ViewportTarget() : impl_(std::make_unique<Impl>()) {}

ViewportTarget::~ViewportTarget() {
    if (sg_isvalid()) impl_->destroyAll();
}

void ViewportTarget::resize(int width, int height) {
    width = std::max(width, 1);
    height = std::max(height, 1);
    Impl& d = *impl_;
    if (d.width == width && d.height == height && d.ldr.image.id) return;
    d.createTargets(width, height);
    if (!d.displaySampler.id) {
        sg_sampler_desc sd{};
        sd.min_filter = SG_FILTER_LINEAR;
        sd.mag_filter = SG_FILTER_LINEAR;
        sd.wrap_u = SG_WRAP_CLAMP_TO_EDGE;
        sd.wrap_v = SG_WRAP_CLAMP_TO_EDGE;
        sd.label = "viewport-display";
        d.displaySampler = sg_make_sampler(&sd);
    }
}

int ViewportTarget::width() const { return impl_->width; }
int ViewportTarget::height() const { return impl_->height; }

std::uint64_t ViewportTarget::imguiTexture() const {
    return simgui_imtextureid_with_sampler(impl_->ldr.texture, impl_->displaySampler);
}

// ---------------------------------------------------------------------------
// Renderer
// ---------------------------------------------------------------------------

struct Renderer::Impl {
    bool ready = false;
    RenderSettings settings;
    RenderStats stats;
    std::uint64_t frame = 0;  // advanced by sg_commit (commit listener)

    Samplers samplers;
    MeshCache meshes;
    GoboAtlas gobos;
    NoiseVolume noise;
    HullMesh hull;
    SurfacePasses surface;
    BeamPasses beamPasses;
    PostPasses post;
    BeamPacker beamPacker;

    // Reused every frame (grow-only).
    std::vector<int> meshCounts;          // instances per mesh id
    std::vector<int> visibleInstances;    // indices into RenderScene::meshes
    std::vector<InstanceGpu> instanceData;
    std::vector<MeshBatch> batches;
    std::vector<LineVertexGpu> lineData;

    static void onCommit(void* self) { ++static_cast<Impl*>(self)->frame; }

    void packMeshes(const RenderScene& scene, const beammath::Frustum& frustum);
    FrameGpu makeFrameConstants(const ViewportTarget::Impl& view, const Camera& camera, const RenderScene& scene) const;
};

Renderer::Renderer() : impl_(std::make_unique<Impl>()) {}
Renderer::~Renderer() = default;

bool Renderer::init() {
    Impl& r = *impl_;
    if (r.ready) return true;
    const sg_features features = sg_query_features();
    if (!features.compute) {
        log::error("render", "storage buffers are not supported by this GPU/driver (OpenGL 4.3 needed)");
        return false;
    }
    r.samplers.create();
    r.gobos.init();
    r.noise.init();
    r.hull.init();
    const bool ok = r.surface.init() && r.beamPasses.init() && r.post.init();
    sg_add_commit_listener({&Impl::onCommit, &r});
    r.ready = ok;
    if (ok)
        log::info("render", "renderer initialised (deferred spot lighting, reduced-resolution volumetric beams)");
    else
        log::error("render", "renderer initialisation failed; see the sokol log above");
    return ok;
}

void Renderer::shutdown() {
    Impl& r = *impl_;
    if (!sg_isvalid()) return;
    sg_remove_commit_listener({&Impl::onCommit, &r});
    r.post.shutdown();
    r.beamPasses.shutdown();
    r.surface.shutdown();
    r.hull.shutdown();
    r.noise.shutdown();
    r.gobos.shutdown();
    r.meshes.shutdown();
    r.samplers.destroy();
    r.ready = false;
}

const RenderStats& Renderer::stats() const { return impl_->stats; }
RenderSettings& Renderer::settings() { return impl_->settings; }
const RenderSettings& Renderer::settings() const { return impl_->settings; }

// Visible mesh instances, grouped by mesh (counting sort, no allocation once
// the vectors have grown), converted to the GPU instance layout.
void Renderer::Impl::packMeshes(const RenderScene& scene, const beammath::Frustum& frustum) {
    meshCounts.assign(meshes.meshCount(), 0);
    visibleInstances.clear();
    for (std::size_t i = 0; i < scene.meshes.size(); ++i) {
        const MeshInstance& inst = scene.meshes[i];
        const MeshCache::Entry* mesh = meshes.find(inst.mesh);
        if (!mesh) continue;
        if (beammath::aabbOutsideFrustum(frustum, mesh->bounds.transformed(inst.world))) continue;
        visibleInstances.push_back(static_cast<int>(i));
        ++meshCounts[static_cast<std::size_t>(inst.mesh)];
    }

    // Prefix sums: meshCounts becomes the running write position of each mesh.
    batches.clear();
    int offset = 0;
    for (std::size_t id = 0; id < meshCounts.size(); ++id) {
        const int count = meshCounts[id];
        if (count > 0) batches.push_back({meshes.find(static_cast<MeshId>(id)), offset, count});
        meshCounts[id] = offset;
        offset += count;
    }

    instanceData.resize(visibleInstances.size());
    for (const int index : visibleInstances) {
        const MeshInstance& inst = scene.meshes[static_cast<std::size_t>(index)];
        InstanceGpu& g = instanceData[static_cast<std::size_t>(meshCounts[static_cast<std::size_t>(inst.mesh)]++)];
        g.world0 = inst.world[0];
        g.world1 = inst.world[1];
        g.world2 = inst.world[2];
        g.world3 = inst.world[3];
        g.albedoRoughness = {inst.material.albedo, std::clamp(inst.material.roughness, 0.02f, 1.0f)};
        g.emissiveMetallic = {inst.material.emissive, std::clamp(inst.material.metallic, 0.0f, 1.0f)};
        g.extra = {static_cast<float>(inst.highlight), 0.0f, 0.0f, 0.0f};
    }
}

FrameGpu Renderer::Impl::makeFrameConstants(const ViewportTarget::Impl& view, const Camera& camera,
                                            const RenderScene& scene) const {
    const Environment& env = scene.environment;
    // Flip y so the image is stored top row first (see "Image orientation" above).
    const glm::mat4 flipY = glm::scale(glm::mat4(1.0f), glm::vec3(1.0f, -1.0f, 1.0f));
    const glm::mat4 invView = glm::inverse(camera.view);

    FrameGpu f;
    f.viewProj = flipY * camera.projection * camera.view;
    f.invViewProj = glm::inverse(f.viewProj);
    // The view matrix is the source of truth for the camera position and direction.
    f.cameraPos = {glm::vec3(invView[3]), static_cast<float>(std::fmod(env.timeSeconds, 3600.0))};
    const float focalPx = 0.5f * static_cast<float>(view.height) * camera.projection[1][1];
    f.cameraForward = {-glm::normalize(glm::vec3(invView[2])), focalPx};
    const float w = static_cast<float>(view.width), h = static_cast<float>(view.height);
    const float vw = static_cast<float>(view.volumeDepth.width), vh = static_cast<float>(view.volumeDepth.height);
    f.viewport = {w, h, 1.0f / w, 1.0f / h};
    f.volumeViewport = {vw, vh, 1.0f / vw, 1.0f / vh};
    f.haze = {std::max(env.hazeDensity, 0.0f) * beammath::kHazeScatteringAtFullDensity,
              std::clamp(env.hazeVariation, 0.0f, 1.0f), std::max(env.beamBrightness, 0.0f),
              std::clamp(settings.hazePhaseG, -0.9f, 0.9f)};
    f.ambientExposure = {env.ambient, std::max(env.exposure, 0.0f)};
    f.params = {beammath::kHdrPerNit, static_cast<float>(frame % 65536),
                std::min(static_cast<float>(std::max(settings.minMarchSteps, 1)), beamPacker.marchStepCap()),
                beamPacker.marchStepCap()};
    f.params2 = {settings.clipBeamsAtFloor ? 1.0f : 0.0f, std::clamp(env.bloomStrength, 0.0f, 1.0f),
                 std::max(settings.marchPixelsPerStep, 0.5f), static_cast<float>(view.volumeDivisor)};
    return f;
}

void Renderer::render(ViewportTarget& target, const Camera& camera, const RenderScene& scene,
                      const assets::AssetLibrary& assets) {
    Impl& r = *impl_;
    ViewportTarget::Impl& view = target.impl();
    if (!view.ldr.image.id) target.resize(16, 16);
    r.stats = {};
    view.setVolumeDivisor(r.settings.volumetricResolution == VolumetricResolution::Quarter ? 4 : 2);
    r.stats.meshInstances = static_cast<int>(scene.meshes.size());
    r.stats.beams = static_cast<int>(scene.beams.size());
    if (!r.ready) return;

    // --- prepare -------------------------------------------------------------
    // Cull with the unflipped matrix: the flip does not change which planes exist.
    const beammath::Frustum frustum = beammath::frustumFromViewProj(camera.projection * camera.view);
    const glm::vec3 cameraPos = glm::vec3(glm::inverse(camera.view)[3]);

    r.meshes.sync(assets);
    r.packMeshes(scene, frustum);
    const float focalPx = 0.5f * static_cast<float>(view.height) * camera.projection[1][1];
    const float volumeScale = 1.0f / static_cast<float>(view.volumeDivisor);
    const VolumeView volumeView{focalPx * volumeScale, static_cast<float>(view.volumeDepth.width) *
                                                           static_cast<float>(view.volumeDepth.height)};
    r.beamPacker.pack(scene.beams, frustum, cameraPos, r.settings, r.gobos, volumeView);
    r.gobos.flush(assets, r.frame);

    const FrameGpu frameConstants = r.makeFrameConstants(view, camera, scene);
    view.frameConstants.upload(&frameConstants, sizeof(frameConstants), r.frame);
    view.instances.upload(r.instanceData.data(), r.instanceData.size() * sizeof(InstanceGpu), r.frame);
    const std::vector<BeamGpu>& beams = r.beamPacker.instances();
    view.beams.upload(beams.data(), beams.size() * sizeof(BeamGpu), r.frame);
    const std::vector<GlowGpu>& glows = r.beamPacker.glows();
    view.glows.upload(glows.data(), glows.size() * sizeof(GlowGpu), r.frame);
    r.lineData.clear();
    for (const DebugLine& l : scene.lines) {
        r.lineData.push_back({glm::vec4(l.from, 1.0f), l.color});
        r.lineData.push_back({glm::vec4(l.to, 1.0f), l.color});
    }
    view.lines.upload(r.lineData.data(), r.lineData.size() * sizeof(LineVertexGpu), r.frame);

    r.stats.beamInstances = static_cast<int>(beams.size());
    r.stats.volumetricDivisor = view.volumeDivisor;
    r.stats.volumetricMaxSteps = r.beamPacker.marchStepCap();
    r.stats.volumetricCoverageMPixels = r.beamPacker.volumetricCoveragePixels() * 1.0e-6f;

    // --- passes ----------------------------------------------------------------
    PassContext ctx{view, scene, r.settings, r.samplers, r.stats};
    r.surface.drawGBuffer(ctx, r.meshes, r.batches);
    r.surface.drawSpotLights(ctx, r.hull, static_cast<int>(beams.size()), r.gobos.textureView());
    const bool haze = r.settings.volumetrics && scene.environment.hazeDensity > 0.0f &&
                      scene.environment.beamBrightness > 0.0f;
    if (haze)
        r.beamPasses.drawVolumetrics(ctx, r.hull, r.beamPacker.volumetricCount(), r.gobos.textureView(),
                                     r.noise.textureView());
    if (r.settings.lensGlow) r.beamPasses.drawLensGlow(ctx, static_cast<int>(glows.size()));
    if (r.settings.bloom) r.post.drawBloom(ctx);
    r.post.drawComposite(ctx, static_cast<int>(r.lineData.size()));
}

}  // namespace dmxviz::render
