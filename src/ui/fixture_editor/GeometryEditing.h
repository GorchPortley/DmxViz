#pragma once
// GeometryEditing: edits of a fixture's geometry tree that the fixture editor needs.
//
// A node is addressed by a NodePath (the child index at every level; the empty path is the root),
// because the tree is plain data without stable ids. All functions work on a FixtureType so
// they can keep references (channels, groups, mode roots) consistent when a node is renamed.
// No ImGui in here: the logic is covered by unit tests.

#include "fixtures/FixtureType.h"

#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace dmxviz::ui::fixture_editor {

using NodePath = std::vector<int>;

fixtures::Geometry* nodeAt(fixtures::Geometry& root, const NodePath& path);
const fixtures::Geometry* nodeAt(const fixtures::Geometry& root, const NodePath& path);
std::optional<NodePath> findNodePath(const fixtures::Geometry& root, std::string_view name);

// "Generic 2", "Beam 3"...: a geometry name that no node and no group of the type uses yet.
std::string uniqueGeometryName(const fixtures::FixtureType& type, std::string_view base);

// Adds a node (with sensible defaults for its type) as the last child of `parent`.
// Returns the new node's path, or nothing when `parent` does not exist.
std::optional<NodePath> addChildNode(fixtures::FixtureType& type, const NodePath& parent, fixtures::GeometryType kind);
// Removes a node and its subtree. The root cannot be removed. Channels that still refer to the
// removed names are left alone: the validation lists them so the user decides what to do.
bool removeNode(fixtures::FixtureType& type, const NodePath& path);
// Moves a node `delta` places among its siblings (-1 = up, +1 = down); `path` follows the node.
bool moveNode(fixtures::FixtureType& type, NodePath& path, int delta);
// Renames a node and every reference to it (channels, groups, mode roots).
// False when the name is empty or already used.
bool renameNode(fixtures::FixtureType& type, const NodePath& path, const std::string& newName);

// Geometry groups (a channel may control a group of nodes, e.g. all pixels of a bar).
// Adds an empty group with a unique name and returns its index.
std::size_t addGroup(fixtures::FixtureType& type);
// Renames a group and the channels that control it. False when the name is empty or already used by a
// node or another group.
bool renameGroup(fixtures::FixtureType& type, std::size_t groupIndex, const std::string& newName);

// "Make cells": a row of beam cells (pixel bars, strobe blinders).
struct CellOptions {
    int count = 8;
    glm::vec3 axis{1.0f, 0.0f, 0.0f};  // direction of the row, in the parent's space
    float pitch = 0.1f;                // m between cell centres
    glm::vec3 centre{0.0f};            // centre of the row, relative to the parent
    std::string namePrefix = "Cell";   // "Cell 1", "Cell 2"...
    fixtures::BeamSpec beam;           // copied to every cell
};

// Adds `options.count` Beam nodes under `parent`. Returns their names (empty on failure).
std::vector<std::string> makeCells(fixtures::FixtureType& type, const NodePath& parent, const CellOptions& options);

// ---- axes -------------------------------------------------------------------------------------

// The Axis nodes a channel moves, in the order the runtime assigns them: the first one follows
// Pan functions, the second one (or the first if there is only one) follows Tilt.
std::vector<const fixtures::Geometry*> channelAxes(const fixtures::FixtureType& type, const fixtures::Channel& channel);

// One Pan/Tilt/rotation function that moves an axis node.
struct AxisDrive {
    fixtures::Attribute attribute = fixtures::Attribute::Pan;
    std::string channel;
};
std::vector<AxisDrive> axisDrives(const fixtures::FixtureType& type, const fixtures::DmxMode& mode,
                                  std::string_view axisName);

// True for Pan, Tilt, PanRotate and TiltRotate.
bool isAxisAttribute(fixtures::Attribute attribute);

}  // namespace dmxviz::ui::fixture_editor
