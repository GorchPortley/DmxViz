#pragma once
// Procedural set pieces: stage decks and risers, steps, walls/flats, the
// floor and a 1.8 m reference person. Like the truss builder, build*()
// functions are pure and *Parts() functions register meshes in the
// AssetLibrary keyed by their parameters so identical pieces share a mesh.

#include "assets/AssetLibrary.h"
#include "assets/MeshData.h"
#include "stage/Node.h"

#include <vector>

namespace dmxviz::stage::setpiece {

constexpr float kLegSize = 0.05f;        // square deck legs
constexpr float kSkirtThickness = 0.01f;
constexpr float kFasciaThickness = 0.02f;

// One deck panel top, centred on the origin (x/z) with its top surface at y = 0.
assets::MeshData buildDeckPanel(const glm::vec2& panelSize, float thickness);
// The four legs of one panel standing on y = 0, `legHeight` tall.
assets::MeshData buildDeckLegs(const glm::vec2& panelSize, float legHeight);
// Solid steps rising toward -Z, footprint centred on the origin, standing on y = 0.
assets::MeshData buildSteps(float width, float height, int steps, float treadDepth, bool handrails);
// Flat: a thin skin facing +Z on a timber frame with a rear brace, standing on y = 0.
assets::MeshData buildFlatSkin(float width, float height, float thickness);
assets::MeshData buildFlatFrame(float width, float height, float thickness);
// A 1.8 m tall low-poly person standing on y = 0, facing +Z.
assets::MeshData buildReferenceFigure();

std::vector<RenderPart> deckParts(const StageDeckContent& deck, assets::AssetLibrary& library);
std::vector<RenderPart> stepsParts(const StepsContent& steps, assets::AssetLibrary& library);
std::vector<RenderPart> wallParts(const WallContent& wall, assets::AssetLibrary& library);
std::vector<RenderPart> figureParts(const ReferenceFigureContent& figure, assets::AssetLibrary& library);

// Total footprint of a deck (x = width, y = depth).
glm::vec2 deckFootprint(const StageDeckContent& deck);

}  // namespace dmxviz::stage::setpiece
