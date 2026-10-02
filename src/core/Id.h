#pragma once
// Handle types shared across modules.

#include <cstdint>

namespace dmxviz {

// Identifies a node in the stage scene graph. 0 is "no node". Ids are unique
// within a project and are persisted in project files.
using NodeId = std::uint64_t;
constexpr NodeId kInvalidNode = 0;

// Handles into assets::AssetLibrary. Negative means "none".
using MeshId = std::int32_t;
using ImageId = std::int32_t;
constexpr MeshId kInvalidMesh = -1;
constexpr ImageId kInvalidImage = -1;

}  // namespace dmxviz
