#pragma once
// The set of fixture types DmxViz knows: the bundled data/fixtures folder plus user
// folders, imported files and types made in the fixture editor.
//
//   FixtureLibrary lib;
//   lib.loadDirectory("data/fixtures");             // native, OFL and GDTF files, recursively
//   const FixtureType* t = lib.find("clay-paky/sharpy");
//   for (const FixtureType* f : lib.search("sharpy")) ...
//
// Files are recognised by name and content:
//   *.dmxviz-fixture.json                 native format (docs/FIXTURE_FORMAT.md)
//   *.json with a "$schema" naming the    Open Fixture Library
//       open-fixture-library
//   *.json with "formatVersion"           native format under another file name
//   *.gdtf                                GDTF archive
//
// Types are stored behind unique_ptr, so a `const FixtureType*` stays valid while other
// types are added. Replacing a type (same id) installs the new one and keeps the old
// object alive until the library is destroyed, so runtimes still built on it stay safe.
// Not thread-safe: use it from the UI / main thread, hand FixtureRuntimes a pointer.

#include "fixtures/FixtureType.h"
#include "fixtures/OflImporter.h"

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <map>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace dmxviz::fixtures {

class FixtureLibrary {
public:
    // A file that could not be loaded. Loading goes on after an error.
    struct LoadError {
        std::filesystem::path path;
        std::string message;
    };

    // Loads every fixture file below `dir` (recursively, in path order, so "last loaded wins"
    // is deterministic). Returns the number of types loaded. Problems go to errors();
    // JSON files that are not fixtures are skipped silently.
    int loadDirectory(const std::filesystem::path& dir);

    // Loads one file of any supported kind. Returns the id of the type it added (or replaced).
    // On failure returns nullopt and describes the problem in `error`; the library is unchanged.
    std::optional<std::string> importFile(const std::filesystem::path& file, std::string* error = nullptr,
                                          std::vector<std::string>* warnings = nullptr);

    const FixtureType* find(std::string_view id) const;
    // All types sorted by manufacturer, then name (case-insensitive).
    std::vector<const FixtureType*> all() const;
    // Types matching every word of `text` (case-insensitive) in manufacturer, name, short name
    // or id, in the order of all(). Empty text returns everything.
    std::vector<const FixtureType*> search(std::string_view text) const;
    std::size_t size() const { return entries_.size(); }
    // Increases whenever a type is added or replaced, so users of the library can resync.
    std::uint64_t revision() const { return revision_; }

    // Adds a type made in memory (fixture editor) or replaces the one with the same id.
    // An empty id is generated from manufacturer and name. Returns false (and sets `error`)
    // when the type has no name.
    bool addOrReplace(FixtureType type, std::string* error = nullptr);

    // Writes a type as a native fixture file with embedded resources.
    bool saveNative(std::string_view id, const std::filesystem::path& file, std::string* error) const;

    // File a type was loaded from (empty for types made in memory).
    std::filesystem::path sourcePath(std::string_view id) const;

    const std::vector<LoadError>& errors() const { return errors_; }
    void clearErrors() { errors_.clear(); }

    // Folder with bundled gobo images that OFL imports use for gobos without an image.
    void setStandInGoboDir(const std::filesystem::path& dir) { oflOptions_.standInGoboDir = dir; }

private:
    struct Entry {
        std::unique_ptr<FixtureType> type;
        std::filesystem::path source;
    };

    std::optional<FixtureType> loadAny(const std::filesystem::path& file, bool* notAFixture, std::string* error,
                                       std::vector<std::string>* warnings) const;
    std::string insert(FixtureType type, const std::filesystem::path& source, bool warnOnReplace);

    std::vector<Entry> entries_;                             // insertion order
    std::map<std::string, std::size_t, std::less<>> index_;  // id -> entries_ index
    std::vector<std::unique_ptr<FixtureType>> retired_;      // replaced types, kept alive
    std::vector<LoadError> errors_;
    std::uint64_t revision_ = 0;
    OflImportOptions oflOptions_;
};

}  // namespace dmxviz::fixtures
