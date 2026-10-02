#include "fixtures/AttributeEncoder.h"

#include "fixtures/DmxValue.h"

#include <algorithm>
#include <cmath>

namespace dmxviz::fixtures {
namespace {

std::uint32_t lerpDmx(const ChannelFunction& f, float t) {
    t = std::clamp(t, 0.0f, 1.0f);
    const double span = static_cast<double>(f.dmxTo) - static_cast<double>(f.dmxFrom);
    return f.dmxFrom + static_cast<std::uint32_t>(std::lround(span * t));
}

std::uint32_t midDmx(const ChannelFunction& f) { return f.dmxFrom + (f.dmxTo - f.dmxFrom) / 2; }

bool isValueKind(FunctionKind k) {
    return k != FunctionKind::WheelSlot && k != FunctionKind::NoFeature && k != FunctionKind::ShutterOpen &&
           k != FunctionKind::ShutterClosed;
}

// Distance of v outside [a, b] (either order); 0 when inside.
float outside(float v, float a, float b) {
    const float lo = std::min(a, b), hi = std::max(a, b);
    return v < lo ? lo - v : (v > hi ? v - hi : 0.0f);
}

}  // namespace

AttributeEncoder::AttributeEncoder(const FixtureType& type, const DmxMode& mode) : type_(type), mode_(mode) {
    for (std::size_t c = 0; c < mode.channels.size(); ++c) {
        const Channel& ch = mode.channels[c];
        for (const ChannelFunction& f : ch.functions) {
            if (f.attribute == Attribute::NoFeature || f.kind == FunctionKind::NoFeature) continue;
            auto it = std::find_if(controls_.begin(), controls_.end(), [&](const Control& k) {
                return k.channel == static_cast<int>(c) && k.attribute == f.attribute && k.label == f.attributeLabel();
            });
            const float lo = std::min(f.physicalFrom, f.physicalTo), hi = std::max(f.physicalFrom, f.physicalTo);
            if (it == controls_.end()) {
                Control k;
                k.attribute = f.attribute;
                k.label = f.attributeLabel();
                k.channel = static_cast<int>(c);
                k.geometry = ch.geometry;
                k.physicalMin = lo;
                k.physicalMax = hi;
                if (f.kind == FunctionKind::WheelSlot) k.wheel = type.findWheel(f.wheel);
                controls_.push_back(k);
            } else {
                it->physicalMin = std::min(it->physicalMin, lo);
                it->physicalMax = std::max(it->physicalMax, hi);
                if (!it->wheel && f.kind == FunctionKind::WheelSlot) it->wheel = type.findWheel(f.wheel);
            }
        }
    }
}

std::vector<Attribute> AttributeEncoder::attributes() const {
    std::vector<Attribute> out;
    for (const Control& k : controls_)
        if (std::find(out.begin(), out.end(), k.attribute) == out.end()) out.push_back(k.attribute);
    return out;
}

bool AttributeEncoder::has(Attribute attribute) const {
    return std::any_of(controls_.begin(), controls_.end(), [&](const Control& k) { return k.attribute == attribute; });
}

void AttributeEncoder::writeChannel(std::span<std::uint8_t> footprint, const Channel& channel, std::uint32_t value) {
    writeDmxValue(footprint, channel.offsets, std::min(value, channel.maxValue()));
}

std::uint32_t AttributeEncoder::readChannel(std::span<const std::uint8_t> footprint, const Channel& channel) const {
    return channel.offsets.empty() ? channel.defaultValue : readDmxValue(footprint, channel.offsets);
}

bool AttributeEncoder::geometryMatches(const Channel& channel, std::string_view geometry) const {
    return geometry.empty() || channel.geometry == geometry;
}

template <typename Score>
int AttributeEncoder::pickFunction(std::span<std::uint8_t> footprint, const Channel& channel, Score&& score) const {
    int best = -1, bestScore = -1;
    bool bestAllowed = false;
    for (std::size_t i = 0; i < channel.functions.size(); ++i) {
        const ChannelFunction& f = channel.functions[i];
        const int s = score(f);
        if (s < 0) continue;
        bool allowed = true;
        if (const Channel* master = f.modeMaster.empty() ? nullptr : mode_.findChannel(f.modeMaster)) {
            const std::uint32_t m = readChannel(footprint, *master);
            allowed = m >= f.modeFrom && m <= f.modeTo;
        }
        if (best < 0 || (allowed && !bestAllowed) || (allowed == bestAllowed && s > bestScore)) {
            best = static_cast<int>(i);
            bestScore = s;
            bestAllowed = allowed;
        }
    }
    if (best >= 0 && !bestAllowed) {
        const ChannelFunction& f = channel.functions[static_cast<std::size_t>(best)];
        if (const Channel* master = mode_.findChannel(f.modeMaster)) writeChannel(footprint, *master, f.modeFrom);
    }
    return best;
}

bool AttributeEncoder::setPhysical(std::span<std::uint8_t> footprint, Attribute attribute, float value,
                                   std::string_view geometry) const {
    bool any = false;
    for (const Channel& ch : mode_.channels) {
        if (!geometryMatches(ch, geometry)) continue;
        // Score: inside the range wins, otherwise the closest range.
        const int index = pickFunction(footprint, ch, [&](const ChannelFunction& f) {
            if (f.attribute != attribute || !isValueKind(f.kind)) return -1;
            const float d = outside(value, f.physicalFrom, f.physicalTo);
            return d == 0.0f ? 1000000 : std::max(0, 999999 - static_cast<int>(d * 1000.0f));
        });
        if (index < 0) continue;
        const ChannelFunction& f = ch.functions[static_cast<std::size_t>(index)];
        const float range = f.physicalTo - f.physicalFrom;
        const float t = std::abs(range) > 1e-9f ? (value - f.physicalFrom) / range : 0.5f;
        writeChannel(footprint, ch, lerpDmx(f, t));
        any = true;
    }
    return any;
}

bool AttributeEncoder::setNormalized(std::span<std::uint8_t> footprint, Attribute attribute, float t,
                                     std::string_view geometry) const {
    bool any = false;
    for (const Channel& ch : mode_.channels) {
        if (!geometryMatches(ch, geometry)) continue;
        const int index = pickFunction(footprint, ch, [&](const ChannelFunction& f) {
            return f.attribute == attribute && f.kind != FunctionKind::NoFeature ? 1 : -1;
        });
        if (index < 0) continue;
        writeChannel(footprint, ch, lerpDmx(ch.functions[static_cast<std::size_t>(index)], t));
        any = true;
    }
    return any;
}

bool AttributeEncoder::setWheelSlot(std::span<std::uint8_t> footprint, Attribute attribute, float slot,
                                    std::string_view geometry) const {
    bool any = false;
    for (const Channel& ch : mode_.channels) {
        if (!geometryMatches(ch, geometry)) continue;
        // Prefer a function that holds exactly this slot over a proportional sweep through it.
        const int index = pickFunction(footprint, ch, [&](const ChannelFunction& f) {
            if (f.attribute != attribute || f.kind != FunctionKind::WheelSlot) return -1;
            if (outside(slot, f.slotFrom, f.slotTo) > 1e-4f) return -1;
            return f.slotFrom == f.slotTo ? 2 : 1;
        });
        if (index < 0) continue;
        const ChannelFunction& f = ch.functions[static_cast<std::size_t>(index)];
        const std::uint32_t dmx =
            f.slotFrom == f.slotTo ? midDmx(f) : lerpDmx(f, (slot - f.slotFrom) / (f.slotTo - f.slotFrom));
        writeChannel(footprint, ch, dmx);
        any = true;
    }
    return any;
}

bool AttributeEncoder::setShutter(std::span<std::uint8_t> footprint, FunctionKind kind, float frequency,
                                  std::string_view geometry) const {
    bool any = false;
    for (const Channel& ch : mode_.channels) {
        if (!geometryMatches(ch, geometry)) continue;
        const int index = pickFunction(footprint, ch, [&](const ChannelFunction& f) {
            if (attributeFamily(f.attribute) != AttributeFamily::Shutter || f.kind != kind) return -1;
            if (frequency <= 0.0f || !isStrobeKind(kind)) return 1;
            const float d = outside(frequency, f.physicalFrom, f.physicalTo);
            return d == 0.0f ? 100000 : std::max(0, 99999 - static_cast<int>(d * 100.0f));
        });
        if (index < 0) continue;
        const ChannelFunction& f = ch.functions[static_cast<std::size_t>(index)];
        std::uint32_t dmx = midDmx(f);
        if (frequency > 0.0f && isStrobeKind(kind) && std::abs(f.physicalTo - f.physicalFrom) > 1e-6f)
            dmx = lerpDmx(f, (frequency - f.physicalFrom) / (f.physicalTo - f.physicalFrom));
        writeChannel(footprint, ch, dmx);
        any = true;
    }
    return any;
}

bool AttributeEncoder::setColor(std::span<std::uint8_t> footprint, const glm::vec3& linearRgb,
                                std::string_view geometry) const {
    const glm::vec3 c = glm::clamp(linearRgb, 0.0f, 1.0f);
    auto hasAttr = [&](Attribute a) {
        return std::any_of(controls_.begin(), controls_.end(), [&](const Control& k) {
            return k.attribute == a && (geometry.empty() || k.geometry == geometry);
        });
    };
    if (hasAttr(Attribute::ColorAdd_R) && hasAttr(Attribute::ColorAdd_G) && hasAttr(Attribute::ColorAdd_B)) {
        glm::vec3 rgb = c;
        float white = 0.0f;
        if (hasAttr(Attribute::ColorAdd_W)) {
            // Pull the common part into the white LEDs: brighter and closer to how desks mix.
            white = std::min(rgb.r, std::min(rgb.g, rgb.b));
            rgb -= glm::vec3(white);
            setPhysical(footprint, Attribute::ColorAdd_W, white, geometry);
        }
        setPhysical(footprint, Attribute::ColorAdd_R, rgb.r, geometry);
        setPhysical(footprint, Attribute::ColorAdd_G, rgb.g, geometry);
        setPhysical(footprint, Attribute::ColorAdd_B, rgb.b, geometry);
        // Other emitters off so the picked colour is what you get.
        for (int a = static_cast<int>(Attribute::ColorAdd_C); a <= static_cast<int>(Attribute::ColorAdd_Indigo); ++a) {
            const auto attr = static_cast<Attribute>(a);
            if (attr != Attribute::ColorAdd_W && hasAttr(attr)) setPhysical(footprint, attr, 0.0f, geometry);
        }
        return true;
    }
    if (hasAttr(Attribute::ColorSub_C) || hasAttr(Attribute::ColorSub_M) || hasAttr(Attribute::ColorSub_Y)) {
        const float peak = std::max(c.r, std::max(c.g, c.b));
        const glm::vec3 n = peak > 1e-6f ? c / peak : glm::vec3(1.0f);
        setPhysical(footprint, Attribute::ColorSub_C, 1.0f - n.r, geometry);
        setPhysical(footprint, Attribute::ColorSub_M, 1.0f - n.g, geometry);
        setPhysical(footprint, Attribute::ColorSub_Y, 1.0f - n.b, geometry);
        return true;
    }
    return false;
}

void AttributeEncoder::writeDefaults(std::span<std::uint8_t> footprint) const {
    for (const Channel& ch : mode_.channels) writeChannel(footprint, ch, ch.defaultValue);
}

void AttributeEncoder::writeHighlight(std::span<std::uint8_t> footprint) const {
    for (const Channel& ch : mode_.channels) writeChannel(footprint, ch, ch.highlightValue.value_or(ch.defaultValue));
}

}  // namespace dmxviz::fixtures
