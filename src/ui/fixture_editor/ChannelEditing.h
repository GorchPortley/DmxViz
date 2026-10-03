#pragma once
// ChannelEditing: edits of DMX modes, channels, channel functions, wheels and resources.
//
// The fixture editor's widgets call these instead of poking the data directly, so rules like
// "changing 8 bit to 16 bit rescales the function ranges" live in one tested place.
// No ImGui in here.

#include "fixtures/FixtureType.h"

#include <cstdint>
#include <filesystem>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace dmxviz::ui::fixture_editor {

// ---- names ------------------------------------------------------------------------------------

// `base` if it is not in `existing`, else "base 2", "base 3"...
std::string uniqueName(std::string_view base, const std::vector<std::string>& existing);

// ---- channels ---------------------------------------------------------------------------------

// Highest DMX value of a channel with this many bytes (255, 65535, 16777215).
std::uint32_t maxDmxValue(int bytes);

// A function over [dmxFrom, dmxTo] with the usual kind and physical range of the attribute.
fixtures::ChannelFunction makeFunction(fixtures::Attribute attribute, std::uint32_t dmxFrom, std::uint32_t dmxTo);

// A channel with one function spanning its whole range. Offsets are firstOffset, firstOffset + 1...
fixtures::Channel makeChannel(std::string name, fixtures::Attribute attribute, int bytes, int firstOffset,
                              std::string geometry = {});

// Appends `channel` after the highest used offset (its own offsets are replaced, the byte count is
// kept) and grows the footprint. Returns the stored channel.
fixtures::Channel& appendChannel(fixtures::DmxMode& mode, fixtures::Channel channel);

// footprint = highest used offset.
void updateFootprint(fixtures::DmxMode& mode);

// Gives every channel consecutive offsets in list order (removes gaps) and updates the footprint.
void renumberOffsets(fixtures::DmxMode& mode);

// Changes a channel to 1..3 bytes. Extra offsets go after the highest used one, default, highlight
// and function ranges are rescaled to the new resolution. Updates the footprint.
void setChannelBytes(fixtures::DmxMode& mode, std::size_t channelIndex, int bytes);

// Stretches function ranges so that they tile 0..max without gaps or overlaps (in DMX order).
void closeFunctionGaps(fixtures::Channel& channel);

// Replaces the functions of a channel by one wheel-slot function per slot of `wheel`.
void fillWheelFunctions(fixtures::Channel& channel, const fixtures::Wheel& wheel, fixtures::Attribute attribute);

// Adds one channel per attribute for every cell, or - with perCell = false - one channel per attribute
// that drives the geometry group `groupName` (created from the cells; ignored when empty).
void addCellChannels(fixtures::FixtureType& type, fixtures::DmxMode& mode, const std::vector<std::string>& cells,
                     const std::vector<fixtures::Attribute>& attributes, bool perCell, const std::string& groupName);

// A copy of `mode` named "<name> copy" (made unique among the type's modes).
fixtures::DmxMode duplicateMode(const fixtures::FixtureType& type, const fixtures::DmxMode& mode);

// ---- wheels -----------------------------------------------------------------------------------

fixtures::WheelSlot makeSlot(fixtures::SlotKind kind, int slotNumber);
// Renames a wheel and the channel functions that use it. False when the name is empty or taken.
bool renameWheel(fixtures::FixtureType& type, std::size_t wheelIndex, const std::string& newName);

// ---- resources --------------------------------------------------------------------------------

// A picked file, ready to become a fixture resource.
struct ImportedFile {
    std::string name;    // file name without extension
    std::string format;  // "png", "jpg", "svg"...
    std::vector<std::uint8_t> data;
};

// Reads a PNG / JPG / SVG gobo image and checks that it decodes. Nothing on failure.
std::optional<ImportedFile> readImageFile(const std::filesystem::path& file, std::string* error);

// Reads a 3D model file (GLB, GLTF, OBJ, 3DS) for use as a geometry's mesh. Nothing on failure.
std::optional<ImportedFile> readModelFile(const std::filesystem::path& file, std::string* error);

// Stores bytes as a resource with a name that is unique in the type. Returns that name.
std::string addResource(fixtures::FixtureType& type, std::string_view suggestedName, std::string_view format,
                        std::vector<std::uint8_t> data);

// Drops resources that no slot and no model refers to. Returns how many were removed.
int removeUnusedResources(fixtures::FixtureType& type);

}  // namespace dmxviz::ui::fixture_editor
