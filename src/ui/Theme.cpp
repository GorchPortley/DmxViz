#include "ui/Theme.h"

#include "imgui.h"

namespace dmxviz::ui {

void applyTheme(float dpiScale) {
    ImGui::StyleColorsDark();
    ImGuiStyle& s = ImGui::GetStyle();
    s.WindowRounding = 3.0f;
    s.FrameRounding = 2.0f;
    s.GrabRounding = 2.0f;
    s.TabRounding = 2.0f;
    s.WindowBorderSize = 1.0f;
    s.FrameBorderSize = 0.0f;
    s.ItemSpacing = ImVec2(6, 4);
    s.ScrollbarSize = 12.0f;

    ImVec4* c = s.Colors;
    const ImVec4 accent(0.95f, 0.55f, 0.15f, 1.0f);  // amber, the classic console accent
    c[ImGuiCol_WindowBg] = ImVec4(0.09f, 0.09f, 0.10f, 1.0f);
    c[ImGuiCol_ChildBg] = ImVec4(0.08f, 0.08f, 0.09f, 1.0f);
    c[ImGuiCol_PopupBg] = ImVec4(0.10f, 0.10f, 0.11f, 0.98f);
    c[ImGuiCol_Border] = ImVec4(0.20f, 0.20f, 0.22f, 1.0f);
    c[ImGuiCol_FrameBg] = ImVec4(0.15f, 0.15f, 0.17f, 1.0f);
    c[ImGuiCol_FrameBgHovered] = ImVec4(0.22f, 0.22f, 0.25f, 1.0f);
    c[ImGuiCol_FrameBgActive] = ImVec4(0.28f, 0.28f, 0.31f, 1.0f);
    c[ImGuiCol_TitleBg] = ImVec4(0.07f, 0.07f, 0.08f, 1.0f);
    c[ImGuiCol_TitleBgActive] = ImVec4(0.11f, 0.11f, 0.12f, 1.0f);
    c[ImGuiCol_MenuBarBg] = ImVec4(0.07f, 0.07f, 0.08f, 1.0f);
    c[ImGuiCol_Header] = ImVec4(0.20f, 0.20f, 0.23f, 1.0f);
    c[ImGuiCol_HeaderHovered] = ImVec4(0.28f, 0.28f, 0.32f, 1.0f);
    c[ImGuiCol_HeaderActive] = ImVec4(accent.x, accent.y, accent.z, 0.45f);
    c[ImGuiCol_Button] = ImVec4(0.18f, 0.18f, 0.21f, 1.0f);
    c[ImGuiCol_ButtonHovered] = ImVec4(0.26f, 0.26f, 0.30f, 1.0f);
    c[ImGuiCol_ButtonActive] = ImVec4(accent.x, accent.y, accent.z, 0.6f);
    c[ImGuiCol_CheckMark] = accent;
    c[ImGuiCol_SliderGrab] = accent;
    c[ImGuiCol_SliderGrabActive] = ImVec4(1.0f, 0.7f, 0.3f, 1.0f);
    c[ImGuiCol_Tab] = ImVec4(0.12f, 0.12f, 0.14f, 1.0f);
    c[ImGuiCol_TabHovered] = ImVec4(0.28f, 0.28f, 0.32f, 1.0f);
    c[ImGuiCol_TabSelected] = ImVec4(0.20f, 0.20f, 0.23f, 1.0f);
    c[ImGuiCol_TabSelectedOverline] = accent;
    c[ImGuiCol_DockingPreview] = ImVec4(accent.x, accent.y, accent.z, 0.5f);

    s.ScaleAllSizes(dpiScale);
    ImGui::GetIO().FontGlobalScale = 1.0f;
}

}  // namespace dmxviz::ui
