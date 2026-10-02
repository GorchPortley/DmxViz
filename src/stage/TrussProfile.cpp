#include "stage/TrussProfile.h"

#include <cmath>

namespace dmxviz::stage {

std::vector<glm::vec2> TrussProfile::chordPositions() const {
    const float s = chordSpacing();
    const float h = s * 0.5f;
    switch (shape) {
        case TrussShape::Box: return {{-h, -h}, {-h, h}, {h, h}, {h, -h}};
        case TrussShape::Triangle: {
            // Equilateral, centred on its bounding box: two bottom chords, apex on top.
            const float height = s * std::sqrt(3.0f) * 0.5f;
            return {{-height * 0.5f, -h}, {-height * 0.5f, h}, {height * 0.5f, 0.0f}};
        }
        case TrussShape::Ladder: return {{-h, 0.0f}, {h, 0.0f}};
        case TrussShape::Flat: return {{0.0f, -h}, {0.0f, h}};
    }
    return {};
}

std::string_view trussShapeName(TrussShape shape) {
    switch (shape) {
        case TrussShape::Box: return "box";
        case TrussShape::Triangle: return "triangle";
        case TrussShape::Ladder: return "ladder";
        case TrussShape::Flat: return "flat";
    }
    return "box";
}

bool parseTrussShape(std::string_view name, TrussShape& out) {
    for (TrussShape s : {TrussShape::Box, TrussShape::Triangle, TrussShape::Ladder, TrussShape::Flat}) {
        if (trussShapeName(s) == name) {
            out = s;
            return true;
        }
    }
    return false;
}

const std::vector<TrussProfile>& standardTrussProfiles() {
    // Dimensions follow the common 290 / 390 mm aluminium systems.
    static const std::vector<TrussProfile> profiles = {
        {"F34", TrussShape::Box, 0.29f, 0.05f, 0.016f},
        {"F44", TrussShape::Box, 0.39f, 0.05f, 0.02f},
        {"F33", TrussShape::Triangle, 0.29f, 0.05f, 0.016f},
        {"F32", TrussShape::Ladder, 0.29f, 0.05f, 0.016f},
        {"F32 flat", TrussShape::Flat, 0.29f, 0.05f, 0.016f},
        {"F24", TrussShape::Box, 0.22f, 0.035f, 0.01f},
    };
    return profiles;
}

const TrussProfile* findTrussProfile(std::string_view name) {
    for (const TrussProfile& p : standardTrussProfiles()) {
        if (p.name == name) return &p;
    }
    return nullptr;
}

const std::vector<float>& standardTrussLengths() {
    static const std::vector<float> lengths = {4.0f, 3.0f, 2.5f, 2.0f, 1.5f, 1.0f, 0.5f};
    return lengths;
}

}  // namespace dmxviz::stage
