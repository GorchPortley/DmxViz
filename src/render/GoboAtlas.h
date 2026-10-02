#pragma once
// All gobo images on the GPU in one 2D array texture ("atlas"), so a single
// instanced draw can render beams with any gobo: each beam just stores its
// layer numbers.
//
// * Every layer is kSize x kSize RGBA8 with a full mip chain; the shaders blur
//   gobos (focus, frost, lens defocus) by sampling a coarser mip level.
// * Layer 0 is "open" (white) and is used for kInvalidImage.
// * RGB is the transmission per colour channel: white passes light, black
//   blocks it, coloured pixels act like glass gobos. Source alpha is folded in
//   (transparent = blocked), so SVG gobos with transparent backgrounds work.
// * Images are registered lazily the first time a beam uses them and baked
//   (resampled to the layer size, letterboxed, mipmapped) on the CPU once per
//   image revision. The texture grows by doubling its layer count.

#include "core/Id.h"

#include "sokol_gfx.h"

#include <array>
#include <cstdint>
#include <vector>

namespace dmxviz::assets {
class AssetLibrary;
struct ImageData;
}  // namespace dmxviz::assets

namespace dmxviz::render {

class GoboAtlas {
public:
    static constexpr int kSize = 256;
    static constexpr int kMipCount = 9;  // 256 .. 1
    static constexpr int kMaxLayers = 256;

    void init();
    void shutdown();

    // Atlas layer of an image (registering it on first use); 0 = open.
    int layerFor(ImageId image);

    // Bakes and uploads layers that are new or whose image changed. Call once
    // per frame after all layerFor() calls and before the passes draw.
    void flush(const assets::AssetLibrary& assets, std::uint64_t frame);

    sg_view textureView() const { return view_; }
    int layerCount() const { return static_cast<int>(layers_.size()); }

private:
    struct Layer {
        ImageId image = kInvalidImage;
        std::uint64_t revision = 0;
        bool baked = false;
    };

    void ensureCapacity(int layers);
    void bakeLayer(int layer, const assets::ImageData* image);
    void buildMips(int layer);

    std::vector<int> layerOfImage_;  // indexed by ImageId; 0 = not registered yet
    std::vector<Layer> layers_;
    std::array<std::vector<std::uint8_t>, kMipCount> mips_;  // CPU copy, per mip all layers back to back
    int capacity_ = 0;
    bool dirty_ = false;
    std::uint64_t checkedLibraryRevision_ = ~std::uint64_t{0};
    std::uint64_t lastUploadFrame_ = ~std::uint64_t{0};
    sg_image image_{};
    sg_view view_{};
};

}  // namespace dmxviz::render
