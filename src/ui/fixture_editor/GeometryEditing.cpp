#include "ui/fixture_editor/GeometryEditing.h"

#include <algorithm>
#include <format>
#include <set>

namespace dmxviz::ui::fixture_editor {

using fixtures::Attribute;
using fixtures::AttributeFamily;
using fixtures::Geometry;
using fixtures::GeometryType;
using fixtures::PrimitiveShape;

namespace {

// All geometry and group names, to find free ones.
std::set<std::string, std::less<>> usedNames(const fixtures::FixtureType& type) {
    std::set<std::string, std::less<>> names;
    fixtures::forEachGeometry(type.geometry, [&](const Geometry& g, const Geometry*) { names.insert(g.name); });
    for (const fixtures::GeometryGroup& group : type.geometryGroups) names.insert(group.name);
    return names;
}

const char* defaultNodeName(GeometryType kind) {
    switch (kind) {
        case GeometryType::Generic: return "Generic";
        case GeometryType::Axis: return "Axis";
        case GeometryType::Beam: return "Beam";
    }
    return "Node";
}

Geometry makeNode(const fixtures::FixtureType& type, GeometryType kind) {
    Geometry node;
    node.name = uniqueGeometryName(type, defaultNodeName(kind));
    node.type = kind;
    switch (kind) {
        case GeometryType::Generic:
            node.model.primitive = PrimitiveShape::Box;
            node.model.size = glm::vec3(0.1f);
            break;
        case GeometryType::Axis:
            node.model.primitive = PrimitiveShape::Cylinder;
            node.model.size = glm::vec3(0.1f);
            break;
        case GeometryType::Beam: break;  // a beam draws its own lens
    }
    return node;
}

void renameReferences(fixtures::FixtureType& type, const std::string& from, const std::string& to) {
    for (fixtures::GeometryGroup& group : type.geometryGroups)
        for (std::string& member : group.members)
            if (member == from) member = to;
    for (fixtures::DmxMode& mode : type.modes) {
        if (mode.geometryRoot == from) mode.geometryRoot = to;
        for (fixtures::Channel& channel : mode.channels)
            if (channel.geometry == from) channel.geometry = to;
    }
}

bool findPathRecursive(const Geometry& node, std::string_view name, NodePath& path) {
    if (node.name == name) return true;
    for (std::size_t i = 0; i < node.children.size(); ++i) {
        path.push_back(static_cast<int>(i));
        if (findPathRecursive(node.children[i], name, path)) return true;
        path.pop_back();
    }
    return false;
}

}  // namespace

fixtures::Geometry* nodeAt(fixtures::Geometry& root, const NodePath& path) {
    Geometry* node = &root;
    for (int index : path) {
        if (index < 0 || index >= static_cast<int>(node->children.size())) return nullptr;
        node = &node->children[static_cast<std::size_t>(index)];
    }
    return node;
}

const fixtures::Geometry* nodeAt(const fixtures::Geometry& root, const NodePath& path) {
    return nodeAt(const_cast<Geometry&>(root), path);  // the non-const overload only reads
}

std::optional<NodePath> findNodePath(const fixtures::Geometry& root, std::string_view name) {
    NodePath path;
    if (findPathRecursive(root, name, path)) return path;
    return std::nullopt;
}

std::string uniqueGeometryName(const fixtures::FixtureType& type, std::string_view base) {
    const auto names = usedNames(type);
    if (!names.contains(base)) return std::string(base);
    for (int n = 2;; ++n) {
        std::string candidate = std::format("{} {}", base, n);
        if (!names.contains(candidate)) return candidate;
    }
}

std::optional<NodePath> addChildNode(fixtures::FixtureType& type, const NodePath& parent, GeometryType kind) {
    Geometry* node = nodeAt(type.geometry, parent);
    if (node == nullptr) return std::nullopt;
    Geometry child = makeNode(type, kind);
    node->children.push_back(std::move(child));
    NodePath path = parent;
    path.push_back(static_cast<int>(node->children.size()) - 1);
    return path;
}

bool removeNode(fixtures::FixtureType& type, const NodePath& path) {
    if (path.empty()) return false;
    const NodePath parentPath(path.begin(), path.end() - 1);
    Geometry* parent = nodeAt(type.geometry, parentPath);
    if (parent == nullptr) return false;
    const int index = path.back();
    if (index < 0 || index >= static_cast<int>(parent->children.size())) return false;
    parent->children.erase(parent->children.begin() + index);
    return true;
}

bool moveNode(fixtures::FixtureType& type, NodePath& path, int delta) {
    if (path.empty()) return false;
    const NodePath parentPath(path.begin(), path.end() - 1);
    Geometry* parent = nodeAt(type.geometry, parentPath);
    if (parent == nullptr) return false;
    const int from = path.back();
    const int to = from + delta;
    const int count = static_cast<int>(parent->children.size());
    if (from < 0 || from >= count || to < 0 || to >= count) return false;
    std::swap(parent->children[static_cast<std::size_t>(from)], parent->children[static_cast<std::size_t>(to)]);
    path.back() = to;
    return true;
}

bool renameNode(fixtures::FixtureType& type, const NodePath& path, const std::string& newName) {
    Geometry* node = nodeAt(type.geometry, path);
    if (node == nullptr || newName.empty()) return false;
    if (node->name == newName) return true;
    if (usedNames(type).contains(newName)) return false;
    const std::string oldName = node->name;
    node->name = newName;
    renameReferences(type, oldName, newName);
    return true;
}

std::size_t addGroup(fixtures::FixtureType& type) {
    fixtures::GeometryGroup group;
    group.name = uniqueGeometryName(type, "Group");
    type.geometryGroups.push_back(std::move(group));
    return type.geometryGroups.size() - 1;
}

bool renameGroup(fixtures::FixtureType& type, std::size_t groupIndex, const std::string& newName) {
    if (groupIndex >= type.geometryGroups.size() || newName.empty()) return false;
    const std::string oldName = type.geometryGroups[groupIndex].name;
    if (oldName == newName) return true;
    if (usedNames(type).contains(newName)) return false;
    type.geometryGroups[groupIndex].name = newName;
    for (fixtures::DmxMode& mode : type.modes)
        for (fixtures::Channel& channel : mode.channels)
            if (channel.geometry == oldName) channel.geometry = newName;
    return true;
}

std::vector<std::string> makeCells(fixtures::FixtureType& type, const NodePath& parent, const CellOptions& options) {
    std::vector<std::string> names;
    Geometry* parentNode = nodeAt(type.geometry, parent);
    if (parentNode == nullptr || options.count < 1) return names;

    const glm::vec3 axis = glm::length(options.axis) > 1e-6f ? glm::normalize(options.axis) : glm::vec3(1, 0, 0);
    const float firstOffset = -0.5f * options.pitch * static_cast<float>(options.count - 1);
    for (int i = 0; i < options.count; ++i) {
        Geometry cell;
        cell.name = uniqueGeometryName(type, std::format("{} {}", options.namePrefix, i + 1));
        cell.type = GeometryType::Beam;
        cell.position = options.centre + axis * (firstOffset + options.pitch * static_cast<float>(i));
        cell.beam = options.beam;
        names.push_back(cell.name);
        // Added one at a time so the next cell's unique name sees the previous one.
        parentNode->children.push_back(std::move(cell));
    }
    return names;
}

// ---------------------------------------------------------------------------
// Axes

bool isAxisAttribute(Attribute attribute) {
    switch (fixtures::attributeFamily(attribute)) {
        case AttributeFamily::Pan:
        case AttributeFamily::Tilt:
        case AttributeFamily::PanRotate:
        case AttributeFamily::TiltRotate: return true;
        default: return false;
    }
}

std::vector<const fixtures::Geometry*> channelAxes(const fixtures::FixtureType& type, const fixtures::Channel& channel) {
    std::vector<const Geometry*> roots;
    if (channel.geometry.empty()) {
        roots.push_back(&type.geometry);
    } else if (const Geometry* node = type.findGeometry(channel.geometry)) {
        roots.push_back(node);
    } else if (const fixtures::GeometryGroup* group = type.findGroup(channel.geometry)) {
        for (const std::string& member : group->members)
            if (const Geometry* m = type.findGeometry(member)) roots.push_back(m);
    }
    std::vector<const Geometry*> axes;
    for (const Geometry* root : roots) {
        fixtures::forEachGeometry(*root, [&](const Geometry& g, const Geometry*) {
            if (g.type == GeometryType::Axis && std::find(axes.begin(), axes.end(), &g) == axes.end())
                axes.push_back(&g);
        });
    }
    return axes;
}

std::vector<AxisDrive> axisDrives(const fixtures::FixtureType& type, const fixtures::DmxMode& mode,
                                  std::string_view axisName) {
    std::vector<AxisDrive> drives;
    for (const fixtures::Channel& channel : mode.channels) {
        const std::vector<const Geometry*> axes = channelAxes(type, channel);
        if (axes.empty()) continue;
        for (const fixtures::ChannelFunction& f : channel.functions) {
            const AttributeFamily family = fixtures::attributeFamily(f.attribute);
            const bool pan = family == AttributeFamily::Pan || family == AttributeFamily::PanRotate;
            const bool tilt = family == AttributeFamily::Tilt || family == AttributeFamily::TiltRotate;
            if (!pan && !tilt) continue;
            const Geometry* target = pan ? axes[0] : (axes.size() > 1 ? axes[1] : axes[0]);
            if (target->name == axisName) drives.push_back({f.attribute, channel.name});
        }
    }
    return drives;
}

}  // namespace dmxviz::ui::fixture_editor
