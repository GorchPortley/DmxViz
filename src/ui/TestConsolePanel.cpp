#include "ui/TestConsolePanel.h"

#include "fixtures/ColorMath.h"
#include "ui/EditorContext.h"
#include "ui/PanelTitles.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <iterator>

namespace dmxviz::ui {

using fixtures::Attribute;
using fixtures::AttributeFamily;

namespace {

constexpr float kSectionHeight = 172.0f;
constexpr float kWheelSectionHeight = 250.0f;  // a row of slot buttons per wheel
constexpr float kPadSize = 108.0f;
constexpr float kSlotButtonWidth = 24.0f;
constexpr int kRawChannelsPerPage = 32;
constexpr int kRawPageCount = static_cast<int>(dmx::kUniverseSize) / kRawChannelsPerPage;

enum class Section { Skip, Intensity, Colour, Position, Beam, Wheels, Shutter, Other };

const fixtures::AttributeEncoder::Control* findControl(const fixtures::AttributeEncoder& encoder, Attribute attribute) {
    for (const fixtures::AttributeEncoder::Control& control : encoder.controls())
        if (control.attribute == attribute) return &control;
    return nullptr;
}

// The colour picker can drive the fixture with additive (RGB) or subtractive (CMY) mixing.
bool hasColourMixing(const fixtures::AttributeEncoder& encoder) {
    const bool rgb = encoder.has(Attribute::ColorAdd_R) && encoder.has(Attribute::ColorAdd_G) &&
                     encoder.has(Attribute::ColorAdd_B);
    const bool cmy = encoder.has(Attribute::ColorSub_C) || encoder.has(Attribute::ColorSub_M) ||
                     encoder.has(Attribute::ColorSub_Y);
    return rgb || cmy;
}

bool hasColourWheel(const fixtures::AttributeEncoder& encoder) {
    const fixtures::AttributeEncoder::Control* control = findControl(encoder, Attribute::Color1);
    return control != nullptr && control->wheel != nullptr;
}

Section classify(Attribute attribute, const fixtures::AttributeEncoder& encoder) {
    switch (attribute) {
        case Attribute::Unknown:
        case Attribute::NoFeature:
            return Section::Skip;
        case Attribute::Dimmer:
            return Section::Intensity;
        case Attribute::Pan:
        case Attribute::Tilt:
            return Section::Position;
        case Attribute::Zoom:
        case Attribute::Focus1:
        case Attribute::Iris:
        case Attribute::Frost1:
        case Attribute::Frost2:
            return Section::Beam;
        case Attribute::ColorAdd_R:
        case Attribute::ColorAdd_G:
        case Attribute::ColorAdd_B:
        case Attribute::ColorAdd_W:
        case Attribute::ColorSub_C:
        case Attribute::ColorSub_M:
        case Attribute::ColorSub_Y:
            return hasColourMixing(encoder) ? Section::Colour : Section::Other;
        default:
            break;
    }
    switch (fixtures::attributeFamily(attribute)) {
        case AttributeFamily::ColorWheel:
        case AttributeFamily::Gobo:
        case AttributeFamily::Prism:
        case AttributeFamily::Animation: {
            const auto* control = findControl(encoder, attribute);
            return control != nullptr && control->wheel != nullptr ? Section::Wheels : Section::Other;
        }
        case AttributeFamily::ColorWheelSpin:
        case AttributeFamily::GoboPos:
        case AttributeFamily::GoboPosRotate:
        case AttributeFamily::GoboWheelSpin:
        case AttributeFamily::AnimationPos:
        case AttributeFamily::AnimationPosRotate:
        case AttributeFamily::PrismPos:
        case AttributeFamily::PrismPosRotate:
            return Section::Wheels;
        case AttributeFamily::Shutter:
            return Section::Shutter;
        default:
            return Section::Other;
    }
}

bool isWheelSlotAttribute(Attribute attribute, const fixtures::AttributeEncoder& encoder) {
    switch (fixtures::attributeFamily(attribute)) {
        case AttributeFamily::ColorWheel:
        case AttributeFamily::Gobo:
        case AttributeFamily::Prism:
        case AttributeFamily::Animation: {
            const auto* control = findControl(encoder, attribute);
            return control != nullptr && control->wheel != nullptr;
        }
        default:
            return false;
    }
}

// "Gobo 1 pos" and so on, into a caller-owned buffer.
void attributeLabel(Attribute attribute, char* out, std::size_t size) {
    const std::string_view pretty = fixtures::attributeInfo(attribute).pretty;
    std::snprintf(out, size, "%.*s", static_cast<int>(pretty.size()), pretty.data());
}

ImU32 srgbColor(const glm::vec3& linear) {
    const glm::vec3 s = fixtures::linearToSrgb(glm::clamp(linear, 0.0f, 1.0f));
    return ImGui::ColorConvertFloat4ToU32(ImVec4(s.x, s.y, s.z, 1.0f));
}

// Two-axis control: drag inside the square. Returns true while it changes x or y (both 0..1).
bool xyPad(const char* id, float& x, float& y, float size) {
    const ImVec2 origin = ImGui::GetCursorScreenPos();
    ImGui::InvisibleButton(id, ImVec2(size, size));
    bool changed = false;
    if (ImGui::IsItemActive() && ImGui::IsMouseDown(ImGuiMouseButton_Left)) {
        const ImVec2 mouse = ImGui::GetMousePos();
        x = std::clamp((mouse.x - origin.x) / size, 0.0f, 1.0f);
        y = std::clamp((mouse.y - origin.y) / size, 0.0f, 1.0f);
        changed = true;
    } else if (ImGui::IsItemHovered() && ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left)) {
        x = 0.5f;
        y = 0.5f;
        changed = true;
    }
    ImDrawList* draw = ImGui::GetWindowDrawList();
    const ImVec2 end(origin.x + size, origin.y + size);
    draw->AddRectFilled(origin, end, IM_COL32(24, 24, 28, 255));
    const ImU32 grid = IM_COL32(60, 60, 68, 255);
    draw->AddLine(ImVec2(origin.x + size * 0.5f, origin.y), ImVec2(origin.x + size * 0.5f, end.y), grid);
    draw->AddLine(ImVec2(origin.x, origin.y + size * 0.5f), ImVec2(end.x, origin.y + size * 0.5f), grid);
    draw->AddRect(origin, end, ImGui::IsItemActive() ? IM_COL32(245, 150, 40, 255) : IM_COL32(90, 90, 100, 255));
    const ImVec2 dot(origin.x + x * size, origin.y + y * size);
    draw->AddCircleFilled(dot, 5.0f, IM_COL32(245, 150, 40, 255));
    draw->AddCircle(dot, 6.5f, IM_COL32(255, 255, 255, 200));
    return changed;
}

// Pan or tilt range in degrees for the label of a slider.
float degreesAt(const fixtures::AttributeEncoder& encoder, Attribute attribute, float t) {
    const auto* control = findControl(encoder, attribute);
    if (control == nullptr) return 0.0f;
    return glm::degrees(control->physicalMin + (control->physicalMax - control->physicalMin) * t);
}

}  // namespace

const char* TestConsolePanel::title() const {
    return kTestConsoleTitle;
}

template <typename Change>
void TestConsolePanel::forEachSelected(Change&& change) {
    for (ConsoleFixture* fixture : selected_) {
        change(*fixture);
        fixture->send(session_->model());
    }
}

// ---------------------------------------------------------------------------
// Frame

void TestConsolePanel::draw(EditorContext& ctx) {
    if (!session_) session_ = std::make_unique<ConsoleSession>(ctx.dmx);
    session_->prune(ctx.scene);
    collectSelected(ctx);

    drawToolbar(ctx);
    if (ImGui::BeginTabBar("##consoleTabs")) {
        if (ImGui::BeginTabItem("Attributes")) {
            drawAttributes(ctx);
            ImGui::EndTabItem();
        }
        if (ImGui::BeginTabItem("Raw channels")) {
            drawRawChannels(ctx);
            ImGui::EndTabItem();
        }
        ImGui::EndTabBar();
    }
}

void TestConsolePanel::collectSelected(EditorContext& ctx) {
    selected_.clear();
    const auto add = [&](NodeId id) {
        ConsoleFixture* fixture = session_->fixture(ctx.scene, ctx.fixtures, id);
        if (fixture != nullptr && std::find(selected_.begin(), selected_.end(), fixture) == selected_.end())
            selected_.push_back(fixture);
    };
    for (const NodeId id : ctx.selection.ids()) {
        const stage::Node* node = ctx.scene.find(id);
        if (node == nullptr) continue;
        if (node->kind() == stage::NodeKind::Fixture) {
            add(id);
        } else {
            for (const NodeId child : ctx.scene.depthFirst(id)) add(child);  // a selected group controls its fixtures
        }
    }
    // The primary selection leads: its values are the ones the controls show.
    const auto primary = std::find_if(selected_.begin(), selected_.end(),
                                      [&ctx](const ConsoleFixture* f) { return f->node() == ctx.selection.primary(); });
    if (primary != selected_.end()) std::rotate(selected_.begin(), primary, primary + 1);
}

void TestConsolePanel::drawToolbar(EditorContext& ctx) {
    int mode = ctx.dmx.programmerMode() == dmx::ProgrammerMode::Override ? 1 : 0;
    ImGui::SetNextItemWidth(120.0f);
    if (ImGui::Combo("##mode", &mode, "Merge (HTP)\0Override\0"))
        ctx.dmx.setProgrammerMode(mode == 1 ? dmx::ProgrammerMode::Override : dmx::ProgrammerMode::Merge);
    ImGui::SetItemTooltip("Merge: the higher of the console and incoming DMX wins (HTP).\n"
                          "Override: the channels the console touches replace incoming DMX.");

    ImGui::SameLine();
    bool output = ctx.dmx.outputEnabled();
    if (ImGui::Checkbox("Output to interfaces", &output)) ctx.dmx.setOutputEnabled(output);
    ImGui::SetItemTooltip("Send the merged universes out of the interfaces' 'Output universes'.\n"
                          "Off: only the visualizer sees the console.");

    ImGui::SameLine(0.0f, 20.0f);
    ImGui::BeginDisabled(selected_.empty());
    if (ImGui::Button("Home")) {
        forEachSelected([](ConsoleFixture& f) { f.home(); });
    }
    ImGui::SetItemTooltip("Back to the default look: shutter open, full intensity, white, default position");
    ImGui::SameLine();
    if (ImGui::Button("Clear")) {
        for (ConsoleFixture* fixture : selected_) fixture->release(session_->model());
    }
    ImGui::SetItemTooltip("Release the selected fixtures from the console; incoming DMX controls them again");
    ImGui::EndDisabled();
    ImGui::SameLine();
    ImGui::BeginDisabled(session_->model().empty());
    if (ImGui::Button("Clear all")) {
        session_->clearAll();
        selected_.clear();  // the fixtures it pointed to are gone
    }
    ImGui::SetItemTooltip("Release everything the console holds");
    ImGui::EndDisabled();

    ImGui::SameLine(0.0f, 20.0f);
    ImGui::TextDisabled("%d selected, %d held", static_cast<int>(selected_.size()), session_->capturedCount());
}

// ---------------------------------------------------------------------------
// Attributes tab

bool TestConsolePanel::beginSection(const char* name, float width, float height) {
    if (sectionsDrawn_ > 0 && lastSectionRight_ + ImGui::GetStyle().ItemSpacing.x + width <= sectionRightEdge_)
        ImGui::SameLine();
    return ImGui::BeginChild(name, ImVec2(width, height), ImGuiChildFlags_Borders);
}

void TestConsolePanel::endSection() {
    ImGui::EndChild();
    lastSectionRight_ = ImGui::GetItemRectMax().x;
    ++sectionsDrawn_;
}

void TestConsolePanel::drawAttributes(EditorContext& ctx) {
    (void)ctx;
    if (selected_.empty()) {
        ImGui::TextDisabled("Select fixtures in the viewport, the outliner or the patch to control them here.");
        return;
    }
    const ConsoleFixture& primary = *selected_.front();

    int unpatched = 0;
    for (const ConsoleFixture* fixture : selected_) unpatched += fixture->patched() ? 0 : 1;
    ImGui::TextUnformatted(primary.type().displayName().c_str());
    ImGui::SameLine();
    ImGui::TextDisabled("%s, %d ch, universe %u address %u%s", primary.mode().name.c_str(), primary.footprint(),
                        static_cast<unsigned>(primary.patch().universe), static_cast<unsigned>(primary.patch().address),
                        selected_.size() > 1 ? "  (controls apply to all selected fixtures)" : "");
    if (unpatched > 0) {
        ImGui::SameLine();
        ImGui::TextColored(ImVec4(1.0f, 0.55f, 0.35f, 1.0f), "%d not patched (no DMX address): they cannot be driven",
                           unpatched);
    }

    // Sort the primary fixture's attributes into sections (each attribute once).
    const fixtures::AttributeEncoder& encoder = primary.encoder();
    attributes_.clear();
    for (const fixtures::AttributeEncoder::Control& control : encoder.controls())
        if (std::find(attributes_.begin(), attributes_.end(), control.attribute) == attributes_.end())
            attributes_.push_back(control.attribute);
    bool present[8] = {};
    for (const Attribute attribute : attributes_) present[static_cast<int>(classify(attribute, encoder))] = true;

    if (!ImGui::BeginChild("##attributes", ImVec2(0.0f, 0.0f), ImGuiChildFlags_None,
                           ImGuiWindowFlags_HorizontalScrollbar)) {
        ImGui::EndChild();
        return;
    }
    sectionRightEdge_ = ImGui::GetCursorScreenPos().x + ImGui::GetContentRegionAvail().x;
    lastSectionRight_ = 0.0f;
    sectionsDrawn_ = 0;

    const bool colourPicker = present[static_cast<int>(Section::Colour)] || hasColourMixing(encoder) || hasColourWheel(encoder);
    // Without a dimmer channel the intensity fader can still dim the colour emitters.
    if (present[static_cast<int>(Section::Intensity)] || hasColourMixing(encoder)) drawIntensity(primary);
    if (colourPicker) drawColour(primary);
    if (present[static_cast<int>(Section::Position)]) drawPosition(primary);
    if (present[static_cast<int>(Section::Beam)]) drawBeam(primary);
    if (present[static_cast<int>(Section::Wheels)]) drawWheels(primary);
    if (present[static_cast<int>(Section::Shutter)]) drawShutter(primary);
    if (present[static_cast<int>(Section::Other)]) drawOther(primary);
    ImGui::EndChild();
}

void TestConsolePanel::sliderForAttribute(const ConsoleFixture& primary, Attribute attribute) {
    char label[64];
    attributeLabel(attribute, label, sizeof(label));
    float percent = primary.values().level[static_cast<std::size_t>(attribute)] * 100.0f;
    ImGui::PushID(static_cast<int>(attribute));
    ImGui::SetNextItemWidth(110.0f);
    if (ImGui::SliderFloat(label, &percent, 0.0f, 100.0f, "%.0f%%")) {
        forEachSelected([&](ConsoleFixture& f) {
            if (f.encoder().has(attribute)) f.setLevel(attribute, percent / 100.0f);
        });
    }
    ImGui::PopID();
}

void TestConsolePanel::drawIntensity(const ConsoleFixture& primary) {
    if (!beginSection("Intensity", 112.0f)) {
        endSection();
        return;
    }
    ImGui::TextUnformatted("Intensity");
    int percent = static_cast<int>(std::lround(primary.values().intensity * 100.0f));
    if (ImGui::VSliderInt("##intensity", ImVec2(38.0f, kSectionHeight - 62.0f), &percent, 0, 100, "%d%%")) {
        forEachSelected([&](ConsoleFixture& f) { f.setIntensity(static_cast<float>(percent) / 100.0f); });
    }
    ImGui::SameLine();
    ImGui::BeginGroup();
    if (ImGui::Button("Full", ImVec2(40.0f, 0.0f))) forEachSelected([](ConsoleFixture& f) { f.setIntensity(1.0f); });
    if (ImGui::Button("Half", ImVec2(40.0f, 0.0f))) forEachSelected([](ConsoleFixture& f) { f.setIntensity(0.5f); });
    if (ImGui::Button("Out", ImVec2(40.0f, 0.0f))) forEachSelected([](ConsoleFixture& f) { f.setIntensity(0.0f); });
    ImGui::EndGroup();
    endSection();
}

void TestConsolePanel::drawColour(const ConsoleFixture& primary) {
    if (!beginSection("Colour", 182.0f)) {
        endSection();
        return;
    }
    const fixtures::AttributeEncoder& encoder = primary.encoder();
    const char* mixing = "no colour channels";
    if (encoder.has(Attribute::ColorAdd_R) && encoder.has(Attribute::ColorAdd_G) && encoder.has(Attribute::ColorAdd_B))
        mixing = encoder.has(Attribute::ColorAdd_W) ? "RGBW" : "RGB";
    else if (encoder.has(Attribute::ColorSub_C) || encoder.has(Attribute::ColorSub_M))
        mixing = "CMY";
    else if (hasColourWheel(encoder))
        mixing = "colour wheel (nearest slot)";
    ImGui::Text("Colour");
    ImGui::SameLine();
    ImGui::TextDisabled("%s", mixing);

    glm::vec3 colour = primary.values().colour;
    ImGui::SetNextItemWidth(150.0f);
    if (ImGui::ColorPicker3("##colour", &colour.x,
                            ImGuiColorEditFlags_NoInputs | ImGuiColorEditFlags_NoSidePreview |
                                ImGuiColorEditFlags_PickerHueBar | ImGuiColorEditFlags_DisplayRGB)) {
        forEachSelected([&](ConsoleFixture& f) { f.setColour(colour); });
    }

    // Quick swatches.
    static const glm::vec3 kSwatches[] = {{1.0f, 1.0f, 1.0f}, {1.0f, 0.1f, 0.1f}, {1.0f, 0.55f, 0.0f}, {1.0f, 0.9f, 0.1f},
                                          {0.1f, 0.9f, 0.2f}, {0.1f, 0.85f, 0.95f}, {0.15f, 0.25f, 1.0f}, {0.9f, 0.1f, 0.8f}};
    for (int i = 0; i < static_cast<int>(std::size(kSwatches)); ++i) {
        ImGui::PushID(i);
        const glm::vec3& swatch = kSwatches[i];
        if (ImGui::ColorButton("##swatch", ImVec4(swatch.x, swatch.y, swatch.z, 1.0f),
                               ImGuiColorEditFlags_NoTooltip | ImGuiColorEditFlags_NoAlpha, ImVec2(16.0f, 16.0f)))
            forEachSelected([&](ConsoleFixture& f) { f.setColour(swatch); });
        ImGui::PopID();
        if (i + 1 < static_cast<int>(std::size(kSwatches))) ImGui::SameLine(0.0f, 3.0f);
    }
    endSection();
}

void TestConsolePanel::drawPosition(const ConsoleFixture& primary) {
    if (!beginSection("Position", 360.0f)) {
        endSection();
        return;
    }
    const fixtures::AttributeEncoder& encoder = primary.encoder();
    ControlValues v = primary.values();
    const auto apply = [&]() {
        forEachSelected([&](ConsoleFixture& f) { f.setPosition(v.pan, v.tilt, v.panFine, v.tiltFine); });
    };

    ImGui::TextUnformatted("Pan / Tilt");
    ImGui::SameLine();
    ImGui::TextDisabled("(double-click the pad to centre)");
    if (xyPad("##pad", v.pan, v.tilt, kPadSize)) apply();
    ImGui::SameLine();
    ImGui::BeginGroup();

    char text[48];
    ImGui::SetNextItemWidth(170.0f);
    std::snprintf(text, sizeof(text), "Pan %.0f deg", static_cast<double>(degreesAt(encoder, Attribute::Pan, v.pan)));
    if (ImGui::SliderFloat("##pan", &v.pan, 0.0f, 1.0f, text)) apply();
    ImGui::SetNextItemWidth(170.0f);
    if (ImGui::SliderFloat("##panFine", &v.panFine, -1.0f, 1.0f, "Pan fine %.2f")) apply();
    ImGui::SetNextItemWidth(170.0f);
    std::snprintf(text, sizeof(text), "Tilt %.0f deg", static_cast<double>(degreesAt(encoder, Attribute::Tilt, v.tilt)));
    if (ImGui::SliderFloat("##tilt", &v.tilt, 0.0f, 1.0f, text)) apply();
    ImGui::SetNextItemWidth(170.0f);
    if (ImGui::SliderFloat("##tiltFine", &v.tiltFine, -1.0f, 1.0f, "Tilt fine %.2f")) apply();
    if (ImGui::Button("Centre")) {
        v.pan = v.tilt = 0.5f;
        v.panFine = v.tiltFine = 0.0f;
        apply();
    }
    ImGui::EndGroup();
    endSection();
}

void TestConsolePanel::drawBeam(const ConsoleFixture& primary) {
    if (!beginSection("Beam", 210.0f)) {
        endSection();
        return;
    }
    ImGui::TextUnformatted("Beam");
    for (const Attribute attribute : attributes_)
        if (classify(attribute, primary.encoder()) == Section::Beam) sliderForAttribute(primary, attribute);
    endSection();
}

void TestConsolePanel::drawWheels(const ConsoleFixture& primary) {
    if (!beginSection("Wheels", 390.0f, kWheelSectionHeight)) {
        endSection();
        return;
    }
    const fixtures::AttributeEncoder& encoder = primary.encoder();

    // Slot buttons of every wheel, then the rotation faders.
    for (const Attribute attribute : attributes_) {
        if (classify(attribute, encoder) != Section::Wheels || !isWheelSlotAttribute(attribute, encoder)) continue;
        const fixtures::Wheel& wheel = *findControl(encoder, attribute)->wheel;
        char label[64];
        attributeLabel(attribute, label, sizeof(label));
        ImGui::TextUnformatted(label);
        ImGui::SameLine();
        ImGui::TextDisabled("%s", wheel.name.c_str());

        const int current = primary.values().slot[static_cast<std::size_t>(attribute)];
        const float rowRight = ImGui::GetCursorScreenPos().x + ImGui::GetContentRegionAvail().x;
        ImGui::PushID(static_cast<int>(attribute));
        for (int slot = 1; slot <= static_cast<int>(wheel.slots.size()); ++slot) {
            const fixtures::WheelSlot& info = wheel.slots[static_cast<std::size_t>(slot - 1)];
            const bool isColour = info.kind == fixtures::SlotKind::Color;
            const bool selected = slot == current;
            ImGui::PushID(slot);
            if (isColour) ImGui::PushStyleColor(ImGuiCol_Button, srgbColor(info.color));
            if (isColour) ImGui::PushStyleColor(ImGuiCol_Text, fixtures::maxComponent(info.color) > 0.55f ? IM_COL32(10, 10, 10, 255) : IM_COL32(240, 240, 240, 255));
            if (selected) {
                ImGui::PushStyleVar(ImGuiStyleVar_FrameBorderSize, 2.0f);
                ImGui::PushStyleColor(ImGuiCol_Border, IM_COL32(255, 255, 255, 255));
            }
            char number[16];
            std::snprintf(number, sizeof(number), "%d", slot);
            if (ImGui::Button(number, ImVec2(kSlotButtonWidth, 0.0f)))
                forEachSelected([&](ConsoleFixture& f) {
                    if (f.encoder().has(attribute)) f.setWheelSlot(attribute, slot);
                });
            if (selected) {
                ImGui::PopStyleColor();
                ImGui::PopStyleVar();
            }
            if (isColour) ImGui::PopStyleColor(2);
            if (ImGui::IsItemHovered()) {
                if (info.kind == fixtures::SlotKind::Prism)
                    ImGui::SetTooltip("%d: %s (%d facets)", slot, info.name.c_str(), static_cast<int>(info.facets.size()));
                else
                    ImGui::SetTooltip("%d: %s", slot, info.name.empty() ? "open" : info.name.c_str());
            }
            ImGui::PopID();
            if (slot < static_cast<int>(wheel.slots.size()) &&
                ImGui::GetItemRectMax().x + ImGui::GetStyle().ItemSpacing.x + kSlotButtonWidth <= rowRight)
                ImGui::SameLine();
        }
        ImGui::PopID();
    }
    for (const Attribute attribute : attributes_)
        if (classify(attribute, encoder) == Section::Wheels && !isWheelSlotAttribute(attribute, encoder))
            sliderForAttribute(primary, attribute);
    endSection();
}

void TestConsolePanel::drawShutter(const ConsoleFixture& primary) {
    if (!beginSection("Shutter", 190.0f)) {
        endSection();
        return;
    }
    ImGui::TextUnformatted("Shutter / strobe");
    ControlValues v = primary.values();
    ShutterMode mode = v.shutter;
    const auto apply = [&](ShutterMode newMode) {
        forEachSelected([&](ConsoleFixture& f) { f.setShutter(newMode, v.strobeHz); });
    };
    if (ImGui::RadioButton("Open", mode == ShutterMode::Open)) apply(ShutterMode::Open);
    ImGui::SameLine();
    if (ImGui::RadioButton("Closed", mode == ShutterMode::Closed)) apply(ShutterMode::Closed);
    ImGui::SameLine();
    if (ImGui::RadioButton("Strobe", mode == ShutterMode::Strobe)) apply(ShutterMode::Strobe);

    ImGui::SetNextItemWidth(160.0f);
    if (ImGui::SliderFloat("##strobeHz", &v.strobeHz, 0.5f, 25.0f, "%.1f Hz", ImGuiSliderFlags_Logarithmic))
        apply(ShutterMode::Strobe);
    ImGui::SetItemTooltip("Moving the slider switches to strobe at this rate");
    endSection();
}

void TestConsolePanel::drawOther(const ConsoleFixture& primary) {
    if (!beginSection("Other", 230.0f)) {
        endSection();
        return;
    }
    ImGui::TextUnformatted("Other attributes");
    for (const Attribute attribute : attributes_)
        if (classify(attribute, primary.encoder()) == Section::Other) sliderForAttribute(primary, attribute);
    endSection();
}

// ---------------------------------------------------------------------------
// Raw channels tab

void TestConsolePanel::drawRawChannels(EditorContext& ctx) {
    ProgrammerModel& model = session_->model();

    ImGui::SetNextItemWidth(110.0f);
    ImGui::InputInt("Universe", &rawUniverse_, 1, 10);
    rawUniverse_ = std::clamp(rawUniverse_, 1, 63999);
    const dmx::UniverseId universe = static_cast<dmx::UniverseId>(rawUniverse_);

    ImGui::SameLine();
    if (ImGui::Button("<") && rawPage_ > 0) --rawPage_;
    ImGui::SameLine();
    if (ImGui::Button(">") && rawPage_ < kRawPageCount - 1) ++rawPage_;
    const int first = rawPage_ * kRawChannelsPerPage + 1;
    ImGui::SameLine();
    ImGui::Text("Channels %d-%d", first, first + kRawChannelsPerPage - 1);

    ImGui::SameLine(0.0f, 20.0f);
    if (ImGui::Button("Full"))
        for (int i = 0; i < kRawChannelsPerPage; ++i) model.setChannel(universe, first + i, 255);
    ImGui::SameLine();
    if (ImGui::Button("Zero"))
        for (int i = 0; i < kRawChannelsPerPage; ++i) model.setChannel(universe, first + i, 0);
    ImGui::SameLine();
    if (ImGui::Button("Release page")) model.release(universe, first, kRawChannelsPerPage);
    ImGui::SameLine();
    if (ImGui::Button("Release universe")) model.release(universe, 1, static_cast<int>(dmx::kUniverseSize));
    ImGui::SameLine();
    ImGui::TextDisabled("highlighted = held by the console; right-click a fader to release it");

    // Faders: 32 in a row when the window is wide enough, otherwise 16 or 8 per row.
    const ImVec2 avail = ImGui::GetContentRegionAvail();
    constexpr float kFaderWidth = 26.0f;
    constexpr float kFaderGap = 4.0f;
    int perRow = kRawChannelsPerPage;
    while (perRow > 8 && static_cast<float>(perRow) * (kFaderWidth + kFaderGap) > avail.x) perRow /= 2;
    const int rows = kRawChannelsPerPage / perRow;
    const float labelHeight = ImGui::GetTextLineHeightWithSpacing();
    const float faderHeight = std::max(48.0f, (avail.y - static_cast<float>(rows) * (labelHeight + 6.0f)) / static_cast<float>(rows));

    for (int i = 0; i < kRawChannelsPerPage; ++i) {
        const int address = first + i;
        const bool held = model.touched(universe, address);
        int value = held ? model.value(universe, address) : ctx.dmxSnapshot.channel(universe, static_cast<std::uint16_t>(address));

        ImGui::PushID(address);
        ImGui::BeginGroup();
        ImGui::Text("%3d", address);
        if (held) ImGui::PushStyleColor(ImGuiCol_FrameBg, ImVec4(0.45f, 0.27f, 0.08f, 1.0f));
        if (ImGui::VSliderInt("##fader", ImVec2(kFaderWidth, faderHeight), &value, 0, 255, "%d"))
            model.setChannel(universe, address, static_cast<std::uint8_t>(value));
        if (held) ImGui::PopStyleColor();
        if (ImGui::IsItemClicked(ImGuiMouseButton_Right)) model.release(universe, address, 1);
        if (ImGui::IsItemHovered())
            ImGui::SetTooltip("Universe %d, channel %d: %d (%d%%)%s", rawUniverse_, address, value, (value * 100 + 127) / 255,
                              held ? "\nRight-click to release" : "");
        ImGui::EndGroup();
        ImGui::PopID();
        if ((i + 1) % perRow != 0) ImGui::SameLine(0.0f, kFaderGap);
    }
}

}  // namespace dmxviz::ui
