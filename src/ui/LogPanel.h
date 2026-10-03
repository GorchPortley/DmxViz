#pragma once
// LogPanel: shows the in-memory log (log::recent) with a level filter, a text
// filter and auto-scroll.

#include "core/Log.h"
#include "ui/Panel.h"

#include "imgui.h"

#include <vector>

namespace dmxviz::ui {

class LogPanel : public Panel {
public:
    const char* title() const override;
    void draw(EditorContext& ctx) override;

private:
    // log::recent() copies strings, so the panel refreshes its copy a few times a second.
    void refreshEntries();

    std::vector<log::Entry> entries_;
    double lastRefresh_ = -1.0;
    log::Level minLevel_ = log::Level::Info;
    ImGuiTextFilter textFilter_;
    bool autoScroll_ = true;
    double clearedBefore_ = -1.0;  // "Clear" hides entries up to this log time
};

}  // namespace dmxviz::ui
