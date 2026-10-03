#include "stage/NodeFactory.h"
#include "ui/PatchModel.h"

#include <doctest/doctest.h>

using namespace dmxviz;
using namespace dmxviz::ui;

namespace {

// A library with one fixture type "test/par" that has a 4-slot and a 16-slot mode.
fixtures::FixtureLibrary makeLibrary() {
    fixtures::FixtureLibrary library;
    fixtures::FixtureType type;
    type.manufacturer = "Test";
    type.name = "Par";
    for (const int footprint : {4, 16}) {
        fixtures::DmxMode mode;
        mode.name = std::to_string(footprint) + "ch";
        mode.footprint = footprint;
        type.modes.push_back(mode);
    }
    std::string error;
    REQUIRE(library.addOrReplace(std::move(type), &error));
    return library;
}

NodeId addFixture(stage::Scene& scene, const std::string& mode, stage::DmxPatch patch) {
    return scene.addNode(stage::factory::fixture("test/par", mode, patch, 1));
}

PatchSlot slot(std::uint32_t universe, std::uint32_t address, int footprint) {
    return PatchSlot{{universe, address}, footprint};
}

}  // namespace

TEST_CASE("patch allocator: finds the first free block") {
    PatchAllocator used;
    CHECK(used.findFree(4) == stage::DmxPatch{1, 1});
    used.reserve(1, 1, 4);
    CHECK(used.findFree(4) == stage::DmxPatch{1, 5});
    used.reserve(1, 9, 4);  // leaves a gap of 4 at 5..8
    CHECK(used.findFree(4) == stage::DmxPatch{1, 5});
    CHECK(used.findFree(5) == stage::DmxPatch{1, 13});
}

TEST_CASE("patch allocator: does not cross 512 and moves to the next universe") {
    PatchAllocator used;
    used.reserve(1, 1, 510);
    CHECK_FALSE(used.isFree(1, 511, 4));  // would run to 514
    CHECK(used.findFree(4) == stage::DmxPatch{2, 1});
    CHECK(used.findFree(2) == stage::DmxPatch{1, 511});
    CHECK_FALSE(used.findFree(513).patched());
}

TEST_CASE("patch allocator: reads the scene and honours ignore") {
    const fixtures::FixtureLibrary library = makeLibrary();
    stage::Scene scene;
    const NodeId a = addFixture(scene, "4ch", {1, 1});
    addFixture(scene, "16ch", {1, 5});
    addFixture(scene, "4ch", {});  // unpatched: uses nothing

    const PatchAllocator all(scene, library);
    CHECK(all.findFree(4) == stage::DmxPatch{1, 21});

    const NodeId ignore[] = {a};
    const PatchAllocator withoutA(scene, library, ignore);
    CHECK(withoutA.findFree(4) == stage::DmxPatch{1, 1});
}

TEST_CASE("fixture footprint: follows the mode, unknown types give 0") {
    const fixtures::FixtureLibrary library = makeLibrary();
    stage::FixtureContent fixture;
    fixture.fixtureTypeId = "test/par";
    fixture.modeName = "16ch";
    CHECK(fixtureFootprint(library, fixture) == 16);
    fixture.modeName = "missing";  // falls back to the first mode
    CHECK(fixtureFootprint(library, fixture) == 4);
    fixture.fixtureTypeId = "nobody/home";
    CHECK(fixtureFootprint(library, fixture) == 0);
}

TEST_CASE("patch problems: overlaps are marked on both fixtures") {
    const std::vector<PatchSlot> slots = {slot(1, 1, 10), slot(1, 11, 10), slot(1, 15, 3),
                                          slot(2, 1, 10), slot(0, 0, 10),  slot(1, 505, 10)};
    std::vector<PatchProblem> problems;
    std::vector<int> scratch;
    findPatchProblems(slots, problems, scratch);
    REQUIRE(problems.size() == slots.size());
    CHECK(problems[0] == PatchProblem::None);
    CHECK(problems[1] == PatchProblem::Overlap);  // 11..20 holds 15..17
    CHECK(problems[2] == PatchProblem::Overlap);
    CHECK(problems[3] == PatchProblem::None);      // other universe
    CHECK(problems[4] == PatchProblem::None);      // unpatched
    CHECK(problems[5] == PatchProblem::Overflow);  // 505..514
}

TEST_CASE("patch problems: a long fixture marks every fixture inside it") {
    const std::vector<PatchSlot> slots = {slot(1, 1, 100), slot(1, 10, 5), slot(1, 30, 5), slot(1, 200, 5)};
    std::vector<PatchProblem> problems;
    std::vector<int> scratch;
    findPatchProblems(slots, problems, scratch);
    CHECK(problems[0] == PatchProblem::Overlap);
    CHECK(problems[1] == PatchProblem::Overlap);
    CHECK(problems[2] == PatchProblem::Overlap);
    CHECK(problems[3] == PatchProblem::None);
    CHECK(patchesOverlap(slots[0], slots[2]));
    CHECK_FALSE(patchesOverlap(slots[1], slots[2]));
}

TEST_CASE("auto patch: consecutive, respects footprints and universe boundaries") {
    const int footprints[] = {200, 200, 200, 0, 100};
    const std::vector<stage::DmxPatch> plan = planAutoPatch(footprints, {1, 1});
    REQUIRE(plan.size() == 5);
    CHECK(plan[0] == stage::DmxPatch{1, 1});
    CHECK(plan[1] == stage::DmxPatch{1, 201});
    CHECK(plan[2] == stage::DmxPatch{2, 1});  // 401..600 would cross 512
    CHECK_FALSE(plan[3].patched());           // unknown type stays unpatched
    CHECK(plan[4] == stage::DmxPatch{2, 201});
}

TEST_CASE("auto patch: can skip blocks used by other fixtures") {
    PatchAllocator others;
    others.reserve(1, 5, 10);  // 5..14
    const int footprints[] = {4, 4, 4};
    const std::vector<stage::DmxPatch> plan = planAutoPatch(footprints, {1, 1}, others);
    CHECK(plan[0] == stage::DmxPatch{1, 1});
    CHECK(plan[1] == stage::DmxPatch{1, 15});
    CHECK(plan[2] == stage::DmxPatch{1, 19});
}
