#include "assets/ModelLoader.h"

// Placeholder until WS4 lands the real loaders. Keeps every module linkable.

namespace dmxviz::assets {

std::optional<ModelData> loadModel(const std::filesystem::path& path, const ModelLoadOptions&, std::string* error) {
    if (error) *error = "model loading not implemented yet: " + path.string();
    return std::nullopt;
}

std::optional<ModelData> loadModelFromMemory(std::span<const std::uint8_t>, std::string_view formatHint,
                                             const ModelLoadOptions&, std::string* error, FileResolver, void*) {
    if (error) *error = "model loading not implemented yet (" + std::string(formatHint) + ")";
    return std::nullopt;
}

}  // namespace dmxviz::assets
