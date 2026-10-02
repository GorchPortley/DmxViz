#pragma once
// Turns a node's content into meshes (RenderParts) using the builders.
// Scene::renderParts() caches the result per node.

#include "assets/AssetLibrary.h"
#include "stage/ModelCache.h"
#include "stage/Node.h"

#include <vector>

namespace dmxviz::stage {

// Size of the red marker shown in place of a model file that failed to load,
// so the node can still be seen, selected and fixed.
constexpr float kMissingModelMarkerSize = 0.3f;

// The meshes of a node in node space. Groups, fixtures, cameras and unknown
// kinds have none. NodeData::materialOverride, when set, replaces every
// part's material.
std::vector<RenderPart> buildRenderParts(const NodeData& data, assets::AssetLibrary& library, ModelCache& models);

}  // namespace dmxviz::stage
