#include "ui/ContentEditors.h"

#include "stage/SetBuilder.h"
#include "stage/TrussBuilder.h"
#include "ui/ColorWidgets.h"

#include "imgui.h"

#include <algorithm>
#include <cstdint>
#include <format>
#include <string>
#include <utility>
#include <vector>

namespace dmxviz::ui::editors {

using namespace stage;

namespace {

// ---- small widget wrappers: widget + EditSession::touch in one call --------------------------------------

bool dragFloat(EditSession& edit, const char* label, float& value, float speed, float min, float max,
               const char* format = "%.2f m") {
    return edit.touch(ImGui::DragFloat(label, &value, speed, min, max, format, ImGuiSliderFlags_AlwaysClamp), label);
}

bool dragInt(EditSession& edit, const char* label, int& value, int min, int max) {
    return edit.touch(ImGui::DragInt(label, &value, 0.2f, min, max, "%d", ImGuiSliderFlags_AlwaysClamp), label);
}

bool slider01(EditSession& edit, const char* label, float& value) {
    return edit.touch(ImGui::SliderFloat(label, &value, 0.0f, 1.0f, "%.2f"), label);
}

bool checkbox(EditSession& edit, const char* label, bool& value) {
    return edit.touch(ImGui::Checkbox(label, &value), label);
}

bool colour(EditSession& edit, const char* label, glm::vec3& linear) {
    return edit.touch(colorEditLinear(label, linear, ImGuiColorEditFlags_NoInputs), label);
}

// Combo box over an enum. `entries` lists every value with its display name.
template <typename Enum>
bool enumCombo(EditSession& edit, const char* label, Enum& value,
               const std::vector<std::pair<Enum, const char*>>& entries) {
    const char* preview = "?";
    for (const auto& entry : entries)
        if (entry.first == value) preview = entry.second;
    bool changed = false;
    if (ImGui::BeginCombo(label, preview)) {
        for (const auto& entry : entries) {
            if (ImGui::Selectable(entry.second, entry.first == value)) {
                value = entry.first;
                changed = true;
            }
        }
        ImGui::EndCombo();
    }
    return edit.touch(changed, label);
}

// ---- truss -------------------------------------------------------------------------------------------------

std::string describeRun(const TrussStraight& straight) {
    const std::vector<float> pieces = truss::splitRun(straight.length, straight.segments);
    std::string text = std::format("{} piece(s):", pieces.size());
    for (std::size_t i = 0; i < pieces.size() && i < 8; ++i) text += std::format(" {:g}", pieces[i]);
    if (pieces.size() > 8) text += " ...";
    return text;
}

void trussProfile(EditSession& edit, TrussProfile& profile) {
    const std::vector<TrussProfile>& catalogue = standardTrussProfiles();
    const bool custom =
        std::none_of(catalogue.begin(), catalogue.end(), [&](const TrussProfile& p) { return p == profile; });
    const std::string preview = custom ? profile.name + " (modified)" : profile.name;

    bool changed = false;
    if (ImGui::BeginCombo("Profile", preview.c_str())) {
        for (const TrussProfile& candidate : catalogue) {
            const std::string label = std::format("{}  ({:g} mm)", candidate.name, candidate.width * 1000.0f);
            if (ImGui::Selectable(label.c_str(), candidate == profile)) {
                profile = candidate;
                changed = true;
            }
        }
        ImGui::EndCombo();
    }
    edit.touch(changed, "Truss profile");

    if (ImGui::TreeNode("Profile dimensions")) {
        enumCombo<TrussShape>(edit, "Shape", profile.shape,
                              {{TrussShape::Box, "Box (4 chords)"},
                               {TrussShape::Triangle, "Triangle"},
                               {TrussShape::Ladder, "Ladder"},
                               {TrussShape::Flat, "Flat"}});
        dragFloat(edit, "Width", profile.width, 0.005f, 0.05f, 1.5f, "%.3f m");
        dragFloat(edit, "Chord diameter", profile.chordDiameter, 0.001f, 0.01f, 0.2f, "%.3f m");
        dragFloat(edit, "Brace diameter", profile.braceDiameter, 0.001f, 0.004f, 0.1f, "%.3f m");
        ImGui::TreePop();
    }
}

void cornerFaces(EditSession& edit, TrussCorner& corner) {
    struct Preset {
        TrussCornerPreset preset;
        const char* name;
    };
    static const Preset kPresets[] = {
        {TrussCornerPreset::TwoWay, "2-way corner"},
        {TrussCornerPreset::ThreeWayT, "3-way T"},
        {TrussCornerPreset::ThreeWayCorner, "3-way corner"},
        {TrussCornerPreset::FourWayCross, "4-way cross"},
        {TrussCornerPreset::FourWayT, "4-way T"},
        {TrussCornerPreset::FiveWay, "5-way"},
        {TrussCornerPreset::SixWay, "6-way"},
    };
    const char* preview = "Custom";
    for (const Preset& p : kPresets)
        if (trussCornerFaces(p.preset) == corner.faces) preview = p.name;

    bool changed = false;
    if (ImGui::BeginCombo("Block type", preview)) {
        for (const Preset& p : kPresets) {
            if (ImGui::Selectable(p.name, trussCornerFaces(p.preset) == corner.faces)) {
                corner.faces = trussCornerFaces(p.preset);
                changed = true;
            }
        }
        ImGui::EndCombo();
    }
    edit.touch(changed, "Corner type");

    struct Face {
        TrussFace bit;
        const char* label;
    };
    static const Face kFaces[] = {{kTrussFacePosX, "+X"}, {kTrussFaceNegX, "-X"}, {kTrussFacePosY, "+Y"},
                                  {kTrussFaceNegY, "-Y"}, {kTrussFacePosZ, "+Z"}, {kTrussFaceNegZ, "-Z"}};
    ImGui::TextDisabled("Open faces");
    for (std::size_t i = 0; i < std::size(kFaces); ++i) {
        if (i % 3 != 0) ImGui::SameLine();
        bool on = (corner.faces & kFaces[i].bit) != 0;
        if (edit.touch(ImGui::Checkbox(kFaces[i].label, &on), "Corner faces")) {
            corner.faces =
                static_cast<std::uint8_t>(on ? (corner.faces | kFaces[i].bit) : (corner.faces & ~kFaces[i].bit));
        }
    }
}

}  // namespace

// ---- material ---------------------------------------------------------------------------------------------

void material(EditSession& edit, const char* title, Material& m) {
    ImGui::SeparatorText(title);
    ImGui::PushID(title);
    colour(edit, "Colour", m.albedo);
    slider01(edit, "Roughness", m.roughness);
    slider01(edit, "Metallic", m.metallic);

    // Emissive is "colour x strength": the picker only handles 0..1, the strength can go above 1.
    float strength = std::max({m.emissive.x, m.emissive.y, m.emissive.z});
    glm::vec3 tint = strength > 0.0f ? m.emissive / strength : glm::vec3(1.0f);
    if (colour(edit, "Emissive", tint)) {
        if (strength <= 0.0f) strength = 1.0f;
        m.emissive = tint * strength;
    }
    if (dragFloat(edit, "Emissive strength", strength, 0.02f, 0.0f, 1000.0f, "%.2f")) m.emissive = tint * strength;
    ImGui::PopID();
}

// ---- primitive --------------------------------------------------------------------------------------------

void primitive(EditSession& edit, PrimitiveContent& c) {
    enumCombo<PrimitiveShape>(edit, "Shape", c.shape,
                              {{PrimitiveShape::Box, "Box"},
                               {PrimitiveShape::Cylinder, "Cylinder"},
                               {PrimitiveShape::Sphere, "Sphere"},
                               {PrimitiveShape::Plane, "Plane"},
                               {PrimitiveShape::Cone, "Cone"},
                               {PrimitiveShape::Disc, "Disc"}});
    const bool round = c.shape == PrimitiveShape::Cylinder || c.shape == PrimitiveShape::Sphere ||
                       c.shape == PrimitiveShape::Cone || c.shape == PrimitiveShape::Disc;
    const bool flat = c.shape == PrimitiveShape::Plane || c.shape == PrimitiveShape::Disc;
    dragFloat(edit, round ? "Diameter X" : "Width", c.size.x, 0.02f, 0.01f, 500.0f);
    if (!flat) dragFloat(edit, "Height", c.size.y, 0.02f, 0.01f, 500.0f);
    dragFloat(edit, round ? "Diameter Z" : "Depth", c.size.z, 0.02f, 0.01f, 500.0f);
    material(edit, "Material", c.material);
}

// ---- truss ------------------------------------------------------------------------------------------------

void truss(EditSession& edit, TrussContent& c) {
    trussProfile(edit, c.profile);
    enumCombo<TrussPiece>(edit, "Piece", c.piece,
                          {{TrussPiece::Straight, "Straight run"},
                           {TrussPiece::Corner, "Corner block"},
                           {TrussPiece::Arc, "Arc / circle"},
                           {TrussPiece::Tower, "Ground support tower"}});
    ImGui::Separator();

    switch (c.piece) {
        case TrussPiece::Straight: {
            dragFloat(edit, "Length", c.straight.length, 0.05f, 0.1f, 100.0f);
            ImGui::TextDisabled("Standard lengths");
            for (float length : standardTrussLengths()) {
                ImGui::SameLine();
                const std::string label = std::format("{:g}##len", length);
                if (edit.touch(ImGui::SmallButton(label.c_str()), "Truss length")) {
                    c.straight.length = length;
                    c.straight.segments.clear();
                }
            }
            ImGui::TextDisabled("%s", describeRun(c.straight).c_str());
            if (!c.straight.segments.empty() && edit.touch(ImGui::Button("Use standard pieces"), "Truss pieces"))
                c.straight.segments.clear();
            break;
        }
        case TrussPiece::Corner:
            cornerFaces(edit, c.corner);
            break;
        case TrussPiece::Arc:
            dragFloat(edit, "Radius", c.arc.radius, 0.05f, 0.2f, 100.0f);
            dragFloat(edit, "Angle", c.arc.angleDeg, 0.5f, 1.0f, 360.0f, "%.1f deg");
            dragInt(edit, "Pieces", c.arc.pieces, 1, 64);
            if (edit.touch(ImGui::Button("Full circle"), "Truss angle")) c.arc.angleDeg = 360.0f;
            break;
        case TrussPiece::Tower:
            dragFloat(edit, "Height", c.tower.height, 0.05f, 0.5f, 50.0f);
            dragFloat(edit, "Sleeve height", c.tower.sleeveHeight, 0.05f, 0.3f, c.tower.height);
            dragFloat(edit, "Base plate", c.tower.basePlateSize, 0.01f, 0.2f, 3.0f);
            break;
    }
}

// ---- stage deck, steps, wall, figure ---------------------------------------------------------------------

void stageDeck(EditSession& edit, StageDeckContent& c) {
    enumCombo<DeckStyle>(edit, "Style", c.style,
                         {{DeckStyle::Deck, "Deck on legs"}, {DeckStyle::Riser, "Closed riser"}});
    dragInt(edit, "Columns (X)", c.columns, 1, 100);
    dragInt(edit, "Rows (Z)", c.rows, 1, 100);

    struct Size {
        float x, z;
    };
    static const Size kSizes[] = {{2.0f, 1.0f}, {1.0f, 1.0f}, {2.0f, 2.0f}, {1.0f, 0.5f}, {2.44f, 1.22f}};
    const std::string preview = std::format("{:g} x {:g} m", c.panelSize.x, c.panelSize.y);
    bool presetChosen = false;
    if (ImGui::BeginCombo("Panel size", preview.c_str())) {
        for (const Size& s : kSizes) {
            const std::string label = std::format("{:g} x {:g} m", s.x, s.z);
            if (ImGui::Selectable(label.c_str(), s.x == c.panelSize.x && s.z == c.panelSize.y)) {
                c.panelSize = {s.x, s.z};
                presetChosen = true;
            }
        }
        ImGui::EndCombo();
    }
    edit.touch(presetChosen, "Panel size");
    dragFloat(edit, "Panel width", c.panelSize.x, 0.01f, 0.1f, 10.0f);
    dragFloat(edit, "Panel depth", c.panelSize.y, 0.01f, 0.1f, 10.0f);
    dragFloat(edit, "Height", c.height, 0.01f, 0.05f, 10.0f);
    dragFloat(edit, "Panel thickness", c.thickness, 0.005f, 0.01f, 0.5f, "%.3f m");
    if (c.style == DeckStyle::Deck) {
        checkbox(edit, "Legs", c.legs);
        ImGui::SameLine();
        checkbox(edit, "Skirt", c.skirt);
    }
    const glm::vec2 footprint = setpiece::deckFootprint(c);
    ImGui::TextDisabled("Footprint %.2f x %.2f m", footprint.x, footprint.y);
    material(edit, "Surface", c.surface);
    if (c.skirt && c.style == DeckStyle::Deck) material(edit, "Skirt", c.skirtMaterial);
}

void steps(EditSession& edit, StepsContent& c) {
    dragFloat(edit, "Width", c.width, 0.02f, 0.2f, 20.0f);
    dragFloat(edit, "Height", c.height, 0.01f, 0.05f, 10.0f);
    dragInt(edit, "Steps (0 = auto)", c.steps, 0, 50);
    ImGui::TextDisabled("%d step(s)", c.effectiveSteps());
    dragFloat(edit, "Tread depth", c.treadDepth, 0.01f, 0.1f, 1.0f);
    checkbox(edit, "Handrails", c.handrails);
    material(edit, "Material", c.material);
}

void wall(EditSession& edit, WallContent& c) {
    enumCombo<WallStyle>(edit, "Style", c.style, {{WallStyle::Wall, "Solid wall"}, {WallStyle::Flat, "Theatre flat"}});
    dragFloat(edit, "Width", c.width, 0.02f, 0.1f, 100.0f);
    dragFloat(edit, "Height", c.height, 0.02f, 0.1f, 50.0f);
    dragFloat(edit, "Thickness", c.thickness, 0.005f, 0.01f, 5.0f, "%.3f m");
    material(edit, "Material", c.material);
}

void referenceFigure(EditSession& edit, ReferenceFigureContent& c) {
    dragFloat(edit, "Height", c.height, 0.01f, 0.3f, 3.0f);
    material(edit, "Material", c.material);
}

void cameraPreset(EditSession& edit, CameraPresetContent& c) {
    dragFloat(edit, "Field of view", c.fovYDeg, 0.2f, 10.0f, 110.0f, "%.1f deg");
    dragFloat(edit, "Target distance", c.targetDistance, 0.1f, 0.5f, 200.0f);
}

void modelSettings(EditSession& edit, ModelContent& c) {
    checkbox(edit, "Z axis is up", c.zUp);
    dragFloat(edit, "Unit scale", c.unitScale, 0.0005f, 0.0001f, 1000.0f, "%.4f");
    struct Unit {
        const char* label;
        float scale;
    };
    static const Unit kUnits[] = {{"m", 1.0f}, {"cm", 0.01f}, {"mm", 0.001f}, {"inch", 0.0254f}};
    ImGui::TextDisabled("File units");
    for (const Unit& u : kUnits) {
        ImGui::SameLine();
        if (edit.touch(ImGui::SmallButton(u.label), "Unit scale")) c.unitScale = u.scale;
    }
}

// ---- fixture ----------------------------------------------------------------------------------------------

void fixture(EditSession& edit, const fixtures::FixtureLibrary& library, FixtureContent& c) {
    const fixtures::FixtureType* type = library.find(c.fixtureTypeId);
    if (type != nullptr) {
        ImGui::LabelText("Type", "%s %s", type->manufacturer.c_str(), type->name.c_str());
    } else {
        ImGui::LabelText("Type", "%s", c.fixtureTypeId.c_str());
        ImGui::TextColored(ImVec4(1.0f, 0.5f, 0.4f, 1.0f), "This fixture type is not in the library.");
    }

    // DMX mode: choose from the fixture type's modes.
    const fixtures::DmxMode* mode = type != nullptr ? type->findMode(c.modeName) : nullptr;
    bool modeChanged = false;
    if (ImGui::BeginCombo("Mode", c.modeName.c_str())) {
        if (type != nullptr) {
            for (const fixtures::DmxMode& candidate : type->modes) {
                const std::string label = std::format("{}  ({} ch)", candidate.name, candidate.footprint);
                if (ImGui::Selectable(label.c_str(), candidate.name == c.modeName)) {
                    c.modeName = candidate.name;
                    modeChanged = true;
                }
            }
        }
        ImGui::EndCombo();
    }
    edit.touch(modeChanged, "DMX mode");

    ImGui::SeparatorText("DMX patch");
    int universe = static_cast<int>(c.patch.universe);
    int address = static_cast<int>(c.patch.address);
    if (edit.touch(ImGui::InputInt("Universe", &universe, 1, 1), "Universe"))
        c.patch.universe = static_cast<std::uint32_t>(std::clamp(universe, 0, 63999));
    if (edit.touch(ImGui::InputInt("Address", &address, 1, 10), "DMX address"))
        c.patch.address = static_cast<std::uint32_t>(std::clamp(address, 0, 512));
    int number = c.fixtureNumber;
    if (edit.touch(ImGui::InputInt("Fixture number", &number, 1, 10), "Fixture number"))
        c.fixtureNumber = std::max(number, 0);

    if (!c.patch.patched()) {
        ImGui::TextDisabled("Not patched (universe and address must be 1 or more)");
    } else if (mode != nullptr) {
        const int last = static_cast<int>(c.patch.address) + mode->footprint - 1;
        if (last > 512)
            ImGui::TextColored(ImVec4(1.0f, 0.5f, 0.4f, 1.0f), "Uses %d channels: runs past channel 512",
                               mode->footprint);
        else
            ImGui::TextDisabled("Uses channels %u-%d", c.patch.address, last);
    }

    ImGui::SeparatorText("Pan / tilt");
    checkbox(edit, "Invert pan", c.invertPan);
    ImGui::SameLine();
    checkbox(edit, "Invert tilt", c.invertTilt);
    dragFloat(edit, "Pan offset", c.panOffsetDeg, 0.2f, -360.0f, 360.0f, "%.1f deg");
    dragFloat(edit, "Tilt offset", c.tiltOffsetDeg, 0.2f, -360.0f, 360.0f, "%.1f deg");
}

}  // namespace dmxviz::ui::editors
