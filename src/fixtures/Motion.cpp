#include "fixtures/Motion.h"

#include "core/Math.h"

#include <cmath>

namespace dmxviz::fixtures {
namespace {

constexpr float kTwoPi = 2.0f * kPi;

float moveToward(float value, float target, float maxStep) {
    if (value < target) return std::min(value + maxStep, target);
    return std::max(value - maxStep, target);
}

float signOf(float v) { return v < 0.0f ? -1.0f : 1.0f; }

// Integer hash -> [0, 1). Deterministic so tests and recordings are repeatable.
float hash01(std::uint32_t x) {
    x ^= x >> 16;
    x *= 0x7feb352dU;
    x ^= x >> 15;
    x *= 0x846ca68bU;
    x ^= x >> 16;
    return static_cast<float>(x & 0xFFFFFFu) / static_cast<float>(0x1000000);
}

}  // namespace

// ------------------------------------------------------------------ AxisMotion

void AxisMotion::setLimits(float maxSpeed, float acceleration) {
    maxSpeed_ = std::max(maxSpeed, 1e-3f);
    acceleration_ = std::max(acceleration, 1e-3f);
}

void AxisMotion::setTarget(float angle) {
    if (spinning_) {
        // Leaving continuous rotation: continue from the equivalent angle closest
        // to the target so the head does not unwind several turns.
        angle_ = angle + std::remainder(angle_ - angle, kTwoPi);
        spinning_ = false;
    }
    target_ = angle;
}

void AxisMotion::setSpin(float speed) {
    spinning_ = true;
    spinSpeed_ = speed;
}

void AxisMotion::snapToTarget() {
    velocity_ = spinning_ ? spinSpeed_ : 0.0f;
    if (!spinning_) angle_ = target_;
}

void AxisMotion::update(float dt) {
    if (dt <= 0.0f) return;
    if (spinning_) {
        velocity_ = moveToward(velocity_, spinSpeed_, acceleration_ * dt);
        angle_ = std::remainder(angle_ + velocity_ * dt, kTwoPi);
        return;
    }
    const float error = target_ - angle_;
    if (std::abs(error) < 1e-6f && std::abs(velocity_) <= acceleration_ * dt) {
        angle_ = target_;
        velocity_ = 0.0f;
        return;
    }
    // Fastest speed from which we can still brake to a stop at the target.
    const float brakingSpeed = std::sqrt(2.0f * acceleration_ * std::abs(error));
    const float desired = signOf(error) * std::min(maxSpeed_, brakingSpeed);
    velocity_ = moveToward(velocity_, desired, acceleration_ * dt);
    const float next = angle_ + velocity_ * dt;
    if ((target_ - next) * error <= 0.0f && std::abs(velocity_) <= 2.0f * acceleration_ * dt + 1e-4f) {
        // Arrived (the last step would cross the target at crawl speed).
        angle_ = target_;
        velocity_ = 0.0f;
    } else {
        angle_ = next;
    }
}

// ----------------------------------------------------------------- WheelMotion

void WheelMotion::setSlotCount(int count) {
    count_ = std::max(1, count);
    position_ = wrap(position_);
    target_ = wrap(target_);
}

float WheelMotion::wrap(float p) const {
    const float n = static_cast<float>(count_);
    p = std::fmod(p, n);
    return p < 0.0f ? p + n : p;
}

void WheelMotion::setTarget(float position) {
    spinning_ = false;
    target_ = wrap(position);
}

void WheelMotion::setSpin(float slotsPerSecond) {
    spinning_ = true;
    spinSpeed_ = slotsPerSecond;
}

void WheelMotion::snapToTarget() {
    if (!spinning_) position_ = target_;
}

void WheelMotion::update(float dt, float slotsPerSecond) {
    if (dt <= 0.0f) return;
    if (spinning_) {
        position_ = wrap(position_ + spinSpeed_ * dt);
        return;
    }
    // Shortest way round the wheel.
    const float n = static_cast<float>(count_);
    float delta = std::remainder(target_ - position_, n);
    const float step = slotsPerSecond * dt;
    if (std::abs(delta) <= step) {
        position_ = target_;
    } else {
        position_ = wrap(position_ + signOf(delta) * step);
    }
}

int WheelMotion::visibleSlot() const {
    int slot = static_cast<int>(std::floor(position_ + 0.5f));
    return slot >= count_ ? 0 : slot;
}

// -------------------------------------------------------------- RotationMotion

void RotationMotion::setIndex(float angle) {
    if (spinning_) {
        angle_ = angle + std::remainder(angle_ - angle, kTwoPi);
        spinning_ = false;
    }
    target_ = angle;
}

void RotationMotion::setSpin(float speed) {
    spinning_ = true;
    spinSpeed_ = speed;
}

void RotationMotion::snapToTarget() {
    if (!spinning_) angle_ = target_;
}

void RotationMotion::update(float dt, float indexSpeed) {
    if (dt <= 0.0f) return;
    if (spinning_) {
        angle_ = std::remainder(angle_ + spinSpeed_ * dt, kTwoPi);
        return;
    }
    angle_ = moveToward(angle_, target_, indexSpeed * dt);
}

// ------------------------------------------------------------- StrobeGenerator

void StrobeGenerator::update(float dt, FunctionKind kind, float frequency, float flashDuration) {
    if (!isStrobeKind(kind)) {
        openness_ = kind == FunctionKind::ShutterClosed ? 0.0f : 1.0f;
        lastKind_ = kind;
        return;
    }
    // A strobe that just started flashes immediately.
    if (!isStrobeKind(lastKind_)) {
        phase_ = 0.0;
        period_ = 0;
    } else {
        const float f = frequency > 0.0f ? frequency : kDefaultFrequency;
        phase_ += static_cast<double>(dt) * f;
        if (phase_ >= 1.0) {
            const double whole = std::floor(phase_);
            period_ += static_cast<std::uint32_t>(whole);
            phase_ -= whole;
        }
    }
    lastKind_ = kind;

    const float f = frequency > 0.0f ? frequency : kDefaultFrequency;
    const float p = static_cast<float>(phase_);
    // Flash length: given by StrobeDuration, else short flashes typical of LED/xenon strobes.
    const float duty = std::clamp(flashDuration > 0.0f ? flashDuration * f : std::min(0.5f, 0.06f * f), 0.02f, 0.95f);

    switch (kind) {
        case FunctionKind::Strobe: openness_ = p < duty ? 1.0f : 0.0f; break;
        case FunctionKind::StrobePulse: openness_ = 0.5f - 0.5f * std::cos(2.0f * kPi * p); break;
        case FunctionKind::StrobePulseOpen: openness_ = 1.0f - p; break;  // snap open, fade out
        case FunctionKind::StrobePulseClose: openness_ = p; break;        // fade in, snap closed
        case FunctionKind::StrobeRampUp: openness_ = p; break;
        case FunctionKind::StrobeRampDown: openness_ = 1.0f - p; break;
        case FunctionKind::StrobeRampUpDown: openness_ = 1.0f - std::abs(2.0f * p - 1.0f); break;
        case FunctionKind::StrobeRandom: {
            // One flash per period at a random moment: irregular timing, same average rate.
            const float start = hash01(period_) * (1.0f - duty);
            openness_ = (p >= start && p < start + duty) ? 1.0f : 0.0f;
            break;
        }
        case FunctionKind::StrobeLightning: {
            // Bursts: the period is split into 8 sub-steps, a few of the first half flash.
            const int step = static_cast<int>(p * 8.0f);
            const bool burst = hash01(period_ * 977u + 13u) > 0.3f;
            openness_ = (burst && step < 4 && hash01(period_ * 8u + static_cast<std::uint32_t>(step)) > 0.45f) ? 1.0f : 0.0f;
            break;
        }
        default: openness_ = 1.0f; break;
    }
}

}  // namespace dmxviz::fixtures
