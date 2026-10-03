#pragma once
// Reads integer settings from the environment (DMXVIZ_STRESS_SCALE, DMXVIZ_SLOW_TESTS). Wrapped
// because MSVC flags std::getenv as deprecated (C4996) and the build treats warnings as errors.

#include <cstdlib>
#include <exception>
#include <string>

namespace dmxviz::robust {

inline int envInt(const char* name, int fallback) {
#ifdef _WIN32
    char* value = nullptr;
    std::size_t length = 0;
    if (_dupenv_s(&value, &length, name) != 0 || value == nullptr) return fallback;
    const std::string text(value);
    std::free(value);
#else
    const char* raw = std::getenv(name);
    if (raw == nullptr) return fallback;
    const std::string text(raw);
#endif
    try {
        return std::stoi(text);
    } catch (const std::exception&) {
        return fallback;
    }
}

}  // namespace dmxviz::robust
