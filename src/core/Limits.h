#pragma once
// Hard limits for untrusted input (files opened by the user, archives, network settings).
//
// A damaged or malicious file must produce a clear error instead of exhausting memory or time.
// The numbers are far above anything a real stage, fixture or model needs; they exist to stop
// absurd values early, before anything is allocated.

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <string_view>
#include <system_error>

namespace dmxviz::limits {

// Any single file read into memory (model, image, project, fixture, settings).
inline constexpr std::uint64_t kMaxFileBytes = 256ull << 20;
// A GDTF/zip archive file, and one entry of it once decompressed (zip bombs).
inline constexpr std::uint64_t kMaxArchiveBytes = 1ull << 30;
inline constexpr std::uint64_t kMaxArchiveEntryBytes = 256ull << 20;
inline constexpr std::size_t kMaxArchiveEntries = 100000;

// Images: each side and the total pixel count (a 16k x 16k RGBA image alone is 1 GiB).
inline constexpr int kMaxImageDimension = 16384;
inline constexpr std::uint64_t kMaxImagePixels = 100'000'000;

// Models: whole model, summed over all parts and node instances.
inline constexpr std::size_t kMaxTriangles = 50'000'000;
inline constexpr std::size_t kMaxVertices = 50'000'000;
inline constexpr std::size_t kMaxModelNodeVisits = 1'000'000;  // glTF node instances (a node can be listed twice)
inline constexpr std::size_t kMaxModelParts = 100'000;         // meshes x materials x instances

// Project files.
inline constexpr std::size_t kMaxSceneNodes = 1'000'000;
inline constexpr int kMaxNodeDepth = 128;  // nesting of scene nodes and fixture geometry trees

// Deepest nesting of arrays/objects accepted in any JSON text. nlohmann::json copies and
// serialises recursively, so unbounded nesting would overflow the stack.
inline constexpr int kMaxJsonDepth = 512;

// True when the file exists and is bigger than `maxBytes` (checked before reading it into memory).
inline bool fileTooLarge(const std::filesystem::path& path, std::uint64_t maxBytes = kMaxFileBytes) {
    std::error_code ec;
    const std::uintmax_t size = std::filesystem::file_size(path, ec);
    return !ec && size > maxBytes;
}

// True when `text` nests arrays/objects deeper than `maxDepth` (brackets inside strings are
// ignored). A cheap pre-scan, so deeply nested input is rejected before it is parsed or copied.
inline bool jsonNestedTooDeeply(std::string_view text, int maxDepth = kMaxJsonDepth) {
    int depth = 0;
    bool inString = false;
    for (std::size_t i = 0; i < text.size(); ++i) {
        const char c = text[i];
        if (inString) {
            if (c == '\\') ++i;  // skip the escaped character
            else if (c == '"') inString = false;
        } else if (c == '"') {
            inString = true;
        } else if (c == '[' || c == '{') {
            if (++depth > maxDepth) return true;
        } else if ((c == ']' || c == '}') && depth > 0) {
            --depth;
        }
    }
    return false;
}

}  // namespace dmxviz::limits
