#pragma once
// PlaceholderPanel: stands in for a panel that is not written yet, so the default
// dock layout already has all its windows. Replace it by deleting the line that
// creates it in App::init() and adding the real panel with the same title.

#include "ui/Panel.h"

#include <string>
#include <utility>

namespace dmxviz::ui {

class PlaceholderPanel : public Panel {
public:
    explicit PlaceholderPanel(std::string title) : title_(std::move(title)) {}

    const char* title() const override { return title_.c_str(); }
    void draw(EditorContext& ctx) override;

private:
    std::string title_;
};

}  // namespace dmxviz::ui
