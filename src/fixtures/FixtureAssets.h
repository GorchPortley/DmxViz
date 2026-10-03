#pragma once
// Turns a FixtureType's models and images into AssetLibrary meshes/images.
//
// A FixtureType only holds raw bytes (and primitive shape descriptions); the
// renderer needs MeshIds and ImageIds. FixtureAssets::realize() does that
// conversion once per fixture type and is what a FixtureRuntime needs to emit
// MeshInstances and gobo images.
//
// Asset keys are stable ("fixture:<id>/<resource>", "primitive:<shape>:<size>")
// so realising the same type again replaces assets in place instead of
// duplicating them, and identical primitive parts are shared between types.
//
// Mesh files go through assets::loadModelFromMemory(); when that fails (format
// not supported yet, corrupt file) the geometry falls back to its primitive
// shape, or to a box of the model's size.

#include "assets/AssetLibrary.h"
#include "core/SceneTypes.h"
#include "fixtures/FixtureType.h"

#include <map>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace dmxviz::fixtures {

// One drawable piece of a geometry node.
struct GeometryPart {
    MeshId mesh = kInvalidMesh;
    glm::mat4 transform{1.0f};  // relative to the geometry node
    Material material;
};

class FixtureAssets {
public:
    static FixtureAssets realize(const FixtureType& type, assets::AssetLibrary& library,
                                 std::vector<std::string>* warnings = nullptr);

    // Drawable parts of a geometry node (empty for invisible nodes).
    std::span<const GeometryPart> parts(std::string_view geometryName) const;
    // Image of a resource (gobo, animation wheel); kInvalidImage if unknown or undecodable.
    ImageId image(std::string_view resourceName) const;

    // Unit meshes used for emissive lens faces (disc r = 0.5, plane 1 x 1, both facing +Y).
    MeshId lensDisc() const { return lensDisc_; }
    MeshId lensPlane() const { return lensPlane_; }

private:
    std::map<std::string, std::vector<GeometryPart>, std::less<>> parts_;
    std::map<std::string, ImageId, std::less<>> images_;
    MeshId lensDisc_ = kInvalidMesh;
    MeshId lensPlane_ = kInvalidMesh;
};

// Mesh of a built-in primitive shape filling `size`, centred on the origin.
assets::MeshData makePrimitiveMesh(PrimitiveShape shape, const glm::vec3& size);

bool isImageFormat(std::string_view format);
bool isModelFormat(std::string_view format);

}  // namespace dmxviz::fixtures
