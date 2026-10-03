#include "fixtures/Attribute.h"

#include "core/Math.h"

#include <array>
#include <unordered_map>

namespace dmxviz::fixtures {
namespace {

using A = Attribute;
using F = AttributeFamily;
using U = PhysicalUnit;
using K = FunctionKind;

constexpr float kDeg = kPi / 180.0f;

// One row per Attribute enumerator, in enum order (checked at startup by a test
// and by the static_assert below).
// clang-format off
const std::array<AttributeInfo, kAttributeCount> kTable = {{
    {A::Unknown,        "Unknown",        "?",          "Control",  F::Other, 0, U::None, K::Linear, 0, 1},
    {A::NoFeature,      "NoFeature",      "-",          "Control",  F::Other, 0, U::None, K::NoFeature, 0, 0},
    {A::Dimmer,         "Dimmer",         "Dim",        "Dimmer",   F::Dimmer, 0, U::Ratio, K::Linear, 0, 1},
    {A::Pan,            "Pan",            "Pan",        "Position", F::Pan, 0, U::Angle, K::Linear, -270 * kDeg, 270 * kDeg},
    {A::Tilt,           "Tilt",           "Tilt",       "Position", F::Tilt, 0, U::Angle, K::Linear, -135 * kDeg, 135 * kDeg},
    {A::PanRotate,      "PanRotate",      "Pan Rot",    "Position", F::PanRotate, 0, U::AngularSpeed, K::Spin, -360 * kDeg, 360 * kDeg},
    {A::TiltRotate,     "TiltRotate",     "Tilt Rot",   "Position", F::TiltRotate, 0, U::AngularSpeed, K::Spin, -360 * kDeg, 360 * kDeg},
    {A::PositionEffect, "PositionEffect", "Pos FX",     "Position", F::Other, 0, U::None, K::Linear, 0, 1},
    {A::PositionMSpeed, "PositionMSpeed", "Pos Speed",  "Position", F::Other, 0, U::Time, K::Linear, 0, 1},

    {A::ColorAdd_R,      "ColorAdd_R",      "R",      "Color", F::ColorAdd, 0,  U::Ratio, K::Linear, 0, 1},
    {A::ColorAdd_G,      "ColorAdd_G",      "G",      "Color", F::ColorAdd, 1,  U::Ratio, K::Linear, 0, 1},
    {A::ColorAdd_B,      "ColorAdd_B",      "B",      "Color", F::ColorAdd, 2,  U::Ratio, K::Linear, 0, 1},
    {A::ColorAdd_C,      "ColorAdd_C",      "C",      "Color", F::ColorAdd, 3,  U::Ratio, K::Linear, 0, 1},
    {A::ColorAdd_M,      "ColorAdd_M",      "M",      "Color", F::ColorAdd, 4,  U::Ratio, K::Linear, 0, 1},
    {A::ColorAdd_Y,      "ColorAdd_Y",      "Y",      "Color", F::ColorAdd, 5,  U::Ratio, K::Linear, 0, 1},
    {A::ColorAdd_RY,     "ColorAdd_RY",     "RY",     "Color", F::ColorAdd, 6,  U::Ratio, K::Linear, 0, 1},
    {A::ColorAdd_GY,     "ColorAdd_GY",     "GY",     "Color", F::ColorAdd, 7,  U::Ratio, K::Linear, 0, 1},
    {A::ColorAdd_GC,     "ColorAdd_GC",     "GC",     "Color", F::ColorAdd, 8,  U::Ratio, K::Linear, 0, 1},
    {A::ColorAdd_BC,     "ColorAdd_BC",     "BC",     "Color", F::ColorAdd, 9,  U::Ratio, K::Linear, 0, 1},
    {A::ColorAdd_BM,     "ColorAdd_BM",     "BM",     "Color", F::ColorAdd, 10, U::Ratio, K::Linear, 0, 1},
    {A::ColorAdd_RM,     "ColorAdd_RM",     "RM",     "Color", F::ColorAdd, 11, U::Ratio, K::Linear, 0, 1},
    {A::ColorAdd_W,      "ColorAdd_W",      "W",      "Color", F::ColorAdd, 12, U::Ratio, K::Linear, 0, 1},
    {A::ColorAdd_WW,     "ColorAdd_WW",     "WW",     "Color", F::ColorAdd, 13, U::Ratio, K::Linear, 0, 1},
    {A::ColorAdd_CW,     "ColorAdd_CW",     "CW",     "Color", F::ColorAdd, 14, U::Ratio, K::Linear, 0, 1},
    {A::ColorAdd_UV,     "ColorAdd_UV",     "UV",     "Color", F::ColorAdd, 15, U::Ratio, K::Linear, 0, 1},
    {A::ColorAdd_A,      "ColorAdd_A",      "A",      "Color", F::ColorAdd, 16, U::Ratio, K::Linear, 0, 1},
    {A::ColorAdd_Lime,   "ColorAdd_Lime",   "Lime",   "Color", F::ColorAdd, 17, U::Ratio, K::Linear, 0, 1},
    {A::ColorAdd_Indigo, "ColorAdd_Indigo", "Indigo", "Color", F::ColorAdd, 18, U::Ratio, K::Linear, 0, 1},

    {A::ColorSub_C,  "ColorSub_C",  "Cyan",    "Color", F::ColorSub, 0, U::Ratio, K::Linear, 0, 1},
    {A::ColorSub_M,  "ColorSub_M",  "Magenta", "Color", F::ColorSub, 1, U::Ratio, K::Linear, 0, 1},
    {A::ColorSub_Y,  "ColorSub_Y",  "Yellow",  "Color", F::ColorSub, 2, U::Ratio, K::Linear, 0, 1},
    {A::CTO,         "CTO",         "CTO",     "Color", F::CTO, 0, U::Temperature, K::Linear, 6500, 3200},
    {A::CTC,         "CTC",         "CTC",     "Color", F::CTC, 0, U::Temperature, K::Linear, 2700, 8000},
    {A::CTB,         "CTB",         "CTB",     "Color", F::CTB, 0, U::Temperature, K::Linear, 3200, 8000},
    {A::ColorMacro1, "ColorMacro1", "Col Mac", "Color", F::ColorMacro, 0, U::None, K::WheelSlot, 0, 0},

    {A::Color1,          "Color1",          "Col 1",      "Color", F::ColorWheel, 0, U::None, K::WheelSlot, 0, 0},
    {A::Color2,          "Color2",          "Col 2",      "Color", F::ColorWheel, 1, U::None, K::WheelSlot, 0, 0},
    {A::Color3,          "Color3",          "Col 3",      "Color", F::ColorWheel, 2, U::None, K::WheelSlot, 0, 0},
    {A::Color1WheelSpin, "Color1WheelSpin", "Col 1 Spin", "Color", F::ColorWheelSpin, 0, U::AngularSpeed, K::Spin, -360 * kDeg, 360 * kDeg},
    {A::Color2WheelSpin, "Color2WheelSpin", "Col 2 Spin", "Color", F::ColorWheelSpin, 1, U::AngularSpeed, K::Spin, -360 * kDeg, 360 * kDeg},
    {A::Color3WheelSpin, "Color3WheelSpin", "Col 3 Spin", "Color", F::ColorWheelSpin, 2, U::AngularSpeed, K::Spin, -360 * kDeg, 360 * kDeg},

    {A::Gobo1,          "Gobo1",          "Gobo 1",      "Gobo", F::Gobo, 0, U::None, K::WheelSlot, 0, 0},
    {A::Gobo2,          "Gobo2",          "Gobo 2",      "Gobo", F::Gobo, 1, U::None, K::WheelSlot, 0, 0},
    {A::Gobo1Pos,       "Gobo1Pos",       "Gobo 1 Pos",  "Gobo", F::GoboPos, 0, U::Angle, K::Linear, 0, 360 * kDeg},
    {A::Gobo2Pos,       "Gobo2Pos",       "Gobo 2 Pos",  "Gobo", F::GoboPos, 1, U::Angle, K::Linear, 0, 360 * kDeg},
    {A::Gobo1PosRotate, "Gobo1PosRotate", "Gobo 1 Rot",  "Gobo", F::GoboPosRotate, 0, U::AngularSpeed, K::Spin, -360 * kDeg, 360 * kDeg},
    {A::Gobo2PosRotate, "Gobo2PosRotate", "Gobo 2 Rot",  "Gobo", F::GoboPosRotate, 1, U::AngularSpeed, K::Spin, -360 * kDeg, 360 * kDeg},
    {A::Gobo1WheelSpin, "Gobo1WheelSpin", "Gobo 1 Spin", "Gobo", F::GoboWheelSpin, 0, U::AngularSpeed, K::Spin, -360 * kDeg, 360 * kDeg},
    {A::Gobo2WheelSpin, "Gobo2WheelSpin", "Gobo 2 Spin", "Gobo", F::GoboWheelSpin, 1, U::AngularSpeed, K::Spin, -360 * kDeg, 360 * kDeg},

    {A::AnimationWheel1,          "AnimationWheel1",          "Anim",     "Gobo", F::Animation, 0, U::None, K::WheelSlot, 0, 0},
    {A::AnimationWheel1Pos,       "AnimationWheel1Pos",       "Anim Pos", "Gobo", F::AnimationPos, 0, U::Angle, K::Linear, 0, 360 * kDeg},
    {A::AnimationWheel1PosRotate, "AnimationWheel1PosRotate", "Anim Rot", "Gobo", F::AnimationPosRotate, 0, U::AngularSpeed, K::Spin, -360 * kDeg, 360 * kDeg},

    {A::Prism1,          "Prism1",          "Prism 1",     "Beam", F::Prism, 0, U::None, K::WheelSlot, 0, 0},
    {A::Prism2,          "Prism2",          "Prism 2",     "Beam", F::Prism, 1, U::None, K::WheelSlot, 0, 0},
    {A::Prism1Pos,       "Prism1Pos",       "Prism 1 Pos", "Beam", F::PrismPos, 0, U::Angle, K::Linear, 0, 360 * kDeg},
    {A::Prism2Pos,       "Prism2Pos",       "Prism 2 Pos", "Beam", F::PrismPos, 1, U::Angle, K::Linear, 0, 360 * kDeg},
    {A::Prism1PosRotate, "Prism1PosRotate", "Prism 1 Rot", "Beam", F::PrismPosRotate, 0, U::AngularSpeed, K::Spin, -360 * kDeg, 360 * kDeg},
    {A::Prism2PosRotate, "Prism2PosRotate", "Prism 2 Rot", "Beam", F::PrismPosRotate, 1, U::AngularSpeed, K::Spin, -360 * kDeg, 360 * kDeg},

    {A::Zoom,   "Zoom",   "Zoom",    "Beam",  F::Zoom, 0, U::Angle, K::Linear, 5 * kDeg, 50 * kDeg},
    {A::Focus1, "Focus1", "Focus",   "Focus", F::Focus, 0, U::Ratio, K::Linear, 0, 1},
    {A::Iris,   "Iris",   "Iris",    "Beam",  F::Iris, 0, U::Ratio, K::Linear, 1, 0},
    {A::Frost1, "Frost1", "Frost 1", "Beam",  F::Frost, 0, U::Ratio, K::Linear, 0, 1},
    {A::Frost2, "Frost2", "Frost 2", "Beam",  F::Frost, 1, U::Ratio, K::Linear, 0, 1},

    {A::Shutter1,                  "Shutter1",                  "Shutter",     "Shutter", F::Shutter, 0, U::None, K::ShutterOpen, 0, 0},
    {A::Shutter1Strobe,            "Shutter1Strobe",            "Strobe",      "Shutter", F::Shutter, 0, U::Frequency, K::Strobe, 1, 20},
    {A::Shutter1StrobePulse,       "Shutter1StrobePulse",       "Pulse",       "Shutter", F::Shutter, 0, U::Frequency, K::StrobePulse, 0.5f, 10},
    {A::Shutter1StrobePulseClose,  "Shutter1StrobePulseClose",  "Pulse Close", "Shutter", F::Shutter, 0, U::Frequency, K::StrobePulseClose, 0.5f, 10},
    {A::Shutter1StrobePulseOpen,   "Shutter1StrobePulseOpen",   "Pulse Open",  "Shutter", F::Shutter, 0, U::Frequency, K::StrobePulseOpen, 0.5f, 10},
    {A::Shutter1StrobeRandom,      "Shutter1StrobeRandom",      "Random",      "Shutter", F::Shutter, 0, U::Frequency, K::StrobeRandom, 1, 20},
    {A::Shutter1StrobeRandomPulse, "Shutter1StrobeRandomPulse", "Rnd Pulse",   "Shutter", F::Shutter, 0, U::Frequency, K::StrobeRandom, 0.5f, 10},
    {A::Shutter1StrobeRampUp,      "Shutter1StrobeRampUp",      "Ramp Up",     "Shutter", F::Shutter, 0, U::Frequency, K::StrobeRampUp, 0.5f, 10},
    {A::Shutter1StrobeRampDown,    "Shutter1StrobeRampDown",    "Ramp Down",   "Shutter", F::Shutter, 0, U::Frequency, K::StrobeRampDown, 0.5f, 10},
    {A::Shutter1StrobeRampUpDown,  "Shutter1StrobeRampUpDown",  "Ramp Up/Dn",  "Shutter", F::Shutter, 0, U::Frequency, K::StrobeRampUpDown, 0.5f, 10},
    {A::Shutter1StrobeLightning,   "Shutter1StrobeLightning",   "Lightning",   "Shutter", F::Shutter, 0, U::Frequency, K::StrobeLightning, 0.5f, 5},
    {A::Shutter1StrobeSpikes,      "Shutter1StrobeSpikes",      "Spikes",      "Shutter", F::Shutter, 0, U::Frequency, K::StrobeLightning, 0.5f, 5},
    {A::Shutter1StrobeEffect,      "Shutter1StrobeEffect",      "Strobe FX",   "Shutter", F::Shutter, 0, U::Frequency, K::Strobe, 1, 20},
    {A::StrobeFrequency,           "StrobeFrequency",           "Strobe Hz",   "Shutter", F::StrobeFrequency, 0, U::Frequency, K::Linear, 0, 25},
    {A::StrobeDuration,            "StrobeDuration",            "Flash Time",  "Shutter", F::StrobeDuration, 0, U::Time, K::Linear, 0, 0.5f},

    {A::Blade1A,   "Blade1A",   "Blade 1A",  "Shutter", F::BladeA, 0, U::Ratio, K::Linear, 0, 1},
    {A::Blade2A,   "Blade2A",   "Blade 2A",  "Shutter", F::BladeA, 1, U::Ratio, K::Linear, 0, 1},
    {A::Blade3A,   "Blade3A",   "Blade 3A",  "Shutter", F::BladeA, 2, U::Ratio, K::Linear, 0, 1},
    {A::Blade4A,   "Blade4A",   "Blade 4A",  "Shutter", F::BladeA, 3, U::Ratio, K::Linear, 0, 1},
    {A::Blade1B,   "Blade1B",   "Blade 1B",  "Shutter", F::BladeB, 0, U::Ratio, K::Linear, 0, 1},
    {A::Blade2B,   "Blade2B",   "Blade 2B",  "Shutter", F::BladeB, 1, U::Ratio, K::Linear, 0, 1},
    {A::Blade3B,   "Blade3B",   "Blade 3B",  "Shutter", F::BladeB, 2, U::Ratio, K::Linear, 0, 1},
    {A::Blade4B,   "Blade4B",   "Blade 4B",  "Shutter", F::BladeB, 3, U::Ratio, K::Linear, 0, 1},
    {A::Blade1Rot, "Blade1Rot", "Blade 1 R", "Shutter", F::BladeRot, 0, U::Angle, K::Linear, -45 * kDeg, 45 * kDeg},
    {A::Blade2Rot, "Blade2Rot", "Blade 2 R", "Shutter", F::BladeRot, 1, U::Angle, K::Linear, -45 * kDeg, 45 * kDeg},
    {A::Blade3Rot, "Blade3Rot", "Blade 3 R", "Shutter", F::BladeRot, 2, U::Angle, K::Linear, -45 * kDeg, 45 * kDeg},
    {A::Blade4Rot, "Blade4Rot", "Blade 4 R", "Shutter", F::BladeRot, 3, U::Angle, K::Linear, -45 * kDeg, 45 * kDeg},
    {A::ShaperRot, "ShaperRot", "Shaper R",  "Shutter", F::ShaperRot, 0, U::Angle, K::Linear, -45 * kDeg, 45 * kDeg},

    {A::Control,         "Control",         "Ctrl",     "Control", F::Other, 0, U::None, K::Linear, 0, 1},
    {A::Effects1,        "Effects1",        "FX",       "Control", F::Other, 0, U::None, K::Linear, 0, 1},
    {A::Effects1Rate,    "Effects1Rate",    "FX Rate",  "Control", F::Other, 0, U::Ratio, K::Linear, 0, 1},
    {A::Effects1Fade,    "Effects1Fade",    "FX Fade",  "Control", F::Other, 0, U::Time, K::Linear, 0, 1},
    {A::Effects1Adjust1, "Effects1Adjust1", "FX Adj",   "Control", F::Other, 0, U::Ratio, K::Linear, 0, 1},
}};
// clang-format on

constexpr std::array<std::string_view, static_cast<int>(FunctionKind::Count)> kKindNames = {
    "linear",          "wheelSlot",        "spin",         "shutterOpen",    "shutterClosed",
    "strobe",          "strobePulse",      "strobePulseOpen", "strobePulseClose", "strobeRandom",
    "strobeRampUp",    "strobeRampDown",   "strobeRampUpDown", "strobeLightning", "noFeature",
};

const std::unordered_map<std::string_view, Attribute>& nameIndex() {
    static const std::unordered_map<std::string_view, Attribute> index = [] {
        std::unordered_map<std::string_view, Attribute> m;
        for (const AttributeInfo& info : kTable) m.emplace(info.name, info.attribute);
        return m;
    }();
    return index;
}

}  // namespace

const AttributeInfo& attributeInfo(Attribute a) {
    const auto i = static_cast<std::size_t>(a);
    return i < kTable.size() ? kTable[i] : kTable[0];
}

std::span<const AttributeInfo> allAttributes() { return kTable; }

std::string_view attributeName(Attribute a) { return attributeInfo(a).name; }

Attribute parseAttribute(std::string_view name) {
    const auto& index = nameIndex();
    auto it = index.find(name);
    return it == index.end() ? Attribute::Unknown : it->second;
}

std::string_view functionKindName(FunctionKind k) {
    const auto i = static_cast<std::size_t>(k);
    return i < kKindNames.size() ? kKindNames[i] : "linear";
}

std::optional<FunctionKind> parseFunctionKind(std::string_view name) {
    for (std::size_t i = 0; i < kKindNames.size(); ++i)
        if (kKindNames[i] == name) return static_cast<FunctionKind>(i);
    return std::nullopt;
}

std::string_view physicalUnitName(PhysicalUnit u) {
    switch (u) {
        case PhysicalUnit::None: return "none";
        case PhysicalUnit::Ratio: return "ratio 0..1";
        case PhysicalUnit::Angle: return "degrees";
        case PhysicalUnit::AngularSpeed: return "degrees/s";
        case PhysicalUnit::Frequency: return "Hz";
        case PhysicalUnit::Temperature: return "K";
        case PhysicalUnit::Time: return "s";
    }
    return "none";
}

Attribute numberedAttribute(AttributeFamily family, int index) {
    for (const AttributeInfo& info : kTable)
        if (info.family == family && info.index == index && info.attribute != Attribute::Unknown) return info.attribute;
    return Attribute::Unknown;
}

float physicalToFile(Attribute a, float memoryValue) {
    const PhysicalUnit u = attributeInfo(a).unit;
    if (u == PhysicalUnit::Angle || u == PhysicalUnit::AngularSpeed) return radToDeg(memoryValue);
    return memoryValue;
}

float physicalFromFile(Attribute a, float fileValue) {
    const PhysicalUnit u = attributeInfo(a).unit;
    if (u == PhysicalUnit::Angle || u == PhysicalUnit::AngularSpeed) return degToRad(fileValue);
    return fileValue;
}

}  // namespace dmxviz::fixtures
