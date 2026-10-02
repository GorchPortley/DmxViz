#include "core/Log.h"

#include <chrono>
#include <cstdio>
#include <deque>
#include <mutex>

namespace dmxviz::log {
namespace {

constexpr std::size_t kCapacity = 4096;

struct State {
    std::mutex mutex;
    std::deque<Entry> entries;
#ifdef NDEBUG
    Level minLevel = Level::Info;
#else
    Level minLevel = Level::Debug;
#endif
    std::chrono::steady_clock::time_point start = std::chrono::steady_clock::now();
};

State& state() {
    static State s;
    return s;
}

}  // namespace

const char* levelName(Level level) {
    switch (level) {
        case Level::Debug: return "debug";
        case Level::Info: return "info";
        case Level::Warn: return "warn";
        case Level::Error: return "error";
    }
    return "?";
}

void setMinLevel(Level level) {
    std::lock_guard lock(state().mutex);
    state().minLevel = level;
}

void write(Level level, std::string_view category, std::string message) {
    State& s = state();
    std::lock_guard lock(s.mutex);
    if (level < s.minLevel) return;
    Entry e;
    e.level = level;
    e.category = std::string(category);
    e.message = std::move(message);
    e.timeSeconds = std::chrono::duration<double>(std::chrono::steady_clock::now() - s.start).count();
    std::fprintf(stderr, "[%9.3f] %-5s %-10s %s\n", e.timeSeconds, levelName(level), e.category.c_str(),
                 e.message.c_str());
    s.entries.push_back(std::move(e));
    if (s.entries.size() > kCapacity) s.entries.pop_front();
}

std::vector<Entry> recent(std::size_t maxEntries) {
    State& s = state();
    std::lock_guard lock(s.mutex);
    const std::size_t n = std::min(maxEntries, s.entries.size());
    return {s.entries.end() - static_cast<std::ptrdiff_t>(n), s.entries.end()};
}

}  // namespace dmxviz::log
