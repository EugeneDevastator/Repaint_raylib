#include "repaint.h"
#include "rlgl.h"

extern bool quickPanelShow;

void SyncLayerTexture(AppState* state, int layer) {
    if (layer < 0 || layer >= state->texCount) return;
    if (state->layerRTs[layer].id == 0) return;
    SyncImageFromRT(state, layer);
    if (state->layerTextures[layer].id > 0) UnloadTexture(state->layerTextures[layer]);
    state->layerTextures[layer] = LoadTextureFromImage(state->canvas.layerImages[layer]);
}

void Viewport_Init(Viewport* vp, Rectangle bounds) {
    vp->bounds = bounds;
    vp->strokeLen = 0;
    vp->wasMouseDown = false;
    vp->debugShowStamps = false;
    vp->rightMouseDown = false;
    vp->lastMousePos = Vector2{0, 0};
    vp->inBounds = false;
    vp->strokeEnded = false;
    vp->endLayer = 0;
    vp->broker = NULL;
    vp->lineLastDabPos = Vector2{0, 0};
    // InputFilter and BrushInterpolator are default-constructed
}

void Viewport_SetBounds(Viewport* vp, Rectangle bounds) {
    vp->bounds = bounds;
}

void Viewport_HandleInput(Viewport* vp, AppState* state) {
    if (IsKeyPressed(KEY_F1)) vp->debugShowStamps = !vp->debugShowStamps;

    // Quick panel open — block ALL viewport interactions
    if (quickPanelShow) return;

    Vector2 mousePos = GetMousePosition();

    // Bounds check first
    vp->inBounds = mousePos.x >= vp->bounds.x && mousePos.x <= vp->bounds.x + vp->bounds.width &&
                   mousePos.y >= vp->bounds.y && mousePos.y <= vp->bounds.y + vp->bounds.height;

    // Pan camera (right mouse — only when within viewport bounds)
    if (vp->inBounds && IsMouseButtonDown(MOUSE_RIGHT_BUTTON)) {
        if (vp->rightMouseDown) {
            Vector2 delta = {
                mousePos.x - vp->lastMousePos.x,
                mousePos.y - vp->lastMousePos.y
            };
            state->camera.target.x -= delta.x / state->camera.zoom;
            state->camera.target.y -= delta.y / state->camera.zoom;
            layersDirty = true;
        }
        vp->rightMouseDown = true;
    } else {
        vp->rightMouseDown = false;
    }

    // Zoom toward cursor (scroll wheel)
    float wheel = GetMouseWheelMove();
    if (wheel != 0) {
        Vector2 worldBefore = GetScreenToWorld2D(mousePos, state->camera);
        state->camera.zoom += wheel * 0.1f;
        state->camera.zoom = fmaxf(0.1f, fminf(5.0f, state->camera.zoom));
        Vector2 worldAfter = GetScreenToWorld2D(mousePos, state->camera);
        state->camera.target.x += worldBefore.x - worldAfter.x;
        state->camera.target.y += worldBefore.y - worldAfter.y;
        layersDirty = true;
    }

    // Alt+Left click/drag color picker
    if (IsKeyDown(KEY_LEFT_ALT) && IsMouseButtonDown(MOUSE_LEFT_BUTTON) && vp->inBounds) {
        g_colorPicking = true;
        Vector2 cp = GetScreenToWorld2D(mousePos, state->camera);
        int px = (int)cp.x;
        int py = (int)cp.y;
        if (px >= 0 && px < state->canvas.width && py >= 0 && py < state->canvas.height) {
            Color picked = {0, 0, 0, 0};
            for (int li = 0; li < state->canvas.layerCount; li++) {
                if (!state->canvas.layerProps[li].visible) continue;
                Color* lpix = (Color*)state->canvas.layerImages[li].data;
                Color sp = lpix[py * state->canvas.width + px];
                float sa = sp.a / 255.0f * state->canvas.layerProps[li].op;
                float da = picked.a / 255.0f;
                float outa = sa + da * (1.0f - sa);
                if (outa > 0.0f) {
                    picked.r = (uint8_t)((sp.r * sa + picked.r * da * (1.0f - sa)) / outa);
                    picked.g = (uint8_t)((sp.g * sa + picked.g * da * (1.0f - sa)) / outa);
                    picked.b = (uint8_t)((sp.b * sa + picked.b * da * (1.0f - sa)) / outa);
                    picked.a = (uint8_t)(outa * 255.0f);
                }
            }
            float tH, tS, tL;
            RGBToHSL(picked, tH, tS, tL);
            if (picked.a == 0) { tS = 0.0f; tL = 1.0f; }
            float spd = 0.5f;
            float dh = tH - colorHue;
            if (dh > 0.5f) dh -= 1.0f;
            else if (dh < -0.5f) dh += 1.0f;
            colorHue += dh * spd;
            if (colorHue < 0.0f) colorHue += 1.0f;
            else if (colorHue > 1.0f) colorHue -= 1.0f;
            colorSat += (tS - colorSat) * spd;
            colorLit += (tL - colorLit) * spd;
            bpQuickHue.slider.clipmaxF = colorHue;
            bpQuickSat.slider.clipmaxF = colorSat;
            bpQuickLit.slider.clipmaxF = colorLit;
        }
    } else {
        g_colorPicking = false;
    }

    vp->lastMousePos = mousePos;

    if (IsKeyDown(KEY_LEFT_ALT)) return;

    Vector2 canvasPos = GetScreenToWorld2D(mousePos, state->camera);
    bool leftDown = IsMouseButtonDown(MOUSE_LEFT_BUTTON);
    int active = state->activeLayer;

    // ── Brush / Smudge ───────────────────────────────────────────────
    if ((vp->inBounds || vp->wasMouseDown) && leftDown &&
        (state->mode == eBrush || state->mode == eSmudge || state->mode == eDisp || state->mode == eCont))
    {
        if (active >= 0 && active < state->texCount && state->layerRTs[active].id > 0) {
            if (state->mode == eBrush || state->mode == eSmudge) {
                if (!vp->wasMouseDown) {
                    vp->inputFilter.Reset();
                    vp->brushInterp.BeginStroke(state->currentBrush, canvasPos.x, canvasPos.y);

                    // First dab: use current brush directly (no interpolation)
                    if (vp->broker) {
                        d_RealBrush br = state->currentBrush.Realb;
                        InputEvent ev = {canvasPos.x, canvasPos.y, canvasPos.x, canvasPos.y, br};
                        vp->broker->on_input(ev);
                    }
                    vp->wasMouseDown = true;
                    if (vp->strokeLen < MAX_STROKE_PTS)
                        vp->strokePts[vp->strokeLen++] = canvasPos;
                } else {
                    // Feed input through the pipeline
                    double now = GetTime();
                    StrokePoint sp = vp->inputFilter.Feed(canvasPos.x, canvasPos.y, now);

                    // Build the target brush with velocity modulation from the stroke point
                    d_RealBrush targetBr = state->currentBrush.Realb;
                    float cpar = (state->currentBrush.Realb.rad_out > 0) ? sp.velocity : 0.0f;
                    targetBr.rad_out  = GetModValFor(&bpSize,       (bpSize.penMode == csVel) ? sp.velocity : 1.0f);
                    float hVal        = GetModValFor(&bpHardness,   (bpHardness.penMode == csVel) ? sp.velocity : 1.0f);
                    targetBr.rad_in   = targetBr.rad_out * hVal;
                    targetBr.crv      = GetModValFor(&bpCurvature,  (bpCurvature.penMode == csVel) ? sp.velocity : 1.0f);
                    targetBr.opacity  = GetModValFor(&bpOpacity,    (bpOpacity.penMode == csVel) ? sp.velocity : 1.0f);
                    float colH        = GetModValFor(&bpQuickHue,   (bpQuickHue.penMode == csVel) ? sp.velocity : 1.0f);
                    float colS        = GetModValFor(&bpQuickSat,   (bpQuickSat.penMode == csVel) ? sp.velocity : 1.0f);
                    float colL        = GetModValFor(&bpQuickLit,   (bpQuickLit.penMode == csVel) ? sp.velocity : 1.0f);
                    targetBr.col      = HSLToRGB(colH, colS, colL);
                    if (state->mode == eSmudge)
                        targetBr.cop = GetModValFor(&bpCloneOpacity, (bpCloneOpacity.penMode == csVel) ? sp.velocity : 1.0f);
                    else
                        targetBr.cop = 0.0f;

                    // Feed through BrushInterpolator → dabs
                    float spacingVal = BParam_GetValue(&bpSpacing);
                InputEvent dabs[128];
                int n = vp->brushInterp.FeedStrokePoint(sp, targetBr, dabs, 128, spacingVal, state->mode);
                    for (int i = 0; i < n; i++) {
                        if (vp->broker) vp->broker->on_input(dabs[i]);
                        if (vp->strokeLen < MAX_STROKE_PTS)
                            vp->strokePts[vp->strokeLen++] = Vector2{dabs[i].x, dabs[i].y};
                    }
                }
            } else {
                // Disp / Cont: simple threshold-based dabbing (old path, kept for now)
                float sv = BParam_GetValue(&bpSpacing);
                float spacing = state->currentBrush.Realb.rad_out * sv * sv;
                if (spacing < 2.0f) spacing = 2.0f;
                if (!vp->wasMouseDown) {
                    if (vp->broker) {
                        InputEvent ev = {canvasPos.x, canvasPos.y, canvasPos.x, canvasPos.y,
                            state->currentBrush.Realb};
                        vp->broker->on_input(ev);
                    }
                    vp->wasMouseDown = true;
                } else {
                    if (Dist2D(vp->brushInterp.lastDabPos, canvasPos) >= spacing) {
                        if (vp->broker) {
                            InputEvent ev = {canvasPos.x, canvasPos.y, canvasPos.x, canvasPos.y,
                                state->currentBrush.Realb};
                            vp->broker->on_input(ev);
                        }
                        vp->brushInterp.lastDabPos = canvasPos;
                    }
                }
            }
        }
        layersDirty = true;
    } else {
        if (vp->strokeLen > 0) {
            vp->strokeEnded = true;
            vp->endLayer = active;
            vp->strokeLen = 0;
        }
        if (vp->wasMouseDown) {
            vp->brushInterp.EndStroke();
            vp->strokeEnded = true;
            vp->endLayer = active;
        }
        layersDirty = true;
        vp->wasMouseDown = false;
    }

    if (!leftDown) {
        vp->strokeLen = 0;
    }

    // Line tool — separate simple pipeline
    if (state->mode == eLine && vp->inBounds && leftDown && active >= 0 && active < state->texCount && state->layerRTs[active].id > 0) {
        if (!vp->wasMouseDown) {
            vp->lineLastDabPos = canvasPos;
            vp->wasMouseDown = true;
        } else {
            float spacing = fmaxf(state->currentBrush.Realb.rad_out * BParam_GetValue(&bpSpacing), 2.0f);
            if (Dist2D(vp->lineLastDabPos, canvasPos) > spacing) {
                float segLen = Dist2D(vp->lineLastDabPos, canvasPos);
                int steps = (int)(segLen / spacing) + 1;
                if (steps < 1) steps = 1;
                d_RealBrush br = state->currentBrush.Realb;
                for (int s = 0; s <= steps; s++) {
                    float t = (float)s / (float)steps;
                    Vector2 pos = {vp->lineLastDabPos.x + (canvasPos.x - vp->lineLastDabPos.x) * t,
                                   vp->lineLastDabPos.y + (canvasPos.y - vp->lineLastDabPos.y) * t};
                    if (vp->broker) {
                        InputEvent ev = {pos.x, pos.y, pos.x, pos.y, br};
                        vp->broker->on_input(ev);
                    }
                }
                vp->lineLastDabPos = canvasPos;
            }
        }
    } else if (state->mode != eLine && !leftDown) {
        if (vp->wasMouseDown) {
            vp->strokeEnded = true;
            vp->endLayer = active;
        }
        layersDirty = true;
        vp->wasMouseDown = false;
    }
}

void Viewport_Draw(Viewport* vp, AppState* state) {
    Rectangle bounds = vp->bounds;

    DrawRectangleRec(bounds, Color{55, 55, 55, 255});

    DrawViewport(state, bounds, state->camera);

    BeginMode2D(state->camera);
    if (vp->debugShowStamps && vp->strokeLen > 0) {
        float rad = state->currentBrush.Realb.rad_out;
        for (int i = 0; i < vp->strokeLen; i++) {
            DrawCircleLines(vp->strokePts[i].x, vp->strokePts[i].y, rad, YELLOW);
            DrawRectangleLines(vp->strokePts[i].x - rad, vp->strokePts[i].y - rad, rad * 2, rad * 2, Color{255, 255, 0, 80});
            DrawCircle(vp->strokePts[i].x, vp->strokePts[i].y, 2, RED);
        }
        DrawText("DEBUG: stamp positions (F1 toggle)", 10, 10, 14, YELLOW);
    }
    EndMode2D();
}
