#include "ui/fixture_editor/WheelsSection.h"

#include "fixtures/FixtureAssets.h"
#include "ui/ColorWidgets.h"
#include "ui/fixture_editor/ChannelEditing.h"

#include "imgui.h"
#include "imgui_stdlib.h"

#include <algorithm>
#include <format>

namespace dmxviz::ui::fixture_editor {

using fixtures::SlotKind;
using fixtures::Wheel;
using fixtures::WheelSlot;

namespace {

// Same order as fixtures::SlotKind.
constexpr const char* kSlotNames[] = {"Open", "Color", "Gobo", "Prism", "Animation", "Frost"};
constexpr const char* kPatternNames[] = {"Circular", "Linear"};
constexpr float kThumbnailSize = 44.0f;

bool hasImage(SlotKind kind) {
    return kind == SlotKind::Gobo || kind == SlotKind::AnimationWheel;
}

}  // namespace

void WheelsSection::reset() {
    selectedWheel_ = 0;
    nameEditWheel_ = -1;
    pending_ = Action::None;
    message_.clear();
}

bool WheelsSection::draw(EditDocument& doc) {
    fixtures::FixtureType& type = doc.type();
    bool changed = false;
    pollImageImport(type, changed);
    if (type.wheels.empty()) selectedWheel_ = 0;
    else selectedWheel_ = std::clamp(selectedWheel_, 0, static_cast<int>(type.wheels.size()) - 1);

    const float listWidth = std::clamp(ImGui::GetContentRegionAvail().x * 0.25f, 130.0f, 220.0f);
    if (ImGui::BeginChild("##wheelList", ImVec2(listWidth, 0.0f), ImGuiChildFlags_Borders)) changed |= drawWheelList(type);
    ImGui::EndChild();
    ImGui::SameLine();
    if (ImGui::BeginChild("##wheelSlots", ImVec2(0.0f, 0.0f), ImGuiChildFlags_Borders)) {
        if (type.wheels.empty()) ImGui::TextDisabled("This fixture has no wheels. Add one on the left.");
        else changed |= drawWheel(type, static_cast<std::size_t>(selectedWheel_));
    }
    ImGui::EndChild();
    return changed;
}

// ---------------------------------------------------------------------------
// Wheel list

bool WheelsSection::drawWheelList(fixtures::FixtureType& type) {
    bool changed = false;
    if (ImGui::SmallButton("Add")) {
        std::vector<std::string> names;
        for (const Wheel& w : type.wheels) names.push_back(w.name);
        Wheel wheel;
        wheel.name = uniqueName("Wheel", names);
        wheel.slots.push_back(makeSlot(SlotKind::Open, 1));
        type.wheels.push_back(std::move(wheel));
        selectedWheel_ = static_cast<int>(type.wheels.size()) - 1;
        changed = true;
    }
    ImGui::SameLine();
    ImGui::BeginDisabled(type.wheels.empty());
    if (ImGui::SmallButton("Remove")) {
        type.wheels.erase(type.wheels.begin() + selectedWheel_);
        removeUnusedResources(type);
        changed = true;
    }
    ImGui::SetItemTooltip("Channel functions that still use the wheel are reported by the validation.");
    ImGui::EndDisabled();
    ImGui::SameLine();
    ImGui::BeginDisabled(selectedWheel_ <= 0);
    if (ImGui::SmallButton("Up")) {
        std::swap(type.wheels[static_cast<std::size_t>(selectedWheel_)], type.wheels[static_cast<std::size_t>(selectedWheel_ - 1)]);
        --selectedWheel_;
        changed = true;
    }
    ImGui::EndDisabled();
    ImGui::SameLine();
    ImGui::BeginDisabled(selectedWheel_ + 1 >= static_cast<int>(type.wheels.size()));
    if (ImGui::SmallButton("Down")) {
        std::swap(type.wheels[static_cast<std::size_t>(selectedWheel_)], type.wheels[static_cast<std::size_t>(selectedWheel_ + 1)]);
        ++selectedWheel_;
        changed = true;
    }
    ImGui::EndDisabled();
    ImGui::Separator();

    for (std::size_t i = 0; i < type.wheels.size(); ++i) {
        ImGui::PushID(static_cast<int>(i));
        const Wheel& wheel = type.wheels[i];
        const std::string label = std::format("{} ({})", wheel.name.empty() ? "(unnamed)" : wheel.name, wheel.slots.size());
        if (ImGui::Selectable(label.c_str(), static_cast<int>(i) == selectedWheel_)) selectedWheel_ = static_cast<int>(i);
        ImGui::PopID();
    }
    return changed;
}

// ---------------------------------------------------------------------------
// Wheel

bool WheelsSection::drawWheel(fixtures::FixtureType& type, std::size_t wheelIndex) {
    bool changed = false;
    Wheel& wheel = type.wheels[wheelIndex];

    // The name is a reference key of channel functions: applied when the field is left, not per keystroke.
    if (nameEditWheel_ != selectedWheel_ || nameEditSource_ != wheel.name) {
        nameEdit_ = wheel.name;
        nameEditSource_ = wheel.name;
        nameEditWheel_ = selectedWheel_;
    }
    bool nameTaken = nameEdit_.empty();
    for (std::size_t i = 0; i < type.wheels.size(); ++i)
        if (i != wheelIndex && type.wheels[i].name == nameEdit_) nameTaken = true;
    if (nameTaken) ImGui::PushStyleColor(ImGuiCol_Text, errorColor());
    ImGui::InputText("Wheel name", &nameEdit_);
    if (nameTaken) ImGui::PopStyleColor();
    if (ImGui::IsItemDeactivatedAfterEdit()) {
        if (!nameTaken && renameWheel(type, wheelIndex, nameEdit_)) changed = true;
        else nameEdit_ = wheel.name;
    }
    ImGui::SetItemTooltip("Channel functions refer to the wheel by name; renaming updates them.");

    const ImGuiTableFlags tableFlags = ImGuiTableFlags_RowBg | ImGuiTableFlags_BordersInnerH | ImGuiTableFlags_SizingFixedFit |
                                       ImGuiTableFlags_ScrollY;
    const float footer = ImGui::GetFrameHeightWithSpacing() * 2.0f;
    if (ImGui::BeginTable("##slots", 5, tableFlags, ImVec2(0.0f, -footer))) {
        ImGui::TableSetupScrollFreeze(0, 1);
        ImGui::TableSetupColumn("#", ImGuiTableColumnFlags_WidthFixed, 24.0f);
        ImGui::TableSetupColumn("Type", ImGuiTableColumnFlags_WidthFixed, 100.0f);
        ImGui::TableSetupColumn("Name", ImGuiTableColumnFlags_WidthFixed, 130.0f);
        ImGui::TableSetupColumn("Content", ImGuiTableColumnFlags_WidthStretch);
        ImGui::TableSetupColumn("##actions", ImGuiTableColumnFlags_WidthFixed, 100.0f);
        ImGui::TableHeadersRow();
        for (std::size_t s = 0; s < wheel.slots.size(); ++s) {
            ImGui::PushID(static_cast<int>(s));
            ImGui::TableNextRow();
            changed |= drawSlotRow(type, wheel, s);
            ImGui::PopID();
        }
        ImGui::EndTable();
    }

    // Add slot.
    for (int k = 0; k < static_cast<int>(std::size(kSlotNames)); ++k) {
        if (k > 0) ImGui::SameLine();
        if (ImGui::SmallButton(std::format("+{}", kSlotNames[k]).c_str())) {
            wheel.slots.push_back(makeSlot(static_cast<SlotKind>(k), static_cast<int>(wheel.slots.size()) + 1));
            changed = true;
        }
    }
    if (!message_.empty()) ImGui::TextColored(errorColor(), "%s", message_.c_str());

    changed |= runSlotAction(wheel);
    return changed;
}

bool WheelsSection::drawSlotRow(fixtures::FixtureType& type, Wheel& wheel, std::size_t slotIndex) {
    bool changed = false;
    WheelSlot& slot = wheel.slots[slotIndex];

    ImGui::TableSetColumnIndex(0);
    ImGui::AlignTextToFramePadding();
    ImGui::Text("%d", static_cast<int>(slotIndex) + 1);

    ImGui::TableSetColumnIndex(1);
    int kind = static_cast<int>(slot.kind);
    ImGui::SetNextItemWidth(-FLT_MIN);
    if (comboIndex("##kind", kind, kSlotNames)) {
        const std::string oldDefault = makeSlot(slot.kind, static_cast<int>(slotIndex) + 1).name;
        const bool keepName = slot.name != oldDefault;
        const std::string oldName = slot.name;
        const bool keepImage = hasImage(slot.kind) && hasImage(static_cast<SlotKind>(kind));
        const std::string oldImage = slot.image;
        slot = makeSlot(static_cast<SlotKind>(kind), static_cast<int>(slotIndex) + 1);
        if (keepName) slot.name = oldName;
        if (keepImage) slot.image = oldImage;
        removeUnusedResources(type);
        changed = true;
    }

    ImGui::TableSetColumnIndex(2);
    ImGui::SetNextItemWidth(-FLT_MIN);
    changed |= inputText("##name", slot.name);

    ImGui::TableSetColumnIndex(3);
    switch (slot.kind) {
        case SlotKind::Open: ImGui::TextDisabled("Light passes unchanged"); break;
        case SlotKind::Color: changed |= colorEditLinear("##color", slot.color, ImGuiColorEditFlags_NoInputs | ImGuiColorEditFlags_PickerHueWheel); break;
        case SlotKind::Gobo:
        case SlotKind::AnimationWheel: changed |= drawImageSlot(type, slot, slotIndex); break;
        case SlotKind::Prism: changed |= drawPrismSlot(slot); break;
        case SlotKind::Frost:
            ImGui::SetNextItemWidth(160.0f);
            changed |= ImGui::SliderFloat("##frost", &slot.frost, 0.0f, 1.0f, "frost %.2f");
            break;
    }

    ImGui::TableSetColumnIndex(4);
    if (ImGui::SmallButton("Up") && slotIndex > 0) {
        pending_ = Action::SlotUp;
        pendingSlot_ = slotIndex;
    }
    ImGui::SameLine();
    if (ImGui::SmallButton("Dn") && slotIndex + 1 < wheel.slots.size()) {
        pending_ = Action::SlotDown;
        pendingSlot_ = slotIndex;
    }
    ImGui::SameLine();
    if (ImGui::SmallButton("Del")) {
        pending_ = Action::RemoveSlot;
        pendingSlot_ = slotIndex;
    }
    return changed;
}

bool WheelsSection::drawImageSlot(fixtures::FixtureType& type, WheelSlot& slot, std::size_t slotIndex) {
    bool changed = false;
    thumbnails_.draw(type.findResource(slot.image), kThumbnailSize);
    ImGui::SameLine();
    ImGui::BeginGroup();

    ImGui::SetNextItemWidth(150.0f);
    if (ImGui::BeginCombo("##image", slot.image.empty() ? "(no image)" : slot.image.c_str())) {
        if (ImGui::Selectable("(no image)", slot.image.empty())) {
            slot.image.clear();
            changed = true;
        }
        for (const fixtures::Resource& r : type.resources) {
            if (!fixtures::isImageFormat(r.format)) continue;
            if (ImGui::Selectable(r.name.c_str(), r.name == slot.image)) {
                slot.image = r.name;
                changed = true;
            }
        }
        ImGui::EndCombo();
    }
    ImGui::BeginDisabled(!FileDialogs::available() || dialogs_.busy());
    if (ImGui::SmallButton("Import image...")) {
        importWheel_ = selectedWheel_;
        importSlot_ = static_cast<int>(slotIndex);
        dialogs_.requestOpen("Import gobo image", lastImportDir_, {"Images (PNG, JPG, SVG)", "*.png *.jpg *.jpeg *.svg", "All files", "*"});
    }
    ImGui::EndDisabled();
    ImGui::SetItemTooltip(FileDialogs::available() ? "White passes the light, black blocks it. The file is stored in the fixture."
                                                   : "No file dialog available (on Linux install zenity or kdialog).");
    ImGui::EndGroup();
    return changed;
}

bool WheelsSection::drawPrismSlot(WheelSlot& slot) {
    bool changed = false;
    if (ImGui::TreeNodeEx("##facets", ImGuiTreeNodeFlags_SpanAvailWidth, "%d facet%s", static_cast<int>(slot.facets.size()),
                          slot.facets.size() == 1 ? "" : "s")) {
        ImGui::SetNextItemWidth(90.0f);
        ImGui::InputInt("Count", &facetCount_);
        facetCount_ = std::clamp(facetCount_, 1, kMaxPrismFacets);
        ImGui::SetNextItemWidth(90.0f);
        ImGui::DragFloat("Spread", &facetSpreadDegrees_, 0.1f, 0.1f, 60.0f, "%.1f deg");
        ImGui::SetItemTooltip("Circular: angle between each sub-beam and the main axis.\nLinear: angle between neighbouring sub-beams.");
        ImGui::SetNextItemWidth(110.0f);
        comboIndex("Pattern", facetPattern_, kPatternNames);
        if (ImGui::SmallButton("Generate")) {
            const float spread = degToRad(facetSpreadDegrees_);
            slot.facets = facetPattern_ == 0 ? fixtures::makeCircularPrismFacets(facetCount_, spread)
                                             : fixtures::makeLinearPrismFacets(facetCount_, spread);
            changed = true;
        }

        int removeIndex = -1;
        for (std::size_t f = 0; f < slot.facets.size(); ++f) {
            ImGui::PushID(static_cast<int>(f));
            float degrees[2] = {radToDeg(slot.facets[f].x), radToDeg(slot.facets[f].y)};
            ImGui::SetNextItemWidth(150.0f);
            if (ImGui::DragFloat2("##facet", degrees, 0.1f, -90.0f, 90.0f, "%.1f")) {
                slot.facets[f] = glm::vec2(degToRad(degrees[0]), degToRad(degrees[1]));
                changed = true;
            }
            ImGui::SameLine();
            if (ImGui::SmallButton("x")) removeIndex = static_cast<int>(f);
            ImGui::PopID();
        }
        if (removeIndex >= 0) {
            slot.facets.erase(slot.facets.begin() + removeIndex);
            changed = true;
        }
        ImGui::BeginDisabled(slot.facets.size() >= static_cast<std::size_t>(kMaxPrismFacets));
        if (ImGui::SmallButton("Add facet")) {
            slot.facets.emplace_back(0.0f, 0.0f);
            changed = true;
        }
        ImGui::EndDisabled();
        ImGui::TextDisabled("Offsets (right, up) in degrees, looking along the beam.");
        ImGui::TreePop();
    }
    return changed;
}

// ---------------------------------------------------------------------------

bool WheelsSection::runSlotAction(Wheel& wheel) {
    const Action action = pending_;
    pending_ = Action::None;
    if (action == Action::None || pendingSlot_ >= wheel.slots.size()) return false;
    switch (action) {
        case Action::RemoveSlot: wheel.slots.erase(wheel.slots.begin() + static_cast<std::ptrdiff_t>(pendingSlot_)); return true;
        case Action::SlotUp:
            if (pendingSlot_ == 0) return false;
            std::swap(wheel.slots[pendingSlot_], wheel.slots[pendingSlot_ - 1]);
            return true;
        case Action::SlotDown:
            if (pendingSlot_ + 1 >= wheel.slots.size()) return false;
            std::swap(wheel.slots[pendingSlot_], wheel.slots[pendingSlot_ + 1]);
            return true;
        case Action::None: break;
    }
    return false;
}

void WheelsSection::pollImageImport(fixtures::FixtureType& type, bool& changed) {
    const std::optional<FileDialogs::Result> result = dialogs_.poll();
    if (!result || result->path.empty()) return;
    lastImportDir_ = result->path.parent_path();

    std::string error;
    std::optional<ImportedFile> file = readImageFile(result->path, &error);
    if (!file) {
        message_ = "Cannot use " + result->path.filename().string() + ": " + error;
        return;
    }
    if (importWheel_ < 0 || importWheel_ >= static_cast<int>(type.wheels.size()) || importSlot_ < 0 ||
        importSlot_ >= static_cast<int>(type.wheels[static_cast<std::size_t>(importWheel_)].slots.size())) {
        message_ = "The slot for this image no longer exists.";
        return;
    }
    message_.clear();
    WheelSlot& slot = type.wheels[static_cast<std::size_t>(importWheel_)].slots[static_cast<std::size_t>(importSlot_)];
    const bool defaultName = slot.name == makeSlot(slot.kind, importSlot_ + 1).name;
    const std::string stem = file->name;
    slot.image = addResource(type, stem, file->format, std::move(file->data));
    if (defaultName) slot.name = stem;
    removeUnusedResources(type);  // the image this one replaced
    changed = true;
}

}  // namespace dmxviz::ui::fixture_editor
