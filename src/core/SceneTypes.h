#pragma once
// Per-frame render data shared between the fixtures module (producer) and the
// render module (consumer). These are plain structs on purpose: the simulation
// fills flat arrays every frame and the renderer uploads them in bulk.
//
// This file is a cross-module CONTRACT. Change it only through the integration
// owner (see CLAUDE.md) because both sides must be updated together.

#include "core/Id.h"
#include "core/Math.h"

#include <array>
#include <cstdint>

namespace dmxviz {

// Simple metal/roughness surface description.
struct Material {
    glm::vec3 albedo{0.5f};    // linear RGB base colour
    float roughness = 0.6f;    // 0 = mirror, 1 = fully diffuse
    float metallic = 0.0f;
    glm::vec3 emissive{0.0f};  // linear RGB radiance added on top of lighting (LED pixels, lenses)
};

enum class Highlight : std::uint8_t { None = 0, Hover = 1, Selected = 2 };

// One mesh to draw this frame. Fixture bodies (yoke, head...) are emitted as
// MeshInstances by the fixture runtime; static stage geometry by the stage.
struct MeshInstance {
    MeshId mesh = kInvalidMesh;
    glm::mat4 world{1.0f};
    Material material;
    NodeId pickId = kInvalidNode;  // scene node this belongs to (for picking/highlight)
    Highlight highlight = Highlight::None;
    bool castsShadow = true;
};

// How the light leaves the lens. Drives both the beam volume and surface lighting.
enum class BeamShape : std::uint8_t {
    Spot,       // hard-edged profile/spot optics, gobos are projected sharply
    Wash,       // soft-edged wash/PC/fresnel optics
    Beam,       // very narrow, very bright "beam" fixture (Sharpy-like)
    Rectangle,  // rectangular emitter (LED panel, blinder cell); uses emitterSize
    Glow,       // emitter only: lights the face (LED pixel, strobe face) but no haze beam
};

constexpr int kMaxGobosPerBeam = 2;
constexpr int kMaxPrismFacets = 16;
constexpr int kMaxBlades = 4;

struct GoboState {
    ImageId image = kInvalidImage;  // greyscale/RGBA gobo image; kInvalidImage = open
    float rotation = 0.0f;          // radians, current absolute angle (index + spin + shake already applied)
};

// A prism splits the beam into several sub-beams. Each facet is an angular
// offset of that sub-beam's axis from the main beam axis, expressed in the
// beam's local frame (x = right, y = up when looking along the beam), radians.
struct PrismState {
    std::uint8_t facetCount = 0;  // 0 = no prism inserted
    std::array<glm::vec2, kMaxPrismFacets> facets{};
    float rotation = 0.0f;  // radians, rotation of the whole facet pattern around the beam axis
};

// Framing shutter blade (profile fixtures). insertion 0 = fully out, 1 = to the beam centre.
struct BladeState {
    float insertion = 0.0f;
    float angle = 0.0f;  // radians, tilt of the blade edge
};

// The full optical state of one beam (one lens / LED cell) for this frame,
// already in world space. A fixture may produce many (multi-cell LED bars).
struct BeamState {
    // --- pose -----------------------------------------------------------
    glm::vec3 position{0.0f};              // centre of the lens / emitting surface
    glm::vec3 direction{0.0f, -1.0f, 0.0f};// unit beam axis
    glm::vec3 up{0.0f, 0.0f, 1.0f};        // unit, perpendicular to direction; gobo/prism/blade reference

    // --- optics ---------------------------------------------------------
    BeamShape shape = BeamShape::Spot;
    float lensRadius = 0.05f;       // m, radius of the exit lens (beams start as a disc, not a point)
    glm::vec2 emitterSize{0.1f};    // m, width/height for BeamShape::Rectangle
    float beamAngle = degToRad(20); // radians, FULL angle at 50% intensity (after zoom and iris)
    float fieldAngle = degToRad(25);// radians, FULL angle at 10% intensity (>= beamAngle)
    float focus = 1.0f;             // 0 = fully blurred, 1 = sharp gobo/edge
    float frost = 0.0f;             // 0..1, diffusion: softens edges and gobos, slightly widens
    float iris = 1.0f;              // 0..1 aperture fraction (already applied to beamAngle; kept for gobo scaling)

    // --- colour / output -------------------------------------------------
    glm::vec3 color{1.0f};          // linear RGB chromaticity of the output, max component ~1
    float intensity = 0.0f;         // 0..1 after dimmer curve, shutter and strobe
    float luminousFlux = 10000.0f;  // lumens at intensity 1 (from the fixture definition)

    // --- patterns --------------------------------------------------------
    std::array<GoboState, kMaxGobosPerBeam> gobos{};
    GoboState animationWheel{};     // animation/effect wheel, drawn like a gobo
    PrismState prism{};
    std::array<BladeState, kMaxBlades> blades{};
    float bladeRotation = 0.0f;     // radians, rotation of the whole blade assembly

    // --- bookkeeping -----------------------------------------------------
    NodeId owner = kInvalidNode;    // fixture scene node (selection highlight, solo, etc.)
    bool castsVolume = true;        // false disables the haze beam but keeps surface lighting
};

}  // namespace dmxviz
