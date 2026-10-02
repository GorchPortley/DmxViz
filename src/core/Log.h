#pragma once
// Minimal thread-safe logging. Messages go to stderr and to an in-memory ring
// buffer that the UI's log panel reads.
//
//   log::info("dmx", "Art-Net listening on {}:{}", ip, port);

#include <cstddef>
#include <format>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace dmxviz::log {

enum class Level { Debug, Info, Warn, Error };

struct Entry {
    Level level = Level::Info;
    std::string category;
    std::string message;
    double timeSeconds = 0.0;  // seconds since program start
};

void write(Level level, std::string_view category, std::string message);

// Copies up to maxEntries most recent entries (oldest first).
std::vector<Entry> recent(std::size_t maxEntries = 1000);

// Messages below this level are dropped. Default: Info (Debug in debug builds).
void setMinLevel(Level level);
const char* levelName(Level level);

template <typename... Args>
void debug(std::string_view cat, std::format_string<Args...> fmt, Args&&... args) {
    write(Level::Debug, cat, std::format(fmt, std::forward<Args>(args)...));
}
template <typename... Args>
void info(std::string_view cat, std::format_string<Args...> fmt, Args&&... args) {
    write(Level::Info, cat, std::format(fmt, std::forward<Args>(args)...));
}
template <typename... Args>
void warn(std::string_view cat, std::format_string<Args...> fmt, Args&&... args) {
    write(Level::Warn, cat, std::format(fmt, std::forward<Args>(args)...));
}
template <typename... Args>
void error(std::string_view cat, std::format_string<Args...> fmt, Args&&... args) {
    write(Level::Error, cat, std::format(fmt, std::forward<Args>(args)...));
}

}  // namespace dmxviz::log
