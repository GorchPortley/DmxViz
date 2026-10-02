#pragma once
// Small helpers that turn event counts into "per second" figures for the UI.

#include "dmx/DmxTypes.h"

#include <chrono>
#include <cstdint>

namespace dmxviz::dmx {

// Packets per second of one stream, measured in windows of about one second.
// Not thread-safe: the owner (e.g. UniverseStore under its mutex) serialises access.
class RateMeter {
public:
    void add(TimePoint now) {
        if (!started_) {
            started_ = true;
            windowStart_ = now;
        }
        const double elapsed = seconds(now - windowStart_);
        if (elapsed >= 1.0) {
            rate_ = static_cast<float>(static_cast<double>(windowCount_) / elapsed);
            windowStart_ = now;
            windowCount_ = 0;
        }
        ++windowCount_;
    }

    // The last full window's rate; decays towards zero when the stream goes quiet.
    float perSecond(TimePoint now) const {
        if (!started_) return 0.0f;
        const double elapsed = seconds(now - windowStart_);
        if (elapsed > 1.0) return static_cast<float>(static_cast<double>(windowCount_) / elapsed);
        return rate_;
    }

private:
    static double seconds(Clock::duration d) { return std::chrono::duration<double>(d).count(); }

    TimePoint windowStart_{};
    std::uint64_t windowCount_ = 0;
    float rate_ = 0.0f;
    bool started_ = false;
};

// Rate of an ever-increasing counter (e.g. an atomic packet counter written by an IO
// thread), sampled whenever someone asks. Not thread-safe by itself.
class CounterRate {
public:
    float update(TimePoint now, std::uint64_t counter) {
        if (!started_) {
            started_ = true;
            lastTime_ = now;
            lastCounter_ = counter;
            return 0.0f;
        }
        const double elapsed = std::chrono::duration<double>(now - lastTime_).count();
        if (elapsed >= 1.0) {
            rate_ = static_cast<float>(static_cast<double>(counter - lastCounter_) / elapsed);
            lastTime_ = now;
            lastCounter_ = counter;
        }
        return rate_;
    }

private:
    TimePoint lastTime_{};
    std::uint64_t lastCounter_ = 0;
    float rate_ = 0.0f;
    bool started_ = false;
};

}  // namespace dmxviz::dmx
