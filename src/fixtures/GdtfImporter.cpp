#include "fixtures/GdtfImporter.h"

#include "core/Log.h"
#include "core/SceneTypes.h"
#include "fixtures/Archive.h"
#include "fixtures/ColorMath.h"
#include "fixtures/DmxValue.h"

#include <pugixml.hpp>

#include <algorithm>
#include <cctype>
#include <charconv>
#include <cmath>
#include <format>
#include <map>
#include <set>

namespace dmxviz::fixtures {
namespace {

namespace fs = std::filesystem;

// Safety limits against malicious or broken files (reference cycles, huge expansions).
constexpr int kMaxGeometryDepth = 24;
constexpr int kMaxGeometryCount = 20000;

// ======================================================== text helpers

std::string attr(const pugi::xml_node& n, const char* name) {
    return n.attribute(name).as_string();
}

std::string lowerCase(std::string_view s) {
    std::string out(s);
    for (char& c : out) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    return out;
}

bool equalsNoCase(std::string_view a, std::string_view b) {
    return lowerCase(a) == lowerCase(b);
}
bool containsNoCase(std::string_view haystack, std::string_view needle) {
    return lowerCase(haystack).find(lowerCase(needle)) != std::string::npos;
}

std::string_view trim(std::string_view s) {
    while (!s.empty() && std::isspace(static_cast<unsigned char>(s.front()))) s.remove_prefix(1);
    while (!s.empty() && std::isspace(static_cast<unsigned char>(s.back()))) s.remove_suffix(1);
    return s;
}

std::optional<double> parseNumber(std::string_view text) {
    text = trim(text);
    if (!text.empty() && text.front() == '+') text.remove_prefix(1);
    if (text.empty()) return std::nullopt;
    double value = 0.0;
    const auto result = std::from_chars(text.data(), text.data() + text.size(), value);
    if (result.ec != std::errc()) return std::nullopt;
    return value;
}

double numberAttr(const pugi::xml_node& n, const char* name, double fallback) {
    return parseNumber(n.attribute(name).as_string()).value_or(fallback);
}

std::vector<std::string_view> splitList(std::string_view text, char separator) {
    std::vector<std::string_view> parts;
    std::size_t start = 0;
    while (start <= text.size()) {
        const std::size_t end = text.find(separator, start);
        parts.push_back(trim(text.substr(start, end == std::string_view::npos ? end : end - start)));
        if (end == std::string_view::npos) break;
        start = end + 1;
    }
    return parts;
}

std::string joinName(const std::string& prefix, const std::string& name) {
    return prefix.empty() ? name : prefix + " " + name;
}

// Last component of a GDTF node link ("Emitters.Red" -> "Red").
std::string lastLinkPart(const std::string& link) {
    const std::size_t dot = link.rfind('.');
    return dot == std::string::npos ? link : link.substr(dot + 1);
}

std::string extensionOf(std::string_view path) {
    const std::size_t dot = path.rfind('.');
    const std::size_t slash = path.find_last_of('/');
    if (dot == std::string_view::npos || (slash != std::string_view::npos && dot < slash)) return {};
    return lowerCase(path.substr(dot + 1));
}

// ======================================================== DMX values

// "128/1": value and the number of bytes it is expressed in.
struct DmxSpec {
    std::uint32_t value = 0;
    int bytes = 1;
};

std::optional<DmxSpec> parseDmxSpec(std::string_view text, int defaultBytes) {
    text = trim(text);
    if (text.empty() || equalsNoCase(text, "None")) return std::nullopt;
    const std::size_t slash = text.find('/');
    auto value = parseNumber(text.substr(0, slash));
    if (!value) return std::nullopt;
    DmxSpec spec;
    spec.value = static_cast<std::uint32_t>(std::clamp(*value, 0.0, 4294967295.0));
    spec.bytes = defaultBytes;
    if (slash != std::string_view::npos)
        if (auto bytes = parseNumber(text.substr(slash + 1))) spec.bytes = std::clamp(static_cast<int>(*bytes), 1, 4);
    return spec;
}

std::uint32_t inChannelResolution(const DmxSpec& v, int channelBytes, bool rangeEnd = false) {
    return convertDmxResolution(v.value, v.bytes, channelBytes, rangeEnd);
}

// ======================================================== geometry math

// GDTF (Z up, beam along -Z) -> DmxViz (Y up, beam along -Y): (x, y, z) -> (x, z, -y).
// That is a rotation of -90 degrees about X, so positions, rotations and
// directions all convert with the same quaternion.
const glm::quat& frameConversion() {
    static const glm::quat q = glm::angleAxis(-kPi * 0.5f, glm::vec3(1.0f, 0.0f, 0.0f));
    return q;
}

// "{1,0,0,0}{0,1,0,0}{0,0,1,0}{0,0,0,1}" -> list of number groups.
std::vector<std::vector<double>> parseMatrixGroups(std::string_view text) {
    std::vector<std::vector<double>> groups;
    std::size_t i = 0;
    while (i < text.size()) {
        if (text[i] != '{') {
            ++i;
            continue;
        }
        const std::size_t end = text.find('}', i);
        if (end == std::string_view::npos) break;
        std::vector<double> numbers;
        for (std::string_view part : splitList(text.substr(i + 1, end - i - 1), ','))
            numbers.push_back(parseNumber(part).value_or(0.0));
        groups.push_back(std::move(numbers));
        i = end + 1;
    }
    return groups;
}

// A GDTF Matrix (4 groups: basis vectors u, v, w and the origin, in metres) or
// Rotation (3 groups) in GDTF axes.
struct GdtfTransform {
    glm::vec3 translation{0.0f};
    glm::mat3 rotation{1.0f};
    bool valid = false;
    bool scaled = false;  // the basis vectors were not unit length; scale is dropped
};

GdtfTransform parseTransform(std::string_view text) {
    GdtfTransform t;
    const auto groups = parseMatrixGroups(text);
    if (groups.size() != 3 && groups.size() != 4) return t;
    for (const auto& g : groups)
        if (g.size() < 3) return t;

    auto vec = [](const std::vector<double>& g) {
        return glm::vec3(static_cast<float>(g[0]), static_cast<float>(g[1]), static_cast<float>(g[2]));
    };
    glm::vec3 u = vec(groups[0]), v = vec(groups[1]), w = vec(groups[2]);
    if (groups.size() == 4) {
        glm::vec3 origin = vec(groups[3]);
        // Some exporters write the matrix row-major with the translation in the fourth
        // column: last row (0,0,0,1) and non-zero fourth entries in the first three rows.
        const bool fourWide = groups[0].size() >= 4 && groups[1].size() >= 4 && groups[2].size() >= 4;
        const bool lastRowIsHomogeneous =
            glm::length(origin) < 1e-9f && groups[3].size() >= 4 && std::abs(groups[3][3] - 1.0) < 1e-6;
        if (fourWide && lastRowIsHomogeneous && (groups[0][3] != 0.0 || groups[1][3] != 0.0 || groups[2][3] != 0.0)) {
            origin = glm::vec3(static_cast<float>(groups[0][3]), static_cast<float>(groups[1][3]),
                               static_cast<float>(groups[2][3]));
            const glm::vec3 r0 = u, r1 = v, r2 = w;  // these were rows of the rotation matrix
            u = {r0.x, r1.x, r2.x};
            v = {r0.y, r1.y, r2.y};
            w = {r0.z, r1.z, r2.z};
        }
        t.translation = origin;
    }

    const float lu = glm::length(u), lv = glm::length(v), lw = glm::length(w);
    if (lu < 1e-6f || lv < 1e-6f || lw < 1e-6f) return t;
    t.scaled = std::abs(lu - 1.0f) > 0.01f || std::abs(lv - 1.0f) > 0.01f || std::abs(lw - 1.0f) > 0.01f;
    u /= lu;
    v -= glm::dot(v, u) * u;  // Gram-Schmidt: keep a proper rotation even with rounded input
    if (glm::length(v) < 1e-6f) return t;
    v = glm::normalize(v);
    w = glm::cross(u, v);
    t.rotation = glm::mat3(u, v, w);
    t.valid = true;
    return t;
}

// ======================================================== colour

// "0.3127,0.3290,100.0": CIE xyY with Y in percent.
std::optional<glm::vec3> parseXyY(std::string_view text) {
    const auto parts = splitList(text, ',');
    if (parts.size() < 3) return std::nullopt;
    auto x = parseNumber(parts[0]), y = parseNumber(parts[1]), Y = parseNumber(parts[2]);
    if (!x || !y || !Y) return std::nullopt;
    return glm::vec3(static_cast<float>(*x), static_cast<float>(*y), static_cast<float>(*Y));
}

// Transmission colour of a wheel slot: 1 = passes everything.
glm::vec3 slotTransmission(const glm::vec3& xyY) {
    const glm::vec3 rgb = xyYToLinear(xyY.x, xyY.y, xyY.z / 100.0f);
    return glm::clamp(rgb, glm::vec3(0.0f), glm::vec3(1.0f));
}

bool nearlyWhite(const glm::vec3& c) {
    return c.r > 0.97f && c.g > 0.97f && c.b > 0.97f;
}

// ======================================================== internal records

struct ModelInfo {
    PrimitiveShape primitive = PrimitiveShape::Box;
    glm::vec3 size{0.0f};  // DmxViz axes, metres
    std::string resource;  // mesh resource name, empty = none
};

// One occurrence of a GDTF geometry in the expanded tree. Channels refer to the
// geometry by its GDTF name, so each copy made for a GeometryReference becomes an instance.
struct GeometryInstance {
    std::string name;  // name in the expanded tree
    int dmxBase = 0;   // added to the channels' Offsets (0 = first slot of the mode)
};

// The geometry tree of one mode root plus where every GDTF geometry ended up in it.
struct Expansion {
    std::string rootName;
    Geometry tree;
    std::map<std::string, std::vector<GeometryInstance>, std::less<>> instances;
};

struct SetDef {
    std::string name;
    std::uint32_t from = 0;  // channel resolution
    int slotIndex = 0;       // 1-based wheel slot, 0 = none
    bool hasPhysical = false;
    double physicalFrom = 0.0, physicalTo = 0.0;
};

// A ChannelFunction as written in the file, before its DMX end is known.
struct FunctionDef {
    std::string name, logicalName, attributeName, wheel, emitter, modeMasterLink;
    std::uint32_t from = 0, dflt = 0;  // channel resolution
    double physicalFrom = 0.0, physicalTo = 1.0;
    double realFade = 0.0, realAcceleration = 0.0;
    std::optional<DmxSpec> modeFrom, modeTo;
    std::vector<SetDef> sets;
};

// A function's mode master: it only applies while the linked channel's value is in [from, to].
// Bytes of 0 mean "in the master channel's own resolution".
struct PendingMaster {
    std::string link;
    std::uint32_t from = 0, to = 0;
    int fromBytes = 0, toBytes = 0;
};

// A DMXChannel before GeometryReference expansion: offsets are relative to its geometry's start.
struct ChannelTemplate {
    std::string geometry;  // GDTF geometry name
    std::string suffix;    // attribute name used for the channel name
    std::vector<std::uint16_t> offsets;
    std::uint32_t defaultValue = 0;
    std::optional<std::uint32_t> highlight;
    std::vector<ChannelFunction> functions;
    std::vector<std::optional<PendingMaster>> masters;  // parallel to functions
    std::string baseName;                               // "Geometry_Attribute", as GDTF links name channels
};

// ======================================================== importer

class GdtfImport {
public:
    GdtfImport(std::span<const std::uint8_t> archive, std::vector<std::string>* warnings)
        : archive_(archive.begin(), archive.end()), warnings_(warnings) {}

    std::optional<FixtureType> run(std::string* error);

private:
    void warn(std::string message) {
        log::warn("fixtures", "GDTF {}: {}", t_.id, message);
        if (warnings_) warnings_->push_back(std::move(message));
    }

    void readMeta(const pugi::xml_node& root, const pugi::xml_node& ft);
    void readAttributeDefinitions(const pugi::xml_node& ft);
    void readPhysical(const pugi::xml_node& ft);
    void scanWheelUsage(const pugi::xml_node& ft);
    void readWheels(const pugi::xml_node& ft);
    void detectMillimetres(const pugi::xml_node& ft);
    void readModels(const pugi::xml_node& ft);
    void buildGeometry(const pugi::xml_node& ft);
    bool readModes(const pugi::xml_node& ft, std::string* error);
    void finish();

    // resources
    std::string mediaResource(const std::string& mediaName);
    std::string modelResource(const std::string& file);
    std::string addResource(std::string name, std::string format, std::vector<std::uint8_t> data);

    // geometry
    void collectGeometryNodes(const pugi::xml_node& parent);
    Geometry expandGeometry(const pugi::xml_node& node, const std::string& fullName, const std::string& position,
                            const std::string& childPrefix, int dmxBase, Expansion& expansion, int depth);
    void expandReference(Geometry& parent, const pugi::xml_node& ref, const std::string& childPrefix, int dmxBase,
                         Expansion& expansion, int depth);
    BeamSpec readBeam(const pugi::xml_node& node);
    std::string uniqueGeometryName(const std::string& wanted);
    std::string rootGeometryFor(const pugi::xml_node& mode) const;

    // modes
    std::optional<ChannelTemplate> readChannel(const pugi::xml_node& node, std::string* skipReason);
    FunctionDef readFunction(const pugi::xml_node& node, const std::string& logicalName, int bytes);
    void appendFunctions(ChannelTemplate& channel, const FunctionDef& def, std::uint32_t from, std::uint32_t to);
    void convertPhysical(ChannelFunction& f, double from, double to) const;
    void noteMovement(const FunctionDef& def, const ChannelFunction& f);
    DmxMode buildMode(const pugi::xml_node& modeNode, int index);
    void resolveModeMasters(DmxMode& mode, const std::vector<ChannelTemplate>& templates,
                            const std::vector<std::vector<std::size_t>>& channelsOfTemplate);

    // data
    std::vector<std::uint8_t> archive_;
    std::vector<std::string>* warnings_;
    ZipReader zip_;
    FixtureType t_;

    std::set<std::string, std::less<>> definedAttributes_;
    std::set<std::string, std::less<>> unrenderedAttributes_;
    std::map<std::string, glm::vec3, std::less<>> filters_;  // name -> xyY
    std::map<std::string, AttributeFamily, std::less<>> wheelFamily_;
    std::map<std::string, std::string, std::less<>> mediaResources_;  // MediaFileName -> resource name
    std::map<std::string, std::string, std::less<>> modelResources_;  // File -> resource name
    std::map<std::string, ModelInfo, std::less<>> models_;

    std::map<std::string, pugi::xml_node, std::less<>> geometryNodes_;
    pugi::xml_node geometriesNode_;
    std::set<std::string, std::less<>> usedGeometryNames_;
    std::vector<Expansion> expansions_;
    std::map<std::string, std::size_t, std::less<>> expansionOfRoot_;
    int geometryCount_ = 0;
    float baseKelvin_ = 0.0f;   // colour temperature of the first beam (CTO/CTB reference)
    float lengthScale_ = 1.0f;  // file length unit in metres: 1, or 0.001 for files that wrongly use millimetres
    bool movementSet_[2] = {false, false};
    pugi::xml_document doc_;
};

// -------------------------------------------------------------- run

std::optional<FixtureType> GdtfImport::run(std::string* error) {
    auto fail = [&](std::string message) -> std::optional<FixtureType> {
        if (error) *error = std::move(message);
        return std::nullopt;
    };

    std::string zipError;
    if (!zip_.open(archive_, &zipError)) return fail("not a GDTF (zip) archive: " + zipError);
    auto description = zip_.read("description.xml");
    if (!description) {
        if (auto name = zip_.findCaseInsensitive("description.xml")) description = zip_.read(*name);
    }
    if (!description) return fail("archive has no description.xml");

    const pugi::xml_parse_result parsed =
        doc_.load_buffer(description->data(), description->size(), pugi::parse_default, pugi::encoding_auto);
    if (!parsed)
        return fail(std::format("description.xml: invalid XML: {} (offset {})", parsed.description(), parsed.offset));

    const pugi::xml_node root = doc_.child("GDTF");
    const pugi::xml_node ft = root.child("FixtureType");
    if (!root || !ft) return fail("description.xml: no <GDTF><FixtureType> element");

    readMeta(root, ft);
    readAttributeDefinitions(ft);
    readPhysical(ft);
    scanWheelUsage(ft);
    readWheels(ft);
    detectMillimetres(ft);
    readModels(ft);
    buildGeometry(ft);
    if (!readModes(ft, error)) return std::nullopt;
    finish();
    return std::move(t_);
}

// -------------------------------------------------------------- meta

void GdtfImport::readMeta(const pugi::xml_node& root, const pugi::xml_node& ft) {
    t_.source = FixtureSource::Gdtf;
    t_.name = attr(ft, "Name");
    if (t_.name.empty()) t_.name = attr(ft, "LongName");
    if (t_.name.empty()) t_.name = attr(ft, "ShortName");
    if (t_.name.empty()) t_.name = "GDTF fixture";
    t_.shortName = attr(ft, "ShortName");
    t_.manufacturer = attr(ft, "Manufacturer");
    t_.description = attr(ft, "Description");
    t_.id = makeFixtureId(t_.manufacturer, t_.name);

    for (const pugi::xml_node& rev : ft.child("Revisions").children("Revision")) t_.revision = attr(rev, "Text");

    const std::string version = attr(root, "DataVersion");
    if (version.empty()) {
        warn("no DataVersion attribute; assuming GDTF 1.1");
    } else if (!version.starts_with("1.0") && !version.starts_with("1.1") && !version.starts_with("1.2")) {
        warn(std::format("GDTF version {} is newer than the supported 1.2; importing what is understood", version));
    }
}

void GdtfImport::readAttributeDefinitions(const pugi::xml_node& ft) {
    for (const pugi::xml_node& a : ft.child("AttributeDefinitions").child("Attributes").children("Attribute"))
        definedAttributes_.insert(attr(a, "Name"));
}

void GdtfImport::readPhysical(const pugi::xml_node& ft) {
    const pugi::xml_node physical = ft.child("PhysicalDescriptions");
    for (const pugi::xml_node& e : physical.child("Emitters").children("Emitter")) {
        Emitter emitter;
        emitter.name = attr(e, "Name");
        if (emitter.name.empty() || t_.findEmitter(emitter.name)) continue;
        glm::vec3 color{1.0f};
        if (auto xyY = parseXyY(attr(e, "Color"))) {
            // Hue only: the largest component is 1 whatever the measured luminance was.
            const glm::vec3 rgb = xyYToLinear(xyY->x, xyY->y, 1.0f);
            if (maxComponent(rgb) > 0.0f) color = normalizeMax(rgb);
        }
        emitter.color = color;
        emitter.dominantWavelength = static_cast<float>(numberAttr(e, "DominantWaveLength", 0.0));
        t_.emitters.push_back(std::move(emitter));
    }
    for (const pugi::xml_node& f : physical.child("Filters").children("Filter"))
        if (auto xyY = parseXyY(attr(f, "Color"))) filters_[attr(f, "Name")] = *xyY;

    const pugi::xml_node props = physical.child("Properties");
    t_.physical.weight = static_cast<float>(numberAttr(props.child("Weight"), "Value", 0.0));
    t_.physical.power = static_cast<float>(numberAttr(props.child("PowerConsumption"), "Value", 0.0));
}

// -------------------------------------------------------------- wheels and resources

std::string GdtfImport::addResource(std::string name, std::string format, std::vector<std::uint8_t> data) {
    // Different files with the same slug must not share a name.
    const std::string base = name;
    for (int n = 2; t_.findResource(name); ++n) name = std::format("{}-{}", base, n);
    t_.resources.push_back({name, std::move(format), std::move(data)});
    return name;
}

std::string GdtfImport::mediaResource(const std::string& mediaName) {
    if (mediaName.empty()) return {};
    if (auto it = mediaResources_.find(mediaName); it != mediaResources_.end()) return it->second;

    std::vector<std::string> candidates;
    const std::string ext = extensionOf(mediaName);
    if (ext == "png" || ext == "svg" || ext == "jpg" || ext == "jpeg") {
        candidates.push_back("wheels/" + mediaName);
    } else {
        for (const char* e : {".png", ".svg", ".jpg"}) candidates.push_back("wheels/" + mediaName + e);
    }
    std::string resource;
    for (const std::string& candidate : candidates) {
        auto stored = zip_.findCaseInsensitive(candidate);
        if (!stored) continue;
        if (auto bytes = zip_.read(*stored)) {
            resource =
                addResource("gobo-" + slugify(ext.empty() ? mediaName : mediaName.substr(0, mediaName.rfind('.'))),
                            extensionOf(*stored), std::move(*bytes));
            break;
        }
    }
    if (resource.empty()) warn(std::format("wheel image \"{}\" not found in the archive", mediaName));
    mediaResources_[mediaName] = resource;
    return resource;
}

std::string GdtfImport::modelResource(const std::string& file) {
    if (file.empty()) return {};
    if (auto it = modelResources_.find(file); it != modelResources_.end()) return it->second;

    std::string stem = file;
    const std::string givenExt = extensionOf(file);
    if (givenExt == "glb" || givenExt == "gltf" || givenExt == "3ds") stem = file.substr(0, file.rfind('.'));

    // glTF is preferred over 3DS when an archive carries both.
    const std::string candidates[] = {"models/gltf/" + stem + ".glb", "models/gltf/" + stem + ".gltf",
                                      "models/3ds/" + stem + ".3ds", "models/" + stem + ".glb",
                                      "models/" + stem + ".3ds"};
    std::string resource;
    for (const std::string& candidate : candidates) {
        auto stored = zip_.findCaseInsensitive(candidate);
        if (!stored) continue;
        auto bytes = zip_.read(*stored);
        if (!bytes) continue;
        const std::string format = extensionOf(*stored);
        resource = addResource("model-" + slugify(stem), format, std::move(*bytes));
        if (format == "gltf") {
            // External buffers sit next to the .gltf and are found by name when the model is loaded.
            const std::string dir = stored->substr(0, stored->rfind('/') + 1);
            for (const std::string& entry : zip_.entries()) {
                if (extensionOf(entry) != "bin" || !lowerCase(entry).starts_with(lowerCase(dir))) continue;
                std::string binName = entry.substr(dir.size());
                binName.erase(binName.size() - 4);
                if (!t_.findResource(binName))
                    if (auto bin = zip_.read(entry)) t_.resources.push_back({binName, "bin", std::move(*bin)});
            }
        }
        break;
    }
    modelResources_[file] = resource;
    return resource;
}

// Which kind of attribute uses each wheel decides the kind of its slots
// (a slot with an image is a gobo on a gobo wheel but an animation frame on an animation wheel).
void GdtfImport::scanWheelUsage(const pugi::xml_node& ft) {
    for (const pugi::xml_node& mode : ft.child("DMXModes").children("DMXMode"))
        for (const pugi::xml_node& ch : mode.child("DMXChannels").children("DMXChannel"))
            for (const pugi::xml_node& lc : ch.children("LogicalChannel"))
                for (const pugi::xml_node& cf : lc.children("ChannelFunction")) {
                    const std::string wheel = attr(cf, "Wheel");
                    if (wheel.empty()) continue;
                    const AttributeFamily family = attributeFamily(parseAttribute(attr(cf, "Attribute")));
                    const bool wheelFamily = family == AttributeFamily::ColorWheel || family == AttributeFamily::Gobo ||
                                             family == AttributeFamily::Prism || family == AttributeFamily::Animation;
                    if (wheelFamily) wheelFamily_.emplace(wheel, family);
                }
}

void GdtfImport::readWheels(const pugi::xml_node& ft) {
    int wheelNumber = 0;
    for (const pugi::xml_node& wheelNode : ft.child("Wheels").children("Wheel")) {
        ++wheelNumber;
        Wheel wheel;
        wheel.name = attr(wheelNode, "Name");
        if (wheel.name.empty()) wheel.name = std::format("Wheel {}", wheelNumber);
        if (t_.findWheel(wheel.name)) {
            warn(std::format("duplicate wheel \"{}\" skipped", wheel.name));
            continue;
        }
        AttributeFamily family = AttributeFamily::Other;
        if (auto it = wheelFamily_.find(wheel.name); it != wheelFamily_.end()) family = it->second;

        int slotNumber = 0;
        for (const pugi::xml_node& slotNode : wheelNode.children("Slot")) {
            ++slotNumber;
            WheelSlot slot;
            slot.name = attr(slotNode, "Name");
            if (slot.name.empty()) slot.name = std::format("Slot {}", slotNumber);

            glm::vec3 color{1.0f};
            const std::string filter = attr(slotNode, "Filter");
            if (auto it = filters_.find(filter); !filter.empty() && it != filters_.end()) {
                color = slotTransmission(it->second);
            } else if (auto it2 = filters_.find(lastLinkPart(filter)); !filter.empty() && it2 != filters_.end()) {
                color = slotTransmission(it2->second);
            } else if (auto xyY = parseXyY(attr(slotNode, "Color"))) {
                color = slotTransmission(*xyY);
            }
            slot.color = color;

            // Prism facets: the facet's rotation tells where its sub-beam goes. The nominal
            // beam axis is -Z; the deflected axis is the rotated one. The facet contract is
            // x = right, y = up when looking along the beam. In DmxViz beam-local axes the beam
            // leaves along -Y with up = +Z, so right = direction x up = local -X: x is negated.
            for (const pugi::xml_node& facetNode : slotNode.children()) {
                const std::string_view tag = facetNode.name();
                if (tag != "Facet" && tag != "PrismFacet") continue;
                const GdtfTransform rotation = parseTransform(attr(facetNode, "Rotation"));
                glm::vec3 axis =
                    rotation.valid ? rotation.rotation * glm::vec3(0.0f, 0.0f, -1.0f) : glm::vec3(0.0f, 0.0f, -1.0f);
                axis.z = std::min(axis.z, -1e-3f);  // facets pointing backwards are clamped to the horizon
                slot.facets.emplace_back(-std::atan2(axis.x, -axis.z), std::atan2(-axis.y, -axis.z));
            }
            if (slot.facets.size() > static_cast<std::size_t>(kMaxPrismFacets)) {
                warn(std::format("wheel \"{}\": prism slot \"{}\" has more than {} facets; extra facets dropped",
                                 wheel.name, slot.name, kMaxPrismFacets));
                slot.facets.resize(static_cast<std::size_t>(kMaxPrismFacets));
            }

            const std::string media = attr(slotNode, "MediaFileName");
            const bool animation = family == AttributeFamily::Animation || slotNode.child("AnimationSystem");
            const bool openName = equalsNoCase(slot.name, "Open") || containsNoCase(slot.name, "open");
            if (!slot.facets.empty()) {
                slot.kind = SlotKind::Prism;
            } else if (!media.empty()) {
                slot.kind = animation ? SlotKind::AnimationWheel : SlotKind::Gobo;
                slot.image = mediaResource(media);
            } else if (animation) {
                slot.kind = SlotKind::AnimationWheel;
            } else if (containsNoCase(slot.name, "frost")) {
                slot.kind = SlotKind::Frost;
                slot.frost = 1.0f;
                slot.color = glm::vec3(1.0f);
            } else if (openName && nearlyWhite(color)) {
                slot.kind = SlotKind::Open;
            } else if (family != AttributeFamily::ColorWheel && family != AttributeFamily::Other &&
                       nearlyWhite(color)) {
                // A white slot without image on a gobo or prism wheel is an empty position.
                slot.kind = SlotKind::Open;
            } else {
                slot.kind = SlotKind::Color;
            }
            if (family == AttributeFamily::Prism && slot.kind == SlotKind::Open && !openName) {
                // A prism wheel slot without facet data: assume the common 3-facet prism.
                slot.kind = SlotKind::Prism;
                slot.facets = makeCircularPrismFacets(3, degToRad(5.0f));
                warn(std::format("wheel \"{}\": slot \"{}\" has no facet data; using a 3-facet prism", wheel.name,
                                 slot.name));
            }
            wheel.slots.push_back(std::move(slot));
        }
        if (wheel.slots.empty()) {
            warn(std::format("wheel \"{}\" has no slots; added an open slot", wheel.name));
            wheel.slots.push_back(WheelSlot{SlotKind::Open, "Open"});
        }
        t_.wheels.push_back(std::move(wheel));
    }
}

// -------------------------------------------------------------- models

// GDTF lengths are metres. A fixture that is several metres wide cannot be right, so a file
// with such numbers is taken to be in millimetres (a known exporter mistake) and scaled.
void GdtfImport::detectMillimetres(const pugi::xml_node& ft) {
    constexpr double kImplausible = 20.0;  // m
    double largest = 0.0;
    for (const pugi::xml_node& m : ft.child("Models").children("Model"))
        for (const char* key : {"Length", "Width", "Height"}) largest = std::max(largest, numberAttr(m, key, 0.0));
    if (largest <= kImplausible) {
        // Positions: look at the translation of every geometry element.
        auto scan = [&](auto&& self, const pugi::xml_node& parent) -> void {
            for (const pugi::xml_node& child : parent.children()) {
                if (child.type() != pugi::node_element) continue;
                const GdtfTransform transform = parseTransform(attr(child, "Position"));
                const glm::vec3 a = glm::abs(transform.translation);
                largest = std::max(largest, static_cast<double>(std::max({a.x, a.y, a.z})));
                self(self, child);
            }
        };
        scan(scan, ft.child("Geometries"));
    }
    if (largest > kImplausible) {
        lengthScale_ = 0.001f;
        warn(std::format("lengths up to {:.0f} look like millimetres; the file is read as millimetres", largest));
    }
}

void GdtfImport::readModels(const pugi::xml_node& ft) {
    for (const pugi::xml_node& m : ft.child("Models").children("Model")) {
        ModelInfo info;
        const std::string name = attr(m, "Name");
        // GDTF: Length along X, Width along Y, Height along Z (all metres); our Y is GDTF Z.
        const auto length = static_cast<float>(numberAttr(m, "Length", 0.0)) * lengthScale_;
        const auto width = static_cast<float>(numberAttr(m, "Width", 0.0)) * lengthScale_;
        const auto height = static_cast<float>(numberAttr(m, "Height", 0.0)) * lengthScale_;
        info.size = glm::vec3(length, height, width);

        const std::string primitive = lowerCase(attr(m, "PrimitiveType"));
        if (primitive == "cylinder")
            info.primitive = PrimitiveShape::Cylinder;
        else if (primitive == "sphere")
            info.primitive = PrimitiveShape::Sphere;
        else if (primitive == "base")
            info.primitive = PrimitiveShape::Base;
        else if (primitive == "yoke")
            info.primitive = PrimitiveShape::Yoke;
        else if (primitive == "head")
            info.primitive = PrimitiveShape::Head;
        else if (primitive == "conventional")
            info.primitive = PrimitiveShape::Conventional;
        else if (primitive == "pigtail")
            info.primitive = PrimitiveShape::None;  // a cable, not worth drawing
        else
            info.primitive = PrimitiveShape::Box;  // Cube, Plane, Scanner, Undefined and anything new

        info.resource = modelResource(attr(m, "File"));
        if (!attr(m, "File").empty() && info.resource.empty())
            warn(std::format("model file \"{}\" of model \"{}\" not found; using the primitive shape", attr(m, "File"),
                             name));
        models_.emplace(name, info);
    }
}

// -------------------------------------------------------------- geometry

// Geometry elements of a GDTF tree and the DmxViz type they become. Everything that is
// neither Axis nor Beam is a static part.
std::optional<GeometryType> geometryTypeOf(std::string_view element) {
    if (element == "Axis") return GeometryType::Axis;
    if (element == "Beam") return GeometryType::Beam;
    static const char* const generic[] = {
        "Geometry",         "FilterBeam",        "FilterColor",       "FilterGobo", "FilterShaper",
        "MediaServerLayer", "MediaServerCamera", "MediaServerMaster", "Display",    "Laser",
        "WiringObject",     "Inventory",         "Structure",         "Support",    "Magnet"};
    for (const char* name : generic)
        if (element == name) return GeometryType::Generic;
    return std::nullopt;
}

std::string GdtfImport::uniqueGeometryName(const std::string& wanted) {
    const std::string base = wanted.empty() ? std::string("Geometry") : wanted;
    std::string name = base;
    for (int n = 2; !usedGeometryNames_.insert(name).second; ++n) name = std::format("{} ({})", base, n);
    return name;
}

BeamSpec GdtfImport::readBeam(const pugi::xml_node& node) {
    BeamSpec b;
    const auto type = parseBeamType(lowerCase(attr(node, "BeamType")));
    b.type = type.value_or(BeamType::Wash);  // GDTF "None" and unknown types
    b.lensRadius = static_cast<float>(numberAttr(node, "BeamRadius", 0.05)) * lengthScale_;
    if (b.lensRadius <= 0.0f) b.lensRadius = 0.05f;
    b.beamAngle = degToRad(static_cast<float>(std::max(numberAttr(node, "BeamAngle", 25.0), 0.1)));
    b.fieldAngle = degToRad(static_cast<float>(numberAttr(node, "FieldAngle", 25.0)));
    b.fieldAngle = std::max(b.fieldAngle, b.beamAngle);  // the importer never emits an invalid pair
    b.luminousFlux = static_cast<float>(std::max(numberAttr(node, "LuminousFlux", 10000.0), 0.0));
    b.colorTemperature =
        std::clamp(static_cast<float>(numberAttr(node, "ColorTemperature", 6000.0)), 1000.0f, 25000.0f);
    if (b.type == BeamType::Rectangle) {
        const double ratio = std::max(numberAttr(node, "RectangleRatio", 1.7777), 0.1);
        const float r = 2.0f * b.lensRadius;
        const auto s = static_cast<float>(std::sqrt(ratio));
        b.emitterSize = glm::vec2(r * s, r / s);  // same area as the round lens, GDTF width : height ratio
    }
    if (baseKelvin_ <= 0.0f) baseKelvin_ = b.colorTemperature;
    return b;
}

void GdtfImport::collectGeometryNodes(const pugi::xml_node& parent) {
    for (const pugi::xml_node& child : parent.children()) {
        if (child.type() != pugi::node_element) continue;
        const std::string_view tag = child.name();
        if (tag == "GeometryReference" || !geometryTypeOf(tag)) continue;
        const std::string name = attr(child, "Name");
        if (!name.empty() && !geometryNodes_.emplace(name, child).second)
            warn(std::format("geometry name \"{}\" is used more than once", name));
        collectGeometryNodes(child);
    }
}

Geometry GdtfImport::expandGeometry(const pugi::xml_node& node, const std::string& fullName,
                                    const std::string& position, const std::string& childPrefix, int dmxBase,
                                    Expansion& expansion, int depth) {
    Geometry g;
    g.name = uniqueGeometryName(fullName);
    g.type = geometryTypeOf(node.name()).value_or(GeometryType::Generic);
    ++geometryCount_;

    const GdtfTransform transform = parseTransform(position);
    if (transform.valid) {
        if (transform.scaled) warn(std::format("geometry \"{}\": scaling in Position is ignored", g.name));
        g.position = frameConversion() * (transform.translation * lengthScale_);
        g.rotation = frameConversion() * glm::quat_cast(transform.rotation) * glm::inverse(frameConversion());
    } else if (!position.empty()) {
        warn(std::format("geometry \"{}\": cannot read Position \"{}\"", g.name, position));
    }

    const std::string modelName = attr(node, "Model");
    if (!modelName.empty()) {
        if (auto it = models_.find(modelName); it != models_.end()) {
            g.model.primitive = it->second.primitive;
            g.model.size = it->second.size;
            g.model.mesh = it->second.resource;
        } else {
            warn(std::format("geometry \"{}\": unknown model \"{}\"", g.name, modelName));
        }
    }
    if (g.type == GeometryType::Beam) g.beam = readBeam(node);

    expansion.instances[attr(node, "Name")].push_back({g.name, dmxBase});

    for (const pugi::xml_node& child : node.children()) {
        if (child.type() != pugi::node_element) continue;
        const std::string_view tag = child.name();
        if (geometryCount_ > kMaxGeometryCount || depth > kMaxGeometryDepth) {
            warn(std::format("geometry \"{}\": tree too deep or too large; the rest is skipped", g.name));
            break;
        }
        if (tag == "GeometryReference") {
            expandReference(g, child, childPrefix, dmxBase, expansion, depth);
        } else if (geometryTypeOf(tag)) {
            g.children.push_back(expandGeometry(child, joinName(childPrefix, attr(child, "Name")),
                                                attr(child, "Position"), childPrefix, dmxBase, expansion, depth + 1));
        } else if (tag != "Break") {
            warn(std::format("geometry \"{}\": unknown element <{}> skipped", g.name, tag));
        }
    }
    return g;
}

void GdtfImport::expandReference(Geometry& parent, const pugi::xml_node& ref, const std::string& childPrefix,
                                 int dmxBase, Expansion& expansion, int depth) {
    const std::string target = attr(ref, "Geometry");
    auto it = geometryNodes_.find(target);
    if (it == geometryNodes_.end()) {
        warn(std::format("geometry reference \"{}\" points to unknown geometry \"{}\"", attr(ref, "Name"), target));
        return;
    }
    std::string name = attr(ref, "Name");
    if (name.empty()) name = target;

    // The Break of the first DMX break gives where this copy's channels start (1 = first slot).
    int base = dmxBase;
    for (const pugi::xml_node& brk : ref.children("Break")) {
        const std::string breakNumber = attr(brk, "DMXBreak");
        if (!breakNumber.empty() && numberAttr(brk, "DMXBreak", 1.0) != 1.0) continue;
        base = dmxBase + std::max(static_cast<int>(numberAttr(brk, "DMXOffset", 1.0)), 1) - 1;
        break;
    }
    const std::string fullName = joinName(childPrefix, name);
    parent.children.push_back(
        expandGeometry(it->second, fullName, attr(ref, "Position"), fullName, base, expansion, depth + 1));
}

std::string GdtfImport::rootGeometryFor(const pugi::xml_node& mode) const {
    const std::string wanted = attr(mode, "Geometry");
    if (!wanted.empty() && geometryNodes_.contains(wanted)) return wanted;
    for (const pugi::xml_node& child : geometriesNode_.children())
        if (child.type() == pugi::node_element && child.name() != std::string_view("GeometryReference") &&
            geometryTypeOf(child.name()))
            return attr(child, "Name");
    return {};
}

void GdtfImport::buildGeometry(const pugi::xml_node& ft) {
    geometriesNode_ = ft.child("Geometries");
    collectGeometryNodes(geometriesNode_);

    // One expansion per distinct mode root (nearly always exactly one).
    std::vector<std::string> roots;
    for (const pugi::xml_node& mode : ft.child("DMXModes").children("DMXMode")) {
        const std::string root = rootGeometryFor(mode);
        if (root.empty()) {
            warn(std::format("mode \"{}\": its root geometry \"{}\" does not exist", attr(mode, "Name"),
                             attr(mode, "Geometry")));
            continue;
        }
        if (!expansionOfRoot_.contains(root)) {
            expansionOfRoot_[root] = roots.size();
            roots.push_back(root);
        }
    }
    if (roots.empty()) {
        const std::string first = rootGeometryFor(pugi::xml_node());
        if (!first.empty()) {
            expansionOfRoot_[first] = 0;
            roots.push_back(first);
        }
    }

    expansions_.reserve(roots.size());
    for (const std::string& root : roots) {
        Expansion& expansion = expansions_.emplace_back();
        expansion.rootName = root;
        const pugi::xml_node node = geometryNodes_.at(root);
        expansion.tree = expandGeometry(node, root, attr(node, "Position"), {}, 0, expansion, 0);
    }

    if (expansions_.empty()) {
        warn("the fixture has no geometry; using an empty body");
        t_.geometry.name = uniqueGeometryName("Body");
    } else if (expansions_.size() == 1) {
        t_.geometry = expansions_.front().tree;
    } else {
        // Modes with different root geometries: a neutral parent holds all of them and every
        // mode selects its subtree with geometryRoot.
        t_.geometry.name = uniqueGeometryName("Root");
        for (Expansion& e : expansions_) t_.geometry.children.push_back(e.tree);
    }
}

// -------------------------------------------------------------- DMX channels

void GdtfImport::convertPhysical(ChannelFunction& f, double from, double to) const {
    const AttributeInfo& info = attributeInfo(f.attribute);
    float a = static_cast<float>(from), b = static_cast<float>(to);
    const float largest = std::max(std::abs(a), std::abs(b));
    switch (info.unit) {
        case PhysicalUnit::Angle:
        case PhysicalUnit::AngularSpeed:
            a = physicalFromFile(f.attribute, a);
            b = physicalFromFile(f.attribute, b);
            break;
        case PhysicalUnit::Ratio:
            // Files use 0..1 or percent; focus may be a distance, so it is scaled to its largest value.
            if (largest > 1.0001f) {
                const float divisor =
                    info.family == AttributeFamily::Focus ? largest : (largest <= 100.0001f ? 100.0f : largest);
                a /= divisor;
                b /= divisor;
            }
            break;
        case PhysicalUnit::Temperature:
            // GDTF describes CTO / CTB / CTC as a 0..1 correction amount; the runtime wants Kelvin.
            if (largest <= 1.0001f) {
                const float base = baseKelvin_ > 0.0f ? baseKelvin_ : 6500.0f;
                auto kelvin = [&](float amount) {
                    const bool warm = f.attribute == Attribute::CTO || (f.attribute == Attribute::CTC && amount < 0.0f);
                    return base + ((warm ? 3200.0f : 8000.0f) - base) * std::abs(amount);
                };
                a = kelvin(a);
                b = kelvin(b);
            }
            break;
        case PhysicalUnit::None:
        case PhysicalUnit::Frequency:
        case PhysicalUnit::Time:
            break;
    }
    f.physicalFrom = a;
    f.physicalTo = b;
}

FunctionDef GdtfImport::readFunction(const pugi::xml_node& node, const std::string& logicalName, int bytes) {
    FunctionDef def;
    def.name = attr(node, "CustomName").empty() ? attr(node, "Name") : attr(node, "CustomName");
    def.logicalName = logicalName;
    def.attributeName = attr(node, "Attribute");
    if (def.attributeName.empty()) def.attributeName = "NoFeature";
    def.wheel = attr(node, "Wheel");
    def.emitter = attr(node, "Emitter");
    def.modeMasterLink = attr(node, "ModeMaster");
    def.modeFrom = parseDmxSpec(attr(node, "ModeFrom"), 0);
    def.modeTo = parseDmxSpec(attr(node, "ModeTo"), 0);
    def.from = inChannelResolution(parseDmxSpec(attr(node, "DMXFrom"), bytes).value_or(DmxSpec{0, bytes}), bytes);
    def.dflt = inChannelResolution(parseDmxSpec(attr(node, "Default"), bytes).value_or(DmxSpec{0, bytes}), bytes);
    def.physicalFrom = numberAttr(node, "PhysicalFrom", 0.0);
    def.physicalTo = numberAttr(node, "PhysicalTo", 1.0);
    def.realFade = numberAttr(node, "RealFade", 0.0);
    def.realAcceleration = numberAttr(node, "RealAcceleration", 0.0);

    for (const pugi::xml_node& s : node.children("ChannelSet")) {
        SetDef set;
        set.name = attr(s, "Name");
        set.from = inChannelResolution(parseDmxSpec(attr(s, "DMXFrom"), bytes).value_or(DmxSpec{0, bytes}), bytes);
        set.slotIndex = static_cast<int>(numberAttr(s, "WheelSlotIndex", 0.0));
        set.hasPhysical = !attr(s, "PhysicalFrom").empty() || !attr(s, "PhysicalTo").empty();
        set.physicalFrom = numberAttr(s, "PhysicalFrom", def.physicalFrom);
        set.physicalTo = numberAttr(s, "PhysicalTo", def.physicalTo);
        def.sets.push_back(std::move(set));
    }
    std::stable_sort(def.sets.begin(), def.sets.end(),
                     [](const SetDef& a, const SetDef& b) { return a.from < b.from; });
    return def;
}

// Remember the speed limits GDTF gives for the first pan and tilt function that has any.
void GdtfImport::noteMovement(const FunctionDef& def, const ChannelFunction& f) {
    if (def.realFade <= 0.0 || (f.attribute != Attribute::Pan && f.attribute != Attribute::Tilt)) return;
    const bool pan = f.attribute == Attribute::Pan;
    if (movementSet_[pan ? 0 : 1]) return;
    const float range = std::abs(f.physicalTo - f.physicalFrom);  // rad over the whole channel
    if (range <= 0.0f) return;
    movementSet_[pan ? 0 : 1] = true;
    MovementSpec& m = t_.physical.movement;
    const float speed = range / static_cast<float>(def.realFade);
    const float acceleration = def.realAcceleration > 0.0 ? speed / static_cast<float>(def.realAcceleration) : 0.0f;
    if (pan) {
        m.panMaxSpeed = speed;
        if (acceleration > 0.0f) m.panAcceleration = acceleration;
    } else {
        m.tiltMaxSpeed = speed;
        if (acceleration > 0.0f) m.tiltAcceleration = acceleration;
    }
}

FunctionKind shutterKindFor(const std::string& label, double from, double to) {
    if (containsNoCase(label, "clos")) return FunctionKind::ShutterClosed;
    if (containsNoCase(label, "open")) return FunctionKind::ShutterOpen;
    return std::max(from, to) > 0.0 ? FunctionKind::ShutterOpen : FunctionKind::ShutterClosed;
}

// Turns one GDTF channel function covering [from, to] into DmxViz functions.
void GdtfImport::appendFunctions(ChannelTemplate& channel, const FunctionDef& def, std::uint32_t from,
                                 std::uint32_t to) {
    const Attribute attribute = parseAttribute(def.attributeName);
    ChannelFunction base;
    base.name = def.name;
    base.attribute = attribute;
    if (attribute == Attribute::Unknown) {
        base.attributeName = def.attributeName;
        unrenderedAttributes_.insert(def.attributeName);
    }
    base.kind = attributeInfo(attribute).defaultKind;
    base.dmxFrom = from;
    base.dmxTo = to;
    convertPhysical(base, def.physicalFrom, def.physicalTo);
    if (const Wheel* wheel = def.wheel.empty() ? nullptr : t_.findWheel(def.wheel)) base.wheel = wheel->name;
    if (!def.emitter.empty()) {
        if (const Emitter* e = t_.findEmitter(def.emitter))
            base.emitter = e->name;
        else if (const Emitter* e2 = t_.findEmitter(lastLinkPart(def.emitter)))
            base.emitter = e2->name;
    }
    noteMovement(def, base);

    // Named sub-ranges of the function, clipped to it, for the UI and for wheel slots.
    struct Range {
        const SetDef* set;
        std::uint32_t from, to;
    };
    std::vector<Range> ranges;
    for (std::size_t i = 0; i < def.sets.size(); ++i) {
        const std::uint32_t setFrom = std::max(def.sets[i].from, from);
        std::uint32_t setTo = i + 1 < def.sets.size() && def.sets[i + 1].from > 0 ? def.sets[i + 1].from - 1 : to;
        setTo = std::min(setTo, to);
        if (setFrom <= setTo) ranges.push_back({&def.sets[i], setFrom, setTo});
    }

    auto push = [&](ChannelFunction f) { channel.functions.push_back(std::move(f)); };

    if (base.kind == FunctionKind::WheelSlot) {
        const Wheel* wheel = base.wheel.empty() ? nullptr : t_.findWheel(base.wheel);
        if (!wheel) {
            warn(std::format("channel function \"{}\" ({}) names no usable wheel (\"{}\"); ignored", def.name,
                             def.attributeName, def.wheel));
            base.kind = FunctionKind::NoFeature;
            push(base);
            return;
        }
        bool any = false;
        for (const Range& r : ranges) {
            if (r.set->slotIndex <= 0) continue;
            if (static_cast<std::size_t>(r.set->slotIndex) > wheel->slots.size()) {
                warn(std::format("wheel \"{}\" has no slot {}", wheel->name, r.set->slotIndex));
                continue;
            }
            ChannelFunction f = base;
            f.dmxFrom = r.from;
            f.dmxTo = r.to;
            f.slotFrom = f.slotTo = static_cast<float>(r.set->slotIndex);
            f.name =
                r.set->name.empty() ? wheel->slots[static_cast<std::size_t>(r.set->slotIndex) - 1].name : r.set->name;
            push(std::move(f));
            any = true;
        }
        if (!any && def.sets.empty()) {
            // No sets: the function scrolls through the whole wheel.
            base.slotFrom = 1.0f;
            base.slotTo = static_cast<float>(wheel->slots.size());
            push(base);
        }
        return;
    }

    if (attribute == Attribute::Shutter1) {
        std::vector<Range> named;
        for (const Range& r : ranges)
            if (!r.set->name.empty()) named.push_back(r);
        if (named.empty()) {
            base.kind = shutterKindFor(def.name, def.physicalFrom, def.physicalTo);
            push(base);
        } else {
            for (const Range& r : named) {
                ChannelFunction f = base;
                f.dmxFrom = r.from;
                f.dmxTo = r.to;
                f.name = r.set->name;
                f.kind = shutterKindFor(r.set->name, r.set->physicalFrom, r.set->physicalTo);
                push(std::move(f));
            }
        }
        return;
    }

    for (const Range& r : ranges)
        if (!r.set->name.empty()) base.sets.push_back({r.set->name, r.from, r.to});
    push(std::move(base));
}

std::optional<ChannelTemplate> GdtfImport::readChannel(const pugi::xml_node& node, std::string* skipReason) {
    ChannelTemplate t;
    t.geometry = attr(node, "Geometry");

    // Only the first DMX break exists in DmxViz's single address space.
    const std::string breakText = attr(node, "DMXBreak");
    if (!breakText.empty() && !equalsNoCase(breakText, "Overwrite") && numberAttr(node, "DMXBreak", 1.0) > 1.0) {
        if (skipReason) *skipReason = std::format("uses DMX break {}", breakText);
        return std::nullopt;
    }

    for (std::string_view part : splitList(attr(node, "Offset"), ','))
        if (auto v = parseNumber(part); v && *v >= 1.0 && *v <= 512.0)
            t.offsets.push_back(static_cast<std::uint16_t>(*v));
    if (t.offsets.size() > 3) {
        warn(std::format("channel on geometry \"{}\" has more than 3 bytes; only the 3 coarsest are used", t.geometry));
        t.offsets.resize(3);
    }
    const int bytes = t.offsets.empty() ? 1 : static_cast<int>(t.offsets.size());

    std::vector<FunctionDef> defs;
    std::string mainAttribute;
    for (const pugi::xml_node& lc : node.children("LogicalChannel")) {
        if (mainAttribute.empty()) mainAttribute = attr(lc, "Attribute");
        for (const pugi::xml_node& cf : lc.children("ChannelFunction"))
            defs.push_back(
                readFunction(cf, attr(lc, "Name").empty() ? attr(lc, "Attribute") : attr(lc, "Name"), bytes));
    }
    if (defs.empty()) {
        if (skipReason) *skipReason = "has no channel functions";
        return std::nullopt;
    }
    if (mainAttribute.empty()) mainAttribute = defs.front().attributeName;
    t.suffix = mainAttribute;
    t.baseName = (t.geometry.empty() ? std::string("Fixture") : t.geometry) + "_" + mainAttribute;

    // A function ends where the next one of the channel starts. Functions that depend on a
    // mode master form their own sequence so they do not cut the ordinary ranges short.
    auto sequence = [](const FunctionDef& d) {
        return d.modeMasterLink + "|" + (d.modeFrom ? std::to_string(d.modeFrom->value) : std::string()) + "|" +
               (d.modeTo ? std::to_string(d.modeTo->value) : std::string());
    };
    const std::uint32_t maxValue = maxDmxValue(bytes);
    std::vector<std::size_t> order(defs.size());
    for (std::size_t i = 0; i < order.size(); ++i) order[i] = i;
    std::stable_sort(order.begin(), order.end(),
                     [&](std::size_t a, std::size_t b) { return defs[a].from < defs[b].from; });
    for (std::size_t i : order) {
        std::uint32_t to = maxValue;
        for (std::size_t j : order)
            if (defs[j].from > defs[i].from && sequence(defs[j]) == sequence(defs[i])) {
                to = defs[j].from - 1;
                break;
            }
        if (defs[i].from > maxValue) continue;
        const std::size_t before = t.functions.size();
        appendFunctions(t, defs[i], defs[i].from, to);
        for (std::size_t k = before; k < t.functions.size(); ++k) {
            std::optional<PendingMaster> master;
            if (!defs[i].modeMasterLink.empty() && defs[i].modeFrom && defs[i].modeTo) {
                PendingMaster pm;
                pm.link = defs[i].modeMasterLink;
                pm.from = defs[i].modeFrom->value;
                pm.fromBytes = defs[i].modeFrom->bytes;
                pm.to = defs[i].modeTo->value;
                pm.toBytes = defs[i].modeTo->bytes;
                master = pm;
            }
            t.masters.push_back(master);
        }
    }

    // Default value: that of the InitialFunction (a link ending in "<logical channel>.<function>"),
    // otherwise of the channel's first function.
    const FunctionDef* initial = &defs.front();
    const std::string link = attr(node, "InitialFunction");
    for (const FunctionDef& d : defs)
        if (!link.empty() && (link == d.name || link.ends_with("." + d.logicalName + "." + d.name))) {
            initial = &d;
            break;
        }
    t.defaultValue = std::min(initial->dflt, maxValue);
    if (auto h = parseDmxSpec(attr(node, "Highlight"), bytes))
        t.highlight = std::min(inChannelResolution(*h, bytes), maxValue);
    return t;
}

DmxMode GdtfImport::buildMode(const pugi::xml_node& modeNode, int index) {
    DmxMode mode;
    mode.name = attr(modeNode, "Name");
    if (mode.name.empty()) mode.name = std::format("Mode {}", index + 1);
    mode.description = attr(modeNode, "Description");

    const std::string root = rootGeometryFor(modeNode);
    const Expansion* expansion = nullptr;
    if (auto it = expansionOfRoot_.find(root); it != expansionOfRoot_.end()) expansion = &expansions_[it->second];
    if (expansions_.size() > 1 && expansion) mode.geometryRoot = expansion->rootName;

    // 1. read the channels as templates
    std::vector<ChannelTemplate> templates;
    for (const pugi::xml_node& node : modeNode.child("DMXChannels").children("DMXChannel")) {
        std::string reason;
        auto t = readChannel(node, &reason);
        if (t) {
            templates.push_back(std::move(*t));
        } else {
            warn(std::format("mode \"{}\": a channel on geometry \"{}\" was skipped ({})", mode.name,
                             attr(node, "Geometry"), reason));
        }
    }

    // 2. one channel per instance of the channel's geometry
    std::vector<std::vector<std::size_t>> channelsOfTemplate(templates.size());  // indices into mode.channels
    std::set<std::string, std::less<>> usedNames;
    auto uniqueName = [&](const std::string& wanted) {
        std::string name = wanted;
        for (int n = 2; !usedNames.insert(name).second; ++n) name = std::format("{} {}", wanted, n);
        return name;
    };
    for (std::size_t ti = 0; ti < templates.size(); ++ti) {
        const ChannelTemplate& tpl = templates[ti];
        std::vector<GeometryInstance> instances;
        if (expansion && !tpl.geometry.empty()) {
            if (auto it = expansion->instances.find(tpl.geometry); it != expansion->instances.end()) {
                instances = it->second;
            } else if (geometryNodes_.contains(tpl.geometry)) {
                warn(std::format(
                    "mode \"{}\": geometry \"{}\" is not part of the mode's geometry tree; its channel is skipped",
                    mode.name, tpl.geometry));
                continue;
            } else {
                warn(std::format(
                    "mode \"{}\": channel geometry \"{}\" does not exist; the channel controls the whole fixture",
                    mode.name, tpl.geometry));
            }
        } else if (!tpl.geometry.empty() && !expansion) {
            warn(
                std::format("mode \"{}\": no geometry tree; channel geometry \"{}\" ignored", mode.name, tpl.geometry));
        }
        if (instances.empty()) instances.push_back({std::string(), 0});

        for (const GeometryInstance& inst : instances) {
            Channel c;
            const std::string owner =
                inst.name.empty() ? (expansion ? expansion->rootName : std::string("Fixture")) : inst.name;
            c.name = uniqueName(owner + "_" + tpl.suffix);
            for (std::uint16_t o : tpl.offsets) c.offsets.push_back(static_cast<std::uint16_t>(o + inst.dmxBase));
            c.geometry = inst.name;
            c.defaultValue = tpl.defaultValue;
            c.highlightValue = tpl.highlight;
            c.functions = tpl.functions;
            channelsOfTemplate[ti].push_back(mode.channels.size());
            mode.channels.push_back(std::move(c));
        }
    }

    // 3. mode masters
    resolveModeMasters(mode, templates, channelsOfTemplate);
    return mode;
}

// The master link is "<channel>.<logical channel>.<function>" with the channel named like GDTF
// names it ("Geometry_Attribute"); the longest matching channel name wins. A copy of a
// multi-cell channel follows the master of its own cell when the master is repeated as well.
void GdtfImport::resolveModeMasters(DmxMode& mode, const std::vector<ChannelTemplate>& templates,
                                    const std::vector<std::vector<std::size_t>>& channelsOfTemplate) {
    for (std::size_t ti = 0; ti < templates.size(); ++ti) {
        const ChannelTemplate& tpl = templates[ti];
        for (std::size_t instance = 0; instance < channelsOfTemplate[ti].size(); ++instance) {
            Channel& channel = mode.channels[channelsOfTemplate[ti][instance]];
            std::vector<ChannelFunction> kept;
            for (std::size_t fi = 0; fi < channel.functions.size(); ++fi) {
                ChannelFunction f = std::move(channel.functions[fi]);
                const std::optional<PendingMaster>& pending = fi < tpl.masters.size() ? tpl.masters[fi] : std::nullopt;
                if (!pending) {
                    kept.push_back(std::move(f));
                    continue;
                }
                std::optional<std::size_t> master;
                std::size_t bestLength = 0;
                for (std::size_t mi = 0; mi < templates.size(); ++mi) {
                    const std::string& name = templates[mi].baseName;
                    const bool matches = pending->link == name || pending->link.starts_with(name + ".");
                    if (matches && name.size() >= bestLength && !channelsOfTemplate[mi].empty()) {
                        master = mi;
                        bestLength = name.size();
                    }
                }
                if (!master) {
                    warn(std::format("mode \"{}\": function \"{}\" depends on unknown master \"{}\"; ignored",
                                     mode.name, f.name, pending->link));
                    continue;
                }
                const auto& masterChannels = channelsOfTemplate[*master];
                const std::size_t pick = masterChannels.size() == channelsOfTemplate[ti].size() ? instance : 0;
                const Channel& masterChannel = mode.channels[masterChannels[pick]];
                const int masterBytes = masterChannel.byteCount();
                f.modeMaster = masterChannel.name;
                f.modeFrom = convertDmxResolution(
                    pending->from, pending->fromBytes > 0 ? pending->fromBytes : masterBytes, masterBytes, false);
                f.modeTo = convertDmxResolution(pending->to, pending->toBytes > 0 ? pending->toBytes : masterBytes,
                                                masterBytes, true);
                kept.push_back(std::move(f));
            }
            channel.functions = std::move(kept);
        }
    }
}

bool GdtfImport::readModes(const pugi::xml_node& ft, std::string* error) {
    int index = 0;
    for (const pugi::xml_node& modeNode : ft.child("DMXModes").children("DMXMode")) {
        t_.modes.push_back(buildMode(modeNode, index));
        t_.modes.back().footprint = t_.modes.back().highestOffset();
        ++index;
    }
    if (t_.modes.empty()) {
        if (error) *error = "the fixture has no DMX modes";
        return false;
    }
    return true;
}

void GdtfImport::finish() {
    bool movingHead = false;
    for (const DmxMode& mode : t_.modes)
        for (const Channel& c : mode.channels)
            for (const ChannelFunction& f : c.functions)
                if (f.attribute == Attribute::Pan || f.attribute == Attribute::Tilt) movingHead = true;
    if (movingHead) t_.categories.push_back("Moving Head");

    // Root model size is the nearest thing GDTF has to the fixture's dimensions.
    if (t_.physical.dimensions == glm::vec3(0.0f)) t_.physical.dimensions = t_.geometry.model.size;

    if (!unrenderedAttributes_.empty()) {
        std::string list;
        for (const std::string& name : unrenderedAttributes_) list += (list.empty() ? "" : ", ") + name;
        warn(std::format("attributes without rendering support are kept as unknown: {}", list));
    }
    for (const std::string& problem : validateFixtureType(t_)) warn("validation: " + problem);
}

}  // namespace

std::optional<FixtureType> importGdtf(std::span<const std::uint8_t> archive, std::string* error,
                                      std::vector<std::string>* warnings) {
    GdtfImport import(archive, warnings);
    return import.run(error);
}

std::optional<FixtureType> importGdtfFile(const std::filesystem::path& path, std::string* error,
                                          std::vector<std::string>* warnings) {
    std::string readError;
    auto bytes = readFileBytes(path, &readError);
    if (!bytes) {
        if (error) *error = path.filename().string() + ": " + readError;
        return std::nullopt;
    }
    auto type = importGdtf(*bytes, error, warnings);
    if (!type && error) *error = path.filename().string() + ": " + *error;
    return type;
}

}  // namespace dmxviz::fixtures
