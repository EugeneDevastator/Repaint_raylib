#include "repaint.h"
#include "compositor.h"
#include "viewport_manager.h"
#include "render_utils.h"
#include "rlgl.h"
#include "external/glad.h"
#include "stroke_engine.h"
#include "brush_blend.h"
#include "../LocalPlayer/StrokeThrottle.h"
#include <math.h>

#define PREVIEW_SZ 512

extern Viewport viewport;
extern bool g_seamlessPreview;

static RenderTexture2D g_previewRT = {0};   // brush preview (strokes on transparent bg)
static unsigned int g_lastPreviewHash = 0;
static int g_lastActiveHud = HUD_NONE;        // force preview redraw on HUD activation

// Cached checkerboard texture for edit-texture backdrop.
// Created on demand, reused across frames — avoids creating + destroying
// a texture inside the draw loop, which could free the GL name before
// the batch flushes and cause the next draw to target a stale slot.
static Texture2D g_editCheckerTex = {0};
static int g_editCheckerW = 0, g_editCheckerH = 0;

// Viewport-resolution RT for crop mode (bypasses canvasView clipping)
static RenderTexture2D g_viewResRT = {0};

static unsigned int ComputeBrushHash(const UserBrushConfig& cfg, TexSlotID texSlot, float initialAngle) {
    #define HF(v) do { float _f = (float)(v); unsigned int _iv; memcpy(&_iv, &_f, sizeof(_iv)); h = h * 33 + _iv; } while(0)
    unsigned int h = 0;
    HF(cfg.toolMode);
    HF(cfg.size.userMax); HF(cfg.size.userMin); HF(cfg.size.outMin); HF(cfg.size.outMax);
    HF(cfg.size.power); HF(cfg.size.modulatorId); HF(cfg.size.jitter);
    HF(cfg.hardness.userMax); HF(cfg.hardness.userMin); HF(cfg.hardness.outMin); HF(cfg.hardness.outMax);
    HF(cfg.hardness.power); HF(cfg.hardness.modulatorId); HF(cfg.hardness.jitter);
    HF(cfg.curvature.userMax); HF(cfg.curvature.userMin); HF(cfg.curvature.outMin); HF(cfg.curvature.outMax);
    HF(cfg.curvature.power); HF(cfg.curvature.modulatorId); HF(cfg.curvature.jitter);
    HF(cfg.opacity.userMax); HF(cfg.opacity.userMin); HF(cfg.opacity.outMin); HF(cfg.opacity.outMax);
    HF(cfg.opacity.power); HF(cfg.opacity.modulatorId); HF(cfg.opacity.jitter);
    HF(cfg.angle.userMax); HF(cfg.angle.userMin); HF(cfg.angle.outMin); HF(cfg.angle.outMax);
    HF(cfg.angle.power); HF(cfg.angle.modulatorId); HF(cfg.angle.jitter);
    HF(cfg.scaleRel.userMax); HF(cfg.scaleRel.userMin); HF(cfg.scaleRel.outMin); HF(cfg.scaleRel.outMax);
    HF(cfg.scaleRel.power); HF(cfg.scaleRel.modulatorId); HF(cfg.scaleRel.jitter);
    HF(cfg.cloneOpacity.userMax); HF(cfg.cloneOpacity.userMin); HF(cfg.cloneOpacity.outMin); HF(cfg.cloneOpacity.outMax);
    HF(cfg.cloneOpacity.power); HF(cfg.cloneOpacity.modulatorId); HF(cfg.cloneOpacity.jitter);
    HF(cfg.hue.userMax); HF(cfg.hue.userMin); HF(cfg.hue.outMin); HF(cfg.hue.outMax);
    HF(cfg.hue.power); HF(cfg.hue.modulatorId); HF(cfg.hue.jitter);
    HF(cfg.sat.userMax); HF(cfg.sat.userMin); HF(cfg.sat.outMin); HF(cfg.sat.outMax);
    HF(cfg.sat.power); HF(cfg.sat.modulatorId); HF(cfg.sat.jitter);
    HF(cfg.lit.userMax); HF(cfg.lit.userMin); HF(cfg.lit.outMin); HF(cfg.lit.outMax);
    HF(cfg.lit.power); HF(cfg.lit.modulatorId); HF(cfg.lit.jitter);
    HF(cfg.texScale.userMax); HF(cfg.texScale.userMin); HF(cfg.texScale.outMin); HF(cfg.texScale.outMax);
    HF(cfg.texScale.power); HF(cfg.texScale.modulatorId); HF(cfg.texScale.jitter);
    HF(cfg.texFeather.userMax); HF(cfg.texFeather.userMin); HF(cfg.texFeather.outMin); HF(cfg.texFeather.outMax);
    HF(cfg.texFeather.power); HF(cfg.texFeather.modulatorId); HF(cfg.texFeather.jitter);
    HF(cfg.texThresh.userMax); HF(cfg.texThresh.userMin); HF(cfg.texThresh.outMin); HF(cfg.texThresh.outMax);
    HF(cfg.texThresh.power); HF(cfg.texThresh.modulatorId); HF(cfg.texThresh.jitter);
    HF(cfg.texBlendVal.userMax); HF(cfg.texBlendVal.userMin); HF(cfg.texBlendVal.outMin); HF(cfg.texBlendVal.outMax);
    HF(cfg.texBlendVal.power); HF(cfg.texBlendVal.modulatorId); HF(cfg.texBlendVal.jitter);
    HF(cfg.power.userMax); HF(cfg.power.userMin); HF(cfg.power.outMin); HF(cfg.power.outMax);
    HF(cfg.power.power); HF(cfg.power.modulatorId); HF(cfg.power.jitter);
    HF(cfg.perspective.userMax); HF(cfg.perspective.userMin); HF(cfg.perspective.outMin); HF(cfg.perspective.outMax);
    HF(cfg.perspective.power); HF(cfg.perspective.modulatorId); HF(cfg.perspective.jitter);
    HF(cfg.focalOffset.userMax); HF(cfg.focalOffset.userMin); HF(cfg.focalOffset.outMin); HF(cfg.focalOffset.outMax);
    HF(cfg.focalOffset.power); HF(cfg.focalOffset.modulatorId); HF(cfg.focalOffset.jitter);
    HF(cfg.sizeMul.userMax); HF(cfg.sizeMul.userMin); HF(cfg.sizeMul.outMin); HF(cfg.sizeMul.outMax);
    HF(cfg.sizeMul.power); HF(cfg.sizeMul.modulatorId); HF(cfg.sizeMul.jitter);
    HF(cfg.spacing.userMax); HF(cfg.spacing.userMin); HF(cfg.spacing.outMin); HF(cfg.spacing.outMax);
    HF(cfg.spacing.power); HF(cfg.spacing.modulatorId); HF(cfg.spacing.jitter);
    HF(cfg.scatter.userMax); HF(cfg.scatter.userMin); HF(cfg.scatter.outMin); HF(cfg.scatter.outMax);
    HF(cfg.scatter.power); HF(cfg.scatter.modulatorId); HF(cfg.scatter.jitter);
    HF(cfg.texBlendMode); HF(cfg.texNoisemode); HF(cfg.texColorMode); HF(cfg.texTiling);
    HF(cfg.useTexLumAsAlpha); HF(cfg.bmidx); HF(cfg.preserveop); HF(cfg.eraseMode);
    HF(cfg.userTexOriginX); HF(cfg.userTexOriginY); HF(cfg.userTexDirection);
    HF(cfg.baseSeed);
    HF(initialAngle);
    HF(texSlot.bucket); HF(texSlot.slot);
    HF(g_seamlessPaint); HF(g_pixelPerfect);
    #undef HF
    return h;
}

static void EnsurePreviewRTs(void) {
    if (g_previewRT.id == 0)
        g_previewRT = Load16BitRT(PREVIEW_SZ, PREVIEW_SZ);
}

// Construct a non-rotated RectXform from two screen-space corners.
// Converts to world coordinates via the camera, so the returned xform
// covers the same world region as the screen rectangle.
static RectXform GetXformFromScreenCorners(float sx1, float sy1, float sx2, float sy2, const Camera2D& cam) {
    Vector2 w1 = GetScreenToWorld2D({sx1, sy1}, cam);
    Vector2 w2 = GetScreenToWorld2D({sx2, sy2}, cam);
    float x = fminf(w1.x, w2.x), y = fminf(w1.y, w2.y);
    float w = fabsf(w2.x - w1.x), h = fabsf(w2.y - w1.y);
    return RectXform_Pivot(x, y, w, h, 0);
}

void ViewportHUD_Draw(AppState* state) {
    int cw = DocOutPxW(&state->doc);
    int ch = DocOutPxH(&state->doc);
    if (cw < 1 || ch < 1) return;

    Rectangle vpBounds = viewport.bounds;
    DrawRectangleRec(vpBounds, Color{55, 55, 55, 255});

    // Edit texture mode
    if (state->editTexMode && TM_IsValid(state->editTexSlot)) {
        TexSlot* bt = TM_Get(state->editTexSlot);
        if (bt && bt->rt.id > 0) {
            int tw = bt->w;
            int th = bt->h;
            float dstX = -state->camera.target.x * state->camera.zoom + state->camera.offset.x;
            float dstY = -state->camera.target.y * state->camera.zoom + state->camera.offset.y;
            float dstW = tw * state->camera.zoom;
            float dstH = th * state->camera.zoom;
            Rectangle dstRect = {dstX, dstY, dstW, dstH};

            // Cached checker backdrop — rebuild only if size changed
            if (g_editCheckerTex.id == 0 || g_editCheckerW != tw || g_editCheckerH != th) {
                if (g_editCheckerTex.id > 0) UnloadTexture(g_editCheckerTex);
                Image img = GenImageColor(tw, th, BLANK);
                for (int y = 0; y < th; y += 8)
                    for (int x = 0; x < tw; x += 8) {
                        bool light = ((x / 8) + (y / 8)) % 2 == 0;
                        Color col = light ? Color{70, 70, 75, 255} : Color{55, 55, 60, 255};
                        ImageDrawRectangle(&img, x, y, 8, 8, col);
                    }
                g_editCheckerTex = LoadTextureFromImage(img);
                UnloadImage(img);
                g_editCheckerW = tw;
                g_editCheckerH = th;
            }
            DrawTexturePro(g_editCheckerTex, Rectangle{0, 0, (float)tw, (float)th},
                dstRect, Vector2{0, 0}, 0.0f, WHITE);
            DrawTexturePro(bt->rt.texture,
                Rectangle{0, 0, (float)tw, (float)-th}, dstRect, Vector2{0, 0}, 0.0f, WHITE);
        }
    }

    bool usePresent = Compositor_PresentInited();
    RenderTexture2D* docBlendTex = NULL;

    // Force composite rebuild before capture when HUD has just opened,
    // so the brush preview sees the latest canvas state.
    if (g_activeHud == HUD_QUICK && g_lastActiveHud != HUD_QUICK)
        ViewportManager_SetDirty();

    if (!state->editTexMode) {

        if (state->framingMode == FRAME_CROP) {
            // Crop mode: render all layers to viewport-sized RT with camera
            // view matrix — no canvasView clipping, full scene visible.
            int vpW = (int)vpBounds.width, vpH = (int)vpBounds.height;
            if (vpW > 0 && vpH > 0) {
                if (g_viewResRT.id == 0 || g_viewResRT.texture.width != (unsigned)vpW || g_viewResRT.texture.height != (unsigned)vpH)
                    g_viewResRT = Load16BitRT(vpW, vpH);
                float vOffX = state->camera.offset.x - vpBounds.x;
                float vOffY = state->camera.offset.y - vpBounds.y;
                RectXform vXf;
                vXf.mat[0]=state->camera.zoom; vXf.mat[1]=0; vXf.mat[2]=-state->camera.target.x*state->camera.zoom+vOffX;
                vXf.mat[3]=0; vXf.mat[4]=state->camera.zoom; vXf.mat[5]=-state->camera.target.y*state->camera.zoom+vOffY;
                vXf.ww=0; vXf.wh=0;
                // Compute checker rect: crop rect AABB in world-space, transformed to viewport pixels
                Rectangle checkerRect = {0,0,0,0};
                {
                    Rectangle cropAABB = GetWorldAABB(&state->cropEntryWindow);
                    float l = vXf.mat[0]*cropAABB.x + vXf.mat[1]*cropAABB.y + vXf.mat[2];
                    float t = vXf.mat[3]*cropAABB.x + vXf.mat[4]*cropAABB.y + vXf.mat[5];
                    float r = vXf.mat[0]*(cropAABB.x+cropAABB.width) + vXf.mat[1]*(cropAABB.y+cropAABB.height) + vXf.mat[2];
                    float b = vXf.mat[3]*(cropAABB.x+cropAABB.width) + vXf.mat[4]*(cropAABB.y+cropAABB.height) + vXf.mat[5];
                    if(r>l&&b>t) checkerRect = {l,t,r-l,b-t};
                }
                // Build dst Quad from viewXform, draw checker, composite layers
                float idMat[6] = {1,0,0,0,1,0};
                float invVxf[6];
                Xform_MulInv(invVxf, idMat, vXf.mat);
                Quad dstQ;
                memcpy(dstQ.xform.mat, invVxf, sizeof(invVxf));
                dstQ.xform.ww = (float)vpW; dstQ.xform.wh = (float)vpH;
                dstQ.rt = g_viewResRT;
                BeginTextureMode(g_viewResRT); ClearBackground(BLANK);
                if(checkerRect.width>0 && checkerRect.height>0) {
                    int cw = LayerStack_RenderW(), ch = LayerStack_RenderH();
                    Compositor_EnsureChecker(cw, ch);
                    Texture2D ck = Compositor_GetCheckerTex();
                    if(ck.id>0) DrawTexturePro(ck, Rectangle{0,0,(float)cw,(float)ch},
                        checkerRect, Vector2{0,0}, 0, WHITE);
                }
                EndTextureMode();
                ViewportManager_CompositeLayersOntoQuad(&dstQ);
                if (usePresent) { BeginShaderMode(Compositor_GetPresentShader()); Compositor_SetPresentTexSize(vpW, vpH); Compositor_SetPresentDither(true); Compositor_SetPresentNearest(true); }
                DrawTextureRec(g_viewResRT.texture,
                    Rectangle{0, 0, (float)vpW, (float)-vpH},
                    Vector2{vpBounds.x, vpBounds.y}, WHITE);
                if (usePresent) EndShaderMode();
                docBlendTex = &g_viewResRT;
            }
        } else {
            // Normal mode: composite at canvas resolution with canvasView
            docBlendTex = ViewportManager_Composite();
            if (!docBlendTex || docBlendTex->id == 0) return;

            glBindTexture(GL_TEXTURE_2D, docBlendTex->texture.id);
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);

            float texW = (float)docBlendTex->texture.width;
            float texH = (float)docBlendTex->texture.height;
            float dstX = -state->camera.target.x * state->camera.zoom + state->camera.offset.x;
            float dstY = -state->camera.target.y * state->camera.zoom + state->camera.offset.y;
            float ww = state->doc.window.ww, wh = state->doc.window.wh;
            float dstW = ww * state->camera.zoom;
            float dstH = wh * state->camera.zoom;
            // Source = full canvas RT (texture resolution). Destination = world
            // region (ww/wh) scaled by zoom. Decoupled: different pixel density
            // gives sharper or pixelated display.
            Rectangle srcRect = {0, 0, texW, -texH};
            Rectangle dstRect = {dstX, dstY, dstW, dstH};

            if (g_seamlessPreview) {
                SetTextureWrap(docBlendTex->texture, TEXTURE_WRAP_REPEAT);
                if (usePresent) { BeginShaderMode(Compositor_GetPresentShader()); Compositor_SetPresentTexSize((int)texW, (int)texH); Compositor_SetPresentDither(true); Compositor_SetPresentNearest(true); }
                for (int dy = -1; dy <= 1; dy++)
                    for (int dx = -1; dx <= 1; dx++)
                        DrawTexturePro(docBlendTex->texture, srcRect,
                            Rectangle{dstX + dx * dstW, dstY + dy * dstH, dstW, dstH},
                            Vector2{0, 0}, 0.0f, WHITE);
                if (usePresent) EndShaderMode();
            } else {
                if (usePresent) { BeginShaderMode(Compositor_GetPresentShader()); Compositor_SetPresentTexSize((int)texW, (int)texH); Compositor_SetPresentDither(true); Compositor_SetPresentNearest(true); }
                DrawTexturePro(docBlendTex->texture, srcRect, dstRect, Vector2{0, 0}, 0.0f, WHITE);
                if (usePresent) EndShaderMode();
            }
        }
    }

    // ── Brush preview overlay (quick HUD) ────────────────────────────
    if (g_activeHud == HUD_QUICK) {
        EnsurePreviewRTs();

        static DabPoint g_previewDabs[4096];
        static int g_previewCount = 0;
        static int g_previewRendered = 0;
        static int g_previewBudget = 50000;
        static uint16_t g_previewBaseSeed = 0;
        UserBrushConfig cfg;
        CaptureBrushConfig(&cfg);
        cfg.toolMode = state->mode;
        {
            const d_RealBrush& br = state->currentBrush.Realb;
            cfg.bmidx          = br.bmidx;
            cfg.texBlendMode   = br.texBlendMode;
            cfg.texNoisemode   = br.texNoisemode;
            cfg.texColorMode   = br.texColorMode;
            cfg.texTiling      = br.texTiling;
            cfg.useTexLumAsAlpha = br.useTexLumAsAlpha;
            cfg.preserveop     = br.preserveop;
            cfg.eraseMode      = br.eraseMode;
            cfg.userTexOriginX = br.userTexOriginX;
            cfg.userTexOriginY = br.userTexOriginY;
            cfg.userTexDirection = br.userTexDirection;
            cfg.baseSeed       = br.seed;
        }
        // Force redraw when HUD_QUICK becomes active (canvas content may have changed)
        if (g_activeHud == HUD_QUICK && g_lastActiveHud != HUD_QUICK) {
            g_lastPreviewHash = 0;
            g_previewRendered = 0;
            g_previewCount = 0;
        }

        unsigned int currentHash = ComputeBrushHash(cfg, state->brushTexSlot, state->initialAngle);
        bool paramsChanged = (currentHash != g_lastPreviewHash);

        if (paramsChanged || g_lastPreviewHash == 0) {
            d_RealBrush zoomBrush = state->currentBrush.Realb;
            if (g_lastPreviewHash == 0) g_previewBaseSeed = zoomBrush.seed;
            zoomBrush.seed = g_previewBaseSeed;

            // Build preview Quad: zoom-dependent screen corners so the world region
            // stays at PREVIEW_SZ units regardless of zoom.
            float pScrSz = (float)PREVIEW_SZ * state->camera.zoom;
            float pX = vpBounds.x + vpBounds.width  * 0.5f - pScrSz * 0.5f;
            float pY = vpBounds.y + vpBounds.height * 0.5f - pScrSz * 0.5f;
            Quad previewQuad;
            previewQuad.xform = GetXformFromScreenCorners(pX, pY, pX + pScrSz, pY + pScrSz, state->camera);
            previewQuad.rt = g_previewRT;

            // Blit canvas composite as background.
            BeginTextureMode(g_previewRT);
            ClearBackground((Color){255,255,255,0});
            if (state->framingMode != FRAME_CROP && docBlendTex) {
                // Compute world region of the preview (from screen corners)
                Vector2 wTL = GetScreenToWorld2D({pX, pY}, state->camera);
                Vector2 wBR = GetScreenToWorld2D({pX + pScrSz, pY + pScrSz}, state->camera);
                float wx = fminf(wTL.x, wBR.x), wy = fminf(wTL.y, wBR.y);
                float ww = fabsf(wBR.x - wTL.x), wh = fabsf(wBR.y - wTL.y);
                // Set camera to show this world region centered in the preview
                Camera2D pcam = { 0 };
                pcam.target = (Vector2){wx + ww*0.5f, wy + wh*0.5f};
                pcam.offset = (Vector2){(float)PREVIEW_SZ*0.5f, (float)PREVIEW_SZ*0.5f};
                pcam.zoom   = (float)PREVIEW_SZ / fmaxf(ww, wh);
                BeginMode2D(pcam);
                float cw = (float)docBlendTex->texture.width;
                float ch = (float)docBlendTex->texture.height;
                float docW = state->doc.window.ww, docH = state->doc.window.wh;
                rlSetBlendMode(RL_BLEND_CUSTOM);
                rlSetBlendFactors(RL_ONE, RL_ZERO, RL_FUNC_ADD);
                DrawTexturePro(docBlendTex->texture,
                    Rectangle{0, 0, cw, -ch},
                    Rectangle{0, 0, docW, docH},
                    Vector2{0, 0}, 0.0f, WHITE);
                rlSetBlendMode(RL_BLEND_ALPHA);
                EndMode2D();
            }
            EndTextureMode();

            // Generate dab points (no rendering)
            g_previewCount = StrokeEngine_GeneratePreviewDabs(&zoomBrush, state->mode,
                state->initialAngle, PREVIEW_SZ * 0.5f, PREVIEW_SZ * 0.5f,
                g_previewDabs, 4096);
            // The dab radius is in world units space. The preview
            // displays the RT at PREVIEW_SZ × zoom screen pixels, so the effective
            // canvas→screen scale is zoom.  Keep the dabs at canvas-pixel size so
            // they match the brush mark already in the composite / canvas texture.
            g_previewRendered = 0;
            // Seed preview budget from the stroke throttle's dynamic budget,
            // so the preview starts at the same performance-adapted level.
            if (g_throttle) g_previewBudget = g_throttle->GetBudget();

            g_lastPreviewHash = currentHash;
        }

        // Render pending dabs incrementally
        if (g_previewRendered < g_previewCount) {
            Texture2D bt = {0};
            bool useTex = false;
            if (TM_IsValid(state->brushTexSlot)) {
                TexSlot* ts = TM_Get(state->brushTexSlot);
                if (ts) { bt = ts->rt.texture; useTex = true; }
            }

            // If total remaining cost fits in budget, render all at once
            int totalRemainingCost = 0;
            int checkMax = g_previewRendered + 100 < g_previewCount ? g_previewRendered + 100 : g_previewCount;
            for (int i = g_previewRendered; i < checkMax; i++) {
                float r = g_previewDabs[i].brush.rad_out_px;
                if (r < 0.5f) r = 0.5f;
                totalRemainingCost += (int)(r * r);
            }
            bool renderAll = (totalRemainingCost <= g_previewBudget);

            BeginTextureMode(g_previewRT);
            double tStart = GetTime();
            int cost = 0;
            while (g_previewRendered < g_previewCount && (renderAll || cost < g_previewBudget)) {
                DabPoint& pt = g_previewDabs[g_previewRendered];
                BrushBlend_ApplyStamp(g_previewRT, pt.brush, bt, useTex,
                    pt.x, pt.y, pt.srcX, pt.srcY, pt.srcRad, pt.srcAngle,
                    g_seamlessPaint ? true : false, g_pixelPerfect ? true : false);
                float r = pt.brush.rad_out_px;
                if (r < 0.5f) r = 0.5f;
                cost += (int)(r * r);
                g_previewRendered++;
            }
            EndTextureMode();

            // Adapt budget
            if (!renderAll) {  // TODO: Could rely on dynamic budget
                double elapsed = GetTime() - tStart;
                if (elapsed > 0.008)
                    g_previewBudget = (int)(g_previewBudget * 0.85f);
                else if (elapsed < 0.002)
                    g_previewBudget = (int)(g_previewBudget * 1.15f);
                if (g_previewBudget < 10000) g_previewBudget = 10000;
                if (g_previewBudget > 8000000) g_previewBudget = 8000000;
            }
        }

        // Display the preview quad — scale by zoom so brush size matches viewport canvas.
        // The zoom cancels GetXformFromScreenCorners' zoom scaling, giving the preview
        // the same world→screen pixel ratio as the canvas viewport.
        float hh = (float)PREVIEW_SZ * state->camera.zoom * 0.5f;
        float px = vpBounds.x + vpBounds.width * 0.5f - hh;
        float py = vpBounds.y + vpBounds.height * 0.5f - hh;
        float dispSz = (float)PREVIEW_SZ * state->camera.zoom;
        DrawTexturePro(g_previewRT.texture,
            Rectangle{0, 0, (float)PREVIEW_SZ, (float)-PREVIEW_SZ},
            Rectangle{px, py, dispSz, dispSz},
            Vector2{0, 0}, 0.0f, WHITE);
    }
    g_lastActiveHud = g_activeHud;
    rlSetBlendMode(RL_BLEND_ALPHA);
    Viewport_DrawDebugOverlays(&viewport, state);
}

void ViewportHUD_Shutdown(void) {
    if (g_previewRT.id > 0) { UnloadRenderTexture(g_previewRT); g_previewRT = RenderTexture2D{0}; }
    if (g_viewResRT.id > 0) { UnloadRenderTexture(g_viewResRT); g_viewResRT = RenderTexture2D{0}; }
    if (g_editCheckerTex.id > 0) { UnloadTexture(g_editCheckerTex); g_editCheckerTex = Texture2D{0}; }
    g_lastPreviewHash = 0;
}
