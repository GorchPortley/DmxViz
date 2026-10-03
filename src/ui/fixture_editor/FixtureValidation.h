#pragma once
// FixtureValidator: finds the problems in a fixture type while it is being edited.
//
// It is the engine behind the fixture editor's validation list. Every problem says how bad it
// is (an Error blocks "Apply", a Warning does not), which editor tab it belongs to and where it
// is (mode / channel / function / wheel / geometry), so the list can jump to it.
//
// Rules (E = error, W = warning):
//   General   E name or id empty, id with odd characters, id used by another library type,
//             empty or duplicate emitter names; W movement speed of zero for a fixture with axes
//   Geometry  E empty or duplicate names, missing mesh resource, field angle < beam angle,
//             beam without a positive lens radius; W no Beam node, axis nobody drives
//   Wheels    E empty or duplicate wheel names, wheel without slots, slot image missing,
//             more than 16 facets; W gobo slot without image, prism slot without facets
//   Modes     E no modes, empty or duplicate names, footprint smaller than the highest offset
//             or above 512, unknown geometry root;
//             channels: E duplicate names, bad or overlapping offsets, default above the
//             channel maximum, unknown geometry or group; W no functions;
//             functions: E range outside the channel resolution or reversed, unknown wheel,
//             emitter or mode master, reversed mode master range; W invalid set ranges, gaps
//             between functions, overlapping functions of the same attribute, slot range outside
//             the wheel, pan/tilt on a geometry without axis
//
// No ImGui and no EditorContext in here: the rules are covered by unit tests.

#include "fixtures/FixtureType.h"

#include <cstdint>
#include <functional>
#include <string>
#include <string_view>
#include <vector>

namespace dmxviz::ui::fixture_editor {

enum class Severity : std::uint8_t { Error, Warning };

// The editor tab a problem belongs to.
enum class ProblemArea : std::uint8_t { General, Geometry, Wheels, Modes };

struct Problem {
    Severity severity = Severity::Error;
    ProblemArea area = ProblemArea::General;
    std::string where;    // "Mode Standard, channel Pan"
    std::string message;  // "offset 3 is also used by channel Tilt"
    // Position, for jumping there. -1 = not applicable.
    int mode = -1;
    int channel = -1;
    int function = -1;
    int wheel = -1;
    std::string geometry;  // geometry node name
};

struct ValidationOptions {
    // True when another fixture type in the library already uses this id. The type being edited
    // must not count as "another", so the caller excludes the id it started from.
    std::function<bool(std::string_view)> idTaken;
};

struct ProblemCounts {
    int errors = 0;
    int warnings = 0;
};

class FixtureValidator {
public:
    FixtureValidator(const fixtures::FixtureType& type, const ValidationOptions& options)
        : type_(type), options_(options) {}

    // Runs every check. Errors come first in each area, in the order of the tabs.
    std::vector<Problem> run();

private:
    // Where a problem is reported from; filled as the checks walk the type.
    struct Place {
        ProblemArea area = ProblemArea::General;
        std::string where;
        int mode = -1;
        int channel = -1;
        int function = -1;
        int wheel = -1;
        std::string geometry;
    };

    void add(Severity severity, const Place& place, std::string message);

    void checkGeneral();
    void checkGeometry();
    void checkWheels();
    void checkModes();
    void checkMode(std::size_t modeIndex);
    void checkOffsets(std::size_t modeIndex);
    void checkChannel(std::size_t modeIndex, std::size_t channelIndex);
    void checkFunctions(std::size_t modeIndex, std::size_t channelIndex);
    void checkFunctionRanges(const Place& channelPlace, const fixtures::Channel& channel);
    void checkAxes(std::size_t modeIndex);

    const fixtures::FixtureType& type_;
    const ValidationOptions& options_;
    std::vector<Problem> problems_;
};

// Convenience wrapper around FixtureValidator.
std::vector<Problem> validateFixture(const fixtures::FixtureType& type, const ValidationOptions& options = {});

ProblemCounts countProblems(const std::vector<Problem>& problems);

}  // namespace dmxviz::ui::fixture_editor
