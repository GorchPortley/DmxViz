#pragma once
// Image decoding: PNG/JPG/BMP/TGA via stb_image, SVG via nanosvg.

#include "assets/MeshData.h"

#include <cstdint>
#include <filesystem>
#include <optional>
#include <span>
#include <string>
#include <string_view>

namespace dmxviz::assets {

struct ImageLoadOptions {
    int desiredChannels = 4;  // 1 or 4
    int svgSize = 512;        // raster size (longest edge) for SVG input
};

std::optional<ImageData> loadImage(const std::filesystem::path& path, const ImageLoadOptions& opts = {},
                                   std::string* error = nullptr);

// formatHint is a file extension without the dot ("png", "svg"...). Only SVG
// needs it; raster formats are detected from their magic bytes.
std::optional<ImageData> loadImageFromMemory(std::span<const std::uint8_t> bytes, std::string_view formatHint,
                                             const ImageLoadOptions& opts = {}, std::string* error = nullptr);

// Writes an RGBA or greyscale image as PNG (screenshots, tests).
bool writePng(const std::filesystem::path& path, const ImageData& image);

}  // namespace dmxviz::assets
