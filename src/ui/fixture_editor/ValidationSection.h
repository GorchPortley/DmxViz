#pragma once
// ValidationSection: the "Validation" tab of the fixture editor. Lists the problems found by
// FixtureValidator, grouped by tab, errors first. Clicking a problem asks the panel to jump to it.

#include "ui/fixture_editor/FixtureValidation.h"

#include <optional>
#include <vector>

namespace dmxviz::ui::fixture_editor {

class ValidationSection {
public:
    // Draws the list. Returns the problem the user clicked, if any.
    std::optional<Problem> draw(const std::vector<Problem>& problems);

    // "Geometry", "Wheels"... for a problem area (also used for the tab names).
    static const char* areaName(ProblemArea area);
};

}  // namespace dmxviz::ui::fixture_editor
