#include "repaint.h"
#include "imgui.h"
#include "imgui_internal.h"
#include <math.h>

// ── Visual constants ─────────────────────────────────────────────────
static const ImU32 SLIDER_BORDER_COL  = IM_COL32(150, 150, 150, 200);
static const ImU32 SLIDER_JITTER_COL  = IM_COL32(80,  120, 240, 180);
static const ImU32 SLIDER_TEXT_COL    = IM_COL32(0,   0,   0,   255);
static const ImU32 SLIDER_TICK_LIGHT  = IM_COL32(255, 255, 255, 190);
static const ImU32 SLIDER_TICK_DARK   = IM_COL32(120,  120,  120,  190);

static const Color SLIDER_BG_COL      = {210, 210, 210, 255};
static const Color SLIDER_GRAD_FROM   = {150, 150, 150, 255};
static const Color SLIDER_GRAD_TO     = {235, 235, 235, 255};
static const float SLIDER_ROUNDING    = 3.0f;
static const bool SLIDER_IS_ALT_PRECISE_MODE = true;  // toggle for 10% snap + extended rect
/*
*and for the next task - look at the bparam slider. it has alternative mode which is currently off.
i want to refactor it a bit -
new idea is - when we move cursor in slider area it behaves as usual, no snapping no nothing.
when cursor was captured initiall and is outside of initial rect - we change behavior.
if it is to the Up (or left for vertical slider) - we do the snapping to each 10%
if it is to the right - we do this precise mode - scanning between diagonals, - the further away from slider - the more precision.
maybe.. hm maybe lets make not direct diagonals but halves. for ex
horizontal slider. its x bounds are x0 and x1
mosue distance to lower bound is ym.
right diagonal point R = x1+ym/2
left diagonal is L = x0-ym/2
and value is calculated from point between those two, normalized.
and after that enable this mode.*/
/* ── Core slider renderer (procedural — works for both H and V) ──────
 *   length  = size along the slide axis (width for H, height for V)
 *   thickness = size perpendicular to the slide axis (height for H, width for V)
 *   orient  = 0 → horizontal, 1 → vertical
 */
static void DrawSliderCore(ImDrawList* dl, int x, int y, int length, int thickness,
    int orient, float clipminF, float clipmaxF, float jitter,
    Color gradStart, Color gradEnd, int colorMode, BParam* bp)
{
    int sliderrad = (int)(thickness * 0.125f);
    if (sliderrad < 2) sliderrad = 2;

    // ── Outer rounded rect with light fill and 2px dark border ─────────
    int padX = (orient == 0) ? sliderrad + 1 : 1;
    int padY = (orient == 0) ? 1 : sliderrad + 1;
    float width  = (orient == 0) ? (float)length : (float)thickness;
    float height = (orient == 0) ? (float)thickness : (float)length;
    ImVec2 oMin(x - padX, y - padY);
    ImVec2 oMax(x + width + padX, y + height + padY);
    dl->AddRectFilled(oMin, oMax, IM_COL32(SLIDER_BG_COL.r, SLIDER_BG_COL.g, SLIDER_BG_COL.b, SLIDER_BG_COL.a), SLIDER_ROUNDING);

    // ── Gradient background (fills full outer area) ───────────────────
    dl->PushClipRect(oMin, oMax, true);
    if (colorMode >= 0) {
        int iterLen = (int)((orient == 0) ? (oMax.x - oMin.x) : (oMax.y - oMin.y));
        for (int k = 0; k < iterLen; k++) {
            float t = (orient == 0) ? (float)k / fmaxf(iterLen-1,1)
                                    : (float)(iterLen-1-k) / fmaxf(iterLen-1,1);
            Color c;
            if (colorMode == 0) c = HSLToRGB(t, colorSat, colorLit);
            else if (colorMode == 1) c = HSLToRGB(colorHue, t, colorLit);
            else c = HSLToRGB(colorHue, colorSat, t);
            uint32_t col = IM_COL32(c.r, c.g, c.b, 255);
            if (orient == 0)
                dl->AddRectFilled(ImVec2(oMin.x + k, oMin.y), ImVec2(oMin.x + k + 1, oMax.y), col);
            else
                dl->AddRectFilled(ImVec2(oMin.x, oMin.y + k), ImVec2(oMax.x, oMin.y + k + 1), col);
        }
    } else {
        bool noMod = (bp && bp->penMode == csNone);
        ImU32 gs = IM_COL32(SLIDER_GRAD_FROM.r, SLIDER_GRAD_FROM.g, SLIDER_GRAD_FROM.b, 255);
        ImU32 ge = IM_COL32(SLIDER_GRAD_TO.r,   SLIDER_GRAD_TO.g,   SLIDER_GRAD_TO.b,   255);
        if (noMod) {
            // No modulation: gradient from 0 to clipmaxF (white tick), no clipminF tick
            if (orient == 0) {
                int x1 = x + (int)(length * clipmaxF);
                if (x1 > x)
                    dl->AddRectFilledMultiColor(ImVec2(x, y), ImVec2(x1, y + thickness), gs, ge, ge, gs);
            } else {
                int y1 = y + (int)(length * (1.0f - clipmaxF));
                if (y1 < y + length)
                    dl->AddRectFilledMultiColor(ImVec2(x, y1), ImVec2(x + thickness, y + length), ge, ge, gs, gs);
            }
        } else {
            // Modulation: gradient between the two tick positions
            if (orient == 0) {
                int x0 = x + (int)(length * clipminF);
                int x1 = x + (int)(length * clipmaxF);
                if (x1 > x0)
                    dl->AddRectFilledMultiColor(ImVec2(x0, y), ImVec2(x1, y + thickness), gs, ge, ge, gs);
                else if (x0 > x1)
                    dl->AddRectFilledMultiColor(ImVec2(x1, y), ImVec2(x0, y + thickness), ge, gs, gs, ge);
            } else {
                int y0 = y + (int)(length * (1.0f - clipmaxF));
                int y1 = y + (int)(length * (1.0f - clipminF));
                if (y1 > y0)
                    dl->AddRectFilledMultiColor(ImVec2(x, y0), ImVec2(x + thickness, y1), ge, ge, gs, gs);
                else if (y0 > y1)
                    dl->AddRectFilledMultiColor(ImVec2(x, y1), ImVec2(x + thickness, y0), gs, gs, ge, ge);
            }
        }
    }
    dl->PopClipRect();

    // 2px dark border (two 1px strokes, outer expanded to avoid tick overlap)
    ImVec2 bMin(oMin.x - 1, oMin.y - 1);
    ImVec2 bMax(oMax.x + 1, oMax.y + 1);
    dl->AddRect(bMin, bMax, SLIDER_BORDER_COL, SLIDER_ROUNDING + 1.0f, ImDrawFlags_RoundCornersAll, 1.0f);
    dl->AddRect(oMin, oMax, SLIDER_BORDER_COL, SLIDER_ROUNDING, ImDrawFlags_RoundCornersAll, 1.0f);

    // ── Jitter bar (blue at top/left when jitter > 0) ────────────────
    if (jitter > 0.001f) {
        uint32_t jCol = SLIDER_JITTER_COL;
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
            snprintf(txt, sizeof(txt), "%.2f", disp);
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
        dl->AddText(ImVec2(tx, ty), SLIDER_TEXT_COL, txt);
    }


    auto drawGrabber = [&](float clipPos, uint32_t fill, uint32_t shade, uint32_t hlite) {
        float pos = orient == 0 ? clipPos : (1.0f - clipPos);
        int gx, gy, gw, gh;
        if (orient == 0) {
            gx = x + (int)(length * pos) - sliderrad;
            gy = y;
            gw = sliderrad * 2;
            gh = thickness;
        } else {
            gx = x + 1;
            gy = y + (int)(length * pos) - sliderrad;
            gw = thickness - 2;
            gh = sliderrad * 2;
        }
        // Kill gradient bleed: opaque background fill first
        // dl->AddRectFilled(ImVec2(gx, gy), ImVec2(gx + gw, gy + gh), SLIDER_BG_FILL);
        // Then draw rounded grabber on top
        int fa = (colorMode >= 0) ? 140 : (int)((fill >> 24) & 0xFF);
        float rnd = fminf(fminf(gw, gh) * 0.5f, 3.0f);
        dl->Flags &= ~ImDrawListFlags_AntiAliasedFill;
        dl->AddRectFilled(ImVec2(gx, gy), ImVec2(gx + gw, gy + gh),
            fill, 3, ImDrawFlags_RoundCornersAll);
        dl->Flags |= ImDrawListFlags_AntiAliasedFill;
    };

    dl->PushClipRect(oMin, oMax, true);
    if (!(bp && bp->penMode == csNone))
        drawGrabber(clipminF, SLIDER_TICK_DARK, 0, 0);
    drawGrabber(clipmaxF, SLIDER_TICK_LIGHT, 0, 0);
    dl->PopClipRect();
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
        ImVec2 mp = ImGui::GetMousePos();
        float v;
        float mside  = (orient == 0) ? mp.x  : mp.y;
        float mperp  = (orient == 0) ? mp.y  : mp.x;
        float bbMinS = (orient == 0) ? bb.Min.x : bb.Min.y;
        float bbMaxS = (orient == 0) ? bb.Max.x : bb.Max.y;
        float bbMinP = (orient == 0) ? bb.Min.y : bb.Min.x;
        float bbMaxP = (orient == 0) ? bb.Max.y : bb.Max.x;
        bool  flip   = (orient != 0);

        float bs = bbMaxS - bbMinS;

        if (SLIDER_IS_ALT_PRECISE_MODE) {
            bool inside2d = (mside >= bbMinS && mside <= bbMaxS &&
                             mperp >= bbMinP && mperp <= bbMaxP);
            if (inside2d) {
                v = (mside - bbMinS) / bs;
            } else if (mperp < bbMinP) {
                // snap side: expand section upward, then snap to 10%
                float dist = bbMinP - mperp;
                float extA = bbMinS - dist * 0.5f;
                float extB = bbMaxS + dist * 0.5f;
                v = roundf((mside - extA) / (extB - extA) * 10.0f) / 10.0f;
            } else {
                // precise side: expand section downward
                float dist = mperp - bbMaxP;
                float extA = bbMinS - dist * 0.5f;
                float extB = bbMaxS + dist * 0.5f;
                v = (mside - extA) / (extB - extA);
            }
        } else {
            v = (mside - bbMinS) / bs;
        }

        if (flip) v = 1.0f - v;



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
        float ctrlH = 37.0f, spacing = 6.0f;

        if (bp->iconLoaded)
            ImGui::Image((ImTextureID)(intptr_t)bp->iconTex.id, ImVec2(ctrlH, ctrlH));
        else
            ImGui::Dummy(ImVec2(ctrlH, ctrlH));
        ImGui::SameLine(0, spacing);

        float avail = ImGui::GetContentRegionAvail().x;
        float btnW = ctrlH + 2.0f;
        float sliderW = avail > btnW + spacing + 10.0f ? avail - btnW - spacing : 10.0f;

        ImVec2 sPos = ImGui::GetCursorScreenPos();
        ImRect sBB(sPos, ImVec2(sPos.x + sliderW, sPos.y + ctrlH));

        DrawSliderCore(ImGui::GetWindowDrawList(), (int)sPos.x, (int)sPos.y, (int)sliderW, (int)ctrlH, 0,
            bp->user.clipminF, bp->user.clipmaxF, bp->user.jitter,
            bp->slider.gradStart, bp->slider.gradEnd, bp->slider.colorMode, bp);
        SliderInteraction(sBB, 0, bp);

        ImGui::SameLine(0, spacing);
        ImGui::SetCursorPosY(ImGui::GetCursorPosY() - 1.0f);
        {
            Texture2D pt = GetPenModeIcon(bp->penMode);
            ImTextureID penTid = (pt.id > 0) ? (ImTextureID)(intptr_t)pt.id : 0;
            ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.85f, 0.85f, 0.85f, 1.0f));
            ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.75f, 0.75f, 0.75f, 1.0f));
            ImGui::PushStyleColor(ImGuiCol_ButtonActive, ImVec4(0.65f, 0.65f, 0.65f, 1.0f));
            ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(0, 0));
            char bname[32]; snprintf(bname, sizeof(bname), "hpm_%d", bp->id);
            if (penTid)
                ImGui::ImageButton(bname, penTid, ImVec2(btnW, ctrlH + 2.0f));
            else
                ImGui::Button("...", ImVec2(btnW, ctrlH + 2.0f));
            ImGui::PopStyleColor(3);
            ImGui::PopStyleVar();
            static int activePenId = -1;
            if (ImGui::IsItemHovered() && ImGui::IsMouseDown(0))
                activePenId = bp->id;
            if (activePenId == bp->id) {
                ImVec2 btnMin = ImGui::GetItemRectMin();
                float ow = 170.0f;
                ImGui::SetNextWindowPos(ImVec2(btnMin.x, btnMin.y + btnW + 2));
                ImGui::SetNextWindowSize(ImVec2(ow, 0));
                ImGui::PushStyleColor(ImGuiCol_WindowBg, ImVec4(1, 1, 1, 1));
                ImGui::Begin("##penOverlay", NULL,
                    ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize |
                    ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoScrollbar |
                    ImGuiWindowFlags_NoSavedSettings);
                bool mouseDown = ImGui::IsMouseDown(0);
                for (int p = 0; p < PEN_MODE_COUNT; p++) {
                    ImGui::PushID(p);
                    if (penModeTex[p].id > 0) {
                        ImGui::Image((ImTextureID)(intptr_t)penModeTex[p].id, ImVec2(16, 16));
                        ImGui::SameLine();
                    }
                    ImGui::Selectable(PenModeNames[p], bp->penMode == p);
                    if (mouseDown && ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenBlockedByActiveItem))
                        bp->penMode = p;
                    ImGui::PopID();
                }
                ImGui::End();
                ImGui::PopStyleColor();
                if (!ImGui::IsMouseDown(0))
                    activePenId = -1;
            }
        }

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