#include "render/AutoQuality.h"

#include <doctest/doctest.h>

#include <functional>

using namespace dmxviz::render;

namespace {

RenderSettings autoSettings() {
    RenderSettings s;
    s.autoQuality = true;
    s.targetFrameMs = 16.7f;
    return s;
}

// Feeds `frames` frames whose duration depends on the current level (a fake GPU) and returns the seconds passed.
float run(AutoQuality& q, const RenderSettings& s, int frames, const std::function<float(int)>& frameSecondsAtLevel) {
    float elapsed = 0.0f;
    for (int i = 0; i < frames; ++i) {
        const float dt = frameSecondsAtLevel(q.level());
        q.onFrame(dt, s);
        elapsed += dt;
    }
    return elapsed;
}

}  // namespace

TEST_CASE("auto quality: frames within the budget keep level 0") {
    AutoQuality q;
    run(q, autoSettings(), 600, [](int) { return 0.0165f; });
    CHECK(q.level() == 0);
}

TEST_CASE("auto quality: one hitch does not lower the quality") {
    AutoQuality q;
    const RenderSettings s = autoSettings();
    run(q, s, 100, [](int) { return 0.0166f; });
    q.onFrame(0.040f, s);
    run(q, s, 100, [](int) { return 0.0166f; });
    CHECK(q.level() == 0);
}

TEST_CASE("auto quality: stalls are ignored") {
    AutoQuality q;
    const RenderSettings s = autoSettings();
    for (int i = 0; i < 100; ++i) q.onFrame(1.5f, s);  // a window drag or shader compile
    q.onFrame(0.0f, s);
    CHECK(q.level() == 0);
}

TEST_CASE("auto quality: slow frames lower the level step by step up to the maximum") {
    AutoQuality q;
    const RenderSettings s = autoSettings();
    int previous = 0;
    for (int i = 0; i < 400; ++i) {
        q.onFrame(0.030f, s);  // too slow whatever the level
        CHECK(q.level() >= previous);
        CHECK(q.level() - previous <= 1);
        previous = q.level();
    }
    CHECK(q.level() == AutoQuality::kMaxLevel);
}

TEST_CASE("auto quality: the level changes the settings, the user's settings stay as they are") {
    RenderSettings user = autoSettings();
    user.minMarchSteps = 10;
    user.maxMarchSteps = 40;
    AutoQuality q;
    CHECK(q.apply(user).maxMarchSteps == 40);

    for (int i = 0; i < 400; ++i) q.onFrame(0.030f, user);
    const RenderSettings low = q.apply(user);
    CHECK(low.volumetricResolution == VolumetricResolution::Quarter);
    CHECK(low.maxMarchSteps == 16);  // 40 * 0.4
    CHECK(low.minMarchSteps == 4);   // 10 * 0.4
    CHECK(low.maxMarchSteps >= low.minMarchSteps);
    CHECK(user.maxMarchSteps == 40);
    CHECK(user.volumetricResolution == VolumetricResolution::Half);
}

TEST_CASE("auto quality: a user who chose quarter resolution only loses steps") {
    RenderSettings user = autoSettings();
    user.volumetricResolution = VolumetricResolution::Quarter;
    AutoQuality q;
    for (int i = 0; i < 600; ++i) q.onFrame(0.030f, user);
    CHECK(q.level() == 2);
}

TEST_CASE("auto quality: clear headroom brings the quality back") {
    AutoQuality q;
    const RenderSettings s = autoSettings();
    for (int i = 0; i < 200; ++i) q.onFrame(0.030f, s);
    REQUIRE(q.level() > 0);
    // Frames of 8 ms (a fast display without vsync): the controller steps back up to level 0.
    run(q, s, 2000, [](int) { return 0.008f; });
    CHECK(q.level() == 0);
}

TEST_CASE("auto quality: with vsync it probes upwards and keeps what holds") {
    AutoQuality q;
    const RenderSettings s = autoSettings();
    // The fake GPU needs level >= 2 for 60 fps; below that frames take 30 ms. Presented frames never take
    // less than the 16.6 ms refresh period.
    auto period = [](int level) { return level >= 2 ? 0.0166f : 0.030f; };
    run(q, s, 400, period);
    CHECK(q.level() == 2);
    // Long time at level 2: probes level 1, fails (30 ms), returns; the level ends at 2 again.
    const float seconds = run(q, s, 3000, period);
    CHECK(seconds > 30.0f);
    CHECK(q.level() == 2);
}

TEST_CASE("auto quality: a successful probe is kept") {
    AutoQuality q;
    const RenderSettings s = autoSettings();
    float load = 0.030f;  // the heavy scene ends: from now on every level is fast enough
    auto period = [&](int) { return load; };
    run(q, s, 200, period);
    REQUIRE(q.level() > 0);
    load = 0.0166f;
    run(q, s, 3000, period);  // 50 s of vsync-limited frames
    CHECK(q.level() == 0);
}

TEST_CASE("auto quality: switching it off resets the level") {
    AutoQuality q;
    RenderSettings s = autoSettings();
    for (int i = 0; i < 200; ++i) q.onFrame(0.030f, s);
    REQUIRE(q.level() > 0);
    s.autoQuality = false;
    q.onFrame(0.030f, s);
    CHECK(q.level() == 0);
    CHECK(q.apply(s).maxMarchSteps == s.maxMarchSteps);
}
