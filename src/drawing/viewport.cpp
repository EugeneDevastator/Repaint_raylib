#include "repaint.h"
#include "undo.h"
#include "replay_recorder.h"
#include "layerstack.h"
#include "rlgl.h"
#include "stroke_engine.h"
#include "StrokeEmitter.h"
#include "InputQueue.h"
#include "tablet_platform.h"
#include "network_broker.h"

static void PushDabSegment(ICommandBroker* b, float x, float y, float srcX, float srcY, const d_RealBrush& brush, int toolMode) {
    CollapsedBrush cb = CollapseBrushParams(brush, 0.0f, toolMode);
    SegmentData s; memset(&s, 0, sizeof(s));
    s.pos1 = Vector2{x, y};
    s.pos2 = Vector2{srcX, srcY};
    s.ctrl0 = s.ctrl3 = s.pos1;
    s.brushFrom = s.brush = cb;
    s.seed = brush.seed;
    s.userTexBucket = 0xFF;
    s.userTexSlot = 0xFF;
    if (b) b->on_segment(s);
}


extern float g_pivotCursorX, g_pivotCursorY;

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
    vp->m_distortLastDabPos = Vector2{0, 0};
}

void Viewport_SetBounds(Viewport* vp, Rectangle bounds) {
    vp->bounds = bounds;
}

void Viewport_HandleInput(Viewport* vp, AppState* state) {

    if (IsKeyPressed(KEY_F1)) vp->debugShowStamps = !vp->debugShowStamps;

    Vector2 mousePos = GetMousePosition();

    vp->inBounds = mousePos.x >= vp->bounds.x && mousePos.x <= vp->bounds.x + vp->bounds.width &&
                   mousePos.y >= vp->bounds.y && mousePos.y <= vp->bounds.y + vp->bounds.height;

    // Global pan: space+move (no click needed)
    bool spaceHeld = IsKeyDown(KEY_SPACE);
    if (spaceHeld && vp->inBounds) {
        Vector2 delta = { mousePos.x - vp->lastMousePos.x, mousePos.y - vp->lastMousePos.y };
        state->camera.target.x -= delta.x / state->camera.zoom;
        state->camera.target.y -= delta.y / state->camera.zoom;
        layersDirty = true;
    }

    // Right-click is only used in HUD_LAYER_XFORM or HUD_CANVAS_XFORM mode (rotation). No default right-click pan.
    if (g_activeHud != HUD_LAYER_XFORM && g_activeHud != HUD_CANVAS_XFORM)
        vp->rightMouseDown = false;

    // Zoom
    float wheel = GetMouseWheelMove();
    if (wheel != 0) {
        Vector2 worldBefore = GetScreenToWorld2D(mousePos, state->camera);
        state->camera.zoom += wheel * 0.1f;
        state->camera.zoom = fmaxf(0.1f, fminf(128.0f, state->camera.zoom));
        Vector2 worldAfter = GetScreenToWorld2D(mousePos, state->camera);
        state->camera.target.x += worldBefore.x - worldAfter.x;
        state->camera.target.y += worldBefore.y - worldAfter.y;
        layersDirty = true;
    }

    // Color picker — position stored here, actual pixel readback in App_Draw after composite is rendered
    if (IsKeyDown(KEY_LEFT_ALT) && IsMouseButtonDown(MOUSE_LEFT_BUTTON) && vp->inBounds) {
        g_colorPicking = true;
        g_colorPickScreenX = (int)mousePos.x;
        g_colorPickScreenY = (int)mousePos.y;
        g_colorPickVpBounds = vp->bounds;
    } else {
        g_colorPicking = false;
    }

    vp->lastMousePos = mousePos;
    if (IsKeyDown(KEY_LEFT_ALT)) return;

    Vector2 canvasPos = GetScreenToWorld2D(mousePos, state->camera);

    // Suppress normal painting while in layer transform mode, crop framing mode, or space-panning
    if (g_activeHud == HUD_LAYER_XFORM || state->framingMode == FRAME_CROP || spaceHeld) return;

    bool leftDown = IsMouseButtonDown(MOUSE_BUTTON_LEFT);
    int active = state->activeLayer;

    // Adjust brush rotation so stamps appear upright in world space
    float adjustedAngle = state->initialAngle;

    // Inverse layer-transform helper — converts world-space coords to paint-space
    auto toPaintSpace = [&](Vector2 worldPt) -> Vector2 {
        if (state->editTexMode || active < 0 || active >= LayerStack_Count())
            return worldPt;
        sLayerProps* lp = LayerStack_GetProps(active);
        float a = lp->xform.mat[0], b = lp->xform.mat[1], tx = lp->xform.mat[2];
        float c = lp->xform.mat[3], d = lp->xform.mat[4], ty = lp->xform.mat[5];
        float det = a * d - b * c;
        if (fabsf(det) <= 0.0001f) return worldPt;
        float invDet = 1.0f / det;
        float ia = d * invDet, ib = -b * invDet, itx = (b * ty - d * tx) * invDet;
        float ic = -c * invDet, id = a * invDet, ity = (c * tx - a * ty) * invDet;
        Vector2 pt;
        pt.x = worldPt.x * ia + worldPt.y * ib + itx;
        pt.y = worldPt.x * ic + worldPt.y * id + ity;
        return pt;
    };

    // Transform brush position if the active layer has a transform
    Vector2 paintPos = toPaintSpace(canvasPos);

    // Subtract layer rotation so brush stamps appear upright in world space
    if (!state->editTexMode && active >= 0 && active < LayerStack_Count()) {
        sLayerProps* lp = LayerStack_GetProps(active);
        float layerRot = atan2f(lp->xform.mat[3], lp->xform.mat[0]) * (180.0f / (float)M_PI);
        adjustedAngle -= layerRot;
    }

    // Compute world-to-texture-pixel conversion for brush radius.
    // rad_out is in world units; multiply by worldToTexPx to get texture pixels.
    float worldToTexPx = WORLD_UNIT_PX;
    if (!state->editTexMode && active >= 0 && active < LayerStack_Count()) {
        sLayerProps* lp = LayerStack_GetProps(active);
        float sx = sqrtf(lp->xform.mat[0] * lp->xform.mat[0] + lp->xform.mat[3] * lp->xform.mat[3]);
        float sy = sqrtf(lp->xform.mat[1] * lp->xform.mat[1] + lp->xform.mat[4] * lp->xform.mat[4]);
        float avg = (sx + sy) * 0.5f;
        if (avg > 0.001f) worldToTexPx = WORLD_UNIT_PX / avg;
    }

    // Record raw input positions for debug (during active stroke)
    if (leftDown && !vp->wasMouseDown)
        vp->inputLen = 0;
    if ((vp->wasMouseDown || leftDown) && vp->inputLen < MAX_STROKE_PTS)
        vp->inputPts[vp->inputLen++] = paintPos;

    // ── Texture editing mode ──────────────────────────────────────────
    if (state->editTexMode && TM_IsValid(state->editTexSlot)) {
        TexSlot* bt = TM_Get(state->editTexSlot);
        if (bt && !bt->builtIn && bt->rt.id > 0 && (state->mode == eBrush || state->mode == eSmudge)) {
            float dstX = -state->camera.target.x * state->camera.zoom + state->camera.offset.x;
            float dstY = -state->camera.target.y * state->camera.zoom + state->camera.offset.y;
            float dstW = bt->w * state->camera.zoom;
            float dstH = bt->h * state->camera.zoom;
            float tx = (mousePos.x - dstX) / dstW * bt->w;
            float ty = (mousePos.y - dstY) / dstH * bt->h;

            if (!vp->wasMouseDown) {
                if (vp->inBounds && leftDown) {
                    Modulators_SnapRunState();
                    if (state->undo) state->undo->Snapshot(state, state->editTexSlot);

                    InputEntry be;
                    be.type = InputEntry::Begin;
                    be.x = tx; be.y = ty;
                    be.brush = state->currentBrush.Realb;
                    be.initAngle = state->initialAngle;
                    be.toolMode = state->mode;
                    be.targetSlot = state->editTexSlot;
                    if (TM_IsValid(state->brushTexSlot)) {
                        be.userTexBucket = TM_BUCKET_USER;
                        be.userTexSlot = state->brushTexSlot.slot;
                    } else {
                        be.userTexBucket = 0xFF;
                        be.userTexSlot = 0xFF;
                    }
                    be.worldToTexPx = WORLD_UNIT_PX;
                    be.timestamp = GetTime();
                    g_inputQueue.AddEntry(be);

                    vp->wasMouseDown = true;
                }
            } else if (leftDown) {
                double now = GetTime();
                StrokePoint sp = vp->inputFilter.Feed(tx, ty, now);
                InputEntry e;
                e.type = InputEntry::Point;
                e.x = sp.x; e.y = sp.y;
                e.pressure = 1.0f; e.rotation = 0.5f;
                e.tiltX = 0; e.tiltY = 0; e.velocity = sp.velocity; e.timestamp = now;
                g_inputQueue.AddEntry(e);
            }

            if (!leftDown && vp->wasMouseDown) {
                InputEntry ee; ee.type = InputEntry::End;
                g_inputQueue.AddEntry(ee);
                vp->wasMouseDown = false;
            }
        }
        layersDirty = true;
    }

    // ── Normal layer painting ────────────────────────────────────────
    if (!state->editTexMode && (vp->inBounds || vp->wasMouseDown) && leftDown &&
        (state->mode == eBrush || state->mode == eSmudge || state->mode == eDistort || state->mode == eContrast))
    {
        if (active >= 0 && active < LayerStack_Count() && LayerStack_GetRT(active).id > 0) {
            if (state->mode == eBrush || state->mode == eSmudge) {
                if (!vp->wasMouseDown) {
                    Modulators_SnapRunState();
                    if (state->undo) state->undo->Snapshot(state, LayerStack_GetSlotID(active));

                    float origRad = state->currentBrush.Realb.rad_out;
                    state->currentBrush.Realb.rad_out *= worldToTexPx;
                    TabletPlatform_ClearMousePos();

                    InputEntry be;
                    be.type = InputEntry::Begin;
                    be.x = paintPos.x; be.y = paintPos.y;
                    be.brush = state->currentBrush.Realb;
                    be.initAngle = adjustedAngle;
                    be.toolMode = state->mode;
                    be.targetSlot = LayerStack_GetSlotID(active);
                    if (TM_IsValid(state->brushTexSlot)) {
                        be.userTexBucket = TM_BUCKET_USER;
                        be.userTexSlot = state->brushTexSlot.slot;
                    } else {
                        be.userTexBucket = 0xFF;
                        be.userTexSlot = 0xFF;
                    }
                    be.worldToTexPx = worldToTexPx;
                    be.timestamp = GetTime();
                    g_inputQueue.AddEntry(be);

                    state->currentBrush.Realb.rad_out = origRad;
                    vp->wasMouseDown = true;
                    if (vp->strokeLen < MAX_STROKE_PTS)
                        vp->strokePts[vp->strokeLen++] = paintPos;
                } else {
                    float origRad = state->currentBrush.Realb.rad_out;
                    state->currentBrush.Realb.rad_out *= worldToTexPx;

                    float mouseBuf[1024];
                    int n = TabletPlatform_DrainMousePos(mouseBuf, 512);
                    for (int i = 0; i < n; i++) {
                        Vector2 screenPos = {mouseBuf[i*2], mouseBuf[i*2+1]};
                        Vector2 worldPos = GetScreenToWorld2D(screenPos, state->camera);
                        Vector2 paintPt = toPaintSpace(worldPos);
                        StrokePoint sp = vp->inputFilter.Feed(paintPt.x, paintPt.y, GetTime());
                        InputEntry e;
                        e.type = InputEntry::Point;
                        e.x = sp.x; e.y = sp.y;
                        e.pressure = g_modPars.Pars[csPressure];
                        e.tiltX = g_modPars.Pars[csTilt];
                        e.tiltY = g_modPars.Pars[csVtilt];
                        e.rotation = g_modPars.Pars[csRot];
                        e.velocity = sp.velocity;
                        e.timestamp = GetTime();
                        g_inputQueue.AddEntry(e);
                    }
                    state->currentBrush.Realb.rad_out = origRad;
                }
            } else {
                // Distort / Contrast
                float sv = BParam_GetValue(&bpSpacing);
                float scaledRad = state->currentBrush.Realb.rad_out * worldToTexPx;
                float spacing = scaledRad * 2.0f * sv;
                if (spacing < 2.0f) spacing = 2.0f;
                if (!vp->wasMouseDown) {
                    if (vp->broker) {
                        d_RealBrush scaled = state->currentBrush.Realb;
                        scaled.rad_out = scaledRad;
                        BrushDab ev = {paintPos.x, paintPos.y, paintPos.x, paintPos.y, scaled};
                        PushDabSegment(vp->broker, ev.x, ev.y, ev.srcX, ev.srcY, ev.brush, state->mode);
                    }
                    vp->wasMouseDown = true;
                } else {
                if (Dist2D(vp->m_distortLastDabPos, paintPos) >= spacing) {
                    if (vp->broker) {
                        d_RealBrush scaled = state->currentBrush.Realb;
                        scaled.rad_out = scaledRad;
                        BrushDab ev = {paintPos.x, paintPos.y, paintPos.x, paintPos.y, scaled};
                        PushDabSegment(vp->broker, ev.x, ev.y, ev.srcX, ev.srcY, ev.brush, state->mode);
                    }
                    vp->m_distortLastDabPos = paintPos;
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
            float origRad = state->currentBrush.Realb.rad_out;
            state->currentBrush.Realb.rad_out *= worldToTexPx;
            InputEntry ee; ee.type = InputEntry::End;
            g_inputQueue.AddEntry(ee);
            state->currentBrush.Realb.rad_out = origRad;
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
    if (state->mode == ePolyStripe && vp->inBounds && leftDown && active >= 0 && active < LayerStack_Count() && LayerStack_GetRT(active).id > 0) {
        if (!vp->wasMouseDown) {
            vp->lineLastDabPos = paintPos;
            vp->wasMouseDown = true;
        } else {
            float spacing = fmaxf(state->currentBrush.Realb.rad_out * worldToTexPx * 2.0f * BParam_GetValue(&bpSpacing), 2.0f);
            if (Dist2D(vp->lineLastDabPos, paintPos) > spacing) {
                float segLen = Dist2D(vp->lineLastDabPos, paintPos);
                int steps = (int)(segLen / spacing) + 1;
                if (steps < 1) steps = 1;
                for (int s = 0; s <= steps; s++) {
                    float t = (float)s / (float)steps;
                    Vector2 pos = {vp->lineLastDabPos.x + (paintPos.x - vp->lineLastDabPos.x) * t,
                                   vp->lineLastDabPos.y + (paintPos.y - vp->lineLastDabPos.y) * t};
                    if (vp->broker) {
                        BrushDab ev = {pos.x, pos.y, pos.x, pos.y, state->currentBrush.Realb};
                        PushDabSegment(vp->broker, ev.x, ev.y, ev.srcX, ev.srcY, ev.brush, state->mode);
                    }
                }
                vp->lineLastDabPos = paintPos;
            }
        }
    } else if (state->mode != ePolyStripe && !leftDown) {
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

    // Raw input points (paintPos every frame)
    for (int i = 0; i < vp->inputLen && i < MAX_STROKE_PTS; i++)
        DrawCircle(vp->inputPts[i].x, vp->inputPts[i].y, 3, BLUE);

    // Spline buffer points (throttled Catmull-Rom control points)
    if (g_emitter) {
        for (int i = 0; i < g_emitter->m_splineCount; i++) {
            DrawCircle(g_emitter->m_splinePts[i].x, g_emitter->m_splinePts[i].y, 4, RED);
            DrawCircleLines(g_emitter->m_splinePts[i].x, g_emitter->m_splinePts[i].y, 4, RED);
        }
    }

    // Emitted segment endpoints (pos1→pos2 pairs)
    if (g_emitter) {
        for (int i = 0; i + 1 < g_emitter->m_segEpCount; i += 2) {
            Vector2 p1 = g_emitter->m_segEndpoints[i];
            Vector2 p2 = g_emitter->m_segEndpoints[i + 1];
            DrawCircle(p1.x, p1.y, 2, YELLOW);
            DrawCircle(p2.x, p2.y, 2, ORANGE);
            DrawLineV(p1, p2, (Color){255, 255, 0, 80});
        }
    }

    // Dab positions (actual stamp locations)
    for (int i = 0; i < vp->strokeLen && i < MAX_STROKE_PTS; i++)
        DrawCircle(vp->strokePts[i].x, vp->strokePts[i].y, 2, GREEN);

    DrawText("BLUE=raw input  RED=spline ctrl  YEL/ORG=segEnds  GREEN=dabs (F1 toggle)", 10, 10, 14, WHITE);
    EndMode2D();
}

// ── ViewportModule ────────────────────────────────────────────────────────

bool ViewportModule::HandleInput(InputState& input, const DrawRect& rect) {
    viewport.bounds = rect.ToRaylib();
    state->camera.offset = Vector2{
        rect.x + rect.w * 0.5f,
        rect.y + rect.h * 0.5f
    };

    // Continue stroke even if mouse leaves viewport bounds
    if (viewport.wasMouseDown) {
        Viewport_HandleInput(&viewport, state);
        return true;
    }

    if (input.mouseCaptured) return false;
    if (!rect.Contains(input.MousePos())) return false;

    Viewport_HandleInput(&viewport, state);
    return true;
}

void ViewportModule::DrawGL(const DrawRect& rect) {
    viewport.bounds = rect.ToRaylib();
    state->camera.offset = Vector2{
        rect.x + rect.w * 0.5f,
        rect.y + rect.h * 0.5f
    };

    ViewportHUD_Draw(state);
    Viewport_DrawDebugOverlays(&viewport, state);
}
