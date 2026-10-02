#pragma once
// Owns every mesh and image in a session (stage models, fixture parts, gobos).
// Main-thread only. Other modules refer to assets by MeshId / ImageId; the
// renderer mirrors them on the GPU and uses the revision counters to notice
// additions and replacements.

#include "assets/MeshData.h"
#include "core/Id.h"

#include <cstdint>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

namespace dmxviz::assets {

// Unit-sized meshes registered at construction (1 m box, r = 0.5 sphere, ...).
enum class BuiltinMesh : int { Cube, Sphere, Cylinder, Cone, Plane, Disc, Count };

class AssetLibrary {
public:
    AssetLibrary();

    // Adds an asset. A non-empty key makes it findable (e.g. a file path or
    // "gdtf:<fixture>/<model>"); adding with an existing key replaces that asset
    // in place and returns the existing id.
    MeshId addMesh(MeshData mesh, std::string key = {});
    ImageId addImage(ImageData image, std::string key = {});

    MeshId findMesh(std::string_view key) const;
    ImageId findImage(std::string_view key) const;

    const MeshData* mesh(MeshId id) const;
    const ImageData* image(ImageId id) const;
    std::size_t meshCount() const { return meshes_.size(); }
    std::size_t imageCount() const { return images_.size(); }

    MeshId builtin(BuiltinMesh which) const { return builtins_[static_cast<int>(which)]; }

    // Incremented on every add/replace. Per-asset revisions say which one changed.
    std::uint64_t revision() const { return revision_; }
    std::uint64_t meshRevision(MeshId id) const;
    std::uint64_t imageRevision(ImageId id) const;

private:
    template <typename T>
    struct Slot {
        T data;
        std::string key;
        std::uint64_t revision = 0;
    };

    std::vector<Slot<MeshData>> meshes_;
    std::vector<Slot<ImageData>> images_;
    std::unordered_map<std::string, MeshId> meshKeys_;
    std::unordered_map<std::string, ImageId> imageKeys_;
    MeshId builtins_[static_cast<int>(BuiltinMesh::Count)]{};
    std::uint64_t revision_ = 0;
};

}  // namespace dmxviz::assets
