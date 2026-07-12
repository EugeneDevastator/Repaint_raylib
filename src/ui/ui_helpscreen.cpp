#include "ui_helpscreen.h"
#include "imgui.h"
#include "layerstack.h"
#include "platform_utils.h"
#include <string.h>

static bool g_showHelp = false;

void Help_Toggle(void) {
    g_showHelp = !g_showHelp;
}

void Help_Draw(AppState* state) {
    if (!g_showHelp) return;
    if (IsKeyPressed(KEY_ESCAPE)) { g_showHelp = false; return; }

    int sw = GetScreenWidth(), sh = GetScreenHeight();
    int winW = sw * 3 / 5, winH = sh * 9 / 10;
    if (winW < 500) winW = 500;
    if (winH < 400) winH = 400;
    ImGui::SetNextWindowSize(ImVec2((float)winW, (float)winH), ImGuiCond_Always);
    ImGui::SetNextWindowPos(ImVec2((float)(sw - winW) * 0.5f, (float)(sh - winH) * 0.5f), ImGuiCond_Always);

    if (ImGui::Begin("Help", &g_showHelp,
        ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoMove))
    {
        float avail = ImGui::GetContentRegionAvail().x;
        float col1W = avail * 2.0f / 3.0f - 8.0f;
        float col2W = avail * 1.0f / 3.0f - 8.0f;

        // ── Left column via BeginChild (starts at top) ──
        ImGui::BeginChild("##leftCol", ImVec2(col1W, 0), false);
        ImGui::Text("Common Key Bindings");
        ImGui::Separator();
        ImGui::Spacing();
        struct { const char* key; const char* desc; } keys[] = {
            {"Shift",       "Open Brush and File settings (your main menu)"},
            {"Space",   "Pan canvas"},
            {"ALT",   "Color picker"},
            {"Mouse Scroll",  "Zoom in / out"},
            {"1",       "Layer Transform — move / scale / rotate"},
            {"2",       "Painting mode (default one)"},
            {"3",       "Matte Extraction"},
            {"4",       "Stable Diffusion HUD"},
            {"5",       "Perspective Warp HUD"},
            {"R",       "Repeat last transformation"},
            {"Enter/E",   "Accept warp / transform"},
            {"Escape",  "Cancel / close HUD"},
            {"F1",      "Toggle this help screen"},
            {"Ctrl+Z",  "Undo Painting"},
            {"Ctrl+Shift+Z","Redo Painting"},
            {"Ctrl+C",  "Copy Layer"},
            {"Ctrl+C,C",  "Copy Merged. ctrl press, C, C, ctrl release"},
            {"Ctrl+C,F",  "Copy Merged as temporary file"},
            {"Ctrl+V",  "Paste"},
        };
        for (auto& k : keys) {
            ImGui::Text("  %s", k.key);
            ImGui::SameLine(170);
            ImGui::TextWrapped("%s", k.desc);
        }
        ImGui::Spacing();
        ImGui::Separator();
        ImGui::Text("Slider Operations");
        ImGui::Separator();
        ImGui::Spacing();
        struct { const char* key; const char* desc; } sliderKeys[] = {
            {"Middle Click","Adjust Jitter"},
            {"Right Click","Adjust Min Checkmark (shows for modulation only)"},
        };
        for (auto& k : sliderKeys) {
            ImGui::Text("  %s", k.key);
            ImGui::SameLine(170);
            ImGui::TextWrapped("%s", k.desc);
        }
        ImGui::EndChild();

        ImGui::SameLine();

        // ── Right column via BeginChild (starts at top, independent of left) ──
        ImGui::BeginChild("##rightCol", ImVec2(col2W, 0), false);
        ImGui::Text("Misc");
        ImGui::Separator();
        ImGui::Spacing();
        ImGui::TextWrapped("Repaint is free, open source, offline painting tool.");
        ImGui::Spacing();
        ImGui::TextWrapped("Active layer: %d", state->activeLayer);
        ImGui::TextWrapped("Canvas: %d x %d px", DocOutPxW(&state->doc), DocOutPxH(&state->doc));
        ImGui::TextWrapped("Camera zoom: %.0f%%", state->camera.zoom * 100.0f);
        ImGui::Spacing();
        if (ImGui::Button("Watch tutorials", ImVec2(ImGui::GetContentRegionAvail().x, 0))) {
            Platform_OpenURL("https://www.youtube.com/playlist?list=PLbvqc9wW3qag");
        }
        if (ImGui::IsItemHovered())
            ImGui::SetTooltip("https://www.youtube.com/playlist?list=PLbvqc9wW3qag");
        ImGui::Spacing();
        ImGui::Separator();
        ImGui::Spacing();
        ImGui::Text("Support the project");
        ImGui::Spacing();
        if (ImGui::Button("Donate on Ko-fi", ImVec2(ImGui::GetContentRegionAvail().x, 0))) {
            Platform_OpenURL("https://ko-fi.com/daveastator");
        }
        if (ImGui::IsItemHovered())
            ImGui::SetTooltip("https://ko-fi.com/daveastator");
        ImGui::Spacing();

        ImGui::EndChild();
    }
    ImGui::End();
}
