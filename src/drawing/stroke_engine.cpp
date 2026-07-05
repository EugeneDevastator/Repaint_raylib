#define _USE_MATH_DEFINES
#include "repaint.h"
#include "replay_recorder.h"
#include "stroke_engine.h"
#include <math.h>
#include <string.h>

int g_strokeSmoothingMode = SMOOTH_MODE_SMOOTH;
float g_strokeThrottle = 0.0f;
ICommandBroker* g_broker = nullptr;

static float ConfigRawVal(const BPConfig& cfg) {
    float n = (cfg.power != 1.0f)
        ? powf(cfg.userMax, cfg.power) : cfg.userMax;
    return n * (cfg.outMax - cfg.outMin) + cfg.outMin;
}

static float ModulateConfigVal(const BPConfig& cfg, const float modValues[csSTOP]) {
    float cpar = (cfg.modulatorId >= 0 && cfg.modulatorId < csSTOP)
        ? modValues[cfg.modulatorId] : 1.0f;
    float n = (cfg.power != 1.0f)
        ? powf(cfg.userMax, cfg.power) : cfg.userMax;
    float rng = cfg.userMax - cfg.userMin;
    float respar = cpar * rng + cfg.userMin;
    float randm = (((float)rand() / (float)RAND_MAX) - 0.5f) * 2.0f * cfg.jitter;
    float res = fminf(fmaxf(respar + randm, 0.0f), 1.0f);
    return res * (cfg.outMax - cfg.outMin) + cfg.outMin;
}

void ResolveBrushParams(const UserBrushConfig& cfg, d_RealBrush* out, int toolMode,
                         float initAngle, const float modValues[csSTOP]) {
    float sizeMul = powf(5.0f, ConfigRawVal(cfg.sizeMul) / 128.0f - 1.0f);

    out->rad_out    = ModulateConfigVal(cfg.size, modValues) * sizeMul;
    out->radInRatio = fminf(ModulateConfigVal(cfg.hardness, modValues), 1.0f);
    out->x2y        = ModulateConfigVal(cfg.scaleRel, modValues);
    out->resangle   = fmodf(initAngle + ModulateConfigVal(cfg.angle, modValues), 360.0f);
    out->opacity    = ModulateConfigVal(cfg.opacity, modValues);
    out->crv        = ModulateConfigVal(cfg.curvature, modValues);
    out->cop        = (toolMode == eSmudge) ? ModulateConfigVal(cfg.cloneOpacity, modValues) : 0.0f;
    out->col        = HSLToRGB(
        ModulateConfigVal(cfg.hue, modValues),
        ModulateConfigVal(cfg.sat, modValues),
        ModulateConfigVal(cfg.lit, modValues));
    out->pwr        = ModulateConfigVal(cfg.power, modValues);
    out->perspective = ModulateConfigVal(cfg.perspective, modValues);

    float ts = ModulateConfigVal(cfg.texScale, modValues);
    if (g_texScaleMode == 1)
        out->texScale = ts * out->rad_out * (WORLD_UNIT_PX / 128.0f);
    else
        out->texScale = ts;

    out->texFeather  = ModulateConfigVal(cfg.texFeather,  modValues);
    out->texThresh   = ModulateConfigVal(cfg.texThresh,   modValues);
    out->texBlendVal = ModulateConfigVal(cfg.texBlendVal, modValues);
}

void ResolveBrushParamsMax(const UserBrushConfig& cfg, d_RealBrush* out,
                           int toolMode, float initAngle) {
    float maxPars[csSTOP];
    for (int i = 0; i < csSTOP; i++) maxPars[i] = 1.0f;
    maxPars[csDir] = maxPars[csIdir] = maxPars[csCrv] = 0.5f;
    maxPars[csHVdir] = maxPars[csRelang] = 0.5f;
    ResolveBrushParams(cfg, out, toolMode, initAngle, maxPars);
}

DabBrush MakeDabBrush(const UserBrushConfig& cfg, const d_RealBrush& resolved) {
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

    cb.focalOffset = ModulateConfigVal(cfg.focalOffset, g_modPars.Pars);
    cb.spacing     = ConfigRawVal(cfg.spacing);

    cb.jitRadOut  = cfg.size.jitter    * cb.rad_out_px;
    cb.jitRadIn   = cfg.hardness.jitter;
    cb.jitOpacity = cfg.opacity.jitter;
    cb.jitCrv     = cfg.curvature.jitter;
    cb.jitX2y     = cfg.scaleRel.jitter;
    cb.jitHue     = cfg.hue.jitter     * (cfg.hue.outMax - cfg.hue.outMin);
    cb.jitSat     = cfg.sat.jitter     * (cfg.sat.outMax - cfg.sat.outMin);
    cb.jitLit     = cfg.lit.jitter     * (cfg.lit.outMax - cfg.lit.outMin);
    cb.jitCloneOp = cfg.cloneOpacity.jitter;
    cb.jitFocal   = cfg.focalOffset.jitter;

    return cb;
}

void StrokeEngine_DrawPreview(RenderTexture2D dstRT, Texture2D brushTex, bool useTexture,
                              const d_RealBrush* baseBrush, int toolMode,
                              float initialAngle, float cx, float cy) {
    UserBrushConfig previewCfg;
    CaptureBrushConfig(&previewCfg);

    float spacingVal = ConfigRawVal(previewCfg.spacing);
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

    // Zero jitter for deterministic preview
    previewCfg.size.jitter = 0;
    previewCfg.hardness.jitter = 0;
    previewCfg.curvature.jitter = 0;
    previewCfg.opacity.jitter = 0;
    previewCfg.angle.jitter = 0;
    previewCfg.scaleRel.jitter = 0;
    previewCfg.hue.jitter = 0;
    previewCfg.sat.jitter = 0;
    previewCfg.lit.jitter = 0;
    previewCfg.cloneOpacity.jitter = 0;
    previewCfg.focalOffset.jitter = 0;

    d_RealBrush previewBrush = *baseBrush;
    ResolveBrushParams(previewCfg, &previewBrush, toolMode, initialAngle, g_modPars.Pars);
    previewBrush.rad_out *= WORLD_UNIT_PX;
    DabBrush cbFull = MakeDabBrush(previewCfg, previewBrush);
    cbFull.jitRadOut = cbFull.jitRadIn = cbFull.jitOpacity = cbFull.jitCrv = cbFull.jitX2y = 0;
    cbFull.jitHue = cbFull.jitSat = cbFull.jitLit = cbFull.jitCloneOp = cbFull.jitFocal = 0;
    cbFull.baseSeed = 0;
    cbFull.spacing = spacingVal;

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
