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

// ── Shared brush Begin entry builder ─────────────────────────────────
static void FillBeginEntry(InputEntry& be, AppState* state, int toolMode,
                           Vector2 canvasPos, float adjustedAngle,
                           TexSlotID targetSlot, float worldToTexPx,
                           const RectXform& destXform)
{
    memset(&be, 0, sizeof(be));
    be.type = InputEntry::Begin;
    be.x = canvasPos.x; be.y = canvasPos.y;
    be.brush = state->currentBrush.Realb;
    be.initAngle = adjustedAngle;
    be.toolMode  = toolMode;
    be.targetSlot = targetSlot;
    be.worldToTexPx = worldToTexPx;
    be.timestamp = GetTime();
    be.destXform = destXform;
    if (TM_IsValid(state->brushTexSlot)) {
        be.userTexBucket = TM_BUCKET_USER;
        be.userTexSlot   = state->brushTexSlot.slot;
    } else {
        be.userTexBucket = 0xFF;
        be.userTexSlot   = 0xFF;
    }
}

static void PushDabSegment(ICommandBroker* b, float x, float y, float srcX, float srcY, const d_RealBrush& brush, int toolMode) {
    UserBrushConfig cfg;
    CaptureBrushConfig(&cfg);
    ModulatorTable mt; Modulator_GetTable(&mt);
    ModulatedBrushConfig mod = ResolveModulatedConfig(cfg, toolMode, 0.0f, &mt);
    DabBrush cb = MakeDabBrush(mod, brush.rad_out);
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

    if (IsKeyPressed(KEY_F3)) vp->debugShowStamps = !vp->debugShowStamps;

    Vector2 mousePos = GetMousePosition();

    vp->inBounds = mousePos.x >= vp->bounds.x && mousePos.x <= vp->bounds.x + vp->bounds.width &&
                   mousePos.y >= vp->bounds.y && mousePos.y <= vp->bounds.y + vp->bounds.height;

    // Global pan: space+move (no click needed)
    bool spaceHeld = IsKeyDown(KEY_SPACE);
    if (spaceHeld && vp->inBounds) {
        if (!vp->spaceHeldPrev) vp->lastMousePos = mousePos;
        vp->spaceHeldPrev = true;
        Vector2 delta = { mousePos.x - vp->lastMousePos.x, mousePos.y - vp->lastMousePos.y };
        state->camera.target.x -= delta.x / state->camera.zoom;
        state->camera.target.y -= delta.y / state->camera.zoom;
        layersDirty = true;
    } else {
        vp->spaceHeldPrev = false;
    }

    // Right-click is only used in HUD_LAYER_XFORM or HUD_CANVAS_XFORM mode (rotation). No default right-click pan.
    if (g_activeHud != HUD_LAYER_XFORM && g_activeHud != HUD_CANVAS_XFORM)
        vp->rightMouseDown = false;

    // Zoom (multiplicative so steps feel uniform at any level)
    float wheel = GetMouseWheelMove();
    if (wheel != 0) {
        Vector2 worldBefore = GetScreenToWorld2D(mousePos, state->camera);
        state->camera.zoom *= (1.0f + wheel * 0.1f);
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

    // Compute destination transform (world → destPixel) and radius scaling
    RectXform destXform = {};
    Xform_Identity(destXform.mat);
    float worldToTexPx   = WORLD_UNIT_PX;
    TexSlotID targetSlot;
    bool  isTexMode = (state->editTexMode && TM_IsValid(state->editTexSlot));
    if (!isTexMode) {
        targetSlot  = LayerStack_GetSlotID(active);
        if (active >= 0 && active < LayerStack_Count()) {
            sLayerProps* lp = LayerStack_GetProps(active);
            // Inverse layer transform → world→paint affine matrix
            float a = lp->xform.mat[0], b = lp->xform.mat[1], ttx = lp->xform.mat[2];
            float c = lp->xform.mat[3], d = lp->xform.mat[4], tty = lp->xform.mat[5];
            float det = a * d - b * c;
            if (fabsf(det) > 0.0001f) {
                float invDet = 1.0f / det;
                float ia = d * invDet, ib = -b * invDet, itx = (b * tty - d * ttx) * invDet;
                float ic = -c * invDet, id = a * invDet, ity = (c * ttx - a * tty) * invDet;
                int texW = GetLayerWpx(active), texH = GetLayerHpx(active);
                float sx = (lp->xform.ww > 0.0f) ? texW / lp->xform.ww : 1.0f;
                float sy = (lp->xform.wh > 0.0f) ? texH / lp->xform.wh : 1.0f;
                destXform.mat[0]=ia*sx; destXform.mat[1]=ib*sx; destXform.mat[2]=itx*sx;
                destXform.mat[3]=ic*sy; destXform.mat[4]=id*sy; destXform.mat[5]=ity*sy;
            }
            // worldToTexPx from layer scale
            int texW = GetLayerWpx(active), texH = GetLayerHpx(active);
            float avgDiv = 0.0f;
            if (lp->xform.ww > 0.0f) avgDiv += lp->xform.ww / texW;
            if (lp->xform.wh > 0.0f) avgDiv += lp->xform.wh / texH;
            if (avgDiv > 0.001f) worldToTexPx = WORLD_UNIT_PX / (avgDiv * 0.5f);
            // Subtrack layer rotation so stamps stay upright in world space
            float layerRot = atan2f(lp->xform.mat[3], lp->xform.mat[0]) * (180.0f / (float)M_PI);
            adjustedAngle -= layerRot;
        }
    } else {
        // User texture: fake identity transform at world (0,0) → texel coords
        targetSlot = state->editTexSlot;
    }

    // Paint-space position (for debug recording and distortion)
    float* m = destXform.mat;
    Vector2 paintPos = {canvasPos.x*m[0] + canvasPos.y*m[1] + m[2],
                        canvasPos.x*m[3] + canvasPos.y*m[4] + m[5]};

    // Record raw input positions for debug (during active stroke)
    if (leftDown && !vp->wasMouseDown)
        vp->inputLen = 0;
    if ((vp->wasMouseDown || leftDown) && vp->inputLen < MAX_STROKE_PTS)
        vp->inputPts[vp->inputLen++] = paintPos;

    // Determine if painting is allowed in current mode
    bool canPaintBrush = false;
    if (!isTexMode && (vp->inBounds || vp->wasMouseDown) && leftDown &&
        (state->mode == eBrush || state->mode == eSmudge || state->mode == eDistort || state->mode == eContrast)) {
        if (active >= 0 && active < LayerStack_Count() && LayerStack_GetRT(active).id > 0)
            canPaintBrush = true;
    }
    if (isTexMode && (vp->inBounds || vp->wasMouseDown) && leftDown) {
        TexSlot* bt = TM_Get(state->editTexSlot);
        if (bt && !bt->builtIn && bt->rt.id > 0 && (state->mode == eBrush || state->mode == eSmudge))
            canPaintBrush = true;
    }

    // ── Unified input processing (world space) ───────────────────────
    bool isBrushSmudge = (state->mode == eBrush || state->mode == eSmudge);
    if (canPaintBrush && !vp->wasMouseDown) {
        Modulators_SnapRunState();
        if (state->undo) state->undo->Snapshot(state, targetSlot);

        float origRad = state->currentBrush.Realb.rad_out;
        state->currentBrush.Realb.rad_out *= worldToTexPx;
        TabletPlatform_ClearMousePos();

        if (isBrushSmudge) {
            InputEntry be;
            FillBeginEntry(be, state, state->mode, canvasPos, adjustedAngle,
                           targetSlot, worldToTexPx, destXform);
            g_inputQueue.AddEntry(be);
        }

        state->currentBrush.Realb.rad_out = origRad;
        vp->wasMouseDown = true;
        if (vp->strokeLen < MAX_STROKE_PTS)
            vp->strokePts[vp->strokeLen++] = paintPos;
    } else if (canPaintBrush && vp->wasMouseDown) {
        if (isBrushSmudge) {
            float mouseBuf[1024];
            int tabletN = TabletPlatform_DrainMousePos(mouseBuf, 512);
            bool  hasTablet = (tabletN > 0);
            int n = hasTablet ? tabletN : 1;
            if (!hasTablet) { mouseBuf[0] = mousePos.x; mouseBuf[1] = mousePos.y; }
            float origRad = state->currentBrush.Realb.rad_out;
            state->currentBrush.Realb.rad_out *= worldToTexPx;
            for (int i = 0; i < n; i++) {
                Vector2 screenPos = {mouseBuf[i*2], mouseBuf[i*2+1]};
                Vector2 worldPos = GetScreenToWorld2D(screenPos, state->camera);
                StrokePoint sp = vp->inputFilter.Feed(worldPos.x, worldPos.y, GetTime());
                InputEntry e;
                memset(&e, 0, sizeof(e));
                e.type = InputEntry::Point;
                e.x = sp.x; e.y = sp.y;
                e.pressure  = hasTablet ? Modulator_Get(csPressure) : 1.0f;
                e.rotation  = hasTablet ? Modulator_Get(csRot)      : 0.5f;
                e.tiltX     = Modulator_Get(csTilt);
                e.tiltY     = Modulator_Get(csVtilt);
                e.velocity  = sp.velocity;
                e.timestamp = GetTime();
                g_inputQueue.AddEntry(e);
            }
            state->currentBrush.Realb.rad_out = origRad;
        } else {
            // Distort / Contrast (existing logic, unchanged)
            UserBrushConfig cfg;
            CaptureBrushConfig(&cfg);
            ModulatorTable mt; Modulator_GetTable(&mt);
            float sv = ResolveModulatedConfig(cfg, state->mode, 0.0f, &mt).spacing;
            float scaledRad = state->currentBrush.Realb.rad_out * worldToTexPx;
            float spacing = scaledRad * 2.0f * sv;
            if (spacing < 2.0f) spacing = 2.0f;
            if (vp->broker) {
                d_RealBrush scaled = state->currentBrush.Realb;
                scaled.rad_out = scaledRad;
                BrushDab ev = {paintPos.x, paintPos.y, paintPos.x, paintPos.y, scaled};
                PushDabSegment(vp->broker, ev.x, ev.y, ev.srcX, ev.srcY, ev.brush, state->mode);
                vp->m_distortLastDabPos = paintPos;
            }
        }
        layersDirty = true;
    } else if (!leftDown && vp->wasMouseDown && state->mode != ePolyStripe) {
        InputEntry ee; memset(&ee, 0, sizeof(ee));
        ee.type = InputEntry::End;
        g_inputQueue.AddEntry(ee);
        vp->strokeEnded = true;
        vp->endLayer = active;
        layersDirty = true;
        vp->wasMouseDown = false;
    }

    if (!leftDown) {
        vp->strokeLen = 0;
    }

    // ── Line tool (ePolyStripe): click → single dab, drag → line ──
    // Press: no Begin entry, just record state. Release: submit entries.
    // Click: Begin+End → handleEnd creates single stamp at start.
    // Drag:  Begin+End (first dab at start) + Begin+Point*3+End (line segment start→end).
    if (state->mode == ePolyStripe && vp->inBounds && active >= 0 && active < LayerStack_Count() && LayerStack_GetRT(active).id > 0) {
        if (leftDown && !vp->wasMouseDown) {
            vp->lineStartPos = canvasPos;
            if (state->undo) state->undo->Snapshot(state, targetSlot);
            vp->wasMouseDown = true;
        }
        if (!leftDown && vp->wasMouseDown) {
            Vector2 start = vp->lineStartPos;
            Vector2 end   = canvasPos;
            float origRad = state->currentBrush.Realb.rad_out;
            state->currentBrush.Realb.rad_out *= worldToTexPx;

            // First dab: Begin+End → handleEnd !m_emittedAny pushes single stamp
            InputEntry be;
            FillBeginEntry(be, state, ePolyStripe, start, adjustedAngle,
                           targetSlot, worldToTexPx, destXform);
            g_inputQueue.AddEntry(be);
            InputEntry ee;
            memset(&ee, 0, sizeof(ee)); ee.type = InputEntry::End;
            g_inputQueue.AddEntry(ee);

            if (Dist2D(start, end) > 1.0f) {
                // Drag: push second Begin+Point*3+End for the line segment
                FillBeginEntry(be, state, ePolyStripe, start, adjustedAngle,
                               targetSlot, worldToTexPx, destXform);
                g_inputQueue.AddEntry(be);
                const float ep = 0.6f;
                Vector2 pts[3] = {end, {end.x+ep, end.y}, {end.x+ep*2, end.y}};
                for (int i = 0; i < 3; i++) {
                    InputEntry pe;
                    memset(&pe, 0, sizeof(pe));
                    pe.type = InputEntry::Point;
                    pe.x = pts[i].x; pe.y = pts[i].y;
                    pe.pressure = 1.0f; pe.rotation = 0.5f;
                    pe.timestamp = GetTime();
                    g_inputQueue.AddEntry(pe);
                }
                memset(&ee, 0, sizeof(ee)); ee.type = InputEntry::End;
                g_inputQueue.AddEntry(ee);
            }

            state->currentBrush.Realb.rad_out = origRad;
            vp->strokeEnded = true;
            vp->endLayer = active;
            layersDirty = true;
            vp->wasMouseDown = false;
        }
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

    // Modulator debug
    {
        char buf[256];
            snprintf(buf, sizeof(buf), "csDir=%.3f  csIdir=%.3f  csCrv=%.3f  csHVdir=%.3f",
                Modulator_Get(csDir), Modulator_Get(csIdir),
                Modulator_Get(csCrv), Modulator_Get(csHVdir));
        DrawText(buf, 10, 28, 14, YELLOW);
    }
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
