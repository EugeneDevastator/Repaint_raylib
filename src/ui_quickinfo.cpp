#include "repaint.h"
#include "imgui.h"
#include <math.h>
#include <stdio.h>

void QuickInfo_Draw(ImDrawList* dl, int gcx, int gcy, int gizR, AppState* state) {
    // ── Info text ───────────────────────────────────────────────────────
    char buf[96];
    float hardness = 1.0f - state->currentBrush.Realb.rad_in / fmaxf(1.0f, state->currentBrush.Realb.rad_out);
    snprintf(buf, sizeof(buf), "Sz:%.0f Hd:%.0f%% Op:%.0f%% Crv:%.0f%% Sp:%.2f Sc:%.2f",
        state->currentBrush.Realb.rad_out, hardness * 100,
        state->currentBrush.Realb.opacity * 100,
        state->currentBrush.Realb.crv * 100,
        BParam_GetValue(&bpSpacing), BParam_GetValue(&bpScatter));
    ImVec2 tsz = ImGui::CalcTextSize(buf);
    dl->AddText(ImVec2(gcx - tsz.x / 2, gcy + gizR + 8), IM_COL32(211, 211, 211, 230), buf);

    // ── Color swatch ────────────────────────────────────────────────────
    int swY = gcy + gizR + 14;
    Color curCol = HSLToRGB(colorHue, colorSat, colorLit);
    dl->AddRectFilled(ImVec2(gcx - 50, swY), ImVec2(gcx + 50, swY + 24), IM_COL32(curCol.r, curCol.g, curCol.b, 255));
    dl->AddRect(ImVec2(gcx - 50, swY), ImVec2(gcx + 50, swY + 24), IM_COL32_WHITE);
}
