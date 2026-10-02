#include "render/GpuResources.h"

#include "core/Log.h"

#include <algorithm>

namespace dmxviz::render {

void RenderTarget::create(int w, int h, sg_pixel_format format, const char* label) {
    destroy();
    width = std::max(w, 1);
    height = std::max(h, 1);
    sg_image_desc d{};
    d.usage.color_attachment = true;
    d.width = width;
    d.height = height;
    d.pixel_format = format;
    d.sample_count = 1;
    d.label = label;
    image = sg_make_image(&d);

    sg_view_desc vd{};
    vd.color_attachment.image = image;
    vd.label = label;
    attachment = sg_make_view(&vd);
    vd = {};
    vd.texture.image = image;
    vd.label = label;
    texture = sg_make_view(&vd);
}

void RenderTarget::destroy() {
    if (texture.id) sg_destroy_view(texture);
    if (attachment.id) sg_destroy_view(attachment);
    if (image.id) sg_destroy_image(image);
    texture = attachment = {};
    image = {};
    width = height = 0;
}

void DepthTarget::create(int w, int h, const char* label) {
    destroy();
    sg_image_desc d{};
    d.usage.depth_stencil_attachment = true;
    d.width = std::max(w, 1);
    d.height = std::max(h, 1);
    d.pixel_format = kDepthFormat;
    d.sample_count = 1;
    d.label = label;
    image = sg_make_image(&d);
    sg_view_desc vd{};
    vd.depth_stencil_attachment.image = image;
    vd.label = label;
    attachment = sg_make_view(&vd);
}

void DepthTarget::destroy() {
    if (attachment.id) sg_destroy_view(attachment);
    if (image.id) sg_destroy_image(image);
    attachment = {};
    image = {};
}

void DynamicBuffer::destroy() {
    if (view_.id) sg_destroy_view(view_);
    if (buffer_.id) sg_destroy_buffer(buffer_);
    view_ = {};
    buffer_ = {};
    capacity_ = 0;
    lastUploadFrame_ = ~std::uint64_t{0};
}

void DynamicBuffer::ensureCapacity(std::size_t bytes) {
    if (bytes <= capacity_ && buffer_.id) return;
    std::size_t capacity = std::max<std::size_t>(capacity_ * 2, 4096);
    while (capacity < bytes) capacity *= 2;
    destroy();
    sg_buffer_desc d{};
    d.size = capacity;
    d.usage.dynamic_update = true;
    d.usage.vertex_buffer = kind_ == Kind::Vertex;
    d.usage.storage_buffer = kind_ == Kind::Storage;
    d.label = label_;
    buffer_ = sg_make_buffer(&d);
    if (kind_ == Kind::Storage) {
        sg_view_desc vd{};
        vd.storage_buffer.buffer = buffer_;
        vd.label = label_;
        view_ = sg_make_view(&vd);
    }
    capacity_ = capacity;
}

bool DynamicBuffer::upload(const void* data, std::size_t bytes, std::uint64_t frame) {
    // A zero-sized frame still needs a valid (if unused) buffer to bind.
    ensureCapacity(std::max<std::size_t>(bytes, 16));
    if (bytes == 0) return true;
    if (frame == lastUploadFrame_) {
        if (!warnedTwice_) log::warn("render", "{} updated twice in one frame; is a viewport rendered twice?", label_);
        warnedTwice_ = true;
        return false;
    }
    sg_range range{data, bytes};
    sg_update_buffer(buffer_, &range);
    lastUploadFrame_ = frame;
    return true;
}

void Samplers::create() {
    sg_sampler_desc d{};
    d.min_filter = SG_FILTER_NEAREST;
    d.mag_filter = SG_FILTER_NEAREST;
    d.wrap_u = SG_WRAP_CLAMP_TO_EDGE;
    d.wrap_v = SG_WRAP_CLAMP_TO_EDGE;
    d.label = "nearest-clamp";
    nearestClamp = sg_make_sampler(&d);

    d.min_filter = SG_FILTER_LINEAR;
    d.mag_filter = SG_FILTER_LINEAR;
    d.label = "linear-clamp";
    linearClamp = sg_make_sampler(&d);

    d.mipmap_filter = SG_FILTER_LINEAR;
    d.label = "gobo";
    gobo = sg_make_sampler(&d);

    d.wrap_u = d.wrap_v = d.wrap_w = SG_WRAP_REPEAT;
    d.mipmap_filter = SG_FILTER_NEAREST;
    d.label = "noise";
    noise = sg_make_sampler(&d);
}

void Samplers::destroy() {
    for (sg_sampler* s : {&nearestClamp, &linearClamp, &gobo, &noise}) {
        if (s->id) sg_destroy_sampler(*s);
        *s = {};
    }
}

}  // namespace dmxviz::render
