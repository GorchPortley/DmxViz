#include "stage/SetBuilder.h"

#include "assets/Primitives.h"
#include "stage/GeometryUtil.h"

#include <algorithm>
#include <cmath>
#include <format>

namespace dmxviz::stage::setpiece {
namespace {

using assets::MeshData;

constexpr float kPanelGap = 0.004f;  // visible seam between deck panels
constexpr float kFlatSkin = 0.006f;
constexpr float kBatten = 0.07f;
constexpr float kFigureHeight = 1.8f;

Material timber() { return Material{glm::vec3(0.45f, 0.3f, 0.17f), 0.8f, 0.0f, glm::vec3(0.0f)}; }

// A unit cube from the library scaled to `size` and centred at `centre`.
RenderPart cubePart(assets::AssetLibrary& library, const glm::vec3& centre, const glm::vec3& size, const Material& mat) {
    glm::mat4 m = glm::translate(glm::mat4(1.0f), centre);
    m = glm::scale(m, size);
    return {library.builtin(assets::BuiltinMesh::Cube), m, mat};
}

}  // namespace

MeshData buildDeckPanel(const glm::vec2& panelSize, float thickness) {
    MeshData m;
    geom::addBox(m, {0.0f, -thickness * 0.5f, 0.0f},
                 {std::max(0.01f, panelSize.x - kPanelGap), thickness, std::max(0.01f, panelSize.y - kPanelGap)});
    return m;
}

MeshData buildDeckLegs(const glm::vec2& panelSize, float legHeight) {
    MeshData m;
    const float inX = std::max(0.0f, panelSize.x * 0.5f - 0.08f);
    const float inZ = std::max(0.0f, panelSize.y * 0.5f - 0.08f);
    for (float x : {-inX, inX}) {
        for (float z : {-inZ, inZ}) geom::addBox(m, {x, legHeight * 0.5f, z}, {kLegSize, legHeight, kLegSize});
    }
    return m;
}

MeshData buildSteps(float width, float height, int steps, float treadDepth, bool handrails) {
    MeshData m;
    steps = std::max(1, steps);
    const float depth = treadDepth * static_cast<float>(steps);
    for (int i = 0; i < steps; ++i) {
        const float h = height * static_cast<float>(i + 1) / static_cast<float>(steps);
        const float z = depth * 0.5f - treadDepth * (static_cast<float>(i) + 0.5f);
        geom::addBox(m, {0.0f, h * 0.5f, z}, {width, h, treadDepth});
    }
    if (handrails) {
        const float railHeight = 0.9f;
        const float r = 0.02f;
        const float zFront = depth * 0.5f - 0.05f;
        const float zBack = -depth * 0.5f + 0.05f;
        const float yFront = height / static_cast<float>(steps) + railHeight;
        const float yBack = height + railHeight;
        for (float x : {-(width * 0.5f + 0.03f), width * 0.5f + 0.03f}) {
            geom::addTube(m, {x, 0.0f, zFront}, {x, yFront, zFront}, r, 8, true);
            geom::addTube(m, {x, 0.0f, zBack}, {x, yBack, zBack}, r, 8, true);
            geom::addTube(m, {x, yFront, zFront}, {x, yBack, zBack}, r, 8, true);
        }
    }
    return m;
}

MeshData buildFlatSkin(float width, float height, float thickness) {
    MeshData m;
    geom::addBox(m, {0.0f, height * 0.5f, thickness * 0.5f - kFlatSkin * 0.5f}, {width, height, kFlatSkin});
    return m;
}

MeshData buildFlatFrame(float width, float height, float thickness) {
    MeshData m;
    const float depth = std::max(0.01f, thickness - kFlatSkin);
    const float z = thickness * 0.5f - kFlatSkin - depth * 0.5f;
    const float b = std::min(kBatten, std::min(width, height) * 0.25f);
    // Stiles, rails and a middle toggle rail behind the skin.
    for (float x : {-(width - b) * 0.5f, (width - b) * 0.5f}) geom::addBox(m, {x, height * 0.5f, z}, {b, height, depth});
    for (float y : {b * 0.5f, height * 0.5f, height - b * 0.5f})
        geom::addBox(m, {0.0f, y, z}, {width - 2.0f * b, b, depth});
    // Stage brace from two thirds up the back down to the floor behind the flat.
    const glm::vec3 top{0.0f, height * 0.66f, z - depth * 0.5f};
    // The foot is lifted by half the brace thickness so its tilted end stays above the floor.
    const glm::vec3 foot{0.0f, b * 0.2f, z - depth * 0.5f - height * 0.4f};
    const glm::vec3 d = top - foot;
    const float len = glm::length(d);
    // Rx(a) maps +Y to (0, cos a, sin a); align the box's long Y axis with d.
    const glm::quat rot = glm::angleAxis(std::atan2(d.z, d.y), glm::vec3(1, 0, 0));
    geom::addBox(m, (top + foot) * 0.5f, {b * 0.6f, len, b * 0.4f}, rot);
    return m;
}

MeshData buildReferenceFigure() {
    MeshData m;
    for (float x : {-0.1f, 0.1f}) {
        geom::addTube(m, {x, 0.06f, 0.0f}, {x, 0.92f, 0.0f}, 0.075f, 8, true);               // leg
        geom::addBox(m, {x, 0.03f, 0.05f}, {0.1f, 0.06f, 0.26f});                           // foot (toes to +Z)
        geom::addTube(m, {x * 2.4f, 1.42f, 0.0f}, {x * 2.7f, 0.82f, 0.02f}, 0.045f, 8, true);  // arm
    }
    geom::addBox(m, {0.0f, 1.16f, 0.0f}, {0.38f, 0.6f, 0.22f});                       // torso
    geom::addTube(m, {-0.24f, 1.42f, 0.0f}, {0.24f, 1.42f, 0.0f}, 0.06f, 8, true);     // shoulders
    geom::addTube(m, {0.0f, 1.44f, 0.0f}, {0.0f, 1.58f, 0.0f}, 0.05f, 8, true);        // neck
    m.append(assets::makeSphere(0.12f, 12, 8), glm::translate(glm::mat4(1.0f), {0.0f, kFigureHeight - 0.12f, 0.0f}));
    geom::addBox(m, {0.0f, kFigureHeight - 0.13f, 0.115f}, {0.03f, 0.04f, 0.04f});    // nose shows facing
    return m;
}

glm::vec2 deckFootprint(const StageDeckContent& deck) {
    return {static_cast<float>(std::max(1, deck.columns)) * deck.panelSize.x,
            static_cast<float>(std::max(1, deck.rows)) * deck.panelSize.y};
}

std::vector<RenderPart> deckParts(const StageDeckContent& deck, assets::AssetLibrary& library) {
    std::vector<RenderPart> parts;
    const int cols = std::max(1, deck.columns);
    const int rows = std::max(1, deck.rows);
    const glm::vec2 panel = glm::max(deck.panelSize, glm::vec2(0.05f));
    const float thickness = std::clamp(deck.thickness, 0.005f, std::max(0.005f, deck.height));
    const glm::vec2 size = deckFootprint(deck);

    const MeshId panelMesh =
        geom::cachedMesh(library,
                         std::format("stage:deck:panel:{}:{}:{}", geom::keyNum(panel.x), geom::keyNum(panel.y),
                                     geom::keyNum(thickness)),
                         [&] { return buildDeckPanel(panel, thickness); });
    const float legHeight = deck.height - thickness;
    const bool legs = deck.style == DeckStyle::Deck && deck.legs && legHeight > 0.01f;
    MeshId legMesh = kInvalidMesh;
    if (legs) {
        legMesh = geom::cachedMesh(library,
                                   std::format("stage:deck:legs:{}:{}:{}", geom::keyNum(panel.x),
                                               geom::keyNum(panel.y), geom::keyNum(legHeight)),
                                   [&] { return buildDeckLegs(panel, legHeight); });
    }
    for (int c = 0; c < cols; ++c) {
        for (int r = 0; r < rows; ++r) {
            const float x = -size.x * 0.5f + panel.x * (static_cast<float>(c) + 0.5f);
            const float z = -size.y * 0.5f + panel.y * (static_cast<float>(r) + 0.5f);
            parts.push_back({panelMesh, glm::translate(glm::mat4(1.0f), {x, deck.height, z}), deck.surface});
            if (legs) parts.push_back({legMesh, glm::translate(glm::mat4(1.0f), {x, 0.0f, z}), materials::aluminium()});
        }
    }
    // A riser is closed by fascia boards up to its top; a deck skirt hangs
    // from just below the top surface to the floor.
    const bool riser = deck.style == DeckStyle::Riser;
    if ((riser || deck.skirt) && deck.height > 0.01f) {
        const float t = riser ? kFasciaThickness : kSkirtThickness;
        const float h = riser ? deck.height : deck.height - 0.005f;
        parts.push_back(cubePart(library, {0.0f, h * 0.5f, size.y * 0.5f + t * 0.5f}, {size.x + 2.0f * t, h, t},
                                 deck.skirtMaterial));
        parts.push_back(cubePart(library, {0.0f, h * 0.5f, -size.y * 0.5f - t * 0.5f}, {size.x + 2.0f * t, h, t},
                                 deck.skirtMaterial));
        parts.push_back(
            cubePart(library, {size.x * 0.5f + t * 0.5f, h * 0.5f, 0.0f}, {t, h, size.y}, deck.skirtMaterial));
        parts.push_back(
            cubePart(library, {-size.x * 0.5f - t * 0.5f, h * 0.5f, 0.0f}, {t, h, size.y}, deck.skirtMaterial));
    }
    return parts;
}

std::vector<RenderPart> stepsParts(const StepsContent& s, assets::AssetLibrary& library) {
    const int n = s.effectiveSteps();
    const float width = std::max(0.05f, s.width);
    const float height = std::max(0.01f, s.height);
    const float tread = std::max(0.05f, s.treadDepth);
    const MeshId mesh = geom::cachedMesh(
        library,
        std::format("stage:steps:{}:{}:{}:{}:{}", geom::keyNum(width), geom::keyNum(height), n, geom::keyNum(tread),
                    s.handrails ? 1 : 0),
        [&] { return buildSteps(width, height, n, tread, s.handrails); });
    return {{mesh, glm::mat4(1.0f), s.material}};
}

std::vector<RenderPart> wallParts(const WallContent& w, assets::AssetLibrary& library) {
    const float width = std::max(0.01f, w.width);
    const float height = std::max(0.01f, w.height);
    const float thickness = std::max(0.005f, w.thickness);
    if (w.style == WallStyle::Wall)
        return {cubePart(library, {0.0f, height * 0.5f, 0.0f}, {width, height, thickness}, w.material)};
    const std::string dims = std::format("{}:{}:{}", geom::keyNum(width), geom::keyNum(height), geom::keyNum(thickness));
    const MeshId skin = geom::cachedMesh(library, "stage:flat:skin:" + dims,
                                         [&] { return buildFlatSkin(width, height, thickness); });
    const MeshId frame = geom::cachedMesh(library, "stage:flat:frame:" + dims,
                                          [&] { return buildFlatFrame(width, height, thickness); });
    return {{skin, glm::mat4(1.0f), w.material}, {frame, glm::mat4(1.0f), timber()}};
}

std::vector<RenderPart> figureParts(const ReferenceFigureContent& f, assets::AssetLibrary& library) {
    const MeshId mesh = geom::cachedMesh(library, "stage:figure", [] { return buildReferenceFigure(); });
    const float s = std::max(0.05f, f.height) / kFigureHeight;
    return {{mesh, glm::scale(glm::mat4(1.0f), glm::vec3(s)), f.material}};
}

}  // namespace dmxviz::stage::setpiece
