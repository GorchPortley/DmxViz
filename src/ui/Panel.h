#pragma once
// Panel: base class of every dockable editor window.
//
// To add a panel:
//   1. derive from Panel, return a unique title() and draw the window *contents*
//      in draw() (no ImGui::Begin/End: show() does that);
//   2. create it in App::init() and add it to the panel list;
//   3. if it should appear in the default layout, dock its title in
//      DockLayout.cpp (titles are listed in PanelTitles.h).
// The View menu lists all panels and toggles `open`.

namespace dmxviz::ui {

struct EditorContext;

class Panel {
public:
    virtual ~Panel() = default;

    // Window title; also the dock identity, so it must be unique and stable.
    virtual const char* title() const = 0;
    // Draws the contents inside the already opened window.
    virtual void draw(EditorContext& ctx) = 0;

    // Optional tweaks for the window (ImGuiWindowFlags values).
    virtual int windowFlags() const { return 0; }
    // False for panels that fill their whole window, like the 3D viewport.
    virtual bool windowPadding() const { return true; }

    // Opens the window, calls draw() and closes it. Does nothing while !open.
    void show(EditorContext& ctx);

    bool open = true;
};

}  // namespace dmxviz::ui
