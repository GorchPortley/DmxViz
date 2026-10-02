#pragma once
// Loads model files (glTF/OBJ/3DS) for Model nodes into the AssetLibrary once
// and remembers the resulting mesh ids, so many nodes can share one file.

#include "assets/AssetLibrary.h"
#include "assets/ModelLoader.h"
#include "stage/NodeContent.h"

#include <string>
#include <unordered_map>
#include <vector>

namespace dmxviz::stage {

// A model file after loading: one mesh per part, placed in model space.
struct LoadedModel {
    struct Part {
        MeshId mesh = kInvalidMesh;
        glm::mat4 transform{1.0f};
        Material material;
    };
    std::vector<Part> parts;
    Aabb bounds;
    std::string error;  // why loading failed; empty on success

    bool ok() const { return error.empty(); }
};

// Cache of loaded model files keyed by path + import settings. Failed loads
// are cached too (with their error) so a missing file is not retried every
// frame; call clear() to retry / reload changed files.
class ModelCache {
public:
    // Loads on first use. Never fails: check LoadedModel::ok().
    const LoadedModel& get(const ModelContent& model, assets::AssetLibrary& library);
    // Returns the cached result without loading, or nullptr.
    const LoadedModel* find(const ModelContent& model) const;
    // Registers already-decoded model data under `model`'s key (GDTF parts,
    // tests, models decoded by another loader).
    const LoadedModel& insert(const ModelContent& model, const assets::ModelData& data,
                              assets::AssetLibrary& library);
    void clear() { entries_.clear(); }

    static std::string cacheKey(const ModelContent& model);

private:
    struct Entry {
        LoadedModel model;
        const assets::AssetLibrary* library = nullptr;
    };
    std::unordered_map<std::string, Entry> entries_;
};

}  // namespace dmxviz::stage
