#include "fixtures/FixtureLibrary.h"

#include "core/Limits.h"
#include "core/Log.h"
#include "fixtures/Archive.h"
#include "fixtures/GdtfImporter.h"
#include "fixtures/NativeFormat.h"

#include <nlohmann/json.hpp>

#include <algorithm>
#include <cctype>
#include <format>

namespace dmxviz::fixtures {
namespace {

namespace fs = std::filesystem;

std::string lowerCase(std::string_view s) {
    std::string out(s);
    for (char& c : out) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    return out;
}

bool endsWith(const std::string& text, std::string_view suffix) {
    return text.size() >= suffix.size() && text.compare(text.size() - suffix.size(), suffix.size(), suffix) == 0;
}

enum class FileKind { Native, Gdtf, Json, Other };

FileKind classify(const fs::path& path) {
    const std::string name = lowerCase(path.filename().string());
    if (endsWith(name, kNativeFixtureExtension)) return FileKind::Native;
    if (endsWith(name, ".gdtf")) return FileKind::Gdtf;
    if (endsWith(name, ".json")) return FileKind::Json;
    return FileKind::Other;
}

bool sortsBefore(const FixtureType* a, const FixtureType* b) {
    const std::string ma = lowerCase(a->manufacturer), mb = lowerCase(b->manufacturer);
    if (ma != mb) return ma < mb;
    const std::string na = lowerCase(a->name), nb = lowerCase(b->name);
    if (na != nb) return na < nb;
    return a->id < b->id;
}

}  // namespace

// Reads one file. `notAFixture` is set for JSON files that are neither OFL nor native, which
// directory scans skip without reporting an error.
std::optional<FixtureType> FixtureLibrary::loadAny(const fs::path& file, bool* notAFixture, std::string* error,
                                                   std::vector<std::string>* warnings) const {
    if (notAFixture) *notAFixture = false;
    switch (classify(file)) {
        case FileKind::Native:
            return FixtureSerializer::loadFile(file, error);
        case FileKind::Gdtf:
            return importGdtfFile(file, error, warnings);
        case FileKind::Json: {
            std::string readError;
            auto bytes = readFileBytes(file, &readError);
            if (!bytes) {
                if (error) *error = readError;
                return std::nullopt;
            }
            // parse() without exceptions: a damaged file yields a discarded value. Huge or deeply
            // nested files are treated like damaged ones.
            const std::string_view text(reinterpret_cast<const char*>(bytes->data()), bytes->size());
            const auto json = bytes->size() > limits::kMaxFileBytes || limits::jsonNestedTooDeeply(text)
                                  ? nlohmann::ordered_json(nlohmann::ordered_json::value_t::discarded)
                                  : nlohmann::ordered_json::parse(text, nullptr, false);
            if (json.is_discarded()) {
                if (error) *error = file.filename().string() + ": invalid JSON";
                return std::nullopt;
            }
            if (isOflJson(json)) return importOflFile(file, oflOptions_, error, warnings);
            if (json.is_object() && json.contains("formatVersion")) return FixtureSerializer::loadFile(file, error);
            if (notAFixture) *notAFixture = true;
            if (error)
                *error =
                    file.filename().string() + ": not a fixture file (neither native nor Open Fixture Library JSON)";
            return std::nullopt;
        }
        case FileKind::Other:
            break;
    }
    if (notAFixture) *notAFixture = true;
    if (error) *error = file.filename().string() + ": unsupported file type";
    return std::nullopt;
}

std::string FixtureLibrary::insert(FixtureType type, const fs::path& source, bool warnOnReplace) {
    if (type.id.empty()) type.id = makeFixtureId(type.manufacturer, type.name);
    const std::string id = type.id;
    auto replacement = std::make_unique<FixtureType>(std::move(type));
    if (auto it = index_.find(id); it != index_.end()) {
        Entry& entry = entries_[it->second];
        if (warnOnReplace)
            log::warn("fixtures", "fixture \"{}\" loaded again{}; the earlier one{} is replaced", id,
                      source.empty() ? std::string() : " from " + source.string(),
                      entry.source.empty() ? std::string() : " (" + entry.source.string() + ")");
        retired_.push_back(std::move(entry.type));
        entry.type = std::move(replacement);
        entry.source = source;
    } else {
        index_.emplace(id, entries_.size());
        entries_.push_back({std::move(replacement), source});
    }
    ++revision_;
    return id;
}

std::optional<std::string> FixtureLibrary::importFile(const fs::path& file, std::string* error,
                                                      std::vector<std::string>* warnings) {
    std::string message;
    auto type = loadAny(file, nullptr, &message, warnings);
    if (!type) {
        if (error) *error = message;
        return std::nullopt;
    }
    return insert(std::move(*type), file, true);
}

int FixtureLibrary::loadDirectory(const fs::path& dir) {
    std::error_code ec;
    if (!fs::is_directory(dir, ec)) {
        errors_.push_back({dir, "not a directory"});
        log::warn("fixtures", "fixture folder {} does not exist", dir.string());
        return 0;
    }

    std::vector<fs::path> files;
    fs::recursive_directory_iterator it(dir, fs::directory_options::skip_permission_denied, ec), end;
    for (; !ec && it != end; it.increment(ec)) {
        std::error_code fileEc;
        if (it->is_regular_file(fileEc) && classify(it->path()) != FileKind::Other) files.push_back(it->path());
    }
    if (ec) errors_.push_back({dir, "cannot read folder: " + ec.message()});
    std::sort(files.begin(), files.end());

    int loaded = 0;
    for (const fs::path& file : files) {
        std::string message;
        bool notAFixture = false;
        auto type = loadAny(file, &notAFixture, &message, nullptr);
        if (type) {
            insert(std::move(*type), file, true);
            ++loaded;
        } else if (!notAFixture) {
            log::warn("fixtures", "cannot load {}: {}", file.string(), message);
            errors_.push_back({file, message});
        }
    }
    log::info("fixtures", "loaded {} fixture types from {}", loaded, dir.string());
    return loaded;
}

const FixtureType* FixtureLibrary::find(std::string_view id) const {
    auto it = index_.find(id);
    return it == index_.end() ? nullptr : entries_[it->second].type.get();
}

std::vector<const FixtureType*> FixtureLibrary::all() const {
    std::vector<const FixtureType*> out;
    out.reserve(entries_.size());
    for (const Entry& e : entries_) out.push_back(e.type.get());
    std::sort(out.begin(), out.end(), sortsBefore);
    return out;
}

std::vector<const FixtureType*> FixtureLibrary::search(std::string_view text) const {
    std::vector<std::string> words;
    const std::string lower = lowerCase(text);
    for (std::size_t pos = 0; pos < lower.size();) {
        while (pos < lower.size() && std::isspace(static_cast<unsigned char>(lower[pos]))) ++pos;
        const std::size_t start = pos;
        while (pos < lower.size() && !std::isspace(static_cast<unsigned char>(lower[pos]))) ++pos;
        if (pos > start) words.push_back(lower.substr(start, pos - start));
    }

    std::vector<const FixtureType*> out;
    for (const FixtureType* t : all()) {
        const std::string haystack = lowerCase(t->manufacturer + "\n" + t->name + "\n" + t->shortName + "\n" + t->id);
        const bool matches = std::all_of(words.begin(), words.end(),
                                         [&](const std::string& w) { return haystack.find(w) != std::string::npos; });
        if (matches) out.push_back(t);
    }
    return out;
}

bool FixtureLibrary::addOrReplace(FixtureType type, std::string* error) {
    if (type.name.empty()) {
        if (error) *error = "the fixture needs a name";
        return false;
    }
    // Editing a type that came from a file keeps its source, so the editor knows where to save it.
    const std::string id = type.id.empty() ? makeFixtureId(type.manufacturer, type.name) : type.id;
    fs::path source;
    if (auto it = index_.find(id); it != index_.end()) source = entries_[it->second].source;
    type.id = id;
    insert(std::move(type), source, false);
    return true;
}

bool FixtureLibrary::saveNative(std::string_view id, const fs::path& file, std::string* error) const {
    const FixtureType* type = find(id);
    if (!type) {
        if (error) *error = std::format("no fixture with id \"{}\"", id);
        return false;
    }
    return FixtureSerializer::saveFile(*type, file, error);
}

fs::path FixtureLibrary::sourcePath(std::string_view id) const {
    auto it = index_.find(id);
    return it == index_.end() ? fs::path() : entries_[it->second].source;
}

}  // namespace dmxviz::fixtures
