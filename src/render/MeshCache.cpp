#include "render/MeshCache.h"

#include "assets/AssetLibrary.h"
#include "core/Log.h"

namespace dmxviz::render {

void MeshCache::shutdown() {
    if (vertexBuffer_.id) sg_destroy_buffer(vertexBuffer_);
    if (indexBuffer_.id) sg_destroy_buffer(indexBuffer_);
    vertexBuffer_ = {};
    indexBuffer_ = {};
    entries_.clear();
    libraryRevision_ = ~std::uint64_t{0};
}

void MeshCache::sync(const assets::AssetLibrary& assets) {
    if (assets.revision() == libraryRevision_) return;
    libraryRevision_ = assets.revision();

    // The library revision also changes for images; only rebuild for meshes.
    bool changed = assets.meshCount() != entries_.size();
    for (std::size_t i = 0; !changed && i < entries_.size(); ++i)
        changed = entries_[i].revision != assets.meshRevision(static_cast<MeshId>(i));
    if (changed) rebuild(assets);
}

void MeshCache::rebuild(const assets::AssetLibrary& assets) {
    std::size_t vertexCount = 0;
    std::size_t indexCount = 0;
    for (std::size_t i = 0; i < assets.meshCount(); ++i) {
        const assets::MeshData* m = assets.mesh(static_cast<MeshId>(i));
        vertexCount += m->vertices.size();
        indexCount += m->indices.size();
    }

    // Temporary staging copies: this runs on load/edit, not every frame.
    std::vector<assets::Vertex> vertices;
    std::vector<std::uint32_t> indices;
    vertices.reserve(vertexCount);
    indices.reserve(indexCount);
    entries_.assign(assets.meshCount(), Entry{});
    for (std::size_t i = 0; i < assets.meshCount(); ++i) {
        const auto id = static_cast<MeshId>(i);
        const assets::MeshData* m = assets.mesh(id);
        Entry& e = entries_[i];
        e.revision = assets.meshRevision(id);
        if (m->vertices.empty() || m->indices.empty()) continue;
        e.vertexOffsetBytes = static_cast<int>(vertices.size() * sizeof(assets::Vertex));
        e.indexOffsetBytes = static_cast<int>(indices.size() * sizeof(std::uint32_t));
        e.indexCount = static_cast<int>(m->indices.size());
        e.bounds = m->bounds;
        if (e.bounds.empty())
            for (const assets::Vertex& v : m->vertices) e.bounds.expand(v.position);
        vertices.insert(vertices.end(), m->vertices.begin(), m->vertices.end());
        indices.insert(indices.end(), m->indices.begin(), m->indices.end());
    }

    // Destroying a buffer that earlier passes of this frame used is fine: GL
    // keeps the storage alive until those commands have executed.
    if (vertexBuffer_.id) sg_destroy_buffer(vertexBuffer_);
    if (indexBuffer_.id) sg_destroy_buffer(indexBuffer_);
    vertexBuffer_ = {};
    indexBuffer_ = {};
    if (vertices.empty()) return;

    sg_buffer_desc vd{};
    vd.usage.vertex_buffer = true;
    vd.data = {vertices.data(), vertices.size() * sizeof(assets::Vertex)};
    vd.label = "mesh-arena-vertices";
    vertexBuffer_ = sg_make_buffer(&vd);

    sg_buffer_desc id{};
    id.usage.index_buffer = true;
    id.data = {indices.data(), indices.size() * sizeof(std::uint32_t)};
    id.label = "mesh-arena-indices";
    indexBuffer_ = sg_make_buffer(&id);

    log::debug("render", "mesh arena rebuilt: {} meshes, {} vertices, {} triangles", entries_.size(), vertices.size(),
               indices.size() / 3);
}

}  // namespace dmxviz::render
