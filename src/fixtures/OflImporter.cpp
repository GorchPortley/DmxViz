#include "fixtures/OflImporter.h"

#include "core/Log.h"
#include "fixtures/Archive.h"
#include "fixtures/ColorMath.h"
#include "fixtures/DmxValue.h"
#include "core/SceneTypes.h"
#include "fixtures/GeometryBuilder.h"

#include <nlohmann/json.hpp>

#include <algorithm>
#include <cctype>
#include <cmath>
#include <format>
#include <fstream>
#include <map>
#include <regex>
#include <set>
#include <stdexcept>
#include <sstream>

// Capability type -> DmxViz mapping (OFL capability-types.md):
//
//   NoFunction                  -> NoFeature
//   ShutterStrobe               -> Shutter1 (open/closed) or Shutter1Strobe/Pulse/RampUp/... (Hz)
//   StrobeSpeed / StrobeDuration-> StrobeFrequency (Hz) / StrobeDuration (s)
//   Intensity                   -> Dimmer
//   ColorIntensity              -> ColorAdd_* (LED fixtures) or ColorSub_C/M/Y (CMY flags on lamp fixtures)
//   ColorPreset                 -> ColorMacro1 on a virtual "presets" wheel
//   ColorTemperature            -> CTO / CTB / CTC (Kelvin)
//   Pan / Tilt                  -> Pan / Tilt, centred on the middle of the range (radians)
//   PanContinuous/TiltContinuous-> PanRotate / TiltRotate (rad/s)
//   WheelSlot / WheelShake      -> Color<n> / Gobo<n> / Prism<n> / AnimationWheel1 slot selection
//   WheelSlotRotation           -> Gobo<n>Pos / Gobo<n>PosRotate (or Prism / Animation)
//   WheelRotation               -> Color<n>WheelSpin / Gobo<n>WheelSpin
//   Prism / PrismRotation       -> Prism1 (+ rotation) / Prism1Pos / Prism1PosRotate
//   Zoom / BeamAngle            -> Zoom;  Focus -> Focus1;  Iris -> Iris;  Frost -> Frost1
//   BladeInsertion / BladeRotation / BladeSystemRotation -> Blade<n>A / Blade<n>Rot / ShaperRot
//   Effect, EffectSpeed, EffectDuration, EffectParameter -> Effects1, Effects1Rate, Effects1Fade, Effects1Adjust1
//   PanTiltSpeed                -> PositionMSpeed;  Maintenance -> Control
//   everything else             -> Unknown, original type name preserved

namespace dmxviz::fixtures {
namespace {

namespace fs = std::filesystem;
using OJson = nlohmann::ordered_json;

constexpr float kDeg = kPi / 180.0f;

// ======================================================== JSON helpers

const OJson* member(const OJson& j, const char* key) {
    if (!j.is_object()) return nullptr;
    auto it = j.find(key);
    return it == j.end() ? nullptr : &*it;
}

std::string stringOf(const OJson& j, const char* key, std::string fallback = {}) {
    const OJson* v = member(j, key);
    return v && v->is_string() ? v->get<std::string>() : fallback;
}

// Numbers in a fixture file are untrusted: 1e300 must neither overflow a later float or integer
// conversion (undefined behaviour) nor poison the physics with infinities.
constexpr double kMaxFixtureNumber = 1.0e9;
constexpr std::size_t kMaxPixels = 4096;  // matrix pixels per fixture

double finiteNumber(double d) {
    if (!(d >= -kMaxFixtureNumber)) return -kMaxFixtureNumber;  // also catches NaN
    return d > kMaxFixtureNumber ? kMaxFixtureNumber : d;
}

std::optional<double> numberOf(const OJson& j, const char* key) {
    const OJson* v = member(j, key);
    if (v && v->is_number()) return finiteNumber(v->get<double>());
    return std::nullopt;
}

// A JSON number as an integer clamped to [lo, hi]. Throws for non-numbers (caught by importOfl).
std::int64_t clampedInt(const OJson& v, std::int64_t lo, std::int64_t hi) {
    if (!v.is_number()) throw std::runtime_error("number expected");
    const double d = finiteNumber(v.get<double>());
    if (d <= static_cast<double>(lo)) return lo;
    if (d >= static_cast<double>(hi)) return hi;
    return static_cast<std::int64_t>(d);
}

float floatOf(const OJson& v) {
    if (!v.is_number()) throw std::runtime_error("number expected");
    return static_cast<float>(finiteNumber(v.get<double>()));
}

// DMX values in capabilities ("dmxRange": [from, to]).
std::uint32_t dmxNumber(const OJson& v) {
    return static_cast<std::uint32_t>(clampedInt(v, 0, 0xFFFFFFFFll));
}

// Pixel-group name patterns are regular expressions from the file. std::regex backtracks, so only
// simple patterns are run: short, few quantifiers, no quantified groups and no back-references
// (nested quantifiers such as (a+)+ take exponential time).
bool isSimpleRegex(const std::string& pattern) {
    if (pattern.size() > 64) return false;
    int quantifiers = 0;
    for (std::size_t i = 0; i < pattern.size(); ++i) {
        const char c = pattern[i];
        if (c == '\\') {
            if (i + 1 < pattern.size() && std::isdigit(static_cast<unsigned char>(pattern[i + 1]))) return false;
            ++i;  // skip the escaped character
            continue;
        }
        if (c == '*' || c == '+' || c == '?' || c == '{') {
            if (i > 0 && pattern[i - 1] == ')') return false;
            if (!(c == '?' && i > 0 && pattern[i - 1] == '(')) ++quantifiers;
        }
    }
    return quantifiers <= 3;
}

// ======================================================== entities

// A parsed OFL entity value: a number with a unit. Keywords and percentages
// become unit "%" with value as a fraction (-1..1).
struct Entity {
    double value = 0.0;
    std::string unit;
    bool percent() const { return unit == "%"; }
};

std::optional<Entity> parseEntity(const OJson& v) {
    if (v.is_number()) return Entity{finiteNumber(v.get<double>()), ""};
    if (!v.is_string()) return std::nullopt;
    const std::string s = v.get<std::string>();
    static const std::map<std::string, double, std::less<>> keywords = {
        {"fast", 1.0},       {"slow", 0.01},     {"stop", 0.0},      {"slow reverse", -0.01}, {"fast reverse", -1.0},
        {"fast CW", 1.0},    {"slow CW", 0.01},  {"slow CCW", -0.01}, {"fast CCW", -1.0},     {"instant", 0.0},
        {"short", 0.01},     {"long", 1.0},      {"near", 0.01},     {"far", 1.0},            {"off", 0.0},
        {"dark", 0.01},      {"bright", 1.0},    {"warm", -1.0},     {"CTO", -1.0},           {"default", 0.0},
        {"cold", 1.0},       {"CTB", 1.0},       {"closed", 0.0},    {"narrow", 0.01},        {"wide", 1.0},
        {"low", 0.01},       {"high", 1.0},      {"small", 0.01},    {"big", 1.0},            {"out", 0.0},
        {"in", 1.0},         {"open", 1.0},      {"weak", 0.01},     {"strong", 1.0},         {"center", 0.0},
        {"left", -1.0},      {"right", 1.0},     {"top", -1.0},      {"bottom", 1.0},
    };
    if (auto it = keywords.find(s); it != keywords.end()) return Entity{it->second, "%"};
    std::size_t pos = 0;
    double number = 0.0;
    try {
        number = std::stod(s, &pos);
    } catch (...) {
        return std::nullopt;
    }
    std::string unit = s.substr(pos);
    if (unit == "%") number /= 100.0;
    return Entity{finiteNumber(number), unit};
}

// "speed" or "speedStart"/"speedEnd".
std::optional<std::pair<Entity, Entity>> entityRange(const OJson& cap, const std::string& prop) {
    if (const OJson* v = member(cap, prop.c_str())) {
        auto e = parseEntity(*v);
        if (e) return std::make_pair(*e, *e);
        return std::nullopt;
    }
    const OJson* a = member(cap, (prop + "Start").c_str());
    const OJson* b = member(cap, (prop + "End").c_str());
    if (!a || !b) return std::nullopt;
    auto ea = parseEntity(*a), eb = parseEntity(*b);
    if (!ea || !eb) return std::nullopt;
    return std::make_pair(*ea, *eb);
}

// Strobe-style speed -> Hz ("slow".."fast" ~ 1..20 Hz).
float toHertz(const Entity& e) {
    if (e.unit == "Hz") return static_cast<float>(e.value);
    if (e.unit == "bpm") return static_cast<float>(e.value / 60.0);
    if (e.percent()) return e.value <= 0.0 ? 0.0f : static_cast<float>(0.5 + 19.5 * e.value);
    return static_cast<float>(e.value);
}

// Rotation speed -> rad/s, CW positive ("fast" = 1.5 turns per second).
float toAngularSpeed(const Entity& e) {
    if (e.unit == "Hz") return static_cast<float>(e.value) * 2.0f * kPi;
    if (e.unit == "rpm") return static_cast<float>(e.value / 60.0) * 2.0f * kPi;
    return static_cast<float>(e.value) * 1.5f * 2.0f * kPi;
}

float toSeconds(const Entity& e, float percentScale) {
    if (e.unit == "s") return static_cast<float>(e.value);
    if (e.unit == "ms") return static_cast<float>(e.value / 1000.0);
    return static_cast<float>(e.value) * percentScale;
}

// ======================================================== small utilities

std::string titleFromKey(std::string_view key) {
    std::string out;
    bool upper = true;
    for (char c : key) {
        if (c == '-' || c == '_') {
            out.push_back(' ');
            upper = true;
        } else {
            out.push_back(upper ? static_cast<char>(std::toupper(static_cast<unsigned char>(c))) : c);
            upper = false;
        }
    }
    return out;
}

std::string replaceAll(std::string s, std::string_view from, std::string_view to) {
    for (std::size_t pos = s.find(from); pos != std::string::npos; pos = s.find(from, pos + to.size()))
        s.replace(pos, from.size(), to);
    return s;
}

// Natural ordering like JavaScript's localeCompare(..., {numeric: true}): "2" < "10" < "alice".
bool naturalLess(const std::string& a, const std::string& b) {
    std::size_t i = 0, j = 0;
    while (i < a.size() && j < b.size()) {
        const bool da = std::isdigit(static_cast<unsigned char>(a[i])) != 0;
        const bool db = std::isdigit(static_cast<unsigned char>(b[j])) != 0;
        if (da && db) {
            std::size_t ei = i, ej = j;
            while (ei < a.size() && std::isdigit(static_cast<unsigned char>(a[ei]))) ++ei;
            while (ej < b.size() && std::isdigit(static_cast<unsigned char>(b[ej]))) ++ej;
            const unsigned long long na = std::stoull(a.substr(i, ei - i));
            const unsigned long long nb = std::stoull(b.substr(j, ej - j));
            if (na != nb) return na < nb;
            i = ei;
            j = ej;
        } else {
            const int ca = std::tolower(static_cast<unsigned char>(a[i]));
            const int cb = std::tolower(static_cast<unsigned char>(b[j]));
            if (ca != cb) return ca < cb;
            ++i;
            ++j;
        }
    }
    return a.size() - i < b.size() - j;
}

int bytesOfResolution(const std::string& r) {
    if (r == "8bit") return 1;
    if (r == "16bit") return 2;
    if (r == "24bit") return 3;
    return 0;
}

// Built-in stand-in gobo when no image (and no bundled stand-in) is available:
// a ring of n dots so different slots still look different.
std::string fallbackGoboSvg(int n) {
    std::string svg =
        R"(<svg xmlns="http://www.w3.org/2000/svg" width="256" height="256" viewBox="-128 -128 256 256">)"
        R"(<rect x="-128" y="-128" width="256" height="256" fill="#000"/>)";
    const int dots = 3 + (n % 6);
    for (int i = 0; i < dots; ++i) {
        const float a = 2.0f * kPi * static_cast<float>(i) / static_cast<float>(dots);
        svg += std::format(R"(<circle cx="{:.1f}" cy="{:.1f}" r="{}" fill="#fff"/>)", 70.0f * std::cos(a),
                           70.0f * std::sin(a), 22 - dots);
    }
    return svg + "</svg>";
}

// ======================================================== importer

struct WheelInfo {
    int index = -1;  // into FixtureType::wheels
    AttributeFamily family = AttributeFamily::ColorWheel;
    int familyIndex = 0;
};

struct Pixel {
    std::string key;
    glm::ivec3 position{1};
};

// One channel definition (available or resolved template channel).
struct ChannelDef {
    std::string key;
    const OJson* json = nullptr;
    std::string pixelKey;  // template channels: the pixel or group it was resolved for
    std::vector<std::string> fineKeys;
    int definitionBytes = 1;  // resolution the capability DMX values are written in
};

// A switching channel alias: which channel it becomes depends on a master channel's capability.
struct SwitchAlias {
    std::string master;
    std::vector<std::string> targets;  // per master capability; "" = unused
};

// Functions produced for one capability (all share its DMX range).
struct CapabilityFunctions {
    std::uint32_t from = 0, to = 0;  // in definition resolution
    std::vector<ChannelFunction> functions;
};

class OflImport {
public:
    OflImport(const OJson& root, const OflImportOptions& options, std::vector<std::string>* warnings)
        : root_(root), options_(options), warnings_(warnings) {}

    std::optional<FixtureType> run(std::string* error);

private:
    void warn(std::string message) {
        log::warn("fixtures", "OFL {}: {}", t_.id, message);
        if (warnings_) warnings_->push_back(std::move(message));
    }

    void readMeta();
    void readPhysical();
    void readMatrix();
    void readWheels();
    void collectChannels();
    void scanFixture();
    bool buildModes(std::string* error);
    void buildGeometry();

    std::string goboResource(const OJson* resource, const std::string& slotName);
    std::string standInGobo();
    std::string channelGeometry(const ChannelDef& def);
    std::vector<std::string> pixelKeysByOrder(char first, char second, char third) const;
    std::vector<std::string> pixelKeysAbc() const;
    std::vector<CapabilityFunctions> convertCapabilities(const ChannelDef& def);
    void convertCapability(const ChannelDef& def, const OJson& cap, CapabilityFunctions& out);
    int prismWheel();
    int colorPresetWheel(const ChannelDef& def);
    void appendFunctions(Channel& channel, const ChannelDef& def, int channelBytes, const std::string& master = {},
                         std::uint32_t masterFrom = 0, std::uint32_t masterTo = 0);

    const OJson& root_;
    OflImportOptions options_;
    std::vector<std::string>* warnings_;
    FixtureType t_;

    // physical
    glm::vec3 dimensionsMm_{0.0f};
    float lumens_ = 0.0f;
    float kelvin_ = 6500.0f;
    float lensMin_ = 0.0f, lensMax_ = 0.0f;
    bool hasLens_ = false;
    float panMax_ = 540.0f, tiltMax_ = 270.0f;
    glm::vec3 pixelSizeMm_{0.0f}, pixelSpacingMm_{0.0f};

    // matrix
    std::vector<Pixel> pixels_;
    std::vector<std::pair<std::string, std::vector<std::string>>> pixelGroups_;

    // wheels and resources
    std::map<std::string, WheelInfo, std::less<>> wheelInfo_;
    std::vector<fs::path> standIns_;
    int standInCounter_ = 0;
    int virtualPrismWheel_ = -1;
    std::map<std::string, int, std::less<>> presetWheels_;

    // channels
    std::map<std::string, ChannelDef, std::less<>> defs_;
    std::map<std::string, std::pair<std::string, int>, std::less<>> fineOf_;  // fine key -> (coarse, 1-based)
    std::map<std::string, SwitchAlias, std::less<>> switches_;
    std::map<std::string, std::vector<CapabilityFunctions>, std::less<>> converted_;

    // what the fixture has (decides mappings and geometry)
    bool hasAdditiveRgb_ = false;
    bool hasPanTilt_ = false;
    bool hasZoom_ = false;
    bool hasGoboWheel_ = false;
};

// -------------------------------------------------------------- meta

void OflImport::readMeta() {
    t_.name = stringOf(root_, "name", "Unnamed");
    t_.shortName = stringOf(root_, "shortName");
    t_.source = FixtureSource::Ofl;
    std::string mfr = options_.manufacturerKey.empty() ? stringOf(root_, "manufacturerKey") : options_.manufacturerKey;
    std::string key = options_.fixtureKey.empty() ? stringOf(root_, "fixtureKey") : options_.fixtureKey;
    if (mfr.empty()) mfr = "unknown";
    if (key.empty()) key = slugify(t_.name);
    t_.manufacturer = options_.manufacturerName.empty() ? titleFromKey(mfr) : options_.manufacturerName;
    t_.id = slugify(mfr) + "/" + slugify(key);
    if (const OJson* cats = member(root_, "categories"); cats && cats->is_array())
        for (const OJson& c : *cats)
            if (c.is_string()) t_.categories.push_back(c.get<std::string>());

    std::string description = stringOf(root_, "comment");
    if (const OJson* meta = member(root_, "meta")) {
        t_.revision = stringOf(*meta, "lastModifyDate");
        std::string authors;
        if (const OJson* a = member(*meta, "authors"); a && a->is_array())
            for (const OJson& name : *a)
                if (name.is_string()) authors += (authors.empty() ? "" : ", ") + name.get<std::string>();
        if (!description.empty()) description += "\n\n";
        description += "Imported from the Open Fixture Library (MIT licence)";
        if (!authors.empty()) description += ", authors: " + authors;
        description += ".";
    }
    t_.description = description;
}

// -------------------------------------------------------------- physical

void OflImport::readPhysical() {
    const OJson* p = member(root_, "physical");
    if (!p) return;
    if (const OJson* d = member(*p, "dimensions"); d && d->is_array() && d->size() == 3)
        for (int i = 0; i < 3; ++i) dimensionsMm_[i] = floatOf((*d)[static_cast<std::size_t>(i)]);
    t_.physical.weight = static_cast<float>(numberOf(*p, "weight").value_or(0.0));
    t_.physical.power = static_cast<float>(numberOf(*p, "power").value_or(0.0));
    if (const OJson* bulb = member(*p, "bulb")) {
        lumens_ = static_cast<float>(numberOf(*bulb, "lumens").value_or(0.0));
        kelvin_ = static_cast<float>(numberOf(*bulb, "colorTemperature").value_or(6500.0));
    }
    if (const OJson* lens = member(*p, "lens")) {
        if (const OJson* mm = member(*lens, "degreesMinMax"); mm && mm->is_array() && mm->size() == 2) {
            lensMin_ = floatOf((*mm)[0]);
            lensMax_ = floatOf((*mm)[1]);
            hasLens_ = true;
        }
    }
    if (const OJson* focus = member(*p, "focus")) {  // schema < 12: {type, panMax, tiltMax}
        panMax_ = static_cast<float>(numberOf(*focus, "panMax").value_or(panMax_));
        tiltMax_ = static_cast<float>(numberOf(*focus, "tiltMax").value_or(tiltMax_));
    }
    if (const OJson* mp = member(*p, "matrixPixels")) {
        if (const OJson* d = member(*mp, "dimensions"); d && d->is_array() && d->size() == 3)
            for (int i = 0; i < 3; ++i) pixelSizeMm_[i] = floatOf((*d)[static_cast<std::size_t>(i)]);
        if (const OJson* s = member(*mp, "spacing"); s && s->is_array() && s->size() == 3)
            for (int i = 0; i < 3; ++i) pixelSpacingMm_[i] = floatOf((*s)[static_cast<std::size_t>(i)]);
    }
    t_.physical.dimensions = dimensionsMm_ * 0.001f;
}

// -------------------------------------------------------------- matrix

std::vector<std::string> OflImport::pixelKeysAbc() const {
    std::vector<std::string> keys;
    for (const Pixel& p : pixels_) keys.push_back(p.key);
    std::sort(keys.begin(), keys.end(), naturalLess);
    return keys;
}

std::vector<std::string> OflImport::pixelKeysByOrder(char first, char second, char third) const {
    auto axis = [](char c) { return c == 'X' ? 0 : (c == 'Y' ? 1 : 2); };
    const int a = axis(first), b = axis(second), c = axis(third);
    std::vector<Pixel> sorted = pixels_;
    std::stable_sort(sorted.begin(), sorted.end(), [&](const Pixel& p, const Pixel& q) {
        if (p.position[c] != q.position[c]) return p.position[c] < q.position[c];
        if (p.position[b] != q.position[b]) return p.position[b] < q.position[b];
        return p.position[a] < q.position[a];
    });
    std::vector<std::string> keys;
    for (const Pixel& p : sorted) keys.push_back(p.key);
    return keys;
}

void OflImport::readMatrix() {
    const OJson* m = member(root_, "matrix");
    if (!m) return;
    if (const OJson* keys = member(*m, "pixelKeys"); keys && keys->is_array()) {
        for (std::size_t z = 0; z < keys->size(); ++z) {
            const OJson& layer = (*keys)[z];
            for (std::size_t y = 0; y < layer.size(); ++y) {
                const OJson& row = layer[y];
                for (std::size_t x = 0; x < row.size(); ++x)
                    if (row[x].is_string()) {
                        if (pixels_.size() >= kMaxPixels)
                            throw std::runtime_error(std::format("matrix has more than {} pixels", kMaxPixels));
                        pixels_.push_back({row[x].get<std::string>(),
                                           {static_cast<int>(x) + 1, static_cast<int>(y) + 1, static_cast<int>(z) + 1}});
                    }
            }
        }
    } else if (const OJson* count = member(*m, "pixelCount"); count && count->is_array() && count->size() == 3) {
        const glm::ivec3 n{static_cast<int>(clampedInt((*count)[0], 0, 4096)),
                           static_cast<int>(clampedInt((*count)[1], 0, 4096)),
                           static_cast<int>(clampedInt((*count)[2], 0, 4096))};
        if (static_cast<std::int64_t>(n.x) * n.y * n.z > static_cast<std::int64_t>(kMaxPixels))
            throw std::runtime_error(std::format("matrix has more than {} pixels", kMaxPixels));
        const int defined = (n.x > 1) + (n.y > 1) + (n.z > 1);
        for (int z = 1; z <= n.z; ++z)
            for (int y = 1; y <= n.y; ++y)
                for (int x = 1; x <= n.x; ++x) {
                    std::string key;
                    if (defined <= 1) {
                        key = std::to_string(std::max(x, std::max(y, z)));
                    } else if (defined == 2) {
                        const int a = n.x > 1 ? x : y;
                        const int b = n.y > 1 ? y : z;
                        key = std::format("({}, {})", a, b);
                    } else {
                        key = std::format("({}, {}, {})", x, y, z);
                    }
                    pixels_.push_back({key, {x, y, z}});
                }
    }

    const OJson* groups = member(*m, "pixelGroups");
    if (!groups || !groups->is_object()) return;
    for (auto it = groups->begin(); it != groups->end(); ++it) {
        std::vector<std::string> members;
        const OJson& g = it.value();
        if (g.is_string() && g.get<std::string>() == "all") {
            members = pixelKeysAbc();
        } else if (g.is_array()) {
            for (const OJson& k : g)
                if (k.is_string()) members.push_back(k.get<std::string>());
        } else if (g.is_object()) {
            // Constraints: x/y/z number rules and name regular expressions; all must hold.
            auto numberRule = [](const std::string& rule, int pos) {
                if (rule == "even") return pos % 2 == 0;
                if (rule == "odd") return pos % 2 == 1;
                if (rule.rfind(">=", 0) == 0) return pos >= std::stoi(rule.substr(2));
                if (rule.rfind("<=", 0) == 0) return pos <= std::stoi(rule.substr(2));
                if (rule.rfind("=", 0) == 0) return pos == std::stoi(rule.substr(1));
                const std::size_t n = rule.find('n');
                if (n != std::string::npos) {
                    const int div = std::stoi(rule.substr(0, n));
                    const int rem = n + 1 < rule.size() ? std::stoi(rule.substr(n + 2)) : 0;
                    return div > 0 && pos % div == rem % div;
                }
                return true;
            };
            const bool byName = member(g, "name") != nullptr;
            const std::vector<std::string> candidates = byName ? pixelKeysAbc() : pixelKeysByOrder('X', 'Y', 'Z');
            for (const std::string& key : candidates) {
                const Pixel& p = *std::find_if(pixels_.begin(), pixels_.end(), [&](const Pixel& q) { return q.key == key; });
                bool ok = true;
                const char* axes[3] = {"x", "y", "z"};
                for (int a = 0; a < 3 && ok; ++a)
                    if (const OJson* rules = member(g, axes[a]); rules && rules->is_array())
                        for (const OJson& r : *rules)
                            if (r.is_string()) {
                                try {
                                    ok = ok && numberRule(r.get<std::string>(), p.position[a]);
                                } catch (...) {
                                    ok = false;
                                }
                            }
                if (const OJson* names = member(g, "name"); ok && names && names->is_array())
                    for (const OJson& pattern : *names)
                        if (pattern.is_string()) {
                            try {
                                const std::string text = pattern.get<std::string>();
                                ok = ok && key.size() <= 64 && isSimpleRegex(text) && std::regex_search(key, std::regex(text));
                            } catch (const std::regex_error&) {
                                ok = false;
                            }
                        }
                if (ok) members.push_back(key);
            }
        }
        pixelGroups_.emplace_back(it.key(), std::move(members));
    }
}

// -------------------------------------------------------------- wheels

std::string OflImport::standInGobo() {
    const int n = standInCounter_++;
    if (!standIns_.empty()) {
        const fs::path& file = standIns_[static_cast<std::size_t>(n) % standIns_.size()];
        const std::string name = "standin-" + file.stem().string();
        if (t_.findResource(name)) return name;
        if (auto bytes = readFileBytes(file)) {
            std::string ext = file.extension().string();
            if (!ext.empty()) ext.erase(0, 1);
            t_.resources.push_back({name, ext, std::move(*bytes)});
            return name;
        }
    }
    const std::string name = std::format("standin-dots-{}", 3 + (n % 6));
    if (!t_.findResource(name)) {
        const std::string svg = fallbackGoboSvg(n);
        t_.resources.push_back({name, "svg", std::vector<std::uint8_t>(svg.begin(), svg.end())});
    }
    return name;
}

std::string OflImport::goboResource(const OJson* resource, const std::string& slotName) {
    if (resource && resource->is_object()) {
        // OFL export plugin: the resource object with its embedded image.
        const std::string key = stringOf(*resource, "key", slugify(slotName));
        if (const OJson* image = member(*resource, "image")) {
            std::string ext = stringOf(*image, "extension");
            if (ext.empty()) ext = stringOf(*image, "mimeType").find("svg") != std::string::npos ? "svg" : "png";
            const std::string data = stringOf(*image, "data");
            std::optional<std::vector<std::uint8_t>> bytes;
            if (stringOf(*image, "encoding") == "base64")
                bytes = base64Decode(data);
            else
                bytes = std::vector<std::uint8_t>(data.begin(), data.end());
            if (bytes && !bytes->empty()) {
                const std::string name = "gobo-" + key;
                if (!t_.findResource(name)) t_.resources.push_back({name, ext, std::move(*bytes)});
                return name;
            }
        }
        return standInGobo();
    }
    if (!resource || !resource->is_string() || options_.resourceDir.empty()) return standInGobo();

    std::string ref = resource->get<std::string>();  // "gobos/<key>" or "gobos/aliases/<file>/<alias>"
    std::string key;
    if (ref.rfind("gobos/aliases/", 0) == 0) {
        const std::string rest = ref.substr(std::string("gobos/aliases/").size());
        const std::size_t slash = rest.find('/');
        if (slash != std::string::npos) {
            const fs::path aliasFile = options_.resourceDir / "gobos" / "aliases" / (rest.substr(0, slash) + ".json");
            std::ifstream in(aliasFile);
            if (in) {
                const OJson aliases = OJson::parse(in, nullptr, false);
                key = stringOf(aliases, rest.substr(slash + 1).c_str());
            }
        }
    } else if (ref.rfind("gobos/", 0) == 0) {
        key = ref.substr(6);
    }
    if (!key.empty()) {
        const std::string name = "gobo-" + key;
        if (t_.findResource(name)) return name;
        for (const char* ext : {"svg", "png"}) {
            const fs::path file = options_.resourceDir / "gobos" / (key + "." + ext);
            if (auto bytes = readFileBytes(file)) {
                t_.resources.push_back({name, ext, std::move(*bytes)});
                return name;
            }
        }
    }
    warn(std::format("gobo image \"{}\" not available, using a stand-in", ref));
    return standInGobo();
}

void OflImport::readWheels() {
    const OJson* wheels = member(root_, "wheels");
    if (!wheels || !wheels->is_object()) return;
    int colorCount = 0, goboCount = 0, prismCount = 0, animationCount = 0;
    for (auto it = wheels->begin(); it != wheels->end(); ++it) {
        Wheel wheel;
        wheel.name = it.key();
        const OJson* slots = member(it.value(), "slots");
        bool hasGobo = false, hasPrism = false, hasAnimation = false;
        std::string lastAnimationImage;
        if (slots && slots->is_array()) {
            for (const OJson& s : *slots) {
                const std::string type = stringOf(s, "type");
                WheelSlot slot;
                slot.name = stringOf(s, "name");
                if (type == "Open" || type == "Iris") {
                    slot.kind = SlotKind::Open;
                    if (slot.name.empty()) slot.name = type;
                } else if (type == "Closed") {
                    slot.kind = SlotKind::Color;
                    slot.color = glm::vec3(0.0f);
                    if (slot.name.empty()) slot.name = "Closed";
                } else if (type == "Color") {
                    slot.kind = SlotKind::Color;
                    glm::vec3 sum{0.0f};
                    int n = 0;
                    if (const OJson* colors = member(s, "colors"); colors && colors->is_array())
                        for (const OJson& c : *colors)
                            if (auto rgb = c.is_string() ? parseHexColor(c.get<std::string>()) : std::nullopt) {
                                sum += *rgb;
                                ++n;
                            }
                    if (n > 0) {
                        slot.color = sum / static_cast<float>(n);
                    } else if (const OJson* ct = member(s, "colorTemperature")) {
                        auto e = parseEntity(*ct);
                        if (e && e->unit == "K") slot.color = kelvinToLinear(static_cast<float>(e->value));
                    }
                    if (slot.name.empty()) slot.name = formatHexColor(slot.color);
                } else if (type == "Gobo") {
                    slot.kind = SlotKind::Gobo;
                    hasGobo = true;
                    slot.image = goboResource(member(s, "resource"), slot.name);
                    if (slot.name.empty()) slot.name = "Gobo";
                } else if (type == "Prism") {
                    slot.kind = SlotKind::Prism;
                    hasPrism = true;
                    const int facets = static_cast<int>(std::clamp(numberOf(s, "facets").value_or(3.0), 1.0, 1000.0));
                    const bool linear = slot.name.find("inear") != std::string::npos;
                    slot.facets = linear ? makeLinearPrismFacets(std::min(facets, dmxviz::kMaxPrismFacets), 4.0f * kDeg)
                                         : makeCircularPrismFacets(std::min(facets, dmxviz::kMaxPrismFacets), 5.0f * kDeg);
                    if (slot.name.empty()) slot.name = std::format("{}-facet prism", facets);
                } else if (type == "Frost") {
                    slot.kind = SlotKind::Frost;
                    if (const OJson* f = member(s, "frostIntensity"))
                        if (auto e = parseEntity(*f)) slot.frost = static_cast<float>(e->value);
                    if (slot.frost <= 0.0f) slot.frost = 1.0f;
                    if (slot.name.empty()) slot.name = "Frost";
                } else if (type == "AnimationGoboStart") {
                    slot.kind = SlotKind::AnimationWheel;
                    hasAnimation = true;
                    lastAnimationImage = standInGobo();
                    slot.image = lastAnimationImage;
                    if (slot.name.empty()) slot.name = "Animation";
                } else if (type == "AnimationGoboEnd") {
                    slot.kind = SlotKind::AnimationWheel;
                    slot.image = lastAnimationImage.empty() ? standInGobo() : lastAnimationImage;
                    if (slot.name.empty()) slot.name = "Animation (end)";
                } else {
                    warn(std::format("wheel \"{}\": unknown slot type \"{}\" treated as open", wheel.name, type));
                }
                wheel.slots.push_back(std::move(slot));
            }
        }
        if (wheel.slots.empty()) wheel.slots.push_back(WheelSlot{SlotKind::Open, "Open"});

        WheelInfo info;
        info.index = static_cast<int>(t_.wheels.size());
        if (hasAnimation) {
            info.family = AttributeFamily::Animation;
            info.familyIndex = animationCount++;
        } else if (hasGobo) {
            info.family = AttributeFamily::Gobo;
            info.familyIndex = std::min(goboCount++, 1);
            hasGoboWheel_ = true;
        } else if (hasPrism) {
            info.family = AttributeFamily::Prism;
            info.familyIndex = std::min(prismCount++, 1);
        } else {
            info.family = AttributeFamily::ColorWheel;
            info.familyIndex = std::min(colorCount++, 2);
        }
        if (goboCount > 2 || colorCount > 3 || prismCount > 2 || animationCount > 1)
            warn(std::format("wheel \"{}\" exceeds the number of wheels DmxViz renders; it shares a slot", wheel.name));
        wheelInfo_[wheel.name] = info;
        t_.wheels.push_back(std::move(wheel));
    }
}

int OflImport::prismWheel() {
    for (const auto& [name, info] : wheelInfo_)
        if (info.family == AttributeFamily::Prism) return info.index;
    if (virtualPrismWheel_ >= 0) return virtualPrismWheel_;
    // OFL "Prism" capabilities without a prism wheel: model the prism as a two-slot wheel.
    Wheel wheel;
    wheel.name = "Prism";
    wheel.slots.push_back(WheelSlot{SlotKind::Open, "Open"});
    WheelSlot prism;
    prism.kind = SlotKind::Prism;
    prism.name = "Prism";
    prism.facets = makeCircularPrismFacets(3, 5.0f * kDeg);
    wheel.slots.push_back(prism);
    virtualPrismWheel_ = static_cast<int>(t_.wheels.size());
    wheelInfo_[wheel.name] = {virtualPrismWheel_, AttributeFamily::Prism, 0};
    t_.wheels.push_back(std::move(wheel));
    return virtualPrismWheel_;
}

int OflImport::colorPresetWheel(const ChannelDef& def) {
    if (auto it = presetWheels_.find(def.key); it != presetWheels_.end()) return it->second;
    Wheel wheel;
    wheel.name = def.key + " presets";
    // Slot 1 "Off" keeps the mixed colour; presets start at slot 2.
    wheel.slots.push_back(WheelSlot{SlotKind::Open, "Off"});
    const int index = static_cast<int>(t_.wheels.size());
    t_.wheels.push_back(std::move(wheel));
    wheelInfo_[t_.wheels.back().name] = {index, AttributeFamily::ColorMacro, 0};
    presetWheels_[def.key] = index;
    return index;
}

// -------------------------------------------------------------- channels

void OflImport::collectChannels() {
    auto addDef = [&](const std::string& key, const OJson& json, const std::string& pixelKey) {
        ChannelDef def;
        def.key = key;
        def.json = &json;
        def.pixelKey = pixelKey;
        if (const OJson* fines = member(json, "fineChannelAliases"); fines && fines->is_array())
            for (const OJson& f : *fines)
                if (f.is_string()) def.fineKeys.push_back(replaceAll(f.get<std::string>(), "$pixelKey", pixelKey));
        const int declared = bytesOfResolution(stringOf(json, "dmxValueResolution"));
        def.definitionBytes = declared > 0 ? declared : 1 + static_cast<int>(def.fineKeys.size());
        for (std::size_t i = 0; i < def.fineKeys.size(); ++i) fineOf_[def.fineKeys[i]] = {key, static_cast<int>(i) + 1};
        // Switching channels: aliases whose meaning depends on this channel's capability.
        if (const OJson* caps = member(json, "capabilities"); caps && caps->is_array()) {
            for (std::size_t c = 0; c < caps->size(); ++c) {
                const OJson* sw = member((*caps)[c], "switchChannels");
                if (!sw || !sw->is_object()) continue;
                for (auto it = sw->begin(); it != sw->end(); ++it) {
                    const std::string alias = replaceAll(it.key(), "$pixelKey", pixelKey);
                    SwitchAlias& s = switches_[alias];
                    s.master = key;
                    s.targets.resize(caps->size());
                    s.targets[c] = it.value().is_string() ? replaceAll(it.value().get<std::string>(), "$pixelKey", pixelKey)
                                                          : std::string();
                }
            }
        }
        defs_[key] = std::move(def);
    };

    if (const OJson* available = member(root_, "availableChannels"); available && available->is_object())
        for (auto it = available->begin(); it != available->end(); ++it) addDef(it.key(), it.value(), {});

    if (const OJson* templates = member(root_, "templateChannels"); templates && templates->is_object()) {
        std::vector<std::string> keys;
        for (const Pixel& p : pixels_) keys.push_back(p.key);
        for (const auto& [group, members] : pixelGroups_) keys.push_back(group);
        for (auto it = templates->begin(); it != templates->end(); ++it)
            for (const std::string& k : keys) {
                const std::string resolved = replaceAll(it.key(), "$pixelKey", k);
                if (!defs_.contains(resolved)) addDef(resolved, it.value(), k);  // available channels override
            }
    }
}

void OflImport::scanFixture() {
    // Fixture-wide facts that change how individual capabilities map.
    bool r = false, g = false, b = false;
    for (const auto& [key, def] : defs_) {
        std::vector<const OJson*> caps;
        if (const OJson* c = member(*def.json, "capability")) caps.push_back(c);
        if (const OJson* cs = member(*def.json, "capabilities"); cs && cs->is_array())
            for (const OJson& c : *cs) caps.push_back(&c);
        for (const OJson* cap : caps) {
            const std::string type = stringOf(*cap, "type");
            if (type == "ColorIntensity") {
                const std::string color = stringOf(*cap, "color");
                r = r || color == "Red";
                g = g || color == "Green";
                b = b || color == "Blue";
            }
            if (type == "Pan" || type == "Tilt" || type == "PanContinuous" || type == "TiltContinuous") hasPanTilt_ = true;
            if (type == "Zoom") hasZoom_ = true;
        }
    }
    hasAdditiveRgb_ = r || g || b;
}

std::string OflImport::channelGeometry(const ChannelDef& def) {
    using namespace geometry_names;
    if (!def.pixelKey.empty()) {
        for (const Pixel& p : pixels_)
            if (p.key == def.pixelKey) return pixelGeometryName(p.key);
        for (const auto& [group, members] : pixelGroups_) {
            if (group != def.pixelKey) continue;
            if (members.size() == pixels_.size()) return {};  // "all": the whole fixture
            const std::string name = "Pixel group " + group;
            if (!t_.findGroup(name)) {
                GeometryGroup g;
                g.name = name;
                for (const std::string& m : members) g.members.push_back(pixelGeometryName(m));
                t_.geometryGroups.push_back(std::move(g));
            }
            return name;
        }
    }
    // Pan turns the yoke, tilt the head; everything else applies to the whole fixture.
    std::vector<const OJson*> caps;
    if (const OJson* c = member(*def.json, "capability")) caps.push_back(c);
    if (const OJson* cs = member(*def.json, "capabilities"); cs && cs->is_array())
        for (const OJson& c : *cs) caps.push_back(&c);
    for (const OJson* cap : caps) {
        const std::string type = stringOf(*cap, "type");
        if (type == "Pan" || type == "PanContinuous") return kYoke;
        if (type == "Tilt" || type == "TiltContinuous") return kHead;
    }
    return {};
}

void OflImport::convertCapability(const ChannelDef& def, const OJson& cap, CapabilityFunctions& out) {
    using A = Attribute;
    const std::string type = stringOf(cap, "type");
    const std::string comment = stringOf(cap, "comment");
    const std::string channelName = replaceAll(stringOf(*def.json, "name", def.key), "$pixelKey", def.pixelKey);

    auto make = [&](Attribute a, float from, float to, FunctionKind kind) {
        ChannelFunction f;
        f.attribute = a;
        f.kind = kind;
        f.physicalFrom = from;
        f.physicalTo = to;
        f.name = comment;
        return f;
    };
    auto linear = [&](Attribute a, float from, float to) { return make(a, from, to, attributeInfo(a).defaultKind); };
    auto unknown = [&](const std::string& name) {
        ChannelFunction f = make(A::Unknown, 0, 1, FunctionKind::Linear);
        f.attributeName = name;
        return f;
    };
    auto push = [&](ChannelFunction f) { out.functions.push_back(std::move(f)); };
    auto range = [&](const char* prop) { return entityRange(cap, prop); };
    auto ratio = [](const Entity& e) { return static_cast<float>(e.value); };

    // Wheel lookups for wheel capabilities ("wheel" may be a string or an array).
    auto wheelNamed = [&]() -> const WheelInfo* {
        std::string name = channelName;
        if (const OJson* w = member(cap, "wheel")) {
            if (w->is_string()) name = w->get<std::string>();
            else if (w->is_array() && !w->empty() && (*w)[0].is_string()) name = (*w)[0].get<std::string>();
        }
        auto it = wheelInfo_.find(name);
        if (it == wheelInfo_.end()) it = wheelInfo_.find(def.key);
        return it == wheelInfo_.end() ? nullptr : &it->second;
    };
    auto slotFunction = [&](const WheelInfo& w, float slotFrom, float slotTo) {
        const Attribute attr = numberedAttribute(w.family, w.familyIndex);
        ChannelFunction f = make(attr, 0, 0, FunctionKind::WheelSlot);
        f.wheel = t_.wheels[static_cast<std::size_t>(w.index)].name;
        f.slotFrom = slotFrom;
        f.slotTo = slotTo;
        if (f.name.empty() && slotFrom == slotTo) {
            const auto& slots = t_.wheels[static_cast<std::size_t>(w.index)].slots;
            const double s = slotFrom - 1.0;  // as double: converting a huge slot number to int is undefined
            if (slotFrom == std::floor(slotFrom) && s >= 0.0 && s < static_cast<double>(slots.size()))
                f.name = slots[static_cast<std::size_t>(s)].name;
        }
        return f;
    };
    auto slotRange = [&]() -> std::optional<std::pair<float, float>> {
        if (auto r = range("slotNumber")) return std::make_pair(ratio(r->first), ratio(r->second));
        return std::nullopt;
    };
    // Rotation of the selected slot: index angle or spin speed.
    auto rotationFunction = [&](AttributeFamily posFamily, AttributeFamily rotateFamily, int index) -> bool {
        if (auto speed = range("speed")) {
            push(linear(numberedAttribute(rotateFamily, index), toAngularSpeed(speed->first), toAngularSpeed(speed->second)));
            return true;
        }
        if (auto angle = range("angle")) {
            auto rad = [](const Entity& e) { return e.percent() ? static_cast<float>(e.value) * 2.0f * kPi : static_cast<float>(e.value) * kDeg; };
            push(linear(numberedAttribute(posFamily, index), rad(angle->first), rad(angle->second)));
            return true;
        }
        return false;
    };

    if (type == "NoFunction") {
        push(make(A::NoFeature, 0, 0, FunctionKind::NoFeature));
    } else if (type == "ShutterStrobe") {
        const std::string effect = stringOf(cap, "shutterEffect");
        const OJson* randomTiming = member(cap, "randomTiming");
        const bool random = randomTiming && randomTiming->is_boolean() && randomTiming->get<bool>();
        float f0 = 0.0f, f1 = 0.0f;  // 0 = rate from a separate strobe speed channel or the default
        if (auto speed = range("speed")) {
            f0 = toHertz(speed->first);
            f1 = toHertz(speed->second);
        } else if (auto duration = range("duration")) {
            const float d0 = toSeconds(duration->first, 1.0f), d1 = toSeconds(duration->second, 1.0f);
            f0 = d0 > 0 ? 1.0f / d0 : 0.0f;
            f1 = d1 > 0 ? 1.0f / d1 : 0.0f;
        }
        if (effect == "Open") push(make(A::Shutter1, 0, 0, FunctionKind::ShutterOpen));
        else if (effect == "Closed") push(make(A::Shutter1, 0, 0, FunctionKind::ShutterClosed));
        else if (effect == "Pulse") push(linear(random ? A::Shutter1StrobeRandomPulse : A::Shutter1StrobePulse, f0, f1));
        else if (effect == "RampUp") push(linear(A::Shutter1StrobeRampUp, f0, f1));
        else if (effect == "RampDown") push(linear(A::Shutter1StrobeRampDown, f0, f1));
        else if (effect == "RampUpDown") push(linear(A::Shutter1StrobeRampUpDown, f0, f1));
        else if (effect == "Lightning") push(linear(A::Shutter1StrobeLightning, f0, f1));
        else if (effect == "Spikes") push(linear(A::Shutter1StrobeSpikes, f0, f1));
        else push(linear(random ? A::Shutter1StrobeRandom : A::Shutter1Strobe, f0, f1));  // Strobe, Burst
    } else if (type == "StrobeSpeed") {
        auto speed = range("speed");
        push(linear(A::StrobeFrequency, speed ? toHertz(speed->first) : 0.0f, speed ? toHertz(speed->second) : 25.0f));
    } else if (type == "StrobeDuration") {
        auto d = range("duration");
        push(linear(A::StrobeDuration, d ? toSeconds(d->first, 0.5f) : 0.0f, d ? toSeconds(d->second, 0.5f) : 0.5f));
    } else if (type == "Intensity") {
        auto b = range("brightness");
        auto level = [&](const Entity& e) {
            if (e.unit == "lm") return lumens_ > 0 ? static_cast<float>(e.value) / lumens_ : 1.0f;
            return ratio(e);
        };
        push(linear(A::Dimmer, b ? level(b->first) : 0.0f, b ? level(b->second) : 1.0f));
    } else if (type == "ColorIntensity") {
        static const std::map<std::string, Attribute, std::less<>> colors = {
            {"Red", A::ColorAdd_R},  {"Green", A::ColorAdd_G},       {"Blue", A::ColorAdd_B},
            {"Cyan", A::ColorAdd_C}, {"Magenta", A::ColorAdd_M},     {"Yellow", A::ColorAdd_Y},
            {"Amber", A::ColorAdd_A}, {"White", A::ColorAdd_W},       {"Warm White", A::ColorAdd_WW},
            {"Cold White", A::ColorAdd_CW}, {"UV", A::ColorAdd_UV}, {"Lime", A::ColorAdd_Lime},
            {"Indigo", A::ColorAdd_Indigo}};
        const std::string color = stringOf(cap, "color");
        Attribute a = A::Unknown;
        if (auto it = colors.find(color); it != colors.end()) a = it->second;
        // CMY on a lamp fixture is subtractive (colour flags), on an LED fixture additive.
        if (!hasAdditiveRgb_) {
            if (a == A::ColorAdd_C) a = A::ColorSub_C;
            if (a == A::ColorAdd_M) a = A::ColorSub_M;
            if (a == A::ColorAdd_Y) a = A::ColorSub_Y;
        }
        auto b = range("brightness");
        if (a == A::Unknown) push(unknown("ColorIntensity " + color));
        else push(linear(a, b ? ratio(b->first) : 0.0f, b ? ratio(b->second) : 1.0f));
    } else if (type == "ColorPreset") {
        glm::vec3 color{1.0f};
        glm::vec3 sum{0.0f};
        int n = 0;
        if (const OJson* cs = member(cap, "colors"); cs && cs->is_array())
            for (const OJson& c : *cs)
                if (auto rgb = c.is_string() ? parseHexColor(c.get<std::string>()) : std::nullopt) {
                    sum += *rgb;
                    ++n;
                }
        if (n > 0) color = normalizeMax(sum / static_cast<float>(n));
        else if (auto k = range("colorTemperature"); k && k->first.unit == "K") color = kelvinToLinear(static_cast<float>(k->first.value));
        const int wheelIndex = colorPresetWheel(def);
        Wheel& wheel = t_.wheels[static_cast<std::size_t>(wheelIndex)];
        WheelSlot slot;
        slot.kind = SlotKind::Color;
        slot.color = color;
        slot.name = comment.empty() ? formatHexColor(color) : comment;
        wheel.slots.push_back(slot);
        const float s = static_cast<float>(wheel.slots.size());
        push(slotFunction(wheelInfo_[wheel.name], s, s));
    } else if (type == "ColorTemperature") {
        auto k = range("colorTemperature");
        auto kelvin = [&](const Entity& e) {
            if (e.unit == "K") return static_cast<float>(e.value);
            const float p = static_cast<float>(e.value);
            return p < 0 ? kelvin_ + (3200.0f - kelvin_) * -p : kelvin_ + (9000.0f - kelvin_) * p;
        };
        const float k0 = k ? kelvin(k->first) : kelvin_, k1 = k ? kelvin(k->second) : kelvin_;
        Attribute a = A::CTC;
        if (std::max(k0, k1) <= kelvin_ + 1.0f) a = A::CTO;
        else if (std::min(k0, k1) >= kelvin_ - 1.0f) a = A::CTB;
        push(linear(a, k0, k1));
    } else if (type == "Pan" || type == "Tilt") {
        const bool pan = type == "Pan";
        const float max = pan ? panMax_ : tiltMax_;
        auto angle = range("angle");
        auto deg = [&](const Entity& e) { return e.percent() ? static_cast<float>(e.value) * max : static_cast<float>(e.value); };
        float a0 = angle ? deg(angle->first) : 0.0f, a1 = angle ? deg(angle->second) : max;
        // DmxViz's home (0) is the middle of the range: OFL's 0..540 deg becomes -270..270.
        float lo = 1e9f, hi = -1e9f;
        std::vector<const OJson*> caps;
        if (const OJson* c = member(*def.json, "capability")) caps.push_back(c);
        if (const OJson* cs = member(*def.json, "capabilities"); cs && cs->is_array())
            for (const OJson& c : *cs) caps.push_back(&c);
        for (const OJson* c : caps)
            if (stringOf(*c, "type") == type)
                if (auto r = entityRange(*c, "angle")) {
                    lo = std::min({lo, deg(r->first), deg(r->second)});
                    hi = std::max({hi, deg(r->first), deg(r->second)});
                }
        const float centre = lo <= hi ? 0.5f * (lo + hi) : 0.5f * max;
        push(linear(pan ? A::Pan : A::Tilt, (a0 - centre) * kDeg, (a1 - centre) * kDeg));
    } else if (type == "PanContinuous" || type == "TiltContinuous") {
        auto speed = range("speed");
        push(linear(type == "PanContinuous" ? A::PanRotate : A::TiltRotate, speed ? toAngularSpeed(speed->first) : 0.0f,
                    speed ? toAngularSpeed(speed->second) : 0.0f));
    } else if (type == "PanTiltSpeed") {
        push(linear(A::PositionMSpeed, 0.0f, 1.0f));
    } else if (type == "WheelSlot" || type == "WheelShake") {
        const WheelInfo* w = wheelNamed();
        auto slots = slotRange();
        if (w && slots) push(slotFunction(*w, slots->first, slots->second));
        else if (!w) {
            warn(std::format("channel \"{}\": wheel not found", def.key));
            push(unknown(type));
        } else push(unknown(type));
    } else if (type == "WheelSlotRotation") {
        const WheelInfo* w = wheelNamed();
        if (w && w->family != AttributeFamily::ColorWheel && w->family != AttributeFamily::ColorMacro) {
            const AttributeFamily pos = w->family == AttributeFamily::Gobo ? AttributeFamily::GoboPos
                                        : w->family == AttributeFamily::Prism ? AttributeFamily::PrismPos
                                                                               : AttributeFamily::AnimationPos;
            const AttributeFamily rot = w->family == AttributeFamily::Gobo ? AttributeFamily::GoboPosRotate
                                        : w->family == AttributeFamily::Prism ? AttributeFamily::PrismPosRotate
                                                                               : AttributeFamily::AnimationPosRotate;
            if (!rotationFunction(pos, rot, w->familyIndex)) push(unknown(type));
            if (auto slots = slotRange()) push(slotFunction(*w, slots->first, slots->second));
        } else {
            push(unknown(type));
        }
    } else if (type == "WheelRotation") {
        const WheelInfo* w = wheelNamed();
        auto speed = range("speed");
        if (w && speed && (w->family == AttributeFamily::ColorWheel || w->family == AttributeFamily::Gobo)) {
            const AttributeFamily f =
                w->family == AttributeFamily::Gobo ? AttributeFamily::GoboWheelSpin : AttributeFamily::ColorWheelSpin;
            ChannelFunction fn = linear(numberedAttribute(f, w->familyIndex), toAngularSpeed(speed->first),
                                        toAngularSpeed(speed->second));
            fn.wheel = t_.wheels[static_cast<std::size_t>(w->index)].name;
            push(fn);
        } else {
            push(unknown(type));
        }
    } else if (type == "Prism") {
        const int wheel = prismWheel();
        const WheelInfo& w = wheelInfo_[t_.wheels[static_cast<std::size_t>(wheel)].name];
        // Insert the first prism slot; "N-facet" in the comment picks a matching prism.
        int slot = 0;
        const auto& slots = t_.wheels[static_cast<std::size_t>(wheel)].slots;
        std::smatch m;
        const std::string text = comment + " " + channelName;
        int facets = 0;
        if (std::regex_search(text, m, std::regex(R"((\d+)\s*-?\s*facet)"))) facets = std::stoi(m[1].str());
        for (std::size_t i = 0; i < slots.size(); ++i)
            if (slots[i].kind == SlotKind::Prism &&
                (slot == 0 || (facets > 0 && static_cast<int>(slots[i].facets.size()) == facets)))
                slot = static_cast<int>(i) + 1;
        if (slot == 0) slot = 2;
        push(slotFunction(w, static_cast<float>(slot), static_cast<float>(slot)));
        rotationFunction(AttributeFamily::PrismPos, AttributeFamily::PrismPosRotate, w.familyIndex);
    } else if (type == "PrismRotation") {
        const WheelInfo& w = wheelInfo_[t_.wheels[static_cast<std::size_t>(prismWheel())].name];
        if (!rotationFunction(AttributeFamily::PrismPos, AttributeFamily::PrismPosRotate, w.familyIndex))
            push(unknown(type));
    } else if (type == "Zoom" || type == "BeamAngle") {
        auto angle = range("angle");
        const float lo = hasLens_ ? std::max(lensMin_, 0.5f) : 5.0f, hi = hasLens_ ? std::max(lensMax_, lo) : 50.0f;
        auto deg = [&](const Entity& e) { return e.percent() ? lo + (hi - lo) * static_cast<float>(e.value) : static_cast<float>(e.value); };
        push(linear(A::Zoom, (angle ? deg(angle->first) : lo) * kDeg, (angle ? deg(angle->second) : hi) * kDeg));
    } else if (type == "Focus") {
        auto d = range("distance");
        float d0 = 0.0f, d1 = 1.0f;
        if (d) {
            d0 = static_cast<float>(d->first.value);
            d1 = static_cast<float>(d->second.value);
            if (d->first.unit == "m" || d->second.unit == "m") {  // metres: far end = sharp (1)
                const float far = std::max({d0, d1, 1e-3f});
                d0 /= far;
                d1 /= far;
            }
        }
        push(linear(A::Focus1, d0, d1));
    } else if (type == "Iris") {
        auto o = range("openPercent");
        push(linear(A::Iris, o ? ratio(o->first) : 1.0f, o ? ratio(o->second) : 0.0f));
    } else if (type == "Frost") {
        auto f = range("frostIntensity");
        push(linear(A::Frost1, f ? ratio(f->first) : 0.0f, f ? ratio(f->second) : 1.0f));
    } else if (type == "BladeInsertion" || type == "BladeRotation") {
        int blade = 1;
        if (const OJson* b = member(cap, "blade")) {
            if (b->is_number()) blade = static_cast<int>(clampedInt(*b, 1, 4));
            else if (b->is_string()) {
                const std::string s = b->get<std::string>();
                blade = s == "Top" ? 1 : s == "Right" ? 2 : s == "Bottom" ? 3 : s == "Left" ? 4 : 1;
            }
        }
        blade = std::clamp(blade, 1, 4) - 1;
        if (type == "BladeInsertion") {
            auto in = range("insertion");
            push(linear(numberedAttribute(AttributeFamily::BladeA, blade), in ? ratio(in->first) : 0.0f,
                        in ? ratio(in->second) : 1.0f));
        } else {
            auto a = range("angle");
            push(linear(numberedAttribute(AttributeFamily::BladeRot, blade),
                        a ? static_cast<float>(a->first.value) * kDeg : 0.0f,
                        a ? static_cast<float>(a->second.value) * kDeg : 0.0f));
        }
    } else if (type == "BladeSystemRotation") {
        auto a = range("angle");
        push(linear(A::ShaperRot, a ? static_cast<float>(a->first.value) * kDeg : 0.0f,
                    a ? static_cast<float>(a->second.value) * kDeg : 0.0f));
    } else if (type == "Effect") {
        ChannelFunction f = linear(A::Effects1, 0, 1);
        if (f.name.empty()) f.name = stringOf(cap, "effectName", stringOf(cap, "effectPreset"));
        push(f);
    } else if (type == "EffectSpeed") {
        push(linear(A::Effects1Rate, 0, 1));
    } else if (type == "EffectDuration") {
        push(linear(A::Effects1Fade, 0, 1));
    } else if (type == "EffectParameter") {
        push(linear(A::Effects1Adjust1, 0, 1));
    } else if (type == "Maintenance") {
        push(linear(A::Control, 0, 1));
    } else {
        push(unknown(type.empty() ? "Generic" : type));
    }
}

std::vector<CapabilityFunctions> OflImport::convertCapabilities(const ChannelDef& def) {
    // Converted once per channel: some capabilities add wheel slots (colour presets).
    if (auto it = converted_.find(def.key); it != converted_.end()) return it->second;
    std::vector<CapabilityFunctions>& out = converted_[def.key];
    const std::uint32_t maxValue = maxDmxValue(def.definitionBytes);
    if (const OJson* cap = member(*def.json, "capability")) {
        CapabilityFunctions cf;
        cf.from = 0;
        cf.to = maxValue;
        convertCapability(def, *cap, cf);
        out.push_back(std::move(cf));
        return out;
    }
    const OJson* caps = member(*def.json, "capabilities");
    if (!caps || !caps->is_array()) return out;
    for (const OJson& cap : *caps) {
        CapabilityFunctions cf;
        if (const OJson* r = member(cap, "dmxRange"); r && r->is_array() && r->size() == 2) {
            cf.from = dmxNumber((*r)[0]);
            cf.to = dmxNumber((*r)[1]);
        }
        convertCapability(def, cap, cf);
        out.push_back(std::move(cf));
    }

    // LED-par quirk: many OFL files mark "no strobe" as ShutterStrobe Closed. If a
    // shutter channel has strobe ranges but no Open range, its Closed range at 0 means open.
    bool hasOpen = false, hasStrobe = false;
    for (const CapabilityFunctions& cf : out)
        for (const ChannelFunction& f : cf.functions) {
            hasOpen = hasOpen || f.kind == FunctionKind::ShutterOpen;
            hasStrobe = hasStrobe || isStrobeKind(f.kind);
        }
    if (!hasOpen && hasStrobe && !out.empty() && out.front().from == 0)
        for (ChannelFunction& f : out.front().functions)
            if (f.kind == FunctionKind::ShutterClosed) f.kind = FunctionKind::ShutterOpen;
    return out;
}

void OflImport::appendFunctions(Channel& channel, const ChannelDef& def, int channelBytes, const std::string& master,
                                std::uint32_t masterFrom, std::uint32_t masterTo) {
    std::uint32_t previousTo = 0;
    bool first = true;
    for (CapabilityFunctions& cf : convertCapabilities(def)) {
        std::uint32_t from = convertDmxResolution(cf.from, def.definitionBytes, channelBytes, false);
        const std::uint32_t to = convertDmxResolution(cf.to, def.definitionBytes, channelBytes, true);
        // Down-scaling (16-bit definition, 8-bit mode) can collapse ranges: keep them disjoint.
        if (!first && from <= previousTo) from = previousTo + 1;
        if (from > to) continue;
        first = false;
        previousTo = to;
        for (ChannelFunction& f : cf.functions) {
            f.dmxFrom = from;
            f.dmxTo = to;
            f.modeMaster = master;
            f.modeFrom = masterFrom;
            f.modeTo = masterTo;
            channel.functions.push_back(std::move(f));
        }
    }
}

// -------------------------------------------------------------- modes

bool OflImport::buildModes(std::string* error) {
    const OJson* modes = member(root_, "modes");
    if (!modes || !modes->is_array() || modes->empty()) {
        if (error) *error = "fixture has no modes";
        return false;
    }
    auto dmxValueOf = [](const OJson& v, int defBytes, int channelBytes) -> std::optional<std::uint32_t> {
        if (v.is_number_unsigned() || v.is_number_integer())
            return convertDmxResolution(static_cast<std::uint32_t>(std::max<std::int64_t>(0, v.get<std::int64_t>())),
                                        defBytes, channelBytes);
        if (v.is_string()) {
            auto e = parseEntity(v);
            if (e && e->percent())
                return static_cast<std::uint32_t>(
                    std::lround(std::clamp(e->value, 0.0, 1.0) * static_cast<double>(maxDmxValue(channelBytes))));
        }
        return std::nullopt;
    };

    for (const OJson& mj : *modes) {
        DmxMode mode;
        mode.name = stringOf(mj, "name", "Mode");
        const std::string shortName = stringOf(mj, "shortName");
        if (!shortName.empty() && shortName != mode.name) mode.description = shortName;

        // 1. Expand the channel list (matrix inserts) into keys by position.
        std::vector<std::string> keys;  // "" = unused slot
        const OJson* list = member(mj, "channels");
        if (list && list->is_array()) {
            for (const OJson& entry : *list) {
                if (entry.is_null()) {
                    keys.emplace_back();
                } else if (entry.is_string()) {
                    keys.push_back(entry.get<std::string>());
                } else if (entry.is_object() && stringOf(entry, "insert") == "matrixChannels") {
                    std::vector<std::string> repeat;
                    const OJson* rf = member(entry, "repeatFor");
                    if (rf && rf->is_array()) {
                        for (const OJson& k : *rf)
                            if (k.is_string()) repeat.push_back(k.get<std::string>());
                    } else if (rf && rf->is_string()) {
                        const std::string order = rf->get<std::string>();
                        if (order == "eachPixelABC") repeat = pixelKeysAbc();
                        else if (order == "eachPixelGroup")
                            for (const auto& g : pixelGroups_) repeat.push_back(g.first);
                        else if (order.size() == 12 && order.rfind("eachPixel", 0) == 0)
                            repeat = pixelKeysByOrder(order[9], order[10], order[11]);
                    }
                    std::vector<std::string> templates;
                    if (const OJson* tc = member(entry, "templateChannels"); tc && tc->is_array())
                        for (const OJson& k : *tc) templates.push_back(k.is_string() ? k.get<std::string>() : std::string());
                    const bool perPixel = stringOf(entry, "channelOrder", "perPixel") == "perPixel";
                    if (perPixel) {
                        for (const std::string& p : repeat)
                            for (const std::string& tpl : templates)
                                keys.push_back(tpl.empty() ? std::string() : replaceAll(tpl, "$pixelKey", p));
                    } else {
                        for (const std::string& tpl : templates)
                            for (const std::string& p : repeat)
                                keys.push_back(tpl.empty() ? std::string() : replaceAll(tpl, "$pixelKey", p));
                    }
                }
            }
        }
        mode.footprint = static_cast<int>(keys.size());

        // 2. Place coarse, fine and switching channels.
        std::map<std::string, std::uint16_t, std::less<>> position;  // key -> 1-based offset
        for (std::size_t i = 0; i < keys.size(); ++i)
            if (!keys[i].empty()) position[keys[i]] = static_cast<std::uint16_t>(i + 1);

        auto fineOffsets = [&](const ChannelDef& def, std::vector<std::uint16_t>& offsets) {
            for (const std::string& fine : def.fineKeys) {
                auto it = position.find(fine);
                if (it == position.end()) break;  // 24-bit needs the 16-bit byte too
                offsets.push_back(it->second);
            }
        };

        for (std::size_t i = 0; i < keys.size(); ++i) {
            const std::string& key = keys[i];
            if (key.empty() || fineOf_.contains(key)) continue;
            Channel channel;
            channel.name = key;
            channel.offsets.push_back(static_cast<std::uint16_t>(i + 1));

            if (auto sw = switches_.find(key); sw != switches_.end()) {
                // Switching channel: the union of its targets, each active for the master
                // capability ranges that select it.
                const SwitchAlias& alias = sw->second;
                bool isFineAlias = false;
                for (const std::string& target : alias.targets) isFineAlias = isFineAlias || fineOf_.contains(target);
                if (isFineAlias) continue;  // placed as the fine byte of its coarse alias below
                const ChannelDef* masterDef = defs_.contains(alias.master) ? &defs_.at(alias.master) : nullptr;
                const ChannelDef* firstTarget = nullptr;
                for (const std::string& target : alias.targets)
                    if (!target.empty() && defs_.contains(target)) {
                        firstTarget = &defs_.at(target);
                        break;
                    }
                // Fine bytes: aliases whose targets are the fine channels of our targets.
                for (const auto& [otherKey, other] : switches_) {
                    if (otherKey == key || other.master != alias.master || !position.contains(otherKey)) continue;
                    bool fineOfOurs = false;
                    for (std::size_t c = 0; c < other.targets.size() && c < alias.targets.size(); ++c)
                        if (auto f = fineOf_.find(other.targets[c]); f != fineOf_.end() && f->second.first == alias.targets[c])
                            fineOfOurs = true;
                    if (fineOfOurs) channel.offsets.push_back(position[otherKey]);
                }
                const int bytes = static_cast<int>(channel.offsets.size());
                if (masterDef && position.contains(alias.master)) {
                    std::vector<std::uint16_t> masterOffsets{position[alias.master]};
                    fineOffsets(*masterDef, masterOffsets);
                    const int masterBytes = static_cast<int>(masterOffsets.size());
                    const OJson* caps = member(*masterDef->json, "capabilities");
                    // Group consecutive master capabilities selecting the same target.
                    for (std::size_t c = 0; caps && c < alias.targets.size() && c < caps->size();) {
                        std::size_t e = c;
                        while (e + 1 < alias.targets.size() && e + 1 < caps->size() && alias.targets[e + 1] == alias.targets[c]) ++e;
                        const OJson* r0 = member((*caps)[c], "dmxRange");
                        const OJson* r1 = member((*caps)[e], "dmxRange");
                        if (r0 && r1 && r0->is_array() && r0->size() == 2 && r1->is_array() && r1->size() == 2) {
                            const std::uint32_t mFrom = convertDmxResolution(dmxNumber((*r0)[0]), masterDef->definitionBytes,
                                                                             masterBytes);
                            const std::uint32_t mTo = convertDmxResolution(dmxNumber((*r1)[1]), masterDef->definitionBytes,
                                                                           masterBytes, true);
                            const std::string& target = alias.targets[c];
                            if (!target.empty() && defs_.contains(target)) {
                                appendFunctions(channel, defs_.at(target), bytes, alias.master, mFrom, mTo);
                            } else {
                                ChannelFunction off;
                                off.attribute = Attribute::NoFeature;
                                off.kind = FunctionKind::NoFeature;
                                off.dmxTo = maxDmxValue(bytes);
                                off.modeMaster = alias.master;
                                off.modeFrom = mFrom;
                                off.modeTo = mTo;
                                channel.functions.push_back(off);
                            }
                        }
                        c = e + 1;
                    }
                }
                if (firstTarget) channel.geometry = channelGeometry(*firstTarget);
                if (channel.functions.empty()) {
                    ChannelFunction off;
                    off.kind = FunctionKind::NoFeature;
                    off.dmxTo = maxDmxValue(bytes);
                    channel.functions.push_back(off);
                }
                mode.channels.push_back(std::move(channel));
                continue;
            }

            auto defIt = defs_.find(key);
            if (defIt == defs_.end()) {
                warn(std::format("mode \"{}\": unknown channel \"{}\" left unused", mode.name, key));
                continue;
            }
            const ChannelDef& def = defIt->second;
            fineOffsets(def, channel.offsets);
            const int bytes = static_cast<int>(channel.offsets.size());
            channel.geometry = channelGeometry(def);
            if (const OJson* dv = member(*def.json, "defaultValue"))
                channel.defaultValue = std::min(dmxValueOf(*dv, def.definitionBytes, bytes).value_or(0), maxDmxValue(bytes));
            if (const OJson* hv = member(*def.json, "highlightValue"))
                if (auto h = dmxValueOf(*hv, def.definitionBytes, bytes)) channel.highlightValue = std::min(*h, maxDmxValue(bytes));
            appendFunctions(channel, def, bytes);
            if (channel.functions.empty()) {
                ChannelFunction off;
                off.kind = FunctionKind::NoFeature;
                off.dmxTo = maxDmxValue(bytes);
                channel.functions.push_back(off);
            }
            mode.channels.push_back(std::move(channel));
        }
        t_.modes.push_back(std::move(mode));
    }
    return true;
}

// -------------------------------------------------------------- geometry

void OflImport::buildGeometry() {
    const std::string mainCategory = t_.categories.empty() ? std::string() : t_.categories.front();
    auto inCategory = [&](std::string_view c) {
        return std::find(t_.categories.begin(), t_.categories.end(), c) != t_.categories.end();
    };
    const bool blinder = mainCategory == "Blinder" || mainCategory == "Strobe";

    // A CTO (CTB) channel's warmest-to-coolest range starts at "no correction": that
    // is the source's real colour temperature, whatever the bulb data says.
    for (const DmxMode& mode : t_.modes)
        for (const Channel& c : mode.channels)
            for (const ChannelFunction& f : c.functions) {
                if (f.attribute == Attribute::CTO) kelvin_ = std::max(f.physicalFrom, f.physicalTo);
                if (f.attribute == Attribute::CTB) kelvin_ = std::min(f.physicalFrom, f.physicalTo);
            }

    GeometryRecipe recipe;
    recipe.movingHead = hasPanTilt_ || inCategory("Moving Head") || inCategory("Scanner") || inCategory("Barrel Scanner");
    const glm::vec3 d = dimensionsMm_ * 0.001f;
    // OFL dimensions are width x height x depth of the fixture standing on the floor.
    // Hanging with the beam pointing down, a static fixture's depth is our Y.
    recipe.size = recipe.movingHead ? glm::vec3(d.x, d.y, d.z) : glm::vec3(d.x, d.z, d.y);

    BeamSpec& beam = recipe.beam;
    if (blinder) beam.type = BeamType::Rectangle;
    else if (recipe.movingHead && hasLens_ && lensMax_ <= 6.0f) beam.type = BeamType::Beam;
    else if (recipe.movingHead && hasGoboWheel_) beam.type = BeamType::Spot;
    else if (mainCategory == "Effect" || mainCategory == "Laser" || mainCategory == "Flower") beam.type = BeamType::Beam;
    else beam.type = BeamType::Wash;

    float angle = 25.0f;
    // With a zoom channel the zoom sets the angle; a range starting at 0 (beam fixtures) means "up to".
    if (hasLens_) angle = std::max(hasZoom_ || lensMin_ <= 0.0f ? lensMax_ : 0.5f * (lensMin_ + lensMax_), 0.5f);
    else if (beam.type == BeamType::Spot) angle = 20.0f;
    else if (beam.type == BeamType::Beam) angle = 4.0f;
    else if (blinder) angle = 60.0f;
    beam.beamAngle = angle * kDeg;
    const float fieldFactor = beam.type == BeamType::Wash ? 1.4f : (beam.type == BeamType::Rectangle ? 1.15f : 1.2f);
    beam.fieldAngle = std::min(beam.beamAngle * fieldFactor, 175.0f * kDeg);
    beam.colorTemperature = std::clamp(kelvin_, 1000.0f, 25000.0f);
    const float lumens = lumens_ > 0.0f ? lumens_ : (t_.physical.power > 0.0f ? 30.0f * t_.physical.power : 3000.0f);
    beam.luminousFlux = pixels_.empty() ? lumens : lumens / static_cast<float>(pixels_.size());
    beam.lensRadius = 0.08f;
    beam.emitterSize = glm::vec2(0.15f);

    for (const Pixel& p : pixels_) recipe.pixels.push_back({p.key, p.position});
    if (pixelSizeMm_.x > 0.0f || pixelSpacingMm_.x > 0.0f)
        recipe.pixelPitch = glm::vec2(pixelSizeMm_.x + pixelSpacingMm_.x, pixelSizeMm_.y + pixelSpacingMm_.y) * 0.001f;
    if (!recipe.movingHead && recipe.pixels.empty()) {
        // Round can (par, PC) or flat box (LED panel, blinder)?
        const bool round = recipe.size.x > 0 && std::abs(recipe.size.x - recipe.size.z) < 0.25f * recipe.size.x &&
                           !blinder && recipe.size.y > 0.4f * recipe.size.x;
        recipe.bodyShape = round ? PrimitiveShape::Conventional : PrimitiveShape::Box;
    }
    t_.geometry = buildFixtureGeometry(recipe);
}

std::optional<FixtureType> OflImport::run(std::string* error) {
    if (!root_.is_object()) {
        if (error) *error = "not a JSON object";
        return std::nullopt;
    }
    if (!options_.standInGoboDir.empty()) {
        std::error_code ec;
        for (const auto& entry : fs::directory_iterator(options_.standInGoboDir, ec)) {
            const std::string ext = entry.path().extension().string();
            if (ext == ".svg" || ext == ".png") standIns_.push_back(entry.path());
        }
        std::sort(standIns_.begin(), standIns_.end());
    }
    try {
        readMeta();
        readPhysical();
        readMatrix();
        readWheels();
        collectChannels();
        scanFixture();
        if (!buildModes(error)) return std::nullopt;
        buildGeometry();
    } catch (const std::exception& e) {
        // Wrong JSON types in unexpected places (nlohmann type_error) end up here.
        if (error) *error = std::string("malformed OFL fixture: ") + e.what();
        return std::nullopt;
    }
    for (const std::string& p : validateFixtureType(t_)) warn("validation: " + p);
    return std::move(t_);
}

}  // namespace

bool isOflJson(const nlohmann::ordered_json& json) {
    return json.is_object() && json.contains("$schema") && json["$schema"].is_string() &&
           json["$schema"].get<std::string>().find("open-fixture-library") != std::string::npos;
}

std::optional<FixtureType> importOfl(const nlohmann::ordered_json& json, const OflImportOptions& options,
                                     std::string* error, std::vector<std::string>* warnings) {
    OflImport import(json, options, warnings);
    return import.run(error);
}

std::optional<FixtureType> importOflFile(const std::filesystem::path& path, OflImportOptions options, std::string* error,
                                         std::vector<std::string>* warnings) {
    std::ifstream in(path, std::ios::binary);
    if (!in) {
        if (error) *error = "cannot open " + path.string();
        return std::nullopt;
    }
    nlohmann::ordered_json json;
    try {
        json = nlohmann::ordered_json::parse(in);
    } catch (const nlohmann::json::exception& e) {
        if (error) *error = path.filename().string() + ": invalid JSON: " + e.what();
        return std::nullopt;
    }
    // fixtures/<manufacturer>/<fixture>.json
    if (options.fixtureKey.empty() && !json.contains("fixtureKey")) options.fixtureKey = path.stem().string();
    if (options.manufacturerKey.empty() && !json.contains("manufacturerKey"))
        options.manufacturerKey = path.parent_path().filename().string();
    if (options.resourceDir.empty()) {
        for (fs::path dir = path.parent_path(); !dir.empty(); dir = dir.parent_path()) {
            std::error_code ec;
            if (fs::is_directory(dir / "resources" / "gobos", ec)) {
                options.resourceDir = dir / "resources";
                break;
            }
            if (dir == dir.parent_path()) break;
        }
    }
    auto type = importOfl(json, options, error, warnings);
    if (!type && error) *error = path.filename().string() + ": " + *error;
    return type;
}

}  // namespace dmxviz::fixtures
