#include "ui/fixture_editor/ModesSection.h"

#include "ui/fixture_editor/ChannelEditing.h"
#include "ui/fixture_editor/GeometryEditing.h"

#include "imgui.h"

#include <algorithm>
#include <format>
#include <string>
#include <vector>

namespace dmxviz::ui::fixture_editor {

using fixtures::Attribute;
using fixtures::Channel;
using fixtures::ChannelFunction;
using fixtures::DmxMode;
using fixtures::FunctionKind;
using fixtures::PhysicalUnit;

namespace {

constexpr ImGuiTableFlags kTableFlags = ImGuiTableFlags_RowBg | ImGuiTableFlags_BordersInnerH |
                                        ImGuiTableFlags_BordersOuter | ImGuiTableFlags_ScrollX |
                                        ImGuiTableFlags_ScrollY | ImGuiTableFlags_SizingFixedFit;

// Display format and drag speed of a physical value (shown in degrees for angles).
const char* physicalFormat(PhysicalUnit unit) {
    switch (unit) {
        case PhysicalUnit::Angle:
            return "%.1f deg";
        case PhysicalUnit::AngularSpeed:
            return "%.1f deg/s";
        case PhysicalUnit::Frequency:
            return "%.2f Hz";
        case PhysicalUnit::Temperature:
            return "%.0f K";
        case PhysicalUnit::Time:
            return "%.2f s";
        case PhysicalUnit::Ratio:
            return "%.3f";
        case PhysicalUnit::None:
            return "%.2f";
    }
    return "%.2f";
}

float physicalSpeed(PhysicalUnit unit) {
    switch (unit) {
        case PhysicalUnit::Angle:
        case PhysicalUnit::AngularSpeed:
            return 0.5f;
        case PhysicalUnit::Frequency:
            return 0.05f;
        case PhysicalUnit::Temperature:
            return 10.0f;
        case PhysicalUnit::Time:
            return 0.01f;
        case PhysicalUnit::Ratio:
            return 0.005f;
        case PhysicalUnit::None:
            return 0.05f;
    }
    return 0.1f;
}

// Does the physical range mean something for this kind of function?
bool usesPhysical(FunctionKind kind) {
    return kind == FunctionKind::Linear || kind == FunctionKind::Spin || fixtures::isStrobeKind(kind);
}

bool dragDmx(const char* id, std::uint32_t& value, std::uint32_t maxValue) {
    const std::uint32_t zero = 0;
    ImGui::SetNextItemWidth(-FLT_MIN);
    return ImGui::DragScalar(id, ImGuiDataType_U32, &value, std::max(1.0f, static_cast<float>(maxValue) / 512.0f),
                             &zero, &maxValue);
}

}  // namespace

void ModesSection::reset() {
    mode_ = 0;
    channel_ = -1;
    function_ = -1;
    selectedFunction_ = -1;
    scrollToFunction_ = false;
    pending_ = Action::None;
}

void ModesSection::select(int mode, int channel, int function) {
    if (mode >= 0) mode_ = mode;
    channel_ = channel;
    function_ = function;
    selectedFunction_ = function;
    scrollToFunction_ = function >= 0;
}

// ---------------------------------------------------------------------------
// Frame

bool ModesSection::draw(EditDocument& doc) {
    fixtures::FixtureType& type = doc.type();
    bool changed = drawModeBar(type);
    if (type.modes.empty()) {
        ImGui::TextDisabled("This fixture has no DMX mode. Add one to define its channels.");
        return changed;
    }
    mode_ = std::clamp(mode_, 0, static_cast<int>(type.modes.size()) - 1);
    DmxMode& mode = type.modes[static_cast<std::size_t>(mode_)];
    channel_ = std::clamp(channel_, mode.channels.empty() ? -1 : 0, static_cast<int>(mode.channels.size()) - 1);

    const float tableHeight = std::max(150.0f, ImGui::GetContentRegionAvail().y * 0.42f);
    if (ImGui::BeginChild("##channels", ImVec2(0.0f, tableHeight), ImGuiChildFlags_Borders)) {
        changed |= drawChannelTable(type, mode);
        changed |= drawChannelFooter(type, mode);
    }
    ImGui::EndChild();

    if (ImGui::BeginChild("##channelDetails", ImVec2(0.0f, 0.0f), ImGuiChildFlags_Borders)) {
        if (channel_ >= 0)
            changed |= drawChannelDetails(type, mode, static_cast<std::size_t>(channel_));
        else
            ImGui::TextDisabled("Select a channel to edit its functions.");
    }
    ImGui::EndChild();

    changed |= runAction(mode);
    return changed;
}

// ---------------------------------------------------------------------------
// Modes

bool ModesSection::drawModeBar(fixtures::FixtureType& type) {
    bool changed = false;
    mode_ = std::clamp(mode_, 0, std::max(0, static_cast<int>(type.modes.size()) - 1));

    const std::string preview = type.modes.empty()
                                    ? std::string("(no modes)")
                                    : std::format("{} ({} ch)", type.modes[static_cast<std::size_t>(mode_)].name,
                                                  type.modes[static_cast<std::size_t>(mode_)].footprint);
    ImGui::SetNextItemWidth(240.0f);
    if (ImGui::BeginCombo("Mode", preview.c_str())) {
        for (std::size_t i = 0; i < type.modes.size(); ++i) {
            ImGui::PushID(static_cast<int>(i));
            if (ImGui::Selectable(std::format("{} ({} ch)", type.modes[i].name, type.modes[i].footprint).c_str(),
                                  static_cast<int>(i) == mode_)) {
                mode_ = static_cast<int>(i);
                channel_ = -1;
            }
            ImGui::PopID();
        }
        ImGui::EndCombo();
    }
    ImGui::SameLine();
    if (ImGui::Button("Add")) {
        std::vector<std::string> names;
        for (const DmxMode& m : type.modes) names.push_back(m.name);
        DmxMode fresh;
        fresh.name = uniqueName("Mode", names);
        appendChannel(fresh, newChannelFor(type, Attribute::Dimmer));
        type.modes.push_back(std::move(fresh));
        mode_ = static_cast<int>(type.modes.size()) - 1;
        channel_ = 0;
        changed = true;
    }
    ImGui::SetItemTooltip("A new mode with a dimmer channel");
    ImGui::SameLine();
    ImGui::BeginDisabled(type.modes.empty());
    if (ImGui::Button("Duplicate")) {
        type.modes.push_back(duplicateMode(type, type.modes[static_cast<std::size_t>(mode_)]));
        mode_ = static_cast<int>(type.modes.size()) - 1;
        changed = true;
    }
    ImGui::SameLine();
    if (ImGui::Button("Delete")) {
        type.modes.erase(type.modes.begin() + mode_);
        mode_ = std::max(0, mode_ - 1);
        channel_ = -1;
        changed = true;
    }
    ImGui::SetItemTooltip("Fixtures patched in this mode need another mode afterwards");
    ImGui::EndDisabled();

    if (type.modes.empty()) return changed;
    DmxMode& mode = type.modes[static_cast<std::size_t>(mode_)];
    ImGui::SetNextItemWidth(240.0f);
    changed |= inputText("Name", mode.name);
    ImGui::SameLine();
    ImGui::SetNextItemWidth(90.0f);
    changed |= ImGui::DragInt("Footprint", &mode.footprint, 0.2f, 0, 512);
    ImGui::SetItemTooltip("DMX slots the mode occupies. It must cover the highest channel offset.");
    ImGui::SameLine();
    const int highest = mode.highestOffset();
    if (mode.footprint != highest) {
        if (ImGui::SmallButton("Fit")) {
            mode.footprint = highest;
            changed = true;
        }
        ImGui::SetItemTooltip("Set the footprint to the highest channel offset (%d)", highest);
    } else {
        ImGui::TextDisabled("fits the channels");
    }

    ImGui::SetNextItemWidth(240.0f);
    changed |= inputText("Description", mode.description);
    ImGui::SameLine();
    // The part of the geometry tree this mode controls (GDTF modes that show different bodies).
    const bool rootKnown = mode.geometryRoot.empty() || type.findGeometry(mode.geometryRoot) != nullptr;
    if (!rootKnown) ImGui::PushStyleColor(ImGuiCol_Text, errorColor());
    ImGui::SetNextItemWidth(150.0f);
    const bool rootOpen =
        ImGui::BeginCombo("Geometry root", mode.geometryRoot.empty() ? "(whole fixture)" : mode.geometryRoot.c_str());
    if (!rootKnown) ImGui::PopStyleColor();
    if (rootOpen) {
        if (ImGui::Selectable("(whole fixture)", mode.geometryRoot.empty())) {
            mode.geometryRoot.clear();
            changed = true;
        }
        fixtures::forEachGeometry(type.geometry, [&](const fixtures::Geometry& g, const fixtures::Geometry*) {
            if (ImGui::Selectable(g.name.c_str(), g.name == mode.geometryRoot)) {
                mode.geometryRoot = g.name;
                changed = true;
            }
        });
        ImGui::EndCombo();
    }
    ImGui::SetItemTooltip("Only this node and what hangs below it belongs to the fixture in this mode");
    return changed;
}

// ---------------------------------------------------------------------------
// Channels

bool ModesSection::drawChannelTable(fixtures::FixtureType& type, DmxMode& mode) {
    bool changed = false;
    const float footer = ImGui::GetFrameHeightWithSpacing() + 4.0f;
    if (!ImGui::BeginTable("##channelTable", 9, kTableFlags, ImVec2(0.0f, -footer))) return false;
    ImGui::TableSetupScrollFreeze(0, 1);
    ImGui::TableSetupColumn("#", ImGuiTableColumnFlags_WidthFixed, 28.0f);
    ImGui::TableSetupColumn("Name", ImGuiTableColumnFlags_WidthFixed, 130.0f);
    ImGui::TableSetupColumn("Coarse", ImGuiTableColumnFlags_WidthFixed, 58.0f);
    ImGui::TableSetupColumn("Fine", ImGuiTableColumnFlags_WidthFixed, 58.0f);
    ImGui::TableSetupColumn("Ultra", ImGuiTableColumnFlags_WidthFixed, 58.0f);
    ImGui::TableSetupColumn("Geometry", ImGuiTableColumnFlags_WidthFixed, 130.0f);
    ImGui::TableSetupColumn("Attribute", ImGuiTableColumnFlags_WidthFixed, 160.0f);
    ImGui::TableSetupColumn("Default", ImGuiTableColumnFlags_WidthFixed, 80.0f);
    ImGui::TableSetupColumn("##actions", ImGuiTableColumnFlags_WidthFixed, 130.0f);
    ImGui::TableHeadersRow();
    for (std::size_t i = 0; i < mode.channels.size(); ++i) {
        ImGui::PushID(static_cast<int>(i));
        changed |= drawChannelRow(type, mode, i);
        ImGui::PopID();
    }
    ImGui::EndTable();
    return changed;
}

bool ModesSection::drawChannelRow(fixtures::FixtureType& type, DmxMode& mode, std::size_t index) {
    bool changed = false;
    Channel& channel = mode.channels[index];
    ImGui::TableNextRow();

    ImGui::TableSetColumnIndex(0);
    char number[16];
    std::snprintf(number, sizeof(number), "%d", static_cast<int>(index) + 1);
    if (ImGui::Selectable(number, static_cast<int>(index) == channel_,
                          ImGuiSelectableFlags_SpanAllColumns | ImGuiSelectableFlags_AllowOverlap)) {
        channel_ = static_cast<int>(index);
        selectedFunction_ = -1;
        scrollToFunction_ = false;
    }

    ImGui::TableSetColumnIndex(1);
    ImGui::SetNextItemWidth(-FLT_MIN);
    changed |= inputText("##name", channel.name);

    static constexpr const char* kOffsetIds[3] = {"##coarse", "##fine", "##ultra"};
    static constexpr const char* kOffsetTips[3] = {
        "First DMX slot of the channel (0 = virtual channel: always the default value)",
        "DMX slot of the second byte (16 bit). 0 = 8 bit.", "DMX slot of the third byte (24 bit). 0 = none."};
    for (int slot = 0; slot < 3; ++slot) {
        ImGui::TableSetColumnIndex(2 + slot);
        int value =
            slot < static_cast<int>(channel.offsets.size()) ? channel.offsets[static_cast<std::size_t>(slot)] : 0;
        ImGui::BeginDisabled(slot > 0 && channel.offsets.empty());  // a virtual channel has no bytes to extend
        ImGui::SetNextItemWidth(-FLT_MIN);
        if (ImGui::InputInt(kOffsetIds[slot], &value, 0, 0)) {
            setChannelOffset(mode, index, slot, value);
            changed = true;
        }
        ImGui::EndDisabled();
        ImGui::SetItemTooltip("%s", kOffsetTips[slot]);
    }

    ImGui::TableSetColumnIndex(5);
    changed |= drawGeometryCombo(type, channel);

    ImGui::TableSetColumnIndex(6);
    Attribute attribute = channel.functions.empty() ? Attribute::NoFeature : channel.functions.front().attribute;
    if (channelPicker_.draw("##attribute", attribute, -FLT_MIN)) {
        setChannelAttribute(channel, attribute);
        changed = true;
    }
    if (channel.functions.size() > 1)
        ImGui::SetItemTooltip("%d functions; the first one is shown", static_cast<int>(channel.functions.size()));

    ImGui::TableSetColumnIndex(7);
    const std::uint32_t max = channel.maxValue();
    changed |= dragDmx("##default", channel.defaultValue, max);

    ImGui::TableSetColumnIndex(8);
    if (ImGui::SmallButton("Up") && index > 0) {
        pending_ = Action::ChannelUp;
        pendingIndex_ = index;
    }
    ImGui::SameLine();
    if (ImGui::SmallButton("Dn") && index + 1 < mode.channels.size()) {
        pending_ = Action::ChannelDown;
        pendingIndex_ = index;
    }
    ImGui::SameLine();
    if (ImGui::SmallButton("Dup")) {
        pending_ = Action::ChannelDuplicate;
        pendingIndex_ = index;
    }
    ImGui::SameLine();
    if (ImGui::SmallButton("Del")) {
        pending_ = Action::ChannelDelete;
        pendingIndex_ = index;
    }
    return changed;
}

bool ModesSection::drawGeometryCombo(const fixtures::FixtureType& type, Channel& channel) {
    bool changed = false;
    const bool known = channel.geometry.empty() || type.findGeometry(channel.geometry) != nullptr ||
                       type.findGroup(channel.geometry) != nullptr;
    const std::string preview = channel.geometry.empty() ? std::string("(whole fixture)") : channel.geometry;
    if (!known) ImGui::PushStyleColor(ImGuiCol_Text, errorColor());
    ImGui::SetNextItemWidth(-FLT_MIN);
    const bool open = ImGui::BeginCombo("##geometry", preview.c_str());
    if (!known) ImGui::PopStyleColor();
    if (!open) return false;

    if (ImGui::Selectable("(whole fixture)", channel.geometry.empty())) {
        channel.geometry.clear();
        changed = true;
    }
    fixtures::forEachGeometry(type.geometry, [&](const fixtures::Geometry& g, const fixtures::Geometry*) {
        if (ImGui::Selectable(g.name.c_str(), g.name == channel.geometry)) {
            channel.geometry = g.name;
            changed = true;
        }
    });
    if (!type.geometryGroups.empty()) ImGui::SeparatorText("Groups");
    for (const fixtures::GeometryGroup& group : type.geometryGroups) {
        if (ImGui::Selectable(group.name.c_str(), group.name == channel.geometry)) {
            channel.geometry = group.name;
            changed = true;
        }
    }
    ImGui::EndCombo();
    return changed;
}

bool ModesSection::drawChannelFooter(fixtures::FixtureType& type, DmxMode& mode) {
    bool changed = false;
    ImGui::AlignTextToFramePadding();
    ImGui::TextUnformatted("New channel");
    ImGui::SameLine();
    newChannelPicker_.draw("##newAttribute", newChannelAttribute_, 190.0f);
    ImGui::SameLine();
    if (ImGui::Button("Add channel")) {
        Channel channel = newChannelFor(type, newChannelAttribute_);
        std::vector<std::string> names;
        for (const Channel& c : mode.channels) names.push_back(c.name);
        channel.name = uniqueName(channel.name, names);
        appendChannel(mode, std::move(channel));
        channel_ = static_cast<int>(mode.channels.size()) - 1;
        changed = true;
    }
    ImGui::SetItemTooltip("Appended after the last DMX slot, with the geometry this attribute usually controls");
    ImGui::SameLine();
    if (ImGui::Button("Pack offsets")) {
        renumberOffsets(mode);
        changed = true;
    }
    ImGui::SetItemTooltip("Give the channels consecutive DMX slots in list order and fit the footprint");
    return changed;
}

// ---------------------------------------------------------------------------
// Functions

bool ModesSection::drawChannelDetails(fixtures::FixtureType& type, DmxMode& mode, std::size_t channelIndex) {
    bool changed = false;
    Channel& channel = mode.channels[channelIndex];
    ImGui::Text("Channel %d: %s (%d bit)", static_cast<int>(channelIndex) + 1, channel.name.c_str(),
                8 * channel.byteCount());

    ImGui::SameLine();
    bool highlight = channel.highlightValue.has_value();
    if (ImGui::Checkbox("Highlight value", &highlight)) {
        channel.highlightValue = highlight ? std::optional<std::uint32_t>(channel.maxValue()) : std::nullopt;
        changed = true;
    }
    ImGui::SetItemTooltip("The value the channel takes when the fixture is highlighted (\"Locate\")");
    if (channel.highlightValue) {
        ImGui::SameLine();
        ImGui::SetNextItemWidth(90.0f);
        std::uint32_t value = *channel.highlightValue;
        if (dragDmx("##highlight", value, channel.maxValue())) {
            channel.highlightValue = value;
            changed = true;
        }
    }
    changed |= drawFunctionTable(type, mode, channel);
    return changed;
}

bool ModesSection::drawFunctionTable(fixtures::FixtureType& type, DmxMode& mode, Channel& channel) {
    bool changed = false;
    const bool showDetails = selectedFunction_ >= 0 && selectedFunction_ < static_cast<int>(channel.functions.size());
    const float detailsHeight = ImGui::GetFrameHeightWithSpacing() * 4.0f;
    const float footer = ImGui::GetFrameHeightWithSpacing() + 4.0f +
                         (showDetails ? detailsHeight + ImGui::GetStyle().ItemSpacing.y : 0.0f);
    if (ImGui::BeginTable("##functionTable", 12, kTableFlags, ImVec2(0.0f, -footer))) {
        ImGui::TableSetupScrollFreeze(0, 1);
        ImGui::TableSetupColumn("#", ImGuiTableColumnFlags_WidthFixed, 26.0f);
        ImGui::TableSetupColumn("DMX from", ImGuiTableColumnFlags_WidthFixed, 74.0f);
        ImGui::TableSetupColumn("DMX to", ImGuiTableColumnFlags_WidthFixed, 74.0f);
        ImGui::TableSetupColumn("Attribute", ImGuiTableColumnFlags_WidthFixed, 160.0f);
        ImGui::TableSetupColumn("Kind", ImGuiTableColumnFlags_WidthFixed, 120.0f);
        ImGui::TableSetupColumn("Physical from", ImGuiTableColumnFlags_WidthFixed, 90.0f);
        ImGui::TableSetupColumn("Physical to", ImGuiTableColumnFlags_WidthFixed, 90.0f);
        ImGui::TableSetupColumn("Wheel", ImGuiTableColumnFlags_WidthFixed, 120.0f);
        ImGui::TableSetupColumn("Slot from", ImGuiTableColumnFlags_WidthFixed, 64.0f);
        ImGui::TableSetupColumn("Slot to", ImGuiTableColumnFlags_WidthFixed, 64.0f);
        ImGui::TableSetupColumn("Name", ImGuiTableColumnFlags_WidthFixed, 150.0f);
        ImGui::TableSetupColumn("##actions", ImGuiTableColumnFlags_WidthFixed, 100.0f);
        ImGui::TableHeadersRow();
        for (std::size_t i = 0; i < channel.functions.size(); ++i) {
            ImGui::PushID(static_cast<int>(i));
            changed |= drawFunctionRow(type, mode, channel, i);
            ImGui::PopID();
        }
        ImGui::EndTable();
    }

    if (showDetails) {
        if (ImGui::BeginChild("##functionDetails", ImVec2(0.0f, detailsHeight), ImGuiChildFlags_Borders))
            changed |= drawFunctionDetails(type, mode, channel,
                                           channel.functions[static_cast<std::size_t>(selectedFunction_)]);
        ImGui::EndChild();
    }

    const std::uint32_t max = channel.maxValue();
    if (ImGui::Button("Add function")) {
        const std::uint32_t last = channel.functions.empty() ? 0 : channel.functions.back().dmxTo;
        const std::uint32_t from = channel.functions.empty() ? 0 : std::min(last + 1, max);
        const Attribute attribute =
            channel.functions.empty() ? Attribute::NoFeature : channel.functions.back().attribute;
        channel.functions.push_back(makeFunction(attribute, from, max));
        changed = true;
    }
    ImGui::SetItemTooltip("Appends a function from the end of the last one to the channel maximum");
    ImGui::SameLine();
    if (ImGui::Button("Close gaps")) {
        closeFunctionGaps(channel);
        changed = true;
    }
    ImGui::SetItemTooltip(
        "Sort the functions by DMX start and stretch them so no DMX value is left without a function");
    ImGui::SameLine();
    ImGui::BeginDisabled(type.wheels.empty());
    if (ImGui::Button("Fill from wheel...")) ImGui::OpenPopup("fillFromWheel");
    ImGui::EndDisabled();
    ImGui::SetItemTooltip("Replace the functions by one function per slot of a wheel");
    if (ImGui::BeginPopup("fillFromWheel")) {
        const Attribute attribute = channel.functions.empty() ? Attribute::Gobo1 : channel.functions.front().attribute;
        for (const fixtures::Wheel& wheel : type.wheels) {
            if (ImGui::Selectable(wheel.name.c_str())) {
                fillWheelFunctions(channel, wheel, attribute);
                changed = true;
            }
        }
        ImGui::EndPopup();
    }
    return changed;
}

bool ModesSection::drawFunctionRow(fixtures::FixtureType& type, DmxMode& mode, Channel& channel, std::size_t index) {
    (void)mode;
    bool changed = false;
    ChannelFunction& f = channel.functions[index];
    const std::uint32_t max = channel.maxValue();
    const fixtures::AttributeInfo& info = fixtures::attributeInfo(f.attribute);
    ImGui::TableNextRow();

    ImGui::TableSetColumnIndex(0);
    char number[16];
    std::snprintf(number, sizeof(number), "%d", static_cast<int>(index) + 1);
    if (ImGui::Selectable(number, static_cast<int>(index) == selectedFunction_,
                          ImGuiSelectableFlags_SpanAllColumns | ImGuiSelectableFlags_AllowOverlap))
        selectedFunction_ = static_cast<int>(index);
    if (scrollToFunction_ && static_cast<int>(index) == function_) {
        ImGui::SetScrollHereY();
        scrollToFunction_ = false;
    }

    ImGui::TableSetColumnIndex(1);
    changed |= dragDmx("##from", f.dmxFrom, max);
    ImGui::TableSetColumnIndex(2);
    changed |= dragDmx("##to", f.dmxTo, max);

    ImGui::TableSetColumnIndex(3);
    Attribute attribute = f.attribute;
    if (functionPicker_.draw("##attribute", attribute, -FLT_MIN)) {
        const ChannelFunction fresh = makeFunction(attribute, f.dmxFrom, f.dmxTo);
        f.attribute = attribute;
        f.kind = fresh.kind;
        f.physicalFrom = fresh.physicalFrom;
        f.physicalTo = fresh.physicalTo;
        changed = true;
    }

    ImGui::TableSetColumnIndex(4);
    ImGui::SetNextItemWidth(-FLT_MIN);
    const std::string kindName(fixtures::functionKindName(f.kind));
    if (ImGui::BeginCombo("##kind", kindName.c_str())) {
        for (int k = 0; k < static_cast<int>(FunctionKind::Count); ++k) {
            const auto kind = static_cast<FunctionKind>(k);
            if (!ImGui::Selectable(std::string(fixtures::functionKindName(kind)).c_str(), kind == f.kind)) continue;
            f.kind = kind;
            if (kind == FunctionKind::WheelSlot) {
                if (f.wheel.empty() && !type.wheels.empty()) f.wheel = type.wheels.front().name;
                f.slotFrom = std::max(f.slotFrom, 1.0f);
                f.slotTo = std::max(f.slotTo, 1.0f);
            }
            changed = true;
        }
        ImGui::EndCombo();
    }

    // Physical range, in the unit of the attribute (degrees for angles).
    for (int end = 0; end < 2; ++end) {
        ImGui::TableSetColumnIndex(5 + end);
        if (!usesPhysical(f.kind)) {
            ImGui::TextDisabled("-");
            continue;
        }
        float& memory = end == 0 ? f.physicalFrom : f.physicalTo;
        float shown = fixtures::physicalToFile(f.attribute, memory);
        ImGui::SetNextItemWidth(-FLT_MIN);
        if (ImGui::DragFloat(end == 0 ? "##physFrom" : "##physTo", &shown, physicalSpeed(info.unit), 0.0f, 0.0f,
                             physicalFormat(info.unit))) {
            memory = fixtures::physicalFromFile(f.attribute, shown);
            changed = true;
        }
    }

    // Wheel and slot range.
    const bool wheelFunction = f.kind == FunctionKind::WheelSlot;
    ImGui::TableSetColumnIndex(7);
    if (wheelFunction) {
        const bool known = type.findWheel(f.wheel) != nullptr;
        if (!known) ImGui::PushStyleColor(ImGuiCol_Text, errorColor());
        ImGui::SetNextItemWidth(-FLT_MIN);
        const bool open = ImGui::BeginCombo("##wheel", f.wheel.empty() ? "(none)" : f.wheel.c_str());
        if (!known) ImGui::PopStyleColor();
        if (open) {
            for (const fixtures::Wheel& wheel : type.wheels) {
                if (ImGui::Selectable(wheel.name.c_str(), wheel.name == f.wheel)) {
                    f.wheel = wheel.name;
                    changed = true;
                }
            }
            ImGui::EndCombo();
        }
    } else {
        ImGui::TextDisabled("-");
    }
    for (int end = 0; end < 2; ++end) {
        ImGui::TableSetColumnIndex(8 + end);
        if (!wheelFunction) {
            ImGui::TextDisabled("-");
            continue;
        }
        const fixtures::Wheel* wheel = type.findWheel(f.wheel);
        const float limit = wheel != nullptr ? static_cast<float>(wheel->slots.size()) + 1.0f : 64.0f;
        ImGui::SetNextItemWidth(-FLT_MIN);
        changed |= ImGui::DragFloat(end == 0 ? "##slotFrom" : "##slotTo", end == 0 ? &f.slotFrom : &f.slotTo, 0.05f,
                                    0.0f, limit, "%.2f");
    }

    ImGui::TableSetColumnIndex(10);
    ImGui::SetNextItemWidth(-FLT_MIN);
    changed |= inputText("##functionName", f.name);

    ImGui::TableSetColumnIndex(11);
    if (ImGui::SmallButton("Up") && index > 0) {
        pending_ = Action::FunctionUp;
        pendingIndex_ = index;
    }
    ImGui::SameLine();
    if (ImGui::SmallButton("Dn") && index + 1 < channel.functions.size()) {
        pending_ = Action::FunctionDown;
        pendingIndex_ = index;
    }
    ImGui::SameLine();
    if (ImGui::SmallButton("Del")) {
        pending_ = Action::FunctionDelete;
        pendingIndex_ = index;
    }
    return changed;
}

// The less common settings of one function: LED emitter, mode master, named ranges.
bool ModesSection::drawFunctionDetails(fixtures::FixtureType& type, DmxMode& mode, Channel& channel,
                                       ChannelFunction& f) {
    bool changed = false;
    ImGui::Text("Function %d of channel \"%s\":", selectedFunction_ + 1, channel.name.c_str());

    // LED emitter, for additive colour functions.
    if (fixtures::attributeFamily(f.attribute) == fixtures::AttributeFamily::ColorAdd) {
        ImGui::SameLine();
        ImGui::SetNextItemWidth(150.0f);
        const bool known = f.emitter.empty() || type.findEmitter(f.emitter) != nullptr;
        if (!known) ImGui::PushStyleColor(ImGuiCol_Text, errorColor());
        const bool open = ImGui::BeginCombo("Emitter", f.emitter.empty() ? "(standard colour)" : f.emitter.c_str());
        if (!known) ImGui::PopStyleColor();
        if (open) {
            if (ImGui::Selectable("(standard colour)", f.emitter.empty())) {
                f.emitter.clear();
                changed = true;
            }
            for (const fixtures::Emitter& emitter : type.emitters) {
                if (ImGui::Selectable(emitter.name.c_str(), emitter.name == f.emitter)) {
                    f.emitter = emitter.name;
                    changed = true;
                }
            }
            ImGui::EndCombo();
        }
        ImGui::SetItemTooltip("Emitters are defined on the General tab");
    }

    // Mode master: the function only works while another channel has a value in a range.
    ImGui::SetNextItemWidth(170.0f);
    const bool masterKnown = f.modeMaster.empty() || mode.findChannel(f.modeMaster) != nullptr;
    if (!masterKnown) ImGui::PushStyleColor(ImGuiCol_Text, errorColor());
    const bool masterOpen =
        ImGui::BeginCombo("Active while channel", f.modeMaster.empty() ? "(always)" : f.modeMaster.c_str());
    if (!masterKnown) ImGui::PopStyleColor();
    if (masterOpen) {
        if (ImGui::Selectable("(always)", f.modeMaster.empty())) {
            f.modeMaster.clear();
            changed = true;
        }
        for (const Channel& other : mode.channels) {
            if (&other == &channel) continue;
            if (ImGui::Selectable(other.name.c_str(), other.name == f.modeMaster)) {
                f.modeMaster = other.name;
                changed = true;
            }
        }
        ImGui::EndCombo();
    }
    ImGui::SetItemTooltip("For fixtures where one channel switches what another channel does");
    if (!f.modeMaster.empty()) {
        const Channel* master = mode.findChannel(f.modeMaster);
        const std::uint32_t masterMax = master != nullptr ? master->maxValue() : 255u;
        ImGui::SameLine();
        ImGui::SetNextItemWidth(70.0f);
        changed |= dragDmx("##modeFrom", f.modeFrom, masterMax);
        ImGui::SameLine();
        ImGui::TextUnformatted("to");
        ImGui::SameLine();
        ImGui::SetNextItemWidth(70.0f);
        changed |= dragDmx("##modeTo", f.modeTo, masterMax);
    }

    // Named ranges (labels for the DMX sub-ranges of a function).
    char header[64];
    std::snprintf(header, sizeof(header), "Named ranges (%d)###sets", static_cast<int>(f.sets.size()));
    if (ImGui::TreeNode(header)) {
        int removeIndex = -1;
        for (std::size_t i = 0; i < f.sets.size(); ++i) {
            ImGui::PushID(static_cast<int>(i));
            fixtures::ChannelSet& set = f.sets[i];
            ImGui::SetNextItemWidth(150.0f);
            changed |= inputText("##setName", set.name);
            ImGui::SameLine();
            ImGui::SetNextItemWidth(70.0f);
            changed |= dragDmx("##setFrom", set.dmxFrom, channel.maxValue());
            ImGui::SameLine();
            ImGui::SetNextItemWidth(70.0f);
            changed |= dragDmx("##setTo", set.dmxTo, channel.maxValue());
            ImGui::SameLine();
            if (ImGui::SmallButton("x")) removeIndex = static_cast<int>(i);
            ImGui::PopID();
        }
        if (removeIndex >= 0) {
            f.sets.erase(f.sets.begin() + removeIndex);
            changed = true;
        }
        if (ImGui::SmallButton("Add range")) {
            f.sets.push_back({"Range", f.dmxFrom, f.dmxTo});
            changed = true;
        }
        ImGui::TreePop();
    }
    return changed;
}

// ---------------------------------------------------------------------------
// Queued structure changes

bool ModesSection::runAction(DmxMode& mode) {
    const Action action = pending_;
    pending_ = Action::None;
    if (action == Action::None) return false;

    const bool functionAction =
        action == Action::FunctionUp || action == Action::FunctionDown || action == Action::FunctionDelete;
    if (functionAction) {
        if (channel_ < 0 || channel_ >= static_cast<int>(mode.channels.size())) return false;
        auto& functions = mode.channels[static_cast<std::size_t>(channel_)].functions;
        if (pendingIndex_ >= functions.size()) return false;
        switch (action) {
            case Action::FunctionUp:
                if (pendingIndex_ == 0) return false;
                std::swap(functions[pendingIndex_], functions[pendingIndex_ - 1]);
                return true;
            case Action::FunctionDown:
                if (pendingIndex_ + 1 >= functions.size()) return false;
                std::swap(functions[pendingIndex_], functions[pendingIndex_ + 1]);
                return true;
            case Action::FunctionDelete:
                functions.erase(functions.begin() + static_cast<std::ptrdiff_t>(pendingIndex_));
                return true;
            default:
                return false;
        }
    }

    auto& channels = mode.channels;
    if (pendingIndex_ >= channels.size()) return false;
    switch (action) {
        case Action::ChannelUp:
            if (pendingIndex_ == 0) return false;
            std::swap(channels[pendingIndex_], channels[pendingIndex_ - 1]);
            if (channel_ == static_cast<int>(pendingIndex_)) --channel_;
            return true;
        case Action::ChannelDown:
            if (pendingIndex_ + 1 >= channels.size()) return false;
            std::swap(channels[pendingIndex_], channels[pendingIndex_ + 1]);
            if (channel_ == static_cast<int>(pendingIndex_)) ++channel_;
            return true;
        case Action::ChannelDelete:
            channels.erase(channels.begin() + static_cast<std::ptrdiff_t>(pendingIndex_));
            channel_ = std::min(channel_, static_cast<int>(channels.size()) - 1);
            return true;
        case Action::ChannelDuplicate: {
            Channel copy = channels[pendingIndex_];
            std::vector<std::string> names;
            for (const Channel& c : channels) names.push_back(c.name);
            copy.name = uniqueName(copy.name + " copy", names);
            // A copy that shares the offsets would overlap: move it behind the last slot.
            const int bytes = static_cast<int>(copy.offsets.size());
            Channel& added = appendChannel(mode, std::move(copy));
            if (bytes == 0) added.offsets.clear();  // virtual channels stay virtual
            channel_ = static_cast<int>(channels.size()) - 1;
            return true;
        }
        default:
            return false;
    }
}

}  // namespace dmxviz::ui::fixture_editor
