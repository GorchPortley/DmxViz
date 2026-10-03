#include "ui/fixture_editor/EditDocument.h"

#include <format>

namespace dmxviz::ui::fixture_editor {

std::string makeUniqueId(const std::string& base, const IdTakenFn& taken) {
    if (!taken || !taken(base)) return base;
    for (int n = 2;; ++n) {
        std::string candidate = std::format("{}-{}", base, n);
        if (!taken(candidate)) return candidate;
    }
}

void EditDocument::openCopy(const fixtures::FixtureType& type) {
    open_ = true;
    type_ = type;
    baseline_ = type;
    originalId_ = type.id;
    autoId_ = false;
    ++revision_;
    baselineRevision_ = revision_;
}

void EditDocument::openNew(FixtureTemplate kind, const IdTakenFn& taken) {
    open_ = true;
    type_ = makeTemplateFixture(kind);
    originalId_.clear();
    autoId_ = true;
    refreshAutoId(taken);
    baseline_ = type_;
    ++revision_;
    baselineRevision_ = revision_;
}

void EditDocument::close() {
    open_ = false;
    type_ = {};
    baseline_ = {};
    originalId_.clear();
    ++revision_;
    baselineRevision_ = revision_;
}

void EditDocument::touch(const IdTakenFn& taken) {
    if (autoId_) refreshAutoId(taken);
    ++revision_;
}

void EditDocument::setAutoId(bool on, const IdTakenFn& taken) {
    autoId_ = on;
    if (on) refreshAutoId(taken);
    ++revision_;
}

void EditDocument::markApplied() {
    baseline_ = type_;
    originalId_ = type_.id;
    baselineRevision_ = revision_;
}

void EditDocument::revert() {
    type_ = baseline_;
    ++revision_;
    baselineRevision_ = revision_;
}

void EditDocument::refreshAutoId(const IdTakenFn& taken) {
    const std::string base = fixtures::makeFixtureId(type_.manufacturer, type_.name);
    // The id this document already owns is not "taken" by someone else.
    type_.id = makeUniqueId(base, [&](std::string_view id) { return taken && id != originalId_ && taken(id); });
}

}  // namespace dmxviz::ui::fixture_editor
