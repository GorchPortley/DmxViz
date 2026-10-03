#pragma once
// GoboThumbnails: small previews of gobo images for the wheel editor.
//
// The fixture type holds the encoded file (PNG/JPG/SVG). Each image is decoded once into a coarse
// grid of grey levels, and the preview is drawn as ImGui rectangles (neighbouring cells of equal
// brightness are merged), so no GPU texture is needed. A resource that was replaced is noticed
// by its size and a checksum and decoded again.

#include "fixtures/FixtureType.h"

#include "imgui.h"

#include <cstdint>
#include <map>
#include <string>
#include <vector>

namespace dmxviz::ui::fixture_editor {

class GoboThumbnails {
public:
    // Draws a square preview of `resource` (a placeholder when null or undecodable) and
    // advances the ImGui cursor like a widget of `size` pixels would.
    void draw(const fixtures::Resource* resource, float size);

private:
    static constexpr int kGrid = 24;
    static constexpr int kLevels = 8;

    struct Entry {
        std::size_t byteCount = 0;
        std::uint32_t checksum = 0;
        bool decoded = false;
        std::vector<std::uint8_t> levels;  // kGrid * kGrid values 0..kLevels-1
    };

    static std::uint32_t checksumOf(const fixtures::Resource& resource);
    static Entry decode(const fixtures::Resource& resource);

    std::map<std::string, Entry> entries_;
};

}  // namespace dmxviz::ui::fixture_editor
