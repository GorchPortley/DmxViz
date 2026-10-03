#include "ui/fixture_editor/GoboThumbnails.h"

#include "assets/ImageLoader.h"

#include <algorithm>

namespace dmxviz::ui::fixture_editor {

std::uint32_t GoboThumbnails::checksumOf(const fixtures::Resource& resource) {
    // FNV-1a over (at most) the first and last 256 bytes: cheap, and enough to notice a different file.
    std::uint32_t hash = 2166136261u;
    const std::size_t n = resource.data.size();
    auto mix = [&](std::size_t i) { hash = (hash ^ resource.data[i]) * 16777619u; };
    for (std::size_t i = 0; i < std::min<std::size_t>(n, 256); ++i) mix(i);
    for (std::size_t i = n > 256 ? std::max<std::size_t>(256, n - 256) : n; i < n; ++i) mix(i);
    return hash;
}

GoboThumbnails::Entry GoboThumbnails::decode(const fixtures::Resource& resource) {
    Entry entry;
    entry.byteCount = resource.data.size();
    entry.checksum = checksumOf(resource);

    assets::ImageLoadOptions options;
    options.desiredChannels = 1;  // gobos are light masks, like the renderer reads them
    options.svgSize = 64;
    const std::optional<assets::ImageData> image = assets::loadImageFromMemory(resource.data, resource.format, options);
    if (!image || !image->valid()) return entry;

    entry.levels.resize(static_cast<std::size_t>(kGrid * kGrid));
    for (int gy = 0; gy < kGrid; ++gy) {
        for (int gx = 0; gx < kGrid; ++gx) {
            // Average the source pixels that fall into this grid cell.
            const int x0 = gx * image->width / kGrid;
            const int x1 = std::max(x0 + 1, (gx + 1) * image->width / kGrid);
            const int y0 = gy * image->height / kGrid;
            const int y1 = std::max(y0 + 1, (gy + 1) * image->height / kGrid);
            int sum = 0;
            int count = 0;
            for (int y = y0; y < y1 && y < image->height; ++y)
                for (int x = x0; x < x1 && x < image->width; ++x, ++count)
                    sum += image->pixels[static_cast<std::size_t>(y * image->width + x)];
            const int grey = count > 0 ? sum / count : 0;
            entry.levels[static_cast<std::size_t>(gy * kGrid + gx)] = static_cast<std::uint8_t>(grey * kLevels / 256);
        }
    }
    entry.decoded = true;
    return entry;
}

void GoboThumbnails::draw(const fixtures::Resource* resource, float size) {
    const ImVec2 origin = ImGui::GetCursorScreenPos();
    ImGui::Dummy(ImVec2(size, size));
    ImDrawList* list = ImGui::GetWindowDrawList();
    const ImVec2 corner(origin.x + size, origin.y + size);
    list->AddRectFilled(origin, corner, IM_COL32(12, 12, 14, 255));

    if (resource != nullptr) {
        Entry& entry = entries_[resource->name];
        if (entry.byteCount != resource->data.size() || entry.checksum != checksumOf(*resource))
            entry = decode(*resource);
        if (entry.decoded) {
            const float cell = size / static_cast<float>(kGrid);
            for (int gy = 0; gy < kGrid; ++gy) {
                for (int gx = 0; gx < kGrid;) {
                    const std::uint8_t level = entry.levels[static_cast<std::size_t>(gy * kGrid + gx)];
                    int end = gx + 1;
                    while (end < kGrid && entry.levels[static_cast<std::size_t>(gy * kGrid + end)] == level) ++end;
                    if (level > 0) {
                        const int grey = 255 * level / (kLevels - 1);
                        list->AddRectFilled(
                            ImVec2(origin.x + cell * static_cast<float>(gx), origin.y + cell * static_cast<float>(gy)),
                            ImVec2(origin.x + cell * static_cast<float>(end),
                                   origin.y + cell * static_cast<float>(gy + 1)),
                            IM_COL32(grey, grey, grey, 255));
                    }
                    gx = end;
                }
            }
        } else {
            list->AddText(ImVec2(origin.x + 4.0f, origin.y + size * 0.3f), IM_COL32(255, 110, 100, 255), "?");
        }
    }
    list->AddRect(origin, corner, IM_COL32(110, 110, 120, 255));
}

}  // namespace dmxviz::ui::fixture_editor
