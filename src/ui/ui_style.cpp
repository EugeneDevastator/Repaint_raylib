#include "ui_style.h"
#include "rlImGui.h"
#include "imgui.h"
#include "raylib.h"

void UIStyle::Init() {
    rlImGuiBeginInitImGui();
    ImGui::StyleColorsLight();

    ImGuiIO& io = ImGui::GetIO();
    io.Fonts->Clear();
    if (FileExists("resources/Cadman_Bold.otf"))
        io.Fonts->AddFontFromFileTTF("resources/Cadman_Bold.otf", 22.0f);
    else
        io.Fonts->AddFontDefault();

    ImGuiStyle& style = ImGui::GetStyle();
    style.FrameBorderSize = 1.0f;
    style.FrameRounding = 3.0f;
    style.GrabRounding = 3.0f;
    style.WindowPadding = ImVec2(8, 8);
    style.FramePadding = ImVec2(5, 4);
    style.ItemSpacing = ImVec2(5, 4);
    style.ItemInnerSpacing = ImVec2(4, 4);
    style.ScrollbarSize = 14.0f;

    // Neutral gray buttons (removing default blue tint)
    style.Colors[ImGuiCol_Button] = ImVec4(0.85f, 0.85f, 0.85f, 1.0f);
    style.Colors[ImGuiCol_ButtonHovered] = ImVec4(0.75f, 0.75f, 0.75f, 1.0f);
    style.Colors[ImGuiCol_ButtonActive] = ImVec4(0.65f, 0.65f, 0.65f, 1.0f);

    rlImGuiEndInitImGui();
}

void UIStyle::Shutdown() {
    rlImGuiShutdown();
}
