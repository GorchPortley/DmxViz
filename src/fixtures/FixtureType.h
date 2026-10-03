#pragma once
// The fixture type data model: everything DmxViz knows about one kind of
// lighting fixture, independent of where it came from (native file, Open
// Fixture Library, GDTF). See docs/ARCHITECTURE.md §5 and docs/FIXTURE_FORMAT.md.
//
//   FixtureType
//   ├── wheels[]      colour / gobo / prism / animation / frost wheels
//   ├── emitters[]    named LED colours used by additive mixing
//   ├── resources[]   gobo images and 3D model files (raw bytes, self-contained)
//   ├── geometry      tree of Generic / Axis / Beam nodes (fixture hanging, beam along local -Y)
//   └── modes[]       DMX modes -> channels -> channel functions
//
// Units in memory: metres, radians, linear RGB, Kelvin, Hz, seconds.
// The structs are plain data on purpose: importers fill them, the fixture
// editor edits them, the runtime reads them.

#include "core/Math.h"
#include "fixtures/Attribute.h"

#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace dmxviz::fixtures {

enum class FixtureSource : std::uint8_t { Native, Ofl, Gdtf };
std::string_view fixtureSourceName(FixtureSource s);  // "native" | "ofl" | "gdtf"

// Binary file carried inside a fixture type so it stays self-contained after
// import (gobo images, 3D models, glTF buffers).
struct Resource {
    std::string name;    // unique key within the fixture type, referenced by wheel slots and models
    std::string format;  // lower-case file extension without the dot: png, svg, jpg, glb, gltf, 3ds, obj, bin
    std::vector<std::uint8_t> data;
};

// ---------------------------------------------------------------- wheels

enum class SlotKind : std::uint8_t { Open, Color, Gobo, Prism, AnimationWheel, Frost };
std::string_view slotKindName(SlotKind k);  // "open", "color", "gobo", "prism", "animation", "frost"
std::optional<SlotKind> parseSlotKind(std::string_view s);

struct WheelSlot {
    SlotKind kind = SlotKind::Open;
    std::string name;
    glm::vec3 color{1.0f};          // linear RGB transmission of the filter (1 = passes everything)
    std::string image;              // resource name of the gobo / animation image (white = light passes)
    std::vector<glm::vec2> facets;  // prism: each facet's sub-beam offset from the beam axis, radians,
                                    // x = right, y = up when looking along the beam
    float frost = 0.0f;             // frost slots: diffusion 0..1
};

struct Wheel {
    std::string name;
    std::vector<WheelSlot> slots;  // slot 1 is slots[0]
};

// Facet patterns for prism slots. deflection = angle between each sub-beam and the main axis.
std::vector<glm::vec2> makeCircularPrismFacets(int count, float deflection);
std::vector<glm::vec2> makeLinearPrismFacets(int count, float spacing);

// ---------------------------------------------------------------- emitters

// A named light source colour (LED die). ColorAdd channel functions may name
// one; otherwise the attribute's standard colour is used (red for ColorAdd_R...).
struct Emitter {
    std::string name;
    glm::vec3 color{1.0f};          // linear RGB, largest component 1
    float dominantWavelength = 0.0f;  // nm, 0 = unknown
};

// Standard emitter colour for a ColorAdd attribute (linear RGB, max component 1).
glm::vec3 defaultEmitterColor(Attribute colorAdd);

// ---------------------------------------------------------------- geometry

enum class GeometryType : std::uint8_t { Generic, Axis, Beam };
std::string_view geometryTypeName(GeometryType t);  // "generic", "axis", "beam"
std::optional<GeometryType> parseGeometryType(std::string_view s);

// Built-in shapes drawn when a geometry has no mesh (or its mesh fails to load).
// All are centred on the geometry origin and fill `size`.
enum class PrimitiveShape : std::uint8_t {
    None,          // invisible node
    Box,
    Cylinder,      // axis along local Y
    Sphere,        // ellipsoid filling the box
    Base,          // moving head base: rounded box
    Yoke,          // U-shaped yoke hanging down from its origin
    Head,          // moving head: cylinder along Y with a lens bezel at -Y
    Conventional,  // par can / profile body: cylinder along Y, lens at -Y
};
std::string_view primitiveShapeName(PrimitiveShape s);
std::optional<PrimitiveShape> parsePrimitiveShape(std::string_view s);

struct ModelSpec {
    PrimitiveShape primitive = PrimitiveShape::None;
    glm::vec3 size{0.0f};       // metres; bounding box of the part along local X, Y, Z
    std::string mesh;           // resource name of a 3D model file; the primitive is the fallback
    glm::vec3 color{0.025f};    // linear albedo for primitives (and meshes without material)

    bool empty() const { return primitive == PrimitiveShape::None && mesh.empty(); }
};

// GDTF beam types. Mapped to the renderer's BeamShape by the runtime.
enum class BeamType : std::uint8_t { Spot, Wash, Beam, PC, Fresnel, Rectangle, Glow };
std::string_view beamTypeName(BeamType t);  // "spot", "wash", ...
std::optional<BeamType> parseBeamType(std::string_view s);

struct BeamSpec {
    BeamType type = BeamType::Wash;
    float lensRadius = 0.05f;              // m
    float beamAngle = degToRad(25.0f);     // rad, full angle at 50 % intensity, before zoom
    float fieldAngle = degToRad(32.0f);    // rad, full angle at 10 % intensity
    float luminousFlux = 5000.0f;          // lm at full output
    float colorTemperature = 6500.0f;      // K, colour of the "white" source (also the CTO/CTB reference)
    glm::vec2 emitterSize{0.1f, 0.1f};     // m, width/height of rectangular emitters
};

// A node of the fixture's body. Hanging convention: base at the top, the beam
// of a Beam node leaves along local -Y at pan = tilt = 0, local +Z is "up" for
// gobos and prisms. Axis nodes rotate around local Y when driven by Pan and
// around local X when driven by Tilt.
struct Geometry {
    std::string name;  // unique within the fixture type; channels refer to it
    GeometryType type = GeometryType::Generic;
    glm::vec3 position{0.0f};                       // m, relative to the parent
    glm::quat rotation{1.0f, 0.0f, 0.0f, 0.0f};     // relative to the parent
    ModelSpec model;
    BeamSpec beam;  // only used when type == Beam
    std::vector<Geometry> children;

    glm::mat4 localTransform() const;
};

// A named set of geometries one channel can control together (OFL pixel
// groups like "left half"). Channel::geometry may name a group instead of a node.
struct GeometryGroup {
    std::string name;
    std::vector<std::string> members;  // geometry names
};

// ---------------------------------------------------------------- DMX

// Named sub-range of a channel function, for the UI ("Gobo 3 shake", "Open").
struct ChannelSet {
    std::string name;
    std::uint32_t dmxFrom = 0;
    std::uint32_t dmxTo = 0;
};

// One DMX range of a channel and what it does. Ranges of the functions of a
// channel normally do not overlap; if they do, every matching function applies
// (used for "prism in + rotate" style ranges that drive two attributes).
struct ChannelFunction {
    std::string name;  // optional label, e.g. "Strobe slow -> fast"
    Attribute attribute = Attribute::NoFeature;
    std::string attributeName;  // original name, kept when attribute == Unknown
    FunctionKind kind = FunctionKind::NoFeature;
    std::uint32_t dmxFrom = 0;  // in the channel's resolution (0..255 / 0..65535 / ...)
    std::uint32_t dmxTo = 255;
    float physicalFrom = 0.0f;  // memory units of the attribute (see PhysicalUnit)
    float physicalTo = 1.0f;
    std::string wheel;          // WheelSlot functions: the wheel
    float slotFrom = 1.0f;      // WheelSlot: 1-based wheel position at dmxFrom (fractional = between slots)
    float slotTo = 1.0f;        // ... and at dmxTo
    std::string emitter;        // ColorAdd: optional emitter name
    std::string modeMaster;     // if set: only active while that channel's value is in [modeFrom, modeTo]
    std::uint32_t modeFrom = 0;
    std::uint32_t modeTo = 0;
    std::vector<ChannelSet> sets;

    // Canonical attribute name, or the preserved original for Unknown attributes.
    std::string_view attributeLabel() const;
};

struct Channel {
    std::string name;
    std::vector<std::uint16_t> offsets;  // 1-based within the mode, coarse first (8/16/24 bit).
                                         // Empty = virtual channel that always holds defaultValue.
    std::string geometry;                // geometry node or group it controls; empty = whole fixture
    std::uint32_t defaultValue = 0;      // channel resolution
    std::optional<std::uint32_t> highlightValue;
    std::vector<ChannelFunction> functions;

    int byteCount() const { return offsets.empty() ? 1 : static_cast<int>(offsets.size()); }
    std::uint32_t maxValue() const;
};

struct DmxMode {
    std::string name;
    std::string description;
    int footprint = 0;          // number of DMX slots the mode occupies
    std::string geometryRoot;   // optional: only this subtree is the fixture in this mode (GDTF)
    std::vector<Channel> channels;

    const Channel* findChannel(std::string_view channelName) const;
    // Highest offset used by any channel (footprint should be >= this).
    int highestOffset() const;
};

// ---------------------------------------------------------------- physical

enum class DimmerCurve : std::uint8_t { Linear, SquareLaw };

struct MovementSpec {
    float panMaxSpeed = degToRad(216.0f);       // rad/s (540 deg in 2.5 s)
    float tiltMaxSpeed = degToRad(180.0f);      // rad/s
    float panAcceleration = degToRad(600.0f);   // rad/s^2
    float tiltAcceleration = degToRad(600.0f);  // rad/s^2
    float wheelSlotsPerSecond = 10.0f;          // how fast colour/gobo/prism wheels index between slots
    float indexRotationSpeed = degToRad(720.0f);  // rad/s, gobo/prism indexing rotation speed
};

struct PhysicalSpec {
    float weight = 0.0f;              // kg
    float power = 0.0f;               // W
    glm::vec3 dimensions{0.0f};       // m, width (X) x height (Y) x depth (Z)
    DimmerCurve dimmerCurve = DimmerCurve::Linear;
    MovementSpec movement;
};

// ---------------------------------------------------------------- type

struct FixtureType {
    std::string id;  // "manufacturer/model" slug, unique in the library
    std::string manufacturer;
    std::string name;
    std::string shortName;
    std::string description;
    FixtureSource source = FixtureSource::Native;
    std::string revision;
    std::vector<std::string> categories;  // e.g. "Moving Head", "Color Changer" (OFL vocabulary)

    PhysicalSpec physical;
    std::vector<Wheel> wheels;
    std::vector<Emitter> emitters;
    std::vector<Resource> resources;
    Geometry geometry;  // root node
    std::vector<GeometryGroup> geometryGroups;
    std::vector<DmxMode> modes;

    const Wheel* findWheel(std::string_view wheelName) const;
    const Emitter* findEmitter(std::string_view emitterName) const;
    const Resource* findResource(std::string_view resourceName) const;
    const Geometry* findGeometry(std::string_view geometryName) const;
    Geometry* findGeometry(std::string_view geometryName);
    const GeometryGroup* findGroup(std::string_view groupName) const;
    const DmxMode* findMode(std::string_view modeName) const;

    std::string displayName() const;  // "Manufacturer Name"
    int beamCount() const;            // Beam nodes in the whole tree
};

// Depth-first pre-order visit of a geometry tree. fn(node, parentOrNull).
template <typename Fn>
void forEachGeometry(const Geometry& root, Fn&& fn, const Geometry* parent = nullptr) {
    fn(root, parent);
    for (const Geometry& child : root.children) forEachGeometry(child, fn, &root);
}

// Checks names, references and ranges. Returns one human-readable line per
// problem, prefixed with a path ("modes[0].channels[2].offsets"). Empty = valid.
std::vector<std::string> validateFixtureType(const FixtureType& type);

// "Clay Paky", "Sharpy" -> "clay-paky/sharpy".
std::string makeFixtureId(std::string_view manufacturer, std::string_view name);
std::string slugify(std::string_view text);

}  // namespace dmxviz::fixtures
