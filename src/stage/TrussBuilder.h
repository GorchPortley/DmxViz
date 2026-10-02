#pragma once
// Procedural aluminium truss: straight segments, corner blocks, arcs and
// ground-support towers, for any TrussProfile.
//
// Coordinates (truss local space, metres):
//   * straight segment: along X, centred on the origin (x = -L/2 .. +L/2);
//   * corner block: a cube of the profile's size centred on the origin;
//   * arc: horizontal, around the origin, centre line at `radius`, starting
//     on +X and turning toward -Z (counter-clockwise seen from above);
//   * tower: standing on the floor at the origin, growing along +Y.
//
// The build*() functions are pure and return a mesh. The *Mesh() functions
// register the mesh in the AssetLibrary under a key made of the parameters,
// so identical pieces (every 3 m F34 segment of a rig) share one mesh.

#include "assets/AssetLibrary.h"
#include "assets/MeshData.h"
#include "stage/Node.h"
#include "stage/TrussProfile.h"

#include <cstdint>
#include <vector>

namespace dmxviz::stage::truss {

// Tessellation. Chords are visible tubes; braces are thin, fewer sides suffice.
constexpr int kChordSides = 10;
constexpr int kBraceSides = 6;
constexpr float kBasePlateThickness = 0.012f;
constexpr float kSleeveHeight = 0.6f;

assets::MeshData buildStraight(const TrussProfile& profile, float length);
assets::MeshData buildCornerBlock(const TrussProfile& profile, std::uint8_t faces);
// One arc piece from angle 0 to `angleRad`.
assets::MeshData buildArcPiece(const TrussProfile& profile, float radius, float angleRad);
assets::MeshData buildBasePlate(float size);
// Sleeve block that slides on a tower, centred on the origin (Y up).
assets::MeshData buildSleeveBlock(const TrussProfile& profile);

MeshId straightMesh(assets::AssetLibrary& library, const TrussProfile& profile, float length);
MeshId cornerMesh(assets::AssetLibrary& library, const TrussProfile& profile, std::uint8_t faces);
MeshId arcPieceMesh(assets::AssetLibrary& library, const TrussProfile& profile, float radius, float angleRad);
MeshId basePlateMesh(assets::AssetLibrary& library, float size);
MeshId sleeveBlockMesh(assets::AssetLibrary& library, const TrussProfile& profile);

// Splits a run into segment lengths. With explicit segments those are used
// (scaled to sum to `length`); otherwise standard lengths, longest first, with
// a final shorter piece (>= 0.25 m) taking up the remainder.
std::vector<float> splitRun(float length, const std::vector<float>& explicitSegments = {});

// All meshes of a truss node, in node space.
std::vector<RenderPart> trussParts(const TrussContent& truss, assets::AssetLibrary& library);

// A straight piece of chord centre line in node space (used to hang fixtures).
struct ChordLine {
    glm::vec3 a{0.0f};
    glm::vec3 b{0.0f};
};
std::vector<ChordLine> trussChords(const TrussContent& truss);
float trussChordRadius(const TrussContent& truss);

}  // namespace dmxviz::stage::truss
