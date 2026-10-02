#pragma once
// UTF-8 <-> std::filesystem::path conversions. Paths are kept as UTF-8
// std::string in the scene and in project files; on Windows the native path
// type is UTF-16, so conversions must go through char8_t explicitly.

#include <filesystem>
#include <string>
#include <string_view>

namespace dmxviz::stage {

inline std::filesystem::path pathFromUtf8(std::string_view utf8) {
    return std::filesystem::path(std::u8string(reinterpret_cast<const char8_t*>(utf8.data()), utf8.size()));
}

// Generic (forward slash) form, UTF-8 encoded.
inline std::string pathToUtf8(const std::filesystem::path& p) {
    const std::u8string s = p.generic_u8string();
    return std::string(reinterpret_cast<const char*>(s.data()), s.size());
}

// For files stored in a project: `path` (UTF-8, absolute) relative to `dir`.
// Relative paths, an empty dir, or a path on another drive are returned as is.
inline std::string relativePathUtf8(const std::string& path, const std::filesystem::path& dir) {
    if (path.empty() || dir.empty()) return path;
    const std::filesystem::path p = pathFromUtf8(path);
    if (!p.is_absolute()) return pathToUtf8(p);
    const std::filesystem::path rel = p.lexically_normal().lexically_relative(dir.lexically_normal());
    return rel.empty() ? pathToUtf8(p) : pathToUtf8(rel);
}

// The inverse: a path read from a project file made absolute against `dir`.
inline std::string absolutePathUtf8(const std::string& path, const std::filesystem::path& dir) {
    if (path.empty() || dir.empty()) return path;
    const std::filesystem::path p = pathFromUtf8(path);
    return p.is_absolute() ? pathToUtf8(p) : pathToUtf8((dir / p).lexically_normal());
}

}  // namespace dmxviz::stage
