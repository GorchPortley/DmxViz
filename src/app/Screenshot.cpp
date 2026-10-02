#include "app/Screenshot.h"

#include "assets/ImageLoader.h"

#if defined(_WIN32)
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#endif
#include <GL/gl.h>

#include <cstring>

namespace dmxviz::app {

bool saveBackbufferPng(const std::filesystem::path& path, int width, int height) {
    if (width <= 0 || height <= 0) return false;
    assets::ImageData img;
    img.width = width;
    img.height = height;
    img.channels = 4;
    img.pixels.resize(static_cast<std::size_t>(width * height * 4));
    glPixelStorei(GL_PACK_ALIGNMENT, 1);
    glReadPixels(0, 0, width, height, GL_RGBA, GL_UNSIGNED_BYTE, img.pixels.data());
    // GL rows are bottom-up; PNG rows are top-down.
    const std::size_t row = static_cast<std::size_t>(width * 4);
    std::vector<std::uint8_t> tmp(row);
    for (int y = 0; y < height / 2; ++y) {
        std::uint8_t* a = img.pixels.data() + static_cast<std::size_t>(y) * row;
        std::uint8_t* b = img.pixels.data() + static_cast<std::size_t>(height - 1 - y) * row;
        std::memcpy(tmp.data(), a, row);
        std::memcpy(a, b, row);
        std::memcpy(b, tmp.data(), row);
    }
    for (std::size_t i = 3; i < img.pixels.size(); i += 4) img.pixels[i] = 255;
    return assets::writePng(path, img);
}

}  // namespace dmxviz::app
