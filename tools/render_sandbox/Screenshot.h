#pragma once

#include <filesystem>

namespace dmxviz::sandbox {

// Reads the current GL default framebuffer and writes it as PNG. Call after the
// swapchain pass has ended and before sg_commit(). (Same approach as the app's
// screenshot helper, which lives in the app executable and cannot be linked.)
bool saveBackbufferPng(const std::filesystem::path& path, int width, int height);

}  // namespace dmxviz::sandbox
