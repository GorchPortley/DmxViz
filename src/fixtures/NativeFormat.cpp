#include "fixtures/NativeFormat.h"

#include "core/Log.h"
#include "fixtures/Archive.h"
#include "fixtures/ColorMath.h"

#include <glm/gtx/euler_angles.hpp>
#include <nlohmann/json.hpp>

#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <format>
#include <fstream>
#include <sstream>

namespace dmxviz::fixtures {
namespace {

namespace fs = std::filesystem;
using OJson = nlohmann::ordered_json;

// ============================================================== writing

// Six significant digits keep files readable (no 0.30000001192092896) and are
// far below anything that matters for a fixture definition.
double clean(double v) {
    if (!std::isfinite(v)) return 0.0;
    char buf[32];
    std::snprintf(buf, sizeof(buf), "%.6g", v);
    return std::strtod(buf, nullptr);
}

OJson vec2Json(const glm::vec2& v) { return OJson::array({clean(v.x), clean(v.y)}); }
OJson vec3Json(const glm::vec3& v) { return OJson::array({clean(v.x), clean(v.y), clean(v.z)}); }

glm::vec3 quatToEulerDegrees(const glm::quat& q) {
    float x = 0, y = 0, z = 0;
    glm::extractEulerAngleXYZ(glm::mat4_cast(q), x, y, z);
    return {radToDeg(x), radToDeg(y), radToDeg(z)};
}

glm::quat eulerDegreesToQuat(const glm::vec3& deg) {
    return glm::normalize(glm::quat_cast(glm::eulerAngleXYZ(degToRad(deg.x), degToRad(deg.y), degToRad(deg.z))));
}

OJson writeModel(const ModelSpec& m) {
    OJson j = OJson::object();
    if (m.primitive != PrimitiveShape::None) j["primitive"] = primitiveShapeName(m.primitive);
    if (!m.mesh.empty()) j["mesh"] = m.mesh;
    j["size"] = vec3Json(m.size);
    if (m.color != ModelSpec{}.color) j["color"] = vec3Json(m.color);
    return j;
}

OJson writeBeam(const BeamSpec& b) {
    OJson j = OJson::object();
    j["type"] = beamTypeName(b.type);
    j["lensRadius"] = clean(b.lensRadius);
    j["beamAngle"] = clean(radToDeg(b.beamAngle));
    j["fieldAngle"] = clean(radToDeg(b.fieldAngle));
    j["luminousFlux"] = clean(b.luminousFlux);
    j["colorTemperature"] = clean(b.colorTemperature);
    if (b.type == BeamType::Rectangle || b.emitterSize != BeamSpec{}.emitterSize) j["emitterSize"] = vec2Json(b.emitterSize);
    return j;
}

OJson writeGeometry(const Geometry& g) {
    OJson j = OJson::object();
    j["name"] = g.name;
    j["type"] = geometryTypeName(g.type);
    if (g.position != glm::vec3(0.0f)) j["position"] = vec3Json(g.position);
    const glm::vec3 euler = quatToEulerDegrees(g.rotation);
    if (glm::length(euler) > 1e-4f) j["rotation"] = vec3Json(euler);
    if (!g.model.empty()) j["model"] = writeModel(g.model);
    if (g.type == GeometryType::Beam) j["beam"] = writeBeam(g.beam);
    if (!g.children.empty()) {
        OJson children = OJson::array();
        for (const Geometry& c : g.children) children.push_back(writeGeometry(c));
        j["children"] = std::move(children);
    }
    return j;
}

OJson writeSlot(const WheelSlot& s) {
    OJson j = OJson::object();
    j["type"] = slotKindName(s.kind);
    if (!s.name.empty()) j["name"] = s.name;
    if (s.kind == SlotKind::Color || s.color != glm::vec3(1.0f)) j["color"] = vec3Json(s.color);
    if (!s.image.empty()) j["image"] = s.image;
    if (!s.facets.empty()) {
        OJson facets = OJson::array();
        for (const glm::vec2& f : s.facets) facets.push_back(vec2Json({radToDeg(f.x), radToDeg(f.y)}));
        j["facets"] = std::move(facets);
    }
    if (s.kind == SlotKind::Frost || s.frost != 0.0f) j["frost"] = clean(s.frost);
    return j;
}

bool functionHasPhysical(const ChannelFunction& f) {
    switch (f.kind) {
        case FunctionKind::WheelSlot:
        case FunctionKind::NoFeature:
        case FunctionKind::ShutterOpen:
        case FunctionKind::ShutterClosed: return false;
        default: return true;
    }
}

OJson writeFunction(const ChannelFunction& f) {
    OJson j = OJson::object();
    if (!f.name.empty()) j["name"] = f.name;
    j["attribute"] = std::string(f.attributeLabel());
    if (f.kind != attributeInfo(f.attribute).defaultKind) j["kind"] = functionKindName(f.kind);
    j["dmx"] = OJson::array({f.dmxFrom, f.dmxTo});
    if (functionHasPhysical(f))
        j["physical"] = OJson::array(
            {clean(physicalToFile(f.attribute, f.physicalFrom)), clean(physicalToFile(f.attribute, f.physicalTo))});
    if (f.kind == FunctionKind::WheelSlot || !f.wheel.empty()) {
        j["wheel"] = f.wheel;
        j["slots"] = OJson::array({clean(f.slotFrom), clean(f.slotTo)});
    }
    if (!f.emitter.empty()) j["emitter"] = f.emitter;
    if (!f.modeMaster.empty()) {
        j["modeMaster"] = f.modeMaster;
        j["modeRange"] = OJson::array({f.modeFrom, f.modeTo});
    }
    if (!f.sets.empty()) {
        OJson sets = OJson::array();
        for (const ChannelSet& s : f.sets) sets.push_back({{"name", s.name}, {"dmx", OJson::array({s.dmxFrom, s.dmxTo})}});
        j["sets"] = std::move(sets);
    }
    return j;
}

OJson writeChannel(const Channel& c) {
    OJson j = OJson::object();
    j["name"] = c.name;
    j["offsets"] = c.offsets;
    if (!c.geometry.empty()) j["geometry"] = c.geometry;
    j["default"] = c.defaultValue;
    if (c.highlightValue) j["highlight"] = *c.highlightValue;
    OJson functions = OJson::array();
    for (const ChannelFunction& f : c.functions) functions.push_back(writeFunction(f));
    j["functions"] = std::move(functions);
    return j;
}

// ============================================================== reading

// Recursive-descent reader over a JSON document. The first error wins and
// carries the JSON path of the offending field. Templated so it works on both
// nlohmann::json and nlohmann::ordered_json without conversions.
template <typename J>
class Reader {
public:
    explicit Reader(fs::path baseDir) : baseDir_(std::move(baseDir)) {}

    std::optional<FixtureType> parse(const J& root);
    const std::string& error() const { return error_; }

private:
    bool fail(const std::string& path, std::string_view message) {
        if (error_.empty()) error_ = (path.empty() ? std::string("<root>") : path) + ": " + std::string(message);
        return false;
    }
    static std::string join(const std::string& path, std::string_view key) {
        return path.empty() ? std::string(key) : path + "." + std::string(key);
    }
    static std::string index(const std::string& path, std::size_t i) { return std::format("{}[{}]", path, i); }

    const J* member(const J& obj, const char* key) const {
        auto it = obj.find(key);
        return it == obj.end() ? nullptr : &*it;
    }

    bool requireObject(const J& j, const std::string& path) {
        return j.is_object() ? true : fail(path, "expected an object");
    }

    bool readString(const J& obj, const char* key, const std::string& path, std::string& out, bool required = false) {
        const J* v = member(obj, key);
        if (!v) return required ? fail(join(path, key), "required string is missing") : true;
        if (!v->is_string()) return fail(join(path, key), "expected a string");
        out = v->template get<std::string>();
        return true;
    }

    bool readFloat(const J& obj, const char* key, const std::string& path, float& out, bool required = false) {
        const J* v = member(obj, key);
        if (!v) return required ? fail(join(path, key), "required number is missing") : true;
        if (!v->is_number()) return fail(join(path, key), "expected a number");
        out = static_cast<float>(v->template get<double>());
        return true;
    }

    bool toUInt(const J& v, const std::string& path, std::uint32_t& out) {
        if (!v.is_number_integer() || v.template get<std::int64_t>() < 0 ||
            v.template get<std::int64_t>() > 0xFFFFFFFFll)
            return fail(path, "expected a non-negative integer");
        out = static_cast<std::uint32_t>(v.template get<std::int64_t>());
        return true;
    }

    bool readUInt(const J& obj, const char* key, const std::string& path, std::uint32_t& out, bool required = false) {
        const J* v = member(obj, key);
        if (!v) return required ? fail(join(path, key), "required integer is missing") : true;
        return toUInt(*v, join(path, key), out);
    }

    bool readFloatPair(const J& obj, const char* key, const std::string& path, float& a, float& b) {
        const J* v = member(obj, key);
        if (!v) return true;
        if (!v->is_array() || v->size() != 2 || !(*v)[0].is_number() || !(*v)[1].is_number())
            return fail(join(path, key), "expected [from, to]");
        a = static_cast<float>((*v)[0].template get<double>());
        b = static_cast<float>((*v)[1].template get<double>());
        return true;
    }

    bool readUIntPair(const J& v, const std::string& path, std::uint32_t& a, std::uint32_t& b) {
        if (!v.is_array() || v.size() != 2) return fail(path, "expected [from, to]");
        return toUInt(v[0], path, a) && toUInt(v[1], path, b);
    }

    template <int N>
    bool readVec(const J& obj, const char* key, const std::string& path, glm::vec<N, float>& out) {
        const J* v = member(obj, key);
        if (!v) return true;
        if (!v->is_array() || v->size() != N) return fail(join(path, key), std::format("expected an array of {} numbers", N));
        for (int i = 0; i < N; ++i) {
            if (!(*v)[static_cast<std::size_t>(i)].is_number())
                return fail(join(path, key), std::format("expected an array of {} numbers", N));
            out[i] = static_cast<float>((*v)[static_cast<std::size_t>(i)].template get<double>());
        }
        return true;
    }

    // "#rrggbb" (sRGB) or [r, g, b] (linear).
    bool readColor(const J& obj, const char* key, const std::string& path, glm::vec3& out) {
        const J* v = member(obj, key);
        if (!v) return true;
        if (v->is_string()) {
            auto c = parseHexColor(v->template get<std::string>());
            if (!c) return fail(join(path, key), "expected \"#rrggbb\" or [r, g, b]");
            out = *c;
            return true;
        }
        return readVec<3>(obj, key, path, out);
    }

    bool readStringArray(const J& obj, const char* key, const std::string& path, std::vector<std::string>& out) {
        const J* v = member(obj, key);
        if (!v) return true;
        if (!v->is_array()) return fail(join(path, key), "expected an array of strings");
        for (std::size_t i = 0; i < v->size(); ++i) {
            if (!(*v)[i].is_string()) return fail(index(join(path, key), i), "expected a string");
            out.push_back((*v)[i].template get<std::string>());
        }
        return true;
    }

    template <typename T, typename Fn>
    bool readArray(const J& obj, const char* key, const std::string& path, std::vector<T>& out, Fn&& readOne,
                   bool required = false) {
        const J* v = member(obj, key);
        const std::string p = join(path, key);
        if (!v) return required ? fail(p, "required array is missing") : true;
        if (!v->is_array()) return fail(p, "expected an array");
        out.reserve(v->size());
        for (std::size_t i = 0; i < v->size(); ++i) {
            T item;
            if (!readOne((*v)[i], index(p, i), item)) return false;
            out.push_back(std::move(item));
        }
        return true;
    }

    bool readPhysical(const J& j, const std::string& path, PhysicalSpec& p);
    bool readResource(const J& j, const std::string& path, Resource& r);
    bool readWheel(const J& j, const std::string& path, Wheel& w);
    bool readSlot(const J& j, const std::string& path, WheelSlot& s);
    bool readEmitter(const J& j, const std::string& path, Emitter& e);
    bool readGeometry(const J& j, const std::string& path, Geometry& g);
    bool readGroup(const J& j, const std::string& path, GeometryGroup& g);
    bool readMode(const J& j, const std::string& path, DmxMode& m);
    bool readChannel(const J& j, const std::string& path, Channel& c);
    bool readFunction(const J& j, const std::string& path, const Channel& channel, std::size_t functionCount,
                      ChannelFunction& f);

    fs::path baseDir_;
    std::string error_;
};

template <typename J>
std::optional<FixtureType> Reader<J>::parse(const J& root) {
    if (!requireObject(root, "")) return std::nullopt;
    std::uint32_t version = 0;
    if (!readUInt(root, "formatVersion", "", version, true)) return std::nullopt;
    if (version < 1 || version > static_cast<std::uint32_t>(kNativeFormatVersion)) {
        fail("formatVersion", std::format("unsupported version {} (this DmxViz reads up to {})", version,
                                          kNativeFormatVersion));
        return std::nullopt;
    }

    FixtureType t;
    std::string source = "native";
    const bool ok = readString(root, "id", "", t.id, true) && readString(root, "manufacturer", "", t.manufacturer) &&
                    readString(root, "name", "", t.name, true) && readString(root, "shortName", "", t.shortName) &&
                    readString(root, "description", "", t.description) && readString(root, "source", "", source) &&
                    readString(root, "revision", "", t.revision) &&
                    readStringArray(root, "categories", "", t.categories);
    if (!ok) return std::nullopt;
    if (source == "native") t.source = FixtureSource::Native;
    else if (source == "ofl") t.source = FixtureSource::Ofl;
    else if (source == "gdtf") t.source = FixtureSource::Gdtf;
    else {
        fail("source", "expected \"native\", \"ofl\" or \"gdtf\"");
        return std::nullopt;
    }

    if (const J* physical = member(root, "physical"))
        if (!readPhysical(*physical, "physical", t.physical)) return std::nullopt;

    auto resource = [this](const J& j, const std::string& p, Resource& r) { return readResource(j, p, r); };
    auto emitter = [this](const J& j, const std::string& p, Emitter& e) { return readEmitter(j, p, e); };
    auto wheel = [this](const J& j, const std::string& p, Wheel& w) { return readWheel(j, p, w); };
    auto group = [this](const J& j, const std::string& p, GeometryGroup& g) { return readGroup(j, p, g); };
    auto mode = [this](const J& j, const std::string& p, DmxMode& m) { return readMode(j, p, m); };
    if (!readArray(root, "resources", "", t.resources, resource) || !readArray(root, "emitters", "", t.emitters, emitter) ||
        !readArray(root, "wheels", "", t.wheels, wheel))
        return std::nullopt;

    const J* geometry = member(root, "geometry");
    if (!geometry) {
        fail("geometry", "required object is missing");
        return std::nullopt;
    }
    if (!readGeometry(*geometry, "geometry", t.geometry)) return std::nullopt;
    if (!readArray(root, "geometryGroups", "", t.geometryGroups, group) || !readArray(root, "modes", "", t.modes, mode, true))
        return std::nullopt;

    // Semantic checks (references, ranges) after the structure parsed fine.
    const std::vector<std::string> problems = validateFixtureType(t);
    if (!problems.empty()) {
        error_ = problems.front();
        if (problems.size() > 1) error_ += std::format(" (and {} more problems)", problems.size() - 1);
        return std::nullopt;
    }
    return t;
}

template <typename J>
bool Reader<J>::readPhysical(const J& j, const std::string& path, PhysicalSpec& p) {
    if (!requireObject(j, path)) return false;
    std::string curve = "linear";
    if (!readFloat(j, "weight", path, p.weight) || !readFloat(j, "power", path, p.power) ||
        !readVec<3>(j, "dimensions", path, p.dimensions) || !readString(j, "dimmerCurve", path, curve))
        return false;
    if (curve == "linear") p.dimmerCurve = DimmerCurve::Linear;
    else if (curve == "squareLaw") p.dimmerCurve = DimmerCurve::SquareLaw;
    else return fail(join(path, "dimmerCurve"), "expected \"linear\" or \"squareLaw\"");

    if (const J* mv = member(j, "movement")) {
        const std::string mp = join(path, "movement");
        if (!requireObject(*mv, mp)) return false;
        MovementSpec& m = p.movement;
        float panSpeed = radToDeg(m.panMaxSpeed), tiltSpeed = radToDeg(m.tiltMaxSpeed);
        float panAcc = radToDeg(m.panAcceleration), tiltAcc = radToDeg(m.tiltAcceleration);
        float indexSpeed = radToDeg(m.indexRotationSpeed);
        if (!readFloat(*mv, "panMaxSpeed", mp, panSpeed) || !readFloat(*mv, "tiltMaxSpeed", mp, tiltSpeed) ||
            !readFloat(*mv, "panAcceleration", mp, panAcc) || !readFloat(*mv, "tiltAcceleration", mp, tiltAcc) ||
            !readFloat(*mv, "wheelSlotsPerSecond", mp, m.wheelSlotsPerSecond) ||
            !readFloat(*mv, "indexRotationSpeed", mp, indexSpeed))
            return false;
        if (panSpeed <= 0 || tiltSpeed <= 0 || panAcc <= 0 || tiltAcc <= 0 || m.wheelSlotsPerSecond <= 0 ||
            indexSpeed <= 0)
            return fail(mp, "speeds and accelerations must be positive");
        m.panMaxSpeed = degToRad(panSpeed);
        m.tiltMaxSpeed = degToRad(tiltSpeed);
        m.panAcceleration = degToRad(panAcc);
        m.tiltAcceleration = degToRad(tiltAcc);
        m.indexRotationSpeed = degToRad(indexSpeed);
    }
    return true;
}

template <typename J>
bool Reader<J>::readResource(const J& j, const std::string& path, Resource& r) {
    if (!requireObject(j, path)) return false;
    std::string base64, file;
    if (!readString(j, "name", path, r.name, true) || !readString(j, "format", path, r.format, true) ||
        !readString(j, "base64", path, base64) || !readString(j, "file", path, file))
        return false;
    if (!base64.empty()) {
        auto bytes = base64Decode(base64);
        if (!bytes) return fail(join(path, "base64"), "invalid base64 data");
        r.data = std::move(*bytes);
    } else if (!file.empty()) {
        const fs::path p = fs::path(file).is_absolute() ? fs::path(file) : baseDir_ / file;
        std::string err;
        auto bytes = readFileBytes(p, &err);
        if (!bytes) return fail(join(path, "file"), err);
        r.data = std::move(*bytes);
    } else {
        return fail(path, "a resource needs \"base64\" or \"file\"");
    }
    return true;
}

template <typename J>
bool Reader<J>::readEmitter(const J& j, const std::string& path, Emitter& e) {
    if (!requireObject(j, path)) return false;
    return readString(j, "name", path, e.name, true) && readColor(j, "color", path, e.color) &&
           readFloat(j, "wavelength", path, e.dominantWavelength);
}

template <typename J>
bool Reader<J>::readSlot(const J& j, const std::string& path, WheelSlot& s) {
    if (!requireObject(j, path)) return false;
    std::string type;
    if (!readString(j, "type", path, type, true)) return false;
    auto kind = parseSlotKind(type);
    if (!kind) return fail(join(path, "type"), "expected open, color, gobo, prism, animation or frost");
    s.kind = *kind;
    if (!readString(j, "name", path, s.name) || !readColor(j, "color", path, s.color) ||
        !readString(j, "image", path, s.image) || !readFloat(j, "frost", path, s.frost))
        return false;
    if (const J* facets = member(j, "facets")) {
        const std::string fp = join(path, "facets");
        if (!facets->is_array()) return fail(fp, "expected an array of [x, y] angles in degrees");
        for (std::size_t i = 0; i < facets->size(); ++i) {
            const J& f = (*facets)[i];
            if (!f.is_array() || f.size() != 2 || !f[0].is_number() || !f[1].is_number())
                return fail(index(fp, i), "expected [x, y] in degrees");
            s.facets.emplace_back(degToRad(static_cast<float>(f[0].template get<double>())),
                                  degToRad(static_cast<float>(f[1].template get<double>())));
        }
    } else if (const J* count = member(j, "facetCount")) {
        // Shorthand for hand-written files: a circular (or linear) facet pattern.
        std::uint32_t n = 0;
        if (!toUInt(*count, join(path, "facetCount"), n)) return false;
        float deflection = 6.0f;
        bool linear = false;
        if (!readFloat(j, "deflection", path, deflection)) return false;
        if (const J* lin = member(j, "linear")) linear = lin->is_boolean() && lin->template get<bool>();
        s.facets = linear ? makeLinearPrismFacets(static_cast<int>(n), degToRad(deflection))
                          : makeCircularPrismFacets(static_cast<int>(n), degToRad(deflection));
    }
    if (s.kind == SlotKind::Prism && s.facets.empty()) return fail(path, "a prism slot needs \"facets\" or \"facetCount\"");
    if ((s.kind == SlotKind::Gobo || s.kind == SlotKind::AnimationWheel) && s.image.empty())
        return fail(join(path, "image"), "gobo and animation slots need an image resource");
    return true;
}

template <typename J>
bool Reader<J>::readWheel(const J& j, const std::string& path, Wheel& w) {
    if (!requireObject(j, path)) return false;
    auto slot = [this](const J& sj, const std::string& p, WheelSlot& s) { return readSlot(sj, p, s); };
    return readString(j, "name", path, w.name, true) && readArray(j, "slots", path, w.slots, slot, true);
}

template <typename J>
bool Reader<J>::readGeometry(const J& j, const std::string& path, Geometry& g) {
    if (!requireObject(j, path)) return false;
    std::string type = "generic";
    glm::vec3 euler{0.0f};
    if (!readString(j, "name", path, g.name, true) || !readString(j, "type", path, type) ||
        !readVec<3>(j, "position", path, g.position) || !readVec<3>(j, "rotation", path, euler))
        return false;
    auto gt = parseGeometryType(type);
    if (!gt) return fail(join(path, "type"), "expected generic, axis or beam");
    g.type = *gt;
    g.rotation = eulerDegreesToQuat(euler);

    if (const J* model = member(j, "model")) {
        const std::string mp = join(path, "model");
        if (!requireObject(*model, mp)) return false;
        std::string primitive = "none";
        if (!readString(*model, "primitive", mp, primitive) || !readString(*model, "mesh", mp, g.model.mesh) ||
            !readVec<3>(*model, "size", mp, g.model.size) || !readColor(*model, "color", mp, g.model.color))
            return false;
        auto shape = parsePrimitiveShape(primitive);
        if (!shape) return fail(join(mp, "primitive"), "expected none, box, cylinder, sphere, base, yoke, head or conventional");
        g.model.primitive = *shape;
    }

    if (const J* beam = member(j, "beam")) {
        const std::string bp = join(path, "beam");
        if (!requireObject(*beam, bp)) return false;
        BeamSpec& b = g.beam;
        std::string beamType = std::string(beamTypeName(b.type));
        float beamAngle = radToDeg(b.beamAngle);
        float fieldAngle = -1.0f;
        if (!readString(*beam, "type", bp, beamType) || !readFloat(*beam, "lensRadius", bp, b.lensRadius) ||
            !readFloat(*beam, "beamAngle", bp, beamAngle) || !readFloat(*beam, "fieldAngle", bp, fieldAngle) ||
            !readFloat(*beam, "luminousFlux", bp, b.luminousFlux) ||
            !readFloat(*beam, "colorTemperature", bp, b.colorTemperature) ||
            !readVec<2>(*beam, "emitterSize", bp, b.emitterSize))
            return false;
        auto bt = parseBeamType(beamType);
        if (!bt) return fail(join(bp, "type"), "expected spot, wash, beam, pc, fresnel, rectangle or glow");
        b.type = *bt;
        if (beamAngle <= 0.0f || beamAngle >= 180.0f) return fail(join(bp, "beamAngle"), "must be in (0, 180) degrees");
        b.beamAngle = degToRad(beamAngle);
        // A missing field angle defaults to a typical 1.2 x the beam angle.
        b.fieldAngle = fieldAngle > 0.0f ? degToRad(fieldAngle) : std::min(degToRad(179.0f), b.beamAngle * 1.2f);
        if (b.colorTemperature < 1000.0f || b.colorTemperature > 25000.0f)
            return fail(join(bp, "colorTemperature"), "must be in 1000..25000 K");
    }

    auto child = [this](const J& cj, const std::string& p, Geometry& c) { return readGeometry(cj, p, c); };
    return readArray(j, "children", path, g.children, child);
}

template <typename J>
bool Reader<J>::readGroup(const J& j, const std::string& path, GeometryGroup& g) {
    if (!requireObject(j, path)) return false;
    return readString(j, "name", path, g.name, true) && readStringArray(j, "members", path, g.members);
}

template <typename J>
bool Reader<J>::readMode(const J& j, const std::string& path, DmxMode& m) {
    if (!requireObject(j, path)) return false;
    std::uint32_t footprint = 0;
    auto channel = [this](const J& cj, const std::string& p, Channel& c) { return readChannel(cj, p, c); };
    if (!readString(j, "name", path, m.name, true) || !readString(j, "description", path, m.description) ||
        !readUInt(j, "footprint", path, footprint) || !readString(j, "geometryRoot", path, m.geometryRoot) ||
        !readArray(j, "channels", path, m.channels, channel, true))
        return false;
    m.footprint = std::max(static_cast<int>(footprint), m.highestOffset());
    return true;
}

template <typename J>
bool Reader<J>::readChannel(const J& j, const std::string& path, Channel& c) {
    if (!requireObject(j, path)) return false;
    if (!readString(j, "name", path, c.name, true) || !readString(j, "geometry", path, c.geometry)) return false;

    if (const J* offsets = member(j, "offsets")) {
        const std::string op = join(path, "offsets");
        if (!offsets->is_array() || offsets->size() > 3) return fail(op, "expected 0..3 offsets (coarse first)");
        for (std::size_t i = 0; i < offsets->size(); ++i) {
            std::uint32_t o = 0;
            if (!toUInt((*offsets)[i], index(op, i), o)) return false;
            if (o < 1 || o > 512) return fail(index(op, i), "offsets are 1..512");
            c.offsets.push_back(static_cast<std::uint16_t>(o));
        }
    } else {
        return fail(join(path, "offsets"), "required array is missing (use [] for a virtual channel)");
    }

    if (!readUInt(j, "default", path, c.defaultValue)) return false;
    if (member(j, "highlight")) {
        std::uint32_t h = 0;
        if (!readUInt(j, "highlight", path, h)) return false;
        c.highlightValue = h;
    }

    const J* functions = member(j, "functions");
    const std::string fp = join(path, "functions");
    if (!functions || !functions->is_array() || functions->empty())
        return fail(fp, "a channel needs at least one function");
    for (std::size_t i = 0; i < functions->size(); ++i) {
        ChannelFunction f;
        if (!readFunction((*functions)[i], index(fp, i), c, functions->size(), f)) return false;
        c.functions.push_back(std::move(f));
    }
    return true;
}

template <typename J>
bool Reader<J>::readFunction(const J& j, const std::string& path, const Channel& channel, std::size_t functionCount,
                             ChannelFunction& f) {
    if (!requireObject(j, path)) return false;
    std::string attribute;
    if (!readString(j, "name", path, f.name) || !readString(j, "attribute", path, attribute, true)) return false;
    f.attribute = parseAttribute(attribute);
    if (f.attribute == Attribute::Unknown) {
        f.attributeName = attribute;
        log::warn("fixtures", "{}.attribute: unknown attribute \"{}\" is kept but not rendered", path, attribute);
    }
    const AttributeInfo& info = attributeInfo(f.attribute);
    f.kind = info.defaultKind;

    std::string kind;
    if (!readString(j, "kind", path, kind)) return false;
    if (!kind.empty()) {
        auto k = parseFunctionKind(kind);
        if (!k) return fail(join(path, "kind"), std::format("unknown kind \"{}\"", kind));
        f.kind = *k;
    }

    if (const J* dmx = member(j, "dmx")) {
        if (!readUIntPair(*dmx, join(path, "dmx"), f.dmxFrom, f.dmxTo)) return false;
    } else if (functionCount == 1) {
        f.dmxFrom = 0;
        f.dmxTo = channel.maxValue();
    } else {
        return fail(join(path, "dmx"), "required when a channel has several functions");
    }

    float from = physicalToFile(f.attribute, info.defaultFrom);
    float to = physicalToFile(f.attribute, info.defaultTo);
    if (!readFloatPair(j, "physical", path, from, to)) return false;
    f.physicalFrom = physicalFromFile(f.attribute, from);
    f.physicalTo = physicalFromFile(f.attribute, to);

    if (!readString(j, "wheel", path, f.wheel) || !readString(j, "emitter", path, f.emitter) ||
        !readString(j, "modeMaster", path, f.modeMaster))
        return false;
    if (member(j, "slot")) {
        if (!readFloat(j, "slot", path, f.slotFrom)) return false;
        f.slotTo = f.slotFrom;
    } else if (member(j, "slots")) {
        if (!readFloatPair(j, "slots", path, f.slotFrom, f.slotTo)) return false;
    } else if (f.kind == FunctionKind::WheelSlot) {
        return fail(join(path, "slots"), "wheel functions need \"slot\" or \"slots\"");
    }
    if (f.kind == FunctionKind::WheelSlot && f.wheel.empty())
        return fail(join(path, "wheel"), "wheel functions need a wheel name");
    if (const J* range = member(j, "modeRange"))
        if (!readUIntPair(*range, join(path, "modeRange"), f.modeFrom, f.modeTo)) return false;
    if (!f.modeMaster.empty() && !member(j, "modeRange"))
        return fail(join(path, "modeRange"), "required together with modeMaster");

    if (const J* sets = member(j, "sets")) {
        const std::string sp = join(path, "sets");
        if (!sets->is_array()) return fail(sp, "expected an array");
        for (std::size_t i = 0; i < sets->size(); ++i) {
            const J& s = (*sets)[i];
            ChannelSet set;
            if (!requireObject(s, index(sp, i)) || !readString(s, "name", index(sp, i), set.name, true)) return false;
            const J* dmx = member(s, "dmx");
            if (!dmx) return fail(index(sp, i) + ".dmx", "required [from, to] is missing");
            if (!readUIntPair(*dmx, index(sp, i) + ".dmx", set.dmxFrom, set.dmxTo)) return false;
            f.sets.push_back(std::move(set));
        }
    }
    return true;
}

std::string readTextFile(const fs::path& path, std::string* error) {
    std::ifstream in(path, std::ios::binary);
    if (!in) {
        if (error) *error = "cannot open " + path.string();
        return {};
    }
    std::ostringstream ss;
    ss << in.rdbuf();
    return ss.str();
}

}  // namespace

bool isNativeFixturePath(const std::filesystem::path& path) {
    const std::string name = path.filename().string();
    return name.size() > kNativeFixtureExtension.size() && name.ends_with(kNativeFixtureExtension);
}

nlohmann::ordered_json FixtureSerializer::toJson(const FixtureType& t) {
    OJson j = OJson::object();
    j["formatVersion"] = kNativeFormatVersion;
    j["id"] = t.id;
    j["manufacturer"] = t.manufacturer;
    j["name"] = t.name;
    if (!t.shortName.empty()) j["shortName"] = t.shortName;
    if (!t.description.empty()) j["description"] = t.description;
    j["source"] = fixtureSourceName(t.source);
    if (!t.revision.empty()) j["revision"] = t.revision;
    if (!t.categories.empty()) j["categories"] = t.categories;

    const PhysicalSpec& p = t.physical;
    const MovementSpec& m = p.movement;
    j["physical"] = {
        {"weight", clean(p.weight)},
        {"power", clean(p.power)},
        {"dimensions", vec3Json(p.dimensions)},
        {"dimmerCurve", p.dimmerCurve == DimmerCurve::SquareLaw ? "squareLaw" : "linear"},
        {"movement",
         {{"panMaxSpeed", clean(radToDeg(m.panMaxSpeed))},
          {"tiltMaxSpeed", clean(radToDeg(m.tiltMaxSpeed))},
          {"panAcceleration", clean(radToDeg(m.panAcceleration))},
          {"tiltAcceleration", clean(radToDeg(m.tiltAcceleration))},
          {"wheelSlotsPerSecond", clean(m.wheelSlotsPerSecond)},
          {"indexRotationSpeed", clean(radToDeg(m.indexRotationSpeed))}}},
    };

    if (!t.emitters.empty()) {
        OJson emitters = OJson::array();
        for (const Emitter& e : t.emitters) {
            OJson ej = {{"name", e.name}, {"color", vec3Json(e.color)}};
            if (e.dominantWavelength > 0.0f) ej["wavelength"] = clean(e.dominantWavelength);
            emitters.push_back(std::move(ej));
        }
        j["emitters"] = std::move(emitters);
    }
    if (!t.wheels.empty()) {
        OJson wheels = OJson::array();
        for (const Wheel& w : t.wheels) {
            OJson slots = OJson::array();
            for (const WheelSlot& s : w.slots) slots.push_back(writeSlot(s));
            wheels.push_back({{"name", w.name}, {"slots", std::move(slots)}});
        }
        j["wheels"] = std::move(wheels);
    }
    j["geometry"] = writeGeometry(t.geometry);
    if (!t.geometryGroups.empty()) {
        OJson groups = OJson::array();
        for (const GeometryGroup& g : t.geometryGroups) groups.push_back({{"name", g.name}, {"members", g.members}});
        j["geometryGroups"] = std::move(groups);
    }
    OJson modes = OJson::array();
    for (const DmxMode& mode : t.modes) {
        OJson mj = OJson::object();
        mj["name"] = mode.name;
        if (!mode.description.empty()) mj["description"] = mode.description;
        mj["footprint"] = mode.footprint;
        if (!mode.geometryRoot.empty()) mj["geometryRoot"] = mode.geometryRoot;
        OJson channels = OJson::array();
        for (const Channel& c : mode.channels) channels.push_back(writeChannel(c));
        mj["channels"] = std::move(channels);
        modes.push_back(std::move(mj));
    }
    j["modes"] = std::move(modes);

    // Resources last: embedded base64 is long and would bury the readable parts.
    if (!t.resources.empty()) {
        OJson resources = OJson::array();
        for (const Resource& r : t.resources)
            resources.push_back({{"name", r.name}, {"format", r.format}, {"base64", base64Encode(r.data)}});
        j["resources"] = std::move(resources);
    }
    return j;
}

std::optional<FixtureType> FixtureSerializer::fromJson(const nlohmann::ordered_json& json,
                                                       const std::filesystem::path& baseDir, std::string* error) {
    Reader<nlohmann::ordered_json> reader(baseDir);
    auto result = reader.parse(json);
    if (!result && error) *error = reader.error();
    return result;
}

std::optional<FixtureType> FixtureSerializer::fromJson(const nlohmann::json& json, const std::filesystem::path& baseDir,
                                                       std::string* error) {
    Reader<nlohmann::json> reader(baseDir);
    auto result = reader.parse(json);
    if (!result && error) *error = reader.error();
    return result;
}

std::string FixtureSerializer::toString(const FixtureType& type) { return toJson(type).dump(2) + "\n"; }

std::optional<FixtureType> FixtureSerializer::fromString(std::string_view text, const std::filesystem::path& baseDir,
                                                         std::string* error) {
    nlohmann::ordered_json json;
    try {
        json = nlohmann::ordered_json::parse(text);
    } catch (const nlohmann::json::exception& e) {
        if (error) *error = std::string("invalid JSON: ") + e.what();
        return std::nullopt;
    }
    return fromJson(json, baseDir, error);
}

std::optional<FixtureType> FixtureSerializer::loadFile(const std::filesystem::path& path, std::string* error) {
    std::string readError;
    const std::string text = readTextFile(path, &readError);
    if (!readError.empty()) {
        if (error) *error = readError;
        return std::nullopt;
    }
    std::string parseError;
    auto type = fromString(text, path.parent_path(), &parseError);
    if (!type && error) *error = path.filename().string() + ": " + parseError;
    return type;
}

bool FixtureSerializer::saveFile(const FixtureType& type, const std::filesystem::path& path, std::string* error,
                                 const NativeSaveOptions& options) {
    OJson json = toJson(type);
    if (!options.embedResources && !type.resources.empty()) {
        // Write resources next to the fixture: "<name>.resources/<resource>.<format>".
        std::string stem = path.filename().string();
        if (isNativeFixturePath(path)) stem.resize(stem.size() - kNativeFixtureExtension.size());
        const fs::path dirName = stem + ".resources";
        std::error_code ec;
        fs::create_directories(path.parent_path() / dirName, ec);
        if (ec) {
            if (error) *error = "cannot create " + (path.parent_path() / dirName).string() + ": " + ec.message();
            return false;
        }
        OJson resources = OJson::array();
        for (const Resource& r : type.resources) {
            const fs::path rel = dirName / (slugify(r.name) + "." + r.format);
            if (!writeFileBytes(path.parent_path() / rel, r.data, error)) return false;
            resources.push_back({{"name", r.name}, {"format", r.format}, {"file", rel.generic_string()}});
        }
        json["resources"] = std::move(resources);
    }
    const std::string text = json.dump(2) + "\n";
    return writeFileBytes(path, std::span(reinterpret_cast<const std::uint8_t*>(text.data()), text.size()), error);
}

}  // namespace dmxviz::fixtures
