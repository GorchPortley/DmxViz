#pragma once
// Automatic quality: keeps the frame rate up by lowering the cost of the haze pass (the most expensive pass with
// many beams) while frames take longer than RenderSettings::targetFrameMs, and by raising it again when there is
// room. Pure CPU logic without any GPU calls, so it is unit tested (tests/render/test_auto_quality.cpp).
//
// The signal is the wall-clock time between presented frames. With vsync that time never drops below the refresh
// period, so a lower frame time cannot prove there is headroom. Instead the controller probes: after some seconds
// at a reduced level it tries one level up and keeps it only if the frames stay within the target. Failed probes
// back off (5 s, 10 s ... 60 s) so a scene that is simply too heavy does not flicker every few seconds.
//
// Levels (0 = exactly what the user configured):
//   0  configured resolution and steps
//   1  steps x 0.65
//   2  steps x 0.4
//   3  quarter-resolution haze, steps x 0.65
//   4  quarter-resolution haze, steps x 0.4
// A user who already chose quarter resolution only has levels 0 to 2.

#include "render/RenderSettings.h"

namespace dmxviz::render {

class AutoQuality {
public:
    static constexpr int kMaxLevel = 4;

    // Call once per presented frame with the seconds since the previous one. Does nothing (and drops back to
    // level 0) while settings.autoQuality is off.
    void onFrame(float frameSeconds, const RenderSettings& settings);

    // The settings to render with: a copy of the user's settings with the current level applied.
    RenderSettings apply(const RenderSettings& user) const;

    int level() const { return level_; }
    void reset();

private:
    void changeLevel(int delta, float budgetSeconds);

    int level_ = 0;
    float smoothed_ = 0.0f;     // moving average of the frame time (s)
    int overFrames_ = 0;        // consecutive frames with the average above the budget
    int fastFrames_ = 0;        // consecutive frames with lots of headroom
    float sinceChange_ = 0.0f;  // s since the level last changed
    int framesSinceChange_ = 0;
    bool probing_ = false;  // the current level was just raised on trial
    int probeFramesLeft_ = 0;
    float probeInterval_ = 5.0f;  // s at a reduced level before the next probe
};

}  // namespace dmxviz::render
