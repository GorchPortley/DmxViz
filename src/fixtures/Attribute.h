#pragma once
// The canonical vocabulary of things a DMX channel can control.
//
// Names follow GDTF (DIN SPEC 15800) so GDTF imports are lossless: the string
// form of every enumerator is its GDTF attribute name ("ColorAdd_R", "Gobo1Pos",
// "Shutter1Strobe"...). A few extensions that GDTF does not define (amber, lime
// and indigo LEDs, generic "Control") use the same naming style.
//
// Attributes we do not know are kept as Attribute::Unknown and the original name
// is stored next to it (ChannelFunction::attributeName), so files round-trip.
//
// AttributeInfo adds what the runtime and UI need to know about an attribute:
// its family (how the runtime applies it), physical unit, default value range
// and the default FunctionKind.

#include <cstdint>
#include <optional>
#include <span>
#include <string>
#include <string_view>

namespace dmxviz::fixtures {

// Order matters only for the table in Attribute.cpp; never persist the numeric value.
enum class Attribute : std::uint16_t {
    Unknown,
    NoFeature,
    // --- intensity -------------------------------------------------------
    Dimmer,
    // --- position --------------------------------------------------------
    Pan,
    Tilt,
    PanRotate,
    TiltRotate,
    PositionEffect,
    PositionMSpeed,
    // --- additive colour (one emitter colour each) -----------------------
    ColorAdd_R,
    ColorAdd_G,
    ColorAdd_B,
    ColorAdd_C,
    ColorAdd_M,
    ColorAdd_Y,
    ColorAdd_RY,
    ColorAdd_GY,
    ColorAdd_GC,
    ColorAdd_BC,
    ColorAdd_BM,
    ColorAdd_RM,
    ColorAdd_W,
    ColorAdd_WW,
    ColorAdd_CW,
    ColorAdd_UV,
    ColorAdd_A,       // extension: amber LED
    ColorAdd_Lime,    // extension: lime LED
    ColorAdd_Indigo,  // extension: indigo LED
    // --- subtractive colour and colour temperature -----------------------
    ColorSub_C,
    ColorSub_M,
    ColorSub_Y,
    CTO,
    CTC,
    CTB,
    ColorMacro1,
    // --- colour wheels ---------------------------------------------------
    Color1,
    Color2,
    Color3,
    Color1WheelSpin,
    Color2WheelSpin,
    Color3WheelSpin,
    // --- gobo wheels -----------------------------------------------------
    Gobo1,
    Gobo2,
    Gobo1Pos,
    Gobo2Pos,
    Gobo1PosRotate,
    Gobo2PosRotate,
    Gobo1WheelSpin,
    Gobo2WheelSpin,
    // --- animation wheel -------------------------------------------------
    AnimationWheel1,
    AnimationWheel1Pos,
    AnimationWheel1PosRotate,
    // --- prisms ----------------------------------------------------------
    Prism1,
    Prism2,
    Prism1Pos,
    Prism2Pos,
    Prism1PosRotate,
    Prism2PosRotate,
    // --- beam ------------------------------------------------------------
    Zoom,
    Focus1,
    Iris,
    Frost1,
    Frost2,
    // --- shutter and strobe ----------------------------------------------
    Shutter1,
    Shutter1Strobe,
    Shutter1StrobePulse,
    Shutter1StrobePulseClose,
    Shutter1StrobePulseOpen,
    Shutter1StrobeRandom,
    Shutter1StrobeRandomPulse,
    Shutter1StrobeRampUp,
    Shutter1StrobeRampDown,
    Shutter1StrobeRampUpDown,
    Shutter1StrobeLightning,
    Shutter1StrobeSpikes,
    Shutter1StrobeEffect,
    StrobeFrequency,
    StrobeDuration,
    // --- framing shutters ------------------------------------------------
    Blade1A,
    Blade2A,
    Blade3A,
    Blade4A,
    Blade1B,
    Blade2B,
    Blade3B,
    Blade4B,
    Blade1Rot,
    Blade2Rot,
    Blade3Rot,
    Blade4Rot,
    ShaperRot,
    // --- control / effects (decoded but not rendered) --------------------
    Control,
    Effects1,
    Effects1Rate,
    Effects1Fade,
    Effects1Adjust1,

    Count
};

constexpr int kAttributeCount = static_cast<int>(Attribute::Count);

// How the runtime applies an attribute. Numbered attributes (Gobo1/Gobo2...)
// share a family and differ in AttributeInfo::index.
enum class AttributeFamily : std::uint8_t {
    Other,  // decoded and shown in the UI, not rendered
    Dimmer,
    Pan,
    Tilt,
    PanRotate,
    TiltRotate,
    ColorAdd,  // index = emitter slot (see kColorAddCount)
    ColorSub,  // index 0 = C, 1 = M, 2 = Y
    CTO,
    CTC,
    CTB,
    ColorMacro,
    ColorWheel,
    ColorWheelSpin,
    Gobo,
    GoboPos,
    GoboPosRotate,
    GoboWheelSpin,
    Animation,
    AnimationPos,
    AnimationPosRotate,
    Prism,
    PrismPos,
    PrismPosRotate,
    Zoom,
    Focus,
    Iris,
    Frost,
    Shutter,  // all Shutter1* attributes; FunctionKind says open/closed/strobe variant
    StrobeFrequency,
    StrobeDuration,
    BladeA,
    BladeB,
    BladeRot,
    ShaperRot,
};

// Unit of ChannelFunction::physicalFrom/To. In memory angles are radians; in
// files they are degrees (the serializer converts).
enum class PhysicalUnit : std::uint8_t {
    None,          // no physical meaning (wheel selection, control)
    Ratio,         // 0..1 (dimmer, colour level, iris, frost, focus, blade insertion)
    Angle,         // radians in memory, degrees in files
    AngularSpeed,  // rad/s in memory, deg/s in files; sign = direction (+ = CW seen from the lens)
    Frequency,     // Hz
    Temperature,   // Kelvin
    Time,          // seconds
};

// How a ChannelFunction maps its DMX range onto behaviour.
enum class FunctionKind : std::uint8_t {
    Linear,            // physical value interpolated across the DMX range
    WheelSlot,         // selects a wheel position slotFrom..slotTo (1-based, fractional = between slots)
    Spin,              // continuous rotation, physical = angular speed
    ShutterOpen,
    ShutterClosed,
    Strobe,            // regular flashes, physical = frequency (Hz)
    StrobePulse,       // smooth sine-like pulses
    StrobePulseOpen,   // snap open, fade out
    StrobePulseClose,  // fade in, snap closed
    StrobeRandom,      // flashes at random intervals around the frequency
    StrobeRampUp,      // sawtooth up
    StrobeRampDown,    // sawtooth down
    StrobeRampUpDown,  // triangle
    StrobeLightning,   // irregular bursts of short flashes
    NoFeature,         // range does nothing
    Count
};

constexpr int kColorAddCount =
    static_cast<int>(Attribute::ColorAdd_Indigo) - static_cast<int>(Attribute::ColorAdd_R) + 1;

struct AttributeInfo {
    Attribute attribute = Attribute::Unknown;
    std::string_view name;    // GDTF-style canonical name
    std::string_view pretty;  // short label for the UI
    std::string_view group;   // UI feature group: Dimmer, Position, Color, Gobo, Beam, Focus, Shutter, Control
    AttributeFamily family = AttributeFamily::Other;
    std::uint8_t index = 0;  // numbered attributes: 0 for Gobo1, 1 for Gobo2 ...
    PhysicalUnit unit = PhysicalUnit::None;
    FunctionKind defaultKind = FunctionKind::Linear;
    float defaultFrom = 0.0f;  // default physical range (memory units)
    float defaultTo = 1.0f;
};

const AttributeInfo& attributeInfo(Attribute a);
std::span<const AttributeInfo> allAttributes();

// Canonical name ("Dimmer"). Unknown -> "Unknown".
std::string_view attributeName(Attribute a);
// Exact (case-sensitive) GDTF name lookup; unrecognised names give Attribute::Unknown.
Attribute parseAttribute(std::string_view name);

std::string_view functionKindName(FunctionKind k);  // "linear", "wheelSlot", "shutterOpen" ...
std::optional<FunctionKind> parseFunctionKind(std::string_view name);

std::string_view physicalUnitName(PhysicalUnit u);  // "ratio", "degrees" (file unit), ...

inline AttributeFamily attributeFamily(Attribute a) { return attributeInfo(a).family; }
inline bool isShutterKind(FunctionKind k) { return k >= FunctionKind::ShutterOpen && k <= FunctionKind::StrobeLightning; }
inline bool isStrobeKind(FunctionKind k) { return k >= FunctionKind::Strobe && k <= FunctionKind::StrobeLightning; }

// Numbered attribute helpers: makeNumbered(AttributeFamily::Gobo, 1) == Gobo2. Returns Unknown when out of range.
Attribute numberedAttribute(AttributeFamily family, int index);

// File <-> memory conversion of physical values (degrees <-> radians for angles).
float physicalToFile(Attribute a, float memoryValue);
float physicalFromFile(Attribute a, float fileValue);

}  // namespace dmxviz::fixtures
