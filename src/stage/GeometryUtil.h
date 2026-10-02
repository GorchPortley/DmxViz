#pragma once
// Small helpers shared by the stage mesh builders.

#include "assets/AssetLibrary.h"
#include "assets/MeshData.h"

#include <string>

namespace dmxviz::stage::geom {

// Appends a cylinder between a and b. Caps are only needed where the tube end
// is visible (chords); braces end inside chords and skip them.
void addTube(assets::MeshData& m, const glm::vec3& a, const glm::vec3& b, float radius, int sides, bool caps);
// Appends an axis-aligned box given its centre and full size.
void addBox(assets::MeshData& m, const glm::vec3& centre, const glm::vec3& size);
// Appends a box rotated by `rotation` around its centre.
void addBox(assets::MeshData& m, const glm::vec3& centre, const glm::vec3& size, const glm::quat& rotation);

// Returns the library mesh registered under `key`, building it with `build()`
// the first time. Identical parameters therefore share one mesh.
template <typename BuildFn>
MeshId cachedMesh(assets::AssetLibrary& library, const std::string& key, BuildFn&& build) {
    const MeshId existing = library.findMesh(key);
    if (existing != kInvalidMesh) return existing;
    assets::MeshData mesh = build();
    mesh.name = key;
    mesh.computeBounds();
    return library.addMesh(std::move(mesh), key);
}

// Formats a length for mesh cache keys with 0.1 mm resolution.
std::string keyNum(float v);

}  // namespace dmxviz::stage::geom
