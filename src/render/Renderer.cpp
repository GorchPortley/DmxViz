#include "render/Renderer.h"

#include "assets/AssetLibrary.h"
#include "core/Log.h"

#include "sokol_app.h"
#include "sokol_gfx.h"
#include "imgui.h"
#include "sokol_imgui.h"

// Bootstrap implementation: clears the viewport to the background colour.
// WS3 replaces this with the real pipeline described in docs/ARCHITECTURE.md.

namespace dmxviz::render {

struct ViewportTarget::Impl {
    int width = 0;
    int height = 0;
    sg_image color{};
    sg_image depth{};
    sg_view colorAttachment{};
    sg_view depthAttachment{};
    sg_view colorTexture{};
    sg_sampler sampler{};

    void destroy() {
        if (colorTexture.id) sg_destroy_view(colorTexture);
        if (colorAttachment.id) sg_destroy_view(colorAttachment);
        if (depthAttachment.id) sg_destroy_view(depthAttachment);
        if (color.id) sg_destroy_image(color);
        if (depth.id) sg_destroy_image(depth);
        colorTexture = colorAttachment = depthAttachment = {};
        color = depth = {};
    }
};

ViewportTarget::ViewportTarget() : impl_(std::make_unique<Impl>()) {}

ViewportTarget::~ViewportTarget() {
    if (sg_isvalid()) {
        impl_->destroy();
        if (impl_->sampler.id) sg_destroy_sampler(impl_->sampler);
    }
}

void ViewportTarget::resize(int width, int height) {
    width = std::max(width, 1);
    height = std::max(height, 1);
    Impl& d = *impl_;
    if (d.width == width && d.height == height && d.color.id) return;
    d.destroy();
    d.width = width;
    d.height = height;

    sg_image_desc cd{};
    cd.usage.color_attachment = true;
    cd.width = width;
    cd.height = height;
    cd.pixel_format = SG_PIXELFORMAT_RGBA8;
    cd.sample_count = 1;
    cd.label = "viewport-color";
    d.color = sg_make_image(&cd);

    sg_image_desc dd{};
    dd.usage.depth_stencil_attachment = true;
    dd.width = width;
    dd.height = height;
    dd.pixel_format = SG_PIXELFORMAT_DEPTH;
    dd.sample_count = 1;
    dd.label = "viewport-depth";
    d.depth = sg_make_image(&dd);

    sg_view_desc vd{};
    vd.color_attachment.image = d.color;
    d.colorAttachment = sg_make_view(&vd);
    vd = {};
    vd.depth_stencil_attachment.image = d.depth;
    d.depthAttachment = sg_make_view(&vd);
    vd = {};
    vd.texture.image = d.color;
    d.colorTexture = sg_make_view(&vd);

    if (!d.sampler.id) {
        sg_sampler_desc sd{};
        sd.min_filter = SG_FILTER_LINEAR;
        sd.mag_filter = SG_FILTER_LINEAR;
        sd.wrap_u = SG_WRAP_CLAMP_TO_EDGE;
        sd.wrap_v = SG_WRAP_CLAMP_TO_EDGE;
        d.sampler = sg_make_sampler(&sd);
    }
}

int ViewportTarget::width() const { return impl_->width; }
int ViewportTarget::height() const { return impl_->height; }

std::uint64_t ViewportTarget::imguiTexture() const {
    return simgui_imtextureid_with_sampler(impl_->colorTexture, impl_->sampler);
}

struct Renderer::Impl {
    RenderStats stats;
};

Renderer::Renderer() : impl_(std::make_unique<Impl>()) {}
Renderer::~Renderer() = default;

bool Renderer::init() {
    log::info("render", "renderer initialised (bootstrap clear-only pipeline)");
    return true;
}

void Renderer::shutdown() {}

void Renderer::render(ViewportTarget& target, const Camera&, const RenderScene& scene, const assets::AssetLibrary&) {
    ViewportTarget::Impl& t = target.impl();
    if (!t.color.id) target.resize(16, 16);
    const glm::vec3 bg = scene.environment.background;

    sg_pass pass{};
    pass.action.colors[0].load_action = SG_LOADACTION_CLEAR;
    pass.action.colors[0].clear_value = {bg.r, bg.g, bg.b, 1.0f};
    pass.action.depth.load_action = SG_LOADACTION_CLEAR;
    pass.action.depth.clear_value = 1.0f;
    pass.attachments.colors[0] = t.colorAttachment;
    pass.attachments.depth_stencil = t.depthAttachment;
    pass.label = "viewport-clear";
    sg_begin_pass(&pass);
    sg_end_pass();

    impl_->stats = {};
    impl_->stats.meshInstances = static_cast<int>(scene.meshes.size());
    impl_->stats.beams = static_cast<int>(scene.beams.size());
}

const RenderStats& Renderer::stats() const { return impl_->stats; }

}  // namespace dmxviz::render
