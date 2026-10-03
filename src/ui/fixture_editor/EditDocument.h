#pragma once
// EditDocument: the fixture type the editor is working on.
//
// The editor never touches the library's copy. It edits a copy held here and writes it back with
// "Apply". The document remembers
//   * the id the copy started from (so Apply knows whether it replaces a library type or adds one),
//   * a baseline to "Revert" to,
//   * a revision counter that every edit bumps (the preview and the validation re-run when it changes),
//   * whether the id follows manufacturer + name automatically (new fixtures).
// No ImGui in here.

#include "fixtures/FixtureType.h"
#include "ui/fixture_editor/FixtureTemplates.h"

#include <cstdint>
#include <functional>
#include <string>
#include <string_view>

namespace dmxviz::ui::fixture_editor {

// True when another type of the library uses the id.
using IdTakenFn = std::function<bool(std::string_view)>;

// "base" or, if taken, "base-2", "base-3"...
std::string makeUniqueId(const std::string& base, const IdTakenFn& taken);

class EditDocument {
public:
    bool isOpen() const { return open_; }

    // Starts editing a copy of an existing type (library "Edit" button).
    void openCopy(const fixtures::FixtureType& type);
    // Starts a new fixture from a template. `taken` makes its id unique.
    void openNew(FixtureTemplate kind, const IdTakenFn& taken);
    void close();

    fixtures::FixtureType& type() { return type_; }
    const fixtures::FixtureType& type() const { return type_; }

    // Id of the library type this document was opened from or last applied as (empty for a new fixture).
    const std::string& originalId() const { return originalId_; }
    bool isNew() const { return originalId_.empty(); }

    // Call after every change of type(); bumps the revision and, while autoId() is on, updates the id.
    void touch(const IdTakenFn& taken);
    std::uint64_t revision() const { return revision_; }

    bool autoId() const { return autoId_; }
    void setAutoId(bool on, const IdTakenFn& taken);

    // Differs from the baseline (the state at open / the last apply).
    bool modified() const { return revision_ != baselineRevision_; }
    // The current state was written to the library under type().id.
    void markApplied();
    // Back to the baseline.
    void revert();

private:
    void refreshAutoId(const IdTakenFn& taken);

    bool open_ = false;
    fixtures::FixtureType type_;
    fixtures::FixtureType baseline_;
    std::string originalId_;
    bool autoId_ = false;
    std::uint64_t revision_ = 0;
    std::uint64_t baselineRevision_ = 0;
};

}  // namespace dmxviz::ui::fixture_editor
