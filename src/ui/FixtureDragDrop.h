#pragma once
// FixtureDragDrop: the ImGui drag-and-drop payload that carries a fixture type from
// the fixture library to the viewport. A plain struct of char arrays, because ImGui
// copies the payload bytes.

#include <cstdio>
#include <string_view>

namespace dmxviz::ui {

inline constexpr const char* kFixtureDragPayload = "DMXVIZ_FIXTURE_TYPE";

struct FixtureDragPayload {
    char typeId[192] = {};
    char modeName[96] = {};  // empty = the type's first mode

    void set(std::string_view type, std::string_view mode) {
        std::snprintf(typeId, sizeof(typeId), "%.*s", static_cast<int>(type.size()), type.data());
        std::snprintf(modeName, sizeof(modeName), "%.*s", static_cast<int>(mode.size()), mode.data());
    }
};

}  // namespace dmxviz::ui
