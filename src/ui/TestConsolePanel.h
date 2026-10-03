#pragma once
// TestConsolePanel: a small lighting console for trying out fixtures and DMX outputs.
//
//   Attributes tab ... for the selected fixtures (a selected group or truss counts for its
//                      fixtures): intensity, colour picker, pan/tilt pad, beam, wheels
//                      (gobo, colour, prism slot buttons plus rotation), shutter/strobe.
//                      Every control goes through the fixture's AttributeEncoder, so one
//                      colour picker drives RGB, RGBW, CMY or a colour wheel.
//   Raw tab .......... one fader per DMX channel, 32 per page, for any universe.
//
// All values go into the DMX manager's programmer, so the visualizer and (when "Output to
// interfaces" is on) the interfaces see them. Merge lets the highest value of the console
// and incoming DMX win; Override makes the console's channels win. The panel edits
// ConsoleSession / ConsoleFixture; this class is only the drawing.

#include "dmx/DmxTypes.h"
#include "ui/ConsoleSession.h"
#include "ui/Panel.h"

#include "imgui.h"

#include <memory>
#include <vector>

namespace dmxviz::ui {

class TestConsolePanel : public Panel {
public:
    const char* title() const override;
    void draw(EditorContext& ctx) override;

private:
    void drawToolbar(EditorContext& ctx);
    void drawAttributes(EditorContext& ctx);
    void drawRawChannels(EditorContext& ctx);

    // The sections of the attributes tab. `primary` is the fixture whose values the controls show.
    void drawIntensity(const ConsoleFixture& primary);
    void drawColour(const ConsoleFixture& primary);
    void drawPosition(const ConsoleFixture& primary);
    void drawBeam(const ConsoleFixture& primary);
    void drawWheels(const ConsoleFixture& primary);
    void drawShutter(const ConsoleFixture& primary);
    void drawOther(const ConsoleFixture& primary);

    // Places sections side by side and wraps them when the window is too narrow.
    bool beginSection(const char* name, float width);
    void endSection();

    // Selected fixtures, primary first.
    void collectSelected(EditorContext& ctx);
    // Runs `change` on every selected fixture and sends the result to the programmer.
    template <typename Change>
    void forEachSelected(Change&& change);
    void sliderForAttribute(const ConsoleFixture& primary, fixtures::Attribute attribute);

    std::unique_ptr<ConsoleSession> session_;
    std::vector<ConsoleFixture*> selected_;
    std::vector<fixtures::Attribute> attributes_;  // distinct attributes of the primary fixture

    // Section layout state of the current frame.
    float sectionRightEdge_ = 0.0f;
    float lastSectionRight_ = 0.0f;
    int sectionsDrawn_ = 0;

    // Raw channel page.
    int rawUniverse_ = 1;
    int rawPage_ = 0;
};

}  // namespace dmxviz::ui
