#pragma once
// StageMenu: the "Add" and "Tools" menus of the main menu bar, plus the small
// dialogs they open. This is the stage builder (FR-STG-5):
//
//   Add ..... primitives, truss (straight runs, corner blocks, arcs, circles, towers), decks, risers,
//             steps, walls, floor, reference figure, group, camera preset from the current view and
//             imported 3D models. New nodes appear where the viewport camera looks (on the floor) and
//             become the selection.
//   Tools ... operate on the selection: linear / grid / circular arrays, align, distribute, mirror,
//             drop to floor, group, and hang fixtures on a truss.
//
// Everything goes through ctx.commands, so every tool is one undo step.

#include "ui/FileDialogs.h"

#include <filesystem>

namespace dmxviz::ui {

struct EditorContext;

class StageMenu {
public:
    // Call between ImGui::BeginMainMenuBar() and EndMainMenuBar(), once per frame.
    void draw(EditorContext& ctx);

private:
    enum class Dialog { None, Truss, LinearArray, GridArray, CircularArray, Mirror, Hang };

    void drawAddMenu(EditorContext& ctx);
    void drawToolsMenu(EditorContext& ctx);
    void openDialog(Dialog dialog) { requested_ = dialog; }
    void showDialogs(EditorContext& ctx);
    void pollImportDialog(EditorContext& ctx);

    // Each dialog draws its fields and returns true when it should close.
    bool trussDialog(EditorContext& ctx);
    bool linearArrayDialog(EditorContext& ctx);
    bool gridArrayDialog(EditorContext& ctx);
    bool circularArrayDialog(EditorContext& ctx);
    bool mirrorDialog(EditorContext& ctx);
    bool hangDialog(EditorContext& ctx);

    Dialog requested_ = Dialog::None;  // set by a menu item, opened once the menu is closed
    Dialog active_ = Dialog::None;
    FileDialogs fileDialogs_;
    std::filesystem::path lastModelDir_;

    // Dialog settings are kept between uses, so repeating a tool starts from the last values.
    struct TrussSettings {
        int piece = 0;  // see kTrussPieces in the .cpp
        int profile = 0;
        float length = 3.0f;
        int cornerPreset = 0;
        float radius = 2.0f;
        float angleDeg = 90.0f;
        int arcPieces = 1;
        int circlePieces = 4;
        float towerHeight = 5.0f;
        float sleeveHeight = 4.0f;
    } truss_;
    struct LinearSettings {
        int copies = 3;
        float offset[3] = {1.0f, 0.0f, 0.0f};
    } linear_;
    struct GridSettings {
        int counts[3] = {3, 1, 3};
        float spacing[3] = {1.0f, 1.0f, 1.0f};
    } grid_;
    struct CircularSettings {
        int count = 8;
        float totalAngleDeg = 360.0f;
        int axis = 1;  // 0 = X, 1 = Y, 2 = Z
        float centre[3] = {0.0f, 0.0f, 0.0f};
        bool rotateCopies = true;
    } circular_;
    struct MirrorSettings {
        int axis = 0;
        float position = 0.0f;
        bool copy = true;
    } mirror_;
    struct HangSettings {
        int facing = 0;  // index into the facing list in the .cpp
        bool spread = true;
        float snapAlong = 0.0f;
        bool reparent = false;
    } hang_;
};

// Draws the Add and Tools menus (one shared StageMenu instance).
void drawStageMenu(EditorContext& ctx);

}  // namespace dmxviz::ui
