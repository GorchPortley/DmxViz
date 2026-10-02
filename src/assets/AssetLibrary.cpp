#include "assets/AssetLibrary.h"

#include "assets/Primitives.h"

namespace dmxviz::assets {

AssetLibrary::AssetLibrary() {
    builtins_[static_cast<int>(BuiltinMesh::Cube)] = addMesh(makeBox(glm::vec3(1.0f)), "builtin:cube");
    builtins_[static_cast<int>(BuiltinMesh::Sphere)] = addMesh(makeSphere(0.5f), "builtin:sphere");
    builtins_[static_cast<int>(BuiltinMesh::Cylinder)] = addMesh(makeCylinder(0.5f, 1.0f), "builtin:cylinder");
    builtins_[static_cast<int>(BuiltinMesh::Cone)] = addMesh(makeCone(0.5f, 1.0f), "builtin:cone");
    builtins_[static_cast<int>(BuiltinMesh::Plane)] = addMesh(makePlane(1.0f, 1.0f), "builtin:plane");
    builtins_[static_cast<int>(BuiltinMesh::Disc)] = addMesh(makeDisc(0.5f), "builtin:disc");
}

MeshId AssetLibrary::addMesh(MeshData mesh, std::string key) {
    if (mesh.bounds.empty()) mesh.computeBounds();
    ++revision_;
    if (!key.empty()) {
        if (auto it = meshKeys_.find(key); it != meshKeys_.end()) {
            meshes_[static_cast<std::size_t>(it->second)].data = std::move(mesh);
            meshes_[static_cast<std::size_t>(it->second)].revision = revision_;
            return it->second;
        }
    }
    const auto id = static_cast<MeshId>(meshes_.size());
    if (!key.empty()) meshKeys_.emplace(key, id);
    meshes_.push_back({std::move(mesh), std::move(key), revision_});
    return id;
}

ImageId AssetLibrary::addImage(ImageData image, std::string key) {
    ++revision_;
    if (!key.empty()) {
        if (auto it = imageKeys_.find(key); it != imageKeys_.end()) {
            images_[static_cast<std::size_t>(it->second)].data = std::move(image);
            images_[static_cast<std::size_t>(it->second)].revision = revision_;
            return it->second;
        }
    }
    const auto id = static_cast<ImageId>(images_.size());
    if (!key.empty()) imageKeys_.emplace(key, id);
    images_.push_back({std::move(image), std::move(key), revision_});
    return id;
}

MeshId AssetLibrary::findMesh(std::string_view key) const {
    auto it = meshKeys_.find(std::string(key));
    return it == meshKeys_.end() ? kInvalidMesh : it->second;
}

ImageId AssetLibrary::findImage(std::string_view key) const {
    auto it = imageKeys_.find(std::string(key));
    return it == imageKeys_.end() ? kInvalidImage : it->second;
}

const MeshData* AssetLibrary::mesh(MeshId id) const {
    if (id < 0 || static_cast<std::size_t>(id) >= meshes_.size()) return nullptr;
    return &meshes_[static_cast<std::size_t>(id)].data;
}

const ImageData* AssetLibrary::image(ImageId id) const {
    if (id < 0 || static_cast<std::size_t>(id) >= images_.size()) return nullptr;
    return &images_[static_cast<std::size_t>(id)].data;
}

std::uint64_t AssetLibrary::meshRevision(MeshId id) const {
    if (id < 0 || static_cast<std::size_t>(id) >= meshes_.size()) return 0;
    return meshes_[static_cast<std::size_t>(id)].revision;
}

std::uint64_t AssetLibrary::imageRevision(ImageId id) const {
    if (id < 0 || static_cast<std::size_t>(id) >= images_.size()) return 0;
    return images_[static_cast<std::size_t>(id)].revision;
}

}  // namespace dmxviz::assets
