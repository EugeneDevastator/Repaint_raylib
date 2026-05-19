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
                                    : (float)k / fmaxf(length-1,1);
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
                IM_COL32(gradEnd.r, gradEnd.g, gradEnd.b, 255),
                IM_COL32(gradEnd.r, gradEnd.g, gradEnd.b, 255),
                IM_COL32(gradStart.r, gradStart.g, gradStart.b, 255),
                IM_COL32(gradStart.r, gradStart.g, gradStart.b, 255));
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
    // temporary disabled
    if (false && clipmaxF > clipminF + 0.001f) {
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

    // ── Value text (centered in slider, before grabbers) ─────────────
    if (bp) {
        float disp = BParam_GetValue(bp);
        char txt[32];
        if (bp == &bpSizeMul) {
            float remap = disp / 128.0f - 1.0f;
            snprintf(txt, sizeof(txt), "%.2f", remap);
        } else if (bp->outMax - bp->outMin >= 1.0f)
            snprintf(txt, sizeof(txt), "%.1f", disp);
        else
            snprintf(txt, sizeof(txt), "%.2f", disp);
        ImVec2 sz = ImGui::CalcTextSize(txt);
        int tx, ty;
        if (orient == 0) {
            tx = x + (length - (int)sz.x) / 2;
            ty = y + (thickness - (int)sz.y) / 2;
        } else {
            tx = x + (thickness - (int)sz.x) / 2;
            ty = y + (length - (int)sz.y) / 2;
        }
        dl->AddText(ImVec2(tx, ty), IM_COL32(255, 255, 255, 255), txt);
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
        int fa = (colorMode >= 0) ? 140 : 255;
        dl->AddRectFilled(ImVec2(gx, gy), ImVec2(gx + gw, gy + gh), fill & 0x00FFFFFF | (fa << 24));
        dl->AddRectFilled(ImVec2(gx, gy), ImVec2(gx + gw, gy + 1), shade);
        dl->AddRectFilled(ImVec2(gx, gy), ImVec2(gx + 1, gy + gh), shade);
        dl->AddRectFilled(ImVec2(gx + gw - 1, gy + 1), ImVec2(gx + gw, gy + gh), hlite);
        dl->AddRectFilled(ImVec2(gx + 1, gy + gh - 1), ImVec2(gx + gw, gy + gh), hlite);
    };

    drawGrabber(clipminF, IM_COL32(60, 60, 60, 255), IM_COL32(40, 40, 40, 255), IM_COL32(130, 130, 130, 255));
    drawGrabber(clipmaxF, IM_COL32(255, 255, 255, 255), IM_COL32(200, 200, 200, 255), IM_COL32(80, 80, 80, 255));
}

/* ── PenMode popup helper (shared by both orientations) ────────────────── */

static void PenModePopup(BParam* bp, const char* popupName) {
    if (ImGui::BeginPopup(popupName, ImGuiWindowFlags_NoScrollbar)) {
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

/* ── PenMode icon button (shared by both orientations) ─────────────────── */

static void PenModeButton(BParam* bp, float btnW, float btnH, const char* popupName) {
    Texture2D pt = GetPenModeIcon(bp->penMode);
    ImTextureID penTid = (pt.id > 0) ? (ImTextureID)(intptr_t)pt.id : 0;
    ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.85f, 0.85f, 0.85f, 1.0f));
    ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.75f, 0.75f, 0.75f, 1.0f));
    ImGui::PushStyleColor(ImGuiCol_ButtonActive, ImVec4(0.65f, 0.65f, 0.65f, 1.0f));
    ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(0, 0));
    if (penTid) {
        if (ImGui::ImageButton("##pm", penTid, ImVec2(btnW, btnH)))
            ImGui::OpenPopup(popupName);
    } else {
        if (ImGui::Button("...", ImVec2(btnW, btnH)))
            ImGui::OpenPopup(popupName);
    }
    ImGui::PopStyleColor(3);
    ImGui::PopStyleVar();
}

/* ── Slider interaction helper — draws 3 separate InvisibleButtons ───────
 *   Left→clipmaxF, Right→clipminF, Middle→jitter (never cross-talk).
 */
static void SliderInteraction(const ImRect& bb, int orient, BParam* bp) {
    ImGui::SetCursorScreenPos(bb.Min);
    ImGui::InvisibleButton("##sb", bb.GetSize(),
        ImGuiButtonFlags_MouseButtonLeft | ImGuiButtonFlags_MouseButtonRight | ImGuiButtonFlags_MouseButtonMiddle);
    if (ImGui::IsItemActive()) {
        float v;
        if (orient == 0)
            v = (ImGui::GetMousePos().x - bb.Min.x) / (bb.Max.x - bb.Min.x);
        else
            v = 1.0f - (ImGui::GetMousePos().y - bb.Min.y) / (bb.Max.y - bb.Min.y);
        v = fminf(fmaxf(v, 0.0f), 1.0f);
        if (ImGui::IsMouseDown(0))
            bp->user.clipmaxF = v;
        else if (ImGui::IsMouseDown(1))
            bp->user.clipminF = v;
        else if (ImGui::IsMouseDown(2))
            bp->user.jitter = v;
    }
    if (ImGui::IsItemHovered() && bp->tooltip[0])
        ImGui::SetTooltip("%s", bp->tooltip);
    ImGui::SetCursorScreenPos(ImVec2(bb.Max.x, bb.Min.y));
}

/* ── DrawSlider — horizontal full widget ─────────────────────────────────
 *   orient=0: [icon] [==slider==] [penMode▼]
 *   orient=1: slider bar only (caller positions cursor for pixel-perfect layout).
 *            Pass thick (bar width) and len (bar height) for orient=1.
 *   3 separate InvisibleButtons guarantee left→clipmaxF, right→clipminF,
 *   middle→jitter never cross-talk.
 */

void DrawSlider(BParam* bp, int orient, float thick, float len) {
    ImGui::PushID(bp->id);

    if (orient == 0) {
        float ctrlH = 28.0f, spacing = 4.0f;

        if (bp->iconLoaded)
            ImGui::Image((ImTextureID)(intptr_t)bp->iconTex.id, ImVec2(ctrlH, ctrlH));
        else
            ImGui::Dummy(ImVec2(ctrlH, ctrlH));
        ImGui::SameLine(0, spacing);

        float avail = ImGui::GetContentRegionAvail().x;
        float btnW = ctrlH;
        float sliderW = avail > btnW + spacing + 10.0f ? avail - btnW - spacing : 10.0f;

        ImVec2 sPos = ImGui::GetCursorScreenPos();
        ImRect sBB(sPos, ImVec2(sPos.x + sliderW, sPos.y + ctrlH));

        DrawSliderCore(ImGui::GetWindowDrawList(), (int)sPos.x, (int)sPos.y, (int)sliderW, (int)ctrlH, 0,
            bp->user.clipminF, bp->user.clipmaxF, bp->user.jitter,
            bp->slider.gradStart, bp->slider.gradEnd, bp->slider.colorMode, bp);
        SliderInteraction(sBB, 0, bp);

        ImGui::SameLine(0, spacing);
        char pname[32]; snprintf(pname, sizeof(pname), "hpen_%d", bp->id);
        PenModeButton(bp, btnW, ctrlH, pname);
        PenModePopup(bp, pname);

    } else {
        // orient=1 — slider bar only, caller positions cursor
        float sw = thick > 0 ? thick : fminf(ImGui::GetContentRegionAvail().x, 40.0f);
        float sh = len  > 0 ? len  : 60.0f;

        ImVec2 sPos = ImGui::GetCursorScreenPos();
        ImRect sBB(sPos, ImVec2(sPos.x + sw, sPos.y + sh));

        DrawSliderCore(ImGui::GetWindowDrawList(), (int)sPos.x, (int)sPos.y, (int)sh, (int)sw, 1,
            bp->user.clipminF, bp->user.clipmaxF, bp->user.jitter,
            bp->slider.gradStart, bp->slider.gradEnd, bp->slider.colorMode, bp);
        SliderInteraction(sBB, 1, bp);
    }

    ImGui::PopID();
}