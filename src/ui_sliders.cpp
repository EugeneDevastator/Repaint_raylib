#include "repaint.h"
#include "imgui.h"
#include "imgui_internal.h"
#include <math.h>

/* ── DrawBParamSlider (horizontal) ─────────────────────────────────────── */

void DrawBParamSlider(BParam* bp) {
    ImGui::PushID(bp->id);

    float ctrlH = 24.0f;
    float spacing = 4.0f;

    if (bp->iconLoaded)
        ImGui::Image((ImTextureID)(intptr_t)bp->iconTex.id, ImVec2(ctrlH, ctrlH));
    else
        ImGui::Dummy(ImVec2(ctrlH, ctrlH));
    ImGui::SameLine(0, spacing);

    float avail = ImGui::GetContentRegionAvail().x;
    float sliderW = avail - ctrlH - spacing;
    if (sliderW < 10.0f) sliderW = 10.0f;

    ImDrawList* dl = ImGui::GetWindowDrawList();
    ImVec2 pos = ImGui::GetCursorScreenPos();
    ImRect bb(pos, ImVec2(pos.x + sliderW, pos.y + ctrlH));

    ImU32 gradA = IM_COL32(bp->slider.gradStart.r, bp->slider.gradStart.g, bp->slider.gradStart.b, bp->slider.gradStart.a);
    ImU32 gradB = IM_COL32(bp->slider.gradEnd.r, bp->slider.gradEnd.g, bp->slider.gradEnd.b, bp->slider.gradEnd.a);
    dl->AddRectFilledMultiColor(bb.Min, bb.Max, gradA, gradB, gradB, gradA);

    ImU32 shade = IM_COL32(bp->slider.shade.r, bp->slider.shade.g, bp->slider.shade.b, bp->slider.shade.a);
    ImU32 hlite = IM_COL32(bp->slider.hlite.r, bp->slider.hlite.g, bp->slider.hlite.b, bp->slider.hlite.a);
    dl->AddLine(bb.Min, ImVec2(bb.Max.x, bb.Min.y), shade);
    dl->AddLine(bb.Min, ImVec2(bb.Min.x, bb.Max.y), shade);
    dl->AddLine(ImVec2(bb.Max.x - 1, bb.Min.y), ImVec2(bb.Max.x - 1, bb.Max.y), hlite);
    dl->AddLine(ImVec2(bb.Min.x, bb.Max.y - 1), ImVec2(bb.Max.x, bb.Max.y - 1), hlite);

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

    {
        float minX = bb.Min.x + (bb.Max.x - bb.Min.x) * bp->slider.clipminF;
        float maxX = bb.Min.x + (bb.Max.x - bb.Min.x) * bp->slider.clipmaxF;
        if (maxX > minX)
            dl->AddRectFilled(ImVec2(minX, bb.Min.y), ImVec2(maxX, bb.Max.y), IM_COL32(0, 80, 200, 40));
    }

    float grabHalf = ctrlH * 0.25f;

    {
        float grabX = bb.Min.x + (bb.Max.x - bb.Min.x) * bp->slider.clipminF;
        ImRect gr(ImVec2(grabX - grabHalf, bb.Min.y),
                  ImVec2(grabX + grabHalf, bb.Max.y));
        dl->AddRectFilled(gr.Min, gr.Max, IM_COL32(60, 60, 60, 255));
        dl->AddRect(gr.Min, gr.Max, IM_COL32(30, 30, 30, 200));
    }

    {
        float grabX = bb.Min.x + (bb.Max.x - bb.Min.x) * bp->slider.clipmaxF;
        ImRect gr(ImVec2(grabX - grabHalf, bb.Min.y),
                  ImVec2(grabX + grabHalf, bb.Max.y));
        dl->AddRectFilled(gr.Min, gr.Max, IM_COL32(255, 255, 255, 255));
        dl->AddRect(gr.Min, gr.Max, IM_COL32(80, 80, 80, 200));
    }

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

    ImGui::PopID();
}

/* ── DrawSliderVertical (vertical slider for gizmo) ──────────────────── */

#define GIZMO_CTRL_SZ 24

void DrawSliderVertical(ImDrawList* dl, BParam* bp, int x, int y, int w, int h, float val, int colorMode) {
    DualSlider* ds = &bp->slider;
    int y0 = y, y1 = y + h;
    for (int yy = y0; yy < y1; yy++) {
        float t = (float)(y1 - 1 - yy) / (float)(y1 - y0 - 1);
        ImU32 col;
        if (colorMode >= 0) {
            Color c;
            if (colorMode == 0) c = HSLToRGB(t, 1.0f, 0.5f);
            else if (colorMode == 1) c = HSLToRGB(colorHue, t, colorLit);
            else c = HSLToRGB(colorHue, colorSat, t);
            col = IM_COL32(c.r, c.g, c.b, 255);
        } else {
            uint8_t r = (uint8_t)(ds->gradStart.r * (1 - t) + ds->gradEnd.r * t);
            uint8_t g = (uint8_t)(ds->gradStart.g * (1 - t) + ds->gradEnd.g * t);
            uint8_t b = (uint8_t)(ds->gradStart.b * (1 - t) + ds->gradEnd.b * t);
            col = IM_COL32(r, g, b, 255);
        }
        dl->AddRectFilled(ImVec2(x, yy), ImVec2(x + w, yy + 1), col);
    }
    dl->AddRect(ImVec2(x, y), ImVec2(x + w, y + h), IM_COL32(180, 180, 200, 180));
    float grabY = y + (1.0f - val) * h;
    float grabHalf = GIZMO_CTRL_SZ * 0.25f;
    dl->AddRectFilled(ImVec2(x + 1, grabY - grabHalf), ImVec2(x + w - 1, grabY + grabHalf), IM_COL32_WHITE);
    dl->AddRect(ImVec2(x + 1, grabY - grabHalf), ImVec2(x + w - 1, grabY + grabHalf), IM_COL32(50, 50, 50, 200));
    char txt[16];
    float disp = BParam_GetValue(bp);
    if (bp->outMax - bp->outMin >= 1.0f) snprintf(txt, sizeof(txt), "%.1f", disp);
    else snprintf(txt, sizeof(txt), "%.2f", disp);
    ImVec2 tsz = ImGui::CalcTextSize(txt);
    dl->AddText(ImVec2(x + (w - tsz.x) / 2, grabY - tsz.y / 2), IM_COL32(255, 255, 255, 220), txt);
}
