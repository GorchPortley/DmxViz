#include "ui/fixture_editor/ChannelEditing.h"

#include "assets/ImageLoader.h"
#include "assets/ModelLoader.h"
#include "fixtures/FixtureAssets.h"
#include "ui/fixture_editor/GeometryEditing.h"

#include <algorithm>
#include <cctype>
#include <format>
#include <fstream>
#include <iterator>
#include <set>

namespace dmxviz::ui::fixture_editor {

using fixtures::Attribute;
using fixtures::Channel;
using fixtures::ChannelFunction;
using fixtures::DmxMode;
using fixtures::FunctionKind;

namespace {

constexpr std::uintmax_t kMaxResourceBytes = 16u * 1024u * 1024u;

std::uint32_t rescaleValue(std::uint32_t value, std::uint32_t oldMax, std::uint32_t newMax) {
    if (oldMax == 0 || oldMax == newMax) return value;
    const std::uint64_t scaled = static_cast<std::uint64_t>(value) * newMax / oldMax;
    return static_cast<std::uint32_t>(std::min<std::uint64_t>(scaled, newMax));
}

void growFootprint(DmxMode& mode) {
    mode.footprint = std::max(mode.footprint, mode.highestOffset());
}

std::string lowerCase(std::string text) {
    std::transform(text.begin(), text.end(), text.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return text;
}

std::optional<std::vector<std::uint8_t>> readWholeFile(const std::filesystem::path& file, std::string* error) {
    std::error_code ec;
    const std::uintmax_t size = std::filesystem::file_size(file, ec);
    if (ec) {
        if (error) *error = "cannot read " + file.string();
        return std::nullopt;
    }
    if (size > kMaxResourceBytes) {
        if (error) *error = "file is larger than 16 MB";
        return std::nullopt;
    }
    std::ifstream in(file, std::ios::binary);
    std::vector<std::uint8_t> bytes((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
    if (bytes.empty()) {
        if (error) *error = "file is empty";
        return std::nullopt;
    }
    return bytes;
}

std::string formatOf(const std::filesystem::path& file) {
    std::string ext = lowerCase(file.extension().string());
    if (!ext.empty() && ext.front() == '.') ext.erase(0, 1);
    if (ext == "jpeg") ext = "jpg";
    return ext;
}

}  // namespace

std::string uniqueName(std::string_view base, const std::vector<std::string>& existing) {
    auto taken = [&](const std::string& name) { return std::find(existing.begin(), existing.end(), name) != existing.end(); };
    std::string candidate(base);
    for (int n = 2; taken(candidate); ++n) candidate = std::format("{} {}", base, n);
    return candidate;
}

// ---------------------------------------------------------------------------
// Channels

std::uint32_t maxDmxValue(int bytes) {
    bytes = std::clamp(bytes, 1, 3);
    return (1u << (8 * bytes)) - 1u;
}

ChannelFunction makeFunction(Attribute attribute, std::uint32_t dmxFrom, std::uint32_t dmxTo) {
    const fixtures::AttributeInfo& info = fixtures::attributeInfo(attribute);
    ChannelFunction f;
    f.attribute = attribute;
    f.kind = info.defaultKind;
    f.dmxFrom = dmxFrom;
    f.dmxTo = dmxTo;
    f.physicalFrom = info.defaultFrom;
    f.physicalTo = info.defaultTo;
    return f;
}

Channel makeChannel(std::string name, Attribute attribute, int bytes, int firstOffset, std::string geometry) {
    Channel channel;
    channel.name = std::move(name);
    channel.geometry = std::move(geometry);
    bytes = std::clamp(bytes, 1, 3);
    for (int i = 0; i < bytes; ++i) channel.offsets.push_back(static_cast<std::uint16_t>(firstOffset + i));
    channel.functions.push_back(makeFunction(attribute, 0, maxDmxValue(bytes)));
    return channel;
}

Channel& appendChannel(DmxMode& mode, Channel channel) {
    const int bytes = std::clamp(static_cast<int>(channel.offsets.size()), 1, 3);
    const int first = mode.highestOffset() + 1;
    channel.offsets.clear();
    for (int i = 0; i < bytes; ++i) channel.offsets.push_back(static_cast<std::uint16_t>(first + i));
    mode.channels.push_back(std::move(channel));
    growFootprint(mode);
    return mode.channels.back();
}

void updateFootprint(DmxMode& mode) {
    mode.footprint = mode.highestOffset();
}

void renumberOffsets(DmxMode& mode) {
    int next = 1;
    for (Channel& channel : mode.channels)
        for (std::uint16_t& offset : channel.offsets) offset = static_cast<std::uint16_t>(next++);
    updateFootprint(mode);
}

void setChannelBytes(DmxMode& mode, std::size_t channelIndex, int bytes) {
    if (channelIndex >= mode.channels.size()) return;
    Channel& channel = mode.channels[channelIndex];
    bytes = std::clamp(bytes, 1, 3);
    if (channel.offsets.empty() || channel.byteCount() == bytes) return;  // virtual channels stay virtual

    const std::uint32_t oldMax = channel.maxValue();
    if (static_cast<int>(channel.offsets.size()) > bytes) {
        channel.offsets.resize(static_cast<std::size_t>(bytes));
    } else {
        while (static_cast<int>(channel.offsets.size()) < bytes)
            channel.offsets.push_back(static_cast<std::uint16_t>(mode.highestOffset() + 1));
    }
    const std::uint32_t newMax = channel.maxValue();

    channel.defaultValue = rescaleValue(channel.defaultValue, oldMax, newMax);
    if (channel.highlightValue) channel.highlightValue = rescaleValue(*channel.highlightValue, oldMax, newMax);
    for (ChannelFunction& f : channel.functions) {
        f.dmxFrom = rescaleValue(f.dmxFrom, oldMax, newMax);
        f.dmxTo = rescaleValue(f.dmxTo, oldMax, newMax);
        for (fixtures::ChannelSet& set : f.sets) {
            set.dmxFrom = rescaleValue(set.dmxFrom, oldMax, newMax);
            set.dmxTo = rescaleValue(set.dmxTo, oldMax, newMax);
        }
    }
    growFootprint(mode);
}

void closeFunctionGaps(Channel& channel) {
    if (channel.functions.empty()) return;
    for (const ChannelFunction& f : channel.functions)
        if (!f.modeMaster.empty()) return;  // ranges of switched functions depend on the master: leave them

    const std::uint32_t max = channel.maxValue();
    std::stable_sort(channel.functions.begin(), channel.functions.end(),
                     [](const ChannelFunction& a, const ChannelFunction& b) { return a.dmxFrom < b.dmxFrom; });
    for (ChannelFunction& f : channel.functions) {
        f.dmxTo = std::min(f.dmxTo, max);
        f.dmxFrom = std::min(f.dmxFrom, f.dmxTo);
    }
    channel.functions.front().dmxFrom = 0;

    // `top` is the function that reaches furthest so far; it is stretched over every gap behind it.
    std::size_t top = 0;
    for (std::size_t i = 1; i < channel.functions.size(); ++i) {
        const std::uint32_t start = channel.functions[i].dmxFrom;
        if (start > channel.functions[top].dmxTo + 1) channel.functions[top].dmxTo = start - 1;
        if (channel.functions[i].dmxTo > channel.functions[top].dmxTo) top = i;
    }
    channel.functions[top].dmxTo = max;
}

void fillWheelFunctions(Channel& channel, const fixtures::Wheel& wheel, Attribute attribute) {
    channel.functions.clear();
    const std::uint32_t max = channel.maxValue();
    const std::uint32_t count = static_cast<std::uint32_t>(std::clamp<std::size_t>(wheel.slots.size(), 1, max + 1));
    const std::uint32_t step = (max + 1) / count;
    for (std::uint32_t i = 0; i < count; ++i) {
        ChannelFunction f = makeFunction(attribute, i * step, i + 1 == count ? max : (i + 1) * step - 1);
        f.kind = FunctionKind::WheelSlot;
        f.wheel = wheel.name;
        f.slotFrom = f.slotTo = static_cast<float>(i + 1);
        f.name = i < wheel.slots.size() && !wheel.slots[i].name.empty() ? wheel.slots[i].name
                                                                          : std::format("Slot {}", i + 1);
        channel.functions.push_back(std::move(f));
    }
}

void addCellChannels(fixtures::FixtureType& type, DmxMode& mode, const std::vector<std::string>& cells,
                     const std::vector<Attribute>& attributes, bool perCell, const std::string& groupName) {
    if (cells.empty() || attributes.empty()) return;
    auto channelNames = [&mode]() {
        std::vector<std::string> names;
        for (const Channel& c : mode.channels) names.push_back(c.name);
        return names;
    };

    if (perCell) {
        for (const std::string& cell : cells)
            for (Attribute attribute : attributes) {
                const std::string label = std::format("{} {}", cell, fixtures::attributeInfo(attribute).pretty);
                appendChannel(mode, makeChannel(uniqueName(label, channelNames()), attribute, 1, 1, cell));
            }
        return;
    }

    std::string group = groupName.empty() ? "All Cells" : groupName;
    group = uniqueGeometryName(type, group);
    type.geometryGroups.push_back({group, cells});
    for (Attribute attribute : attributes) {
        const std::string label(fixtures::attributeInfo(attribute).pretty);
        appendChannel(mode, makeChannel(uniqueName(label, channelNames()), attribute, 1, 1, group));
    }
}

DmxMode duplicateMode(const fixtures::FixtureType& type, const DmxMode& mode) {
    std::vector<std::string> names;
    for (const DmxMode& m : type.modes) names.push_back(m.name);
    DmxMode copy = mode;
    copy.name = uniqueName(mode.name + " copy", names);
    return copy;
}

// ---------------------------------------------------------------------------
// Wheels

fixtures::WheelSlot makeSlot(fixtures::SlotKind kind, int slotNumber) {
    using fixtures::SlotKind;
    fixtures::WheelSlot slot;
    slot.kind = kind;
    switch (kind) {
        case SlotKind::Open: slot.name = "Open"; break;
        case SlotKind::Color:
            slot.name = std::format("Color {}", slotNumber);
            slot.color = glm::vec3(1.0f, 0.15f, 0.15f);
            break;
        case SlotKind::Gobo: slot.name = std::format("Gobo {}", slotNumber); break;
        case SlotKind::Prism:
            slot.name = "Prism";
            slot.facets = fixtures::makeCircularPrismFacets(3, degToRad(6.0f));
            break;
        case SlotKind::AnimationWheel: slot.name = std::format("Animation {}", slotNumber); break;
        case SlotKind::Frost:
            slot.name = "Frost";
            slot.frost = 0.6f;
            break;
    }
    return slot;
}

bool renameWheel(fixtures::FixtureType& type, std::size_t wheelIndex, const std::string& newName) {
    if (wheelIndex >= type.wheels.size() || newName.empty()) return false;
    const std::string oldName = type.wheels[wheelIndex].name;
    if (oldName == newName) return true;
    for (const fixtures::Wheel& w : type.wheels)
        if (w.name == newName) return false;
    type.wheels[wheelIndex].name = newName;
    for (DmxMode& mode : type.modes)
        for (Channel& channel : mode.channels)
            for (ChannelFunction& f : channel.functions)
                if (f.wheel == oldName) f.wheel = newName;
    return true;
}

// ---------------------------------------------------------------------------
// Resources

std::optional<ImportedFile> readImageFile(const std::filesystem::path& file, std::string* error) {
    ImportedFile out;
    out.format = formatOf(file);
    if (out.format != "png" && out.format != "jpg" && out.format != "svg") {
        if (error) *error = "only PNG, JPG and SVG images are supported";
        return std::nullopt;
    }
    std::optional<std::vector<std::uint8_t>> bytes = readWholeFile(file, error);
    if (!bytes) return std::nullopt;
    assets::ImageLoadOptions options;
    options.desiredChannels = 1;
    options.svgSize = 64;  // just a decode check
    if (!assets::loadImageFromMemory(*bytes, out.format, options, error)) return std::nullopt;
    out.name = file.stem().string();
    out.data = std::move(*bytes);
    return out;
}

std::optional<ImportedFile> readModelFile(const std::filesystem::path& file, std::string* error) {
    ImportedFile out;
    out.format = formatOf(file);
    if (!fixtures::isModelFormat(out.format)) {
        if (error) *error = "only GLB, GLTF, OBJ and 3DS models are supported";
        return std::nullopt;
    }
    std::optional<std::vector<std::uint8_t>> bytes = readWholeFile(file, error);
    if (!bytes) return std::nullopt;
    if (out.format == "gltf") {
        // A .gltf usually points at a separate .bin: only a self-contained file is useful here.
        if (!assets::loadModelFromMemory(*bytes, out.format, {}, error)) return std::nullopt;
    }
    out.name = file.stem().string();
    out.data = std::move(*bytes);
    return out;
}

std::string addResource(fixtures::FixtureType& type, std::string_view suggestedName, std::string_view format,
                        std::vector<std::uint8_t> data) {
    std::vector<std::string> names;
    for (const fixtures::Resource& r : type.resources) names.push_back(r.name);
    const std::string name = uniqueName(suggestedName.empty() ? "resource" : suggestedName, names);
    type.resources.push_back({name, std::string(format), std::move(data)});
    return name;
}

int removeUnusedResources(fixtures::FixtureType& type) {
    std::set<std::string, std::less<>> used;
    for (const fixtures::Wheel& wheel : type.wheels)
        for (const fixtures::WheelSlot& slot : wheel.slots)
            if (!slot.image.empty()) used.insert(slot.image);
    fixtures::forEachGeometry(type.geometry, [&](const fixtures::Geometry& g, const fixtures::Geometry*) {
        if (!g.model.mesh.empty()) used.insert(g.model.mesh);
    });
    const std::size_t before = type.resources.size();
    std::erase_if(type.resources, [&](const fixtures::Resource& r) { return !used.contains(r.name); });
    return static_cast<int>(before - type.resources.size());
}

}  // namespace dmxviz::ui::fixture_editor
