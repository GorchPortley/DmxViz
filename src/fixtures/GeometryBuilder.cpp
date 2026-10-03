#include "fixtures/GeometryBuilder.h"

#include <algorithm>

namespace dmxviz::fixtures {
namespace {

using namespace geometry_names;

float orDefault(float v, float fallback) { return v > 0.0f ? v : fallback; }

glm::ivec3 gridExtent(const std::vector<PixelCell>& pixels) {
    glm::ivec3 extent{1};
    for (const PixelCell& p : pixels) extent = glm::max(extent, p.gridPosition);
    return extent;
}

// The light-emitting part: a body with either one beam at its front face (-Y)
// or a grid of pixel cells on that face.
Geometry makeEmitterPart(const GeometryRecipe& r, const glm::vec3& size, const char* name, GeometryType type) {
    Geometry body;
    body.name = name;
    body.type = type;
    body.model.primitive = r.pixels.empty() ? r.bodyShape : PrimitiveShape::Box;
    body.model.size = size;

    const float face = -0.5f * size.y;
    if (r.pixels.empty()) {
        Geometry beam;
        beam.name = kBeam;
        beam.type = GeometryType::Beam;
        beam.position = {0.0f, face, 0.0f};
        beam.beam = r.beam;
        const float maxRadius = 0.45f * std::min(size.x, size.z);
        beam.beam.lensRadius = std::min(r.beam.lensRadius, maxRadius);
        if (beam.beam.type == BeamType::Rectangle)
            beam.beam.emitterSize = glm::min(r.beam.emitterSize, glm::vec2(size.x, size.z) * 0.95f);
        body.children.push_back(std::move(beam));
        return body;
    }

    const glm::ivec3 extent = gridExtent(r.pixels);
    const float pitchX = orDefault(r.pixelPitch.x, size.x / static_cast<float>(extent.x));
    const float pitchZ = orDefault(r.pixelPitch.y, size.z / static_cast<float>(extent.y));
    const float cellDepth = std::min(0.01f, 0.2f * size.y);
    for (const PixelCell& p : r.pixels) {
        Geometry cell;
        cell.name = pixelGeometryName(p.key);
        cell.type = GeometryType::Beam;
        const float cx = (static_cast<float>(p.gridPosition.x - 1) - 0.5f * static_cast<float>(extent.x - 1)) * pitchX;
        // Rows count top to bottom; "up" of a hanging fixture's face is +Z.
        const float cz = -(static_cast<float>(p.gridPosition.y - 1) - 0.5f * static_cast<float>(extent.y - 1)) * pitchZ;
        // Deeper layers (z) sit slightly further back in the body.
        const float layer = static_cast<float>(p.gridPosition.z - 1) * 0.25f * size.y;
        cell.position = {cx, face - 0.5f * cellDepth + layer, cz};
        cell.model.primitive = PrimitiveShape::Box;
        cell.model.size = {0.85f * pitchX, cellDepth, 0.85f * pitchZ};
        cell.model.color = glm::vec3(0.06f);
        cell.beam = r.beam;
        cell.beam.lensRadius = std::min(r.beam.lensRadius, 0.4f * std::min(pitchX, pitchZ));
        if (cell.beam.type == BeamType::Rectangle) cell.beam.emitterSize = {0.8f * pitchX, 0.8f * pitchZ};
        body.children.push_back(std::move(cell));
    }
    return body;
}

}  // namespace

std::string pixelGeometryName(const std::string& pixelKey) { return "Pixel " + pixelKey; }

Geometry buildFixtureGeometry(const GeometryRecipe& r) {
    const glm::ivec3 extent = gridExtent(r.pixels);
    if (!r.movingHead) {
        glm::vec3 size = r.size;
        if (r.pixels.empty()) {
            size = {orDefault(size.x, 0.25f), orDefault(size.y, 0.3f), orDefault(size.z, 0.25f)};
        } else {
            size = {orDefault(size.x, 0.1f * static_cast<float>(extent.x)), orDefault(size.y, 0.08f),
                    orDefault(size.z, 0.1f * static_cast<float>(extent.y))};
        }
        return makeEmitterPart(r, size, kBody, GeometryType::Generic);
    }

    // Moving head, hanging: base at the top, yoke below, head between the yoke arms.
    const glm::vec3 total{orDefault(r.size.x, 0.36f), orDefault(r.size.y, 0.5f), orDefault(r.size.z, 0.3f)};
    const float baseHeight = 0.22f * total.y;
    const float yokeHeight = total.y - baseHeight;

    Geometry base;
    base.name = kBase;
    base.position = {0.0f, -0.5f * baseHeight, 0.0f};
    base.model = {PrimitiveShape::Base, {total.x, baseHeight, total.z}};

    Geometry yoke;
    yoke.name = kYoke;
    yoke.type = GeometryType::Axis;
    yoke.position = {0.0f, -0.5f * baseHeight - 0.5f * yokeHeight, 0.0f};
    yoke.model = {PrimitiveShape::Yoke, {0.92f * total.x, yokeHeight, std::min(0.45f * total.z, 0.3f * total.x)}};

    glm::vec3 headSize;
    if (r.pixels.empty())
        headSize = {0.68f * total.x, 0.7f * yokeHeight, std::min(0.68f * total.x, 0.9f * total.z)};
    else  // a tilting bar: the "head" is the bar itself
        headSize = {0.75f * total.x, std::min(0.5f * yokeHeight, 0.9f * total.z), 0.7f * yokeHeight};
    GeometryRecipe headRecipe = r;
    headRecipe.bodyShape = r.pixels.empty() ? PrimitiveShape::Head : PrimitiveShape::Box;
    Geometry head = makeEmitterPart(headRecipe, headSize, kHead, GeometryType::Axis);
    // The tilt pivot sits a little below the middle of the yoke arms.
    head.position = {0.0f, -0.1f * yokeHeight, 0.0f};

    yoke.children.push_back(std::move(head));
    base.children.push_back(std::move(yoke));
    return base;
}

}  // namespace dmxviz::fixtures
