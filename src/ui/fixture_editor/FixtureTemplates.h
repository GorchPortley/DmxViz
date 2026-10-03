#pragma once
// FixtureTemplates: starting points for "New fixture" in the fixture editor.
//
//   Blank        a body with one beam and a dimmer channel
//   LED par      RGBW wash par: dimmer, red, green, blue, white, strobe
//   Moving head  pan/tilt, dimmer, shutter, zoom, colour wheel, gobo wheel with two gobos and
//                rotation, prism
//
// Each template is a complete, valid fixture type, so the live preview works from the first
// moment. No ImGui in here.

#include "fixtures/FixtureType.h"

#include <array>

namespace dmxviz::ui::fixture_editor {

enum class FixtureTemplate { Blank, LedPar, MovingHead };

inline constexpr std::array<FixtureTemplate, 3> kAllTemplates = {FixtureTemplate::Blank, FixtureTemplate::LedPar,
                                                                 FixtureTemplate::MovingHead};

const char* templateName(FixtureTemplate kind);

fixtures::FixtureType makeTemplateFixture(FixtureTemplate kind);

}  // namespace dmxviz::ui::fixture_editor
