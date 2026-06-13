#include "repaint.h"
#include "raylib.h"

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

d_StrokePars g_modPars;

static float ApplyBP(float clipminF, float clipmaxF, float jitter,
                    float cpar, float outMin, float outMax) {
    float rng = clipmaxF - clipminF;
    float respar = cpar * rng + clipminF;
    float randm = (((float)rand() / (float)RAND_MAX) - 0.5f) * 2.0f * jitter;
    float res = fminf(fmaxf(respar + randm, 0.0f), 1.0f);
    return res * (outMax - outMin) + outMin;
}

float GetModVal(BParam* bp) {
    float cpar = 1.0f;
    int pm = bp->penMode;
    if (pm >= 0 && pm < csSTOP)
        cpar = g_modPars.Pars[pm];
    return ApplyBP(bp->user.clipminF, bp->user.clipmaxF, bp->user.jitter,
                   cpar, bp->outMin, bp->outMax);
}

float GetModValFor(BParam* bp, float cpar) {
    return ApplyBP(bp->run.clipminF, bp->run.clipmaxF, bp->user.jitter,
                   cpar, bp->outMin, bp->outMax);
}

void Modulators_Init(void) {
    BParam_Init(&bpOpacity, 0, "Opacity", 0.0f, 1.0f, 1.0f);
    strncpy(bpOpacity.tooltip, "Overall opacity of the brush stroke", sizeof(bpOpacity.tooltip) - 1);
    BParam_SetIcon(&bpOpacity, "ctlop");

    BParam_Init(&bpSize, 1, "Size", 0.0f, 256.0f, 128.0f);
    strncpy(bpSize.tooltip, "Outer radius of the brush tip in pixels", sizeof(bpSize.tooltip) - 1);
    BParam_SetIcon(&bpSize, "ctlrad");

    BParam_Init(&bpHardness, 2, "Hardness", 0.0f, 1.0f, 0.5f);
    strncpy(bpHardness.tooltip, "Transition sharpness from brush center to edge", sizeof(bpHardness.tooltip) - 1);
    bpHardness.user.clipmaxF = 0.5f;
    BParam_SetIcon(&bpHardness, "ctlrrel");

    BParam_Init(&bpSpacing, 3, "Spacing", 0.0f, 2.0f, 0.3f);
    strncpy(bpSpacing.tooltip, "Distance between successive dabs as fraction of brush diameter", sizeof(bpSpacing.tooltip) - 1);
    BParam_SetIcon(&bpSpacing, "ctlspc");

    BParam_Init(&bpCurvature, 4, "Curve", 0.0f, 1.0f, 0.0f);
    strncpy(bpCurvature.tooltip, "Bias toward center (low) or edge (high) of the brush mask", sizeof(bpCurvature.tooltip) - 1);
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
    strncpy(bpAngle.tooltip, "Modulated offset from base angle (deg). Default 360=no offset. Pen mode = Direction rotates brush along stroke.", sizeof(bpAngle.tooltip) - 1);
    BParam_SetIcon(&bpAngle, "ctlang");

    BParam_Init(&bpScaleRel, 41, "Proportion", 0.0f, 1.0f, 0.8f);
    bpScaleRel.user.clipmaxF = 0.8f;
    strncpy(bpScaleRel.tooltip, "Aspect ratio (0.5=tall, 0.8=slight ellipse, 1.0=circle) — set <1.0 to see rotation", sizeof(bpScaleRel.tooltip) - 1);
    BParam_SetIcon(&bpScaleRel, "ctlscalerel");

    BParam_Init(&bpSizeMul, 42, "SizeMul", 0.0f, 256.0f, 128.0f);
    strncpy(bpSizeMul.tooltip, "Size multiplier: 0=÷16, 128=×1, 256=×16", sizeof(bpSizeMul.tooltip) - 1);
    BParam_SetIcon(&bpSizeMul, "ctlradmul");

    BParam_Init(&bpPower, 43, "Power", 0.0f, 1.0f, 0.0f);
    strncpy(bpPower.tooltip, "Displacement power for the Disp tool (0=no displacement, 1=max)", sizeof(bpPower.tooltip) - 1);

    BParam_Init(&bpPerspective, 44, "Perspective", 0.0f, 1.0f, 0.0f);
    strncpy(bpPerspective.tooltip, "Perspective distortion: rotates brush along Y axis before in-plane rotation", sizeof(bpPerspective.tooltip) - 1);
    BParam_SetIcon(&bpPerspective, "ctlpersp");

    BParam_Init(&bpFocalOffset, 45, "Focal", -1.0f, 1.0f, 0.0f);
    strncpy(bpFocalOffset.tooltip, "Shifts radial gradient convergence point (-1..1, 0=center)", sizeof(bpFocalOffset.tooltip) - 1);

    // Init global modulator defaults
    for (int i = 0; i < csSTOP; i++) g_modPars.Pars[i] = 1.0f;
    g_modPars.Pars[csDir]    = 0.5f;
    g_modPars.Pars[csIdir]   = 0.5f;
    g_modPars.Pars[csCrv]    = 1.0f;
    g_modPars.Pars[csAcc]    = 1.0f;
    g_modPars.Pars[csLenpx]  = 1.0f;
    g_modPars.Pars[csHVdir]  = 0.5f;
    g_modPars.Pars[csRot]    = 0.5f;
    g_modPars.Pars[csTilt]   = 0.5f;
    g_modPars.Pars[csRelang] = 0.5f;
    g_modPars.Pars[csHtilt]  = 0.5f;
    g_modPars.Pars[csVtilt]  = 0.5f;
    g_modPars.Pars[csXtilt]  = 0.5f;
    g_modPars.Pars[csYtilt]  = 0.5f;
    g_modPars.Pars[csPressure] = 1.0f;
    g_modPars.Pars[csHVrot]  = 0.5f;
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
