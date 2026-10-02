#pragma once
// Gobo images generated in code, so the sandbox needs no data files.
// Greyscale: white lets light through, black blocks it.

#include "assets/MeshData.h"

namespace dmxviz::sandbox {

enum class GoboPattern { Dots, Breakup, Star, Lines, Ring, Glass };

// Glass is an RGBA colour gobo; the others are single-channel.
assets::ImageData makeGobo(GoboPattern pattern, int size = 256);

}  // namespace dmxviz::sandbox
