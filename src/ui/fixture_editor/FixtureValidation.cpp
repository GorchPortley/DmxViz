#include "ui/fixture_editor/FixtureValidation.h"

#include "core/SceneTypes.h"
#include "ui/fixture_editor/ChannelEditing.h"
#include "ui/fixture_editor/GeometryEditing.h"

#include <algorithm>
#include <cmath>
#include <format>
#include <map>
#include <set>
#include <utility>

namespace dmxviz::ui::fixture_editor {

using fixtures::Channel;
using fixtures::ChannelFunction;
using fixtures::DmxMode;
using fixtures::Geometry;
using fixtures::GeometryType;
using fixtures::SlotKind;

namespace {

constexpr int kMaxFootprint = 512;
constexpr int kMaxOffsetBytes = 3;

bool isValidIdChar(char c) {
    return (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '-' || c == '_' || c == '.' || c == '/';
}

std::string nameOrPlaceholder(const std::string& name, const char* placeholder) {
    return name.empty() ? std::string(placeholder) : name;
}

// "Strobe (32..95)": a function as the user sees it in the table.
std::string describeFunction(const ChannelFunction& f) {
    const std::string label = f.name.empty() ? std::string(f.attributeLabel()) : f.name;
    return std::format("{} ({}..{})", label, f.dmxFrom, f.dmxTo);
}

bool hasAxisNode(const fixtures::FixtureType& type) {
    bool found = false;
    fixtures::forEachGeometry(type.geometry, [&](const Geometry& g, const Geometry*) {
        if (g.type == GeometryType::Axis) found = true;
    });
    return found;
}

}  // namespace

void FixtureValidator::add(Severity severity, const Place& place, std::string message) {
    Problem p;
    p.severity = severity;
    p.area = place.area;
    p.where = place.where;
    p.message = std::move(message);
    p.mode = place.mode;
    p.channel = place.channel;
    p.function = place.function;
    p.wheel = place.wheel;
    p.geometry = place.geometry;
    problems_.push_back(std::move(p));
}

std::vector<Problem> FixtureValidator::run() {
    problems_.clear();
    checkGeneral();
    checkGeometry();
    checkWheels();
    checkModes();

    // Group by tab, errors first, otherwise keep the order the checks found them in.
    std::stable_sort(problems_.begin(), problems_.end(), [](const Problem& a, const Problem& b) {
        if (a.area != b.area) return a.area < b.area;
        return a.severity < b.severity;
    });

    // Safety net: whatever the library's own checker finds must not slip through unreported.
    if (countProblems(problems_).errors == 0) {
        for (const std::string& line : fixtures::validateFixtureType(type_)) {
            Place place;
            place.where = "Library check";
            add(Severity::Error, place, line);
        }
    }
    return std::move(problems_);
}

// ---------------------------------------------------------------------------
// General

void FixtureValidator::checkGeneral() {
    Place place;
    place.area = ProblemArea::General;
    place.where = "General";

    if (type_.name.empty()) add(Severity::Error, place, "The name is empty.");

    if (type_.id.empty()) {
        add(Severity::Error, place, "The id is empty.");
    } else {
        if (!std::all_of(type_.id.begin(), type_.id.end(), isValidIdChar))
            add(Severity::Error, place, "The id may only contain a-z, 0-9, '-', '_', '.' and '/'.");
        if (options_.idTaken && options_.idTaken(type_.id))
            add(Severity::Error, place, std::format("The id \"{}\" is already used by another fixture in the library.", type_.id));
    }

    for (std::size_t i = 0; i < type_.emitters.size(); ++i) {
        const std::string& name = type_.emitters[i].name;
        if (name.empty()) add(Severity::Error, place, std::format("Emitter {} has no name.", i + 1));
        for (std::size_t other = 0; other < i; ++other)
            if (!name.empty() && type_.emitters[other].name == name) {
                add(Severity::Error, place, std::format("Two emitters are named \"{}\".", name));
                break;
            }
    }

    const fixtures::PhysicalSpec& physical = type_.physical;
    if (physical.weight < 0.0f || physical.power < 0.0f)
        add(Severity::Error, place, "Weight and power must not be negative.");
    if (hasAxisNode(type_) && (physical.movement.panMaxSpeed <= 0.0f || physical.movement.tiltMaxSpeed <= 0.0f))
        add(Severity::Warning, place, "A maximum pan/tilt speed of zero means the head can never move.");
}

// ---------------------------------------------------------------------------
// Geometry

void FixtureValidator::checkGeometry() {
    std::set<std::string, std::less<>> names;
    int beams = 0;
    fixtures::forEachGeometry(type_.geometry, [&](const Geometry& g, const Geometry*) {
        Place place;
        place.area = ProblemArea::Geometry;
        place.geometry = g.name;
        place.where = std::format("Geometry {}", nameOrPlaceholder(g.name, "(unnamed)"));

        if (g.name.empty()) add(Severity::Error, place, "A geometry node has no name.");
        else if (!names.insert(g.name).second) add(Severity::Error, place, "Another node has the same name.");

        if (!g.model.mesh.empty() && type_.findResource(g.model.mesh) == nullptr)
            add(Severity::Error, place, std::format("The mesh resource \"{}\" does not exist.", g.model.mesh));
        if (g.model.primitive != fixtures::PrimitiveShape::None && g.model.mesh.empty() &&
            (g.model.size.x <= 0.0f || g.model.size.y <= 0.0f || g.model.size.z <= 0.0f))
            add(Severity::Warning, place, "The model has a zero dimension, so it is invisible.");

        if (g.type == GeometryType::Beam) {
            ++beams;
            if (g.beam.lensRadius <= 0.0f) add(Severity::Error, place, "The lens radius must be positive.");
            if (g.beam.fieldAngle + 1e-6f < g.beam.beamAngle)
                add(Severity::Error, place, "The field angle must not be smaller than the beam angle.");
            if (g.beam.luminousFlux < 0.0f) add(Severity::Error, place, "The luminous flux must not be negative.");
        }
    });

    for (std::size_t i = 0; i < type_.geometryGroups.size(); ++i) {
        const fixtures::GeometryGroup& group = type_.geometryGroups[i];
        Place place;
        place.area = ProblemArea::Geometry;
        place.where = std::format("Geometry group {}", nameOrPlaceholder(group.name, "(unnamed)"));
        if (names.contains(group.name)) add(Severity::Error, place, "A geometry node has the same name as this group.");
        for (const std::string& member : group.members)
            if (!names.contains(member))
                add(Severity::Error, place, std::format("Member \"{}\" is not a geometry node.", member));
    }

    if (beams == 0) {
        Place place;
        place.area = ProblemArea::Geometry;
        place.where = "Geometry";
        add(Severity::Warning, place, "There is no Beam node, so the fixture emits no light.");
    }
}

// ---------------------------------------------------------------------------
// Wheels

void FixtureValidator::checkWheels() {
    std::set<std::string, std::less<>> wheelNames;
    std::set<std::string, std::less<>> usedWheels;
    for (const DmxMode& mode : type_.modes)
        for (const Channel& channel : mode.channels)
            for (const ChannelFunction& f : channel.functions)
                if (!f.wheel.empty()) usedWheels.insert(f.wheel);

    for (std::size_t w = 0; w < type_.wheels.size(); ++w) {
        const fixtures::Wheel& wheel = type_.wheels[w];
        Place wheelPlace;
        wheelPlace.area = ProblemArea::Wheels;
        wheelPlace.wheel = static_cast<int>(w);
        wheelPlace.where = std::format("Wheel {}", nameOrPlaceholder(wheel.name, "(unnamed)"));

        if (wheel.name.empty()) add(Severity::Error, wheelPlace, "The wheel has no name.");
        else if (!wheelNames.insert(wheel.name).second) add(Severity::Error, wheelPlace, "Another wheel has the same name.");
        if (wheel.slots.empty()) add(Severity::Error, wheelPlace, "A wheel needs at least one slot.");
        if (!wheel.name.empty() && !usedWheels.contains(wheel.name))
            add(Severity::Warning, wheelPlace, "No channel function uses this wheel, so it has no effect.");

        for (std::size_t s = 0; s < wheel.slots.size(); ++s) {
            const fixtures::WheelSlot& slot = wheel.slots[s];
            Place place = wheelPlace;
            place.where = std::format("Wheel {}, slot {} ({})", nameOrPlaceholder(wheel.name, "(unnamed)"), s + 1, slot.name);
            if (!slot.image.empty() && type_.findResource(slot.image) == nullptr)
                add(Severity::Error, place, std::format("The image resource \"{}\" does not exist.", slot.image));
            if (slot.facets.size() > static_cast<std::size_t>(kMaxPrismFacets))
                add(Severity::Error, place, std::format("A prism supports at most {} facets.", kMaxPrismFacets));
            if (slot.kind == SlotKind::Gobo && slot.image.empty())
                add(Severity::Warning, place, "The gobo slot has no image, so it behaves like an open slot.");
            if (slot.kind == SlotKind::Prism && slot.facets.empty())
                add(Severity::Warning, place, "The prism slot has no facets.");
        }
    }
}

// ---------------------------------------------------------------------------
// Modes and channels

void FixtureValidator::checkModes() {
    if (type_.modes.empty()) {
        Place place;
        place.area = ProblemArea::Modes;
        place.where = "Modes";
        add(Severity::Error, place, "The fixture needs at least one DMX mode.");
        return;
    }
    for (std::size_t m = 0; m < type_.modes.size(); ++m) checkMode(m);
}

void FixtureValidator::checkMode(std::size_t modeIndex) {
    const DmxMode& mode = type_.modes[modeIndex];
    Place place;
    place.area = ProblemArea::Modes;
    place.mode = static_cast<int>(modeIndex);
    place.where = std::format("Mode {}", nameOrPlaceholder(mode.name, "(unnamed)"));

    if (mode.name.empty()) add(Severity::Error, place, "The mode has no name.");
    for (std::size_t other = 0; other < modeIndex; ++other)
        if (type_.modes[other].name == mode.name) {
            add(Severity::Error, place, "Another mode has the same name.");
            break;
        }

    const int highest = mode.highestOffset();
    if (mode.footprint < highest)
        add(Severity::Error, place,
            std::format("The footprint ({}) is smaller than the highest channel offset ({}).", mode.footprint, highest));
    if (mode.footprint > kMaxFootprint)
        add(Severity::Error, place, std::format("The footprint ({}) is larger than a universe ({}).", mode.footprint, kMaxFootprint));
    if (mode.channels.empty()) add(Severity::Warning, place, "The mode has no channels.");
    if (!mode.geometryRoot.empty() && type_.findGeometry(mode.geometryRoot) == nullptr)
        add(Severity::Error, place, std::format("The geometry root \"{}\" does not exist.", mode.geometryRoot));

    checkOffsets(modeIndex);
    for (std::size_t c = 0; c < mode.channels.size(); ++c) checkChannel(modeIndex, c);
    checkAxes(modeIndex);
}

void FixtureValidator::checkOffsets(std::size_t modeIndex) {
    const DmxMode& mode = type_.modes[modeIndex];
    std::map<int, std::size_t> owner;  // offset -> channel that uses it first
    for (std::size_t c = 0; c < mode.channels.size(); ++c) {
        const Channel& channel = mode.channels[c];
        Place place;
        place.area = ProblemArea::Modes;
        place.mode = static_cast<int>(modeIndex);
        place.channel = static_cast<int>(c);
        place.where = std::format("Mode {}, channel {}", nameOrPlaceholder(mode.name, "(unnamed)"),
                                  nameOrPlaceholder(channel.name, "(unnamed)"));

        if (static_cast<int>(channel.offsets.size()) > kMaxOffsetBytes)
            add(Severity::Error, place, "A channel can use at most 3 offsets (24 bit).");
        for (std::uint16_t offset : channel.offsets) {
            if (offset < 1) {
                add(Severity::Error, place, "Offsets start at 1.");
                continue;
            }
            const auto [it, inserted] = owner.emplace(offset, c);
            if (inserted) continue;
            if (it->second == c)
                add(Severity::Error, place, std::format("Offset {} is listed twice.", offset));
            else
                add(Severity::Error, place,
                    std::format("Offset {} overlaps channel {}.", offset, nameOrPlaceholder(mode.channels[it->second].name, "(unnamed)")));
        }
    }
}

void FixtureValidator::checkChannel(std::size_t modeIndex, std::size_t channelIndex) {
    const DmxMode& mode = type_.modes[modeIndex];
    const Channel& channel = mode.channels[channelIndex];
    Place place;
    place.area = ProblemArea::Modes;
    place.mode = static_cast<int>(modeIndex);
    place.channel = static_cast<int>(channelIndex);
    place.where = std::format("Mode {}, channel {}", nameOrPlaceholder(mode.name, "(unnamed)"),
                              nameOrPlaceholder(channel.name, "(unnamed)"));

    if (channel.name.empty()) add(Severity::Error, place, "The channel has no name.");
    for (std::size_t other = 0; other < channelIndex; ++other)
        if (mode.channels[other].name == channel.name) {
            add(Severity::Error, place, "Another channel in this mode has the same name.");
            break;
        }

    const std::uint32_t max = channel.maxValue();
    if (channel.defaultValue > max)
        add(Severity::Error, place, std::format("The default value {} is above the channel maximum {}.", channel.defaultValue, max));
    if (channel.highlightValue && *channel.highlightValue > max)
        add(Severity::Error, place, std::format("The highlight value {} is above the channel maximum {}.", *channel.highlightValue, max));

    if (!channel.geometry.empty() && type_.findGeometry(channel.geometry) == nullptr && type_.findGroup(channel.geometry) == nullptr)
        add(Severity::Error, place, std::format("The geometry \"{}\" does not exist.", channel.geometry));

    if (channel.functions.empty()) add(Severity::Warning, place, "The channel has no functions, so it does nothing.");

    const bool movesAxis = std::any_of(channel.functions.begin(), channel.functions.end(),
                                       [](const ChannelFunction& f) { return isAxisAttribute(f.attribute); });
    if (movesAxis && channelAxes(type_, channel).empty()) {
        add(Severity::Warning, place,
            channel.geometry.empty() ? std::string("Pan/Tilt functions need an Axis node in the geometry tree.")
                                     : std::format("Pan/Tilt functions need an Axis node at or below \"{}\".", channel.geometry));
    }

    checkFunctions(modeIndex, channelIndex);
}

void FixtureValidator::checkFunctions(std::size_t modeIndex, std::size_t channelIndex) {
    const DmxMode& mode = type_.modes[modeIndex];
    const Channel& channel = mode.channels[channelIndex];
    const std::uint32_t max = channel.maxValue();

    Place channelPlace;
    channelPlace.area = ProblemArea::Modes;
    channelPlace.mode = static_cast<int>(modeIndex);
    channelPlace.channel = static_cast<int>(channelIndex);
    channelPlace.where = std::format("Mode {}, channel {}", nameOrPlaceholder(mode.name, "(unnamed)"),
                                     nameOrPlaceholder(channel.name, "(unnamed)"));

    for (std::size_t i = 0; i < channel.functions.size(); ++i) {
        const ChannelFunction& f = channel.functions[i];
        Place place = channelPlace;
        place.function = static_cast<int>(i);
        place.where = std::format("{}, function {} ({})", channelPlace.where, i + 1, f.attributeLabel());

        if (f.dmxFrom > f.dmxTo)
            add(Severity::Error, place, std::format("The DMX range {}..{} is reversed.", f.dmxFrom, f.dmxTo));
        else if (f.dmxTo > max)
            add(Severity::Error, place,
                std::format("The DMX range {}..{} is outside the {}-bit channel (maximum {}).", f.dmxFrom, f.dmxTo,
                            8 * channel.byteCount(), max));

        if (f.attribute == fixtures::Attribute::Unknown && f.attributeName.empty())
            add(Severity::Warning, place, "The attribute is unknown and has no name.");

        if (f.kind == fixtures::FunctionKind::WheelSlot) {
            const fixtures::Wheel* wheel = type_.findWheel(f.wheel);
            if (f.wheel.empty()) {
                add(Severity::Error, place, "No wheel is selected.");
            } else if (wheel == nullptr) {
                add(Severity::Error, place, std::format("The wheel \"{}\" does not exist.", f.wheel));
            } else {
                const float limit = static_cast<float>(wheel->slots.size()) + 1.0f;
                if (f.slotFrom < 0.0f || f.slotTo < 0.0f || f.slotFrom > limit || f.slotTo > limit)
                    add(Severity::Warning, place,
                        std::format("The slot range {}..{} is outside wheel {} ({} slots).", f.slotFrom, f.slotTo, f.wheel,
                                    wheel->slots.size()));
            }
        } else if (!f.wheel.empty() && type_.findWheel(f.wheel) == nullptr) {
            add(Severity::Error, place, std::format("The wheel \"{}\" does not exist.", f.wheel));
        }

        if (!f.emitter.empty() && type_.findEmitter(f.emitter) == nullptr)
            add(Severity::Error, place, std::format("The emitter \"{}\" does not exist.", f.emitter));
        if (!f.modeMaster.empty() && mode.findChannel(f.modeMaster) == nullptr)
            add(Severity::Error, place, std::format("The mode master channel \"{}\" does not exist.", f.modeMaster));
        if (!f.modeMaster.empty() && f.modeFrom > f.modeTo)
            add(Severity::Error, place, std::format("The mode master range {}..{} is reversed.", f.modeFrom, f.modeTo));
        for (const fixtures::ChannelSet& set : f.sets) {
            if (set.dmxFrom > set.dmxTo || set.dmxTo > max)
                add(Severity::Warning, place, std::format("The set \"{}\" has an invalid DMX range {}..{}.", set.name, set.dmxFrom, set.dmxTo));
        }
    }

    checkFunctionRanges(channelPlace, channel);
}

void FixtureValidator::checkFunctionRanges(const Place& channelPlace, const Channel& channel) {
    // Functions switched by a mode master legitimately share ranges and leave holes: skip those channels.
    for (const ChannelFunction& f : channel.functions)
        if (!f.modeMaster.empty()) return;

    struct Span {
        std::uint64_t from = 0;
        std::uint64_t to = 0;
        const ChannelFunction* function = nullptr;
    };
    std::vector<Span> spans;
    for (const ChannelFunction& f : channel.functions)
        if (f.dmxFrom <= f.dmxTo) spans.push_back({f.dmxFrom, std::min<std::uint64_t>(f.dmxTo, channel.maxValue()), &f});
    if (spans.empty()) return;
    std::stable_sort(spans.begin(), spans.end(), [](const Span& a, const Span& b) { return a.from < b.from; });

    std::uint64_t next = 0;  // first DMX value no function covers yet
    for (const Span& span : spans) {
        if (span.from > next)
            add(Severity::Warning, channelPlace,
                std::format("DMX {}..{} has no function: the fixture ignores those values.", next, span.from - 1));
        next = std::max(next, span.to + 1);
    }
    if (next <= channel.maxValue())
        add(Severity::Warning, channelPlace,
            std::format("DMX {}..{} has no function: the fixture ignores those values.", next, channel.maxValue()));

    // Overlaps of different attributes are deliberate ("prism in + rotate"); the same attribute twice is not.
    for (std::size_t i = 0; i < spans.size(); ++i)
        for (std::size_t j = i + 1; j < spans.size(); ++j) {
            if (spans[j].from > spans[i].to) continue;
            if (spans[i].function->attribute != spans[j].function->attribute) continue;
            add(Severity::Warning, channelPlace,
                std::format("Functions {} and {} overlap.", describeFunction(*spans[i].function),
                            describeFunction(*spans[j].function)));
        }
}

void FixtureValidator::checkAxes(std::size_t modeIndex) {
    const DmxMode& mode = type_.modes[modeIndex];
    fixtures::forEachGeometry(type_.geometry, [&](const Geometry& g, const Geometry*) {
        if (g.type != GeometryType::Axis) return;
        if (!axisDrives(type_, mode, g.name).empty()) return;
        Place place;
        place.area = ProblemArea::Geometry;
        place.mode = static_cast<int>(modeIndex);
        place.geometry = g.name;
        place.where = std::format("Geometry {}, mode {}", g.name, nameOrPlaceholder(mode.name, "(unnamed)"));
        add(Severity::Warning, place, "No Pan or Tilt channel moves this axis in this mode.");
    });
}

// ---------------------------------------------------------------------------

std::vector<Problem> validateFixture(const fixtures::FixtureType& type, const ValidationOptions& options) {
    return FixtureValidator(type, options).run();
}

ProblemCounts countProblems(const std::vector<Problem>& problems) {
    ProblemCounts counts;
    for (const Problem& p : problems) (p.severity == Severity::Error ? counts.errors : counts.warnings) += 1;
    return counts;
}

}  // namespace dmxviz::ui::fixture_editor
