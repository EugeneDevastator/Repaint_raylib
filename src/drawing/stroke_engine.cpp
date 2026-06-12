#define _USE_MATH_DEFINES
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

// Shared modulation: applies bpAngle, bpSize, bpHardness, etc. to a brush.
// Caller must set g_modPars.Pars[csDir], csPressure, etc. as desired before calling.
d_RealBrush ModulateBrushParams(const d_RealBrush& brush, float initAngle, int toolMode) {
    float sizeMul = powf(16.0f, BParam_GetValue(&bpSizeMul) / 128.0f - 1.0f);
    d_RealBrush target = brush;
    target.rad_out    = GetModVal(&bpSize) * sizeMul;
    target.radInRatio = GetModVal(&bpHardness);
    target.crv        = GetModVal(&bpCurvature);
    target.opacity    = GetModVal(&bpOpacity);
    target.resangle   = fmodf(initAngle + GetModVal(&bpAngle), 360.0f);
    target.x2y        = GetModVal(&bpScaleRel);
    target.col        = HSLToRGB(GetModVal(&bpQuickHue), GetModVal(&bpQuickSat), GetModVal(&bpQuickLit));
    target.cop        = (toolMode == eSmudge) ? GetModVal(&bpCloneOpacity) : 0.0f;
    target.rad_out   *= sizeMul;
    return target;
}

void StrokeEngine_DrawPreview(RenderTexture2D dstRT, Texture2D brushTex, bool useTexture,
                              const d_RealBrush* baseBrush, int toolMode,
                              float initialAngle, float cx, float cy) {
    float spacingVal = BParam_GetValue(&bpSpacing);
    float radOut = baseBrush->rad_out;
    float segLen = radOut * 3.0f;
    if (segLen < 2.0f) segLen = 2.0f;

    float dirX = 1.0f, dirY = -1.0f;
    float dirLen = sqrtf(dirX * dirX + dirY * dirY);
    dirX /= dirLen; dirY /= dirLen;
    float dirAng = AtanXY(dirX, dirY);

    // Set modulator state exactly as the real stroke does
    float savedPars[csSTOP];
    memcpy(savedPars, g_modPars.Pars, sizeof(float) * csSTOP);
    g_modPars.Pars[csDir]    = RngConv(dirAng, -(float)M_PI, (float)M_PI, 0.0f, 1.0f);
    g_modPars.Pars[csIdir]   = g_modPars.Pars[csDir];
    g_modPars.Pars[csCrv]    = 0.5f;
    g_modPars.Pars[csAcc]    = 1.0f;
    g_modPars.Pars[csHVdir]  = fabsf(dirX);
    g_modPars.Pars[csVel]    = 1.0f;
    g_modPars.Pars[csLenpx]  = 1.0f;
    g_modPars.Pars[csRelang] = 0.5f;
    g_modPars.Pars[csPressure] = 1.0f;

    // Suppress jitter for deterministic preview
    float jitSize    = bpSize.user.jitter;        bpSize.user.jitter = 0;
    float jitHard    = bpHardness.user.jitter;     bpHardness.user.jitter = 0;
    float jitCrv     = bpCurvature.user.jitter;    bpCurvature.user.jitter = 0;
    float jitOpacity = bpOpacity.user.jitter;      bpOpacity.user.jitter = 0;
    float jitAngle   = bpAngle.user.jitter;        bpAngle.user.jitter = 0;
    float jitScale   = bpScaleRel.user.jitter;     bpScaleRel.user.jitter = 0;
    float jitHue     = bpQuickHue.user.jitter;     bpQuickHue.user.jitter = 0;
    float jitSat     = bpQuickSat.user.jitter;     bpQuickSat.user.jitter = 0;
    float jitLit     = bpQuickLit.user.jitter;     bpQuickLit.user.jitter = 0;
    float jitCop     = bpCloneOpacity.user.jitter; bpCloneOpacity.user.jitter = 0;

    // Same modulation as the real stroke (emitSegment line 67-77)
    d_RealBrush modulated = ModulateBrushParams(*baseBrush, initialAngle, toolMode);
    CollapsedBrush cbFull = CollapseBrushParams(modulated, initialAngle, toolMode);
    // Zero jitter ranges for deterministic preview
    cbFull.jitRadOut = cbFull.jitRadIn = cbFull.jitOpacity = cbFull.jitCrv = cbFull.jitX2y = 0;
    cbFull.jitHue = cbFull.jitSat = cbFull.jitLit = cbFull.jitCloneOp = 0;
    cbFull.baseSeed = 0;
    cbFull.spacing = spacingVal;

    // Restore jitter and pars
    bpSize.user.jitter        = jitSize;
    bpHardness.user.jitter    = jitHard;
    bpCurvature.user.jitter   = jitCrv;
    bpOpacity.user.jitter     = jitOpacity;
    bpAngle.user.jitter       = jitAngle;
    bpScaleRel.user.jitter    = jitScale;
    bpQuickHue.user.jitter    = jitHue;
    bpQuickSat.user.jitter    = jitSat;
    bpQuickLit.user.jitter    = jitLit;
    bpCloneOpacity.user.jitter = jitCop;
    memcpy(g_modPars.Pars, savedPars, sizeof(float) * csSTOP);

    CollapsedBrush cbTiny = cbFull;
    cbTiny.rad_out_px = 1.0f;

    // Single stamp at center (same as single-click real stroke)
    SegmentData seed;
    memset(&seed, 0, sizeof(seed));
    seed.pos1 = seed.pos2 = Vector2{cx, cy};
    seed.ctrl0 = seed.ctrl3 = seed.pos1;
    seed.brushFrom = seed.brush = cbFull;
    seed.seed = baseBrush->seed;
    seed.tool = eSingleStamp;
    seed.seamless = g_seamlessPaint ? 1 : 0;
    seed.smudgeSrcX = cx;
    seed.smudgeSrcY = cy;
    seed.initAngle = initialAngle;
    DrawSegment(seed, dstRT, brushTex, useTexture, seed.seamless != 0, 0, false);

    // Path segment with a curve so per-dab rotation modulation is visible
    Vector2 start = {cx, cy};
    Vector2 end   = {cx + segLen * dirX, cy + segLen * dirY};
    Vector2 mid   = {(start.x + end.x) * 0.5f, (start.y + end.y) * 0.5f};
    float perpX = -dirY * segLen * 0.25f, perpY = dirX * segLen * 0.25f;
    Vector2 c0 = {mid.x + perpX, mid.y + perpY};
    Vector2 c3 = {mid.x - perpX, mid.y - perpY};
    SegmentData s;
    memset(&s, 0, sizeof(s));
    s.pos1 = start; s.pos2 = end;
    s.ctrl0 = c0; s.ctrl3 = c3;
    s.brushFrom = cbFull; s.brush = cbTiny;
    s.seed = baseBrush->seed;
    s.tool = (uint8_t)toolMode;
    s.seamless = g_seamlessPaint ? 1 : 0;
    s.smudgeSrcX = cx;
    s.smudgeSrcY = cy;
    s.initAngle = initialAngle;
    DrawSegment(s, dstRT, brushTex, useTexture, s.seamless != 0, 0);
}
