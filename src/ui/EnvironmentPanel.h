#pragma once
// EnvironmentPanel: haze, lighting and quality settings.
//
//   * Environment (haze, ambient, background, exposure, bloom, beam brightness, grid) belongs to
//     the show and is saved in the project;
//   * RenderSettings (volumetrics, march steps, lens glow, bloom pass, tone mapper...) are the
//     renderer's quality knobs; the app stores them in the project too.
//
// These are not scene edits, so they do not use the CommandStack; instead the panel sets
// ctx.settingsDirty so the project shows as modified.

#include "ui/Panel.h"

namespace dmxviz::ui {

class EnvironmentPanel : public Panel {
public:
    const char* title() const override;
    void draw(EditorContext& ctx) override;
};

}  // namespace dmxviz::ui
