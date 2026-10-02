#pragma once
// 3D model import for stage set pieces and GDTF fixture parts.
//
// OWNER: stage work stream (WS4). Formats to support: glTF 2.0 (.gltf/.glb,
// via cgltf), 3DS (.3ds, used by many GDTF files) and Wavefront OBJ.

#include "assets/MeshData.h"
#include "core/SceneTypes.h"

#include <cstdint>
#include <filesystem>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace dmxviz::assets {

struct ModelPart {
    MeshData mesh;              // in the model's Y-up, metre coordinate space
    Material material;
    glm::mat4 transform{1.0f};  // part placement within the model
};

struct ModelData {
    std::vector<ModelPart> parts;
    Aabb bounds;  // of all parts, transforms applied
};

struct ModelLoadOptions {
    // Converts a Z-up source (3DS files, GDTF models) to DmxViz's Y-up space.
    bool zUp = false;
    // Multiplier applied to positions (e.g. 0.001 for millimetre sources).
    float unitScale = 1.0f;
};

std::optional<ModelData> loadModel(const std::filesystem::path& path, const ModelLoadOptions& opts = {},
                                   std::string* error = nullptr);

// formatHint: "gltf", "glb", "3ds" or "obj". For .gltf with external buffers,
// `resolveFile` must supply referenced files by relative URI (GDTF archives).
using FileResolver = std::optional<std::vector<std::uint8_t>> (*)(void* user, std::string_view uri);
std::optional<ModelData> loadModelFromMemory(std::span<const std::uint8_t> bytes, std::string_view formatHint,
                                             const ModelLoadOptions& opts = {}, std::string* error = nullptr,
                                             FileResolver resolveFile = nullptr, void* resolverUser = nullptr);

}  // namespace dmxviz::assets
