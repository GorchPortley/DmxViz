#pragma once
// GPU copies of the meshes in the AssetLibrary.
//
// All meshes share one vertex and one index buffer (an "arena"); each mesh is
// a range inside them. That keeps the number of sokol buffers constant no
// matter how many meshes a show has (sokol pools are fixed-size), and lets a
// draw select a mesh just by buffer offsets.
//
// The arena is rebuilt from the library whenever a mesh is added or replaced
// (AssetLibrary revisions). That costs one upload of all mesh data, which is
// fine because it only happens while loading or editing, never per frame.

#include "core/Id.h"
#include "core/Math.h"

#include "sokol_gfx.h"

#include <cstdint>
#include <vector>

namespace dmxviz::assets {
class AssetLibrary;
struct Vertex;
}  // namespace dmxviz::assets

namespace dmxviz::render {

class MeshCache {
public:
    struct Entry {
        int vertexOffsetBytes = 0;
        int indexOffsetBytes = 0;
        int indexCount = 0;  // 0 = empty or unknown mesh
        Aabb bounds;
        std::uint64_t revision = 0;
    };

    void shutdown();

    // Brings the arena up to date with the library. Cheap when nothing changed.
    void sync(const assets::AssetLibrary& assets);

    const Entry* find(MeshId id) const {
        if (id < 0 || static_cast<std::size_t>(id) >= entries_.size()) return nullptr;
        const Entry& e = entries_[static_cast<std::size_t>(id)];
        return e.indexCount > 0 ? &e : nullptr;
    }
    sg_buffer vertexBuffer() const { return vertexBuffer_; }
    sg_buffer indexBuffer() const { return indexBuffer_; }
    std::size_t meshCount() const { return entries_.size(); }

private:
    void rebuild(const assets::AssetLibrary& assets);

    std::uint64_t libraryRevision_ = ~std::uint64_t{0};
    std::vector<Entry> entries_;
    sg_buffer vertexBuffer_{};
    sg_buffer indexBuffer_{};
};

}  // namespace dmxviz::render
