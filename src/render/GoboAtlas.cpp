#include "render/GoboAtlas.h"

#include "assets/AssetLibrary.h"
#include "core/Log.h"
#include "core/Math.h"

#include <algorithm>
#include <cmath>
#include <cstring>

namespace dmxviz::render {
namespace {

constexpr std::size_t kLayerBytes0 = static_cast<std::size_t>(GoboAtlas::kSize) * GoboAtlas::kSize * 4;

int mipSize(int mip) { return std::max(GoboAtlas::kSize >> mip, 1); }

std::size_t mipLayerBytes(int mip) {
    const auto s = static_cast<std::size_t>(mipSize(mip));
    return s * s * 4;
}

// Transmission (0..1 per channel) of a source pixel; outside the image = blocked.
glm::vec3 sourceTexel(const assets::ImageData& img, int x, int y) {
    if (x < 0 || y < 0 || x >= img.width || y >= img.height) return glm::vec3(0.0f);
    const std::size_t i = (static_cast<std::size_t>(y) * img.width + x) * img.channels;
    if (img.channels == 1) return glm::vec3(img.pixels[i] / 255.0f);
    const float a = img.pixels[i + 3] / 255.0f;
    return glm::vec3(img.pixels[i], img.pixels[i + 1], img.pixels[i + 2]) * (a / 255.0f);
}

glm::vec3 sampleBilinear(const assets::ImageData& img, float x, float y) {
    const float fx = std::floor(x), fy = std::floor(y);
    const int x0 = static_cast<int>(fx), y0 = static_cast<int>(fy);
    const float tx = x - fx, ty = y - fy;
    const glm::vec3 a = glm::mix(sourceTexel(img, x0, y0), sourceTexel(img, x0 + 1, y0), tx);
    const glm::vec3 b = glm::mix(sourceTexel(img, x0, y0 + 1), sourceTexel(img, x0 + 1, y0 + 1), tx);
    return glm::mix(a, b, ty);
}

}  // namespace

void GoboAtlas::init() {
    layers_.clear();
    layers_.push_back({kInvalidImage, 0, false});  // layer 0: open
    layerOfImage_.clear();
    ensureCapacity(8);
    dirty_ = true;
}

void GoboAtlas::shutdown() {
    if (view_.id) sg_destroy_view(view_);
    if (image_.id) sg_destroy_image(image_);
    view_ = {};
    image_ = {};
    capacity_ = 0;
    for (auto& m : mips_) m.clear();
    layers_.clear();
    layerOfImage_.clear();
}

int GoboAtlas::layerFor(ImageId image) {
    if (image < 0) return 0;
    const auto index = static_cast<std::size_t>(image);
    if (index < layerOfImage_.size() && layerOfImage_[index] > 0) return layerOfImage_[index];
    if (static_cast<int>(layers_.size()) >= kMaxLayers) {
        static bool warned = false;
        if (!warned) log::warn("render", "gobo atlas full ({} layers); further gobos render open", kMaxLayers);
        warned = true;
        return 0;
    }
    if (index >= layerOfImage_.size()) layerOfImage_.resize(index + 1, 0);
    const int layer = static_cast<int>(layers_.size());
    layers_.push_back({image, 0, false});
    layerOfImage_[index] = layer;
    dirty_ = true;
    return layer;
}

void GoboAtlas::ensureCapacity(int layers) {
    if (layers <= capacity_ && image_.id) return;
    int capacity = std::max(capacity_, 8);
    while (capacity < layers) capacity *= 2;

    // Grow the CPU copy, keeping the existing layers (they are contiguous per mip).
    for (int mip = 0; mip < kMipCount; ++mip) mips_[static_cast<std::size_t>(mip)].resize(mipLayerBytes(mip) * capacity, 0);

    if (view_.id) sg_destroy_view(view_);
    if (image_.id) sg_destroy_image(image_);
    sg_image_desc d{};
    d.type = SG_IMAGETYPE_ARRAY;
    d.usage.dynamic_update = true;
    d.width = kSize;
    d.height = kSize;
    d.num_slices = capacity;
    d.num_mipmaps = kMipCount;
    d.pixel_format = SG_PIXELFORMAT_RGBA8;
    d.label = "gobo-atlas";
    image_ = sg_make_image(&d);
    sg_view_desc vd{};
    vd.texture.image = image_;
    vd.label = "gobo-atlas";
    view_ = sg_make_view(&vd);
    capacity_ = capacity;
    dirty_ = true;  // the new image has no content yet
}

void GoboAtlas::bakeLayer(int layer, const assets::ImageData* image) {
    std::uint8_t* dst = mips_[0].data() + kLayerBytes0 * static_cast<std::size_t>(layer);
    if (!image || !image->valid() || (image->channels != 1 && image->channels != 4)) {
        std::memset(dst, 255, kLayerBytes0);  // open
        buildMips(layer);
        return;
    }
    // Fit the image into the square layer (letterbox, centred), 4x4 supersampled
    // so large source images are averaged rather than aliased.
    const float scale = static_cast<float>(std::max(image->width, image->height)) / kSize;
    const float offsetX = 0.5f * (std::max(image->width, image->height) - image->width);
    const float offsetY = 0.5f * (std::max(image->width, image->height) - image->height);
    constexpr int kSub = 4;
    for (int y = 0; y < kSize; ++y) {
        for (int x = 0; x < kSize; ++x) {
            glm::vec3 sum(0.0f);
            for (int sy = 0; sy < kSub; ++sy) {
                for (int sx = 0; sx < kSub; ++sx) {
                    const float u = (x + (sx + 0.5f) / kSub) * scale - offsetX - 0.5f;
                    const float v = (y + (sy + 0.5f) / kSub) * scale - offsetY - 0.5f;
                    sum += sampleBilinear(*image, u, v);
                }
            }
            const glm::vec3 c = glm::clamp(sum / float(kSub * kSub), 0.0f, 1.0f);
            std::uint8_t* p = dst + (static_cast<std::size_t>(y) * kSize + x) * 4;
            p[0] = static_cast<std::uint8_t>(c.r * 255.0f + 0.5f);
            p[1] = static_cast<std::uint8_t>(c.g * 255.0f + 0.5f);
            p[2] = static_cast<std::uint8_t>(c.b * 255.0f + 0.5f);
            p[3] = 255;
        }
    }
    buildMips(layer);
}

void GoboAtlas::buildMips(int layer) {
    for (int mip = 1; mip < kMipCount; ++mip) {
        const int srcSize = mipSize(mip - 1);
        const int dstSize = mipSize(mip);
        const std::uint8_t* src = mips_[static_cast<std::size_t>(mip - 1)].data() + mipLayerBytes(mip - 1) * layer;
        std::uint8_t* dst = mips_[static_cast<std::size_t>(mip)].data() + mipLayerBytes(mip) * layer;
        for (int y = 0; y < dstSize; ++y) {
            for (int x = 0; x < dstSize; ++x) {
                for (int c = 0; c < 4; ++c) {
                    int sum = 0;
                    for (int k = 0; k < 4; ++k) {
                        const int sx = std::min(2 * x + (k & 1), srcSize - 1);
                        const int sy = std::min(2 * y + (k >> 1), srcSize - 1);
                        sum += src[(static_cast<std::size_t>(sy) * srcSize + sx) * 4 + c];
                    }
                    dst[(static_cast<std::size_t>(y) * dstSize + x) * 4 + c] = static_cast<std::uint8_t>((sum + 2) / 4);
                }
            }
        }
    }
}

void GoboAtlas::flush(const assets::AssetLibrary& assets, std::uint64_t frame) {
    // Replaced images: only look when the library changed at all.
    if (assets.revision() != checkedLibraryRevision_) {
        checkedLibraryRevision_ = assets.revision();
        for (std::size_t i = 1; i < layers_.size(); ++i) {
            if (layers_[i].baked && layers_[i].revision != assets.imageRevision(layers_[i].image)) {
                layers_[i].baked = false;
                dirty_ = true;
            }
        }
    }
    if (!dirty_ || frame == lastUploadFrame_) return;

    ensureCapacity(static_cast<int>(layers_.size()));
    for (std::size_t i = 0; i < layers_.size(); ++i) {
        Layer& l = layers_[i];
        if (l.baked) continue;
        bakeLayer(static_cast<int>(i), i == 0 ? nullptr : assets.image(l.image));
        l.revision = i == 0 ? 0 : assets.imageRevision(l.image);
        l.baked = true;
    }
    sg_image_data data{};
    for (int mip = 0; mip < kMipCount; ++mip)
        data.mip_levels[mip] = {mips_[static_cast<std::size_t>(mip)].data(), mips_[static_cast<std::size_t>(mip)].size()};
    sg_update_image(image_, &data);
    lastUploadFrame_ = frame;
    dirty_ = false;
}

}  // namespace dmxviz::render
