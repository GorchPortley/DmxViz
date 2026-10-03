#include "assets/ImageLoader.h"

#include "core/Limits.h"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstring>
#include <exception>
#include <filesystem>
#include <format>
#include <fstream>
#include <iterator>
#include <string>
#include <vector>

// Implementations are compiled in third_party/single_header_impls.c.
#include "stb_image.h"
#include "stb_image_write.h"
#include <cstdio>
#include "nanosvg.h"
#include "nanosvgrast.h"

namespace dmxviz::assets {
namespace {

void setError(std::string* error, std::string msg) {
    if (error) *error = std::move(msg);
}

std::string lower(std::string_view s) {
    std::string r(s);
    for (char& c : r) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    return r;
}

ImageData toChannels(ImageData rgba, int channels) {
    if (channels == 4) return rgba;
    // Greyscale = luminance multiplied by alpha (gobos are "light passes where bright").
    ImageData grey;
    grey.width = rgba.width;
    grey.height = rgba.height;
    grey.channels = 1;
    grey.pixels.resize(static_cast<std::size_t>(rgba.width) * static_cast<std::size_t>(rgba.height));
    for (std::size_t i = 0; i < grey.pixels.size(); ++i) {
        const std::uint8_t* p = &rgba.pixels[i * 4];
        const float l = (0.2126f * p[0] + 0.7152f * p[1] + 0.0722f * p[2]) * (p[3] / 255.0f);
        grey.pixels[i] = static_cast<std::uint8_t>(std::min(255.0f, l + 0.5f));
    }
    return grey;
}

bool sizeAllowed(std::uint64_t width, std::uint64_t height, std::string* error) {
    if (width > static_cast<std::uint64_t>(limits::kMaxImageDimension) ||
        height > static_cast<std::uint64_t>(limits::kMaxImageDimension) || width * height > limits::kMaxImagePixels) {
        setError(error, std::format("image of {} x {} pixels is too large (limit {} per side, {} megapixels)", width,
                                    height, limits::kMaxImageDimension, limits::kMaxImagePixels / 1'000'000));
        return false;
    }
    return true;
}

std::optional<ImageData> rasterizeSvg(std::span<const std::uint8_t> bytes, const ImageLoadOptions& opts,
                                      std::string* error) {
    // nanosvg parses in place and needs a mutable, null-terminated buffer.
    std::vector<char> text(bytes.begin(), bytes.end());
    text.push_back('\0');
    NSVGimage* svg = nsvgParse(text.data(), "px", 96.0f);
    // The size check also rejects NaN and infinity (every comparison with NaN is false).
    if (!svg || !(svg->width > 0.0f) || !(svg->height > 0.0f) || !(svg->width < 1.0e9f) || !(svg->height < 1.0e9f)) {
        if (svg) nsvgDelete(svg);
        setError(error, "invalid SVG");
        return std::nullopt;
    }
    const int rasterSize = std::clamp(opts.svgSize, 1, 4096);
    const float scale = static_cast<float>(rasterSize) / std::max(svg->width, svg->height);
    ImageData img;
    img.width = std::clamp(static_cast<int>(svg->width * scale), 1, rasterSize);
    img.height = std::clamp(static_cast<int>(svg->height * scale), 1, rasterSize);
    img.channels = 4;
    img.pixels.assign(static_cast<std::size_t>(img.width) * static_cast<std::size_t>(img.height) * 4, 0);
    NSVGrasterizer* rast = nsvgCreateRasterizer();
    nsvgRasterize(rast, svg, 0, 0, scale, img.pixels.data(), img.width, img.height, img.width * 4);
    nsvgDeleteRasterizer(rast);
    nsvgDelete(svg);
    return toChannels(std::move(img), opts.desiredChannels);
}

std::optional<ImageData> loadImageChecked(std::span<const std::uint8_t> bytes, std::string_view formatHint,
                                          const ImageLoadOptions& opts, std::string* error) {
    if (bytes.empty()) {
        setError(error, "empty image data");
        return std::nullopt;
    }
    if (bytes.size() > limits::kMaxFileBytes) {
        setError(error, "image file is too large");
        return std::nullopt;
    }
    if (lower(formatHint) == "svg") return rasterizeSvg(bytes, opts, error);

    // Read only the header first: a few bytes can claim a gigapixel image.
    int w = 0, h = 0, n = 0;
    // (When the header cannot be read, stbi_info overwrites its own reason with "unknown image type";
    // decoding then fails with the precise one, e.g. stb's own "too large".)
    if (stbi_info_from_memory(bytes.data(), static_cast<int>(bytes.size()), &w, &h, &n) &&
        (w <= 0 || h <= 0 || !sizeAllowed(static_cast<std::uint64_t>(w), static_cast<std::uint64_t>(h), error)))
        return std::nullopt;
    stbi_uc* data = stbi_load_from_memory(bytes.data(), static_cast<int>(bytes.size()), &w, &h, &n, 4);
    if (!data) {
        setError(error, stbi_failure_reason() ? stbi_failure_reason() : "unknown image format");
        return std::nullopt;
    }
    ImageData img;
    img.width = w;
    img.height = h;
    img.channels = 4;
    img.pixels.assign(data, data + static_cast<std::size_t>(w) * static_cast<std::size_t>(h) * 4);
    stbi_image_free(data);
    // A source image without alpha is treated as opaque; for greyscale output
    // that means plain luminance.
    return toChannels(std::move(img), opts.desiredChannels);
}

}  // namespace

std::optional<ImageData> loadImageFromMemory(std::span<const std::uint8_t> bytes, std::string_view formatHint,
                                             const ImageLoadOptions& opts, std::string* error) {
    try {
        return loadImageChecked(bytes, formatHint, opts, error);
    } catch (const std::exception& e) {  // e.g. std::bad_alloc: never let it cross the module boundary
        setError(error, std::string("cannot load image: ") + e.what());
        return std::nullopt;
    }
}

std::optional<ImageData> loadImage(const std::filesystem::path& path, const ImageLoadOptions& opts,
                                   std::string* error) {
    std::error_code sizeError;
    const std::uintmax_t fileSize = std::filesystem::file_size(path, sizeError);
    if (!sizeError && fileSize > limits::kMaxFileBytes) {
        setError(error, "image file is too large: " + path.string());
        return std::nullopt;
    }
    std::ifstream in(path, std::ios::binary);
    if (!in) {
        setError(error, "cannot open " + path.string());
        return std::nullopt;
    }
    std::vector<std::uint8_t> bytes((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
    std::string ext = path.extension().string();
    if (!ext.empty() && ext[0] == '.') ext.erase(0, 1);
    return loadImageFromMemory(bytes, ext, opts, error);
}

bool writePng(const std::filesystem::path& path, const ImageData& image) {
    if (!image.valid()) return false;
    return stbi_write_png(path.string().c_str(), image.width, image.height, image.channels, image.pixels.data(),
                          image.width * image.channels) != 0;
}

}  // namespace dmxviz::assets
