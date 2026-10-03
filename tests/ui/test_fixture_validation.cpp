#include "fixtures/FixtureLibrary.h"
#include "ui/fixture_editor/FixtureTemplates.h"
#include "ui/fixture_editor/FixtureValidation.h"

#include <doctest/doctest.h>

#include <algorithm>
#include <filesystem>
#include <string>

using namespace dmxviz;
using namespace dmxviz::ui::fixture_editor;
using fixtures::Attribute;

namespace {

fixtures::FixtureType movingHead() {
    return makeTemplateFixture(FixtureTemplate::MovingHead);
}

// The first problem whose message contains `text`, or nullptr.
const Problem* findProblem(const std::vector<Problem>& problems, const std::string& text) {
    for (const Problem& p : problems)
        if (p.message.find(text) != std::string::npos) return &p;
    return nullptr;
}

fixtures::Channel& channelNamed(fixtures::FixtureType& type, const std::string& name) {
    for (fixtures::Channel& c : type.modes.front().channels)
        if (c.name == name) return c;
    FAIL("no such channel: " << name);
    return type.modes.front().channels.front();
}

}  // namespace

TEST_CASE("fixture validation: the templates are valid") {
    for (FixtureTemplate kind : kAllTemplates) {
        CAPTURE(templateName(kind));
        const std::vector<Problem> problems = validateFixture(makeTemplateFixture(kind));
        for (const Problem& p : problems) MESSAGE(p.where << ": " << p.message);
        CHECK(problems.empty());
    }
}

TEST_CASE("fixture validation: the bundled fixtures have no errors") {
    fixtures::FixtureLibrary library;
    REQUIRE(library.loadDirectory(std::filesystem::path(DMXVIZ_DATA_DIR) / "fixtures") > 0);
    for (const fixtures::FixtureType* type : library.all()) {
        CAPTURE(type->id);
        const std::vector<Problem> problems = validateFixture(*type);
        CHECK(countProblems(problems).errors == 0);
        for (const Problem& p : problems)
            if (p.severity == Severity::Error) MESSAGE(p.where << ": " << p.message);
    }
}

TEST_CASE("fixture validation: general fields and unique id") {
    fixtures::FixtureType type = movingHead();
    ValidationOptions options;
    options.idTaken = [&](std::string_view id) { return id == type.id; };
    std::vector<Problem> problems = validateFixture(type, options);
    REQUIRE(findProblem(problems, "already used"));
    CHECK(findProblem(problems, "already used")->area == ProblemArea::General);
    CHECK(findProblem(problems, "already used")->severity == Severity::Error);

    type.name.clear();
    type.id = "Has Space";
    problems = validateFixture(type);
    CHECK(findProblem(problems, "name is empty"));
    CHECK(findProblem(problems, "may only contain"));

    type.id.clear();
    CHECK(findProblem(validateFixture(type), "id is empty"));
}

TEST_CASE("fixture validation: overlapping offsets are errors") {
    fixtures::FixtureType type = movingHead();
    // Dimmer moves onto the offset of Zoom.
    const std::uint16_t zoomOffset = channelNamed(type, "Zoom").offsets.front();
    channelNamed(type, "Dimmer").offsets = {zoomOffset};

    const std::vector<Problem> problems = validateFixture(type);
    const Problem* overlap = findProblem(problems, "overlaps channel");
    REQUIRE(overlap);
    CHECK(overlap->severity == Severity::Error);
    CHECK(overlap->area == ProblemArea::Modes);
    CHECK(overlap->mode == 0);
    CHECK(overlap->channel >= 0);
    CHECK(countProblems(problems).errors >= 1);
}

TEST_CASE("fixture validation: footprint, offsets and names of modes and channels") {
    fixtures::FixtureType type = movingHead();
    type.modes.front().footprint = 3;
    type.modes.front().channels[1].name = type.modes.front().channels[0].name;
    type.modes.front().channels[2].offsets = {0};
    type.modes.push_back(type.modes.front());

    const std::vector<Problem> problems = validateFixture(type);
    CHECK(findProblem(problems, "smaller than the highest channel offset"));
    CHECK(findProblem(problems, "same name"));
    CHECK(findProblem(problems, "Offsets start at 1"));
    CHECK(std::any_of(problems.begin(), problems.end(), [](const Problem& p) { return p.mode == 1; }));
}

TEST_CASE("fixture validation: functions outside the channel resolution") {
    fixtures::FixtureType type = movingHead();
    fixtures::Channel& dimmer = channelNamed(type, "Dimmer");  // 8 bit
    dimmer.functions.front().dmxTo = 300;

    const std::vector<Problem> problems = validateFixture(type);
    const Problem* p = findProblem(problems, "outside the 8-bit channel");
    REQUIRE(p);
    CHECK(p->severity == Severity::Error);
    CHECK(p->function == 0);

    dimmer.functions.front().dmxTo = 255;
    dimmer.defaultValue = 999;
    CHECK(findProblem(validateFixture(type), "default value 999"));

    // The same range is fine for a 16-bit channel.
    fixtures::Channel& pan = channelNamed(type, "Pan");
    CHECK(pan.maxValue() == 65535);
    pan.functions.front().dmxTo = 65535;
    pan.defaultValue = 32768;
    dimmer.defaultValue = 0;
    CHECK(validateFixture(type).empty());
}

TEST_CASE("fixture validation: gaps and overlaps in function ranges") {
    fixtures::FixtureType type = movingHead();
    fixtures::Channel& dimmer = channelNamed(type, "Dimmer");

    SUBCASE("a gap at the end") {
        dimmer.functions.front().dmxTo = 99;
        const std::vector<Problem> problems = validateFixture(type);
        const Problem* gap = findProblem(problems, "DMX 100..255 has no function");
        REQUIRE(gap);
        CHECK(gap->severity == Severity::Warning);
        CHECK(countProblems(problems).errors == 0);
    }
    SUBCASE("a gap in the middle and at the start") {
        dimmer.functions.clear();
        dimmer.functions.push_back(fixtures::ChannelFunction{.attribute = Attribute::Dimmer, .dmxFrom = 10, .dmxTo = 100});
        dimmer.functions.push_back(fixtures::ChannelFunction{.attribute = Attribute::Dimmer, .dmxFrom = 150, .dmxTo = 255});
        const std::vector<Problem> problems = validateFixture(type);
        CHECK(findProblem(problems, "DMX 0..9 has no function"));
        CHECK(findProblem(problems, "DMX 101..149 has no function"));
    }
    SUBCASE("the same attribute twice overlaps, different attributes do not") {
        dimmer.functions.push_back(fixtures::ChannelFunction{.attribute = Attribute::Dimmer, .dmxFrom = 100, .dmxTo = 200});
        CHECK(findProblem(validateFixture(type), "overlap"));

        dimmer.functions.back().attribute = Attribute::Focus1;
        CHECK_FALSE(findProblem(validateFixture(type), "overlap"));
    }
}

TEST_CASE("fixture validation: references to missing geometry and wheels") {
    fixtures::FixtureType type = movingHead();

    SUBCASE("channel geometry") {
        channelNamed(type, "Dimmer").geometry = "Lamp";
        const std::vector<Problem> problems = validateFixture(type);
        const Problem* p = findProblem(problems, "geometry \"Lamp\" does not exist");
        REQUIRE(p);
        CHECK(p->severity == Severity::Error);
    }
    SUBCASE("wheel of a function") {
        type.wheels.erase(type.wheels.begin() + 1);  // the gobo wheel
        const std::vector<Problem> problems = validateFixture(type);
        CHECK(findProblem(problems, "wheel \"Gobo wheel\" does not exist"));
        CHECK(countProblems(problems).errors > 0);
    }
    SUBCASE("removed geometry node") {
        type.geometry.children.clear();  // Yoke and everything below it
        const std::vector<Problem> problems = validateFixture(type);
        CHECK(findProblem(problems, "geometry \"Yoke\" does not exist"));
        CHECK(findProblem(problems, "no Beam node"));
    }
    SUBCASE("image resource of a gobo") {
        type.resources.clear();
        CHECK(findProblem(validateFixture(type), "image resource \"dots\" does not exist"));
    }
    SUBCASE("mode master and emitter") {
        fixtures::ChannelFunction& f = channelNamed(type, "Dimmer").functions.front();
        f.modeMaster = "Nobody";
        f.emitter = "Nothing";
        const std::vector<Problem> problems = validateFixture(type);
        CHECK(findProblem(problems, "mode master channel \"Nobody\""));
        CHECK(findProblem(problems, "emitter \"Nothing\""));
    }
}

TEST_CASE("fixture validation: geometry rules") {
    fixtures::FixtureType type = movingHead();

    SUBCASE("duplicate names") {
        type.geometry.children.front().name = type.geometry.name;
        CHECK(findProblem(validateFixture(type), "same name"));
    }
    SUBCASE("beam angles and lens") {
        fixtures::Geometry* beam = type.findGeometry("Beam");
        REQUIRE(beam);
        beam->beam.fieldAngle = beam->beam.beamAngle - 0.1f;
        beam->beam.lensRadius = 0.0f;
        const std::vector<Problem> problems = validateFixture(type);
        CHECK(findProblem(problems, "field angle"));
        CHECK(findProblem(problems, "lens radius"));
    }
    SUBCASE("a mesh that does not exist") {
        type.geometry.model.mesh = "missing.glb";
        CHECK(findProblem(validateFixture(type), "mesh resource \"missing.glb\""));
    }
    SUBCASE("axis nobody drives") {
        for (fixtures::Channel& c : type.modes.front().channels)
            if (c.name == "Pan") c.functions.front().attribute = Attribute::NoFeature;
        const std::vector<Problem> problems = validateFixture(type);
        const Problem* p = findProblem(problems, "moves this axis");
        REQUIRE(p);
        CHECK(p->severity == Severity::Warning);
        CHECK(p->geometry == "Yoke");
    }
    SUBCASE("pan on a fixture without axes") {
        type.geometry.children.clear();
        for (fixtures::Channel& c : type.modes.front().channels) c.geometry.clear();
        CHECK(findProblem(validateFixture(type), "need an Axis node"));
    }
}

TEST_CASE("fixture validation: wheel rules") {
    fixtures::FixtureType type = movingHead();

    SUBCASE("a gobo slot without image only warns") {
        type.wheels[1].slots[1].image.clear();
        const std::vector<Problem> problems = validateFixture(type);
        const Problem* p = findProblem(problems, "no image");
        REQUIRE(p);
        CHECK(p->severity == Severity::Warning);
        CHECK(p->wheel == 1);
        CHECK(countProblems(problems).errors == 0);
    }
    SUBCASE("empty and duplicate wheels") {
        type.wheels[0].slots.clear();
        type.wheels[2].name = type.wheels[1].name;
        const std::vector<Problem> problems = validateFixture(type);
        CHECK(findProblem(problems, "at least one slot"));
        CHECK(findProblem(problems, "Another wheel has the same name"));
    }
    SUBCASE("too many facets") {
        type.wheels[2].slots[1].facets.assign(20, glm::vec2(0.0f));
        CHECK(findProblem(validateFixture(type), "at most 16 facets"));
    }
    SUBCASE("a wheel no channel uses") {
        for (fixtures::Channel& c : type.modes.front().channels)
            if (c.name == "Prism") c.functions.clear();
        CHECK(findProblem(validateFixture(type), "No channel function uses this wheel"));
    }
}

TEST_CASE("fixture validation: a type without modes") {
    fixtures::FixtureType type = makeTemplateFixture(FixtureTemplate::Blank);
    type.modes.clear();
    const std::vector<Problem> problems = validateFixture(type);
    const Problem* p = findProblem(problems, "at least one DMX mode");
    REQUIRE(p);
    CHECK(p->area == ProblemArea::Modes);
}

TEST_CASE("fixture validation: problems are grouped by tab, errors first") {
    fixtures::FixtureType type = movingHead();
    type.name.clear();                                   // general error
    channelNamed(type, "Dimmer").functions.front().dmxTo = 99;  // modes warning (gap)
    channelNamed(type, "Zoom").geometry = "Nowhere";    // modes error
    const std::vector<Problem> problems = validateFixture(type);
    REQUIRE(problems.size() >= 3);
    CHECK(problems.front().area == ProblemArea::General);
    // Within the Modes area every error precedes every warning.
    bool sawWarning = false;
    for (const Problem& p : problems) {
        if (p.area != ProblemArea::Modes) continue;
        if (p.severity == Severity::Warning) sawWarning = true;
        else CHECK_FALSE(sawWarning);
    }
    CHECK(sawWarning);
}
