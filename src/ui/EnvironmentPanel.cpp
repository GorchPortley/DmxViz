#include "ui/EnvironmentPanel.h"

#include "ui/ColorWidgets.h"
#include "ui/EditorContext.h"
#include "ui/PanelTitles.h"

#include "imgui.h"

#include <algorithm>

namespace dmxviz::ui {

namespace {

// Collects "something changed" over many widgets.
struct Changes {
    bool any = false;
    bool operator()(bool widgetChanged) {
        any = any || widgetChanged;
        return widgetChanged;
    }
};

}  // namespace

const char* EnvironmentPanel::title() const {
    return kEnvironmentTitle;
}

void EnvironmentPanel::draw(EditorContext& ctx) {
    render::Environment& env = ctx.environment;
    render::RenderSettings& quality = ctx.renderer.settings();
    Changes changed;

    ImGui::PushItemWidth(-ImGui::GetFontSize() * 9.0f);

    ImGui::SeparatorText("Haze");
    changed(ImGui::SliderFloat("Density", &env.hazeDensity, 0.0f, 1.0f, "%.2f"));
    ImGui::SetItemTooltip("0 = clean air, 1 = thick haze. Beams are only visible in haze.");
    changed(ImGui::SliderFloat("Variation", &env.hazeVariation, 0.0f, 1.0f, "%.2f"));
    ImGui::SetItemTooltip("How much slowly drifting noise modulates the haze.");

    ImGui::SeparatorText("Look");
    changed(colorEditLinear("Ambient light", env.ambient, ImGuiColorEditFlags_NoInputs));
    changed(colorEditLinear("Background", env.background, ImGuiColorEditFlags_NoInputs));
    changed(ImGui::SliderFloat("Exposure", &env.exposure, 0.1f, 8.0f, "%.2f", ImGuiSliderFlags_Logarithmic));
    changed(ImGui::SliderFloat("Bloom strength", &env.bloomStrength, 0.0f, 1.0f, "%.3f"));
    changed(ImGui::SliderFloat("Beam brightness", &env.beamBrightness, 0.0f, 4.0f, "%.2f"));
    changed(ImGui::Checkbox("Show grid", &env.showGrid));

    ImGui::SeparatorText("Quality");
    changed(ImGui::Checkbox("Volumetric beams", &quality.volumetrics));
    ImGui::SetItemTooltip("Off draws no haze beams: much faster, useful on slow graphics cards.");
    ImGui::BeginDisabled(!quality.volumetrics);
    changed(ImGui::SliderInt("Min march steps", &quality.minMarchSteps, 2, 64));
    changed(ImGui::SliderInt("Max march steps", &quality.maxMarchSteps, 4, 128));
    quality.maxMarchSteps = std::max(quality.maxMarchSteps, quality.minMarchSteps);
    changed(ImGui::SliderFloat("Pixels per step", &quality.marchPixelsPerStep, 2.0f, 24.0f, "%.1f"));
    ImGui::SetItemTooltip("Larger values use fewer steps per beam: faster but more banding.");
    changed(ImGui::SliderFloat("Haze forward scatter", &quality.hazePhaseG, 0.0f, 0.95f, "%.2f"));
    ImGui::EndDisabled();
    changed(ImGui::Checkbox("Lens glow", &quality.lensGlow));
    ImGui::SameLine();
    changed(ImGui::Checkbox("Bloom", &quality.bloom));
    changed(ImGui::Checkbox("Beams stop at the floor", &quality.clipBeamsAtFloor));

    int tonemapper = static_cast<int>(quality.tonemapper);
    if (changed(ImGui::Combo("Tone mapping", &tonemapper, "ACES, keeps colours saturated\0ACES per channel\0")))
        quality.tonemapper = static_cast<render::Tonemapper>(tonemapper);

    ImGui::Spacing();
    if (ImGui::Button("Reset to defaults")) {
        const double time = env.timeSeconds;
        env = render::Environment{};
        env.timeSeconds = time;
        quality = render::RenderSettings{};
        changed(true);
    }
    ImGui::PopItemWidth();

    if (changed.any) ctx.settingsDirty = true;
}

}  // namespace dmxviz::ui
