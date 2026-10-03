#include "stage/SceneJson.h"

#include "core/Limits.h"
#include "stage/PathUtil.h"

#include <algorithm>
#include <charconv>
#include <cmath>
#include <format>
#include <iterator>
#include <limits>
#include <stdexcept>
#include <type_traits>
#include <unordered_set>

namespace dmxviz::stage {
namespace {

using json = nlohmann::json;

constexpr int kMaxDepth = limits::kMaxNodeDepth;  // later stages walk the tree recursively

// Thrown by the readers below and turned into an error string at the API boundary.
struct ParseError : std::runtime_error {
    using std::runtime_error::runtime_error;
};

struct ReadContext {
    const JsonPathContext& paths;
    std::vector<std::string>* warnings;
    mutable std::size_t nodeCount = 0;  // nodes read so far (limit: limits::kMaxSceneNodes)
    void warn(std::string message) const {
        if (warnings) warnings->push_back(std::move(message));
    }
};

std::string at(const std::string& where, std::string_view key) { return where + "." + std::string(key); }

const json* field(const json& obj, const char* key) {
    if (!obj.is_object()) return nullptr;
    auto it = obj.find(key);
    return it == obj.end() || it->is_null() ? nullptr : &*it;
}

void requireObject(const json& j, const std::string& where) {
    if (!j.is_object()) throw ParseError(where + ": expected an object");
}

float readFloat(const json& obj, const char* key, float def, const std::string& where) {
    const json* v = field(obj, key);
    if (!v) return def;
    if (!v->is_number()) throw ParseError(at(where, key) + ": expected a number");
    const double d = v->get<double>();
    if (!std::isfinite(d) || std::abs(d) > static_cast<double>(std::numeric_limits<float>::max()))
        throw ParseError(at(where, key) + ": number out of range");
    return static_cast<float>(d);
}

bool readBool(const json& obj, const char* key, bool def, const std::string& where) {
    const json* v = field(obj, key);
    if (!v) return def;
    if (!v->is_boolean()) throw ParseError(at(where, key) + ": expected true or false");
    return v->get<bool>();
}

long long readInteger(const json& obj, const char* key, long long def, long long lo, long long hi,
                      const std::string& where) {
    const json* v = field(obj, key);
    if (!v) return def;
    long long value = 0;
    if (v->is_number_unsigned()) {
        const std::uint64_t u = v->get<std::uint64_t>();
        if (u > static_cast<std::uint64_t>(hi))
            throw ParseError(std::format("{}: {} is outside {}..{}", at(where, key), u, lo, hi));
        value = static_cast<long long>(u);
    } else if (v->is_number_integer()) {
        value = v->get<long long>();
    } else if (v->is_number_float() && std::floor(v->get<double>()) == v->get<double>() &&
               std::abs(v->get<double>()) < 9e15) {
        value = static_cast<long long>(v->get<double>());
    } else {
        throw ParseError(at(where, key) + ": expected a whole number");
    }
    if (value < lo || value > hi)
        throw ParseError(std::format("{}: {} is outside {}..{}", at(where, key), value, lo, hi));
    return value;
}

int readInt(const json& obj, const char* key, int def, int lo, int hi, const std::string& where) {
    return static_cast<int>(readInteger(obj, key, def, lo, hi, where));
}

std::string readString(const json& obj, const char* key, const std::string& def, const std::string& where) {
    const json* v = field(obj, key);
    if (!v) return def;
    if (!v->is_string()) throw ParseError(at(where, key) + ": expected a string");
    return v->get<std::string>();
}

template <int N>
glm::vec<N, float> readVec(const json& obj, const char* key, const glm::vec<N, float>& def, const std::string& where) {
    const json* v = field(obj, key);
    if (!v) return def;
    if (!v->is_array() || v->size() != static_cast<std::size_t>(N))
        throw ParseError(std::format("{}: expected an array of {} numbers", at(where, key), N));
    glm::vec<N, float> r;
    for (int i = 0; i < N; ++i) {
        const json& e = (*v)[static_cast<std::size_t>(i)];
        if (!e.is_number() || !std::isfinite(e.get<double>()))
            throw ParseError(std::format("{}[{}]: expected a number", at(where, key), i));
        r[i] = static_cast<float>(e.get<double>());
    }
    return r;
}

// Enum stored as a name. Unknown names (from a newer version) fall back to the default with a warning.
template <typename E, typename ParseFn>
E readEnum(const json& obj, const char* key, E def, ParseFn&& parse, const std::string& where,
           const ReadContext& ctx) {
    const std::string name = readString(obj, key, {}, where);
    if (name.empty()) return def;
    E value = def;
    if (!parse(name, value)) ctx.warn(std::format("{}: unknown value '{}', using the default", at(where, key), name));
    return value;
}

// ---- writing helpers ---------------------------------------------------------

json num(float f) { return jsonFloat(f); }
json vec(const glm::vec3& v) { return json::array({num(v.x), num(v.y), num(v.z)}); }
json vec(const glm::vec2& v) { return json::array({num(v.x), num(v.y)}); }

Material readMaterial(const json& obj, const char* key, const Material& def, const std::string& where) {
    const json* v = field(obj, key);
    if (!v) return def;
    const std::string w = at(where, key);
    requireObject(*v, w);
    Material m;
    m.albedo = readVec<3>(*v, "albedo", def.albedo, w);
    m.roughness = readFloat(*v, "roughness", def.roughness, w);
    m.metallic = readFloat(*v, "metallic", def.metallic, w);
    m.emissive = readVec<3>(*v, "emissive", def.emissive, w);
    return m;
}

// ---- content -----------------------------------------------------------------

json profileToJson(const TrussProfile& p) {
    return {{"name", p.name},
            {"shape", trussShapeName(p.shape)},
            {"width", num(p.width)},
            {"chordDiameter", num(p.chordDiameter)},
            {"braceDiameter", num(p.braceDiameter)}};
}

json contentToJson(const NodeContent& content, const JsonPathContext& paths) {
    return std::visit(
        [&](const auto& c) -> json {
            using T = std::decay_t<decltype(c)>;
            if constexpr (std::is_same_v<T, PrimitiveContent>) {
                return {{"shape", primitiveShapeName(c.shape)}, {"size", vec(c.size)},
                        {"material", materialToJson(c.material)}};
            } else if constexpr (std::is_same_v<T, ModelContent>) {
                return {{"path", relativePathUtf8(c.path, paths.baseDir)}, {"zUp", c.zUp}, {"unitScale", num(c.unitScale)}};
            } else if constexpr (std::is_same_v<T, TrussContent>) {
                json segments = json::array();
                for (float s : c.straight.segments) segments.push_back(num(s));
                return {{"profile", profileToJson(c.profile)},
                        {"piece", trussPieceName(c.piece)},
                        {"straight", {{"length", num(c.straight.length)}, {"segments", segments}}},
                        {"corner", {{"faces", c.corner.faces}}},
                        {"arc", {{"radius", num(c.arc.radius)}, {"angleDeg", num(c.arc.angleDeg)}, {"pieces", c.arc.pieces}}},
                        {"tower",
                         {{"height", num(c.tower.height)},
                          {"sleeveHeight", num(c.tower.sleeveHeight)},
                          {"basePlateSize", num(c.tower.basePlateSize)}}}};
            } else if constexpr (std::is_same_v<T, StageDeckContent>) {
                return {{"style", c.style == DeckStyle::Riser ? "riser" : "deck"},
                        {"columns", c.columns},
                        {"rows", c.rows},
                        {"panelSize", vec(c.panelSize)},
                        {"height", num(c.height)},
                        {"thickness", num(c.thickness)},
                        {"legs", c.legs},
                        {"skirt", c.skirt},
                        {"surface", materialToJson(c.surface)},
                        {"skirtMaterial", materialToJson(c.skirtMaterial)}};
            } else if constexpr (std::is_same_v<T, StepsContent>) {
                return {{"width", num(c.width)},         {"height", num(c.height)},
                        {"steps", c.steps},              {"treadDepth", num(c.treadDepth)},
                        {"handrails", c.handrails},      {"material", materialToJson(c.material)}};
            } else if constexpr (std::is_same_v<T, WallContent>) {
                return {{"style", c.style == WallStyle::Flat ? "flat" : "wall"},
                        {"width", num(c.width)},
                        {"height", num(c.height)},
                        {"thickness", num(c.thickness)},
                        {"material", materialToJson(c.material)}};
            } else if constexpr (std::is_same_v<T, FixtureContent>) {
                return {{"fixtureTypeId", c.fixtureTypeId},
                        {"mode", c.modeName},
                        {"patch", {{"universe", c.patch.universe}, {"address", c.patch.address}}},
                        {"fixtureNumber", c.fixtureNumber},
                        {"invertPan", c.invertPan},
                        {"invertTilt", c.invertTilt},
                        {"panOffsetDeg", num(c.panOffsetDeg)},
                        {"tiltOffsetDeg", num(c.tiltOffsetDeg)}};
            } else if constexpr (std::is_same_v<T, CameraPresetContent>) {
                return {{"fovYDeg", num(c.fovYDeg)}, {"targetDistance", num(c.targetDistance)}};
            } else if constexpr (std::is_same_v<T, ReferenceFigureContent>) {
                return {{"height", num(c.height)}, {"material", materialToJson(c.material)}};
            } else {
                return json();  // Group (nothing to store), Unknown (handled by the caller)
            }
        },
        content);
}

TrussProfile readProfile(const json& obj, const std::string& where, const ReadContext& ctx) {
    TrussProfile p;
    const json* v = field(obj, "profile");
    if (!v) return p;
    const std::string w = at(where, "profile");
    requireObject(*v, w);
    p.name = readString(*v, "name", p.name, w);
    // A known catalogue name provides defaults for omitted sizes.
    if (const TrussProfile* known = findTrussProfile(p.name)) p = *known;
    p.shape = readEnum(*v, "shape", p.shape, parseTrussShape, w, ctx);
    p.width = readFloat(*v, "width", p.width, w);
    p.chordDiameter = readFloat(*v, "chordDiameter", p.chordDiameter, w);
    p.braceDiameter = readFloat(*v, "braceDiameter", p.braceDiameter, w);
    return p;
}

NodeContent contentFromJson(NodeKind kind, const json& c, const std::string& w, const ReadContext& ctx) {
    if (!c.is_null()) requireObject(c, w);
    switch (kind) {
        case NodeKind::Group: return GroupContent{};
        case NodeKind::Primitive: {
            PrimitiveContent p;
            p.shape = readEnum(c, "shape", p.shape, parsePrimitiveShape, w, ctx);
            p.size = readVec<3>(c, "size", p.size, w);
            p.material = readMaterial(c, "material", p.material, w);
            return p;
        }
        case NodeKind::Model: {
            ModelContent m;
            m.path = absolutePathUtf8(readString(c, "path", {}, w), ctx.paths.baseDir);
            m.zUp = readBool(c, "zUp", m.zUp, w);
            m.unitScale = readFloat(c, "unitScale", m.unitScale, w);
            return m;
        }
        case NodeKind::Truss: {
            TrussContent t;
            t.profile = readProfile(c, w, ctx);
            t.piece = readEnum(c, "piece", t.piece, parseTrussPiece, w, ctx);
            if (const json* s = field(c, "straight")) {
                const std::string sw = at(w, "straight");
                requireObject(*s, sw);
                t.straight.length = readFloat(*s, "length", t.straight.length, sw);
                if (const json* segs = field(*s, "segments")) {
                    if (!segs->is_array()) throw ParseError(at(sw, "segments") + ": expected an array");
                    for (const json& e : *segs) {
                        if (!e.is_number()) throw ParseError(at(sw, "segments") + ": expected numbers");
                        t.straight.segments.push_back(static_cast<float>(e.get<double>()));
                    }
                }
            }
            if (const json* s = field(c, "corner")) {
                t.corner.faces = static_cast<std::uint8_t>(readInt(*s, "faces", t.corner.faces, 0, 63, at(w, "corner")));
            }
            if (const json* s = field(c, "arc")) {
                const std::string aw = at(w, "arc");
                t.arc.radius = readFloat(*s, "radius", t.arc.radius, aw);
                t.arc.angleDeg = readFloat(*s, "angleDeg", t.arc.angleDeg, aw);
                t.arc.pieces = readInt(*s, "pieces", t.arc.pieces, 1, 1024, aw);
            }
            if (const json* s = field(c, "tower")) {
                const std::string tw = at(w, "tower");
                t.tower.height = readFloat(*s, "height", t.tower.height, tw);
                t.tower.sleeveHeight = readFloat(*s, "sleeveHeight", t.tower.sleeveHeight, tw);
                t.tower.basePlateSize = readFloat(*s, "basePlateSize", t.tower.basePlateSize, tw);
            }
            return t;
        }
        case NodeKind::StageDeck: {
            StageDeckContent d;
            d.style = readEnum(
                c, "style", d.style,
                [](std::string_view n, DeckStyle& out) {
                    if (n == "deck") out = DeckStyle::Deck;
                    else if (n == "riser") out = DeckStyle::Riser;
                    else return false;
                    return true;
                },
                w, ctx);
            d.columns = readInt(c, "columns", d.columns, 1, 10000, w);
            d.rows = readInt(c, "rows", d.rows, 1, 10000, w);
            d.panelSize = readVec<2>(c, "panelSize", d.panelSize, w);
            d.height = readFloat(c, "height", d.height, w);
            d.thickness = readFloat(c, "thickness", d.thickness, w);
            d.legs = readBool(c, "legs", d.legs, w);
            d.skirt = readBool(c, "skirt", d.skirt, w);
            d.surface = readMaterial(c, "surface", d.surface, w);
            d.skirtMaterial = readMaterial(c, "skirtMaterial", d.skirtMaterial, w);
            return d;
        }
        case NodeKind::Steps: {
            StepsContent s;
            s.width = readFloat(c, "width", s.width, w);
            s.height = readFloat(c, "height", s.height, w);
            s.steps = readInt(c, "steps", s.steps, 0, 1000, w);
            s.treadDepth = readFloat(c, "treadDepth", s.treadDepth, w);
            s.handrails = readBool(c, "handrails", s.handrails, w);
            s.material = readMaterial(c, "material", s.material, w);
            return s;
        }
        case NodeKind::Wall: {
            WallContent wall;
            wall.style = readEnum(
                c, "style", wall.style,
                [](std::string_view n, WallStyle& out) {
                    if (n == "wall") out = WallStyle::Wall;
                    else if (n == "flat") out = WallStyle::Flat;
                    else return false;
                    return true;
                },
                w, ctx);
            wall.width = readFloat(c, "width", wall.width, w);
            wall.height = readFloat(c, "height", wall.height, w);
            wall.thickness = readFloat(c, "thickness", wall.thickness, w);
            wall.material = readMaterial(c, "material", wall.material, w);
            return wall;
        }
        case NodeKind::Fixture: {
            FixtureContent f;
            f.fixtureTypeId = readString(c, "fixtureTypeId", {}, w);
            f.modeName = readString(c, "mode", {}, w);
            if (const json* p = field(c, "patch")) {
                const std::string pw = at(w, "patch");
                requireObject(*p, pw);
                f.patch.universe = static_cast<std::uint32_t>(readInteger(*p, "universe", 0, 0, 65535, pw));
                f.patch.address = static_cast<std::uint32_t>(readInteger(*p, "address", 0, 0, 512, pw));
            }
            f.fixtureNumber = readInt(c, "fixtureNumber", 0, 0, std::numeric_limits<int>::max(), w);
            f.invertPan = readBool(c, "invertPan", false, w);
            f.invertTilt = readBool(c, "invertTilt", false, w);
            f.panOffsetDeg = readFloat(c, "panOffsetDeg", 0.0f, w);
            f.tiltOffsetDeg = readFloat(c, "tiltOffsetDeg", 0.0f, w);
            return f;
        }
        case NodeKind::CameraPreset: {
            CameraPresetContent cam;
            cam.fovYDeg = readFloat(c, "fovYDeg", cam.fovYDeg, w);
            cam.targetDistance = readFloat(c, "targetDistance", cam.targetDistance, w);
            return cam;
        }
        case NodeKind::ReferenceFigure: {
            ReferenceFigureContent f;
            f.height = readFloat(c, "height", f.height, w);
            f.material = readMaterial(c, "material", f.material, w);
            return f;
        }
        case NodeKind::Unknown: break;
    }
    return GroupContent{};
}

// Keys every node has; everything else in an unknown node is kind data.
const char* const kCommonKeys[] = {"id",    "kind",  "name",  "transform",        "visible",
                                   "locked", "layer", "color", "materialOverride", "children"};

NodeSnapshot readNode(const json& j, const std::string& where, const ReadContext& ctx, int depth) {
    if (depth > kMaxDepth) throw ParseError(where + ": nodes are nested too deeply");
    if (++ctx.nodeCount > limits::kMaxSceneNodes)
        throw ParseError(std::format("more than {} nodes", limits::kMaxSceneNodes));
    requireObject(j, where);
    NodeSnapshot s;
    s.id = static_cast<NodeId>(readInteger(j, "id", 0, 0, std::numeric_limits<long long>::max(), where));
    const std::string kindName = readString(j, "kind", "group", where);
    NodeKind kind = NodeKind::Group;
    if (parseNodeKind(kindName, kind)) {
        const json* c = field(j, kindName.c_str());
        s.data.content = contentFromJson(kind, c ? *c : json(), at(where, kindName), ctx);
    } else {
        UnknownContent u;
        u.kind = kindName;
        // Copy everything except the common keys; copying "children" as well (and erasing it afterwards)
        // would duplicate every subtree once per nesting level.
        u.data = json::object();
        for (auto it = j.begin(); it != j.end(); ++it) {
            const bool common = std::any_of(std::begin(kCommonKeys), std::end(kCommonKeys),
                                            [&](const char* key) { return it.key() == key; });
            if (!common) u.data[it.key()] = it.value();
        }
        s.data.content = std::move(u);
        ctx.warn(std::format("{}: unknown node kind '{}' (kept as is, not displayed)", where, kindName));
    }
    s.data.name = readString(j, "name", std::string(nodeKindLabel(s.data.kind())), where);
    if (const json* t = field(j, "transform")) {
        const std::string tw = at(where, "transform");
        requireObject(*t, tw);
        s.transform.position = readVec<3>(*t, "position", s.transform.position, tw);
        if (field(*t, "rotation")) {
            const glm::vec4 q = readVec<4>(*t, "rotation", glm::vec4(0, 0, 0, 1), tw);  // x, y, z, w
            const glm::quat rq(q.w, q.x, q.y, q.z);
            const float len = glm::length(q);
            if (len < 1e-6f) throw ParseError(at(tw, "rotation") + ": not a rotation quaternion");
            // Keep the stored value bit-exact when it is already unit length.
            s.transform.rotation = std::abs(len - 1.0f) < 1e-6f ? rq : glm::normalize(rq);
        } else if (field(*t, "rotationDeg")) {
            s.transform.setEulerDegrees(readVec<3>(*t, "rotationDeg", glm::vec3(0.0f), tw));
        }
        s.transform.scale = readVec<3>(*t, "scale", s.transform.scale, tw);
    }
    s.data.visible = readBool(j, "visible", true, where);
    s.data.locked = readBool(j, "locked", false, where);
    s.data.layer = readInt(j, "layer", 0, 0, 1 << 20, where);
    s.data.color = readVec<3>(j, "color", s.data.color, where);
    if (field(j, "materialOverride")) s.data.materialOverride = readMaterial(j, "materialOverride", Material{}, where);
    if (const json* kids = field(j, "children")) {
        if (!kids->is_array()) throw ParseError(at(where, "children") + ": expected an array");
        for (std::size_t i = 0; i < kids->size(); ++i)
            s.children.push_back(readNode((*kids)[i], std::format("{}.children[{}]", where, i), ctx, depth + 1));
    }
    return s;
}

// Duplicate or missing ids get a fresh id on insertion (id 0).
void dedupeIds(NodeSnapshot& s, std::unordered_set<NodeId>& seen, const ReadContext& ctx) {
    if (s.id != kInvalidNode && !seen.insert(s.id).second) {
        ctx.warn(std::format("node id {} is used twice; '{}' gets a new id", s.id, s.data.name));
        s.id = kInvalidNode;
    }
    for (NodeSnapshot& c : s.children) dedupeIds(c, seen, ctx);
}

}  // namespace

double jsonFloat(float f) {
    if (!std::isfinite(f)) return 0.0;
    char buf[32];
    const auto r = std::to_chars(buf, buf + sizeof(buf), f);
    double d = 0.0;
    std::from_chars(buf, r.ptr, d);
    return d;
}

json materialToJson(const Material& m) {
    return {{"albedo", vec(m.albedo)},
            {"roughness", num(m.roughness)},
            {"metallic", num(m.metallic)},
            {"emissive", vec(m.emissive)}};
}

json nodeToJson(const NodeSnapshot& node, const JsonPathContext& paths) {
    json j = json::object();
    const UnknownContent* unknown = node.data.as<UnknownContent>();
    if (unknown && unknown->data.is_object()) j = unknown->data;
    j["id"] = node.id;
    j["kind"] = unknown ? unknown->kind : std::string(nodeKindName(node.data.kind()));
    j["name"] = node.data.name;
    const glm::quat& q = node.transform.rotation;
    j["transform"] = {{"position", vec(node.transform.position)},
                      {"rotation", json::array({num(q.x), num(q.y), num(q.z), num(q.w)})},
                      {"scale", vec(node.transform.scale)}};
    j["visible"] = node.data.visible;
    j["locked"] = node.data.locked;
    j["layer"] = node.data.layer;
    j["color"] = vec(node.data.color);
    if (node.data.materialOverride) j["materialOverride"] = materialToJson(*node.data.materialOverride);
    if (!unknown) {
        json content = contentToJson(node.data.content, paths);
        if (!content.is_null()) j[std::string(nodeKindName(node.data.kind()))] = std::move(content);
    }
    if (!node.children.empty()) {
        json kids = json::array();
        for (const NodeSnapshot& c : node.children) kids.push_back(nodeToJson(c, paths));
        j["children"] = std::move(kids);
    }
    return j;
}

std::optional<NodeSnapshot> nodeFromJson(const json& j, const JsonPathContext& paths, std::string* error,
                                         std::vector<std::string>* warnings, const std::string& where) {
    try {
        return readNode(j, where, ReadContext{paths, warnings}, 0);
    } catch (const std::exception& e) {
        if (error) *error = e.what();
        return std::nullopt;
    }
}

json sceneToJson(const Scene& scene, const JsonPathContext& paths) {
    json layers = json::array();
    for (const Layer& l : scene.layers())
        layers.push_back({{"name", l.name}, {"visible", l.visible}, {"locked", l.locked}, {"color", vec(l.color)}});
    json nodes = json::array();
    for (NodeId id : scene.roots()) nodes.push_back(nodeToJson(scene.snapshot(id), paths));
    return {{"nextNodeId", scene.nextId()}, {"layers", std::move(layers)}, {"nodes", std::move(nodes)}};
}

bool sceneFromJson(const json& j, Scene& scene, const JsonPathContext& paths, std::string* error,
                   std::vector<std::string>* warnings) {
    const ReadContext ctx{paths, warnings};
    try {
        requireObject(j, "scene");
        if (const json* layers = field(j, "layers")) {
            if (!layers->is_array()) throw ParseError("scene.layers: expected an array");
            std::vector<Layer> list;
            for (std::size_t i = 0; i < layers->size(); ++i) {
                const std::string w = std::format("scene.layers[{}]", i);
                const json& l = (*layers)[i];
                requireObject(l, w);
                Layer layer;
                layer.name = readString(l, "name", layer.name, w);
                layer.visible = readBool(l, "visible", true, w);
                layer.locked = readBool(l, "locked", false, w);
                layer.color = readVec<3>(l, "color", layer.color, w);
                list.push_back(std::move(layer));
            }
            scene.setLayers(std::move(list));
        }
        std::vector<NodeSnapshot> roots;
        if (const json* nodes = field(j, "nodes")) {
            if (!nodes->is_array()) throw ParseError("scene.nodes: expected an array");
            for (std::size_t i = 0; i < nodes->size(); ++i)
                roots.push_back(readNode((*nodes)[i], std::format("scene.nodes[{}]", i), ctx, 0));
        }
        std::unordered_set<NodeId> seen;
        for (NodeSnapshot& r : roots) dedupeIds(r, seen, ctx);
        // Set the id counter first: ids below it may have belonged to deleted
        // nodes and must not be handed out again to nodes that need a fresh id.
        scene.setNextId(static_cast<NodeId>(
            readInteger(j, "nextNodeId", 1, 1, std::numeric_limits<long long>::max(), "scene")));
        for (const NodeSnapshot& r : roots) scene.insertSnapshot(r);
        return true;
    } catch (const std::exception& e) {
        if (error) *error = e.what();
        return false;
    }
}

std::string snapshotsToClipboardText(const std::vector<NodeSnapshot>& nodes) {
    json list = json::array();
    for (const NodeSnapshot& n : nodes) list.push_back(nodeToJson(n));
    return json{{"dmxvizClipboard", 1}, {"nodes", std::move(list)}}.dump();
}

std::optional<std::vector<NodeSnapshot>> snapshotsFromClipboardText(std::string_view text, std::string* error) {
    if (text.size() > limits::kMaxFileBytes || limits::jsonNestedTooDeeply(text)) {
        if (error) *error = "the clipboard text is too large or nested too deeply";
        return std::nullopt;
    }
    const json j = json::parse(text, nullptr, false);
    if (j.is_discarded() || !j.is_object() || !j.contains("dmxvizClipboard") || !j.contains("nodes") ||
        !j["nodes"].is_array()) {
        if (error) *error = "the clipboard does not contain DmxViz nodes";
        return std::nullopt;
    }
    std::vector<NodeSnapshot> out;
    const JsonPathContext none;
    for (std::size_t i = 0; i < j["nodes"].size(); ++i) {
        auto node = nodeFromJson(j["nodes"][i], none, error, nullptr, std::format("clipboard.nodes[{}]", i));
        if (!node) return std::nullopt;
        out.push_back(std::move(*node));
    }
    return out;
}

}  // namespace dmxviz::stage
