#include "repaint.h"
#include "imgui.h"
#include "imgui_internal.h"
#include <math.h>

/* ── Core slider renderer (procedural — works for both H and V) ──────
 *   length  = size along the slide axis (width for H, height for V)
 *   thickness = size perpendicular to the slide axis (height for H, width for V)
 *   orient  = 0 → horizontal, 1 → vertical
 */
static void DrawSliderCore(ImDrawList* dl, int x, int y, int length, int thickness,
    int orient, float clipminF, float clipmaxF, float jitter,
    Color gradStart, Color gradEnd, int colorMode, BParam* bp)
{
    // ── Gradient background (fills entire thickness) ──────────────────
    if (colorMode >= 0) {
        for (int k = 0; k < length; k++) {
            float t = (orient == 0) ? (float)k / fmaxf(length-1,1)
                                    : (float)(length-1-k) / fmaxf(length-1,1);
            Color c;
            if (colorMode == 0) c = HSLToRGB(t, 1.0f, 0.5f);
            else if (colorMode == 1) c = HSLToRGB(colorHue, t, colorLit);
            else c = HSLToRGB(colorHue, colorSat, t);
            uint32_t col = IM_COL32(c.r, c.g, c.b, 255);
            if (orient == 0)
                dl->AddRectFilled(ImVec2(x + k, y), ImVec2(x + k + 1, y + thickness), col);
            else
                dl->AddRectFilled(ImVec2(x, y + k), ImVec2(x + thickness, y + k + 1), col);
        }
    } else {
        if (orient == 0) {
            dl->AddRectFilledMultiColor(ImVec2(x, y), ImVec2(x + length, y + thickness),
                IM_COL32(gradStart.r, gradStart.g, gradStart.b, 255),
                IM_COL32(gradEnd.r, gradEnd.g, gradEnd.b, 255),
                IM_COL32(gradEnd.r, gradEnd.g, gradEnd.b, 255),
                IM_COL32(gradStart.r, gradStart.g, gradStart.b, 255));
        } else {
            dl->AddRectFilledMultiColor(ImVec2(x, y), ImVec2(x + thickness, y + length),
                IM_COL32(gradStart.r, gradStart.g, gradStart.b, 255),
                IM_COL32(gradStart.r, gradStart.g, gradStart.b, 255),
                IM_COL32(gradEnd.r, gradEnd.g, gradEnd.b, 255),
                IM_COL32(gradEnd.r, gradEnd.g, gradEnd.b, 255));
        }
    }

    // ── Jitter bar (blue at top/left when jitter > 0) ────────────────
    if (jitter > 0.001f) {
        uint32_t jCol = IM_COL32(80, 120, 240, 180);
        if (orient == 0)
            dl->AddRectFilled(ImVec2(x, y), ImVec2(x + (int)(length * jitter), y + 7), jCol);
        else
            dl->AddRectFilled(ImVec2(x, y + length - (int)(length * jitter)), ImVec2(x + 7, y + length), jCol);
    }

    // ── Selection range highlight ────────────────────────────────────
    if (clipmaxF > clipminF + 0.001f) {
        uint32_t selCol = IM_COL32(0, 80, 200, 30);
        if (orient == 0) {
            int l = x + (int)(length * clipminF);
            int r = x + (int)(length * clipmaxF);
            dl->AddRectFilled(ImVec2(l, y), ImVec2(r, y + thickness), selCol);
        } else {
            int b = y + length - (int)(length * clipminF);
            int t_ = y + length - (int)(length * clipmaxF);
            dl->AddRectFilled(ImVec2(x, t_), ImVec2(x + thickness, b), selCol);
        }
    }

    // ── Grabber "tick marks" (thin bar matching Qt, with 3D frame) ──
    int sliderrad = (int)(thickness * 0.125f);
    if (sliderrad < 2) sliderrad = 2;

    auto drawGrabber = [&](float clipPos, uint32_t fill, uint32_t shade, uint32_t hlite) {
        float pos = orient == 0 ? clipPos : (1.0f - clipPos);
        int gx, gy, gw, gh;
        if (orient == 0) {
            gx = x + (int)(length * pos) - sliderrad;
            gy = y + 1;
            gw = sliderrad * 2;
            gh = thickness - 2;
        } else {
            gx = x + 1;
            gy = y + (int)(length * pos) - sliderrad;
            gw = thickness - 2;
            gh = sliderrad * 2;
        }
        dl->AddRectFilled(ImVec2(gx, gy), ImVec2(gx + gw, gy + gh), fill);
        dl->AddRectFilled(ImVec2(gx, gy), ImVec2(gx + gw, gy + 1), shade);
        dl->AddRectFilled(ImVec2(gx, gy), ImVec2(gx + 1, gy + gh), shade);
        dl->AddRectFilled(ImVec2(gx + gw - 1, gy + 1), ImVec2(gx + gw, gy + gh), hlite);
        dl->AddRectFilled(ImVec2(gx + 1, gy + gh - 1), ImVec2(gx + gw, gy + gh), hlite);
    };

    drawGrabber(clipminF, IM_COL32(60, 60, 60, 255), IM_COL32(40, 40, 40, 255), IM_COL32(130, 130, 130, 255));
    drawGrabber(clipmaxF, IM_COL32(255, 255, 255, 255), IM_COL32(200, 200, 200, 255), IM_COL32(80, 80, 80, 255));

    // ── Border frame ──────────────────────────────────────────────────
    dl->AddRect(ImVec2(x, y), ImVec2(x + length, y + thickness), IM_COL32(180, 180, 200, 180));

    // ── Value text ────────────────────────────────────────────────────
    if (bp) {
        float disp = BParam_GetValue(bp);
        char txt[32];
        if (bp->outMax - bp->outMin >= 1.0f)
            snprintf(txt, sizeof(txt), "%.1f", disp);
        else
            snprintf(txt, sizeof(txt), "%.2f", disp);
        ImVec2 sz = ImGui::CalcTextSize(txt);
        int tx = orient == 0
            ? x + (int)(length * clipmaxF) - (int)sz.x / 2
            : x + (thickness - (int)sz.x) / 2;
        int ty = orient == 0
            ? y + (thickness - (int)sz.y) / 2
            : y + (int)(length * (1.0f - clipmaxF)) - (int)sz.y / 2;
        tx = fmaxf(x + 2, fminf(tx, x + length - (int)sz.x - 2));
        ty = fmaxf(y + 2, fminf(ty, y + thickness - (int)sz.y - 2));
        dl->AddText(ImVec2(tx, ty), IM_COL32(255, 255, 255, 220), txt);
    }
}

/* ── DrawBParamSlider (horizontal — full widget) ──────────────────────── */

void DrawBParamSlider(BParam* bp) {
    float ctrlH = 28.0f;
    float spacing = 4.0f;
    int orient = 0;

    ImGui::PushID(bp->id);

    // ── Icon ──────────────────────────────────────────────────────────
    if (bp->iconLoaded)
        ImGui::Image((ImTextureID)(intptr_t)bp->iconTex.id, ImVec2(ctrlH, ctrlH));
    else
        ImGui::Dummy(ImVec2(ctrlH, ctrlH));

    ImGui::SameLine(0, spacing);

    // ── Slider (expanding) ────────────────────────────────────────────
    float avail = ImGui::GetContentRegionAvail().x;
    float btnW = ctrlH;
    float sliderW = avail - btnW - spacing;
    if (sliderW < 10.0f) sliderW = 10.0f;

    ImDrawList* dl = ImGui::GetWindowDrawList();
    ImVec2 pos = ImGui::GetCursorScreenPos();

    DrawSliderCore(dl, (int)pos.x, (int)pos.y, (int)sliderW, (int)ctrlH,
        orient,
        bp->slider.clipminF, bp->slider.clipmaxF, bp->slider.jitter,
        bp->slider.gradStart, bp->slider.gradEnd, -1, bp);

    ImRect bb(pos, ImVec2(pos.x + sliderW, pos.y + ctrlH));

    // ── Slider mouse interaction (local per-button tracking via ImGui item state) ─
    ImGui::InvisibleButton("##sl", bb.GetSize(),
        ImGuiButtonFlags_MouseButtonLeft | ImGuiButtonFlags_MouseButtonRight | ImGuiButtonFlags_MouseButtonMiddle);
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

    ImGui::SameLine(0, spacing);

    // ── Pen mode button (right of slider) ─────────────────────────────
    {
        Texture2D pt = GetPenModeIcon(bp->penMode);
        ImTextureID penTid = (pt.id > 0) ? (ImTextureID)(intptr_t)pt.id : 0;

        ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.85f, 0.85f, 0.85f, 1.0f));
        ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.75f, 0.75f, 0.75f, 1.0f));
        ImGui::PushStyleColor(ImGuiCol_ButtonActive, ImVec4(0.65f, 0.65f, 0.65f, 1.0f));

        char pname[32];
        snprintf(pname, sizeof(pname), "penpop_%d", bp->id);

        if (penTid) {
            if (ImGui::ImageButton("##pm", penTid, ImVec2(btnW, ctrlH)))
                ImGui::OpenPopup(pname);
        } else {
            if (ImGui::Button("...", ImVec2(btnW, ctrlH)))
                ImGui::OpenPopup(pname);
        }
        ImGui::PopStyleColor(3);

        if (ImGui::BeginPopup(pname)) {
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

/* ── DrawSliderVertical (vertical slider for quick panel) ────────────── */

void DrawSliderVertical(ImDrawList* dl, BParam* bp, int x, int y, int w, int h,
    float val, int colorMode)
{
    // For orient=1: length = h (vertical extent), thickness = w (horizontal extent)
    DrawSliderCore(dl, x, y, h, w, 1,
        bp->slider.clipminF, bp->slider.clipmaxF, bp->slider.jitter,
        bp->slider.gradStart, bp->slider.gradEnd, colorMode, bp);
}