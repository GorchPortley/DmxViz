#pragma once
// DmxInterfacesPanel: configure the DMX ports - Art-Net, sACN, Enttec USB widgets and the
// loopback. One section per interface with
//
//   * live status: state, packets per second in/out, rejected packets, remote senders;
//   * start / stop and remove;
//   * the settings of its type (network card, universe offset, serial port, ...);
//   * which universes it sends ("output universes").
//
// Settings are stored in the DmxManager, which saves them in the project's DMX block, so
// every change sets ctx.settingsDirty. A running interface needs a restart to use changed
// settings; the panel says so and offers a button.

#include "dmx/DmxManager.h"
#include "dmx/platform/NetworkInterfaces.h"
#include "dmx/platform/SerialPort.h"
#include "ui/Panel.h"

#include <map>
#include <string>
#include <vector>

namespace dmxviz::ui {

class DmxInterfacesPanel : public Panel {
public:
    const char* title() const override;
    void draw(EditorContext& ctx) override;

private:
    // A text field that holds a list ("1-4, 7"). `valid` is false while the text does not parse.
    struct TextField {
        char text[256] = {};
        bool valid = true;
    };
    // What the panel remembers per interface between frames.
    struct EditState {
        const dmx::DmxInterface* owner = nullptr;  // a different pointer means the interface was replaced
        TextField universes;                       // sACN: universes to receive
        TextField unicast;                         // Art-Net / sACN: unicast output targets
        TextField announced;                       // Art-Net: universes listed in ArtPollReply
        TextField outputs;                         // universes sent through this interface
        bool outputsActive = false;                // the user is typing in `outputs`
        std::string startError;
        bool restartNeeded = false;
    };

    void drawToolbar(EditorContext& ctx);
    void drawInterface(EditorContext& ctx, dmx::DmxInterface& iface);
    void drawStatus(EditorContext& ctx, dmx::DmxInterface& iface, EditState& state);
    void drawSettings(EditorContext& ctx, dmx::DmxInterface& iface, EditState& state);
    void drawOutputUniverses(EditorContext& ctx, dmx::DmxInterface& iface, EditState& state);

    // Settings per interface type. Each returns true when something changed.
    bool drawArtNetSettings(dmx::DmxInterface& iface, EditState& state);
    bool drawSacnSettings(dmx::DmxInterface& iface, EditState& state);
    bool drawSerialSettings(dmx::DmxInterface& iface);
    bool drawLoopbackSettings(dmx::DmxInterface& iface);

    // Combos fed by the system enumeration.
    bool nicCombo(const char* label, dmx::Ipv4Address& nic);
    bool serialPortPicker(const char* label, std::string& path);
    void refreshNetworkInterfaces();
    void refreshSerialPorts();

    EditState& stateFor(const dmx::DmxInterface& iface);
    void startInterface(EditorContext& ctx, dmx::DmxInterface& iface, EditState& state);
    // Distinct remote senders feeding this interface, from the frame's snapshot.
    int countRemoteSenders(const EditorContext& ctx, dmx::InterfaceId id);
    // Lists the senders found by the last countRemoteSenders() call.
    void drawSenderTooltip() const;

    std::map<dmx::InterfaceId, EditState> states_;
    dmx::InterfaceId pendingRemove_ = 0;

    bool enumerated_ = false;
    std::vector<dmx::NetworkInterfaceInfo> nics_;
    std::vector<std::string> nicLabels_;
    std::vector<dmx::SerialPortInfo> serialPorts_;
    std::vector<std::string> serialLabels_;
    std::vector<const dmx::SourceInfo*> senderScratch_;  // reused every frame
};

}  // namespace dmxviz::ui
