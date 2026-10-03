#pragma once
// Import of GDTF fixture files (DIN SPEC 15800, GDTF 1.1 / 1.2).
//
// A .gdtf file is a zip archive:
//   description.xml      fixture type, attributes, wheels, physical data, models,
//                        geometry tree and DMX modes
//   wheels/*.png         gobo / animation wheel images (MediaFileName)
//   models/gltf/*.glb, models/3ds/*.3ds   3D models of the geometry parts
//
// What maps where:
//   FixtureType Name / Manufacturer / ...   -> id ("manufacturer/name"), name, description
//   Wheels / Slot                           -> Wheels (Color "x,y,Y" -> linear RGB filter, MediaFileName
//        -> gobo image resource, Facet rotations -> prism facet angles)
//   PhysicalDescriptions / Emitters         -> emitters (xyY -> linear RGB), weight, power
//   Models                                  -> primitive shape + size, optional mesh resource
//   Geometries                              -> geometry tree (Geometry / Axis / Beam, others -> Generic)
//   DMXModes / DMXChannel / LogicalChannel / ChannelFunction / ChannelSet
//                                           -> modes, channels (Offset list = 8/16/24 bit) and functions
//
// Conversion notes:
//   * GDTF is Z-up (beam along -Z); DmxViz is Y-up (beam along -Y):
//     (x, y, z)_gdtf -> (x, z, -y), a rotation of -90 degrees about X. Geometry positions
//     and model sizes in description.xml are metres already; 3DS model files are
//     millimetres (FixtureAssets scales them while loading).
//   * Wheel and emitter colours are CIE xyY and become linear sRGB.
//   * GeometryReferences are expanded: each reference becomes a copy of the
//     referenced geometry (named after the reference, its children prefixed with that name),
//     and the DMX channels of that geometry are repeated per copy, shifted by the
//     reference's Break DMXOffset (multi-cell fixtures).
//   * ChannelSets with WheelSlotIndex become one WheelSlot function each.
//   * Pan/Tilt RealFade / RealAcceleration set the movement speed limits.
//   * Model files are kept as resources; FixtureAssets loads them later and falls
//     back to the model's primitive type / box size if loading fails.
//
// Not supported (skipped with a warning, never fatal): DMX breaks other than the first,
// Relations / macros / DMX profiles, animation wheel slot systems, wheel shake / half-slot
// positions, colour spaces other than sRGB, geometry scaling in Position matrices.
// Unknown elements are skipped with a warning; unknown attributes are kept as
// Attribute::Unknown so nothing is lost on a round trip through the native format.

#include "fixtures/FixtureType.h"

#include <cstdint>
#include <filesystem>
#include <optional>
#include <span>
#include <string>
#include <vector>

namespace dmxviz::fixtures {

// `error` receives a one-line reason when nullopt is returned; `warnings` collects the
// non-fatal problems (also written to the log).
std::optional<FixtureType> importGdtf(std::span<const std::uint8_t> archive, std::string* error,
                                      std::vector<std::string>* warnings = nullptr);
std::optional<FixtureType> importGdtfFile(const std::filesystem::path& path, std::string* error,
                                          std::vector<std::string>* warnings = nullptr);

}  // namespace dmxviz::fixtures
