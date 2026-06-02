#include "repaint.h"
#include "replay_recorder.h"
#include "stroke_engine.h"
#include <math.h>

int g_strokeSmoothingMode = SMOOTH_MODE_SMOOTH;
float g_strokeThrottle = 0.0f;
ICommandBroker* g_broker = nullptr;

CollapsedBrush CollapseBrushParams(const d_RealBrush& b, float initialAngle, int toolMode) {
    CollapsedBrush cb;
    cb.rad_out_px = b.rad_out;
    cb.radInRatio = fminf(b.radInRatio, 1.0f);
    cb.scale_x    = 1.0f;
    cb.scale_y    = b.x2y;
    cb.resangle   = (float)b.resangle;
    cb.opacity    = b.opacity;
    cb.crv        = b.crv;
    cb.cop        = (toolMode == eSmudge) ? b.cop : 0.0f;
    cb.col        = b.col;
    cb.pwr        = b.pwr;
    cb.bmidx      = (int)b.bmidx;
    cb.eraseMode  = b.eraseMode;
    cb.preserveop = b.preserveop;
    cb.perspective = b.perspective;
    cb.texScale   = b.texScale;
    cb.texFeather = b.texFeather;
    cb.texThresh  = b.texThresh;
    cb.texBlendVal = b.texBlendVal;
    cb.texBlendMode = b.texBlendMode;
    cb.texNoisemode = b.texNoisemode;
    cb.texColorMode = b.texColorMode;
    cb.useTexLumAsAlpha = b.useTexLumAsAlpha;
    cb.userTexOriginX = b.userTexOriginX;
    cb.userTexOriginY = b.userTexOriginY;
    cb.userTexDirection = b.userTexDirection;
    cb.spacing = BParam_GetValue(&bpSpacing);
    cb.jitRadOut  = bpSize.user.jitter * b.rad_out;
    cb.jitRadIn   = bpHardness.user.jitter;
    cb.jitOpacity = bpOpacity.user.jitter;
    cb.jitCrv     = bpCurvature.user.jitter;
    cb.jitX2y     = bpScaleRel.user.jitter;
    cb.jitHue     = bpQuickHue.user.jitter * (bpQuickHue.outMax - bpQuickHue.outMin);
    cb.jitSat     = bpQuickSat.user.jitter * (bpQuickSat.outMax - bpQuickSat.outMin);
    cb.jitLit     = bpQuickLit.user.jitter * (bpQuickLit.outMax - bpQuickLit.outMin);
    cb.jitCloneOp = bpCloneOpacity.user.jitter;
    cb.baseSeed   = b.seed;
    return cb;
}

void StrokeEngine_DrawPreview(RenderTexture2D dstRT, Texture2D brushTex,
                              const d_RealBrush* baseBrush, int toolMode,
                              float cx, float cy) {
    float spacingVal = BParam_GetValue(&bpSpacing);
    float radOut = baseBrush->rad_out;
    float segLen = radOut * 3.0f;
    if (segLen < 2.0f) segLen = 2.0f;

    float dirX = 1.0f, dirY = -1.0f;
    float dirLen = sqrtf(dirX * dirX + dirY * dirY);
    dirX /= dirLen; dirY /= dirLen;

    Vector2 start = {cx, cy};
    Vector2 end   = {cx + segLen * dirX, cy + segLen * dirY};

    CollapsedBrush cbFull = CollapseBrushParams(*baseBrush, 0.0f, toolMode);
    cbFull.jitRadOut = cbFull.jitRadIn = cbFull.jitOpacity = cbFull.jitCrv = cbFull.jitX2y = 0;
    cbFull.jitHue = cbFull.jitSat = cbFull.jitLit = cbFull.jitCloneOp = 0;
    cbFull.baseSeed = 0;
    cbFull.spacing = spacingVal;

    CollapsedBrush cbTiny = cbFull;
    cbTiny.rad_out_px = 1.0f;

    DrawSegment seed;
    memset(&seed, 0, sizeof(seed));
    seed.pos1 = seed.pos2 = Vector2{cx, cy};
    seed.ctrl0 = seed.ctrl3 = seed.pos1;
    seed.brushFrom = seed.brush = cbFull;
    seed.seed = baseBrush->seed;
    seed.tool = eSingleStamp;
    seed.seamless = g_seamlessPaint ? 1 : 0;
    seed.smudgeSrcX = cx;
    seed.smudgeSrcY = cy;
    DrawOneSegment(seed, dstRT, brushTex, seed.seamless != 0);

    DrawSegment s;
    memset(&s, 0, sizeof(s));
    s.pos1 = start; s.pos2 = end;
    s.brushFrom = cbFull; s.brush = cbTiny;
    s.seed = baseBrush->seed;
    s.tool = (uint8_t)toolMode;
    s.seamless = g_seamlessPaint ? 1 : 0;
    s.smudgeSrcX = cx;
    s.smudgeSrcY = cy;
    DrawOneSegment(s, dstRT, brushTex, s.seamless != 0);
}
