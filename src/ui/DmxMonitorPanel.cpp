#include "ui/DmxMonitorPanel.h"

#include "ui/EditorContext.h"
#include "ui/PanelTitles.h"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdio>

namespace dmxviz::ui {

namespace {

constexpr float kMinCellWidth = 22.0f;     // below this the grid switches from 32 to 16 columns
constexpr float kMinCellHeight = 15.0f;
constexpr float kAddressColumnWidth = 34.0f;
constexpr float kSourcesWidth = 330.0f;
constexpr float kSideBySideMinWidth = 780.0f;

const ImU32 kCellDark = IM_COL32(30, 31, 36, 255);
const ImU32 kCellBright = IM_COL32(245, 150, 40, 255);

ImU32 mixColor(ImU32 a, ImU32 b, float t) {
    const auto channel = [t](ImU32 x, ImU32 y, int shift) {
        const float ca = static_cast<float>((x >> shift) & 0xFFu);
        const float cb = static_cast<float>((y >> shift) & 0xFFu);
        return static_cast<ImU32>(ca + (cb - ca) * t) & 0xFFu;
    };
    return IM_COL32(channel(a, b, IM_COL32_R_SHIFT), channel(a, b, IM_COL32_G_SHIFT), channel(a, b, IM_COL32_B_SHIFT), 255);
}

// A stable colour per fixture so neighbouring fixtures are told apart in the grid.
ImU32 fixtureColor(NodeId id) {
    const float hue = std::fmod(static_cast<float>(id) * 0.61803398875f, 1.0f);
    float r = 0.0f;
    float g = 0.0f;
    float b = 0.0f;
    ImGui::ColorConvertHSVtoRGB(hue, 0.6f, 0.95f, r, g, b);
    return ImGui::ColorConvertFloat4ToU32(ImVec4(r, g, b, 1.0f));
}

void formatValue(char* out, std::size_t size, std::uint8_t value, int format) {
    switch (format) {
        case 1:  // percent
            std::snprintf(out, size, "%d", (static_cast<int>(value) * 100 + 127) / 255);
            break;
        case 2:  // hex
            std::snprintf(out, size, "%02X", static_cast<unsigned>(value));
            break;
        default:
            std::snprintf(out, size, "%d", static_cast<int>(value));
            break;
    }
}

const char* byteName(int byte, int byteCount) {
    if (byteCount <= 1) return "";
    if (byte == 0) return " (coarse)";
    return byte == 1 ? " (fine)" : " (ultra fine)";
}

long long millisecondsSince(dmx::TimePoint now, dmx::TimePoint then) {
    if (then == dmx::TimePoint{} || then > now) return 0;
    return std::chrono::duration_cast<std::chrono::milliseconds>(now - then).count();
}

}  // namespace

const char* DmxMonitorPanel::title() const {
    return kDmxMonitorTitle;
}

void DmxMonitorPanel::draw(EditorContext& ctx) {
    drawToolbar(ctx);

    const std::span<const dmx::UniverseId> universes = ctx.dmxSnapshot.universes();
    if (universes.empty()) {
        ImGui::Separator();
        ImGui::TextDisabled("No DMX data yet. Start an input in 'DMX Interfaces' or move a fader in the Test Console.");
        return;
    }
    const dmx::UniverseData* values = ctx.dmxSnapshot.universe(selected_);
    if (values == nullptr) return;

    refreshOwners(ctx);
    const dmx::UniverseInfo* info = ctx.dmxSnapshot.info(selected_);

    const ImVec2 avail = ImGui::GetContentRegionAvail();
    const bool sideBySide = avail.x >= kSideBySideMinWidth;
    const float gridWidth = sideBySide ? avail.x - kSourcesWidth - ImGui::GetStyle().ItemSpacing.x : avail.x;

    if (ImGui::BeginChild("##grid", ImVec2(gridWidth, sideBySide ? 0.0f : avail.y * 0.65f))) drawGrid(ctx, *values);
    ImGui::EndChild();
    if (sideBySide) ImGui::SameLine();
    if (ImGui::BeginChild("##sources", ImVec2(0.0f, 0.0f), ImGuiChildFlags_Borders)) drawSources(info);
    ImGui::EndChild();
}

void DmxMonitorPanel::drawToolbar(EditorContext& ctx) {
    const std::span<const dmx::UniverseId> universes = ctx.dmxSnapshot.universes();
    if (std::find(universes.begin(), universes.end(), selected_) == universes.end())
        selected_ = universes.empty() ? dmx::kInvalidUniverse : universes.front();

    // Universe selector: only universes that have data, the programmer's included.
    char preview[48] = "none";
    if (selected_ != dmx::kInvalidUniverse) std::snprintf(preview, sizeof(preview), "Universe %u", static_cast<unsigned>(selected_));
    ImGui::SetNextItemWidth(150.0f);
    if (ImGui::BeginCombo("##universe", preview)) {
        for (const dmx::UniverseId id : universes) {
            const dmx::UniverseInfo* info = ctx.dmxSnapshot.info(id);
            bool programmer = false;
            if (info != nullptr)
                for (const dmx::SourceInfo& s : info->sources) programmer |= s.protocol == dmx::Protocol::Programmer;
            char label[64];
            std::snprintf(label, sizeof(label), "Universe %u%s", static_cast<unsigned>(id),
                          programmer ? "  (programmer)" : "");
            if (ImGui::Selectable(label, id == selected_)) selected_ = id;
        }
        ImGui::EndCombo();
    }

    ImGui::SameLine();
    int format = static_cast<int>(format_);
    ImGui::RadioButton("Dec", &format, 0);
    ImGui::SameLine();
    ImGui::RadioButton("%", &format, 1);
    ImGui::SameLine();
    ImGui::RadioButton("Hex", &format, 2);
    format_ = static_cast<ValueFormat>(format);

    if (const dmx::UniverseInfo* info = ctx.dmxSnapshot.info(selected_)) {
        ImGui::SameLine(0.0f, 24.0f);
        ImGui::TextDisabled("%d source%s, last data %lld ms ago%s", static_cast<int>(info->sources.size()),
                            info->sources.size() == 1 ? "" : "s",
                            millisecondsSince(ctx.dmxSnapshot.time(), info->lastUpdate),
                            info->held ? ", HELD (sources timed out)" : "");
    }
}

void DmxMonitorPanel::refreshOwners(EditorContext& ctx) {
    const double now = ImGui::GetTime();
    // The revision alone cannot tell a replaced scene from the old one, so also refresh now and then.
    if (owners_.universe() == selected_ && ownersRevision_ == ctx.scene.revision() && now - ownersTime_ < 0.5) return;
    owners_.build(ctx.scene, ctx.fixtures, selected_);
    ownersRevision_ = ctx.scene.revision();
    ownersTime_ = now;
}

// ---------------------------------------------------------------------------
// Grid

void DmxMonitorPanel::drawGrid(EditorContext& ctx, const dmx::UniverseData& values) {
    const ImVec2 avail = ImGui::GetContentRegionAvail();
    const float usable = avail.x - kAddressColumnWidth;
    const int columns = usable / 32.0f >= kMinCellWidth ? 32 : 16;
    const int rows = kDmxSlots / columns;
    const float cellW = std::floor(usable / static_cast<float>(columns));
    const float cellH = std::clamp(std::floor(avail.y / static_cast<float>(rows)), kMinCellHeight, 26.0f);

    ImDrawList* draw = ImGui::GetWindowDrawList();
    const ImVec2 origin = ImGui::GetCursorScreenPos();
    const ImVec2 gridOrigin(origin.x + kAddressColumnWidth, origin.y);
    ImGui::Dummy(ImVec2(kAddressColumnWidth + cellW * static_cast<float>(columns), cellH * static_cast<float>(rows)));

    // Which cell is under the mouse (if any).
    int hoveredAddress = 0;
    if (ImGui::IsWindowHovered()) {
        const ImVec2 mouse = ImGui::GetMousePos();
        const int col = static_cast<int>(std::floor((mouse.x - gridOrigin.x) / cellW));
        const int row = static_cast<int>(std::floor((mouse.y - gridOrigin.y) / cellH));
        if (mouse.x >= gridOrigin.x && col >= 0 && col < columns && row >= 0 && row < rows)
            hoveredAddress = row * columns + col + 1;
    }

    const ImU32 labelColor = IM_COL32(130, 130, 140, 255);
    for (int row = 0; row < rows; ++row) {
        char label[8];
        std::snprintf(label, sizeof(label), "%d", row * columns + 1);
        draw->AddText(ImVec2(origin.x, origin.y + static_cast<float>(row) * cellH + 1.0f), labelColor, label);
    }

    for (int i = 0; i < kDmxSlots; ++i) {
        const int col = i % columns;
        const int row = i / columns;
        const ImVec2 p0(gridOrigin.x + static_cast<float>(col) * cellW, gridOrigin.y + static_cast<float>(row) * cellH);
        const ImVec2 p1(p0.x + cellW - 1.0f, p0.y + cellH - 1.0f);

        const std::uint8_t value = values[static_cast<std::size_t>(i)];
        const float level = static_cast<float>(value) / 255.0f;
        draw->AddRectFilled(p0, p1, mixColor(kCellDark, kCellBright, std::pow(level, 0.75f)));

        const ChannelOwner& owner = owners_.at(i + 1);
        if (owner.used()) {
            draw->AddRectFilled(ImVec2(p0.x, p1.y - 2.0f), p1, fixtureColor(owner.fixture));
            if (owner.first) draw->AddRectFilled(p0, ImVec2(p0.x + 2.0f, p1.y), IM_COL32(255, 255, 255, 230));
            if (owner.shared) draw->AddRect(p0, p1, IM_COL32(255, 70, 60, 255));
        }

        char text[8];
        formatValue(text, sizeof(text), value, static_cast<int>(format_));
        const ImVec2 size = ImGui::CalcTextSize(text);
        const ImU32 textColor = level > 0.6f ? IM_COL32(15, 15, 18, 255)
                                              : (value == 0 ? IM_COL32(95, 95, 105, 255) : IM_COL32(225, 225, 230, 255));
        draw->AddText(ImVec2(p0.x + (cellW - 1.0f - size.x) * 0.5f, p0.y + (cellH - 1.0f - size.y) * 0.5f), textColor, text);
    }

    if (hoveredAddress > 0) {
        const int i = hoveredAddress - 1;
        const ImVec2 p0(gridOrigin.x + static_cast<float>(i % columns) * cellW,
                        gridOrigin.y + static_cast<float>(i / columns) * cellH);
        draw->AddRect(p0, ImVec2(p0.x + cellW - 1.0f, p0.y + cellH - 1.0f), IM_COL32(255, 255, 255, 255));
        drawCellTooltip(ctx, hoveredAddress, values[static_cast<std::size_t>(i)]);
    }
}

void DmxMonitorPanel::drawCellTooltip(EditorContext& ctx, int address, std::uint8_t value) const {
    if (!ImGui::BeginTooltip()) return;
    ImGui::Text("Universe %u, address %d", static_cast<unsigned>(selected_), address);
    ImGui::Text("Value %d  (%d%%, 0x%02X)", static_cast<int>(value), (static_cast<int>(value) * 100 + 127) / 255,
                static_cast<unsigned>(value));

    const ChannelOwner& owner = owners_.at(address);
    const stage::Node* node = owner.used() ? ctx.scene.find(owner.fixture) : nullptr;
    const stage::FixtureContent* fixture = node != nullptr ? node->as<stage::FixtureContent>() : nullptr;
    const fixtures::FixtureType* type = fixture != nullptr ? ctx.fixtures.find(fixture->fixtureTypeId) : nullptr;
    const fixtures::DmxMode* mode = type != nullptr ? fixtureMode(*type, *fixture) : nullptr;

    ImGui::Separator();
    if (mode == nullptr || owner.channel < 0 || owner.channel >= static_cast<int>(mode->channels.size())) {
        ImGui::TextDisabled("Not used by any patched fixture");
        ImGui::EndTooltip();
        return;
    }
    const fixtures::Channel& channel = mode->channels[static_cast<std::size_t>(owner.channel)];
    ImGui::PushStyleColor(ImGuiCol_Text, fixtureColor(owner.fixture));
    ImGui::Text("%s", node->name().c_str());
    ImGui::PopStyleColor();
    ImGui::SameLine();
    ImGui::TextDisabled("%s, %s mode", type->displayName().c_str(), mode->name.c_str());
    ImGui::Text("Channel %d of %d: %s%s", owner.channel + 1, static_cast<int>(mode->channels.size()),
                channel.name.c_str(), byteName(owner.byte, channel.byteCount()));
    if (!channel.functions.empty()) {
        const std::string_view attribute = channel.functions.front().attributeLabel();
        ImGui::TextDisabled("Attribute %.*s", static_cast<int>(attribute.size()), attribute.data());
    }
    if (owner.shared)
        ImGui::TextColored(ImVec4(1.0f, 0.45f, 0.40f, 1.0f), "Another fixture uses this address too");
    ImGui::EndTooltip();
}

// ---------------------------------------------------------------------------
// Sources

void DmxMonitorPanel::drawSources(const dmx::UniverseInfo* info) {
    ImGui::TextUnformatted("Sources");
    if (info == nullptr || info->sources.empty()) {
        ImGui::TextDisabled("none");
        return;
    }
    constexpr ImGuiTableFlags flags = ImGuiTableFlags_RowBg | ImGuiTableFlags_SizingStretchProp |
                                      ImGuiTableFlags_BordersInnerV | ImGuiTableFlags_ScrollY;
    if (!ImGui::BeginTable("##sourceTable", 5, flags)) return;
    ImGui::TableSetupScrollFreeze(0, 1);
    ImGui::TableSetupColumn("Source", ImGuiTableColumnFlags_WidthStretch, 1.5f);
    ImGui::TableSetupColumn("Protocol", ImGuiTableColumnFlags_WidthStretch, 1.4f);
    ImGui::TableSetupColumn("From", ImGuiTableColumnFlags_WidthStretch, 1.6f);
    ImGui::TableSetupColumn("Prio", ImGuiTableColumnFlags_WidthFixed, 34.0f);
    ImGui::TableSetupColumn("Rate", ImGuiTableColumnFlags_WidthFixed, 56.0f);
    ImGui::TableHeadersRow();

    for (const dmx::SourceInfo& source : info->sources) {
        ImGui::TableNextRow();
        const bool dim = source.held;
        if (dim) ImGui::PushStyleColor(ImGuiCol_Text, ImGui::GetStyleColorVec4(ImGuiCol_TextDisabled));

        ImGui::TableNextColumn();
        ImGui::TextUnformatted(source.name.c_str());
        if (source.held) ImGui::SetItemTooltip("Timed out: DmxViz keeps the last values");
        ImGui::TableNextColumn();
        const std::string_view protocol = dmx::protocolName(source.protocol);
        ImGui::TextUnformatted(protocol.data(), protocol.data() + protocol.size());
        if (source.preview) {
            ImGui::SameLine();
            ImGui::TextDisabled("preview");
        }
        ImGui::TableNextColumn();
        if (source.protocol == dmx::Protocol::Programmer || source.endpoint.address.isAny()) {
            ImGui::TextUnformatted(source.protocol == dmx::Protocol::Programmer ? "local" : "-");
        } else {
            const dmx::Ipv4Address a = source.endpoint.address;
            ImGui::Text("%u.%u.%u.%u:%u", a.octet(0), a.octet(1), a.octet(2), a.octet(3),
                        static_cast<unsigned>(source.endpoint.port));
        }
        ImGui::TableNextColumn();
        ImGui::Text("%u", static_cast<unsigned>(source.priority));
        ImGui::TableNextColumn();
        if (source.protocol == dmx::Protocol::Programmer)
            ImGui::TextUnformatted("-");
        else
            ImGui::Text("%.1f/s", static_cast<double>(source.packetsPerSecond));

        if (dim) ImGui::PopStyleColor();
    }
    ImGui::EndTable();
}

}  // namespace dmxviz::ui
