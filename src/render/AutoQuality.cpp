#include "render/AutoQuality.h"

#include <algorithm>
#include <cmath>

namespace dmxviz::render {
namespace {

constexpr float kStallSeconds = 0.25f;    // longer frames (window drag, shader compile) say nothing about load
constexpr float kSmoothing = 0.15f;       // weight of the newest frame in the moving average
constexpr float kOverBudget = 1.06f;      // average above budget x this counts as too slow
constexpr float kHeadroom = 0.80f;        // average below budget x this counts as clearly fast
constexpr int kOverFramesToDegrade = 10;  // sustained, so that one hitch does not lower the quality
constexpr int kSettleFrames = 12;         // frames to let a new level show its effect before judging again
constexpr int kFastFramesToImprove = 90;  // 1.5 s of clear headroom (high refresh rate, vsync off)
constexpr int kProbeFrames = 45;          // how long a trial level has to hold
constexpr float kFirstProbeSeconds = 5.0f;
constexpr float kMaxProbeSeconds = 60.0f;

// Multipliers of every level: for the step counts, and for the cost budget (which is what actually lowers the
// cost when many beams are on screen and the step counts sit at their floor).
constexpr float kStepScale[AutoQuality::kMaxLevel + 1] = {1.0f, 0.65f, 0.4f, 0.65f, 0.4f};
constexpr float kBudgetScale[AutoQuality::kMaxLevel + 1] = {1.0f, 0.5f, 0.25f, 0.15f, 0.08f};
constexpr int kFirstQuarterLevel = 3;

int maxLevelFor(const RenderSettings& settings) {
    return settings.volumetricResolution == VolumetricResolution::Quarter ? kFirstQuarterLevel - 1
                                                                          : AutoQuality::kMaxLevel;
}

}  // namespace

void AutoQuality::reset() {
    *this = AutoQuality{};
}

void AutoQuality::changeLevel(int delta, float budgetSeconds) {
    level_ += delta;
    sinceChange_ = 0.0f;
    framesSinceChange_ = 0;
    overFrames_ = 0;
    fastFrames_ = 0;
    // Judge the new level on its own frames, not on the history of the old one.
    smoothed_ = budgetSeconds;
}

void AutoQuality::onFrame(float frameSeconds, const RenderSettings& settings) {
    if (!settings.autoQuality) {
        if (level_ != 0) reset();
        return;
    }
    if (!(frameSeconds > 0.0f) || frameSeconds > kStallSeconds) return;

    const float budget = std::max(settings.targetFrameMs, 1.0f) * 0.001f;
    if (smoothed_ <= 0.0f) smoothed_ = budget;
    smoothed_ += (frameSeconds - smoothed_) * kSmoothing;
    sinceChange_ += frameSeconds;
    ++framesSinceChange_;
    level_ = std::min(level_, maxLevelFor(settings));  // the user may have switched to quarter resolution

    const bool over = smoothed_ > budget * kOverBudget;
    overFrames_ = over ? overFrames_ + 1 : 0;
    fastFrames_ = smoothed_ < budget * kHeadroom ? fastFrames_ + 1 : 0;

    if (probing_) {
        if (overFrames_ >= kOverFramesToDegrade) {
            // The trial failed: go back and wait longer before trying again.
            changeLevel(+1, budget);
            probing_ = false;
            probeInterval_ = std::min(probeInterval_ * 2.0f, kMaxProbeSeconds);
        } else if (--probeFramesLeft_ <= 0) {
            probing_ = false;  // it held: keep it
            probeInterval_ = kFirstProbeSeconds;
        }
        return;
    }

    if (overFrames_ >= kOverFramesToDegrade && framesSinceChange_ >= kSettleFrames) {
        if (level_ < maxLevelFor(settings)) changeLevel(+1, budget);
    } else if (level_ > 0) {
        if (fastFrames_ >= kFastFramesToImprove) {
            changeLevel(-1, budget);
        } else if (sinceChange_ >= probeInterval_) {
            changeLevel(-1, budget);
            probing_ = true;
            probeFramesLeft_ = kProbeFrames;
        }
    }
}

RenderSettings AutoQuality::apply(const RenderSettings& user) const {
    RenderSettings s = user;
    if (!user.autoQuality || level_ <= 0) return s;
    const int level = std::min(level_, kMaxLevel);
    const float scale = kStepScale[level];
    s.minMarchSteps = std::max(2, static_cast<int>(std::lround(static_cast<float>(user.minMarchSteps) * scale)));
    s.maxMarchSteps =
        std::max(s.minMarchSteps, static_cast<int>(std::lround(static_cast<float>(user.maxMarchSteps) * scale)));
    s.volumetricBudget = user.volumetricBudget * kBudgetScale[level];
    if (level >= kFirstQuarterLevel) s.volumetricResolution = VolumetricResolution::Quarter;
    return s;
}

}  // namespace dmxviz::render
