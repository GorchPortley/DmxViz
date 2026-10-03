#include "fixtures/FixtureAssets.h"

#include "assets/ImageLoader.h"
#include "assets/ModelLoader.h"
#include "assets/Primitives.h"
#include "core/Log.h"

#include <cmath>
#include <format>

namespace dmxviz::fixtures {
namespace {

void warn(std::vector<std::string>* warnings, std::string message) {
    log::warn("fixtures", "{}", message);
    if (warnings) warnings->push_back(std::move(message));
}

// Cylinder along Y with an elliptical cross-section filling size.x / size.z.
assets::MeshData ellipticCylinder(const glm::vec3& size, float yCenter = 0.0f) {
    assets::MeshData out;
    const float r = 0.5f * std::max(1e-4f, std::min(size.x, size.z));
    const assets::MeshData unit = assets::makeCylinder(r, std::max(1e-4f, size.y), 32);
    const glm::mat4 m = glm::translate(glm::mat4(1.0f), {0.0f, yCenter, 0.0f}) *
                        glm::scale(glm::mat4(1.0f), {0.5f * size.x / r, 1.0f, 0.5f * size.z / r});
    out.append(unit, m);
    return out;
}

assets::MeshData boxAt(const glm::vec3& size, const glm::vec3& center) {
    assets::MeshData out;
    out.append(assets::makeBox(size), glm::translate(glm::mat4(1.0f), center));
    return out;
}

std::string primitiveKey(PrimitiveShape shape, const glm::vec3& size) {
    // Millimetre resolution is plenty to share identical parts between fixture types.
    return std::format("primitive:{}:{}x{}x{}", primitiveShapeName(shape), std::lround(size.x * 1000.0f),
                       std::lround(size.y * 1000.0f), std::lround(size.z * 1000.0f));
}

Material bodyMaterial(const glm::vec3& albedo) {
    Material m;
    m.albedo = albedo;
    m.roughness = 0.45f;
    m.metallic = 0.15f;
    return m;
}

struct ResolverContext {
    const FixtureType* type = nullptr;
};

// Supplies files referenced from a glTF (external .bin buffers, textures) out of
// the fixture's resource table, matching by name with or without extension.
std::optional<std::vector<std::uint8_t>> resolveResource(void* user, std::string_view uri) {
    const auto* ctx = static_cast<const ResolverContext*>(user);
    std::string_view file = uri;
    if (auto slash = file.find_last_of('/'); slash != std::string_view::npos) file.remove_prefix(slash + 1);
    for (const Resource& r : ctx->type->resources) {
        if (r.name == uri || r.name == file || r.name + "." + r.format == file) return r.data;
    }
    return std::nullopt;
}

}  // namespace

bool isImageFormat(std::string_view f) {
    return f == "png" || f == "svg" || f == "jpg" || f == "jpeg" || f == "bmp" || f == "tga" || f == "gif";
}

bool isModelFormat(std::string_view f) { return f == "glb" || f == "gltf" || f == "3ds" || f == "obj"; }

assets::MeshData makePrimitiveMesh(PrimitiveShape shape, const glm::vec3& rawSize) {
    const glm::vec3 size = glm::max(rawSize, glm::vec3(1e-3f));
    assets::MeshData mesh;
    switch (shape) {
        case PrimitiveShape::None: break;
        case PrimitiveShape::Box: mesh = assets::makeBox(size); break;
        case PrimitiveShape::Cylinder:
        case PrimitiveShape::Head: mesh = ellipticCylinder(size); break;
        case PrimitiveShape::Sphere: {
            mesh.append(assets::makeSphere(0.5f, 32, 16), glm::scale(glm::mat4(1.0f), size));
            break;
        }
        case PrimitiveShape::Base: {
            // Housing with the pan bearing underneath (hanging: bearing points down).
            const float housing = 0.85f * size.y;
            mesh.append(boxAt({size.x, housing, size.z}, {0.0f, 0.5f * size.y - 0.5f * housing, 0.0f}));
            const float d = 0.6f * std::min(size.x, size.z);
            mesh.append(ellipticCylinder({d, size.y - housing, d}, -0.5f * size.y + 0.5f * (size.y - housing)));
            break;
        }
        case PrimitiveShape::Yoke: {
            // U-shape: cross bar at the top, two arms hanging down.
            const float bar = 0.12f * size.y;
            const float arm = 0.14f * size.x;
            mesh.append(boxAt({size.x, bar, size.z}, {0.0f, 0.5f * size.y - 0.5f * bar, 0.0f}));
            for (float side : {-1.0f, 1.0f})
                mesh.append(boxAt({arm, size.y - bar, size.z}, {side * 0.5f * (size.x - arm), -0.5f * bar, 0.0f}));
            break;
        }
        case PrimitiveShape::Conventional: {
            // Par can / lantern: body plus a slightly wider front rim at -Y.
            const float rim = 0.12f * size.y;
            mesh.append(ellipticCylinder({0.9f * size.x, size.y - rim, 0.9f * size.z}, 0.5f * rim));
            mesh.append(ellipticCylinder({size.x, rim, size.z}, -0.5f * size.y + 0.5f * rim));
            break;
        }
    }
    mesh.computeBounds();
    return mesh;
}

FixtureAssets FixtureAssets::realize(const FixtureType& type, assets::AssetLibrary& library,
                                     std::vector<std::string>* warnings) {
    FixtureAssets out;
    out.lensDisc_ = library.builtin(assets::BuiltinMesh::Disc);
    out.lensPlane_ = library.builtin(assets::BuiltinMesh::Plane);
    const std::string prefix = "fixture:" + type.id + "/";

    // --- images (gobos, animation wheels) ---------------------------------
    for (const Resource& r : type.resources) {
        if (!isImageFormat(r.format)) continue;
        assets::ImageLoadOptions opts;
        opts.desiredChannels = 1;  // gobos are light masks: white passes
        opts.svgSize = 256;        // matches the renderer's gobo atlas layers
        std::string err;
        auto image = assets::loadImageFromMemory(r.data, r.format, opts, &err);
        if (!image) {
            warn(warnings, std::format("{}: image \"{}\" could not be decoded: {}", type.id, r.name, err));
            continue;
        }
        out.images_[r.name] = library.addImage(std::move(*image), prefix + r.name);
    }

    // --- meshes ----------------------------------------------------------
    ResolverContext resolver{&type};
    forEachGeometry(type.geometry, [&](const Geometry& g, const Geometry*) {
        const ModelSpec& model = g.model;
        if (model.empty()) return;
        std::vector<GeometryPart>& parts = out.parts_[g.name];

        if (!model.mesh.empty()) {
            const Resource* res = type.findResource(model.mesh);
            std::optional<assets::ModelData> data;
            std::string err = "resource missing";
            if (res) {
                assets::ModelLoadOptions opts;
                // 3DS files in GDTF archives are Z-up millimetres; glTF is Y-up metres by definition.
                if (res->format == "3ds") {
                    opts.zUp = true;
                    opts.unitScale = 0.001f;
                }
                data = assets::loadModelFromMemory(res->data, res->format, opts, &err, &resolveResource, &resolver);
            }
            if (data && !data->parts.empty()) {
                // Stretch the model to the declared size (GDTF models carry Length/Width/Height).
                glm::vec3 fit{1.0f};
                const glm::vec3 bounds = data->bounds.size();
                for (int i = 0; i < 3; ++i)
                    if (model.size[i] > 1e-4f && bounds[i] > 1e-4f && std::abs(model.size[i] / bounds[i] - 1.0f) > 0.02f)
                        fit[i] = model.size[i] / bounds[i];
                const glm::mat4 fitMatrix = glm::scale(glm::mat4(1.0f), fit);
                for (std::size_t i = 0; i < data->parts.size(); ++i) {
                    assets::ModelPart& part = data->parts[i];
                    GeometryPart gp;
                    gp.transform = fitMatrix * part.transform;
                    gp.material = part.material;
                    gp.mesh = library.addMesh(std::move(part.mesh), std::format("{}{}#{}", prefix, model.mesh, i));
                    parts.push_back(gp);
                }
                return;
            }
            warn(warnings, std::format("{}: model \"{}\" of geometry \"{}\" not loaded ({}); using a primitive",
                                       type.id, model.mesh, g.name, err));
        }

        PrimitiveShape shape = model.primitive == PrimitiveShape::None ? PrimitiveShape::Box : model.primitive;
        if (glm::any(glm::lessThanEqual(model.size, glm::vec3(0.0f)))) {
            warn(warnings, std::format("{}: geometry \"{}\" has no model size; nothing drawn", type.id, g.name));
            return;
        }
        const std::string key = primitiveKey(shape, model.size);
        MeshId mesh = library.findMesh(key);
        if (mesh == kInvalidMesh) mesh = library.addMesh(makePrimitiveMesh(shape, model.size), key);
        parts.push_back({mesh, glm::mat4(1.0f), bodyMaterial(model.color)});
    });
    return out;
}

std::span<const GeometryPart> FixtureAssets::parts(std::string_view geometryName) const {
    auto it = parts_.find(geometryName);
    if (it == parts_.end()) return {};
    return it->second;
}

ImageId FixtureAssets::image(std::string_view resourceName) const {
    auto it = images_.find(resourceName);
    return it == images_.end() ? kInvalidImage : it->second;
}

}  // namespace dmxviz::fixtures
