#include "repaint.h"
#include "raylib.h"
#include "brush_draw.h"

BParam bpOpacity;
BParam bpSize;
BParam bpHardness;
BParam bpSpacing;
BParam bpCurvature;
BParam bpScatter;
BParam bpCloneOpacity;
BParam bpQuickHue;
BParam bpQuickSat;
BParam bpQuickLit;
BParam bpTexScale;
BParam bpTexFeather;
BParam bpTexThresh;
BParam bpTexBlendVal;
BParam bpAngle;
BParam bpScaleRel;
BParam bpSizeMul;
BParam bpPower;
BParam bpPerspective;
BParam bpFocalOffset;

static ModulatorTable g_mods;

void Modulator_Init(void) {
    for (int i = 0; i < 25; i++) g_mods.val[i] = 1.0f;
    g_mods.val[csDir]    = 0.5f;
    g_mods.val[csIdir]   = 0.5f;
    g_mods.val[csCrv]    = 1.0f;
    g_mods.val[csAcc]    = 1.0f;
    g_mods.val[csLenpx]  = 1.0f;
    g_mods.val[csHVdir]  = 0.5f;
    g_mods.val[csRot]    = 0.5f;
    g_mods.val[csTilt]   = 0.5f;
    g_mods.val[csRelang] = 0.5f;
    g_mods.val[csHtilt]  = 0.5f;
    g_mods.val[csVtilt]  = 0.5f;
    g_mods.val[csXtilt]  = 0.5f;
    g_mods.val[csYtilt]  = 0.5f;
    g_mods.val[csPressure] = 1.0f;
    g_mods.val[csHVrot]  = 0.5f;
}

void Modulator_Set(int slot, float val) {
    g_mods.val[slot] = val;
}

float Modulator_Get(int slot) {
    return g_mods.val[slot];
}

void Modulator_GetTable(ModulatorTable* out) {
    memcpy(out->val, g_mods.val, sizeof(g_mods.val));
}

RootModulators Modulator_SnapRoot(void) {
    RootModulators r = {};
    r.pressure = g_mods.val[csPressure];
    r.rotation = g_mods.val[csRot];
    r.tiltX    = g_mods.val[csHtilt];
    r.tiltY    = g_mods.val[csVtilt];
    r.velocity = g_mods.val[csVel];
    // dirX/dirY not stored globally — caller must provide
    return r;
}

void Modulator_ResetStroke(void) {
    g_mods.val[csDir]    = 0.5f;
    g_mods.val[csIdir]   = 0.5f;
    g_mods.val[csCrv]    = 0.5f;
    g_mods.val[csAcc]    = 1.0f;
    g_mods.val[csLenpx]  = 1.0f;
    g_mods.val[csHVdir]  = 0.5f;
    g_mods.val[csRelang] = 0.5f;
    g_mods.val[csVel]    = 1.0f;
    g_mods.val[csPressure] = 1.0f;
}

void Modulator_Restore(const ModulatorTable* saved) {
    memcpy(g_mods.val, saved->val, sizeof(g_mods.val));
}

void Modulators_Init(void) {
    Modulator_Init();
    BParam_Init(&bpOpacity, 0, "Opacity", 0.0f, 1.0f, 1.0f);
    bpOpacity.power = 2.0f;
    strncpy(bpOpacity.tooltip, "Overall opacity of the brush stroke", sizeof(bpOpacity.tooltip) - 1);
    BParam_SetIcon(&bpOpacity, "ctlop");

    BParam_Init(&bpSize, 1, "Size", 0.0f, 1.0f, 0.5f);
    strncpy(bpSize.tooltip, "Outer radius of the brush tip in world units (256px per wu)", sizeof(bpSize.tooltip) - 1);
    BParam_SetIcon(&bpSize, "ctlrad");

    BParam_Init(&bpHardness, 2, "Inner Radius", 0.0f, 1.0f, 0.5f);
    strncpy(bpHardness.tooltip, "Inner radius, as ratio", sizeof(bpHardness.tooltip) - 1);
    bpHardness.user.clipmaxF = 0.5f;
    BParam_SetIcon(&bpHardness, "ctlrrel");

    BParam_Init(&bpSpacing, 3, "Spacing", 0.0f, 2.0f, 0.3f);
    bpSpacing.power = 2.0f;
    bpSpacing.defClipmaxF = sqrtf((0.3f - 0.0f) / (2.0f - 0.0f));
    bpSpacing.user.clipmaxF = bpSpacing.defClipmaxF;
    bpSpacing.run.clipmaxF = bpSpacing.defClipmaxF;
    strncpy(bpSpacing.tooltip, "Distance between dabs. 1 = dabs touch outer edge", sizeof(bpSpacing.tooltip) - 1);
    BParam_SetIcon(&bpSpacing, "ctlspc");

    BParam_Init(&bpCurvature, 4, "Curve", 0.0f, 1.0f, 0.0f);
    strncpy(bpCurvature.tooltip, "Exponent/Curvature of the gradient ramp", sizeof(bpCurvature.tooltip) - 1);
    BParam_SetIcon(&bpCurvature, "ctlcrv");

    BParam_Init(&bpScatter, 5, "Scatter", 0.0f, 5.0f, 0.0f);
    strncpy(bpScatter.tooltip, "Random jitter of dab position perpendicular to stroke direction", sizeof(bpScatter.tooltip) - 1);
    BParam_SetIcon(&bpScatter, "ctlspcjit");

    BParam_Init(&bpCloneOpacity, 6, "Clone", 0.7f, 1.0f, 1.0f);
    strncpy(bpCloneOpacity.tooltip, "Smudge/clone source opacity", sizeof(bpCloneOpacity.tooltip) - 1);
    BParam_SetIcon(&bpCloneOpacity, "ctlcop");

    BParam_Init(&bpQuickHue, 10, "Hue", 0.0f, 1.0f, 0.35f);
    strncpy(bpQuickHue.tooltip, "Color hue (0=red, 0.33=green, 0.66=blue)", sizeof(bpQuickHue.tooltip) - 1);
    bpQuickHue.user.clipmaxF = 0.35f;
    bpQuickHue.slider.colorMode = 0;
    BParam_SetIcon(&bpQuickHue, "ctlhue");

    BParam_Init(&bpQuickSat, 11, "Sat", 0.0f, 1.0f, 1.0f);
    strncpy(bpQuickSat.tooltip, "Color saturation (0=gray, 1=full)", sizeof(bpQuickSat.tooltip) - 1);
    bpQuickSat.user.clipmaxF = 1.0f;
    bpQuickSat.slider.colorMode = 1;
    BParam_SetIcon(&bpQuickSat, "ctlsat");

    BParam_Init(&bpQuickLit, 12, "Lit", 0.0f, 1.0f, 0.5f);
    strncpy(bpQuickLit.tooltip, "Color lightness (0=dark, 1=light)", sizeof(bpQuickLit.tooltip) - 1);
    bpQuickLit.user.clipmaxF = 0.5f;
    bpQuickLit.slider.colorMode = 2;
    BParam_SetIcon(&bpQuickLit, "ctllit");

    BParam_Init(&bpTexScale, 30, "Scale", 0.1f, 5.0f, 1.0f);
    strncpy(bpTexScale.tooltip, "Texture pattern scale multiplier", sizeof(bpTexScale.tooltip) - 1);
    BParam_SetIcon(&bpTexScale, "ctlscale");

    BParam_Init(&bpTexFeather, 31, "Feather", 0.0f, 1.0f, 0.05f);
    strncpy(bpTexFeather.tooltip, "Softness of the threshold mask edge", sizeof(bpTexFeather.tooltip) - 1);
    BParam_SetIcon(&bpTexFeather, "ctlfeather");

    BParam_Init(&bpTexThresh, 32, "Thresh Mul", -1.0f, 1.0f, 0.0f);
    strncpy(bpTexThresh.tooltip, "Threshold multiplier; negative inverts texture mask", sizeof(bpTexThresh.tooltip) - 1);
    BParam_SetIcon(&bpTexThresh, "ctltresh");

    BParam_Init(&bpTexBlendVal, 33, "TexColorBlendStrength", 0.0f, 1.0f, 0.5f);
    strncpy(bpTexBlendVal.tooltip, "How much brush color tints the texture (0=texture only, 1=texture*brush)", sizeof(bpTexBlendVal.tooltip) - 1);

    BParam_Init(&bpAngle, 40, "Angle", 0.0f, 360.0f, 360.0f);
    strncpy(bpAngle.tooltip, "Offset from base angle (deg). Default 360=no offset. Direction Modulation rotates brush along stroke.", sizeof(bpAngle.tooltip) - 1);
    BParam_SetIcon(&bpAngle, "ctlang");

    BParam_Init(&bpScaleRel, 41, "Proportion", 0.0f, 1.0f, 0.8f);
    bpScaleRel.user.clipmaxF = 0.8f;
    strncpy(bpScaleRel.tooltip, "Aspect ratio (0.5=tall, 0.8=slight ellipse, 1.0=circle) — set <1.0 to see rotation", sizeof(bpScaleRel.tooltip) - 1);
    BParam_SetIcon(&bpScaleRel, "ctlscalerel");

    BParam_Init(&bpSizeMul, 42, "SizeMul", 0.0f, 256.0f, 128.0f);
    strncpy(bpSizeMul.tooltip, "Size multiplier: 0=÷5, 128=×1, 256=×5", sizeof(bpSizeMul.tooltip) - 1);
    BParam_SetIcon(&bpSizeMul, "ctlradmul");

    BParam_Init(&bpPower, 43, "Power", 0.0f, 1.0f, 0.0f);
    strncpy(bpPower.tooltip, "Displacement power for the Disp tool (0=no displacement, 1=max)", sizeof(bpPower.tooltip) - 1);
    BParam_SetIcon(&bpPower, "ctlpwr");

    BParam_Init(&bpPerspective, 44, "Perspective", 0.0f, 1.0f, 0.0f);
    strncpy(bpPerspective.tooltip, "Perspective distortion: rotates brush along Y axis before in-plane rotation", sizeof(bpPerspective.tooltip) - 1);
    BParam_SetIcon(&bpPerspective, "ctlpersp");

    BParam_Init(&bpFocalOffset, 45, "Focal", -1.0f, 1.0f, 0.0f);
    strncpy(bpFocalOffset.tooltip, "Shifts radial gradient convergence point (-1..1, 0=center)", sizeof(bpFocalOffset.tooltip) - 1);
    BParam_SetIcon(&bpFocalOffset, "ctlfocal");
}

void Modulators_SnapRunState(void) {
    BParam_SnapRunState(&bpOpacity);
    BParam_SnapRunState(&bpSize);
    BParam_SnapRunState(&bpHardness);
    BParam_SnapRunState(&bpSpacing);
    BParam_SnapRunState(&bpCurvature);
    BParam_SnapRunState(&bpScatter);
    BParam_SnapRunState(&bpCloneOpacity);
    BParam_SnapRunState(&bpQuickHue);
    BParam_SnapRunState(&bpQuickSat);
    BParam_SnapRunState(&bpQuickLit);
    BParam_SnapRunState(&bpAngle);
    BParam_SnapRunState(&bpScaleRel);
    BParam_SnapRunState(&bpSizeMul);
}

void Modulators_Shutdown(void) {
    if (bpOpacity.iconLoaded) UnloadTexture(bpOpacity.iconTex);
    if (bpSize.iconLoaded) UnloadTexture(bpSize.iconTex);
    if (bpHardness.iconLoaded) UnloadTexture(bpHardness.iconTex);
    if (bpSpacing.iconLoaded) UnloadTexture(bpSpacing.iconTex);
    if (bpCurvature.iconLoaded) UnloadTexture(bpCurvature.iconTex);
    if (bpScatter.iconLoaded) UnloadTexture(bpScatter.iconTex);
    if (bpCloneOpacity.iconLoaded) UnloadTexture(bpCloneOpacity.iconTex);
}

static void CaptureBP(const BParam& src, BPConfig* dst) {
    dst->userMax     = src.user.clipmaxF;
    dst->userMin     = src.user.clipminF;
    dst->outMin      = src.outMin;
    dst->outMax      = src.outMax;
    dst->power       = src.power;
    dst->modulatorId = src.penMode;
    dst->jitter      = src.user.jitter;
}

void CaptureBrushConfig(UserBrushConfig* cfg) {
    memset(cfg, 0, sizeof(*cfg));
    CaptureBP(bpSize,        &cfg->size);
    CaptureBP(bpHardness,    &cfg->hardness);
    CaptureBP(bpCurvature,   &cfg->curvature);
    CaptureBP(bpOpacity,     &cfg->opacity);
    CaptureBP(bpAngle,       &cfg->angle);
    CaptureBP(bpScaleRel,    &cfg->scaleRel);
    CaptureBP(bpCloneOpacity,&cfg->cloneOpacity);
    CaptureBP(bpQuickHue,    &cfg->hue);
    CaptureBP(bpQuickSat,    &cfg->sat);
    CaptureBP(bpQuickLit,    &cfg->lit);
    CaptureBP(bpTexScale,    &cfg->texScale);
    CaptureBP(bpTexFeather,  &cfg->texFeather);
    CaptureBP(bpTexThresh,   &cfg->texThresh);
    CaptureBP(bpTexBlendVal, &cfg->texBlendVal);
    CaptureBP(bpPower,       &cfg->power);
    CaptureBP(bpPerspective, &cfg->perspective);
    CaptureBP(bpFocalOffset, &cfg->focalOffset);
    CaptureBP(bpSizeMul,     &cfg->sizeMul);
    CaptureBP(bpSpacing,     &cfg->spacing);
    CaptureBP(bpScatter,     &cfg->scatter);
}

void FillSliderMods(const UserBrushConfig& cfg, uint8_t mods[20]) {
    mods[0]  = (uint8_t)cfg.size.modulatorId;
    mods[1]  = (uint8_t)cfg.sizeMul.modulatorId;
    mods[2]  = (uint8_t)cfg.hardness.modulatorId;
    mods[3]  = (uint8_t)cfg.curvature.modulatorId;
    mods[4]  = (uint8_t)cfg.spacing.modulatorId;
    mods[5]  = (uint8_t)cfg.opacity.modulatorId;
    mods[6]  = (uint8_t)cfg.angle.modulatorId;
    mods[7]  = (uint8_t)cfg.scaleRel.modulatorId;
    mods[8]  = (uint8_t)cfg.cloneOpacity.modulatorId;
    mods[9]  = (uint8_t)cfg.scatter.modulatorId;
    mods[10] = (uint8_t)cfg.power.modulatorId;
    mods[11] = (uint8_t)cfg.perspective.modulatorId;
    mods[12] = (uint8_t)cfg.hue.modulatorId;
    mods[13] = (uint8_t)cfg.sat.modulatorId;
    mods[14] = (uint8_t)cfg.lit.modulatorId;
    mods[15] = (uint8_t)cfg.texScale.modulatorId;
    mods[16] = (uint8_t)cfg.texFeather.modulatorId;
    mods[17] = (uint8_t)cfg.texThresh.modulatorId;
    mods[18] = (uint8_t)cfg.texBlendVal.modulatorId;
    mods[19] = (uint8_t)cfg.focalOffset.modulatorId;
}
