#pragma once
// Small physical simulations used by FixtureRuntime. Each one integrates over
// time (update(dt)) toward a target that DMX sets, so fixtures move like real
// ones instead of snapping (FR-FIX-9).

#include "fixtures/Attribute.h"

#include <cstdint>

namespace dmxviz::fixtures {

// Pan or tilt: moves toward a target angle with limited speed and acceleration
// (trapezoidal velocity profile), or spins continuously (PanRotate/TiltRotate).
class AxisMotion {
public:
    void setLimits(float maxSpeed, float acceleration);  // rad/s, rad/s^2
    void setTarget(float angle);                          // position mode
    void setSpin(float speed);                            // continuous rotation, rad/s
    void snapToTarget();
    void update(float dt);

    float angle() const { return angle_; }
    float velocity() const { return velocity_; }
    float target() const { return target_; }
    bool spinning() const { return spinning_; }

private:
    float angle_ = 0.0f;
    float velocity_ = 0.0f;
    float target_ = 0.0f;
    float spinSpeed_ = 0.0f;
    bool spinning_ = false;
    float maxSpeed_ = 3.77f;
    float acceleration_ = 10.0f;
};

// Position of a wheel (colour, gobo, prism...) in slot units: 0 = slot 1 in the
// beam, 1.5 = half way between slots 2 and 3. Cyclic over the slot count.
// Indexing moves along the shortest way round at a fixed slot speed.
class WheelMotion {
public:
    void setSlotCount(int count);
    void setTarget(float position);  // 0-based slot position
    void setSpin(float slotsPerSecond);
    void snapToTarget();
    void update(float dt, float slotsPerSecond);

    float position() const { return position_; }
    // Slot currently (mostly) in the beam, 0-based. Split positions round to the nearest slot.
    int visibleSlot() const;

private:
    float wrap(float p) const;

    int count_ = 1;
    float position_ = 0.0f;
    float target_ = 0.0f;
    float spinSpeed_ = 0.0f;
    bool spinning_ = false;
};

// Rotation of a gobo / prism / animation in its holder: indexed to an angle or
// spinning continuously.
class RotationMotion {
public:
    void setIndex(float angle);  // rad
    void setSpin(float speed);   // rad/s
    void snapToTarget();
    void update(float dt, float indexSpeed);

    float angle() const { return angle_; }

private:
    float angle_ = 0.0f;
    float target_ = 0.0f;
    float spinSpeed_ = 0.0f;
    bool spinning_ = false;
};

// Shutter openness (0 = closed, 1 = open) of one beam over time, for the
// shutter/strobe FunctionKinds. The phase is integrated so frequency changes
// do not cause jumps.
class StrobeGenerator {
public:
    // frequency in Hz (<= 0 uses a default); flashDuration in s (<= 0 = automatic duty).
    void update(float dt, FunctionKind kind, float frequency, float flashDuration);
    float openness() const { return openness_; }

    static constexpr float kDefaultFrequency = 8.0f;

private:
    double phase_ = 0.0;           // 0..1 within the current period
    std::uint32_t period_ = 0;     // periods elapsed, seeds the random variants
    FunctionKind lastKind_ = FunctionKind::ShutterOpen;
    float openness_ = 1.0f;
};

}  // namespace dmxviz::fixtures
