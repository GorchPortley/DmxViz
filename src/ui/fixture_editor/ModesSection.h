#pragma once
// ModesSection: the "Modes & Channels" tab of the fixture editor.
//
//   mode bar ....... pick, add, duplicate, delete and rename DMX modes; the footprint
//   channel table .. one row per channel: name, coarse / fine / ultra offset, geometry it controls,
//                    attribute (searchable), default value; reorder and delete
//   function table . for the selected channel: DMX range, attribute, kind, physical range,
//                    wheel + slot range, name; below it the details of the selected function
//                    (LED emitter, mode master, named ranges)
// Structure changes (delete, reorder) are queued while a table is drawn and run afterwards.

#include "ui/fixture_editor/EditDocument.h"
#include "ui/fixture_editor/EditorWidgets.h"

#include <cstddef>

namespace dmxviz::ui::fixture_editor {

class ModesSection {
public:
    // Returns true when the fixture type changed.
    bool draw(EditDocument& doc);

    // The mode the other tabs work on (new axis channels, cell channels, preview).
    int selectedMode() const { return mode_; }
    // Selects a mode, channel and function (any may be -1), e.g. from the validation list.
    void select(int mode, int channel, int function);
    void reset();

private:
    enum class Action {
        None,
        ChannelUp,
        ChannelDown,
        ChannelDelete,
        ChannelDuplicate,
        FunctionUp,
        FunctionDown,
        FunctionDelete
    };

    bool drawModeBar(fixtures::FixtureType& type);
    bool drawChannelTable(fixtures::FixtureType& type, fixtures::DmxMode& mode);
    bool drawChannelRow(fixtures::FixtureType& type, fixtures::DmxMode& mode, std::size_t index);
    bool drawChannelFooter(fixtures::FixtureType& type, fixtures::DmxMode& mode);
    bool drawChannelDetails(fixtures::FixtureType& type, fixtures::DmxMode& mode, std::size_t channelIndex);
    bool drawFunctionTable(fixtures::FixtureType& type, fixtures::DmxMode& mode, fixtures::Channel& channel);
    bool drawFunctionRow(fixtures::FixtureType& type, fixtures::DmxMode& mode, fixtures::Channel& channel,
                         std::size_t index);
    bool drawFunctionDetails(fixtures::FixtureType& type, fixtures::DmxMode& mode, fixtures::Channel& channel,
                             fixtures::ChannelFunction& function);
    bool drawGeometryCombo(const fixtures::FixtureType& type, fixtures::Channel& channel);
    bool runAction(fixtures::DmxMode& mode);

    int mode_ = 0;
    int channel_ = -1;
    int function_ = -1;  // function to scroll to once (jump from the validation list)
    int selectedFunction_ = -1;
    bool scrollToFunction_ = false;

    Action pending_ = Action::None;
    std::size_t pendingIndex_ = 0;

    AttributePicker channelPicker_;
    AttributePicker functionPicker_;
    AttributePicker newChannelPicker_;
    fixtures::Attribute newChannelAttribute_ = fixtures::Attribute::Dimmer;
};

}  // namespace dmxviz::ui::fixture_editor
