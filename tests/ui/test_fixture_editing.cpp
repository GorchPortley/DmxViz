#include "fixtures/FixtureRuntime.h"
#include "fixtures/NativeFormat.h"
#include "ui/fixture_editor/ChannelEditing.h"
#include "ui/fixture_editor/EditDocument.h"
#include "ui/fixture_editor/FixtureTemplates.h"
#include "ui/fixture_editor/FixtureValidation.h"
#include "ui/fixture_editor/GeometryEditing.h"

#include <doctest/doctest.h>

#include <filesystem>
#include <set>

using namespace dmxviz;
using namespace dmxviz::ui::fixture_editor;
using fixtures::Attribute;
using fixtures::GeometryType;

namespace {

fixtures::FixtureType movingHead() {
    return makeTemplateFixture(FixtureTemplate::MovingHead);
}

const fixtures::Channel* channelNamed(const fixtures::FixtureType& type, const std::string& name) {
    return type.modes.front().findChannel(name);
}

}  // namespace

// ---------------------------------------------------------------------------
// Templates

TEST_CASE("fixture editor templates: the moving head runs in a FixtureRuntime") {
    const fixtures::FixtureType type = movingHead();
    CHECK(type.beamCount() == 1);
    CHECK(type.wheels.size() == 3);
    CHECK(type.resources.size() == 2);
    CHECK(type.modes.front().footprint == 12);

    fixtures::FixtureRuntime runtime(type, type.modes.front());
    CHECK(runtime.footprint() == 12);
    CHECK(runtime.beamCount() == 1);
}

TEST_CASE("fixture editor templates: survive a save and load as native JSON") {
    for (FixtureTemplate kind : kAllTemplates) {
        CAPTURE(templateName(kind));
        const fixtures::FixtureType original = makeTemplateFixture(kind);
        std::string error;
        const auto loaded = fixtures::FixtureSerializer::fromString(fixtures::FixtureSerializer::toString(original), {}, &error);
        REQUIRE_MESSAGE(loaded.has_value(), error);
        CHECK(validateFixture(*loaded).empty());
        CHECK(loaded->modes.front().channels.size() == original.modes.front().channels.size());
        CHECK(loaded->resources.size() == original.resources.size());
    }
}

// ---------------------------------------------------------------------------
// Geometry editing

TEST_CASE("geometry editing: add, move and remove nodes") {
    fixtures::FixtureType type = makeTemplateFixture(FixtureTemplate::Blank);  // Body -> Beam
    REQUIRE(type.geometry.children.size() == 1);

    const auto added = addChildNode(type, {}, GeometryType::Axis);
    REQUIRE(added.has_value());
    CHECK(*added == NodePath{1});
    CHECK(nodeAt(type.geometry, *added)->name == "Axis");
    CHECK(nodeAt(type.geometry, *added)->type == GeometryType::Axis);
    CHECK(nodeAt(type.geometry, *addChildNode(type, {}, GeometryType::Axis))->name == "Axis 2");

    NodePath path = *added;
    CHECK(moveNode(type, path, -1));
    CHECK(path == NodePath{0});
    CHECK(type.geometry.children[0].name == "Axis");
    CHECK_FALSE(moveNode(type, path, -1));  // already first

    CHECK(removeNode(type, path));
    CHECK(type.geometry.children.size() == 2);
    CHECK_FALSE(removeNode(type, {}));      // the root stays
    CHECK_FALSE(addChildNode(type, {7, 7}, GeometryType::Generic).has_value());
}

TEST_CASE("geometry editing: renaming fixes references") {
    fixtures::FixtureType type = movingHead();
    type.geometryGroups.push_back({"Heads", {"Head"}});
    type.modes.front().geometryRoot = "Head";

    const auto path = findNodePath(type.geometry, "Head");
    REQUIRE(path.has_value());
    CHECK(path->size() == 2);

    CHECK_FALSE(renameNode(type, *path, "Yoke"));  // taken
    CHECK_FALSE(renameNode(type, *path, ""));
    CHECK(renameNode(type, *path, "Tilt Head"));
    CHECK(channelNamed(type, "Tilt")->geometry == "Tilt Head");
    CHECK(type.geometryGroups.front().members.front() == "Tilt Head");
    CHECK(type.modes.front().geometryRoot == "Tilt Head");
    CHECK(validateFixture(type).empty());
}

TEST_CASE("geometry editing: make cells") {
    fixtures::FixtureType type = makeTemplateFixture(FixtureTemplate::Blank);
    CellOptions options;
    options.count = 4;
    options.pitch = 0.2f;
    options.centre = glm::vec3(0.0f, -0.05f, 0.0f);
    options.namePrefix = "Pixel";

    const std::vector<std::string> cells = makeCells(type, {0}, options);
    REQUIRE(cells.size() == 4);
    CHECK(cells.front() == "Pixel 1");
    const fixtures::Geometry* first = type.findGeometry("Pixel 1");
    const fixtures::Geometry* last = type.findGeometry("Pixel 4");
    REQUIRE(first);
    REQUIRE(last);
    CHECK(first->type == GeometryType::Beam);
    CHECK(first->position.x == doctest::Approx(-0.3f));
    CHECK(last->position.x == doctest::Approx(0.3f));
    CHECK(first->position.y == doctest::Approx(-0.05f));
    // Making them again does not clash with the first row.
    CHECK(makeCells(type, {0}, options).front() == "Pixel 1 2");
}

TEST_CASE("geometry editing: which attribute drives an axis") {
    const fixtures::FixtureType type = movingHead();
    const fixtures::DmxMode& mode = type.modes.front();

    const auto yoke = axisDrives(type, mode, "Yoke");
    REQUIRE(yoke.size() == 1);
    CHECK(yoke.front().attribute == Attribute::Pan);
    CHECK(yoke.front().channel == "Pan");

    const auto head = axisDrives(type, mode, "Head");
    REQUIRE(head.size() == 1);
    CHECK(head.front().attribute == Attribute::Tilt);

    CHECK(axisDrives(type, mode, "Base").empty());
    CHECK(channelAxes(type, *mode.findChannel("Dimmer")).empty());  // the Beam node has no axis below it
    CHECK(channelAxes(type, *mode.findChannel("Pan")).size() == 2);  // Yoke first (pan), then Head (tilt)
}

// ---------------------------------------------------------------------------
// Channel editing

TEST_CASE("channel editing: append channels after the highest offset") {
    fixtures::FixtureType type = makeTemplateFixture(FixtureTemplate::Blank);
    fixtures::DmxMode& mode = type.modes.front();
    CHECK(mode.footprint == 1);

    appendChannel(mode, makeChannel("Zoom", Attribute::Zoom, 2, 1, "Beam"));
    CHECK(mode.channels.back().offsets == std::vector<std::uint16_t>{2, 3});
    CHECK(mode.footprint == 3);

    // A removed channel leaves a hole; "pack" closes it.
    mode.channels.erase(mode.channels.begin());
    CHECK(mode.channels.front().offsets.front() == 2);
    renumberOffsets(mode);
    CHECK(mode.channels.front().offsets == std::vector<std::uint16_t>{1, 2});
    CHECK(mode.footprint == 2);
}

TEST_CASE("channel editing: changing the resolution rescales values") {
    fixtures::FixtureType type = makeTemplateFixture(FixtureTemplate::Blank);
    fixtures::DmxMode& mode = type.modes.front();
    fixtures::Channel& dimmer = mode.channels.front();
    dimmer.defaultValue = 128;
    dimmer.functions.front().dmxTo = 255;

    setChannelBytes(mode, 0, 2);
    CHECK(dimmer.offsets.size() == 2);
    CHECK(dimmer.maxValue() == 65535);
    CHECK(dimmer.defaultValue == 128u * 257u);
    CHECK(dimmer.functions.front().dmxTo == 65535);
    CHECK(mode.footprint == 2);
    CHECK(validateFixture(type).empty());

    setChannelBytes(mode, 0, 1);
    CHECK(dimmer.offsets.size() == 1);
    CHECK(dimmer.defaultValue == 128);
    CHECK(dimmer.functions.front().dmxTo == 255);
    CHECK(mode.footprint == 2);  // a larger footprint is kept: the user may want the spare slot
}

TEST_CASE("channel editing: coarse, fine and ultra offsets") {
    fixtures::FixtureType type = makeTemplateFixture(FixtureTemplate::Blank);
    fixtures::DmxMode& mode = type.modes.front();
    mode.channels.push_back(makeChannel("Other", Attribute::Zoom, 1, 2, "Beam"));
    mode.footprint = 2;

    setChannelOffset(mode, 0, 1, 5);  // dimmer gets a fine byte at offset 5
    CHECK(mode.channels[0].offsets == std::vector<std::uint16_t>{1, 5});
    CHECK(mode.channels[0].functions.front().dmxTo == 65535);
    CHECK(mode.footprint == 5);

    setChannelOffset(mode, 0, 0, 3);
    CHECK(mode.channels[0].offsets == std::vector<std::uint16_t>{3, 5});
    setChannelOffset(mode, 0, 2, 4);
    CHECK(mode.channels[0].offsets == std::vector<std::uint16_t>{3, 5, 4});
    CHECK(mode.channels[0].maxValue() == 16777215u);

    setChannelOffset(mode, 0, 1, 0);  // removing fine removes ultra too
    CHECK(mode.channels[0].offsets == std::vector<std::uint16_t>{3});
    CHECK(mode.channels[0].functions.front().dmxTo == 255);
    setChannelOffset(mode, 0, 0, 0);  // the coarse byte stays
    CHECK(mode.channels[0].offsets == std::vector<std::uint16_t>{3});
}

TEST_CASE("channel editing: new channels get sensible defaults") {
    const fixtures::FixtureType type = movingHead();

    const fixtures::Channel pan = newChannelFor(type, Attribute::Pan);
    CHECK(pan.offsets.size() == 2);
    CHECK(pan.geometry == "Yoke");
    CHECK(pan.defaultValue == 32768u);
    CHECK(newChannelFor(type, Attribute::Tilt).geometry == "Head");

    const fixtures::Channel dimmer = newChannelFor(type, Attribute::Dimmer);
    CHECK(dimmer.geometry == "Beam");
    CHECK(dimmer.highlightValue == 255u);
    CHECK(dimmer.functions.front().attribute == Attribute::Dimmer);
    CHECK(newChannelFor(makeTemplateFixture(FixtureTemplate::Blank), Attribute::Pan).geometry.empty());
}

TEST_CASE("channel editing: changing the attribute of a channel") {
    fixtures::Channel single = makeChannel("X", Attribute::Dimmer, 1, 1);
    setChannelAttribute(single, Attribute::Zoom);
    CHECK(single.functions.front().attribute == Attribute::Zoom);
    CHECK(single.functions.front().physicalTo == doctest::Approx(degToRad(50.0f)));  // the usual zoom range

    fixtures::FixtureType type = movingHead();
    fixtures::Channel colors = *type.modes.front().findChannel("Color wheel");
    const std::size_t count = colors.functions.size();
    setChannelAttribute(colors, Attribute::Color2);
    CHECK(colors.functions.size() == count);
    for (const fixtures::ChannelFunction& f : colors.functions) {
        CHECK(f.attribute == Attribute::Color2);
        CHECK(f.kind == fixtures::FunctionKind::WheelSlot);  // slot functions keep their kind
    }
}

TEST_CASE("channel editing: closing gaps between functions") {
    fixtures::Channel channel = makeChannel("Test", Attribute::Dimmer, 1, 1);
    channel.functions = {makeFunction(Attribute::Dimmer, 100, 150), makeFunction(Attribute::Focus1, 10, 40),
                         makeFunction(Attribute::Zoom, 200, 220)};
    closeFunctionGaps(channel);

    REQUIRE(channel.functions.size() == 3);
    CHECK(channel.functions[0].dmxFrom == 0);    // sorted by start, the first one starts at 0
    CHECK(channel.functions[0].dmxTo == 99);
    CHECK(channel.functions[1].dmxFrom == 100);
    CHECK(channel.functions[1].dmxTo == 199);
    CHECK(channel.functions[2].dmxTo == 255);
}

TEST_CASE("channel editing: one function per wheel slot") {
    fixtures::FixtureType type = movingHead();
    fixtures::Channel channel = makeChannel("Colors", Attribute::Color1, 1, 1, "Beam");
    fillWheelFunctions(channel, type.wheels[0], Attribute::Color1);

    REQUIRE(channel.functions.size() == type.wheels[0].slots.size());
    CHECK(channel.functions.front().dmxFrom == 0);
    CHECK(channel.functions.back().dmxTo == 255);
    CHECK(channel.functions[1].wheel == "Color wheel");
    CHECK(channel.functions[1].slotFrom == doctest::Approx(2.0f));
    CHECK(channel.functions[1].name == "Red");
    for (std::size_t i = 1; i < channel.functions.size(); ++i)
        CHECK(channel.functions[i].dmxFrom == channel.functions[i - 1].dmxTo + 1);
}

TEST_CASE("channel editing: pan and tilt range") {
    fixtures::FixtureType type = movingHead();
    type.modes.push_back(duplicateMode(type, type.modes.front()));

    const auto pan = attributeRange(type, Attribute::Pan);
    REQUIRE(pan.has_value());
    CHECK(radToDeg(pan->to) == doctest::Approx(270.0f));

    CHECK(setAttributeRange(type, Attribute::Pan, degToRad(-180.0f), degToRad(180.0f)) == 2);  // one per mode
    CHECK(radToDeg(attributeRange(type, Attribute::Pan)->from) == doctest::Approx(-180.0f));
    CHECK(radToDeg(type.modes.back().findChannel("Pan")->functions.front().physicalTo) == doctest::Approx(180.0f));
    CHECK_FALSE(attributeRange(makeTemplateFixture(FixtureTemplate::LedPar), Attribute::Pan).has_value());
}

TEST_CASE("channel editing: channels for cells") {
    fixtures::FixtureType type = makeTemplateFixture(FixtureTemplate::Blank);
    CellOptions options;
    options.count = 3;
    options.namePrefix = "Pixel";
    const std::vector<std::string> cells = makeCells(type, {0}, options);
    fixtures::DmxMode& mode = type.modes.front();
    const std::vector<Attribute> rgb = {Attribute::ColorAdd_R, Attribute::ColorAdd_G, Attribute::ColorAdd_B};

    SUBCASE("one set per cell") {
        addCellChannels(type, mode, cells, rgb, true, "");
        CHECK(mode.channels.size() == 1 + 9);
        CHECK(mode.footprint == 10);
        CHECK(mode.channels[1].geometry == "Pixel 1");
        CHECK(mode.channels.back().geometry == "Pixel 3");
        CHECK(validateFixture(type).empty());
    }
    SUBCASE("one shared set through a group") {
        addCellChannels(type, mode, cells, rgb, false, "All Pixels");
        CHECK(mode.channels.size() == 1 + 3);
        REQUIRE(type.geometryGroups.size() == 1);
        CHECK(type.geometryGroups.front().name == "All Pixels");
        CHECK(type.geometryGroups.front().members == cells);
        CHECK(mode.channels.back().geometry == "All Pixels");
        CHECK(validateFixture(type).empty());
    }
}

TEST_CASE("channel editing: duplicate a mode and rename a wheel") {
    fixtures::FixtureType type = movingHead();
    type.modes.push_back(duplicateMode(type, type.modes.front()));
    CHECK(type.modes.back().name == "Standard copy");
    type.modes.push_back(duplicateMode(type, type.modes.front()));
    CHECK(type.modes.back().name == "Standard copy 2");

    CHECK_FALSE(renameWheel(type, 0, "Gobo wheel"));
    CHECK(renameWheel(type, 0, "Colours"));
    CHECK(channelNamed(type, "Color wheel")->functions[1].wheel == "Colours");
    CHECK(type.modes.back().findChannel("Color wheel")->functions[1].wheel == "Colours");
    CHECK(validateFixture(type).empty());
}

TEST_CASE("channel editing: gobo images become resources") {
    fixtures::FixtureType type = makeTemplateFixture(FixtureTemplate::Blank);

    std::string error;
    const auto file = readImageFile(std::filesystem::path(DMXVIZ_DATA_DIR) / "gobos" / "dots.svg", &error);
    REQUIRE_MESSAGE(file.has_value(), error);
    CHECK(file->format == "svg");
    CHECK(file->name == "dots");
    CHECK_FALSE(file->data.empty());

    CHECK(addResource(type, file->name, file->format, file->data) == "dots");
    CHECK(addResource(type, file->name, file->format, file->data) == "dots 2");

    CHECK_FALSE(readImageFile(std::filesystem::path(DMXVIZ_DATA_DIR) / "does-not-exist.png", &error).has_value());
    CHECK_FALSE(readImageFile(std::filesystem::path(DMXVIZ_DATA_DIR) / "fixtures", &error).has_value());

    // Nothing refers to them yet.
    CHECK(removeUnusedResources(type) == 2);
    CHECK(type.resources.empty());

    fixtures::FixtureType head = movingHead();
    CHECK(removeUnusedResources(head) == 0);
    head.wheels[1].slots[1].image.clear();
    CHECK(removeUnusedResources(head) == 1);
    CHECK(head.resources.size() == 1);
}

// ---------------------------------------------------------------------------
// Document

TEST_CASE("edit document: copy, modify, revert and apply") {
    const fixtures::FixtureType original = movingHead();
    std::set<std::string> libraryIds = {original.id};
    const IdTakenFn taken = [&](std::string_view id) { return libraryIds.contains(std::string(id)); };

    EditDocument doc;
    CHECK_FALSE(doc.isOpen());
    doc.openCopy(original);
    CHECK(doc.isOpen());
    CHECK_FALSE(doc.isNew());
    CHECK_FALSE(doc.modified());
    CHECK(doc.originalId() == original.id);

    doc.type().description = "changed";
    doc.touch(taken);
    CHECK(doc.modified());
    CHECK(doc.type().id == original.id);  // editing a copy keeps its id (no auto id)

    doc.revert();
    CHECK_FALSE(doc.modified());
    CHECK(doc.type().description == original.description);

    doc.type().description = "changed again";
    doc.touch(taken);
    doc.markApplied();
    CHECK_FALSE(doc.modified());
    doc.revert();
    CHECK(doc.type().description == "changed again");

    doc.close();
    CHECK_FALSE(doc.isOpen());
}

TEST_CASE("edit document: new fixtures get a unique id that follows the name") {
    std::set<std::string> libraryIds = {"custom/moving-head"};
    const IdTakenFn taken = [&](std::string_view id) { return libraryIds.contains(std::string(id)); };

    EditDocument doc;
    doc.openNew(FixtureTemplate::MovingHead, taken);
    CHECK(doc.isNew());
    CHECK(doc.autoId());
    CHECK(doc.type().id == "custom/moving-head-2");

    doc.type().manufacturer = "Acme";
    doc.type().name = "Spotter 700";
    doc.touch(taken);
    CHECK(doc.type().id == "acme/spotter-700");

    doc.setAutoId(false, taken);
    doc.type().id = "my/own";
    doc.type().name = "Renamed";
    doc.touch(taken);
    CHECK(doc.type().id == "my/own");

    CHECK(makeUniqueId("a/b", taken) == "a/b");
    CHECK(makeUniqueId("custom/moving-head", taken) == "custom/moving-head-2");
}

// ---------------------------------------------------------------------------
// Emitters, categories and groups

TEST_CASE("fixture editing: emitters keep their references when renamed") {
    fixtures::FixtureType type = makeTemplateFixture(FixtureTemplate::LedPar);
    const std::size_t first = addEmitter(type);
    const std::size_t second = addEmitter(type);
    CHECK(type.emitters[first].name == "Emitter");
    CHECK(type.emitters[second].name == "Emitter 2");
    type.modes.front().channels[1].functions.front().emitter = "Emitter";  // the red channel

    CHECK_FALSE(renameEmitter(type, first, "Emitter 2"));
    CHECK_FALSE(renameEmitter(type, first, ""));
    CHECK(renameEmitter(type, first, "Deep red"));
    CHECK(type.modes.front().channels[1].functions.front().emitter == "Deep red");
    CHECK(validateFixture(type).empty());

    type.emitters[second].name = "Deep red";
    const std::vector<Problem> problems = validateFixture(type);
    CHECK(countProblems(problems).errors == 1);
}

TEST_CASE("fixture editing: categories as text") {
    CHECK(joinCategories({"Moving Head", "Color Changer"}) == "Moving Head, Color Changer");
    CHECK(joinCategories({}).empty());
    const std::vector<std::string> parsed = splitCategories("  Moving Head ,, Color Changer,");
    REQUIRE(parsed.size() == 2);
    CHECK(parsed[0] == "Moving Head");
    CHECK(parsed[1] == "Color Changer");
    CHECK(splitCategories("   ").empty());
}

TEST_CASE("geometry editing: groups keep their references when renamed") {
    fixtures::FixtureType type = makeTemplateFixture(FixtureTemplate::LedPar);
    const std::size_t group = addGroup(type);
    CHECK(type.geometryGroups[group].name == "Group");
    CHECK(addGroup(type) == group + 1);
    CHECK(type.geometryGroups.back().name == "Group 2");
    type.geometryGroups[group].members = {"Beam"};
    type.modes.front().channels.front().geometry = "Group";

    CHECK_FALSE(renameGroup(type, group, "Beam"));     // a node has this name
    CHECK_FALSE(renameGroup(type, group, "Group 2"));  // another group has it
    CHECK(renameGroup(type, group, "All beams"));
    CHECK(type.modes.front().channels.front().geometry == "All beams");
    CHECK(validateFixture(type).empty());
}

TEST_CASE("fixture validation: mode master ranges and sets") {
    fixtures::FixtureType type = makeTemplateFixture(FixtureTemplate::LedPar);
    fixtures::ChannelFunction& f = type.modes.front().channels.front().functions.front();
    f.modeMaster = "Red";
    f.modeFrom = 50;
    f.modeTo = 10;
    f.sets.push_back({"Bad", 300, 100});

    const std::vector<Problem> problems = validateFixture(type);
    bool reversed = false;
    bool badSet = false;
    for (const Problem& p : problems) {
        reversed |= p.message.find("mode master range") != std::string::npos && p.severity == Severity::Error;
        badSet |= p.message.find("set \"Bad\"") != std::string::npos && p.severity == Severity::Warning;
    }
    CHECK(reversed);
    CHECK(badSet);
}
