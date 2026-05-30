#include "repaint.h"
#include "brush_preset.h"
#include "ui_texpanel.h"
#include "rlgl.h"
#include "imgui.h"
#include <math.h>
#include <algorithm>

static int g_texPanelSelected = -1;

bool TexPanelModule::HandleInput(InputState& input, const DrawRect& rect) {
    (void)rect;
    if (input.mouseCaptured) return false;
    if (g_activeHud != HUD_QUICK) return false;
    if (g_texPanelAreaY > 0 && input.MousePos().y >= g_texPanelAreaY) {
        input.mouseCaptured = true;
        return true;
    }
    return false;
}

void TexPanelModule::DrawGUI(const DrawRect& rect) {
    if (g_activeHud != HUD_QUICK) return;
    if (g_texPanelAreaY <= 0) return;

    // Draws within the current ImGui window context (##qpui),
    // positioned using g_texPanelAreaY for the y-coordinate.

    rlSetBlendMode(RL_BLEND_ALPHA);

    float thirdW = rect.w / 3.0f;
    float yPos = (float)g_texPanelAreaY;

    // ── Left 1/3: radio groups ──
    int mm = state->currentBrush.Realb.useTexLumAsAlpha ? 0 : 1;
    int mx = state->currentBrush.Realb.texBlendMode;
    int tnm = state->currentBrush.Realb.texNoisemode;
    int cm = state->currentBrush.Realb.texColorMode;

    float colW = (float)uiPanelWidth * 0.6f;

    rlSetBlendMode(RL_BLEND_ALPHA);
    ImGui::SetCursorScreenPos(ImVec2(rect.x, yPos));
    ImGui::BeginChild("##texLeft", ImVec2(thirdW, 0), false);
    {
        ImGui::SetCursorPos(ImVec2(10, 10));
        float x = 10;

        static const char* items0[] = {"lum is alpha", "tex.a is alpha"};
        static const char* items1[] = {"multiply", "threshold", "use tex mask"};
        static const char* items2[] = {"Stencil", "Random", "Const"};
        static const char* items3[] = {"brush RGB", "texture RGB", "mul brush*tex"};

        ImGui::SetCursorPos(ImVec2(x, 10));
        ImGui::BeginChild("##rg0", ImVec2(colW, 0), false);
        { int v = mm; DrawRadioGroup("Mask Mode", &v, items0, 2); mm = v; }
        ImGui::EndChild();
        x += colW + 2.0f;

        ImGui::SetCursorPos(ImVec2(x, 10));
        ImGui::BeginChild("##rg1", ImVec2(colW, 0), false);
        { int v = mx; DrawRadioGroup("Mask Mix", &v, items1, 3); mx = v; }
        ImGui::EndChild();
        x += colW + 2.0f;

        ImGui::SetCursorPos(ImVec2(x, 10));
        ImGui::BeginChild("##rg2", ImVec2(colW, 0), false);
        { int v = tnm; DrawRadioGroup("Sample Mode", &v, items2, 3); tnm = v; }
        ImGui::EndChild();

        ImGui::SetCursorPos(ImVec2(10, 120));
        ImGui::BeginChild("##rg3", ImVec2(colW, 0), false);
        { int v = cm; DrawRadioGroup("Color", &v, items3, 3); cm = v; }
        ImGui::EndChild();
        ImGui::SetCursorPos(ImVec2(10 + colW + 2, 120));
        ImGui::BeginChild("##rg4", ImVec2(colW, 0), false);
        { static const char* items4[] = {"Brush", "Global"};
          int v = g_texScaleMode; DrawRadioGroup("Tex Scale", &v, items4, 2); g_texScaleMode = v; }
        ImGui::EndChild();
    }
    ImGui::EndChild();

    state->currentBrush.Realb.useTexLumAsAlpha = (mm == 0);
    state->currentBrush.Realb.texBlendMode = mx;
    state->currentBrush.Realb.texNoisemode = tnm;
    state->currentBrush.Realb.texColorMode = cm;

    // ── Middle 1/3: texture sliders ──
    rlSetBlendMode(RL_BLEND_ALPHA);
    ImGui::SetCursorScreenPos(ImVec2(rect.x + thirdW, yPos));
    ImGui::BeginChild("##texMiddle", ImVec2(thirdW, 0), false);
    ImGui::SetCursorPos(ImVec2(10, 10));
    DrawSlider(&bpTexScale, 0);
    rlSetBlendMode(RL_BLEND_ALPHA);
    DrawSlider(&bpTexFeather, 0);
    rlSetBlendMode(RL_BLEND_ALPHA);
    DrawSlider(&bpTexThresh, 0);
    rlSetBlendMode(RL_BLEND_ALPHA);
    DrawSlider(&bpTexBlendVal, 0);
    ImGui::EndChild();

    // ── Right 1/3: texture panel ──
    rlSetBlendMode(RL_BLEND_ALPHA);
    ImGui::SetCursorScreenPos(ImVec2(rect.x + 2.0f * thirdW, yPos));
    ImGui::BeginChild("##texPanel", ImVec2(thirdW, 0), false);

    float btnW5 = (ImGui::GetContentRegionAvail().x - ImGui::GetStyle().ItemSpacing.x * 4) / 5.0f;
    if (btnW5 < 30) btnW5 = 30;
    if (ImGui::Button("Add", ImVec2(btnW5, 0))) {
        char name[64]; snprintf(name, sizeof(name), "Texture %d", state->brushTexCount + 1);
        int idx = BrushTex_Add(state, name, 512, 512);
        if (idx >= 0) { state->editTexMode = 1; state->activeBrushTex = idx; }
    }
    ImGui::SameLine();
    if (ImGui::Button("Dupe", ImVec2(btnW5, 0))) {
        if (g_texPanelSelected >= 0) {
            int src = g_texPanelSelected;
            if (src >= state->brushTexCount) src = state->brushTexCount - 1;
            if (src >= 0) {
                int di = BrushTex_Add(state, state->brushTex[src].name, state->brushTex[src].w, state->brushTex[src].h);
                if (di >= 0) {
                    UnloadImage(state->brushTex[di].cpuImage);
                    state->brushTex[di].cpuImage = ImageCopy(state->brushTex[src].cpuImage);
                    Texture2D tmp = LoadTextureFromImage(state->brushTex[di].cpuImage);
                    BeginTextureMode(state->brushTex[di].rt);
                    ClearBackground(BLANK);
                    rlSetBlendMode(RL_BLEND_CUSTOM);
                    rlSetBlendFactors(RL_ONE, RL_ZERO, RL_FUNC_ADD);
                    DrawTextureRec(state->brushTex[src].rt.texture,
                        Rectangle{0, 0, (float)state->brushTex[src].w, (float)-state->brushTex[src].h},
                        Vector2{0, 0}, WHITE);
                    rlSetBlendMode(RL_BLEND_ALPHA);
                    EndTextureMode();
                    UnloadTexture(tmp);
                }
            }
        }
    }
    ImGui::SameLine();
    if (ImGui::Button("Edit", ImVec2(btnW5, 0))) {
        if (g_texPanelSelected >= 0 && g_texPanelSelected < state->brushTexCount)
            { state->editTexMode = 1; state->activeBrushTex = g_texPanelSelected; }
    }
    ImGui::SameLine();
    if (ImGui::Button("Use", ImVec2(btnW5, 0))) {
        if (g_texPanelSelected >= 0 && g_texPanelSelected < state->brushTexCount)
            { state->editTexMode = 0; state->activeBrushTex = g_texPanelSelected; }
        else if (g_texPanelSelected == -1)
            { state->editTexMode = 0; state->activeBrushTex = -1; }
    }
    ImGui::SameLine();
    if (ImGui::Button("Del", ImVec2(btnW5, 0))) {
        if (g_texPanelSelected >= 0 && g_texPanelSelected < state->brushTexCount && !state->brushTex[g_texPanelSelected].builtIn) {
            BrushTex_Delete(state, g_texPanelSelected);
            g_texPanelSelected = -1;
        }
    }

    float listH = ImGui::GetContentRegionAvail().y;
    if (listH < 20) listH = 20;
    int texSz = 72, texGap = 1;
    int texCols = (int)(ImGui::GetContentRegionAvail().x) / (texSz + texGap);
    if (texCols < 2) texCols = 2;

    int bundledCount = 0;
    for (int ti = 0; ti < state->brushTexCount; ti++)
        if (state->brushTex[ti].builtIn) bundledCount++;

    ImGui::PushStyleColor(ImGuiCol_ChildBg, ImVec4(1, 1, 1, 1));
    ImGui::BeginChild("##texGrid", ImVec2(0, listH), ImGuiChildFlags_Borders);

    ImGui::PushID("texNone");
    bool isNone = (g_texPanelSelected == -1);
    if (isNone) ImGui::PushStyleColor(ImGuiCol_Border, ImVec4(1,1,1,1));
    if (ImGui::ImageButton("##tn", (ImTextureID)0, ImVec2(texSz, texSz))) {
        g_texPanelSelected = -1; state->activeBrushTex = -1; state->editTexMode = 0;
    }
    if (isNone) ImGui::PopStyleColor();
    if (ImGui::IsItemHovered()) ImGui::SetTooltip("No Texture");
    ImGui::PopID();

    int col = 0;
    for (int ti = 0; ti < bundledCount; ti++) {
        if (col % texCols != 0) ImGui::SameLine(0, texGap);
        col++; ImGui::PushID(700 + ti);
        Texture2D thumb = BrushTex_GetThumb(state, ti);
        bool isSel = (g_texPanelSelected == ti);
        if (isSel) ImGui::PushStyleColor(ImGuiCol_Border, ImVec4(0.3f, 0.7f, 1.0f, 1.0f));
        if (thumb.id > 0 && ImGui::ImageButton("##t", (ImTextureID)(intptr_t)thumb.id, ImVec2(texSz, texSz))) {
            g_texPanelSelected = ti; state->activeBrushTex = ti; state->editTexMode = 0;
        }
        if (isSel) ImGui::PopStyleColor();
        if (ImGui::IsItemHovered()) ImGui::SetTooltip("%s", state->brushTex[ti].name);
        ImGui::PopID();
    }

    if (bundledCount > 0 && bundledCount < state->brushTexCount) {
        ImGui::Separator(); col = 0;
    }

    for (int ti = bundledCount; ti < state->brushTexCount; ti++) {
        if (col % texCols != 0) ImGui::SameLine(0, texGap);
        col++; ImGui::PushID(700 + ti);
        Texture2D thumb = BrushTex_GetThumb(state, ti);
        bool isSel = (g_texPanelSelected == ti);
        if (isSel) ImGui::PushStyleColor(ImGuiCol_Border, ImVec4(0.3f, 0.7f, 1.0f, 1.0f));
        if (thumb.id > 0 && ImGui::ImageButton("##t", (ImTextureID)(intptr_t)thumb.id, ImVec2(texSz, texSz))) {
            g_texPanelSelected = ti; state->activeBrushTex = ti; state->editTexMode = 0;
        }
        if (isSel) ImGui::PopStyleColor();
        if (ImGui::IsItemHovered()) ImGui::SetTooltip("%s", state->brushTex[ti].name);
        ImGui::PopID();
    }

    ImGui::EndChild();
    ImGui::PopStyleColor();
    ImGui::EndChild();
}
