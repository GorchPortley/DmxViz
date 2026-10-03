#pragma once
// FixtureRuntime: one patched fixture brought to life.
//
// Every frame the simulation feeds it the raw DMX bytes of its footprint,
// advances time, and asks it for render data:
//
//     runtime.setDmx(bytes);            // decode channels -> per-beam controls, axis targets
//     runtime.update(dt, time);         // pan/tilt physics, wheels, rotation, strobe, colour mixing
//     runtime.emit(world, nodeId, meshes, beams);   // MeshInstances + one BeamState per Beam geometry
//
// It works on any FixtureType/DmxMode (native, OFL, GDTF) and never allocates
// after construction: setDmx/update/emit only touch preallocated arrays, and
// emit appends to vectors the caller reuses every frame.
//
// Decoding rules (ARCHITECTURE.md §5.4): 8/16/24-bit channels, function lookup
// by DMX range (+ mode masters), additive / subtractive / wheel colour mixing,
// CTO/CTB/CTC, dimmer curve, shutter and strobe variants, pan/tilt with speed and
// acceleration limits, continuous rotation, wheels that move between slots,
// gobo index/spin, prisms, animation wheel, zoom, iris, frost, focus, framing.

#include "core/SceneTypes.h"
#include "fixtures/FixtureAssets.h"
#include "fixtures/FixtureType.h"
#include "fixtures/Motion.h"

#include <array>
#include <cstdint>
#include <memory>
#include <span>
#include <string_view>
#include <vector>

namespace dmxviz::fixtures {

// Per-instance orientation tweaks from the stage (fixture hung the other way round...).
struct OrientationOptions {
    bool invertPan = false;
    bool invertTilt = false;
    float panOffset = 0.0f;   // rad, added after inversion
    float tiltOffset = 0.0f;  // rad
};

// Decoded state of one channel for the inspector / test console.
struct AttributeValue {
    Attribute attribute = Attribute::NoFeature;
    std::string_view attributeLabel;  // canonical name or preserved original
    std::string_view channel;         // channel name
    std::string_view geometry;        // geometry or group the channel controls
    std::string_view function;        // active function name (may be empty)
    std::uint32_t dmx = 0;            // raw channel value (channel resolution)
    float physical = 0.0f;            // memory units (radians, Hz, 0..1, K ...)
    float normalized = 0.0f;          // position within the active function's DMX range, 0..1
};
using AttributeValues = std::vector<AttributeValue>;

class FixtureRuntime {
public:
    // Keeps the type alive. An unknown mode name falls back to the first mode.
    FixtureRuntime(std::shared_ptr<const FixtureType> type, std::string_view modeName,
                   const FixtureAssets* assets = nullptr);
    // The caller guarantees `type` outlives the runtime; `mode` must belong to `type`.
    FixtureRuntime(const FixtureType& type, const DmxMode& mode, const FixtureAssets* assets = nullptr);

    const FixtureType& type() const { return *type_; }
    const DmxMode& mode() const { return *mode_; }
    int footprint() const { return static_cast<int>(dmx_.size()); }
    int beamCount() const { return static_cast<int>(beams_.size()); }

    void setOrientation(const OrientationOptions& options);

    // Raw bytes of this fixture's footprint (slot 1 first). Shorter spans read as zeros.
    void setDmx(std::span<const std::uint8_t> footprint);
    // Advances physics, wheels, strobe and recomputes the optical output. time is
    // only used for deterministic noise; dt drives everything else.
    void update(float dt, double timeSeconds);
    // Jumps pan/tilt, wheels and index rotations to their current targets
    // (e.g. after loading a show) instead of moving there physically.
    void snapToTargets();

    // Appends one MeshInstance per drawable part (bodies + emissive lenses) and
    // one BeamState per Beam geometry, in world space.
    void emit(const glm::mat4& world, NodeId owner, std::vector<MeshInstance>& meshes,
              std::vector<BeamState>& beams) const;

    // One entry per channel (allocates: UI use only).
    AttributeValues values() const;

    // Current pan/tilt of the first axis driven by Pan/Tilt (radians), for tests and the UI.
    float panAngle() const;
    float tiltAngle() const;

private:
    // ---- static structure (built once) ------------------------------------
    struct Node {
        int parent = -1;
        int subtreeEnd = 0;  // pre-order: descendants are (index, subtreeEnd)
        glm::mat4 local{1.0f};
        const Geometry* geometry = nullptr;
        int axis = -1;  // index into axes_ for Axis nodes
        int beam = -1;  // index into beams_ for Beam nodes
    };
    struct Part {
        int node = 0;
        MeshId mesh = kInvalidMesh;
        glm::mat4 transform{1.0f};
        Material material;
        int lensOfBeam = -1;  // >= 0: emissive lens of that beam
    };
    struct AxisState {
        int node = 0;
        AxisMotion pan;
        AxisMotion tilt;
    };

    // Wheel-driven features of one beam. Gobo/prism/animation also rotate in their holder.
    enum WheelGroup : int { kColor1, kColor2, kColor3, kGobo1, kGobo2, kAnimation1, kPrism1, kPrism2, kMacro1, kWheelGroups };

    struct WheelSelect {  // decoded from DMX
        int wheel = -1;        // index into type wheels; -1 = nothing selected
        float slot = 0.0f;     // 0-based target position
        float spin = 0.0f;     // rad/s whole-wheel rotation
        bool spinning = false;
        float rotation = 0.0f; // index angle (rad) or spin speed (rad/s) of the slot in its holder
        bool rotationSpinning = false;
    };

    struct BeamControls {  // rebuilt from DMX by every setDmx()
        float dimmer = 1.0f;
        std::array<float, kColorAddCount> colorAdd{};
        glm::vec3 colorSub{0.0f};
        float cto = 0.0f, ctc = 0.0f, ctb = 0.0f;  // K, 0 = not set
        std::array<WheelSelect, kWheelGroups> wheels{};
        float zoom = -1.0f;  // rad, < 0 = not set
        float focus = 1.0f;
        float iris = 1.0f;
        float frost = 0.0f;
        FunctionKind shutter = FunctionKind::ShutterOpen;
        float shutterFrequency = 0.0f;  // Hz from the shutter function itself
        float strobeFrequency = 0.0f;   // Hz from a separate StrobeFrequency channel
        float strobeDuration = 0.0f;    // s
        std::array<float, kMaxBlades> bladeA{}, bladeB{}, bladeRot{};
        std::array<bool, kMaxBlades> hasBladeB{};
        float shaperRot = 0.0f;
    };

    struct BeamRuntime {
        // static, from the fixture definition
        int node = 0;
        const BeamSpec* spec = nullptr;
        float lensOffset = 0.0f;  // m along -Y where the light leaves (bottom of the node's model)
        bool hasAdditive = false;        // colour comes from LED emitters, not a white lamp
        float additiveReference = 1.0f;  // max component of all emitters at full
        std::array<glm::vec3, kColorAddCount> emitterColor{};
        glm::vec3 white{1.0f};           // colour of the source at its colour temperature
        std::array<int, kWheelGroups> defaultWheel{};  // wheel used when only spin/rotation is set
        bool hasShutterChannel = false;
        bool strobeByFrequency = false;  // StrobeFrequency channel without a shutter channel: rate > 0 strobes
        // dynamic
        BeamControls controls;
        std::array<WheelMotion, kWheelGroups> wheelMotion{};
        std::array<RotationMotion, kWheelGroups> rotationMotion{};
        std::array<int, kWheelGroups> activeWheel{};  // wheel the motion currently runs on
        StrobeGenerator strobe;
        BeamState optics;  // computed by update(), pose filled by emit()
    };

    struct FunctionRuntime {
        int master = -1;          // channel index of the mode master
        int wheel = -1;           // resolved wheel index
        glm::vec3 emitterColor{1.0f};
    };
    struct ChannelRuntime {
        const Channel* channel = nullptr;
        int firstFunction = 0;    // into functions_
        int firstBeam = 0;        // into beamTargets_
        int beamCount = 0;
        int panAxis = -1, tiltAxis = -1;  // into axes_
        std::uint32_t value = 0;
        int activeFunction = -1;  // first matching function this frame
    };

    static int wheelGroupFor(AttributeFamily family, int index);

    void build(const FixtureAssets* assets);
    int flatten(const Geometry& g, int parent);
    void collectTargets(const Channel& channel, std::vector<int>& beams, std::vector<int>& axesInOrder) const;
    void applyFunction(const ChannelRuntime& ch, const ChannelFunction& f, const FunctionRuntime& fr, float t);
    void applyToBeam(BeamControls& c, const ChannelFunction& f, const FunctionRuntime& fr, float physical, float t);
    void decode();
    void syncMotionTargets(BeamRuntime& b);
    void computeOptics(BeamRuntime& b, float dt);
    void updateNodeTransforms();
    bool functionActive(const ChannelFunction& f, const FunctionRuntime& fr, std::uint32_t value) const;

    std::shared_ptr<const FixtureType> owned_;
    const FixtureType* type_ = nullptr;
    const DmxMode* mode_ = nullptr;
    OrientationOptions orientation_;

    std::vector<Node> nodes_;
    std::vector<glm::mat4> nodeModel_;  // fixture-space transform of every node, updated each frame
    std::vector<Part> parts_;
    std::vector<AxisState> axes_;
    std::vector<BeamRuntime> beams_;
    std::vector<ChannelRuntime> channels_;
    std::vector<int> channelOrder_;     // least specific first (most beams), so cell values override masters
    std::vector<FunctionRuntime> functions_;
    std::vector<int> beamTargets_;
    std::vector<std::vector<ImageId>> slotImages_;  // [wheel][slot]
    std::vector<std::uint8_t> dmx_;
    int firstPanAxis_ = -1;
    int firstTiltAxis_ = -1;
};

}  // namespace dmxviz::fixtures
