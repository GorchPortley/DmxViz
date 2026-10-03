#pragma once
// Small RAII-free helpers around sokol resources used by the render passes:
// render-target images with their views, grow-only per-frame buffers and the
// shared samplers. All functions must be called on the main thread with
// sokol_gfx set up.

#include "sokol_gfx.h"

#include <cstddef>
#include <cstdint>

namespace dmxviz::render {

// Pixel formats of the per-viewport targets (see docs in src/render/README.md).
constexpr sg_pixel_format kAlbedoFormat = SG_PIXELFORMAT_RGBA8;
constexpr sg_pixel_format kNormalFormat = SG_PIXELFORMAT_RGBA16F;
constexpr sg_pixel_format kDistanceFormat = SG_PIXELFORMAT_R32F;
constexpr sg_pixel_format kHdrFormat = SG_PIXELFORMAT_RGBA16F;
constexpr sg_pixel_format kDepthFormat = SG_PIXELFORMAT_DEPTH;  // 32-bit float depth on GL
constexpr sg_pixel_format kLdrFormat = SG_PIXELFORMAT_RGBA8;
constexpr sg_pixel_format kVolumeDepthFormat = SG_PIXELFORMAT_RG32F;
constexpr sg_pixel_format kVolumeFormat = SG_PIXELFORMAT_RGBA16F;
constexpr sg_pixel_format kBloomFormat = SG_PIXELFORMAT_RGBA16F;
constexpr int kBloomLevels = 6;

// An image that is rendered into (attachment view) and read later (texture view).
struct RenderTarget {
    sg_image image{};
    sg_view attachment{};
    sg_view texture{};
    int width = 0;
    int height = 0;

    void create(int w, int h, sg_pixel_format format, const char* label);
    void destroy();
};

// The depth buffer is only ever an attachment (its distance copy is sampled instead).
struct DepthTarget {
    sg_image image{};
    sg_view attachment{};

    void create(int w, int h, const char* label);
    void destroy();
};

// A buffer whose whole content is replaced from the CPU every frame. It only
// grows (doubling), so after the first frames there are no reallocations.
// sokol allows one update per buffer per frame; upload() returns false when it
// was already updated this frame (the old content stays in use).
class DynamicBuffer {
public:
    enum class Kind { Vertex, Storage };

    void init(Kind kind, const char* label) {
        kind_ = kind;
        label_ = label;
    }
    void destroy();
    bool upload(const void* data, std::size_t bytes, std::uint64_t frame);

    sg_buffer buffer() const { return buffer_; }
    sg_view storageView() const { return view_; }  // Storage kind only
    bool valid() const { return buffer_.id != SG_INVALID_ID; }

private:
    void ensureCapacity(std::size_t bytes);

    Kind kind_ = Kind::Vertex;
    const char* label_ = "dynamic-buffer";
    sg_buffer buffer_{};
    sg_view view_{};
    std::size_t capacity_ = 0;
    std::uint64_t lastUploadFrame_ = ~std::uint64_t{0};
    bool warnedTwice_ = false;
};

// Samplers shared by all passes.
struct Samplers {
    sg_sampler nearestClamp{};  // texelFetch-style reads of G-buffer / depth
    sg_sampler linearClamp{};   // bloom, upsample
    sg_sampler gobo{};          // trilinear, clamp: mip bias blurs gobos
    sg_sampler noise{};         // trilinear, repeat: 3D haze noise

    void create();
    void destroy();
};

}  // namespace dmxviz::render
