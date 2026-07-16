#include "repaint.h"
#include "rlgl.h"
#include "external/glad.h"
#include "input_modulator.h"
#include <math.h>

void PaintHudModule::DrawGL(const DrawRect& rect) {
    // Skip during color picking — XOR overlay corrupts screen readback
    if (g_activeHud != HUD_NONE || g_colorPicking) return;
    Vector2 mp = GetMousePosition();
    float radPx = state->currentBrush.Realb.rad_out * WORLD_UNIT_PX * state->camera.zoom;

    // ── Line tool XOR preview ──────────────────────────────────────
    if (state->mode == ePolyStripe && viewport.wasMouseDown) {
        Vector2 startScr = GetWorldToScreen2D(viewport.lineStartPos, state->camera);
        Vector2 dir = {mp.x - startScr.x, mp.y - startScr.y};
        float len = sqrtf(dir.x*dir.x + dir.y*dir.y);
        rlDrawRenderBatchActive();
        glEnable(GL_COLOR_LOGIC_OP);
        glLogicOp(GL_XOR);
        // Center line
        DrawLineEx(startScr, mp, 2.0f, WHITE);
        // Thickness guide lines (offset perpendicular by brush radius + scatter)
        if (radPx > 2.0f && len > 1.0f) {
            float scatterFactor = BParam_GetValue(&bpScatter);
            float thickPx = radPx * (1.0f + scatterFactor);
            Vector2 perp = { -dir.y / len * thickPx, dir.x / len * thickPx };
            DrawLineEx({startScr.x+perp.x, startScr.y+perp.y},
                       {mp.x+perp.x, mp.y+perp.y}, 1.0f, WHITE);
            DrawLineEx({startScr.x-perp.x, startScr.y-perp.y},
                       {mp.x-perp.x, mp.y-perp.y}, 1.0f, WHITE);
        }
        rlDrawRenderBatchActive();
        glDisable(GL_COLOR_LOGIC_OP);
    }

    if (state->mode != eBrush && state->mode != eSmudge && state->mode != ePolyStripe) return;

    // ── XOR overlay ────────────────────────────────────────────────
    rlDrawRenderBatchActive();
    glEnable(GL_COLOR_LOGIC_OP);
    glLogicOp(GL_XOR);

    // Crosshair at cursor position, 1px
    float crossLen = 20.0f;
    DrawLineEx(Vector2{mp.x - crossLen, mp.y}, Vector2{mp.x + crossLen, mp.y}, 1.0f, WHITE);
    DrawLineEx(Vector2{mp.x, mp.y - crossLen}, Vector2{mp.x, mp.y + crossLen}, 1.0f, WHITE);

    // Brush radius ring at cursor position, 1px — round for pixel-perfect
    if (radPx > 2.0f) {
        Vector2 cp = {roundf(mp.x), roundf(mp.y)};
        float r = floorf(radPx);
        int segs = radPx > 150.0f ? 80 : (radPx > 50.0f ? 48 : 24);
        DrawCircleLinesV(cp, r, WHITE);

        // Direction indicator: "L" shape outward at the direction angle
        ModulatorTable _mt; InputModulator_GetAllSnapshot(&_mt);
        float dirF = _mt.val[csDir];
        float dirAng = dirF * (float)(M_PI * 2.0) - (float)M_PI;
        float cosA = cosf(dirAng), sinA = sinf(dirAng);
        float armExt = 10.0f;                       // extension past the ring
        float tickLen = armExt * 1.3f;               // tick is 1.3x arm
        Vector2 tip = {cp.x + cosA * (r + armExt), cp.y + sinA * (r + armExt)};
        Vector2 tick = {tip.x + sinA * tickLen, tip.y - cosA * tickLen};  // outward (right side)
        DrawLineEx(cp, tip, 1.0f, WHITE);            // arm from center
        DrawLineEx(tip, tick, 1.0f, WHITE);          // perpendicular tick outward
    }

    rlDrawRenderBatchActive();
    glDisable(GL_COLOR_LOGIC_OP);
}

bool PaintHudModule::HandleInput(InputState& input, const DrawRect& rect) {
    (void)input; (void)rect;
    return false;  // purely visual, no input capture
}
