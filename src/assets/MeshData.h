#pragma once
// CPU-side mesh and image data. The renderer uploads these lazily to the GPU.

#include "core/Math.h"

#include <cstdint>
#include <string>
#include <vector>

namespace dmxviz::assets {

struct Vertex {
    glm::vec3 position{0.0f};
    glm::vec3 normal{0.0f, 1.0f, 0.0f};
    glm::vec2 uv{0.0f};
};

// Indexed triangle list.
struct MeshData {
    std::string name;
    std::vector<Vertex> vertices;
    std::vector<std::uint32_t> indices;  // 3 per triangle
    Aabb bounds;

    std::size_t triangleCount() const { return indices.size() / 3; }
    void computeBounds();
    // Recomputes smooth per-vertex normals from triangle geometry.
    void computeNormals();
    // Appends `other` transformed by `transform` (normals use the inverse-transpose).
    void append(const MeshData& other, const glm::mat4& transform = glm::mat4(1.0f));
};

// 8-bit image, tightly packed rows, top row first.
struct ImageData {
    int width = 0;
    int height = 0;
    int channels = 4;  // 1 (greyscale) or 4 (RGBA)
    std::vector<std::uint8_t> pixels;

    bool valid() const {
        return width > 0 && height > 0 && pixels.size() == static_cast<std::size_t>(width * height * channels);
    }
};

}  // namespace dmxviz::assets
