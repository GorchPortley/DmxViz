#include "ui/DmxInterfacesPanel.h"

#include "core/Log.h"
#include "dmx/interfaces/ArtNetInterface.h"
#include "dmx/interfaces/EnttecProInterface.h"
#include "dmx/interfaces/LoopbackInterface.h"
#include "dmx/interfaces/OpenDmxInterface.h"
#include "dmx/interfaces/SacnInterface.h"
#include "dmx/interfaces/SerialDmxInterface.h"
#include "ui/DmxTextFormat.h"
#include "ui/EditorContext.h"
#include "ui/PanelTitles.h"

#include "imgui.h"

#include <algorithm>
#include <cstdio>

namespace dmxviz::ui {

namespace {

constexpr int kMaxUniverseOffset = 32768;
constexpr int kLabelWidth = 150;  // pixels reserved for field labels before the widgets

const ImVec4 kRunningColor(0.45f, 0.85f, 0.45f, 1.0f);
const ImVec4 kStoppedColor(0.60f, 0.60f, 0.64f, 1.0f);
const ImVec4 kErrorColor(1.00f, 0.45f, 0.40f, 1.0f);

ImVec4 stateColor(dmx::InterfaceState state) {
    switch (state) {
        case dmx::InterfaceState::Running:
            return kRunningColor;
        case dmx::InterfaceState::Error:
            return kErrorColor;
        case dmx::InterfaceState::Stopped:
            break;
    }
    return kStoppedColor;
}

const char* stateName(dmx::InterfaceState state) {
    switch (state) {
        case dmx::InterfaceState::Running:
            return "running";
        case dmx::InterfaceState::Error:
            return "error";
        case dmx::InterfaceState::Stopped:
            break;
    }
    return "stopped";
}

void setText(char* buffer, std::size_t size, const std::string& text) {
    std::snprintf(buffer, size, "%s", text.c_str());
}

// A single-line text field that turns red while its text does not parse.
bool textField(const char* label, char* text, std::size_t size, bool valid, const char* hint) {
    if (!valid) ImGui::PushStyleColor(ImGuiCol_FrameBg, ImVec4(0.45f, 0.12f, 0.12f, 1.0f));
    const bool changed = ImGui::InputTextWithHint(label, hint, text, size);
    if (!valid) ImGui::PopStyleColor();
    return changed;
}

// Writes "label" then lets the next widget start at a fixed column.
void fieldLabel(const char* text) {
    ImGui::AlignTextToFramePadding();
    ImGui::TextUnformatted(text);
    ImGui::SameLine(static_cast<float>(kLabelWidth));
}

std::vector<dmx::UniverseId> routedUniverses(const dmx::DmxManager& manager, dmx::InterfaceId id) {
    std::vector<dmx::UniverseId> universes;
    for (const dmx::OutputRoute& route : manager.routes())
        if (route.interfaceId == id) universes.push_back(route.universe);
    return universes;
}

// Replaces the routes of one interface and leaves those of the others alone.
void setRoutedUniverses(dmx::DmxManager& manager, dmx::InterfaceId id, const std::vector<std::uint16_t>& universes) {
    std::vector<dmx::OutputRoute> routes;
    for (const dmx::OutputRoute& route : manager.routes())
        if (route.interfaceId != id) routes.push_back(route);
    for (const std::uint16_t universe : universes) routes.push_back(dmx::OutputRoute{universe, id});
    manager.setRoutes(routes);
}

}  // namespace

const char* DmxInterfacesPanel::title() const {
    return kDmxInterfacesTitle;
}

// ---------------------------------------------------------------------------
// Enumeration

void DmxInterfacesPanel::refreshNetworkInterfaces() {
    nics_ = dmx::listNetworkInterfaces();
    nicLabels_.clear();
    for (const dmx::NetworkInterfaceInfo& nic : nics_) nicLabels_.push_back(nic.label());
}

void DmxInterfacesPanel::refreshSerialPorts() {
    serialPorts_ = dmx::listSerialPorts();
    serialLabels_.clear();
    for (const dmx::SerialPortInfo& port : serialPorts_)
        serialLabels_.push_back(port.description.empty() ? port.systemName : port.systemName + "  " + port.description);
}

DmxInterfacesPanel::EditState& DmxInterfacesPanel::stateFor(const dmx::DmxInterface& iface) {
    EditState& state = states_[iface.id()];
    if (state.owner == &iface) return state;

    // First sight (or a new project replaced the interface): fill the text fields from its settings.
    state = EditState{};
    state.owner = &iface;
    if (const auto* artnet = dynamic_cast<const dmx::ArtNetInterface*>(&iface)) {
        setText(state.unicast.text, sizeof(state.unicast.text),
                formatEndpointList(artnet->config().unicastTargets, artnet->config().port));
        setText(state.announced.text, sizeof(state.announced.text),
                formatUniverseList(artnet->config().announcedUniverses));
    } else if (const auto* sacn = dynamic_cast<const dmx::SacnInterface*>(&iface)) {
        setText(state.unicast.text, sizeof(state.unicast.text),
                formatEndpointList(sacn->config().unicastTargets, sacn->config().port));
        setText(state.universes.text, sizeof(state.universes.text), formatUniverseList(sacn->config().universes));
    }
    return state;
}

// ---------------------------------------------------------------------------
// Frame

void DmxInterfacesPanel::draw(EditorContext& ctx) {
    if (!enumerated_) {
        refreshNetworkInterfaces();
        refreshSerialPorts();
        enumerated_ = true;
    }
    drawToolbar(ctx);
    ImGui::Separator();

    const std::vector<dmx::DmxInterface*> interfaces = ctx.dmx.interfaces();
    if (interfaces.empty()) ImGui::TextDisabled("No DMX interfaces. Add one to receive or send DMX.");

    ImGui::BeginChild("##interfaces");
    for (dmx::DmxInterface* iface : interfaces) {
        ImGui::PushID(static_cast<int>(iface->id()));
        drawInterface(ctx, *iface);
        ImGui::PopID();
    }
    ImGui::EndChild();

    // Removing while drawing would leave a dangling pointer in the list above.
    if (pendingRemove_ != 0) {
        log::info("dmx", "removing interface {}", pendingRemove_);
        ctx.dmx.removeInterface(pendingRemove_);
        states_.erase(pendingRemove_);
        pendingRemove_ = 0;
        ctx.settingsDirty = true;
    }
}

void DmxInterfacesPanel::drawToolbar(EditorContext& ctx) {
    if (ImGui::Button("Add interface...")) ImGui::OpenPopup("##addInterface");
    if (ImGui::BeginPopup("##addInterface")) {
        for (const dmx::InterfaceTypeInfo& type : ctx.dmx.registry().types()) {
            if (ImGui::Selectable(type.name.c_str())) {
                const dmx::InterfaceId id = ctx.dmx.addInterface(type.name);
                if (id != 0) ctx.settingsDirty = true;
            }
            ImGui::SetItemTooltip("%s", type.description.c_str());
        }
        ImGui::EndPopup();
    }

    ImGui::SameLine();
    float rate = static_cast<float>(ctx.dmx.outputRate());
    ImGui::SetNextItemWidth(110.0f);
    if (ImGui::SliderFloat("Output rate", &rate, static_cast<float>(dmx::DmxManager::kMinOutputRate),
                           static_cast<float>(dmx::DmxManager::kMaxOutputRate), "%.0f Hz")) {
        ctx.dmx.setOutputRate(static_cast<double>(rate));
        ctx.settingsDirty = true;
    }
    ImGui::SetItemTooltip("How often the output universes are refreshed (changed universes go out at once,\n"
                          "unchanged ones once per second).");
}

// ---------------------------------------------------------------------------
// One interface

void DmxInterfacesPanel::drawInterface(EditorContext& ctx, dmx::DmxInterface& iface) {
    EditState& state = stateFor(iface);
    const dmx::InterfaceState interfaceState = iface.state();

    char header[160];
    std::snprintf(header, sizeof(header), "%s   [%s]###header", iface.label().c_str(), stateName(interfaceState));
    ImGui::PushStyleColor(ImGuiCol_Text, stateColor(interfaceState));
    const bool open = ImGui::CollapsingHeader(header, ImGuiTreeNodeFlags_DefaultOpen);
    ImGui::PopStyleColor();
    if (!open) return;

    ImGui::Indent();
    drawStatus(ctx, iface, state);
    drawSettings(ctx, iface, state);
    ImGui::Unindent();
    ImGui::Spacing();
}

void DmxInterfacesPanel::drawStatus(EditorContext& ctx, dmx::DmxInterface& iface, EditState& state) {
    const dmx::InterfaceStatus status = iface.status();
    const bool running = status.state == dmx::InterfaceState::Running;

    // Name, start/stop, remove.
    char label[96];
    setText(label, sizeof(label), iface.label());
    ImGui::SetNextItemWidth(200.0f);
    if (ImGui::InputText("##label", label, sizeof(label))) {
        iface.setLabel(label);
        ctx.settingsDirty = true;
    }
    ImGui::SetItemTooltip("Name of the interface (also the source name of USB input)");
    ImGui::SameLine();
    ImGui::TextDisabled("%s", iface.typeName().c_str());
    ImGui::SameLine();
    if (running) {
        if (ImGui::Button("Stop")) {
            ctx.dmx.stopInterface(iface.id());
            state.restartNeeded = false;
            ctx.settingsDirty = true;
        }
    } else if (ImGui::Button("Start")) {
        startInterface(ctx, iface, state);
    }
    ImGui::SameLine();
    if (ImGui::Button("Remove")) ImGui::OpenPopup("Remove interface?");
    if (ImGui::BeginPopup("Remove interface?")) {
        ImGui::Text("Remove '%s'?", iface.label().c_str());
        if (ImGui::Button("Remove")) {
            pendingRemove_ = iface.id();
            ImGui::CloseCurrentPopup();
        }
        ImGui::SameLine();
        if (ImGui::Button("Cancel")) ImGui::CloseCurrentPopup();
        ImGui::EndPopup();
    }

    // Status line.
    const dmx::Capabilities caps = iface.caps();
    ImGui::TextColored(stateColor(status.state), "%s", stateName(status.state));
    if (!status.message.empty()) {
        ImGui::SameLine();
        ImGui::TextDisabled("%s", status.message.c_str());
    }
    if (!state.startError.empty()) ImGui::TextColored(kErrorColor, "Cannot start: %s", state.startError.c_str());
    if (running) {
        if (caps.input) {
            ImGui::Text("In %.1f/s (%llu)", static_cast<double>(status.packetsInPerSecond),
                        static_cast<unsigned long long>(status.packetsIn));
            ImGui::SameLine();
        }
        if (caps.output) {
            ImGui::Text("Out %.1f/s (%llu)", static_cast<double>(status.packetsOutPerSecond),
                        static_cast<unsigned long long>(status.packetsOut));
            ImGui::SameLine();
        }
        if (caps.input) {
            const int senders = countRemoteSenders(ctx, iface.id());
            ImGui::Text("Senders %d", senders);
            if (senders > 0 && ImGui::IsItemHovered()) drawSenderTooltip();
            ImGui::SameLine();
        }
        if (status.packetsInvalid > 0)
            ImGui::TextColored(kErrorColor, "Rejected %llu", static_cast<unsigned long long>(status.packetsInvalid));
        else
            ImGui::TextDisabled("Rejected 0");
    }
}

void DmxInterfacesPanel::startInterface(EditorContext& ctx, dmx::DmxInterface& iface, EditState& state) {
    state.startError.clear();
    if (!ctx.dmx.startInterface(iface.id(), state.startError)) {
        if (state.startError.empty()) state.startError = "unknown error";
        log::warn("dmx", "interface {} not started: {}", iface.id(), state.startError);
    }
    state.restartNeeded = false;
    ctx.settingsDirty = true;
}

// ---------------------------------------------------------------------------
// Remote senders

int DmxInterfacesPanel::countRemoteSenders(const EditorContext& ctx, dmx::InterfaceId id) {
    senderScratch_.clear();
    for (const dmx::UniverseId universe : ctx.dmxSnapshot.universes()) {
        const dmx::UniverseInfo* info = ctx.dmxSnapshot.info(universe);
        if (info == nullptr) continue;
        for (const dmx::SourceInfo& source : info->sources) {
            if (source.interfaceId != id || source.held) continue;
            const bool known = std::any_of(senderScratch_.begin(), senderScratch_.end(), [&source](const dmx::SourceInfo* s) {
                return s->endpoint == source.endpoint && s->name == source.name;
            });
            if (!known) senderScratch_.push_back(&source);
        }
    }
    return static_cast<int>(senderScratch_.size());
}

void DmxInterfacesPanel::drawSenderTooltip() const {
    if (!ImGui::BeginTooltip()) return;
    for (const dmx::SourceInfo* source : senderScratch_) {
        const dmx::Endpoint& e = source->endpoint;
        ImGui::Text("%s  %u.%u.%u.%u:%u  prio %u  %.0f/s", source->name.c_str(), e.address.octet(0), e.address.octet(1),
                    e.address.octet(2), e.address.octet(3), static_cast<unsigned>(e.port),
                    static_cast<unsigned>(source->priority), static_cast<double>(source->packetsPerSecond));
    }
    ImGui::EndTooltip();
}

// ---------------------------------------------------------------------------
// Settings

void DmxInterfacesPanel::drawSettings(EditorContext& ctx, dmx::DmxInterface& iface, EditState& state) {
    if (!ImGui::TreeNodeEx("Settings", ImGuiTreeNodeFlags_DefaultOpen)) return;

    bool changed = false;
    if (iface.caps().input) {
        bool input = iface.inputEnabled();
        fieldLabel("Receive input");
        if (ImGui::Checkbox("##input", &input)) iface.setInputEnabled(input);
        ImGui::SetItemTooltip("Switch off to ignore incoming DMX without stopping the interface");
    }

    if (dynamic_cast<dmx::ArtNetInterface*>(&iface) != nullptr)
        changed |= drawArtNetSettings(iface, state);
    else if (dynamic_cast<dmx::SacnInterface*>(&iface) != nullptr)
        changed |= drawSacnSettings(iface, state);
    else if (dynamic_cast<dmx::SerialDmxInterface*>(&iface) != nullptr)
        changed |= drawSerialSettings(iface);
    else if (dynamic_cast<dmx::LoopbackInterface*>(&iface) != nullptr)
        changed |= drawLoopbackSettings(iface);

    if (iface.caps().output) drawOutputUniverses(ctx, iface, state);

    if (changed) {
        ctx.settingsDirty = true;
        if (iface.running()) state.restartNeeded = true;
    }
    if (state.restartNeeded && iface.running()) {
        ImGui::TextColored(ImVec4(1.0f, 0.8f, 0.3f, 1.0f), "Settings changed: restart the interface to apply them.");
        ImGui::SameLine();
        if (ImGui::Button("Restart")) {
            ctx.dmx.stopInterface(iface.id());
            startInterface(ctx, iface, state);
        }
    }
    ImGui::TreePop();
}

bool DmxInterfacesPanel::drawArtNetSettings(dmx::DmxInterface& iface, EditState& state) {
    auto& artnet = static_cast<dmx::ArtNetInterface&>(iface);
    dmx::ArtNetConfig config = artnet.config();
    bool changed = false;

    fieldLabel("Network interface");
    changed |= nicCombo("##nic", config.nic);

    int port = config.port;
    fieldLabel("UDP port");
    ImGui::SetNextItemWidth(100.0f);
    if (ImGui::InputInt("##port", &port, 0, 0)) {
        config.port = static_cast<std::uint16_t>(std::clamp(port, 0, 65535));
        changed = true;
    }
    ImGui::SetItemTooltip("Art-Net uses 6454; 0 picks any free port");

    fieldLabel("Universe offset");
    ImGui::SetNextItemWidth(100.0f);
    changed |= ImGui::InputInt("##offset", &config.universeOffset, 1, 10);
    config.universeOffset = std::clamp(config.universeOffset, -kMaxUniverseOffset, kMaxUniverseOffset);
    ImGui::SetItemTooltip("Logical universe = Art-Net port-address + 1 + offset");

    fieldLabel("Reply to ArtPoll");
    changed |= ImGui::Checkbox("##poll", &config.replyToPoll);
    ImGui::SetItemTooltip("Lets consoles discover DmxViz as a visualiser node");

    fieldLabel("Announced universes");
    ImGui::SetNextItemWidth(220.0f);
    if (textField("##announced", state.announced.text, sizeof(state.announced.text), state.announced.valid, "e.g. 1-4")) {
        std::vector<std::uint16_t> universes;
        state.announced.valid = parseUniverseList(state.announced.text, 1, 32768, universes);
        if (state.announced.valid) {
            config.announcedUniverses = universes;
            changed = true;
        }
    }

    fieldLabel("Broadcast output");
    changed |= ImGui::Checkbox("##broadcast", &config.broadcastOutput);
    fieldLabel("Unicast targets");
    ImGui::SetNextItemWidth(220.0f);
    if (textField("##unicast", state.unicast.text, sizeof(state.unicast.text), state.unicast.valid,
                  "192.168.1.20, 10.0.0.5:6454")) {
        std::vector<dmx::Endpoint> targets;
        state.unicast.valid = parseEndpointList(state.unicast.text, config.port == 0 ? 6454 : config.port, targets);
        if (state.unicast.valid) {
            config.unicastTargets = targets;
            changed = true;
        }
    }

    if (changed) artnet.setConfig(config);
    return changed;
}

bool DmxInterfacesPanel::drawSacnSettings(dmx::DmxInterface& iface, EditState& state) {
    auto& sacn = static_cast<dmx::SacnInterface&>(iface);
    dmx::SacnConfig config = sacn.config();
    bool changed = false;

    fieldLabel("Network interface");
    changed |= nicCombo("##nic", config.nic);

    int port = config.port;
    fieldLabel("UDP port");
    ImGui::SetNextItemWidth(100.0f);
    if (ImGui::InputInt("##port", &port, 0, 0)) {
        config.port = static_cast<std::uint16_t>(std::clamp(port, 0, 65535));
        changed = true;
    }
    ImGui::SetItemTooltip("sACN uses 5568; 0 picks any free port");

    fieldLabel("Universe offset");
    ImGui::SetNextItemWidth(100.0f);
    changed |= ImGui::InputInt("##offset", &config.universeOffset, 1, 10);
    config.universeOffset = std::clamp(config.universeOffset, -kMaxUniverseOffset, kMaxUniverseOffset);
    ImGui::SetItemTooltip("Logical universe = sACN universe + offset");

    fieldLabel("Receive universes");
    ImGui::SetNextItemWidth(220.0f);
    if (textField("##universes", state.universes.text, sizeof(state.universes.text), state.universes.valid,
                  "e.g. 1-4, 7")) {
        std::vector<std::uint16_t> universes;
        state.universes.valid = parseUniverseList(state.universes.text, 1, 63999, universes);
        if (state.universes.valid) {
            config.universes = universes;
            changed = true;
        }
    }
    fieldLabel("Multicast input");
    changed |= ImGui::Checkbox("##mcin", &config.multicastInput);
    ImGui::SameLine();
    changed |= ImGui::Checkbox("Accept all universes", &config.acceptAllUniverses);
    ImGui::SameLine();
    changed |= ImGui::Checkbox("Accept preview data", &config.acceptPreview);

    fieldLabel("Source name");
    char name[64];
    setText(name, sizeof(name), config.sourceName);
    ImGui::SetNextItemWidth(220.0f);
    if (ImGui::InputText("##source", name, sizeof(name))) {
        config.sourceName = name;
        changed = true;
    }
    fieldLabel("Output priority");
    int priority = config.priority;
    ImGui::SetNextItemWidth(220.0f);
    if (ImGui::SliderInt("##priority", &priority, 0, dmx::kMaxNetworkPriority)) {
        config.priority = static_cast<std::uint8_t>(priority);
        changed = true;
    }
    fieldLabel("Multicast output");
    changed |= ImGui::Checkbox("##mcout", &config.multicastOutput);
    fieldLabel("Unicast targets");
    ImGui::SetNextItemWidth(220.0f);
    if (textField("##unicast", state.unicast.text, sizeof(state.unicast.text), state.unicast.valid,
                  "192.168.1.20, 10.0.0.5:5568")) {
        std::vector<dmx::Endpoint> targets;
        state.unicast.valid = parseEndpointList(state.unicast.text, config.port == 0 ? 5568 : config.port, targets);
        if (state.unicast.valid) {
            config.unicastTargets = targets;
            changed = true;
        }
    }

    if (changed) sacn.setConfig(config);
    return changed;
}

bool DmxInterfacesPanel::drawSerialSettings(dmx::DmxInterface& iface) {
    auto& serial = static_cast<dmx::SerialDmxInterface&>(iface);
    bool changed = false;

    std::string path = serial.portPath();
    fieldLabel("Serial port");
    if (serialPortPicker("##port", path)) {
        serial.setPortPath(path);
        changed = true;
    }

    if (auto* pro = dynamic_cast<dmx::EnttecProInterface*>(&iface)) {
        int universe = pro->inputUniverse();
        fieldLabel("Input universe");
        ImGui::SetNextItemWidth(100.0f);
        if (ImGui::InputInt("##inUniverse", &universe, 1, 10)) {
            pro->setInputUniverse(static_cast<dmx::UniverseId>(std::clamp(universe, 1, 63999)));
            changed = true;
        }
        ImGui::SetItemTooltip("DMX received by the widget appears in this logical universe");
    } else if (auto* open = dynamic_cast<dmx::OpenDmxInterface*>(&iface)) {
        int rate = open->refreshRate();
        fieldLabel("Refresh rate");
        ImGui::SetNextItemWidth(220.0f);
        if (ImGui::SliderInt("##refresh", &rate, dmx::OpenDmxInterface::kMinRefreshRate,
                             dmx::OpenDmxInterface::kMaxRefreshRate, "%d Hz")) {
            open->setRefreshRate(rate);
            changed = true;
        }
        ImGui::SetItemTooltip("This cable has no processor: DmxViz generates the DMX signal itself");
    }
    return changed;
}

bool DmxInterfacesPanel::drawLoopbackSettings(dmx::DmxInterface& iface) {
    auto& loopback = static_cast<dmx::LoopbackInterface&>(iface);
    int offset = loopback.universeOffset();
    fieldLabel("Universe offset");
    ImGui::SetNextItemWidth(100.0f);
    const bool changed = ImGui::InputInt("##offset", &offset, 1, 10);
    if (changed) loopback.setUniverseOffset(std::clamp(offset, -kMaxUniverseOffset, kMaxUniverseOffset));
    ImGui::SetItemTooltip("Output universe U comes back as input universe U + offset");
    return changed;
}

void DmxInterfacesPanel::drawOutputUniverses(EditorContext& ctx, dmx::DmxInterface& iface, EditState& state) {
    // The routes live in the DmxManager; the text mirrors them except while the user is typing.
    if (!state.outputsActive && state.outputs.valid) {
        const std::vector<dmx::UniverseId> routed = routedUniverses(ctx.dmx, iface.id());
        setText(state.outputs.text, sizeof(state.outputs.text), formatUniverseList(routed));
    }
    fieldLabel("Output universes");
    ImGui::SetNextItemWidth(220.0f);
    if (textField("##outputs", state.outputs.text, sizeof(state.outputs.text), state.outputs.valid,
                  "none - e.g. 1-4")) {
        std::vector<std::uint16_t> universes;
        state.outputs.valid = parseUniverseList(state.outputs.text, 1, 63999, universes);
        if (state.outputs.valid) {
            setRoutedUniverses(ctx.dmx, iface.id(), universes);
            ctx.settingsDirty = true;
        }
    }
    state.outputsActive = ImGui::IsItemActive();
    ImGui::SetItemTooltip("Logical universes sent through this interface (merged inputs and the test console).\n"
                          "Output also needs 'Output to interfaces' in the Test Console.");
}

// ---------------------------------------------------------------------------
// Combos

bool DmxInterfacesPanel::nicCombo(const char* label, dmx::Ipv4Address& nic) {
    constexpr const char* kAll = "All interfaces (0.0.0.0)";
    int found = -1;
    for (std::size_t i = 0; i < nics_.size(); ++i)
        if (nics_[i].address == nic) found = static_cast<int>(i);

    char preview[128];
    if (nic.isAny())
        std::snprintf(preview, sizeof(preview), "%s", kAll);
    else if (found >= 0)
        std::snprintf(preview, sizeof(preview), "%s", nicLabels_[static_cast<std::size_t>(found)].c_str());
    else
        std::snprintf(preview, sizeof(preview), "%s (not found)", nic.toString().c_str());

    bool changed = false;
    ImGui::SetNextItemWidth(260.0f);
    if (ImGui::BeginCombo(label, preview)) {
        if (ImGui::Selectable(kAll, nic.isAny())) {
            nic = dmx::kAnyAddress;
            changed = true;
        }
        for (std::size_t i = 0; i < nics_.size(); ++i) {
            if (ImGui::Selectable(nicLabels_[i].c_str(), static_cast<int>(i) == found)) {
                nic = nics_[i].address;
                changed = true;
            }
        }
        ImGui::EndCombo();
    }
    ImGui::SameLine();
    if (ImGui::SmallButton("Refresh##nic")) refreshNetworkInterfaces();
    ImGui::SetItemTooltip("Look for network adapters again");
    return changed;
}

bool DmxInterfacesPanel::serialPortPicker(const char* label, std::string& path) {
    bool changed = false;
    char text[256];
    setText(text, sizeof(text), path);
    ImGui::SetNextItemWidth(230.0f);
    if (ImGui::InputTextWithHint(label, "/dev/ttyUSB0 or COM3", text, sizeof(text))) {
        path = text;
        changed = true;
    }
    ImGui::SameLine(0.0f, 2.0f);
    ImGui::SetNextItemWidth(ImGui::GetFrameHeight());
    if (ImGui::BeginCombo("##pick", "", ImGuiComboFlags_NoPreview)) {
        if (serialPorts_.empty()) ImGui::TextDisabled("No serial ports found");
        for (std::size_t i = 0; i < serialPorts_.size(); ++i) {
            if (ImGui::Selectable(serialLabels_[i].c_str(), serialPorts_[i].path == path)) {
                path = serialPorts_[i].path;
                changed = true;
            }
            ImGui::SetItemTooltip("%s", serialPorts_[i].path.c_str());
        }
        ImGui::EndCombo();
    }
    ImGui::SameLine();
    if (ImGui::SmallButton("Refresh##serial")) refreshSerialPorts();
    ImGui::SetItemTooltip("Look for serial ports and USB adapters again");
    return changed;
}

}  // namespace dmxviz::ui
