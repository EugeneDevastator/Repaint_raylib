#include "repaint.h"
#include "rlgl.h"
#include "stroke_engine.h"

extern bool quickPanelShow;

static BrushDab MakeBrushDab(float x, float y, const DrawDab& d) {
    BrushDab r;
    r.x = x; r.y = y;
    r.srcX = d.srcX; r.srcY = d.srcY;
    r.brush.rad_out     = d.brush.rad_out_px;
    r.brush.rad_in      = d.brush.rad_out_px * d.brush.radInRatio;
    r.brush.opacity     = d.brush.opacity;
    r.brush.crv         = d.brush.crv;
    r.brush.x2y         = d.brush.scale_y;
    r.brush.resangle    = d.brush.resangle;
    r.brush.col         = d.brush.col;
    r.brush.cop         = d.brush.cop;
    r.brush.bmidx       = (uint8_t)d.brush.bmidx;
    r.brush.preserveop  = d.brush.preserveop;
    r.brush.eraseMode   = d.brush.eraseMode;
    r.brush.perspective = d.brush.perspective;
    r.brush.texScale    = d.brush.texScale;
    r.brush.texFeather  = d.brush.texFeather;
    r.brush.texThresh   = d.brush.texThresh;
    r.brush.texBlendVal = d.brush.texBlendVal;
    r.brush.texBlendMode = d.brush.texBlendMode;
    r.brush.texNoisemode = d.brush.texNoisemode;
    r.brush.texColorMode = d.brush.texColorMode;
    r.brush.useTexLumAsAlpha = d.brush.useTexLumAsAlpha;
    r.brush.pwr         = d.brush.pwr;
    r.brush.seed        = 0;
    r.brush.sol         = 1.0f;
    r.brush.sol2op      = 0.0f;
    return r;
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
    StrokeEngine_Init(&vp->strokeEng);
}

void Viewport_SetBounds(Viewport* vp, Rectangle bounds) {
    vp->bounds = bounds;
}

void Viewport_HandleInput(Viewport* vp, AppState* state) {
    static DrawDab dabs[1024];

    if (IsKeyPressed(KEY_F1)) vp->debugShowStamps = !vp->debugShowStamps;
    if (quickPanelShow) return;

    Vector2 mousePos = GetMousePosition();

    vp->inBounds = mousePos.x >= vp->bounds.x && mousePos.x <= vp->bounds.x + vp->bounds.width &&
                   mousePos.y >= vp->bounds.y && mousePos.y <= vp->bounds.y + vp->bounds.height;

    // Pan
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

    // Zoom
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

    // Color picker
    if (IsKeyDown(KEY_LEFT_ALT) && IsMouseButtonDown(MOUSE_LEFT_BUTTON) && vp->inBounds) {
        g_colorPicking = true;
        Vector2 cp = GetScreenToWorld2D(mousePos, state->camera);
        int cx = (int)cp.x, cy = (int)cp.y;
        Color picked = {0, 0, 0, 0};
        int gi = 0;
        for (int dy = -1; dy <= 1; dy++) {
            for (int dx = -1; dx <= 1; dx++) {
                int px = cx + dx, py = cy + dy;
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

    // Record input positions for debug (during active stroke)
    if ((vp->wasMouseDown || leftDown) && vp->inputLen < MAX_STROKE_PTS)
        vp->inputPts[vp->inputLen++] = canvasPos;

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
            Vector2 texPos = {tx, ty};

            Texture2D savedTex = g_activeBrushTex;
            g_activeBrushTex = g_defaultBrushTex;

            if (!vp->wasMouseDown) {
                if (vp->inBounds && leftDown) {
                    Modulators_SnapRunState();
                    vp->inputLen = 0;
                    StrokeEngine_BeginStroke(&vp->strokeEng, &state->currentBrush, tx, ty);
                    vp->inputFilter.Reset();
                    vp->inputFilter.Feed(tx, ty, GetTime());
                    vp->wasMouseDown = true;
                }
            } else if (leftDown) {
                double now = GetTime();
                StrokePoint sp = vp->inputFilter.Feed(tx, ty, now);
                int n = StrokeEngine_FeedPoint(&vp->strokeEng, sp,
                    &state->currentBrush.Realb, state->initialAngle, state->mode,
                    dabs, 1024);
                if (n > 0)
                    StrokeEngine_ApplyDabs(bt->rt, g_activeBrushTex, dabs, n);
            }
            g_activeBrushTex = savedTex;
        }
        layersDirty = true;
        if (!leftDown) {
            if (vp->wasMouseDown) StrokeEngine_EndStroke(&vp->strokeEng);
            vp->wasMouseDown = false;
        }
    }

    // ── Normal layer painting ────────────────────────────────────────
    if (!state->editTexMode && (vp->inBounds || vp->wasMouseDown) && leftDown &&
        (state->mode == eBrush || state->mode == eSmudge || state->mode == eDisp || state->mode == eCont))
    {
        if (active >= 0 && active < state->texCount && state->layerRTs[active].id > 0) {
            if (state->mode == eBrush || state->mode == eSmudge) {
                if (!vp->wasMouseDown) {
                    Modulators_SnapRunState();
                    vp->inputLen = 0;
                    StrokeEngine_BeginStroke(&vp->strokeEng, &state->currentBrush,
                                             canvasPos.x, canvasPos.y);
                    vp->inputFilter.Reset();
                    vp->inputFilter.Feed(canvasPos.x, canvasPos.y, GetTime());
                    vp->wasMouseDown = true;
                    if (vp->strokeLen < MAX_STROKE_PTS)
                        vp->strokePts[vp->strokeLen++] = canvasPos;
                } else {
                    double now = GetTime();
                    StrokePoint sp = vp->inputFilter.Feed(canvasPos.x, canvasPos.y, now);
                    int n = StrokeEngine_FeedPoint(&vp->strokeEng, sp,
                        &state->currentBrush.Realb, state->initialAngle, state->mode,
                        dabs, 1024);
                    for (int i = 0; i < n; i++) {
                        if (vp->broker) {
                            BrushDab bd = MakeBrushDab(dabs[i].x, dabs[i].y, dabs[i]);
                            vp->broker->on_input(bd);
                        }
                        if (vp->strokeLen < MAX_STROKE_PTS)
                            vp->strokePts[vp->strokeLen++] = Vector2{dabs[i].x, dabs[i].y};
                    }
                }
            } else {
                // Disp / Cont
                float sv = BParam_GetValue(&bpSpacing);
                float spacing = state->currentBrush.Realb.rad_out * 2.0f * sv;
                if (spacing < 2.0f) spacing = 2.0f;
                if (!vp->wasMouseDown) {
                    if (vp->broker) {
                        BrushDab ev = {canvasPos.x, canvasPos.y, canvasPos.x, canvasPos.y,
                                       state->currentBrush.Realb};
                        vp->broker->on_input(ev);
                    }
                    vp->wasMouseDown = true;
                } else {
                    if (Dist2D(vp->strokeEng.lastDabPos, canvasPos) >= spacing) {
                        if (vp->broker) {
                            BrushDab ev = {canvasPos.x, canvasPos.y, canvasPos.x, canvasPos.y,
                                           state->currentBrush.Realb};
                            vp->broker->on_input(ev);
                        }
                        vp->strokeEng.lastDabPos = canvasPos;
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
            StrokeEngine_EndStroke(&vp->strokeEng);
            vp->strokeEnded = true;
            vp->endLayer = active;
        }
        layersDirty = true;
        vp->wasMouseDown = false;
    }

    if (!leftDown) {
        vp->strokeLen = 0;
    }

    // Line tool
    if (state->mode == eLine && vp->inBounds && leftDown && active >= 0 && active < state->texCount && state->layerRTs[active].id > 0) {
        if (!vp->wasMouseDown) {
            vp->lineLastDabPos = canvasPos;
            vp->wasMouseDown = true;
        } else {
            float spacing = fmaxf(state->currentBrush.Realb.rad_out * 2.0f * BParam_GetValue(&bpSpacing), 2.0f);
            if (Dist2D(vp->lineLastDabPos, canvasPos) > spacing) {
                float segLen = Dist2D(vp->lineLastDabPos, canvasPos);
                int steps = (int)(segLen / spacing) + 1;
                if (steps < 1) steps = 1;
                for (int s = 0; s <= steps; s++) {
                    float t = (float)s / (float)steps;
                    Vector2 pos = {vp->lineLastDabPos.x + (canvasPos.x - vp->lineLastDabPos.x) * t,
                                   vp->lineLastDabPos.y + (canvasPos.y - vp->lineLastDabPos.y) * t};
                    if (vp->broker) {
                        BrushDab ev = {pos.x, pos.y, pos.x, pos.y, state->currentBrush.Realb};
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
    if (!vp->debugShowStamps) return;
    BeginMode2D(state->camera);

    for (int i = 0; i < vp->inputLen && i < MAX_STROKE_PTS; i++)
        DrawCircle(vp->inputPts[i].x, vp->inputPts[i].y, 3, BLUE);

    DrawText("DEBUG: input pos (F1 toggle)", 10, 10, 14, BLUE);
    EndMode2D();
}
