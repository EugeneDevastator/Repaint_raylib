#include "repaint.h"
#include "rlgl.h"

static Shader brushBlendShader = {0};

static int locRadIn = -1, locRadOut = -1, locOpacity = -1;
static int locRectBounds = -1;
static int locX2Y = -1, locResAngle = -1;
static int locUseTex = -1, locUserMaskTex = -1;
static int locMaskMode = -1, locMaskMix = -1;
static int locTexScale = -1, locTexFeather = -1;
static bool brushBlendInited = false;

Texture2D g_activeBrushTex = {0};

static RenderTexture2D canvasCopyRT = {0};
static int canvasCopyW = 0, canvasCopyH = 0;

void BrushBlend_Init(void) {
    if (brushBlendInited) return;

    const char* ad = GetApplicationDirectory();
    char vsPath[512], fsPath[512];

    snprintf(vsPath, sizeof(vsPath), "%sshaders/brush_blend.vs", ad);
    snprintf(fsPath, sizeof(fsPath), "%sshaders/brush_blend.fs", ad);
    brushBlendShader = LoadShader(vsPath, fsPath);
    if (brushBlendShader.id == 0) { printf("BRUSH BLEND SHADER FAILED\n"); return; }

    locRadIn      = GetShaderLocation(brushBlendShader, "radIn");
    locRadOut     = GetShaderLocation(brushBlendShader, "radOut");
    locOpacity    = GetShaderLocation(brushBlendShader, "opacity");
    locX2Y        = GetShaderLocation(brushBlendShader, "x2y");
    locResAngle   = GetShaderLocation(brushBlendShader, "resangle");
    locRectBounds = GetShaderLocation(brushBlendShader, "rectBounds");
    locUseTex      = GetShaderLocation(brushBlendShader, "useTex");
    locUserMaskTex = GetShaderLocation(brushBlendShader, "userMaskTex");
    locMaskMode    = GetShaderLocation(brushBlendShader, "maskMode");
    locMaskMix     = GetShaderLocation(brushBlendShader, "maskMix");
    locTexScale    = GetShaderLocation(brushBlendShader, "texScale");
    locTexFeather  = GetShaderLocation(brushBlendShader, "texFeather");

    printf("BrushBlend: locUseTex=%d locUserMaskTex=%d locMaskMode=%d locMaskMix=%d locTexScale=%d locTexFeather=%d\n",
        locUseTex, locUserMaskTex, locMaskMode, locMaskMix, locTexScale, locTexFeather);

    brushBlendInited = true;
}

void BrushBlend_Shutdown(void) {
    if (!brushBlendInited) return;
    UnloadShader(brushBlendShader);
    if (canvasCopyRT.id > 0) UnloadRenderTexture(canvasCopyRT);
    canvasCopyRT = RenderTexture2D{0};
    brushBlendInited = false;
}

void BrushBlend_ApplyStamp(
    RenderTexture2D dstRT,
    d_Brush* brush,
    Texture2D brushTex,
    float stampX, float stampY,
    float srcX,   float srcY
) {
    (void)srcX;
    (void)srcY;

    if (!brushBlendInited) return;
    if (dstRT.id == 0) return;

    int canvasW = dstRT.texture.width;
    int canvasH = dstRT.texture.height;

    float radOut = brush->Realb.rad_out;
    if (radOut < 0.5f) radOut = 0.5f;

    // ── Pass 1: Copy canvas ─────────────────────────────────────────
    if (canvasCopyRT.id == 0 || canvasCopyW != canvasW || canvasCopyH != canvasH) {
        if (canvasCopyRT.id > 0) UnloadRenderTexture(canvasCopyRT);
        canvasCopyRT = Load16BitRT(canvasW, canvasH);
        canvasCopyW  = canvasW;
        canvasCopyH  = canvasH;
    }

    rlSetBlendMode(RL_BLEND_CUSTOM);
    rlSetBlendFactors(RL_ONE, RL_ZERO, RL_FUNC_ADD);
    BeginTextureMode(canvasCopyRT);
    DrawTextureRec(dstRT.texture,
        Rectangle{0, 0, (float)canvasW, (float)-canvasH},
        Vector2{0, 0}, WHITE);
    EndTextureMode();

    // ── Uniforms ────────────────────────────────────────────────────
    SetShaderValue(brushBlendShader, locRadIn,     &brush->Realb.rad_in,  SHADER_UNIFORM_FLOAT);
    SetShaderValue(brushBlendShader, locRadOut,    &radOut,               SHADER_UNIFORM_FLOAT);
    SetShaderValue(brushBlendShader, locOpacity,   &brush->Realb.opacity, SHADER_UNIFORM_FLOAT);

    float x2yVal = fmaxf((float)brush->Realb.x2y, 0.01f);
    SetShaderValue(brushBlendShader, locX2Y, &x2yVal, SHADER_UNIFORM_FLOAT);

    float resangleVal = (float)brush->Realb.resangle;
    SetShaderValue(brushBlendShader, locResAngle, &resangleVal, SHADER_UNIFORM_FLOAT);

    float useTexVal = (brushTex.id > 0) ? 1.0f : 0.0f;
    SetShaderValue(brushBlendShader, locUseTex, &useTexVal, SHADER_UNIFORM_FLOAT);

    float maskModeVal = brush->Realb.useTexLumAsAlpha ? 1.0f : 0.0f;
    float maskMixVal  = (float)brush->Realb.texBlendMode;
    float texScaleVal = brush->Realb.texScale;
    float texFeatherVal = brush->Realb.texFeather;
    SetShaderValue(brushBlendShader, locMaskMode,   &maskModeVal,   SHADER_UNIFORM_FLOAT);
    SetShaderValue(brushBlendShader, locMaskMix,    &maskMixVal,    SHADER_UNIFORM_FLOAT);
    SetShaderValue(brushBlendShader, locTexScale,   &texScaleVal,   SHADER_UNIFORM_FLOAT);
    SetShaderValue(brushBlendShader, locTexFeather, &texFeatherVal, SHADER_UNIFORM_FLOAT);

    float bounds[4] = {
        (stampX - radOut) / (float)canvasW,
        (float)(canvasH - (stampY + radOut)) / (float)canvasH,
        (radOut * 2.0f) / (float)canvasW,
        (radOut * 2.0f) / (float)canvasH
    };
    SetShaderValue(brushBlendShader, locRectBounds, bounds, SHADER_UNIFORM_VEC4);

    // ── Pass 2: Blend onto dstRT ────────────────────────────────────
    BeginTextureMode(dstRT);
    rlSetBlendMode(RL_BLEND_CUSTOM);
    rlSetBlendFactors(RL_ONE, RL_ZERO, RL_FUNC_ADD);
    BeginShaderMode(brushBlendShader);

    if (brushTex.id > 0 && locUserMaskTex >= 0) {
        SetShaderValueTexture(brushBlendShader, locUserMaskTex, brushTex);
    }

    DrawTextureRec(canvasCopyRT.texture,
        Rectangle{0, 0, (float)canvasW, (float)-canvasH},
        Vector2{0, 0}, WHITE);

    EndShaderMode();
    EndTextureMode();

    rlSetBlendMode(RL_BLEND_ALPHA);
}
