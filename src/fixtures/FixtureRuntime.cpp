#include "fixtures/FixtureRuntime.h"

#include "core/Log.h"
#include "fixtures/ColorMath.h"
#include "fixtures/DmxValue.h"

#include <algorithm>
#include <cmath>
#include <numeric>
#include <unordered_map>

namespace dmxviz::fixtures {
namespace {

// Emissive radiance of a lens at full output. The renderer works in HDR, so
// a fully lit lens should clip to white and bloom.
constexpr float kLensEmissiveGain = 4.0f;
// The light leaves just below the model's bottom face so the lens never z-fights it.
constexpr float kLensGap = 0.001f;

const DmxMode& emptyMode() {
    static const DmxMode mode;
    return mode;
}

BeamShape toBeamShape(BeamType t) {
    switch (t) {
        case BeamType::Spot: return BeamShape::Spot;
        case BeamType::Beam: return BeamShape::Beam;
        case BeamType::Rectangle: return BeamShape::Rectangle;
        case BeamType::Glow: return BeamShape::Glow;
        case BeamType::Wash:
        case BeamType::PC:
        case BeamType::Fresnel: return BeamShape::Wash;
    }
    return BeamShape::Wash;
}

// Colour filter that turns a source at `baseWhite` into one at `target` K.
glm::vec3 temperatureFilter(float targetKelvin, const glm::vec3& baseWhite) {
    const glm::vec3 r = kelvinToLinear(targetKelvin) / glm::max(baseWhite, glm::vec3(1e-4f));
    return r / std::max(1.0f, maxComponent(r));
}

template <typename T>
void pushUnique(std::vector<T>& v, T value) {
    if (std::find(v.begin(), v.end(), value) == v.end()) v.push_back(value);
}

}  // namespace

// ============================================================ construction

FixtureRuntime::FixtureRuntime(std::shared_ptr<const FixtureType> type, std::string_view modeName,
                               const FixtureAssets* assets)
    : owned_(std::move(type)) {
    static const FixtureType kEmptyType;
    type_ = owned_ ? owned_.get() : &kEmptyType;
    mode_ = type_->findMode(modeName);
    if (!mode_) {
        if (!type_->modes.empty()) {
            log::warn("fixtures", "{}: unknown mode \"{}\", using \"{}\"", type_->id, modeName, type_->modes[0].name);
            mode_ = &type_->modes[0];
        } else {
            mode_ = &emptyMode();
        }
    }
    build(assets);
}

FixtureRuntime::FixtureRuntime(const FixtureType& type, const DmxMode& mode, const FixtureAssets* assets)
    : type_(&type), mode_(&mode) {
    build(assets);
}

int FixtureRuntime::wheelGroupFor(AttributeFamily family, int index) {
    switch (family) {
        case AttributeFamily::ColorWheel:
        case AttributeFamily::ColorWheelSpin: return kColor1 + std::min(index, 2);
        case AttributeFamily::Gobo:
        case AttributeFamily::GoboPos:
        case AttributeFamily::GoboPosRotate:
        case AttributeFamily::GoboWheelSpin: return kGobo1 + std::min(index, 1);
        case AttributeFamily::Animation:
        case AttributeFamily::AnimationPos:
        case AttributeFamily::AnimationPosRotate: return kAnimation1;
        case AttributeFamily::Prism:
        case AttributeFamily::PrismPos:
        case AttributeFamily::PrismPosRotate: return kPrism1 + std::min(index, 1);
        case AttributeFamily::ColorMacro: return kMacro1;
        default: return -1;
    }
}

int FixtureRuntime::flatten(const Geometry& g, int parent) {
    const int index = static_cast<int>(nodes_.size());
    Node node;
    node.parent = parent;
    node.local = g.localTransform();
    node.geometry = &g;
    nodes_.push_back(node);
    for (const Geometry& child : g.children) flatten(child, index);
    nodes_[static_cast<std::size_t>(index)].subtreeEnd = static_cast<int>(nodes_.size());
    return index;
}

void FixtureRuntime::collectTargets(const Channel& channel, std::vector<int>& beams, std::vector<int>& axes) const {
    std::vector<int> roots;
    if (nodes_.empty()) return;
    auto findNode = [&](std::string_view name) {
        for (std::size_t i = 0; i < nodes_.size(); ++i)
            if (nodes_[i].geometry->name == name) return static_cast<int>(i);
        return -1;
    };
    if (channel.geometry.empty()) {
        roots.push_back(0);
    } else if (int n = findNode(channel.geometry); n >= 0) {
        roots.push_back(n);
    } else if (const GeometryGroup* group = type_->findGroup(channel.geometry)) {
        for (const std::string& member : group->members)
            if (int m = findNode(member); m >= 0) roots.push_back(m);
    } else {
        log::warn("fixtures", "{}: channel \"{}\" controls unknown geometry \"{}\"", type_->id, channel.name,
                  channel.geometry);
    }
    for (int r : roots) {
        for (int j = r; j < nodes_[static_cast<std::size_t>(r)].subtreeEnd; ++j) {
            const Node& n = nodes_[static_cast<std::size_t>(j)];
            if (n.beam >= 0) pushUnique(beams, n.beam);
            if (n.axis >= 0) pushUnique(axes, n.axis);
        }
    }
}

void FixtureRuntime::build(const FixtureAssets* assets) {
    // --- geometry tree -> flat arrays ----------------------------------------
    const Geometry* root = &type_->geometry;
    if (!mode_->geometryRoot.empty()) {
        if (const Geometry* g = type_->findGeometry(mode_->geometryRoot)) root = g;
    }
    flatten(*root, -1);
    nodeModel_.assign(nodes_.size(), glm::mat4(1.0f));

    const MovementSpec& movement = type_->physical.movement;
    for (std::size_t i = 0; i < nodes_.size(); ++i) {
        Node& node = nodes_[i];
        const Geometry& g = *node.geometry;
        if (g.type == GeometryType::Axis) {
            node.axis = static_cast<int>(axes_.size());
            AxisState axis;
            axis.node = static_cast<int>(i);
            axis.pan.setLimits(movement.panMaxSpeed, movement.panAcceleration);
            axis.tilt.setLimits(movement.tiltMaxSpeed, movement.tiltAcceleration);
            axes_.push_back(axis);
        } else if (g.type == GeometryType::Beam) {
            node.beam = static_cast<int>(beams_.size());
            BeamRuntime beam;
            beam.node = static_cast<int>(i);
            beam.spec = &g.beam;
            beam.lensOffset = (g.model.empty() ? 0.0f : 0.5f * g.model.size.y) + kLensGap;
            beam.white = kelvinToLinear(g.beam.colorTemperature);
            beam.defaultWheel.fill(-1);
            beam.activeWheel.fill(-1);
            for (int a = 0; a < kColorAddCount; ++a)
                beam.emitterColor[static_cast<std::size_t>(a)] =
                    defaultEmitterColor(static_cast<Attribute>(static_cast<int>(Attribute::ColorAdd_R) + a));
            beams_.push_back(beam);
        }
    }

    // --- drawable parts and emissive lenses -------------------------------------
    if (assets) {
        for (std::size_t i = 0; i < nodes_.size(); ++i)
            for (const GeometryPart& p : assets->parts(nodes_[i].geometry->name))
                parts_.push_back({static_cast<int>(i), p.mesh, p.transform, p.material, -1});
        for (std::size_t b = 0; b < beams_.size(); ++b) {
            const BeamSpec& spec = *beams_[b].spec;
            const bool rect = spec.type == BeamType::Rectangle;
            Part lens;
            lens.node = beams_[b].node;
            lens.mesh = rect ? assets->lensPlane() : assets->lensDisc();
            if (lens.mesh == kInvalidMesh) continue;
            const glm::vec3 scale = rect ? glm::vec3(spec.emitterSize.x, 1.0f, spec.emitterSize.y)
                                         : glm::vec3(2.0f * spec.lensRadius, 1.0f, 2.0f * spec.lensRadius);
            // Unit disc/plane face +Y; flip so the lens faces along the beam (-Y).
            lens.transform = glm::translate(glm::mat4(1.0f), {0.0f, -beams_[b].lensOffset, 0.0f}) *
                             glm::rotate(glm::mat4(1.0f), kPi, {1.0f, 0.0f, 0.0f}) * glm::scale(glm::mat4(1.0f), scale);
            lens.material.albedo = glm::vec3(0.02f);
            lens.material.roughness = 0.1f;
            lens.lensOfBeam = static_cast<int>(b);
            parts_.push_back(lens);
        }
    }
    slotImages_.resize(type_->wheels.size());
    for (std::size_t w = 0; w < type_->wheels.size(); ++w)
        for (const WheelSlot& slot : type_->wheels[w].slots)
            slotImages_[w].push_back(assets && !slot.image.empty() ? assets->image(slot.image) : kInvalidImage);

    // --- channels -------------------------------------------------------------
    std::unordered_map<std::string_view, int> channelIndex;
    for (std::size_t c = 0; c < mode_->channels.size(); ++c) channelIndex.emplace(mode_->channels[c].name, static_cast<int>(c));
    auto wheelIndex = [&](std::string_view name) {
        for (std::size_t w = 0; w < type_->wheels.size(); ++w)
            if (type_->wheels[w].name == name) return static_cast<int>(w);
        return -1;
    };

    std::vector<std::vector<bool>> additiveSeen(beams_.size(), std::vector<bool>(kColorAddCount, false));
    std::vector<int> targetBeams, targetAxes;
    for (const Channel& channel : mode_->channels) {
        ChannelRuntime ch;
        ch.channel = &channel;
        ch.firstFunction = static_cast<int>(functions_.size());
        targetBeams.clear();
        targetAxes.clear();
        collectTargets(channel, targetBeams, targetAxes);
        ch.firstBeam = static_cast<int>(beamTargets_.size());
        ch.beamCount = static_cast<int>(targetBeams.size());
        beamTargets_.insert(beamTargets_.end(), targetBeams.begin(), targetBeams.end());
        if (!targetAxes.empty()) {
            ch.panAxis = targetAxes[0];
            ch.tiltAxis = targetAxes.size() > 1 ? targetAxes[1] : targetAxes[0];
        }

        for (const ChannelFunction& f : channel.functions) {
            FunctionRuntime fr;
            if (!f.modeMaster.empty())
                if (auto it = channelIndex.find(f.modeMaster); it != channelIndex.end()) fr.master = it->second;
            if (!f.wheel.empty()) fr.wheel = wheelIndex(f.wheel);
            const AttributeInfo& info = attributeInfo(f.attribute);
            if (info.family == AttributeFamily::ColorAdd) {
                const Emitter* emitter = f.emitter.empty() ? nullptr : type_->findEmitter(f.emitter);
                fr.emitterColor = emitter ? emitter->color : defaultEmitterColor(f.attribute);
                for (int b : targetBeams) {
                    BeamRuntime& beam = beams_[static_cast<std::size_t>(b)];
                    beam.hasAdditive = true;
                    beam.emitterColor[info.index] = fr.emitterColor;
                    additiveSeen[static_cast<std::size_t>(b)][info.index] = true;
                }
            }
            const int group = wheelGroupFor(info.family, info.index);
            if (group >= 0 && fr.wheel >= 0)
                for (int b : targetBeams) {
                    int& def = beams_[static_cast<std::size_t>(b)].defaultWheel[static_cast<std::size_t>(group)];
                    if (def < 0) def = fr.wheel;
                }
            if (info.family == AttributeFamily::Shutter)
                for (int b : targetBeams) beams_[static_cast<std::size_t>(b)].hasShutterChannel = true;
            if (info.family == AttributeFamily::StrobeFrequency)
                for (int b : targetBeams) beams_[static_cast<std::size_t>(b)].strobeByFrequency = true;
            if (info.family == AttributeFamily::Pan && firstPanAxis_ < 0) firstPanAxis_ = ch.panAxis;
            if (info.family == AttributeFamily::Tilt && firstTiltAxis_ < 0) firstTiltAxis_ = ch.tiltAxis;
            functions_.push_back(fr);
        }
        channels_.push_back(ch);
    }

    // Reference for additive mixing: everything at full is "intensity 1".
    for (std::size_t b = 0; b < beams_.size(); ++b) {
        BeamRuntime& beam = beams_[b];
        if (beam.hasShutterChannel) beam.strobeByFrequency = false;
        if (!beam.hasAdditive) continue;
        glm::vec3 sum{0.0f};
        for (int a = 0; a < kColorAddCount; ++a)
            if (additiveSeen[b][static_cast<std::size_t>(a)]) sum += beam.emitterColor[static_cast<std::size_t>(a)];
        beam.additiveReference = std::max(1e-3f, maxComponent(sum));
    }

    // Masters (whole fixture) first, cells last, so per-cell values override.
    channelOrder_.resize(channels_.size());
    std::iota(channelOrder_.begin(), channelOrder_.end(), 0);
    std::stable_sort(channelOrder_.begin(), channelOrder_.end(), [&](int a, int b) {
        return channels_[static_cast<std::size_t>(a)].beamCount > channels_[static_cast<std::size_t>(b)].beamCount;
    });

    // --- initial DMX state: channel defaults, already in position --------------
    dmx_.assign(static_cast<std::size_t>(std::max(mode_->footprint, mode_->highestOffset())), 0);
    for (const Channel& c : mode_->channels) writeDmxValue(dmx_, c.offsets, c.defaultValue);
    decode();
    snapToTargets();
}

// ================================================================ decoding

void FixtureRuntime::setOrientation(const OrientationOptions& options) {
    orientation_ = options;
    decode();
}

void FixtureRuntime::setDmx(std::span<const std::uint8_t> footprint) {
    const std::size_t n = std::min(footprint.size(), dmx_.size());
    // Most fixtures hold their look for many frames, and decode() is a pure function of dmx_ (the orientation
    // re-decodes itself in setOrientation), so identical bytes need no new decode: this is the common case in a
    // real show and saves about a third of the CPU time of the simulation.
    const auto stored = dmx_.begin() + static_cast<std::ptrdiff_t>(n);
    if (std::equal(footprint.begin(), footprint.begin() + static_cast<std::ptrdiff_t>(n), dmx_.begin()) &&
        std::all_of(stored, dmx_.end(), [](std::uint8_t v) { return v == 0; }))
        return;
    std::copy_n(footprint.begin(), n, dmx_.begin());
    std::fill(dmx_.begin() + static_cast<std::ptrdiff_t>(n), dmx_.end(), std::uint8_t{0});
    decode();
}

bool FixtureRuntime::functionActive(const ChannelFunction& f, const FunctionRuntime& fr, std::uint32_t value) const {
    if (value < f.dmxFrom || value > f.dmxTo) return false;
    if (fr.master >= 0) {
        const std::uint32_t m = channels_[static_cast<std::size_t>(fr.master)].value;
        if (m < f.modeFrom || m > f.modeTo) return false;
    }
    return true;
}

void FixtureRuntime::decode() {
    for (BeamRuntime& b : beams_) {
        b.controls = BeamControls{};
        for (int g = 0; g < kWheelGroups; ++g)
            b.controls.wheels[static_cast<std::size_t>(g)].wheel = b.defaultWheel[static_cast<std::size_t>(g)];
    }
    // Raw values first: mode masters may be decoded after the channels they switch.
    for (ChannelRuntime& ch : channels_)
        ch.value = ch.channel->offsets.empty() ? ch.channel->defaultValue : readDmxValue(dmx_, ch.channel->offsets);

    for (int index : channelOrder_) {
        ChannelRuntime& ch = channels_[static_cast<std::size_t>(index)];
        ch.activeFunction = -1;
        const std::vector<ChannelFunction>& functions = ch.channel->functions;
        for (std::size_t i = 0; i < functions.size(); ++i) {
            const ChannelFunction& f = functions[i];
            const FunctionRuntime& fr = functions_[static_cast<std::size_t>(ch.firstFunction) + i];
            if (!functionActive(f, fr, ch.value)) continue;
            if (ch.activeFunction < 0) ch.activeFunction = static_cast<int>(i);
            const float t = f.dmxTo > f.dmxFrom ? static_cast<float>(ch.value - f.dmxFrom) /
                                                      static_cast<float>(f.dmxTo - f.dmxFrom)
                                                : 0.0f;
            applyFunction(ch, f, fr, t);
        }
    }
}

void FixtureRuntime::applyFunction(const ChannelRuntime& ch, const ChannelFunction& f, const FunctionRuntime& fr,
                                   float t) {
    if (f.kind == FunctionKind::NoFeature) return;
    const float physical = f.physicalFrom + (f.physicalTo - f.physicalFrom) * t;
    const AttributeFamily family = attributeInfo(f.attribute).family;
    auto pan = [&](float v) { return (orientation_.invertPan ? -v : v) + orientation_.panOffset; };
    auto tilt = [&](float v) { return (orientation_.invertTilt ? -v : v) + orientation_.tiltOffset; };
    switch (family) {
        case AttributeFamily::Pan:
            if (ch.panAxis >= 0) axes_[static_cast<std::size_t>(ch.panAxis)].pan.setTarget(pan(physical));
            return;
        case AttributeFamily::Tilt:
            if (ch.tiltAxis >= 0) axes_[static_cast<std::size_t>(ch.tiltAxis)].tilt.setTarget(tilt(physical));
            return;
        case AttributeFamily::PanRotate:
            if (ch.panAxis >= 0)
                axes_[static_cast<std::size_t>(ch.panAxis)].pan.setSpin(orientation_.invertPan ? -physical : physical);
            return;
        case AttributeFamily::TiltRotate:
            if (ch.tiltAxis >= 0)
                axes_[static_cast<std::size_t>(ch.tiltAxis)].tilt.setSpin(orientation_.invertTilt ? -physical : physical);
            return;
        default: break;
    }
    for (int i = 0; i < ch.beamCount; ++i) {
        const int b = beamTargets_[static_cast<std::size_t>(ch.firstBeam + i)];
        applyToBeam(beams_[static_cast<std::size_t>(b)].controls, f, fr, physical, t);
    }
}

void FixtureRuntime::applyToBeam(BeamControls& c, const ChannelFunction& f, const FunctionRuntime& fr, float physical,
                                 float t) {
    const AttributeInfo& info = attributeInfo(f.attribute);
    const std::size_t index = info.index;
    const float ratio = std::clamp(physical, 0.0f, 1.0f);
    const int group = wheelGroupFor(info.family, info.index);
    WheelSelect* wheel = group >= 0 ? &c.wheels[static_cast<std::size_t>(group)] : nullptr;

    switch (info.family) {
        case AttributeFamily::Dimmer: c.dimmer *= ratio; break;  // master x cell
        case AttributeFamily::ColorAdd: c.colorAdd[index] = ratio; break;
        case AttributeFamily::ColorSub: c.colorSub[static_cast<int>(index)] = ratio; break;
        case AttributeFamily::CTO: c.cto = physical; break;
        case AttributeFamily::CTC: c.ctc = physical; break;
        case AttributeFamily::CTB: c.ctb = physical; break;
        case AttributeFamily::ColorMacro:
        case AttributeFamily::ColorWheel:
        case AttributeFamily::Gobo:
        case AttributeFamily::Animation:
        case AttributeFamily::Prism:
            if (f.kind != FunctionKind::WheelSlot) break;
            if (fr.wheel >= 0) wheel->wheel = fr.wheel;
            wheel->slot = f.slotFrom + (f.slotTo - f.slotFrom) * t - 1.0f;
            wheel->spinning = false;
            break;
        case AttributeFamily::ColorWheelSpin:
        case AttributeFamily::GoboWheelSpin:
            if (fr.wheel >= 0) wheel->wheel = fr.wheel;
            wheel->spin = physical;
            wheel->spinning = true;
            break;
        case AttributeFamily::GoboPos:
        case AttributeFamily::AnimationPos:
        case AttributeFamily::PrismPos:
            wheel->rotation = physical;
            wheel->rotationSpinning = false;
            break;
        case AttributeFamily::GoboPosRotate:
        case AttributeFamily::AnimationPosRotate:
        case AttributeFamily::PrismPosRotate:
            wheel->rotation = physical;
            wheel->rotationSpinning = true;
            break;
        case AttributeFamily::Zoom: c.zoom = physical; break;
        case AttributeFamily::Focus: c.focus = ratio; break;
        case AttributeFamily::Iris: c.iris = ratio; break;
        case AttributeFamily::Frost: c.frost = std::max(c.frost, ratio); break;
        case AttributeFamily::Shutter:
            c.shutter = f.kind;
            c.shutterFrequency = isStrobeKind(f.kind) ? physical : 0.0f;
            break;
        case AttributeFamily::StrobeFrequency: c.strobeFrequency = physical; break;
        case AttributeFamily::StrobeDuration: c.strobeDuration = physical; break;
        case AttributeFamily::BladeA: c.bladeA[index % kMaxBlades] = ratio; break;
        case AttributeFamily::BladeB:
            c.bladeB[index % kMaxBlades] = ratio;
            c.hasBladeB[index % kMaxBlades] = true;
            break;
        case AttributeFamily::BladeRot: c.bladeRot[index % kMaxBlades] = physical; break;
        case AttributeFamily::ShaperRot: c.shaperRot = physical; break;
        default: break;  // position handled per axis; Other is not rendered
    }
}

// =============================================================== simulation

void FixtureRuntime::syncMotionTargets(BeamRuntime& b) {
    for (int g = 0; g < kWheelGroups; ++g) {
        const std::size_t gi = static_cast<std::size_t>(g);
        const WheelSelect& w = b.controls.wheels[gi];
        if (w.wheel < 0) continue;
        WheelMotion& motion = b.wheelMotion[gi];
        const int slots = static_cast<int>(type_->wheels[static_cast<std::size_t>(w.wheel)].slots.size());
        if (b.activeWheel[gi] != w.wheel) {
            // A different wheel took over this feature: start where DMX says.
            b.activeWheel[gi] = w.wheel;
            motion.setSlotCount(slots);
            motion.setTarget(w.slot);
            motion.snapToTarget();
        }
        motion.setSlotCount(slots);
        if (w.spinning)
            motion.setSpin(w.spin / (2.0f * kPi) * static_cast<float>(slots));
        else
            motion.setTarget(w.slot);
        RotationMotion& rotation = b.rotationMotion[gi];
        if (w.rotationSpinning)
            rotation.setSpin(w.rotation);
        else
            rotation.setIndex(w.rotation);
    }
}

void FixtureRuntime::snapToTargets() {
    for (AxisState& a : axes_) {
        a.pan.snapToTarget();
        a.tilt.snapToTarget();
    }
    for (BeamRuntime& b : beams_) {
        syncMotionTargets(b);
        for (int g = 0; g < kWheelGroups; ++g) {
            b.wheelMotion[static_cast<std::size_t>(g)].snapToTarget();
            b.rotationMotion[static_cast<std::size_t>(g)].snapToTarget();
        }
    }
    update(0.0f, 0.0);
}

void FixtureRuntime::update(float dt, double /*timeSeconds*/) {
    dt = std::max(dt, 0.0f);
    for (AxisState& a : axes_) {
        a.pan.update(dt);
        a.tilt.update(dt);
    }
    updateNodeTransforms();
    for (BeamRuntime& b : beams_) computeOptics(b, dt);
}

void FixtureRuntime::updateNodeTransforms() {
    for (std::size_t i = 0; i < nodes_.size(); ++i) {
        const Node& n = nodes_[i];
        glm::mat4 m = n.local;
        if (n.axis >= 0) {
            const AxisState& a = axes_[static_cast<std::size_t>(n.axis)];
            m = m * glm::rotate(glm::mat4(1.0f), a.pan.angle(), {0.0f, 1.0f, 0.0f}) *
                glm::rotate(glm::mat4(1.0f), a.tilt.angle(), {1.0f, 0.0f, 0.0f});
        }
        nodeModel_[i] = n.parent >= 0 ? nodeModel_[static_cast<std::size_t>(n.parent)] * m : m;
    }
}

void FixtureRuntime::computeOptics(BeamRuntime& b, float dt) {
    const BeamControls& c = b.controls;
    const BeamSpec& spec = *b.spec;
    const MovementSpec& movement = type_->physical.movement;

    // --- wheels move between slots, gobos/prisms rotate ---------------------------
    syncMotionTargets(b);
    for (int g = 0; g < kWheelGroups; ++g) {
        if (c.wheels[static_cast<std::size_t>(g)].wheel < 0) continue;
        b.wheelMotion[static_cast<std::size_t>(g)].update(dt, movement.wheelSlotsPerSecond);
        b.rotationMotion[static_cast<std::size_t>(g)].update(dt, movement.indexRotationSpeed);
    }
    auto visibleSlot = [&](int g) -> const WheelSlot* {
        const int w = c.wheels[static_cast<std::size_t>(g)].wheel;
        if (w < 0) return nullptr;
        const Wheel& wheel = type_->wheels[static_cast<std::size_t>(w)];
        const int s = b.wheelMotion[static_cast<std::size_t>(g)].visibleSlot();
        return s < static_cast<int>(wheel.slots.size()) ? &wheel.slots[static_cast<std::size_t>(s)] : nullptr;
    };
    auto visibleImage = [&](int g) {
        const int w = c.wheels[static_cast<std::size_t>(g)].wheel;
        const int s = b.wheelMotion[static_cast<std::size_t>(g)].visibleSlot();
        const auto& images = slotImages_[static_cast<std::size_t>(w)];
        return s < static_cast<int>(images.size()) ? images[static_cast<std::size_t>(s)] : kInvalidImage;
    };

    // --- colour -------------------------------------------------------------------
    glm::vec3 source = b.white;
    float reference = 1.0f;
    if (b.hasAdditive) {
        source = glm::vec3(0.0f);
        for (int a = 0; a < kColorAddCount; ++a)
            source += c.colorAdd[static_cast<std::size_t>(a)] * b.emitterColor[static_cast<std::size_t>(a)];
        reference = b.additiveReference;
    }
    if (const WheelSlot* macro = visibleSlot(kMacro1); macro && macro->kind == SlotKind::Color)
        source = macro->color * reference;  // colour macro: the fixture mixes that colour at full

    glm::vec3 filter = glm::vec3(1.0f) - c.colorSub;
    float wheelFrost = 0.0f;
    for (int g = 0; g < kWheelGroups; ++g) {
        const WheelSlot* slot = visibleSlot(g);
        if (!slot) continue;
        if (slot->kind == SlotKind::Frost) wheelFrost = std::max(wheelFrost, slot->frost);
        if (g >= kColor1 && g <= kColor3) filter *= slot->color;
    }
    const float baseKelvin = spec.colorTemperature;
    if (c.cto > 0.0f) filter *= temperatureFilter(std::min(c.cto, baseKelvin), b.white);  // CTO only warms
    if (c.ctb > 0.0f) filter *= temperatureFilter(std::max(c.ctb, baseKelvin), b.white);  // CTB only cools
    if (c.ctc > 0.0f) filter *= temperatureFilter(c.ctc, b.white);

    const glm::vec3 mixed = source * filter;
    const float peak = maxComponent(mixed);
    BeamState& o = b.optics;
    o.color = peak > 1e-6f ? mixed / peak : normalizeMax(glm::max(filter * b.white, glm::vec3(1e-6f)));
    // Energy lost to normalisation goes into intensity (ARCHITECTURE.md §5.4).
    const float colorLevel = std::clamp(peak / reference, 0.0f, 1.0f);

    // --- intensity: dimmer curve x colour level x shutter ----------------------------
    float dimmer = std::clamp(c.dimmer, 0.0f, 1.0f);
    if (type_->physical.dimmerCurve == DimmerCurve::SquareLaw) dimmer *= dimmer;
    FunctionKind shutter = c.shutter;
    float frequency = c.shutterFrequency > 0.0f ? c.shutterFrequency : c.strobeFrequency;
    if (b.strobeByFrequency && c.strobeFrequency > 0.0f) shutter = FunctionKind::Strobe;
    b.strobe.update(dt, shutter, frequency, c.strobeDuration);
    o.intensity = dimmer * colorLevel * b.strobe.openness();

    // --- optics: zoom, iris, frost, focus ------------------------------------------------
    o.shape = toBeamShape(spec.type);
    o.lensRadius = spec.lensRadius;
    o.emitterSize = spec.emitterSize;
    float beamAngle = c.zoom > 0.0f ? c.zoom : spec.beamAngle;
    float fieldAngle = beamAngle * (spec.beamAngle > 1e-6f ? spec.fieldAngle / spec.beamAngle : 1.2f);
    const float iris = std::clamp(c.iris, 0.02f, 1.0f);
    beamAngle *= iris;
    fieldAngle *= iris;
    const float frost = std::clamp(std::max(c.frost, wheelFrost), 0.0f, 1.0f);
    beamAngle += frost * std::max(0.5f * beamAngle, degToRad(8.0f));
    fieldAngle += frost * std::max(0.6f * fieldAngle, degToRad(15.0f));
    o.beamAngle = std::clamp(beamAngle, degToRad(0.2f), degToRad(170.0f));
    o.fieldAngle = std::clamp(std::max(fieldAngle, o.beamAngle), o.beamAngle, degToRad(175.0f));
    o.iris = iris;
    o.frost = frost;
    o.focus = std::clamp(c.focus, 0.0f, 1.0f);
    o.luminousFlux = spec.luminousFlux * iris * iris;  // the iris blocks light, it does not concentrate it

    // --- patterns ---------------------------------------------------------------------
    for (int i = 0; i < kMaxGobosPerBeam; ++i) {
        const int g = kGobo1 + i;
        GoboState gobo;
        const WheelSlot* slot = visibleSlot(g);
        if (slot && (slot->kind == SlotKind::Gobo || slot->kind == SlotKind::AnimationWheel)) {
            gobo.image = visibleImage(g);
            gobo.rotation = b.rotationMotion[static_cast<std::size_t>(g)].angle();
        }
        o.gobos[static_cast<std::size_t>(i)] = gobo;
    }
    o.animationWheel = GoboState{};
    if (const WheelSlot* slot = visibleSlot(kAnimation1); slot && !slot->image.empty()) {
        o.animationWheel.image = visibleImage(kAnimation1);
        o.animationWheel.rotation = b.rotationMotion[kAnimation1].angle();
    }
    o.prism = PrismState{};
    for (int g : {static_cast<int>(kPrism1), static_cast<int>(kPrism2)}) {
        const WheelSlot* slot = visibleSlot(g);
        if (!slot || slot->kind != SlotKind::Prism || slot->facets.empty()) continue;
        const std::size_t n = std::min<std::size_t>(slot->facets.size(), kMaxPrismFacets);
        o.prism.facetCount = static_cast<std::uint8_t>(n);
        std::copy_n(slot->facets.begin(), n, o.prism.facets.begin());
        o.prism.rotation = b.rotationMotion[static_cast<std::size_t>(g)].angle();
        break;
    }
    for (std::size_t i = 0; i < kMaxBlades; ++i) {
        // A and B are the two ends of a blade: their mean is the insertion, their difference the tilt.
        const float a = c.bladeA[i];
        const float bEnd = c.hasBladeB[i] ? c.bladeB[i] : a;
        o.blades[i].insertion = std::clamp(0.5f * (a + bEnd), 0.0f, 1.0f);
        o.blades[i].angle = std::atan(bEnd - a) + c.bladeRot[i];
    }
    o.bladeRotation = c.shaperRot;
    o.castsVolume = o.shape != BeamShape::Glow;
}

// ===================================================================== output

void FixtureRuntime::emit(const glm::mat4& world, NodeId owner, std::vector<MeshInstance>& meshes,
                          std::vector<BeamState>& beams) const {
    for (const Part& p : parts_) {
        MeshInstance mi;
        mi.mesh = p.mesh;
        mi.world = world * nodeModel_[static_cast<std::size_t>(p.node)] * p.transform;
        mi.material = p.material;
        mi.pickId = owner;
        if (p.lensOfBeam >= 0) {
            const BeamState& o = beams_[static_cast<std::size_t>(p.lensOfBeam)].optics;
            mi.material.emissive = o.color * (o.intensity * kLensEmissiveGain);
            mi.castsShadow = false;
        }
        meshes.push_back(mi);
    }
    for (const BeamRuntime& b : beams_) {
        BeamState s = b.optics;
        const glm::mat4 m = world * nodeModel_[static_cast<std::size_t>(b.node)];
        const glm::mat3 r(m);
        s.position = glm::vec3(m * glm::vec4(0.0f, -b.lensOffset, 0.0f, 1.0f));
        s.direction = glm::normalize(r * glm::vec3(0.0f, -1.0f, 0.0f));
        glm::vec3 up = r * glm::vec3(0.0f, 0.0f, 1.0f);
        up -= glm::dot(up, s.direction) * s.direction;
        const float len = glm::length(up);
        s.up = len > 1e-6f ? up / len : glm::vec3(1.0f, 0.0f, 0.0f);
        s.owner = owner;
        beams.push_back(s);
    }
}

AttributeValues FixtureRuntime::values() const {
    AttributeValues out;
    out.reserve(channels_.size());
    for (const ChannelRuntime& ch : channels_) {
        AttributeValue v;
        v.channel = ch.channel->name;
        v.geometry = ch.channel->geometry;
        v.dmx = ch.value;
        if (ch.activeFunction >= 0) {
            const ChannelFunction& f = ch.channel->functions[static_cast<std::size_t>(ch.activeFunction)];
            v.attribute = f.attribute;
            v.attributeLabel = f.attributeLabel();
            v.function = f.name;
            v.normalized = f.dmxTo > f.dmxFrom
                               ? static_cast<float>(ch.value - f.dmxFrom) / static_cast<float>(f.dmxTo - f.dmxFrom)
                               : 0.0f;
            v.physical = f.physicalFrom + (f.physicalTo - f.physicalFrom) * v.normalized;
        } else if (!ch.channel->functions.empty()) {
            v.attribute = ch.channel->functions.front().attribute;
            v.attributeLabel = ch.channel->functions.front().attributeLabel();
        }
        out.push_back(v);
    }
    return out;
}

float FixtureRuntime::panAngle() const {
    const int a = firstPanAxis_ >= 0 ? firstPanAxis_ : (axes_.empty() ? -1 : 0);
    return a >= 0 ? axes_[static_cast<std::size_t>(a)].pan.angle() : 0.0f;
}

float FixtureRuntime::tiltAngle() const {
    const int a = firstTiltAxis_ >= 0 ? firstTiltAxis_ : (axes_.empty() ? -1 : 0);
    return a >= 0 ? axes_[static_cast<std::size_t>(a)].tilt.angle() : 0.0f;
}

}  // namespace dmxviz::fixtures
