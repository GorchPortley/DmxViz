#include "fixtures/FixtureType.h"

#include "fixtures/ColorMath.h"

#include <array>
#include <cctype>
#include <format>
#include <set>

namespace dmxviz::fixtures {
namespace {

template <typename Enum, std::size_t N>
std::optional<Enum> parseByName(std::string_view s, const std::array<std::string_view, N>& names) {
    for (std::size_t i = 0; i < N; ++i)
        if (names[i] == s) return static_cast<Enum>(i);
    return std::nullopt;
}

template <std::size_t N>
std::string_view nameOf(std::size_t i, const std::array<std::string_view, N>& names) {
    return i < N ? names[i] : names[0];
}

constexpr std::array<std::string_view, 3> kSourceNames = {"native", "ofl", "gdtf"};
constexpr std::array<std::string_view, 6> kSlotNames = {"open", "color", "gobo", "prism", "animation", "frost"};
constexpr std::array<std::string_view, 3> kGeometryNames = {"generic", "axis", "beam"};
constexpr std::array<std::string_view, 8> kPrimitiveNames = {"none", "box",  "cylinder", "sphere",
                                                             "base", "yoke", "head",     "conventional"};
constexpr std::array<std::string_view, 7> kBeamNames = {"spot", "wash", "beam", "pc", "fresnel", "rectangle", "glow"};

template <typename T>
const T* findByName(const std::vector<T>& items, std::string_view name) {
    for (const T& item : items)
        if (item.name == name) return &item;
    return nullptr;
}

const Geometry* findInTree(const Geometry& node, std::string_view name) {
    if (node.name == name) return &node;
    for (const Geometry& child : node.children)
        if (const Geometry* g = findInTree(child, name)) return g;
    return nullptr;
}

}  // namespace

std::string_view fixtureSourceName(FixtureSource s) { return nameOf(static_cast<std::size_t>(s), kSourceNames); }
std::string_view slotKindName(SlotKind k) { return nameOf(static_cast<std::size_t>(k), kSlotNames); }
std::optional<SlotKind> parseSlotKind(std::string_view s) { return parseByName<SlotKind>(s, kSlotNames); }
std::string_view geometryTypeName(GeometryType t) { return nameOf(static_cast<std::size_t>(t), kGeometryNames); }
std::optional<GeometryType> parseGeometryType(std::string_view s) {
    return parseByName<GeometryType>(s, kGeometryNames);
}
std::string_view primitiveShapeName(PrimitiveShape s) {
    return nameOf(static_cast<std::size_t>(s), kPrimitiveNames);
}
std::optional<PrimitiveShape> parsePrimitiveShape(std::string_view s) {
    return parseByName<PrimitiveShape>(s, kPrimitiveNames);
}
std::string_view beamTypeName(BeamType t) { return nameOf(static_cast<std::size_t>(t), kBeamNames); }
std::optional<BeamType> parseBeamType(std::string_view s) { return parseByName<BeamType>(s, kBeamNames); }

std::vector<glm::vec2> makeCircularPrismFacets(int count, float deflection) {
    std::vector<glm::vec2> facets;
    if (count <= 0) return facets;
    facets.reserve(static_cast<std::size_t>(count));
    // Start at "up" so a 3-facet prism makes the familiar upright triangle.
    for (int i = 0; i < count; ++i) {
        const float a = kPi * 0.5f + 2.0f * kPi * static_cast<float>(i) / static_cast<float>(count);
        facets.emplace_back(deflection * std::cos(a), deflection * std::sin(a));
    }
    return facets;
}

std::vector<glm::vec2> makeLinearPrismFacets(int count, float spacing) {
    std::vector<glm::vec2> facets;
    if (count <= 0) return facets;
    facets.reserve(static_cast<std::size_t>(count));
    const float start = -0.5f * spacing * static_cast<float>(count - 1);
    for (int i = 0; i < count; ++i) facets.emplace_back(start + spacing * static_cast<float>(i), 0.0f);
    return facets;
}

glm::vec3 defaultEmitterColor(Attribute colorAdd) {
    switch (colorAdd) {
        case Attribute::ColorAdd_R: return {1.0f, 0.0f, 0.0f};
        case Attribute::ColorAdd_G: return {0.0f, 1.0f, 0.0f};
        case Attribute::ColorAdd_B: return {0.0f, 0.0f, 1.0f};
        case Attribute::ColorAdd_C: return {0.0f, 1.0f, 1.0f};
        case Attribute::ColorAdd_M: return {1.0f, 0.0f, 1.0f};
        case Attribute::ColorAdd_Y: return {1.0f, 1.0f, 0.0f};
        case Attribute::ColorAdd_RY: return {1.0f, 0.35f, 0.0f};
        case Attribute::ColorAdd_GY: return {0.5f, 1.0f, 0.0f};
        case Attribute::ColorAdd_GC: return {0.0f, 1.0f, 0.5f};
        case Attribute::ColorAdd_BC: return {0.0f, 0.5f, 1.0f};
        case Attribute::ColorAdd_BM: return {0.5f, 0.0f, 1.0f};
        case Attribute::ColorAdd_RM: return {1.0f, 0.0f, 0.5f};
        case Attribute::ColorAdd_W: return kelvinToLinear(6500.0f);
        case Attribute::ColorAdd_WW: return kelvinToLinear(3000.0f);
        case Attribute::ColorAdd_CW: return kelvinToLinear(8000.0f);
        case Attribute::ColorAdd_UV: return {0.18f, 0.0f, 0.6f};
        case Attribute::ColorAdd_A: return {1.0f, 0.52f, 0.0f};
        case Attribute::ColorAdd_Lime: return {0.55f, 1.0f, 0.0f};
        case Attribute::ColorAdd_Indigo: return {0.12f, 0.0f, 1.0f};
        default: return {1.0f, 1.0f, 1.0f};
    }
}

glm::mat4 Geometry::localTransform() const {
    return glm::translate(glm::mat4(1.0f), position) * glm::mat4_cast(rotation);
}

std::string_view ChannelFunction::attributeLabel() const {
    if (attribute == Attribute::Unknown && !attributeName.empty()) return attributeName;
    return fixtures::attributeName(attribute);
}

std::uint32_t Channel::maxValue() const {
    const int bytes = std::clamp(byteCount(), 1, 4);
    return bytes >= 4 ? 0xFFFFFFFFu : (1u << (8 * bytes)) - 1u;
}

const Channel* DmxMode::findChannel(std::string_view channelName) const { return findByName(channels, channelName); }

int DmxMode::highestOffset() const {
    int highest = 0;
    for (const Channel& c : channels)
        for (std::uint16_t o : c.offsets) highest = std::max(highest, static_cast<int>(o));
    return highest;
}

const Wheel* FixtureType::findWheel(std::string_view n) const { return findByName(wheels, n); }
const Emitter* FixtureType::findEmitter(std::string_view n) const { return findByName(emitters, n); }
const Resource* FixtureType::findResource(std::string_view n) const { return findByName(resources, n); }
const GeometryGroup* FixtureType::findGroup(std::string_view n) const { return findByName(geometryGroups, n); }
const DmxMode* FixtureType::findMode(std::string_view n) const { return findByName(modes, n); }
const Geometry* FixtureType::findGeometry(std::string_view n) const { return findInTree(geometry, n); }
Geometry* FixtureType::findGeometry(std::string_view n) { return const_cast<Geometry*>(findInTree(geometry, n)); }

std::string FixtureType::displayName() const {
    if (manufacturer.empty()) return name;
    return manufacturer + " " + name;
}

int FixtureType::beamCount() const {
    int count = 0;
    forEachGeometry(geometry, [&](const Geometry& g, const Geometry*) {
        if (g.type == GeometryType::Beam) ++count;
    });
    return count;
}

std::string slugify(std::string_view text) {
    std::string out;
    bool dash = false;
    for (char ch : text) {
        const auto c = static_cast<unsigned char>(ch);
        if (std::isalnum(c)) {
            if (dash && !out.empty()) out.push_back('-');
            out.push_back(static_cast<char>(std::tolower(c)));
            dash = false;
        } else {
            dash = true;
        }
    }
    return out.empty() ? std::string("fixture") : out;
}

std::string makeFixtureId(std::string_view manufacturer, std::string_view name) {
    return slugify(manufacturer.empty() ? std::string_view("generic") : manufacturer) + "/" + slugify(name);
}

std::vector<std::string> validateFixtureType(const FixtureType& t) {
    std::vector<std::string> problems;
    auto problem = [&](std::string msg) { problems.push_back(std::move(msg)); };

    if (t.id.empty()) problem("id: must not be empty");
    if (t.name.empty()) problem("name: must not be empty");

    std::set<std::string, std::less<>> resourceNames;
    for (std::size_t i = 0; i < t.resources.size(); ++i) {
        if (t.resources[i].name.empty()) problem(std::format("resources[{}].name: must not be empty", i));
        if (!resourceNames.insert(t.resources[i].name).second)
            problem(std::format("resources[{}].name: duplicate resource \"{}\"", i, t.resources[i].name));
    }

    std::set<std::string, std::less<>> wheelNames;
    for (std::size_t w = 0; w < t.wheels.size(); ++w) {
        const Wheel& wheel = t.wheels[w];
        if (!wheelNames.insert(wheel.name).second)
            problem(std::format("wheels[{}].name: duplicate wheel \"{}\"", w, wheel.name));
        if (wheel.slots.empty()) problem(std::format("wheels[{}].slots: a wheel needs at least one slot", w));
        for (std::size_t s = 0; s < wheel.slots.size(); ++s) {
            const WheelSlot& slot = wheel.slots[s];
            if (!slot.image.empty() && !resourceNames.contains(slot.image))
                problem(std::format("wheels[{}].slots[{}].image: unknown resource \"{}\"", w, s, slot.image));
            if (slot.facets.size() > 16)
                problem(std::format("wheels[{}].slots[{}].facets: at most 16 facets are supported", w, s));
        }
    }

    std::set<std::string, std::less<>> geometryNames;
    forEachGeometry(t.geometry, [&](const Geometry& g, const Geometry*) {
        if (g.name.empty()) problem("geometry: every geometry needs a name");
        else if (!geometryNames.insert(g.name).second)
            problem(std::format("geometry \"{}\": duplicate geometry name", g.name));
        if (!g.model.mesh.empty() && !resourceNames.contains(g.model.mesh))
            problem(std::format("geometry \"{}\".model.mesh: unknown resource \"{}\"", g.name, g.model.mesh));
        if (g.type == GeometryType::Beam && g.beam.fieldAngle + 1e-6f < g.beam.beamAngle)
            problem(std::format("geometry \"{}\".beam: fieldAngle must be >= beamAngle", g.name));
    });

    std::set<std::string, std::less<>> groupNames;
    for (std::size_t i = 0; i < t.geometryGroups.size(); ++i) {
        const GeometryGroup& group = t.geometryGroups[i];
        if (geometryNames.contains(group.name))
            problem(std::format("geometryGroups[{}].name: \"{}\" is also a geometry name", i, group.name));
        if (!groupNames.insert(group.name).second)
            problem(std::format("geometryGroups[{}].name: duplicate group \"{}\"", i, group.name));
        for (const std::string& m : group.members)
            if (!geometryNames.contains(m))
                problem(std::format("geometryGroups[{}].members: unknown geometry \"{}\"", i, m));
    }

    if (t.modes.empty()) problem("modes: a fixture type needs at least one mode");
    std::set<std::string, std::less<>> modeNames;
    for (std::size_t m = 0; m < t.modes.size(); ++m) {
        const DmxMode& mode = t.modes[m];
        const std::string mp = std::format("modes[{}]", m);
        if (mode.name.empty()) problem(mp + ".name: must not be empty");
        if (!modeNames.insert(mode.name).second) problem(std::format("{}.name: duplicate mode \"{}\"", mp, mode.name));
        if (mode.footprint < mode.highestOffset() || mode.footprint > 512)
            problem(std::format("{}.footprint: {} does not cover the highest offset {} (max 512)", mp, mode.footprint,
                                mode.highestOffset()));
        if (!mode.geometryRoot.empty() && !geometryNames.contains(mode.geometryRoot))
            problem(std::format("{}.geometryRoot: unknown geometry \"{}\"", mp, mode.geometryRoot));

        std::set<int> usedOffsets;
        std::set<std::string, std::less<>> channelNames;
        for (std::size_t c = 0; c < mode.channels.size(); ++c) {
            const Channel& ch = mode.channels[c];
            const std::string cp = std::format("{}.channels[{}]", mp, c);
            if (!channelNames.insert(ch.name).second)
                problem(std::format("{}.name: duplicate channel name \"{}\"", cp, ch.name));
            if (ch.offsets.size() > 3) problem(cp + ".offsets: at most 3 bytes (24 bit) per channel");
            for (std::uint16_t o : ch.offsets) {
                if (o < 1) problem(cp + ".offsets: offsets are 1-based");
                if (!usedOffsets.insert(o).second)
                    problem(std::format("{}.offsets: offset {} is used by another channel", cp, o));
            }
            if (!ch.geometry.empty() && !geometryNames.contains(ch.geometry) && !groupNames.contains(ch.geometry))
                problem(std::format("{}.geometry: unknown geometry or group \"{}\"", cp, ch.geometry));
            if (ch.defaultValue > ch.maxValue())
                problem(std::format("{}.default: {} exceeds the channel maximum {}", cp, ch.defaultValue, ch.maxValue()));
            for (std::size_t f = 0; f < ch.functions.size(); ++f) {
                const ChannelFunction& fn = ch.functions[f];
                const std::string fp = std::format("{}.functions[{}]", cp, f);
                if (fn.dmxFrom > fn.dmxTo || fn.dmxTo > ch.maxValue())
                    problem(std::format("{}.dmx: range [{}, {}] is invalid for a {}-bit channel", fp, fn.dmxFrom,
                                        fn.dmxTo, 8 * ch.byteCount()));
                if (fn.kind == FunctionKind::WheelSlot) {
                    const Wheel* wheel = t.findWheel(fn.wheel);
                    if (!wheel) {
                        problem(std::format("{}.wheel: unknown wheel \"{}\"", fp, fn.wheel));
                    } else {
                        const float maxSlot = static_cast<float>(wheel->slots.size()) + 1.0f;
                        if (fn.slotFrom < 0.0f || fn.slotTo < 0.0f || fn.slotFrom > maxSlot || fn.slotTo > maxSlot)
                            problem(std::format("{}.slots: [{}, {}] outside wheel \"{}\" ({} slots)", fp, fn.slotFrom,
                                                fn.slotTo, fn.wheel, wheel->slots.size()));
                    }
                }
                if (!fn.emitter.empty() && !t.findEmitter(fn.emitter))
                    problem(std::format("{}.emitter: unknown emitter \"{}\"", fp, fn.emitter));
                if (!fn.modeMaster.empty() && !mode.findChannel(fn.modeMaster))
                    problem(std::format("{}.modeMaster: unknown channel \"{}\"", fp, fn.modeMaster));
            }
        }
    }
    return problems;
}

}  // namespace dmxviz::fixtures
