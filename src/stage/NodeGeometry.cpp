#include "stage/NodeGeometry.h"

#include "stage/SetBuilder.h"
#include "stage/TrussBuilder.h"

#include <type_traits>

namespace dmxviz::stage {
namespace {

using assets::BuiltinMesh;

std::vector<RenderPart> primitiveParts(const PrimitiveContent& p, assets::AssetLibrary& library) {
    BuiltinMesh mesh = BuiltinMesh::Cube;
    glm::vec3 scale = p.size;
    switch (p.shape) {
        case PrimitiveShape::Box: mesh = BuiltinMesh::Cube; break;
        case PrimitiveShape::Sphere: mesh = BuiltinMesh::Sphere; break;
        case PrimitiveShape::Cylinder: mesh = BuiltinMesh::Cylinder; break;
        case PrimitiveShape::Cone: mesh = BuiltinMesh::Cone; break;
        case PrimitiveShape::Plane:
            mesh = BuiltinMesh::Plane;
            scale.y = 1.0f;
            break;
        case PrimitiveShape::Disc:
            mesh = BuiltinMesh::Disc;
            scale.y = 1.0f;
            break;
    }
    return {{library.builtin(mesh), glm::scale(glm::mat4(1.0f), scale), p.material}};
}

std::vector<RenderPart> modelParts(const ModelContent& m, assets::AssetLibrary& library, ModelCache& models) {
    const LoadedModel& loaded = models.get(m, library);
    std::vector<RenderPart> parts;
    if (!loaded.ok()) {
        const Material marker{glm::vec3(0.8f, 0.05f, 0.05f), 0.5f, 0.0f, glm::vec3(0.4f, 0.0f, 0.0f)};
        parts.push_back({library.builtin(BuiltinMesh::Cube),
                         glm::scale(glm::translate(glm::mat4(1.0f), {0.0f, kMissingModelMarkerSize * 0.5f, 0.0f}),
                                    glm::vec3(kMissingModelMarkerSize)),
                         marker});
        return parts;
    }
    parts.reserve(loaded.parts.size());
    for (const LoadedModel::Part& p : loaded.parts) parts.push_back({p.mesh, p.transform, p.material});
    return parts;
}

}  // namespace

std::vector<RenderPart> buildRenderParts(const NodeData& data, assets::AssetLibrary& library, ModelCache& models) {
    std::vector<RenderPart> parts = std::visit(
        [&](const auto& c) -> std::vector<RenderPart> {
            using T = std::decay_t<decltype(c)>;
            if constexpr (std::is_same_v<T, PrimitiveContent>) {
                return primitiveParts(c, library);
            } else if constexpr (std::is_same_v<T, ModelContent>) {
                return modelParts(c, library, models);
            } else if constexpr (std::is_same_v<T, TrussContent>) {
                return truss::trussParts(c, library);
            } else if constexpr (std::is_same_v<T, StageDeckContent>) {
                return setpiece::deckParts(c, library);
            } else if constexpr (std::is_same_v<T, StepsContent>) {
                return setpiece::stepsParts(c, library);
            } else if constexpr (std::is_same_v<T, WallContent>) {
                return setpiece::wallParts(c, library);
            } else if constexpr (std::is_same_v<T, ReferenceFigureContent>) {
                return setpiece::figureParts(c, library);
            } else {
                // Group, Fixture (drawn by the simulation), CameraPreset, Unknown.
                return {};
            }
        },
        data.content);
    if (data.materialOverride) {
        for (RenderPart& p : parts) p.material = *data.materialOverride;
    }
    return parts;
}

}  // namespace dmxviz::stage
