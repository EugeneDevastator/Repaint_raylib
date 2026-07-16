#define _USE_MATH_DEFINES
#include "repaint.h"
#include "replay_recorder.h"
#include "stroke_engine.h"
#include "input_modulator.h"
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

static float ModulateConfigVal(const BPConfig& cfg, const ModulatorTable* mods) {
    float cpar = (cfg.modulatorId >= 0 && cfg.modulatorId < csSTOP)
        ? mods->val[cfg.modulatorId] : 1.0f;
    float minN = powf(cfg.userMin, cfg.power);
    float maxN = powf(cfg.userMax, cfg.power);
    float rng = maxN - minN;
    float respar = cpar * rng + minN;
    float randm = (((float)rand() / (float)RAND_MAX) - 0.5f) * 2.0f * cfg.jitter;
    float res = fminf(fmaxf(respar + randm, 0.0f), 1.0f);
    return res * (cfg.outMax - cfg.outMin) + cfg.outMin;
}

ModulatedBrushConfig ResolveModulatedConfig(const UserBrushConfig& cfg, int toolMode,
                                             float initAngle, const ModulatorTable* mods) {
    float sizeMul = powf(5.0f, ConfigRawVal(cfg.sizeMul) / 128.0f - 1.0f);

    ModulatedBrushConfig out;
    memset(&out, 0, sizeof(out));

    out.radOut       = ModulateConfigVal(cfg.size, mods) * sizeMul;
    out.radInRatio   = fminf(ModulateConfigVal(cfg.hardness, mods), 1.0f);
    out.scaleRel     = ModulateConfigVal(cfg.scaleRel, mods);
    out.resangle     = fmodf(initAngle + ModulateConfigVal(cfg.angle, mods), 360.0f);
    out.opacity      = ModulateConfigVal(cfg.opacity, mods);
    out.crv          = ModulateConfigVal(cfg.curvature, mods);
    out.cop          = (toolMode == eSmudge) ? ModulateConfigVal(cfg.cloneOpacity, mods) : 0.0f;
    out.col          = HSLToRGB(
        ModulateConfigVal(cfg.hue, mods),
        ModulateConfigVal(cfg.sat, mods),
        ModulateConfigVal(cfg.lit, mods));
    out.pwr          = ModulateConfigVal(cfg.power, mods);
    out.perspective  = ModulateConfigVal(cfg.perspective, mods);

    float ts = ModulateConfigVal(cfg.texScale, mods);
    if (g_texScaleMode == 1)
        out.texScale = ts * out.radOut * (WORLD_UNIT_PX / 128.0f);
    else
        out.texScale = ts;

    out.texFeather   = ModulateConfigVal(cfg.texFeather,  mods);
    out.texThresh    = ModulateConfigVal(cfg.texThresh,   mods);
    out.texBlendVal  = ModulateConfigVal(cfg.texBlendVal, mods);

    out.focalOffset  = ModulateConfigVal(cfg.focalOffset, mods);
    out.spacing      = ModulateConfigVal(cfg.spacing, mods);
    out.scatter      = ModulateConfigVal(cfg.scatter, mods);

    out.texBlendMode   = cfg.texBlendMode;
    out.texNoisemode   = cfg.texNoisemode;
    out.texColorMode   = cfg.texColorMode;
    out.texTiling      = cfg.texTiling;
    out.useTexLumAsAlpha = cfg.useTexLumAsAlpha;
    out.bmidx          = cfg.bmidx;
    out.preserveop     = cfg.preserveop;
    out.eraseMode      = cfg.eraseMode;
    out.userTexOriginX = cfg.userTexOriginX;
    out.userTexOriginY = cfg.userTexOriginY;
    out.userTexDirection = cfg.userTexDirection;
    out.baseSeed       = cfg.baseSeed;

    out.jitRadOut  = cfg.size.jitter    * out.radOut;
    out.jitRadIn   = cfg.hardness.jitter;
    out.jitOpacity = cfg.opacity.jitter;
    out.jitCrv     = cfg.curvature.jitter;
    out.jitX2y     = cfg.scaleRel.jitter;
    out.jitHue     = cfg.hue.jitter     * (cfg.hue.outMax - cfg.hue.outMin);
    out.jitSat     = cfg.sat.jitter     * (cfg.sat.outMax - cfg.sat.outMin);
    out.jitLit     = cfg.lit.jitter     * (cfg.lit.outMax - cfg.lit.outMin);
    out.jitCloneOp = cfg.cloneOpacity.jitter;
    out.jitFocal   = cfg.focalOffset.jitter;

    return out;
}

ModulatedBrushConfig ResolveModulatedConfigMax(const UserBrushConfig& cfg, int toolMode, float initAngle) {
    ModulatorTable tbl;
    for (int i = 0; i < csSTOP; i++) tbl.val[i] = 1.0f;
    tbl.val[csDir] = tbl.val[csIdir] = tbl.val[csCrv] = 0.5f;
    tbl.val[csHVdir] = tbl.val[csRelang] = 0.5f;
    return ResolveModulatedConfig(cfg, toolMode, initAngle, &tbl);
}

DabBrush MakeDabBrush(const ModulatedBrushConfig& mod, const float rad_out_px_override) {
    DabBrush cb;
    memset(&cb, 0, sizeof(cb));

    cb.rad_out_px  = rad_out_px_override > 0.0f ? rad_out_px_override : mod.radOut;
    cb.radInRatio  = mod.radInRatio;
    cb.scale_x     = 1.0f;
    cb.scale_y     = mod.scaleRel;
    cb.resangle    = mod.resangle;
    cb.opacity     = mod.opacity;
    cb.crv         = mod.crv;
    cb.cop         = mod.cop;
    cb.col         = mod.col;
    cb.pwr         = mod.pwr;
    cb.bmidx       = mod.bmidx;
    cb.eraseMode   = mod.eraseMode;
    cb.preserveop  = mod.preserveop;
    cb.perspective = mod.perspective;
    cb.texScale    = mod.texScale;
    cb.texFeather  = mod.texFeather;
    cb.texThresh   = mod.texThresh;
    cb.texBlendVal = mod.texBlendVal;
    cb.texBlendMode   = mod.texBlendMode;
    cb.texNoisemode   = mod.texNoisemode;
    cb.texColorMode   = mod.texColorMode;
    cb.texTiling      = mod.texTiling;
    cb.useTexLumAsAlpha = mod.useTexLumAsAlpha;
    cb.userTexOriginX  = mod.userTexOriginX;
    cb.userTexOriginY  = mod.userTexOriginY;
    cb.userTexDirection = mod.userTexDirection;
    cb.baseSeed        = mod.baseSeed;

    cb.focalOffset = mod.focalOffset;
    cb.spacing     = mod.spacing;

    cb.jitRadOut  = mod.jitRadOut;
    cb.jitRadIn   = mod.jitRadIn;
    cb.jitOpacity = mod.jitOpacity;
    cb.jitCrv     = mod.jitCrv;
    cb.jitX2y     = mod.jitX2y;
    cb.jitHue     = mod.jitHue;
    cb.jitSat     = mod.jitSat;
    cb.jitLit     = mod.jitLit;
    cb.jitCloneOp = mod.jitCloneOp;
    cb.jitFocal   = mod.jitFocal;
    cb.scatter    = mod.scatter;

    return cb;
}

int StrokeEngine_GeneratePreviewDabs(const d_RealBrush* baseBrush, int toolMode,
                                     float initialAngle, float cx, float cy,
                                     DabPoint* outBuf, int maxOut) {
    UserBrushConfig cfg;
    CaptureBrushConfig(&cfg);

    float radOut = baseBrush->rad_out;  // already in pixel units (outMax=256)
    float segLen = fmaxf(radOut * 2.0f, 80.0f);
    // Fixed stroke direction — initialAngle only affects brush rotation, not geometry
    float dirX = 1.0f, dirY = -1.0f;
    float dirLen = sqrtf(dirX * dirX + dirY * dirY);
    dirX /= dirLen; dirY /= dirLen;

    cfg.bmidx          = baseBrush->bmidx;
    cfg.texBlendMode   = baseBrush->texBlendMode;
    cfg.texNoisemode   = baseBrush->texNoisemode;
    cfg.texColorMode   = baseBrush->texColorMode;
    cfg.texTiling      = baseBrush->texTiling;
    cfg.useTexLumAsAlpha = baseBrush->useTexLumAsAlpha;
    cfg.preserveop     = baseBrush->preserveop;
    cfg.eraseMode      = baseBrush->eraseMode;
    cfg.userTexOriginX = baseBrush->userTexOriginX;
    cfg.userTexOriginY = baseBrush->userTexOriginY;
    cfg.userTexDirection = baseBrush->userTexDirection;
    cfg.baseSeed       = baseBrush->seed;

    // Curve geometry
    Vector2 start = {cx, cy};
    Vector2 end   = {cx + segLen * dirX, cy + segLen * dirY};
    Vector2 mid   = {(start.x + end.x) * 0.5f, (start.y + end.y) * 0.5f};
    float perpX = -dirY * segLen * 0.25f, perpY = dirX * segLen * 0.25f;
    Vector2 c0 = {mid.x + perpX, mid.y + perpY};
    Vector2 c3 = {mid.x - perpX, mid.y - perpY};

    // Direction at curve start and end — so modulation (esp. angle) follows the stroke
    float startDir = DirAng(c0.x - start.x, c0.y - start.y);
    float endDir   = DirAng(end.x - c3.x, end.y - c3.y);

    // Main brush (start of curve)
    ModulatorTable mt; InputModulator_GetAllSnapshot(&mt);
    mt.val[csDir] = RngConv(startDir, -(float)M_PI, (float)M_PI, 0.0f, 1.0f);
    mt.val[csIdir] = mt.val[csDir];
    ModulatedBrushConfig mod = ResolveModulatedConfig(cfg, toolMode, initialAngle, &mt);
    float spacingVal = mod.spacing;
    mod.spacing = spacingVal;
    DabBrush cbFull = MakeDabBrush(mod, mod.radOut);

    // End brush (1px, with end-direction angle)
    mt.val[csDir] = RngConv(endDir, -(float)M_PI, (float)M_PI, 0.0f, 1.0f);
    mt.val[csIdir] = mt.val[csDir];
    ModulatedBrushConfig modEnd = ResolveModulatedConfig(cfg, toolMode, initialAngle, &mt);
    modEnd.radOut = 1.0f;
    DabBrush cbTiny = MakeDabBrush(modEnd, 1.0f);

    int total = 0;

    // Stamp (uses start brush)
    SegmentData seed;
    memset(&seed, 0, sizeof(seed));
    seed.pos1 = seed.pos2 = Vector2{cx, cy};
    seed.ctrl0 = seed.ctrl3 = seed.pos1;
    seed.brushFrom = seed.brush = cbFull;
    seed.seed = baseBrush->seed;
    seed.tool = eSingleStamp;
    seed.seamless = g_seamlessPaint ? 1 : 0;
    seed.pixelPerfect = g_pixelPerfect ? 1 : 0;
    if (g_pixelPerfect) {
        float firstRad = cbFull.rad_out_px;
        int d0 = (int)(firstRad * 2.0f + 0.5f);
        if (d0 < 1) d0 = 1;
        seed.ppBias = (d0 % 2 == 1) ? 0.5f : 0.0f;
    }
    seed.smudgeSrcX = cx;
    seed.smudgeSrcY = cy;
    seed.initAngle = initialAngle;
    {
        SegResult r;
        int cnt = BuildSegment(seed, 0, 0.0f, outBuf, maxOut, &r);
        total += cnt;
    }

    // Curved stroke (draws over several frames via incremental rendering)
    SegmentData s;
    memset(&s, 0, sizeof(s));
    s.pos1 = start; s.pos2 = end;
    s.ctrl0 = c0; s.ctrl3 = c3;
    s.brushFrom = cbFull; s.brush = cbTiny;
    s.seed = baseBrush->seed;
    s.tool = (uint8_t)toolMode;
    s.seamless = g_seamlessPaint ? 1 : 0;
    s.pixelPerfect = g_pixelPerfect ? 1 : 0;
    if (g_pixelPerfect) {
        float firstRad = cbFull.rad_out_px;
        int d0 = (int)(firstRad * 2.0f + 0.5f);
        if (d0 < 1) d0 = 1;
        s.ppBias = (d0 % 2 == 1) ? 0.5f : 0.0f;
    }
    s.smudgeSrcX = cx;
    s.smudgeSrcY = cy;
    s.initAngle = initialAngle;
    SegResult r;
    int cnt = BuildSegment(s, total, 0.0f, outBuf + total, maxOut - total, &r);
    total += cnt;

    // Per-dab angle fixup — recompute resangle from actual curve direction
    for (int i = 1; i < total; i++) {
        DabPoint& pt = outBuf[i];
        // Direction from previous dab to this dab
        float dx = pt.x - outBuf[i-1].x;
        float dy = pt.y - outBuf[i-1].y;
        float len = sqrtf(dx*dx + dy*dy);
        if (len > 0.001f) {
            float dirAng = DirAng(dx, dy);
            float dirVal = RngConv(dirAng, -(float)M_PI, (float)M_PI, 0.0f, 1.0f);
            // Compute angle modulation using the same formula as ResolveModulatedConfig
            float cpar = (cfg.angle.modulatorId >= 0 && cfg.angle.modulatorId < csSTOP)
                ? dirVal : 1.0f;
            float n = (cfg.angle.power != 1.0f)
                ? powf(cfg.angle.userMax, cfg.angle.power) : cfg.angle.userMax;
            float rng = cfg.angle.userMax - cfg.angle.userMin;
            float respar = cpar * rng + cfg.angle.userMin;
            float res = fminf(fmaxf(respar, 0.0f), 1.0f);
            float angleMod = res * (cfg.angle.outMax - cfg.angle.outMin) + cfg.angle.outMin;
            pt.brush.resangle = fmodf(initialAngle + angleMod, 360.0f);
        }
    }

    return total;
}

// (StrokeEngine_DrawPreview removed — callers use StrokeEngine_GeneratePreviewDabs + incremental render)

