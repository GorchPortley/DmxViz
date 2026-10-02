#include "stage/ModelCache.h"

#include "core/Log.h"
#include "stage/PathUtil.h"

#include <format>

namespace dmxviz::stage {

std::string ModelCache::cacheKey(const ModelContent& model) {
    return std::format("{}|zup={}|scale={}", model.path, model.zUp ? 1 : 0, model.unitScale);
}

const LoadedModel* ModelCache::find(const ModelContent& model) const {
    auto it = entries_.find(cacheKey(model));
    return it == entries_.end() ? nullptr : &it->second.model;
}

const LoadedModel& ModelCache::insert(const ModelContent& model, const assets::ModelData& data,
                                      assets::AssetLibrary& library) {
    const std::string key = cacheKey(model);
    Entry& e = entries_[key];
    e.library = &library;
    e.model = LoadedModel{};
    e.model.bounds = data.bounds;
    for (std::size_t i = 0; i < data.parts.size(); ++i) {
        const assets::ModelPart& p = data.parts[i];
        LoadedModel::Part part;
        part.mesh = library.addMesh(p.mesh, std::format("model:{}#{}", key, i));
        part.transform = p.transform;
        part.material = p.material;
        e.model.parts.push_back(part);
    }
    return e.model;
}

const LoadedModel& ModelCache::get(const ModelContent& model, assets::AssetLibrary& library) {
    const std::string key = cacheKey(model);
    if (auto it = entries_.find(key); it != entries_.end() && it->second.library == &library) return it->second.model;

    assets::ModelLoadOptions opts;
    opts.zUp = model.zUp;
    opts.unitScale = model.unitScale;
    std::string error;
    std::optional<assets::ModelData> data;
    if (model.path.empty())
        error = "no model file set";
    else
        data = assets::loadModel(pathFromUtf8(model.path), opts, &error);
    if (!data) {
        log::warn("stage", "cannot load model '{}': {}", model.path, error);
        Entry& e = entries_[key];
        e.library = &library;
        e.model = LoadedModel{};
        e.model.error = error.empty() ? "unknown error" : error;
        return e.model;
    }
    return insert(model, *data, library);
}

}  // namespace dmxviz::stage
