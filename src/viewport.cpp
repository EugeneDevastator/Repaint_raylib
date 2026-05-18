#include "repaint.h"
#include "rlgl.h"

extern bool quickPanelShow;

/* ── Per-dab jitter: adds random offset to each dab in the array ────────
 *   Uses one rand() per dab, scaled by each BParam's jitter setting.
 *   Base brush was computed without jitter so the interpolation is preserved.
 *   For color, the dab's interpolated RGB is converted to HSL, jittered,
 *   then converted back, so per-dab jitter is on top of the segment's
 *   interpolated base.
 */
static void PerDabJitter(InputEvent* dabs, int n) {
    for (int i = 0; i < n; i++) {
        float dr = ((float)rand() / (float)RAND_MAX) * 2.0f - 1.0f;
        d_RealBrush& b = dabs[i].brush;

        { float d = dr * 2.0f * bpSize.user.jitter * (bpSize.outMax - bpSize.outMin);
          b.rad_out += d; b.rad_out = fmaxf(bpSize.outMin, fminf(bpSize.outMax, b.rad_out)); }

        { float h = b.rad_in / fmaxf(b.rad_out, 0.001f);
          float d = dr * 2.0f * bpHardness.user.jitter * (bpHardness.outMax - bpHardness.outMin);
          h += d; h = fmaxf(0.0f, fminf(1.0f, h)); b.rad_in = b.rad_out * h; }

        { float d = dr * 2.0f * bpCurvature.user.jitter * (bpCurvature.outMax - bpCurvature.outMin);
          b.crv += d; b.crv = fmaxf(0.0f, fminf(1.0f, b.crv)); }

        { float d = dr * 2.0f * bpOpacity.user.jitter * (bpOpacity.outMax - bpOpacity.outMin);
          b.opacity += d; b.opacity = fmaxf(0.0f, fminf(1.0f, b.opacity)); }

        { float d = dr * 2.0f * bpScaleRel.user.jitter * (bpScaleRel.outMax - bpScaleRel.outMin);
          b.x2y += d; b.x2y = fmaxf(0.0f, fminf(1.0f, b.x2y)); }

        { float h, s, l; RGBToHSL(b.col, h, s, l);
          h += dr * 2.0f * bpQuickHue.user.jitter * (bpQuickHue.outMax - bpQuickHue.outMin);
          s += dr * 2.0f * bpQuickSat.user.jitter * (bpQuickSat.outMax - bpQuickSat.outMin);
          l += dr * 2.0f * bpQuickLit.user.jitter * (bpQuickLit.outMax - bpQuickLit.outMin);
          h = fmodf(h, 1.0f); if (h < 0) h += 1.0f;
          s = fmaxf(0.0f, fminf(1.0f, s)); l = fmaxf(0.0f, fminf(1.0f, l));
          b.col = HSLToRGB(h, s, l); }

        { float d = dr * 2.0f * bpCloneOpacity.user.jitter * (bpCloneOpacity.outMax - bpCloneOpacity.outMin);
          b.cop += d; b.cop = fmaxf(0.0f, fminf(1.0f, b.cop)); }
    }
}

/* ── Compute base modulated value without jitter — maps cpar through the
 *    clip range and output range, but with the random term fixed to zero. */
static inline float BaseModVal(const BParam& bp, float cpar) {
    float rng = bp.run.clipmaxF - bp.run.clipminF;
    float base = cpar * rng + bp.run.clipminF;
    base = fminf(fmaxf(base, 0.0f), 1.0f);
    return base * (bp.outMax - bp.outMin) + bp.outMin;
}

extern bool quickPanelShow;

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
    vp->prevSegPos = Vector2{0, 0};
    vp->prevSegDir = Vector2{0, 0};
    vp->prevSegLen = 0.0f;
    vp->prevVel = 0.0f;
    vp->initDir = 0.0f;
    vp->initDirSet = false;
    // InputFilter and BrushInterpolator are default-constructed
}

void Viewport_SetBounds(Viewport* vp, Rectangle bounds) {
    vp->bounds = bounds;
}

void Viewport_HandleInput(Viewport* vp, AppState* state) {
    static InputEvent dabs[1024];
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
        int cx = (int)cp.x;
        int cy = (int)cp.y;
        Color picked = {0, 0, 0, 0};
        int gi = 0;
        for (int dy = -1; dy <= 1; dy++) {
            for (int dx = -1; dx <= 1; dx++) {
                int px = cx + dx;
                int py = cy + dy;
                Color c = {0, 0, 0, 0};
                if (px >= 0 && px < state->canvas.width && py >= 0 && py < state->canvas.height) {
                    for (int li = 0; li < state->canvas.layerCount; li++) {
                        if (!state->canvas.layerProps[li].visible) continue;
                        Color* lpix = (Color*)state->canvas.layerImages[li].data;
                        Color sp = lpix[py * state->canvas.width + px];
                        float sa = sp.a / 255.0f * state->canvas.layerProps[li].op;
                        float da = c.a / 255.0f;
                        float outa = sa + da * (1.0f - sa);
                        if (outa > 0.0f) {
                            c.r = (uint8_t)((sp.r * sa + c.r * da * (1.0f - sa)) / outa);
                            c.g = (uint8_t)((sp.g * sa + c.g * da * (1.0f - sa)) / outa);
                            c.b = (uint8_t)((sp.b * sa + c.b * da * (1.0f - sa)) / outa);
                            c.a = (uint8_t)(outa * 255.0f);
                        }
                    }
                }
                g_colorPickGrid[gi++] = c;
                if (dy == 0 && dx == 0) picked = c;
            }
        }
        if (picked.a > 0) {
            float tH, tS, tL;
            RGBToHSL(picked, tH, tS, tL);
            float spd = 0.5f;
            float dh = tH - colorHue;
            if (dh > 0.5f) dh -= 1.0f; else if (dh < -0.5f) dh += 1.0f;
            colorHue += dh * spd;
            if (colorHue < 0.0f) colorHue += 1.0f; else if (colorHue > 1.0f) colorHue -= 1.0f;
            colorSat += (tS - colorSat) * spd;
            colorLit += (tL - colorLit) * spd;
            bpQuickHue.user.clipmaxF = colorHue;
            bpQuickSat.user.clipmaxF = colorSat;
            bpQuickLit.user.clipmaxF = colorLit;
        }
    } else {
        g_colorPicking = false;
    }

    vp->lastMousePos = mousePos;

    if (IsKeyDown(KEY_LEFT_ALT)) return;

    Vector2 canvasPos = GetScreenToWorld2D(mousePos, state->camera);
    bool leftDown = IsMouseButtonDown(MOUSE_LEFT_BUTTON);
    int active = state->activeLayer;

    // ── Texture editing mode ──────────────────────────────────────────
       if (state->editTexMode && state->activeBrushTex >= 0 &&
        state->activeBrushTex < state->brushTexCount)
    {
        BrushTexture* bt = &state->brushTex[state->activeBrushTex];
        if (bt->rt.id > 0 && (state->mode == eBrush || state->mode == eSmudge)) {
            float dstX = -state->camera.target.x * state->camera.zoom + state->camera.offset.x;
            float dstY = -state->camera.target.y * state->camera.zoom + state->camera.offset.y;
            float dstW = bt->w * state->camera.zoom;
            float dstH = bt->h * state->camera.zoom;
            float tx = (mousePos.x - dstX) / dstW * bt->w;
            float ty = (mousePos.y - dstY) / dstH * bt->h;

            Texture2D savedTex = g_activeBrushTex;
            g_activeBrushTex = g_defaultBrushTex;
            if (!vp->wasMouseDown) {
                if (vp->inBounds && leftDown) {
                    Modulators_SnapRunState();
                    vp->inputFilter.Reset();
                    vp->brushInterp.BeginStroke(state->currentBrush, tx, ty);

                    vp->prevSegPos = Vector2{tx, ty};
                    vp->prevSegDir = Vector2{0, 0};
                    vp->prevSegLen = 0.0f;
                    vp->prevVel = 0.0f;
                    vp->initDirSet = false;

                    d_Brush tb; memset(&tb, 0, sizeof(tb));
                    tb.Realb = state->currentBrush.Realb;
                    tb.Realb.opacity = 1.0f;
                    BrushBlend_ApplyStamp(bt->rt, &tb, g_activeBrushTex, tx, ty, tx, ty);
                    vp->wasMouseDown = true;
                }
            } else if (leftDown) {
                double now = GetTime();
                StrokePoint sp = vp->inputFilter.Feed(tx, ty, now);

                // Compute modulators for this segment
                float segDx = tx - vp->prevSegPos.x;
                float segDy = ty - vp->prevSegPos.y;
                float segLen = sqrtf(segDx * segDx + segDy * segDy);
                float dirAng = AtanXY(segDx, segDy);

                g_modPars.Pars[csVel] = sp.velocity;
                g_modPars.Pars[csDir] = RngConv(dirAng, -(float)M_PI, (float)M_PI, 0.0f, 1.0f);

                if (!vp->initDirSet && segLen > 0.5f) {
                    vp->initDir = dirAng;
                    vp->initDirSet = true;
                }
                g_modPars.Pars[csIdir] = RngConv(vp->initDir, -(float)M_PI, (float)M_PI, 0.0f, 1.0f);

                if (vp->prevSegLen > 0.5f && segLen > 0.5f) {
                    float dot = (vp->prevSegDir.x * segDx + vp->prevSegDir.y * segDy)
                              / (vp->prevSegLen * segLen);
                    g_modPars.Pars[csCrv] = RngConv(dot, 0.8f, 1.0f, 0.0f, 1.0f);
                }

                g_modPars.Pars[csAcc] = 1.0f - fabsf(sp.velocity - vp->prevVel);
                g_modPars.Pars[csAcc] = RngConv(g_modPars.Pars[csAcc], 0.7f, 1.0f, 0.0f, 1.0f);

                if (segLen > 0.001f)
                    g_modPars.Pars[csHVdir] = fabsf(segDx / segLen);

                vp->prevSegPos = Vector2{tx, ty};
                vp->prevSegDir = Vector2{segDx, segDy};
                vp->prevSegLen = segLen;
                vp->prevVel = sp.velocity;

                d_RealBrush targetBr = state->currentBrush.Realb;
                targetBr.rad_out  = BaseModVal(bpSize,       g_modPars.Pars[bpSize.penMode]);
                float hVal        = BaseModVal(bpHardness,   g_modPars.Pars[bpHardness.penMode]);
                targetBr.rad_in   = targetBr.rad_out * hVal;
                targetBr.crv      = BaseModVal(bpCurvature,  g_modPars.Pars[bpCurvature.penMode]);
                targetBr.opacity  = BaseModVal(bpOpacity,    g_modPars.Pars[bpOpacity.penMode]);
                targetBr.resangle = fmodf(state->initialAngle + BaseModVal(bpAngle, g_modPars.Pars[bpAngle.penMode]), 360.0f);
                targetBr.x2y      = BaseModVal(bpScaleRel,   g_modPars.Pars[bpScaleRel.penMode]);
                float spacingVal  = BParam_GetValue(&bpSpacing);
                float spacing = fmaxf(state->currentBrush.Realb.rad_out * spacingVal * spacingVal, 1.0f);
                int n = vp->brushInterp.FeedStrokePoint(sp, targetBr, dabs, 1024, spacing, state->mode);
                PerDabJitter(dabs, n);
                d_Brush tb; memset(&tb, 0, sizeof(tb));
                for (int i = 0; i < n; i++) {
                    tb.Realb = dabs[i].brush;
                    BrushBlend_ApplyStamp(bt->rt, &tb, g_activeBrushTex, dabs[i].x, dabs[i].y, dabs[i].srcX, dabs[i].srcY);
                }
            }
            g_activeBrushTex = savedTex;
        }
        layersDirty = true;
        if (!leftDown) {
            if (vp->wasMouseDown) vp->brushInterp.EndStroke();
            vp->wasMouseDown = false;
        }
    }


    // ── Brush / Smudge (normal layer painting) ────────────────────────
    if (!state->editTexMode && (vp->inBounds || vp->wasMouseDown) && leftDown &&
        (state->mode == eBrush || state->mode == eSmudge || state->mode == eDisp || state->mode == eCont))
    {
        if (active >= 0 && active < state->texCount && state->layerRTs[active].id > 0) {
            if (state->mode == eBrush || state->mode == eSmudge) {
                if (!vp->wasMouseDown) {
                    Modulators_SnapRunState();
                    vp->inputFilter.Reset();
                    vp->inputFilter.Feed(canvasPos.x, canvasPos.y, GetTime());
                    vp->brushInterp.BeginStroke(state->currentBrush, canvasPos.x, canvasPos.y);

                    // Reset modulator tracking for new stroke
                    vp->prevSegPos = canvasPos;
                    vp->prevSegDir = Vector2{0, 0};
                    vp->prevSegLen = 0.0f;
                    vp->prevVel = 0.0f;
                    vp->initDirSet = false;

                    // First dab: use current brush directly (no modulation — no segment to measure)
                    if (vp->broker) {
                        d_RealBrush br = state->currentBrush.Realb;
                        InputEvent ev = {canvasPos.x, canvasPos.y, canvasPos.x, canvasPos.y, br};
                        vp->broker->on_input(ev);
                    }
                    vp->wasMouseDown = true;
                    if (vp->strokeLen < MAX_STROKE_PTS)
                        vp->strokePts[vp->strokeLen++] = canvasPos;
                } else {
                    double now = GetTime();
                    StrokePoint sp = vp->inputFilter.Feed(canvasPos.x, canvasPos.y, now);

                    // ── Compute all non-tablet modulator values for this segment ──
                    float segDx = canvasPos.x - vp->prevSegPos.x;
                    float segDy = canvasPos.y - vp->prevSegPos.y;
                    float segLen = sqrtf(segDx * segDx + segDy * segDy);
                    float dirAng = AtanXY(segDx, segDy);

                    g_modPars.Pars[csVel] = sp.velocity;
                    g_modPars.Pars[csDir] = RngConv(dirAng, -(float)M_PI, (float)M_PI, 0.0f, 1.0f);

                    if (!vp->initDirSet && segLen > 0.5f) {
                        vp->initDir = dirAng;
                        vp->initDirSet = true;
                    }
                    g_modPars.Pars[csIdir] = RngConv(vp->initDir, -(float)M_PI, (float)M_PI, 0.0f, 1.0f);

                    if (vp->prevSegLen > 0.5f && segLen > 0.5f) {
                        float dot = (vp->prevSegDir.x * segDx + vp->prevSegDir.y * segDy)
                                  / (vp->prevSegLen * segLen);
                        g_modPars.Pars[csCrv] = RngConv(dot, 0.8f, 1.0f, 0.0f, 1.0f);
                    }

                    g_modPars.Pars[csAcc] = 1.0f - fabsf(sp.velocity - vp->prevVel);
                    g_modPars.Pars[csAcc] = RngConv(g_modPars.Pars[csAcc], 0.7f, 1.0f, 0.0f, 1.0f);

                    {   // csRelang: alignment between stroke dir and brush rotation
                        float dir01 = g_modPars.Pars[csDir];
                        float rot01 = state->currentBrush.Realb.resangle / 360.0f;
                        float rel = fabsf(dir01 - rot01);
                        if (rel > 0.5f) rel = 1.0f - rel;
                        rel = rel * 2.0f;
                        rel = 1.0f - fabsf(rel - 0.5f) * 2.0f;
                        g_modPars.Pars[csRelang] = rel;
                    }

                    if (segLen > 0.001f)
                        g_modPars.Pars[csHVdir] = fabsf(segDx / segLen);

                    // Update tracking state
                    vp->prevSegPos = canvasPos;
                    vp->prevSegDir = Vector2{segDx, segDy};
                    vp->prevSegLen = segLen;
                    vp->prevVel = sp.velocity;

                    // ── Build target brush (no jitter) using all modulators ──
                    d_RealBrush targetBr = state->currentBrush.Realb;
                    targetBr.rad_out  = BaseModVal(bpSize,       g_modPars.Pars[bpSize.penMode]);
                    float hVal        = BaseModVal(bpHardness,   g_modPars.Pars[bpHardness.penMode]);
                    targetBr.rad_in   = targetBr.rad_out * hVal;
                    targetBr.crv      = BaseModVal(bpCurvature,  g_modPars.Pars[bpCurvature.penMode]);
                    targetBr.opacity  = BaseModVal(bpOpacity,    g_modPars.Pars[bpOpacity.penMode]);
                    targetBr.resangle = fmodf(state->initialAngle + BaseModVal(bpAngle, g_modPars.Pars[bpAngle.penMode]), 360.0f);
                    targetBr.x2y      = BaseModVal(bpScaleRel,   g_modPars.Pars[bpScaleRel.penMode]);
                    targetBr.col      = HSLToRGB(
                        BaseModVal(bpQuickHue,   g_modPars.Pars[bpQuickHue.penMode]),
                        BaseModVal(bpQuickSat,   g_modPars.Pars[bpQuickSat.penMode]),
                        BaseModVal(bpQuickLit,   g_modPars.Pars[bpQuickLit.penMode]));
                    targetBr.cop = (state->mode == eSmudge)
                        ? BaseModVal(bpCloneOpacity, g_modPars.Pars[bpCloneOpacity.penMode]) : 0.0f;

                    // Feed through BrushInterpolator → dabs
                    float spacingVal = BParam_GetValue(&bpSpacing);
                    float spacing = fmaxf(state->currentBrush.Realb.rad_out * spacingVal * spacingVal, 1.0f);
                    int n = vp->brushInterp.FeedStrokePoint(sp, targetBr, dabs, 1024, spacing, state->mode);
                    PerDabJitter(dabs, n);
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
    } else if (!state->editTexMode) {
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

void Viewport_DrawDebugOverlays(Viewport* vp, AppState* state) {
    if (!vp->debugShowStamps || vp->strokeLen <= 0) return;
    BeginMode2D(state->camera);
    float rad = state->currentBrush.Realb.rad_out;
    for (int i = 0; i < vp->strokeLen; i++) {
        DrawCircleLines(vp->strokePts[i].x, vp->strokePts[i].y, rad, YELLOW);
        DrawRectangleLines(vp->strokePts[i].x - rad, vp->strokePts[i].y - rad, rad * 2, rad * 2, Color{255, 255, 0, 80});
        DrawCircle(vp->strokePts[i].x, vp->strokePts[i].y, 2, RED);
    }
    DrawText("DEBUG: stamp positions (F1 toggle)", 10, 10, 14, YELLOW);
    EndMode2D();
}
