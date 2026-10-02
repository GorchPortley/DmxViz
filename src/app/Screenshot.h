#pragma once

#include <filesystem>

namespace dmxviz::app {

// Reads the current GL default framebuffer and writes it as PNG. Call after the
// swapchain pass has ended and before sg_commit().
bool saveBackbufferPng(const std::filesystem::path& path, int width, int height);

}  // namespace dmxviz::app
