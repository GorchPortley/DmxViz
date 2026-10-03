#pragma once
// DemoShow: the stage that is built when DmxViz starts without a project file,
// and the --test-pattern DMX values that switch its fixtures on.

#include "dmx/DmxManager.h"
#include "fixtures/FixtureLibrary.h"
#include "render/RenderScene.h"
#include "stage/Scene.h"

namespace dmxviz::app {

// Replaces the scene with a small stage: floor, deck, back wall, an upstage and a
// downstage truss with eight generic/profile-spot fixtures patched from universe 1
// address 1, plus up to four fixtures of every other generic/* type on a mid-stage
// truss. Fixture types missing from the library are skipped. Also sets a hazy
// environment so the beams are visible.
void buildDemoShow(stage::Scene& scene, const fixtures::FixtureLibrary& library, render::Environment& environment);

// Drives the DMX manager's programmer (test console) so every patched fixture is
// on: dimmer full, shutter open, varied colours, gobos and prisms, and pan/tilt
// spread over the stage. The values are computed with the fixture library's
// AttributeEncoder, so any fixture type works. Used for screenshots.
void applyTestPattern(const stage::Scene& scene, const fixtures::FixtureLibrary& library, dmx::DmxManager& dmx);

}  // namespace dmxviz::app
