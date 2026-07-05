#define _USE_MATH_DEFINES
#include "repaint.h"
#include "replay_recorder.h"
#include "stroke_engine.h"
#include <math.h>
#include <string.h>

int g_strokeSmoothingMode = SMOOTH_MODE_SMOOTH;
float g_strokeThrottle = 0.0f;
ICommandBroker* g_broker = nullptr;

void ResolveBrushParams(d_RealBrush* out, int toolMode, float initAngle, const float modValues[csSTOP]) {
    float savedPars[csSTOP];
    memcpy(savedPars, g_modPars.Pars, sizeof(float) * csSTOP);
    memcpy(g_modPars.Pars, modValues, sizeof(float) * csSTOP);

    float sizeMul = powf(5.0f, BParam_GetValue(&bpSizeMul) / 128.0f - 1.0f);

    out->rad_out    = GetModVal(&bpSize) * sizeMul;
    out->radInRatio = fminf(GetModVal(&bpHardness), 1.0f);
    out->x2y        = GetModVal(&bpScaleRel);
    out->resangle   = fmodf(initAngle + GetModVal(&bpAngle), 360.0f);
    out->opacity    = GetModVal(&bpOpacity);
    out->crv        = GetModVal(&bpCurvature);
    out->cop        = (toolMode == eSmudge) ? GetModVal(&bpCloneOpacity) : 0.0f;
    out->col        = HSLToRGB(GetModVal(&bpQuickHue), GetModVal(&bpQuickSat), GetModVal(&bpQuickLit));
    out->pwr        = GetModVal(&bpPower);
    out->perspective = GetModVal(&bpPerspective);

    float ts = GetModVal(&bpTexScale);
    if (g_texScaleMode == 1)
        out->texScale = ts * out->rad_out * (WORLD_UNIT_PX / 128.0f);
    else
        out->texScale = ts;

    out->texFeather  = GetModVal(&bpTexFeather);
    out->texThresh   = GetModVal(&bpTexThresh);
    out->texBlendVal = GetModVal(&bpTexBlendVal);

    memcpy(g_modPars.Pars, savedPars, sizeof(float) * csSTOP);
}

void ResolveBrushParamsMax(d_RealBrush* out, int toolMode, float initAngle) {
    float maxPars[csSTOP];
    for (int i = 0; i < csSTOP; i++) maxPars[i] = 1.0f;
    maxPars[csDir] = maxPars[csIdir] = maxPars[csCrv] = 0.5f;
    maxPars[csHVdir] = maxPars[csRelang] = 0.5f;
    ResolveBrushParams(out, toolMode, initAngle, maxPars);
}

DabBrush MakeDabBrush(const d_RealBrush& resolved) {
    DabBrush cb;
    memset(&cb, 0, sizeof(cb));

    cb.rad_out_px  = resolved.rad_out;
    cb.radInRatio  = fminf(resolved.radInRatio, 1.0f);
    cb.scale_x     = 1.0f;
    cb.scale_y     = resolved.x2y;
    cb.resangle    = (float)resolved.resangle;
    cb.opacity     = resolved.opacity;
    cb.crv         = resolved.crv;
    cb.cop         = resolved.cop;
    cb.col         = resolved.col;
    cb.pwr         = resolved.pwr;
    cb.bmidx       = (int)resolved.bmidx;
    cb.eraseMode   = resolved.eraseMode;
    cb.preserveop  = resolved.preserveop;
    cb.perspective = resolved.perspective;
    cb.texScale    = resolved.texScale;
    cb.texFeather  = resolved.texFeather;
    cb.texThresh   = resolved.texThresh;
    cb.texBlendVal = resolved.texBlendVal;
    cb.texBlendMode   = resolved.texBlendMode;
    cb.texNoisemode   = resolved.texNoisemode;
    cb.texColorMode   = resolved.texColorMode;
    cb.useTexLumAsAlpha = resolved.useTexLumAsAlpha;
    cb.userTexOriginX  = resolved.userTexOriginX;
    cb.userTexOriginY  = resolved.userTexOriginY;
    cb.userTexDirection = resolved.userTexDirection;
    cb.baseSeed        = resolved.seed;

    cb.focalOffset = GetModVal(&bpFocalOffset);
    cb.spacing     = BParam_GetValue(&bpSpacing);

    cb.jitRadOut   = bpSize.user.jitter * cb.rad_out_px;
    cb.jitRadIn    = bpHardness.user.jitter;
    cb.jitOpacity  = bpOpacity.user.jitter;
    cb.jitCrv      = bpCurvature.user.jitter;
    cb.jitX2y      = bpScaleRel.user.jitter;
    cb.jitHue      = bpQuickHue.user.jitter * (bpQuickHue.outMax - bpQuickHue.outMin);
    cb.jitSat      = bpQuickSat.user.jitter * (bpQuickSat.outMax - bpQuickSat.outMin);
    cb.jitLit      = bpQuickLit.user.jitter * (bpQuickLit.outMax - bpQuickLit.outMin);
    cb.jitCloneOp  = bpCloneOpacity.user.jitter;
    cb.jitFocal    = bpFocalOffset.user.jitter;

    return cb;
}

void StrokeEngine_DrawPreview(RenderTexture2D dstRT, Texture2D brushTex, bool useTexture,
                              const d_RealBrush* baseBrush, int toolMode,
                              float initialAngle, float cx, float cy) {
    float spacingVal = BParam_GetValue(&bpSpacing);
    float radOut = baseBrush->rad_out * WORLD_UNIT_PX;
    float segLen = radOut * 3.0f;
    if (segLen < 2.0f) segLen = 2.0f;

    float dirX = 1.0f, dirY = -1.0f;
    float dirLen = sqrtf(dirX * dirX + dirY * dirY);
    dirX /= dirLen; dirY /= dirLen;
    float dirAng = AtanXY(dirX, dirY);

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
    float jitFocal   = bpFocalOffset.user.jitter;  bpFocalOffset.user.jitter = 0;

    d_RealBrush previewBrush = *baseBrush;
    ResolveBrushParams(&previewBrush, toolMode, initialAngle, g_modPars.Pars);
    previewBrush.rad_out *= WORLD_UNIT_PX;
    DabBrush cbFull = MakeDabBrush(previewBrush);
    cbFull.jitRadOut = cbFull.jitRadIn = cbFull.jitOpacity = cbFull.jitCrv = cbFull.jitX2y = 0;
    cbFull.jitHue = cbFull.jitSat = cbFull.jitLit = cbFull.jitCloneOp = cbFull.jitFocal = 0;
    cbFull.baseSeed = 0;
    cbFull.spacing = spacingVal;

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
    bpFocalOffset.user.jitter  = jitFocal;
    memcpy(g_modPars.Pars, savedPars, sizeof(float) * csSTOP);

    DabBrush cbTiny = cbFull;
    cbTiny.rad_out_px = 1.0f;

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
    DrawSegment(seed, dstRT, brushTex, useTexture, seed.seamless != 0, 0, g_pixelPerfect);

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
    DrawSegment(s, dstRT, brushTex, useTexture, s.seamless != 0, 0, g_pixelPerfect);
}
