#pragma once
// Small helpers around miniz (zip) and base64, used by the GDTF importer
// (GDTF files are zip archives), the native format (embedded resources) and
// the tests (which build GDTF archives in memory).

#include <cstdint>
#include <filesystem>
#include <memory>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace dmxviz::fixtures {

// Read-only view of a zip archive held in memory.
class ZipReader {
public:
    ZipReader();
    ~ZipReader();
    ZipReader(const ZipReader&) = delete;
    ZipReader& operator=(const ZipReader&) = delete;

    // Takes a copy of the bytes. Returns false (and sets error) if they are not a zip archive.
    bool open(std::vector<std::uint8_t> bytes, std::string* error = nullptr);
    bool openFile(const std::filesystem::path& path, std::string* error = nullptr);

    // Entry names, '/' separated, files only.
    const std::vector<std::string>& entries() const { return entries_; }
    bool contains(std::string_view name) const;
    // Case-insensitive lookup; returns the stored name.
    std::optional<std::string> findCaseInsensitive(std::string_view name) const;
    std::optional<std::vector<std::uint8_t>> read(std::string_view name) const;

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
    std::vector<std::uint8_t> bytes_;
    std::vector<std::string> entries_;
};

// Builds a zip archive in memory (deflate compressed).
class ZipWriter {
public:
    void add(std::string name, std::span<const std::uint8_t> data);
    void add(std::string name, std::string_view text);
    // Returns the finished archive (empty on failure).
    std::vector<std::uint8_t> finish() const;

private:
    std::vector<std::pair<std::string, std::vector<std::uint8_t>>> files_;
};

std::string base64Encode(std::span<const std::uint8_t> data);
// Ignores whitespace; returns nullopt on invalid characters.
std::optional<std::vector<std::uint8_t>> base64Decode(std::string_view text);

std::optional<std::vector<std::uint8_t>> readFileBytes(const std::filesystem::path& path, std::string* error = nullptr);
bool writeFileBytes(const std::filesystem::path& path, std::span<const std::uint8_t> data, std::string* error = nullptr);

}  // namespace dmxviz::fixtures
