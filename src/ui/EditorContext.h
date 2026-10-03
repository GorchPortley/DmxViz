#pragma once
// EditorContext: everything a panel may read or change, in one place.
//
// The application owns the real objects and hands every panel the same context
// each frame (Panel::draw). It is a bundle of references on purpose: panels stay
// small classes that do not know who owns what. To give panels access to a new
// subsystem, add a reference here and fill it in App::init().
//
// Rules of thumb for panels:
//   * change the scene through ctx.commands (so it is undoable), never directly;
//   * after something that is not a command (environment, DMX configuration)
//     set ctx.settingsDirty = true so the project shows as modified.

#include "assets/AssetLibrary.h"
#include "dmx/DmxManager.h"
#include "dmx/DmxSnapshot.h"
#include "fixtures/FixtureLibrary.h"
#include "render/RenderScene.h"
#include "render/Renderer.h"
#include "stage/CommandStack.h"
#include "stage/Scene.h"
#include "stage/Selection.h"

#include <filesystem>

namespace dmxviz::ui {

class ViewportCamera;

struct EditorContext {
    stage::Scene& scene;
    stage::CommandStack& commands;
    stage::Selection& selection;
    fixtures::FixtureLibrary& fixtures;
    dmx::DmxManager& dmx;
    const dmx::DmxSnapshot& dmxSnapshot;  // merged DMX values of this frame
    assets::AssetLibrary& assets;
    render::Renderer& renderer;
    render::Environment& environment;  // haze, exposure... saved with the project
    render::RenderScene& frame;        // what the simulation produced for this frame

    float dpiScale = 1.0f;  // framebuffer pixels per ImGui pixel (high-DPI screens)

    // The 3D view's camera (owned by the viewport panel). Panels use it to place new nodes in front of
    // the camera and to frame or apply camera presets. May be null (tests, headless tools).
    ViewportCamera* viewportCamera = nullptr;

    std::filesystem::path projectPath;  // empty until the project was saved or opened
    bool settingsDirty = false;         // environment / DMX config changed since the last save

    // True when the project differs from what is on disk.
    bool dirty() const { return settingsDirty || commands.isDirty(); }
};

}  // namespace dmxviz::ui
