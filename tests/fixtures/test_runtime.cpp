#include "FixtureTestUtil.h"

#include "assets/AssetLibrary.h"
#include "fixtures/AttributeEncoder.h"
#include "fixtures/FixtureAssets.h"
#include "fixtures/FixtureRuntime.h"

#include <doctest/doctest.h>

#include <memory>

using namespace dmxviz;
using namespace dmxviz::fixtures;
using doctest::Approx;

namespace {

struct Rig {
    std::shared_ptr<const FixtureType> type;
    FixtureRuntime runtime;
    AttributeEncoder encoder;
    std::vector<std::uint8_t> dmx;

    explicit Rig(FixtureType t, const FixtureAssets* assets = nullptr)
        : type(std::make_shared<const FixtureType>(std::move(t))),
          runtime(type, type->modes[0].name, assets),
          encoder(*type, type->modes[0]),
          dmx(static_cast<std::size_t>(type->modes[0].footprint), 0) {
        encoder.writeDefaults(dmx);
    }
    void send() { runtime.setDmx(dmx); }
    void run(float seconds, float dt = 1.0f / 120.0f) {
        send();
        for (float t = 0; t < seconds; t += dt) runtime.update(dt, t);
    }
    BeamState beam(int index = 0) const {
        std::vector<MeshInstance> meshes;
        std::vector<BeamState> beams;
        runtime.emit(glm::mat4(1.0f), 7, meshes, beams);
        return beams.at(static_cast<std::size_t>(index));
    }
    std::uint8_t& ch(int offset) { return dmx[static_cast<std::size_t>(offset - 1)]; }
};

void checkVec(const glm::vec3& a, const glm::vec3& b, float eps = 1e-3f) {
    CHECK(a.x == Approx(b.x).epsilon(eps));
    CHECK(a.y == Approx(b.y).epsilon(eps));
    CHECK(a.z == Approx(b.z).epsilon(eps));
}

}  // namespace

TEST_CASE("runtime decodes 16-bit pan and tilt to angles") {
    Rig rig(test::makeTestSpot());
    // Defaults (32768) are the home position: pan = tilt = 0 (within one 16-bit step).
    CHECK(rig.runtime.panAngle() == Approx(0.0f).epsilon(1e-3));
    rig.ch(1) = 0xFF;
    rig.ch(2) = 0xFF;
    rig.ch(3) = 0x00;
    rig.ch(4) = 0x00;
    rig.send();
    rig.runtime.snapToTargets();
    CHECK(radToDeg(rig.runtime.panAngle()) == Approx(270.0f));
    CHECK(radToDeg(rig.runtime.tiltAngle()) == Approx(-135.0f));

    // The fine byte matters: 0x4000 = 16384 -> -270 + 540 * 16384 / 65535.
    rig.ch(1) = 0x40;
    rig.ch(2) = 0x00;
    rig.send();
    rig.runtime.snapToTargets();
    CHECK(radToDeg(rig.runtime.panAngle()) == Approx(-270.0f + 540.0f * 16384.0f / 65535.0f).epsilon(1e-4));

    // Encoder round trip: ask for 90 deg, get 90 deg within one 16-bit step.
    CHECK(rig.encoder.setPhysical(rig.dmx, Attribute::Pan, degToRad(90.0f)));
    rig.send();
    rig.runtime.snapToTargets();
    CHECK(std::abs(radToDeg(rig.runtime.panAngle()) - 90.0f) < 540.0f / 65535.0f);
}

TEST_CASE("pan/tilt movement respects max speed and acceleration") {
    FixtureType t = test::makeTestSpot();
    t.physical.movement.panMaxSpeed = degToRad(216.0f);
    t.physical.movement.panAcceleration = degToRad(600.0f);
    Rig rig(std::move(t));
    rig.encoder.setPhysical(rig.dmx, Attribute::Pan, 0.0f);
    rig.send();
    rig.runtime.snapToTargets();

    rig.encoder.setPhysical(rig.dmx, Attribute::Pan, degToRad(180.0f));
    rig.send();
    const float dt = 1.0f / 240.0f;
    float peakSpeed = 0.0f, previous = rig.runtime.panAngle(), arrival = -1.0f;
    for (int i = 1; i <= 240 * 3; ++i) {
        rig.runtime.update(dt, i * dt);
        const float a = rig.runtime.panAngle();
        peakSpeed = std::max(peakSpeed, std::abs(a - previous) / dt);
        previous = a;
        if (arrival < 0 && std::abs(radToDeg(a) - 180.0f) < 0.05f) arrival = static_cast<float>(i) * dt;
    }
    // Trapezoid: accelerate to 216 deg/s, cruise, brake. Ideal time = d/v + v/a = 1.193 s.
    CHECK(radToDeg(peakSpeed) == Approx(216.0f).epsilon(0.01));
    CHECK(arrival == Approx(180.0f / 216.0f + 216.0f / 600.0f).epsilon(0.03));
    CHECK(radToDeg(rig.runtime.panAngle()) == Approx(180.0f).epsilon(1e-4));
}

TEST_CASE("RGBW additive mixing without a dimmer channel") {
    Rig rig(test::makeTestRgbwPar());
    rig.run(0.05f);
    CHECK(rig.beam().intensity == Approx(0.0f));  // all emitters off = dark, no dimmer needed

    rig.ch(1) = 255;  // red only
    rig.run(0.05f);
    checkVec(rig.beam().color, {1, 0, 0});
    CHECK(rig.beam().intensity == Approx(0.5f).epsilon(1e-3));  // RGB+W at full is the reference (2)

    rig.ch(2) = 255;
    rig.ch(3) = 255;
    rig.ch(4) = 255;  // everything at full: white at full output
    rig.run(0.05f);
    checkVec(rig.beam().color, {1, 1, 1});
    CHECK(rig.beam().intensity == Approx(1.0f));

    rig.ch(1) = rig.ch(2) = rig.ch(3) = 0;  // white LEDs only
    rig.run(0.05f);
    checkVec(rig.beam().color, {1, 1, 1});
    CHECK(rig.beam().intensity == Approx(0.5f).epsilon(1e-3));

    // Colour picker through the encoder: orange-ish with white extraction.
    CHECK(rig.encoder.setColor(rig.dmx, {1.0f, 0.5f, 0.0f}));
    CHECK(rig.ch(4) == 0);
    CHECK(rig.ch(1) == 255);
    CHECK(rig.ch(2) == 128);
    rig.run(0.05f);
    checkVec(rig.beam().color, {1.0f, 128.0f / 255.0f, 0.0f}, 2e-3f);
}

TEST_CASE("CMY and colour wheel filter a white source") {
    Rig rig(test::makeTestSpot());
    rig.ch(5) = 255;  // dimmer full, shutter defaults to open
    rig.run(0.05f);
    checkVec(rig.beam().color, {1, 1, 1});
    CHECK(rig.beam().intensity == Approx(1.0f));

    rig.ch(15) = 255;  // cyan flag removes red
    rig.run(0.05f);
    checkVec(rig.beam().color, {0, 1, 1});
    rig.ch(16) = 255;  // + magenta = blue
    rig.run(0.05f);
    checkVec(rig.beam().color, {0, 0, 1});
    CHECK(rig.beam().intensity == Approx(1.0f));
    rig.ch(15) = rig.ch(16) = 0;

    // Colour wheel: slot 3 (green). The wheel does not snap: at 10 slots/s it
    // passes the red slot on the way.
    CHECK(rig.encoder.setWheelSlot(rig.dmx, Attribute::Color1, 3.0f));
    CHECK(rig.ch(7) == 24);
    rig.run(1.0f / 120.0f);
    checkVec(rig.beam().color, {1, 1, 1});  // still near open after one frame
    rig.run(0.09f);
    checkVec(rig.beam().color, {1, 0, 0});
    rig.run(1.0f);
    checkVec(rig.beam().color, {0, 1, 0});

    // Continuous wheel spin cycles through the slots.
    rig.encoder.setNormalized(rig.dmx, Attribute::Color1WheelSpin, 1.0f);
    rig.send();
    int changes = 0;
    glm::vec3 last = rig.beam().color;
    for (int i = 0; i < 120; ++i) {
        rig.runtime.update(1.0f / 120.0f, 0.0);
        if (glm::length(rig.beam().color - last) > 0.1f) ++changes;
        last = rig.beam().color;
    }
    CHECK(changes >= 4);  // one turn per second over 4 slots
}

TEST_CASE("strobe frequency and duty") {
    Rig rig(test::makeTestSpot());
    rig.ch(5) = 255;
    CHECK(rig.encoder.setShutter(rig.dmx, FunctionKind::Strobe, 5.0f));
    rig.send();
    const float dt = 1.0f / 1000.0f;
    int risingEdges = 0, openSamples = 0;
    bool wasOpen = false;
    const int samples = 2000;
    for (int i = 0; i < samples; ++i) {
        rig.runtime.update(dt, i * dt);
        const bool open = rig.beam().intensity > 0.5f;
        if (open && !wasOpen) ++risingEdges;
        if (open) ++openSamples;
        wasOpen = open;
    }
    // 5 Hz for 2 s = 10 flashes; the actual DMX value quantises the frequency slightly.
    CHECK(risingEdges >= 9);
    CHECK(risingEdges <= 11);
    // Automatic flash length: min(0.5, 0.06 s * f) = 30 % duty.
    CHECK(static_cast<float>(openSamples) / samples == Approx(0.3f).epsilon(0.1));

    // Closed shutter is dark, open is lit.
    rig.encoder.setShutter(rig.dmx, FunctionKind::ShutterClosed);
    rig.run(0.02f);
    CHECK(rig.beam().intensity == Approx(0.0f));
    rig.encoder.setShutter(rig.dmx, FunctionKind::ShutterOpen);
    rig.run(0.02f);
    CHECK(rig.beam().intensity == Approx(1.0f));
}

TEST_CASE("gobo, prism and beam optics") {
    assets::AssetLibrary library;
    FixtureType type = test::makeTestSpot();
    const FixtureAssets assets = FixtureAssets::realize(type, library);
    REQUIRE(assets.image("dots") != kInvalidImage);
    Rig rig(std::move(type), &assets);
    rig.ch(5) = 255;

    rig.encoder.setWheelSlot(rig.dmx, Attribute::Gobo1, 2.0f);
    rig.encoder.setPhysical(rig.dmx, Attribute::Gobo1Pos, degToRad(90.0f));
    rig.encoder.setWheelSlot(rig.dmx, Attribute::Prism1, 2.0f);
    rig.encoder.setPhysical(rig.dmx, Attribute::Prism1PosRotate, degToRad(180.0f));
    rig.encoder.setPhysical(rig.dmx, Attribute::Zoom, degToRad(30.0f));
    rig.run(1.0f);
    BeamState b = rig.beam();
    CHECK(b.gobos[0].image == assets.image("dots"));
    CHECK(radToDeg(b.gobos[0].rotation) == Approx(90.0f).epsilon(0.01));
    CHECK(b.prism.facetCount == 3);
    CHECK(glm::length(b.prism.facets[0]) == Approx(degToRad(5.0f)));
    CHECK(radToDeg(b.beamAngle) == Approx(30.0f).epsilon(0.01));
    CHECK(radToDeg(b.fieldAngle) == Approx(36.0f).epsilon(0.01));  // keeps the 20:24 ratio

    // Prism rotates at ~180 deg/s.
    const float before = b.prism.rotation;
    rig.runtime.update(0.1f, 1.1);
    const float turned = std::remainder(rig.beam().prism.rotation - before, 2.0f * kPi);
    CHECK(radToDeg(turned) == Approx(18.0f).epsilon(0.05));

    // Iris closes the beam and blocks light; frost widens and softens.
    rig.encoder.setPhysical(rig.dmx, Attribute::Iris, 0.5f);
    rig.run(0.02f);
    b = rig.beam();
    CHECK(radToDeg(b.beamAngle) == Approx(15.0f).epsilon(0.02));
    CHECK(b.luminousFlux == Approx(0.25f * 5000.0f).epsilon(0.02));
    rig.encoder.setPhysical(rig.dmx, Attribute::Iris, 1.0f);
    rig.encoder.setPhysical(rig.dmx, Attribute::Frost1, 1.0f);
    rig.run(0.02f);
    b = rig.beam();
    CHECK(b.frost == Approx(1.0f));
    CHECK(b.beamAngle > degToRad(30.0f));

    // Mesh instances: body parts + one emissive lens carrying the beam colour.
    std::vector<MeshInstance> meshes;
    std::vector<BeamState> beams;
    rig.runtime.emit(glm::mat4(1.0f), 42, meshes, beams);
    CHECK(meshes.size() == 4);  // base, yoke, head, lens
    int lit = 0;
    for (const MeshInstance& m : meshes) {
        CHECK(m.pickId == 42);
        if (glm::length(m.material.emissive) > 0.1f) ++lit;
    }
    CHECK(lit == 1);
    CHECK(beams[0].owner == 42);
}

TEST_CASE("emit pose follows pan and tilt") {
    Rig rig(test::makeTestSpot());
    rig.encoder.setPhysical(rig.dmx, Attribute::Pan, degToRad(90.0f));
    rig.encoder.setPhysical(rig.dmx, Attribute::Tilt, degToRad(90.0f));
    rig.send();
    rig.runtime.snapToTargets();
    const glm::mat4 world = glm::translate(glm::mat4(1.0f), {2.0f, 6.0f, -1.0f});
    std::vector<MeshInstance> meshes;
    std::vector<BeamState> beams;
    rig.runtime.emit(world, 1, meshes, beams);
    REQUIRE(beams.size() == 1);
    // Tilt +90 about X turns -Y into -Z, pan +90 about Y turns -Z into -X.
    checkVec(beams[0].direction, {-1, 0, 0}, 2e-3f);
    CHECK(std::abs(glm::dot(beams[0].up, beams[0].direction)) < 1e-4f);
    CHECK(glm::length(beams[0].up) == Approx(1.0f));
    // The lens hangs below the rigging point.
    CHECK(beams[0].position.y < 6.0f);
    CHECK(beams[0].position.y > 5.0f);

    // Home position points straight down.
    rig.encoder.setPhysical(rig.dmx, Attribute::Pan, 0.0f);
    rig.encoder.setPhysical(rig.dmx, Attribute::Tilt, 0.0f);
    rig.send();
    rig.runtime.snapToTargets();
    checkVec(rig.beam().direction, {0, -1, 0}, 2e-3f);
    checkVec(rig.beam().up, {0, 0, 1}, 2e-3f);
}

TEST_CASE("multi-cell fixtures emit one beam per cell with cell colours and master dimmer") {
    Rig rig(test::makeTestPixelBar(8));
    rig.ch(1) = 255;  // master
    for (int i = 0; i < 8; ++i) rig.ch(2 + 3 * i + (i % 3)) = 255;  // R, G, B, R, ...
    rig.run(0.02f);
    std::vector<MeshInstance> meshes;
    std::vector<BeamState> beams;
    rig.runtime.emit(glm::mat4(1.0f), 3, meshes, beams);
    REQUIRE(beams.size() == 8);
    for (int i = 0; i < 8; ++i) {
        glm::vec3 expected{0.0f};
        expected[i % 3] = 1.0f;
        checkVec(beams[static_cast<std::size_t>(i)].color, expected);
        CHECK(beams[static_cast<std::size_t>(i)].intensity == Approx(1.0f));
        // Evenly spaced along X across the 1 m body, centred.
        CHECK(beams[static_cast<std::size_t>(i)].position.x == Approx(-0.4375f + 0.125f * i).epsilon(1e-3));
    }
    rig.ch(1) = 128;  // master dimmer scales every cell
    rig.run(0.02f);
    CHECK(rig.beam(5).intensity == Approx(128.0f / 255.0f).epsilon(1e-3));
}

TEST_CASE("mode master switches channel functions") {
    using A = Attribute;
    FixtureType t = test::makeTestRgbwPar();
    DmxMode& m = t.modes[0];
    // Channel 5 is a dimmer when channel 6 < 128, otherwise a strobe rate.
    ChannelFunction dim = test::fn(A::Dimmer, 0, 255, 0, 1);
    dim.modeMaster = "Mode";
    dim.modeFrom = 0;
    dim.modeTo = 127;
    ChannelFunction strobe = test::fn(A::Shutter1Strobe, 0, 255, 1, 10);
    strobe.modeMaster = "Mode";
    strobe.modeFrom = 128;
    strobe.modeTo = 255;
    m.channels.push_back(test::channel("Dim/Strobe", {5}, "", {dim, strobe}));
    m.channels.push_back(test::channel("Mode", {6}, "", {test::kindFn(A::Control, FunctionKind::Linear, 0, 255)}));
    m.footprint = 6;
    Rig rig(std::move(t));
    rig.ch(1) = rig.ch(2) = rig.ch(3) = rig.ch(4) = 255;
    rig.ch(5) = 64;
    rig.run(0.02f);
    CHECK(rig.beam().intensity == Approx(64.0f / 255.0f).epsilon(1e-3));
    const AttributeValues values = rig.runtime.values();
    CHECK(values[4].attribute == A::Dimmer);

    rig.ch(6) = 200;  // channel 5 becomes a strobe: no dimmer -> full, strobing
    rig.send();
    CHECK(rig.runtime.values()[4].attribute == A::Shutter1Strobe);
}
