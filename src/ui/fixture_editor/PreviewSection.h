#pragma once
// PreviewSection: the live 3D preview of the fixture being edited.
//
// It builds a FixtureRuntime from a private copy of the edited type (with its own id, so the
// library's gobo images and the stage are never touched), hangs it above a floor in a hazy room
// and renders that through its own ViewportTarget and orbit camera. A few faders (dimmer, pan,
// tilt, zoom, colour, gobo) drive it through an AttributeEncoder, the same way the test console
// does, so a channel that is edited wrongly is visible at once.
//
// The preview is rebuilt shortly after the last edit (not on every drag step), and the meshes of
// the geometry are only re-created when the geometry or a resource changed.

#include "assets/AssetLibrary.h"
#include "fixtures/AttributeEncoder.h"
#include "fixtures/FixtureAssets.h"
#include "fixtures/FixtureRuntime.h"
#include "render/RenderScene.h"
#include "render/Renderer.h"
#include "ui/ViewportCamera.h"

#include <cstdint>
#include <memory>
#include <vector>

namespace dmxviz::ui {
struct EditorContext;
}

namespace dmxviz::ui::fixture_editor {

class PreviewSection {
public:
    PreviewSection();

    // Draws the 3D view and the faders. `revision` changes whenever `type` was edited.
    void draw(EditorContext& ctx, const fixtures::FixtureType& type, std::uint64_t revision, int modeIndex);

    // A different fixture was opened: reset the faders and the camera.
    void reset();

private:
    // Fader positions; pan, tilt, zoom and dimmer are 0..1 across the attribute's range.
    struct Faders {
        float dimmer = 1.0f;
        float pan = 0.5f;
        float tilt = 0.5f;
        float zoom = 0.5f;
        glm::vec3 colour{1.0f};  // sRGB, as the picker shows it
        int colourSlot = 1;      // colour wheel slot (1-based), for fixtures without colour mixing
        int goboSlot = 1;        // gobo wheel slot (1-based)
    };

    void rebuild(EditorContext& ctx, const fixtures::FixtureType& type, int modeIndex);
    void applyFaders();
    void buildScene(EditorContext& ctx);
    bool drawFaders();
    void resetCamera();

    render::ViewportTarget target_;
    ViewportCamera camera_;
    render::RenderScene scene_;
    Faders faders_;

    // What the runtime works on. Members are destroyed in reverse order: runtime and encoder first.
    std::shared_ptr<fixtures::FixtureType> type_;
    std::unique_ptr<fixtures::FixtureAssets> assets_;
    std::unique_ptr<fixtures::FixtureRuntime> runtime_;
    std::unique_ptr<fixtures::AttributeEncoder> encoder_;
    std::vector<std::uint8_t> dmx_;

    std::uint64_t builtRevision_ = 0;
    bool built_ = false;
    int builtMode_ = -1;
    double lastBuildTime_ = -1.0;
    std::uint64_t assetSignature_ = 0;
    double time_ = 0.0;
};

}  // namespace dmxviz::ui::fixture_editor
