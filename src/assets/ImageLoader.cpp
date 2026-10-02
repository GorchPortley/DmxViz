#include "assets/ImageLoader.h"

#include <algorithm>
#include <cctype>
#include <cstring>
#include <fstream>
#include <iterator>
#include <string>
#include <vector>

#define STB_IMAGE_IMPLEMENTATION
#define STBI_NO_HDR
#define STBI_NO_PIC
#define STBI_NO_PNM
#include "stb_image.h"

#define STB_IMAGE_WRITE_IMPLEMENTATION
#include "stb_image_write.h"

#define NANOSVG_IMPLEMENTATION
#include "nanosvg.h"
#define NANOSVGRAST_IMPLEMENTATION
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
    grey.pixels.resize(static_cast<std::size_t>(rgba.width * rgba.height));
    for (std::size_t i = 0; i < grey.pixels.size(); ++i) {
        const std::uint8_t* p = &rgba.pixels[i * 4];
        const float l = (0.2126f * p[0] + 0.7152f * p[1] + 0.0722f * p[2]) * (p[3] / 255.0f);
        grey.pixels[i] = static_cast<std::uint8_t>(std::min(255.0f, l + 0.5f));
    }
    return grey;
}

std::optional<ImageData> rasterizeSvg(std::span<const std::uint8_t> bytes, const ImageLoadOptions& opts,
                                      std::string* error) {
    // nanosvg parses in place and needs a mutable, null-terminated buffer.
    std::vector<char> text(bytes.begin(), bytes.end());
    text.push_back('\0');
    NSVGimage* svg = nsvgParse(text.data(), "px", 96.0f);
    if (!svg || svg->width <= 0 || svg->height <= 0) {
        if (svg) nsvgDelete(svg);
        setError(error, "invalid SVG");
        return std::nullopt;
    }
    const float scale = static_cast<float>(opts.svgSize) / std::max(svg->width, svg->height);
    ImageData img;
    img.width = std::max(1, static_cast<int>(svg->width * scale));
    img.height = std::max(1, static_cast<int>(svg->height * scale));
    img.channels = 4;
    img.pixels.assign(static_cast<std::size_t>(img.width * img.height * 4), 0);
    NSVGrasterizer* rast = nsvgCreateRasterizer();
    nsvgRasterize(rast, svg, 0, 0, scale, img.pixels.data(), img.width, img.height, img.width * 4);
    nsvgDeleteRasterizer(rast);
    nsvgDelete(svg);
    return toChannels(std::move(img), opts.desiredChannels);
}

}  // namespace

std::optional<ImageData> loadImageFromMemory(std::span<const std::uint8_t> bytes, std::string_view formatHint,
                                             const ImageLoadOptions& opts, std::string* error) {
    if (bytes.empty()) {
        setError(error, "empty image data");
        return std::nullopt;
    }
    if (lower(formatHint) == "svg") return rasterizeSvg(bytes, opts, error);

    int w = 0, h = 0, n = 0;
    stbi_uc* data = stbi_load_from_memory(bytes.data(), static_cast<int>(bytes.size()), &w, &h, &n, 4);
    if (!data) {
        setError(error, stbi_failure_reason() ? stbi_failure_reason() : "unknown image format");
        return std::nullopt;
    }
    ImageData img;
    img.width = w;
    img.height = h;
    img.channels = 4;
    img.pixels.assign(data, data + static_cast<std::size_t>(w * h * 4));
    stbi_image_free(data);
    // A source image without alpha is treated as opaque; for greyscale output
    // that means plain luminance.
    return toChannels(std::move(img), opts.desiredChannels);
}

std::optional<ImageData> loadImage(const std::filesystem::path& path, const ImageLoadOptions& opts,
                                   std::string* error) {
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
