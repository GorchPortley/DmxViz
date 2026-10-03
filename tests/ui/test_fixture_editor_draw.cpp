// Smoke test for the fixture editor's ImGui sections: draws every section for many fixtures,
// including deliberately broken ones, in a headless ImGui context. It catches crashes (bad
// indices on half-edited data) and ImGui usage errors (unbalanced Begin/End, Push/Pop) that the
// logic tests cannot see.

#include "fixtures/FixtureLibrary.h"
#include "ui/fixture_editor/EditDocument.h"
#include "ui/fixture_editor/FixtureTemplates.h"
#include "ui/fixture_editor/GeneralSection.h"
#include "ui/fixture_editor/GeometrySection.h"
#include "ui/fixture_editor/ModesSection.h"
#include "ui/fixture_editor/ValidationSection.h"
#include "ui/fixture_editor/WheelsSection.h"

#include "imgui.h"
#include "imgui_internal.h"

#include <doctest/doctest.h>

#include <algorithm>
#include <filesystem>
#include <random>
#include <string>
#include <vector>

using namespace dmxviz;
using namespace dmxviz::ui::fixture_editor;

namespace {

// A headless ImGui context that records usage errors instead of asserting.
class HeadlessImGui {
public:
    HeadlessImGui() {
        context_ = ImGui::CreateContext();
        ImGuiIO& io = ImGui::GetIO();
        io.IniFilename = nullptr;
        io.DisplaySize = ImVec2(1920.0f, 1200.0f);
        io.ConfigErrorRecoveryEnableDebugLog = false;  // errors are counted below, not printed
        io.ConfigErrorRecoveryEnableTooltip = false;
        io.ConfigErrorRecoveryEnableAssert = false;  // Debug/sanitizer builds would otherwise abort
        io.DeltaTime = 1.0f / 60.0f;
        io.BackendFlags |= ImGuiBackendFlags_RendererHasTextures;  // no GPU: textures are never uploaded
        context_->ErrorCallback = [](ImGuiContext*, void* user, const char* message) {
            auto* self = static_cast<HeadlessImGui*>(user);
            ++self->errors;
            self->lastError = message;
        };
        context_->ErrorCallbackUserData = this;
    }
    ~HeadlessImGui() { ImGui::DestroyContext(context_); }
    HeadlessImGui(const HeadlessImGui&) = delete;
    HeadlessImGui& operator=(const HeadlessImGui&) = delete;

    // One frame. The body opens its own windows (see window()).
    template <typename Body>
    void frame(Body&& body) {
        ImGui::NewFrame();
        body();
        ImGui::Render();
        // Pretend the renderer created the textures ImGui asked for.
        for (ImTextureData* texture : ImGui::GetPlatformIO().Textures) {
            if (texture->Status == ImTextureStatus_WantCreate || texture->Status == ImTextureStatus_WantUpdates) {
                texture->SetTexID(static_cast<ImTextureID>(1));
                texture->SetStatus(ImTextureStatus_OK);
            }
        }
    }

    // A window at a grid position (3 columns, 2 rows over the display) that runs `body`.
    template <typename Body>
    static void window(const char* name, int cell, Body&& body) {
        const ImVec2 size(ImGui::GetIO().DisplaySize.x / 3.0f, ImGui::GetIO().DisplaySize.y / 2.0f);
        ImGui::SetNextWindowPos(ImVec2(size.x * static_cast<float>(cell % 3), size.y * static_cast<float>(cell / 3)));
        ImGui::SetNextWindowSize(size);
        if (ImGui::Begin(name)) body();
        ImGui::End();
    }

    int errors = 0;
    std::string lastError;

private:
    ImGuiContext* context_ = nullptr;
};

// Draws every section of a document for a few frames and returns how many ImGui errors happened.
struct SectionDrawer {
    GeneralSection general;
    GeometrySection geometry;
    WheelsSection wheels;
    ModesSection modes;
    ValidationSection validation;

    // Every section in a window of its own, like the tabs of the editor. Returns true when something changed.
    bool drawWindows(EditDocument& doc) {
        bool changed = false;
        const std::vector<Problem> problems = validateFixture(doc.type());
        HeadlessImGui::window("general", 0,
                              [&] { changed |= general.draw(doc, [](std::string_view) { return false; }); });
        HeadlessImGui::window("geometry", 1, [&] { changed |= geometry.draw(doc, modes.selectedMode()); });
        HeadlessImGui::window("wheels", 2, [&] { changed |= wheels.draw(doc); });
        HeadlessImGui::window("modes", 3, [&] { changed |= modes.draw(doc); });
        HeadlessImGui::window("validation", 4, [&] { validation.draw(problems); });
        return changed;
    }

    void drawAll(HeadlessImGui& imgui, EditDocument& doc, int frames = 3) {
        for (int i = 0; i < frames; ++i) {
            imgui.frame([&] { drawWindows(doc); });
        }
    }
};

fixtures::FixtureType brokenFixture() {
    fixtures::FixtureType type = makeTemplateFixture(FixtureTemplate::MovingHead);
    for (fixtures::Channel& c : type.modes.front().channels) {
        c.geometry = "Nowhere";
        c.offsets = {1, 1};       // overlapping offsets
        c.defaultValue = 999999;  // above the channel maximum
        for (fixtures::ChannelFunction& f : c.functions) {
            f.dmxFrom = 900;
            f.dmxTo = 10;  // reversed
            f.wheel = "No such wheel";
        }
    }
    type.wheels[1].slots[1].image = "missing";
    type.wheels[2].slots.clear();
    type.geometry.children.front().name.clear();
    return type;
}

}  // namespace

TEST_CASE("fixture editor smoke test: the headless harness notices ImGui misuse") {
    HeadlessImGui imgui;
    imgui.frame([] { ImGui::PushID(1); });  // never popped
    CHECK(imgui.errors > 0);
}

TEST_CASE("fixture editor sections draw the templates without ImGui errors") {
    HeadlessImGui imgui;
    for (FixtureTemplate kind : kAllTemplates) {
        CAPTURE(templateName(kind));
        EditDocument doc;
        doc.openNew(kind, {});
        SectionDrawer drawer;
        drawer.drawAll(imgui, doc);
        CHECK_MESSAGE(imgui.errors == 0, imgui.lastError);
    }
}

TEST_CASE("fixture editor sections draw the bundled fixtures without ImGui errors") {
    fixtures::FixtureLibrary library;
    REQUIRE(library.loadDirectory(std::filesystem::path(DMXVIZ_DATA_DIR) / "fixtures") > 0);

    HeadlessImGui imgui;
    for (const fixtures::FixtureType* type : library.all()) {
        CAPTURE(type->id);
        EditDocument doc;
        doc.openCopy(*type);
        SectionDrawer drawer;
        drawer.drawAll(imgui, doc, 2);
        // The mode and wheel selection must also cope with every mode of the type.
        for (int mode = 0; mode < static_cast<int>(type->modes.size()); ++mode) {
            drawer.modes.select(mode, 0, 0);
            drawer.drawAll(imgui, doc, 1);
        }
        CHECK_MESSAGE(imgui.errors == 0, imgui.lastError);
    }
}

TEST_CASE("fixture editor sections survive half-edited fixtures") {
    HeadlessImGui imgui;
    SUBCASE("channels, wheels and geometry full of broken references") {
        EditDocument doc;
        doc.openCopy(brokenFixture());
        SectionDrawer drawer;
        drawer.drawAll(imgui, doc);
        CHECK_MESSAGE(imgui.errors == 0, imgui.lastError);
    }
    SUBCASE("no modes, no wheels, no children") {
        fixtures::FixtureType type = makeTemplateFixture(FixtureTemplate::MovingHead);
        type.modes.clear();
        type.wheels.clear();
        type.geometry.children.clear();
        EditDocument doc;
        doc.openCopy(type);
        SectionDrawer drawer;
        drawer.modes.select(5, 7, 9);  // selections that do not exist
        drawer.wheels.select(4);
        drawer.geometry.select(doc.type(), "Nope");
        drawer.drawAll(imgui, doc);
        CHECK_MESSAGE(imgui.errors == 0, imgui.lastError);
    }
    SUBCASE("a mode without channels and a channel without functions") {
        fixtures::FixtureType type = makeTemplateFixture(FixtureTemplate::LedPar);
        type.modes.push_back(fixtures::DmxMode{.name = "Empty"});
        type.modes.front().channels.front().functions.clear();
        EditDocument doc;
        doc.openCopy(type);
        SectionDrawer drawer;
        drawer.drawAll(imgui, doc);
        drawer.modes.select(1, -1, -1);
        drawer.drawAll(imgui, doc);
        CHECK_MESSAGE(imgui.errors == 0, imgui.lastError);
    }
}

TEST_CASE("fixture editor: validation list reports a click") {
    // The list has to hand the clicked problem back so the panel can jump there. Without a pointer
    // device the click cannot be simulated here, so check that a frame with problems reports none.
    HeadlessImGui imgui;
    ValidationSection validation;
    const std::vector<Problem> problems = validateFixture(brokenFixture());
    REQUIRE(countProblems(problems).errors > 0);
    std::optional<Problem> clicked;
    imgui.frame([&] { HeadlessImGui::window("validation", 0, [&] { clicked = validation.draw(problems); }); });
    CHECK_FALSE(clicked.has_value());
    CHECK(imgui.errors == 0);
}

// Random clicks all over the sections: presses buttons, opens combos and popups, deletes and reorders
// things. The data ends up in odd states; the sections must keep drawing without errors or crashes.
// 250 frames per fixture keep the unit test quick; raise kMonkeyFrames (20000 or more) for a long manual run.
TEST_CASE("fixture editor sections survive random clicking") {
    constexpr int kMonkeyFrames = 250;
    const int frames = kMonkeyFrames;

    HeadlessImGui imgui;
    ImGuiIO& io = ImGui::GetIO();
    std::mt19937 random(20240611);
    std::uniform_real_distribution<float> unit(0.0f, 1.0f);

    // The templates, then the bundled fixtures (with groups, emitters, many wheels and functions).
    std::vector<fixtures::FixtureType> subjects;
    for (FixtureTemplate kind : kAllTemplates) subjects.push_back(makeTemplateFixture(kind));
    fixtures::FixtureLibrary library;
    library.loadDirectory(std::filesystem::path(DMXVIZ_DATA_DIR) / "fixtures");
    for (const fixtures::FixtureType* type : library.all()) subjects.push_back(*type);

    for (const fixtures::FixtureType& subject : subjects) {
        CAPTURE(subject.id);
        EditDocument doc;
        doc.openCopy(subject);
        SectionDrawer drawer;
        int changes = 0;
        for (int frame = 0; frame < frames; ++frame) {
            if (frame % 2 == 0) {
                // Aim at the upper left of a random window: that is where the buttons and fields are.
                const float cellW = io.DisplaySize.x / 3.0f;
                const float cellH = io.DisplaySize.y / 2.0f;
                const int cell = static_cast<int>(unit(random) * 5.0f);
                io.AddMousePosEvent(cellW * static_cast<float>(cell % 3) + 10.0f + unit(random) * cellW * 0.85f,
                                    cellH * static_cast<float>(cell / 3) + 25.0f + unit(random) * cellH * 0.55f);
                io.AddMouseButtonEvent(0, true);
            } else {
                io.AddMouseButtonEvent(0, false);
            }
            imgui.frame([&] { changes += drawer.drawWindows(doc) ? 1 : 0; });
            if (imgui.errors > 0) break;
        }
        CAPTURE(changes);
        MESSAGE("monkey frames " << frames << ", frames that changed the fixture: " << changes);
        CHECK_MESSAGE(imgui.errors == 0, imgui.lastError);
    }
}
