#pragma once
// Generates a plausible geometry tree for fixtures that come without one
// (Open Fixture Library imports, "new fixture" in the editor).
//
//   moving head:  Base ─ Yoke (Axis, pan) ─ Head (Axis, tilt) ─ beam(s)
//   static:       Body ─ beam(s)
//
// The beam part is either one Beam node or a grid of pixel cells (LED bars,
// matrices, multi-cell blinders), each cell being its own Beam node.
// The result follows the hanging convention: origin at the top (rigging
// point), beams pointing down (-Y), +Z = "up" of gobos and pixel rows.

#include "fixtures/FixtureType.h"

#include <string>
#include <vector>

namespace dmxviz::fixtures {

namespace geometry_names {
constexpr const char* kBase = "Base";
constexpr const char* kYoke = "Yoke";
constexpr const char* kHead = "Head";
constexpr const char* kBody = "Body";
constexpr const char* kBeam = "Beam";
}  // namespace geometry_names

// Name of the geometry node of a pixel cell ("Pixel 3", "Pixel Top-Left").
std::string pixelGeometryName(const std::string& pixelKey);

struct PixelCell {
    std::string key;                 // unique cell key (OFL pixel key)
    glm::ivec3 gridPosition{1};      // 1-based column (x), row (y, top to bottom), layer (z)
};

struct GeometryRecipe {
    bool movingHead = false;  // base + yoke + head with pan/tilt axes
    // Overall size in DmxViz axes (X width, Y height along the hanging beam, Z depth), metres.
    // Zero components are guessed.
    glm::vec3 size{0.0f};
    PrimitiveShape bodyShape = PrimitiveShape::Conventional;  // static fixtures / moving head "head"
    BeamSpec beam;                   // template for the beam (or every cell)
    std::vector<PixelCell> pixels;   // empty: a single beam
    glm::vec2 pixelPitch{0.0f};      // m between cell centres along X / Z (0 = spread over the body)
};

Geometry buildFixtureGeometry(const GeometryRecipe& recipe);

}  // namespace dmxviz::fixtures
