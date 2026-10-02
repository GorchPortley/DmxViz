#pragma once
// Truss cross-section profiles (box, triangle, ladder, flat) and a small
// catalogue of the common aluminium truss sizes.

#include "core/Math.h"

#include <string>
#include <string_view>
#include <vector>

namespace dmxviz::stage {

enum class TrussShape {
    Box,       // 4 chords, zig-zag diagonals on every face
    Triangle,  // 3 chords, apex up (two bottom chords)
    Ladder,    // 2 chords one above the other, joined by perpendicular rungs
    Flat,      // 2 chords side by side, joined by zig-zag diagonals
};

// Describes the cross-section of a truss. A run lies along local +X; the
// section is centred on the X axis in the local YZ plane. All sizes are metres.
struct TrussProfile {
    std::string name = "F34";    // catalogue name, informative only
    TrussShape shape = TrussShape::Box;
    float width = 0.29f;          // outer size of the section (outside of chords)
    float chordDiameter = 0.05f;  // main tubes
    float braceDiameter = 0.016f; // diagonals and rungs

    // Distance between chord centre lines.
    float chordSpacing() const { return width - chordDiameter; }

    // Chord centre positions in the section (y, z), for a run along +X.
    std::vector<glm::vec2> chordPositions() const;

    bool operator==(const TrussProfile&) const = default;
};

std::string_view trussShapeName(TrussShape shape);
bool parseTrussShape(std::string_view name, TrussShape& out);

// The built-in catalogue: F34 / F44 box, F33 triangle, F32 ladder + flat, F24 mini box.
const std::vector<TrussProfile>& standardTrussProfiles();
// Looks a profile up by catalogue name ("F34"); returns nullptr when unknown.
const TrussProfile* findTrussProfile(std::string_view name);

// Standard straight segment lengths in metres, longest first.
const std::vector<float>& standardTrussLengths();

}  // namespace dmxviz::stage
