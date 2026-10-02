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

}  // namespace dmxviz::stage
