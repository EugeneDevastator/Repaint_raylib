#include "ui_leftpanel.h"
#include "rlImGui.h"
#include "imgui.h"
#include "imgui_internal.h"
#include <cstdint>

extern bool layersDirty;
extern bool panelResizing;
extern int uiPanelWidth;
extern BParam bpOpacity, bpSize, bpHardness, bpSpacing, bpCurvature, bpScatter;

static RenderTexture2D stampPrev = {0};
static bool stampPrevInited = false;
Texture2D g_blendModeIcon = {0};
bool g_blendIconLoaded = false;

void LeftPanel_Init(void) {
    if (!stampPrevInited) {
        stampPrev = LoadRenderTexture(100, 100);
        stampPrevInited = true;
    }
    if (!g_blendIconLoaded) {
        if (FileExists("resources/ctlbm.png")) {
            Image img = LoadImage("resources/ctlbm.png");
            ImageResize(&img, 24, 24);
            g_blendModeIcon = LoadTextureFromImage(img);
            g_blendIconLoaded = g_blendModeIcon.id > 0;
            UnloadImage(img);
        }
    }
}

void LeftPanel_Shutdown(void) {
    if (stampPrevInited && stampPrev.id > 0) {
        UnloadRenderTexture(stampPrev);
        stampPrevInited = false;
    }
    if (g_blendIconLoaded) {
        UnloadTexture(g_blendModeIcon);
        g_blendIconLoaded = false;
    }
}

static void DrawBParamSlider(BParam* bp) {
    ImGui::PushID(bp->id);

    float ctrlH = 24.0f;
    float spacing = 4.0f;

    // Icon — ctrlH square
    if (bp->iconLoaded)
        ImGui::Image((ImTextureID)(intptr_t)bp->iconTex.id, ImVec2(ctrlH, ctrlH));
    else
        ImGui::Dummy(ImVec2(ctrlH, ctrlH));
    ImGui::SameLine(0, spacing);

    // Slider
    float avail = ImGui::GetContentRegionAvail().x;
    float sliderW = avail - ctrlH - spacing;
    if (sliderW < 10.0f) sliderW = 10.0f;

    ImDrawList* dl = ImGui::GetWindowDrawList();
    ImVec2 pos = ImGui::GetCursorScreenPos();
    ImRect bb(pos, ImVec2(pos.x + sliderW, pos.y + ctrlH));

    // Background gradient
    ImU32 gradA = IM_COL32(bp->slider.gradStart.r, bp->slider.gradStart.g, bp->slider.gradStart.b, bp->slider.gradStart.a);
    ImU32 gradB = IM_COL32(bp->slider.gradEnd.r, bp->slider.gradEnd.g, bp->slider.gradEnd.b, bp->slider.gradEnd.a);
    dl->AddRectFilledMultiColor(bb.Min, bb.Max, gradA, gradB, gradB, gradA);

    // 3D sunken frame
    ImU32 shade = IM_COL32(bp->slider.shade.r, bp->slider.shade.g, bp->slider.shade.b, bp->slider.shade.a);
    ImU32 hlite = IM_COL32(bp->slider.hlite.r, bp->slider.hlite.g, bp->slider.hlite.b, bp->slider.hlite.a);
    dl->AddLine(bb.Min, ImVec2(bb.Max.x, bb.Min.y), shade);
    dl->AddLine(bb.Min, ImVec2(bb.Min.x, bb.Max.y), shade);
    dl->AddLine(ImVec2(bb.Max.x - 1, bb.Min.y), ImVec2(bb.Max.x - 1, bb.Max.y), hlite);
    dl->AddLine(ImVec2(bb.Min.x, bb.Max.y - 1), ImVec2(bb.Max.x, bb.Max.y - 1), hlite);

    // Invisible button — direct position mapping
    ImGui::InvisibleButton("##sl", bb.GetSize(), ImGuiButtonFlags_MouseButtonLeft | ImGuiButtonFlags_MouseButtonMiddle | ImGuiButtonFlags_MouseButtonRight);
    if (ImGui::IsItemActive()) {
        float mx = (ImGui::GetMousePos().x - bb.Min.x) / (bb.Max.x - bb.Min.x);
        mx = fminf(fmaxf(mx, 0.0f), 1.0f);
        if (ImGui::IsMouseDown(0))
            bp->slider.clipmaxF = mx;
        else if (ImGui::IsMouseDown(1))
            bp->slider.clipminF = mx;
        else if (ImGui::IsMouseDown(2))
            bp->slider.jitter = mx;
    }

    // Range fill
    {
        float minX = bb.Min.x + (bb.Max.x - bb.Min.x) * bp->slider.clipminF;
        float maxX = bb.Min.x + (bb.Max.x - bb.Min.x) * bp->slider.clipmaxF;
        if (maxX > minX)
            dl->AddRectFilled(ImVec2(minX, bb.Min.y), ImVec2(maxX, bb.Max.y), IM_COL32(0, 80, 200, 40));
    }

    // Grabber width = 0.5 * control height
    float grabHalf = ctrlH * 0.25f;

    // Secondary grabber (dark, right-click)
    {
        float grabX = bb.Min.x + (bb.Max.x - bb.Min.x) * bp->slider.clipminF;
        ImRect gr(ImVec2(grabX - grabHalf, bb.Min.y),
                  ImVec2(grabX + grabHalf, bb.Max.y));
        dl->AddRectFilled(gr.Min, gr.Max, IM_COL32(60, 60, 60, 255));
        dl->AddRect(gr.Min, gr.Max, IM_COL32(30, 30, 30, 200));
    }

    // Primary grabber (white, left-click)
    {
        float grabX = bb.Min.x + (bb.Max.x - bb.Min.x) * bp->slider.clipmaxF;
        ImRect gr(ImVec2(grabX - grabHalf, bb.Min.y),
                  ImVec2(grabX + grabHalf, bb.Max.y));
        dl->AddRectFilled(gr.Min, gr.Max, IM_COL32(255, 255, 255, 255));
        dl->AddRect(gr.Min, gr.Max, IM_COL32(80, 80, 80, 200));
    }

    // Value text
    {
        float dispVal = bp->slider.clipmaxF * (bp->outMax - bp->outMin) + bp->outMin;
        char txt[32];
        if (bp->outMax - bp->outMin >= 10.0f)
            snprintf(txt, sizeof(txt), "%.0f", dispVal);
        else
            snprintf(txt, sizeof(txt), "%.2f", dispVal);
        ImVec2 txtSz = ImGui::CalcTextSize(txt);
        dl->AddText(ImVec2(bb.Min.x + (bb.Max.x - bb.Min.x - txtSz.x) * 0.5f,
                           bb.Min.y + (bb.Max.y - bb.Min.y - txtSz.y) * 0.5f),
                    IM_COL32(0, 0, 0, 200), txt);
    }

    // Pen mode button — ctrlH square
    ImGui::SetCursorScreenPos(ImVec2(bb.Max.x + spacing, bb.Min.y));
    {
        char popupID[32];
        snprintf(popupID, sizeof(popupID), "pen_%d", bp->id);

        Texture2D ptex = GetPenModeIcon(bp->penMode);
        ImTextureID texID = (ptex.id > 0) ? (ImTextureID)(intptr_t)ptex.id : 0;

        ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.90f, 0.90f, 0.90f, 1.0f));
        ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.80f, 0.80f, 0.80f, 1.0f));
        ImGui::PushStyleColor(ImGuiCol_ButtonActive, ImVec4(0.72f, 0.72f, 0.72f, 1.0f));
        ImGui::PushStyleColor(ImGuiCol_NavHighlight, ImVec4(0, 0, 0, 0));

        if (texID) {
            if (ImGui::ImageButton("##pen", texID, ImVec2(ctrlH, ctrlH)))
                ImGui::OpenPopup(popupID);
        } else {
            if (ImGui::Button("...", ImVec2(ctrlH, ctrlH)))
                ImGui::OpenPopup(popupID);
        }

        ImGui::PopStyleColor(4);

        if (ImGui::BeginPopup(popupID)) {
            for (int p = 0; p < PEN_MODE_COUNT; p++) {
                Texture2D itex = GetPenModeIcon(p);
                if (itex.id > 0) {
                    ImGui::Image((ImTextureID)(intptr_t)itex.id, ImVec2(16, 16));
                    ImGui::SameLine();
                }
                if (ImGui::Selectable(PenModeNames[p], bp->penMode == p))
                    bp->penMode = p;
            }
            ImGui::EndPopup();
        }
    }

    ImGui::PopID();
}

void LeftPanel_Draw(AppState* state) {
    ImGui::SetNextWindowPos(ImVec2(0, 0), ImGuiCond_Always);
    ImGui::SetNextWindowSize(ImVec2((float)uiPanelWidth, (float)SCREEN_HEIGHT), ImGuiCond_Always);
    ImGui::Begin("Tools", NULL,
        ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoMove |
        ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoTitleBar |
        ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse);

    // Brush type
    {
        static const char* blendNames[] = {
            "Normal","Add","Dodge","Screen","Lighten","Burn",
            "Multiply","Darken","Overlay","Highlight","Shadowlight",
            "Xor","Diff","Exclusion"
        };
        int blend = (int)state->currentBrush.Realb.bmidx;
        if (g_blendIconLoaded)
            ImGui::Image((ImTextureID)(intptr_t)g_blendModeIcon.id, ImVec2(24, 24));
        else
            ImGui::Dummy(ImVec2(24, 24));
        ImGui::SameLine();
        ImGui::SetNextItemWidth(-1);
        if (ImGui::Combo("##brushBlend", &blend, blendNames, 14, 14))
            state->currentBrush.Realb.bmidx = (uint8_t)blend;
    }

    ImGui::Separator();
    ImGui::Text("Settings");
    ImGui::Spacing();

    // Brush stamp preview
    if (stampPrevInited && stampPrev.id > 0) {
        BeginTextureMode(stampPrev);
        ClearBackground(BLANK);
        EndTextureMode();

        d_Brush pb = state->currentBrush;
        Color pc = pb.Realb.col;
        pb.Realb.col = WHITE;
        pb.Realb.opacity = 1.0f;
        float prevRadOut = pb.Realb.rad_out;
        if (prevRadOut > 45.0f) {
            pb.Realb.rad_out = 45.0f;
            pb.Realb.rad_in = pb.Realb.rad_in * (45.0f / fmaxf(prevRadOut, 1.0f));
        }
        BrushBlend_ApplyStamp(stampPrev, &pb, 50, 50);
        pb.Realb.rad_out = prevRadOut;
        pb.Realb.col = pc;

        float cw = ImGui::GetContentRegionAvail().x;
        float previewSize = fminf(cw, 100.0f);
        ImGui::SetCursorPosX((cw - previewSize) * 0.5f);
        ImGui::Image((ImTextureID)(intptr_t)stampPrev.texture.id,
            ImVec2(previewSize, previewSize));
    }

    // BParam sliders
    BParam* bps[] = {&bpSize, &bpHardness, &bpCurvature, &bpSpacing, &bpOpacity, &bpScatter};
    for (int i = 0; i < 6; i++)
        DrawBParamSlider(bps[i]);

    ImGui::Spacing();
    int preserve = state->currentBrush.Realb.preserveop;
    ImGui::Checkbox("Preserve Layer Alpha", (bool*)&preserve);
    state->currentBrush.Realb.preserveop = (uint8_t)preserve;

    // Pipeline selector
    {
        const char* pipeNames[] = {"CFNSR", "RS"};
        int pipe = (int)state->currentBrush.Realb.pipeID;
        ImGui::SetNextItemWidth(-1);
        if (ImGui::Combo("##pipeline", &pipe, pipeNames, 2))
            state->currentBrush.Realb.pipeID = (uint8_t)pipe;
    }

    // Reload shaders button at bottom
    {
        float windowH = ImGui::GetWindowHeight();
        float itemH = ImGui::GetFrameHeight() + ImGui::GetStyle().ItemSpacing.y;
        ImGui::SetCursorPosY(windowH - itemH * 3 - ImGui::GetStyle().FramePadding.y * 2);

        if (ImGui::Button("Reload Shaders", ImVec2(-1, 0))) {
            BrushBlend_Shutdown();
            BrushBlend_Init();
            ReloadViewportShader();
        }
    }

    // Zoom and mode info at bottom
    {
        float windowH = ImGui::GetWindowHeight();
        float itemH = ImGui::GetFrameHeight() + ImGui::GetStyle().ItemSpacing.y;
        ImGui::SetCursorPosY(windowH - itemH * 2);

        char zoomInfo[32];
        sprintf(zoomInfo, "Zoom: %.0f%%", state->camera.zoom * 100.0f);
        ImGui::Text("%s", zoomInfo);

        const char* modeNames[] = {"None", "Brush", "Smudge", "Disp", "Cont", "Line"};
        ImGui::Text("%s", modeNames[state->mode > 5 ? 0 : state->mode]);
    }

    // Separator + resize handle at right edge (drawn inside ImGui for proper z-order)
    {
        ImDrawList* dl = ImGui::GetWindowDrawList();
        ImVec2 wMin = ImGui::GetWindowPos();
        ImVec2 wSize = ImGui::GetWindowSize();
        float handleX = wMin.x + wSize.x;
        ImU32 col = panelResizing ? IM_COL32(80, 120, 200, 255) : IM_COL32(160, 160, 160, 255);
        dl->AddRectFilled(ImVec2(handleX - 3, wMin.y), ImVec2(handleX + 4, wMin.y + SCREEN_HEIGHT), col);

        // Invisible button for resize interaction
        ImGui::SetCursorScreenPos(ImVec2(handleX - 3, wMin.y));
        ImGui::InvisibleButton("##resize", ImVec2(7, SCREEN_HEIGHT));
        if (ImGui::IsItemHovered() || ImGui::IsItemActive())
            ImGui::SetMouseCursor(ImGuiMouseCursor_ResizeEW);
        if (ImGui::IsItemActive()) {
            panelResizing = true;
            float mx = ImGui::GetMousePos().x;
            uiPanelWidth = (int)fmaxf(120.0f, fminf(mx, (float)(SCREEN_WIDTH - RIGHT_PANEL_WIDTH - 100)));
            Rectangle vb = {(float)uiPanelWidth, 0,
                (float)(SCREEN_WIDTH - uiPanelWidth - RIGHT_PANEL_WIDTH), (float)SCREEN_HEIGHT};
            Viewport_SetBounds(&viewport, vb);
        } else {
            panelResizing = false;
        }
    }

    ImGui::End();
}
