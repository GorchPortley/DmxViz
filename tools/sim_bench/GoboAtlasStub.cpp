// The CPU-only part of render/GoboAtlas.cpp that BeamPacker uses: image id -> atlas layer. The real file
// needs sokol_gfx (a GL context), which this benchmark deliberately avoids. Keep in step with GoboAtlas::layerFor.

#include "render/GoboAtlas.h"

namespace dmxviz::render {

int GoboAtlas::layerFor(ImageId image) {
    if (image < 0) return 0;
    if (layers_.empty()) layers_.push_back({});  // layer 0 is the "open" layer; GoboAtlas::init() adds it
    const auto index = static_cast<std::size_t>(image);
    if (index < layerOfImage_.size() && layerOfImage_[index] > 0) return layerOfImage_[index];
    if (static_cast<int>(layers_.size()) >= kMaxLayers) return 0;
    if (index >= layerOfImage_.size()) layerOfImage_.resize(index + 1, 0);
    const int layer = static_cast<int>(layers_.size());
    layers_.push_back({image, 0, false});
    layerOfImage_[index] = layer;
    return layer;
}

}  // namespace dmxviz::render
