#include "repaint.h"
#include "rlgl.h"

static Shader brushBlendShader = {0};
static int locRectBounds = -1;
static int locRadIn = -1;
static int locRadOut = -1;
static int locOpacity = -1;
static int locBrushColor = -1;
static int locSeed = -1;
static int locCrv = -1;
static int locX2Y = -1;
static int locResangle = -1;
static int locSol = -1;
static int locSol2op = -1;
static int locBmidx = -1;
static int locPreserveOp = -1;
static int locSmudgeStrength = -1;
static int locSmudgeOffsetUV = -1;
static int locBrushTex = -1;
static int locTexBlendVal = -1;
static int locTexScale = -1;
static int locTexOffset = -1;
static int locTexFeather = -1;
static int locTexThresh = -1;
static int locUseLumAsAlpha = -1;
static int locTexUseRGB = -1;
static int locTexBlendMode = -1;
static int locTexNoisemode = -1;
static bool brushBlendInited = false;

// Active brush texture set before each ApplyStamp call
Texture2D g_activeBrushTex = {0};

// Temp render target to avoid feedback loop (reading from dstRT while writing to it)
static RenderTexture2D brushTempRT = {0};
static int brushTempW = 0;
static int brushTempH = 0;

void BrushBlend_Init(void) {
    if (brushBlendInited) return;

    const char* ad = GetApplicationDirectory();
    char vsPath[512], fsPath[512];
    snprintf(vsPath, sizeof(vsPath), "%sshaders/brush_gen.vs", ad);
    snprintf(fsPath, sizeof(fsPath), "%sshaders/brush_blend.fs", ad);
    brushBlendShader = LoadShader(vsPath, fsPath);
    if (brushBlendShader.id == 0) return;

    locRectBounds = GetShaderLocation(brushBlendShader, "rectBounds");
    locRadIn      = GetShaderLocation(brushBlendShader, "radIn");
    locRadOut     = GetShaderLocation(brushBlendShader, "radOut");
    locOpacity    = GetShaderLocation(brushBlendShader, "opacity");
    locBrushColor = GetShaderLocation(brushBlendShader, "brushColor");
    locSeed       = GetShaderLocation(brushBlendShader, "seed");
    locCrv        = GetShaderLocation(brushBlendShader, "crv");
    locX2Y        = GetShaderLocation(brushBlendShader, "x2y");
    locResangle   = GetShaderLocation(brushBlendShader, "resangle");
    locSol        = GetShaderLocation(brushBlendShader, "sol");
    locSol2op     = GetShaderLocation(brushBlendShader, "sol2op");
    locBmidx      = GetShaderLocation(brushBlendShader, "bmidx");
    locPreserveOp = GetShaderLocation(brushBlendShader, "preserveop");
    locSmudgeStrength = GetShaderLocation(brushBlendShader, "smudgeStrength");
    locSmudgeOffsetUV = GetShaderLocation(brushBlendShader, "smudgeOffsetUV");
    locBrushTex       = GetShaderLocation(brushBlendShader, "brushTex");
    locTexBlendVal    = GetShaderLocation(brushBlendShader, "texBlendVal");
    locTexScale       = GetShaderLocation(brushBlendShader, "texScale");
    locTexOffset      = GetShaderLocation(brushBlendShader, "texOffset");
    locTexFeather     = GetShaderLocation(brushBlendShader, "texFeather");
    locTexThresh      = GetShaderLocation(brushBlendShader, "texThresh");
    locUseLumAsAlpha  = GetShaderLocation(brushBlendShader, "useLumAsAlpha");
    locTexUseRGB      = GetShaderLocation(brushBlendShader, "texUseRGB");
    locTexBlendMode   = GetShaderLocation(brushBlendShader, "texBlendMode");
    locTexNoisemode   = GetShaderLocation(brushBlendShader, "texNoisemode");

    // Assign brushTex sampler to texture unit 1 (unit 0 is reserved for texture0/canvas)
    if (locBrushTex >= 0) {
        int texUnit = 1;
        SetShaderValue(brushBlendShader, locBrushTex, &texUnit, SHADER_UNIFORM_INT);
    }

    brushBlendInited = true;
}

void BrushBlend_Shutdown(void) {
    if (!brushBlendInited) return;
    UnloadShader(brushBlendShader);
    if (brushTempRT.id > 0) UnloadRenderTexture(brushTempRT);
    brushTempRT = RenderTexture2D{0};
    brushBlendInited = false;
}

static float randf(void) {
    return (float)rand() / (float)RAND_MAX;
}

void BrushBlend_ApplyStamp(
    RenderTexture2D dstRT,
    d_Brush* brush,
    float stampX, float stampY,
    float srcX, float srcY
) {
    if (!brushBlendInited || brushBlendShader.id == 0) return;
    if (dstRT.id == 0) return;

    int canvasW = dstRT.texture.width;
    int canvasH = dstRT.texture.height;

    // Ensure temp RT matches canvas size (avoids feedback loop)
    if (brushTempRT.id == 0 || brushTempW != canvasW || brushTempH != canvasH) {
        if (brushTempRT.id > 0) UnloadRenderTexture(brushTempRT);
        brushTempRT = Load16BitRT(canvasW, canvasH);
        brushTempW = canvasW;
        brushTempH = canvasH;
    }

    float radOut = brush->Realb.rad_out;
    if (radOut < 0.5f) radOut = 0.5f;

    float aspect = fmaxf(brush->Realb.x2y, 0.01f);
    float stampW = radOut * 2.0f * aspect;
    float stampH = radOut * 2.0f;
    float rectX = stampX - stampW * 0.5f;
    float rectY = stampY - stampH * 0.5f;

    float nRect[4] = {
        rectX / (float)canvasW,
        (float)(canvasH - rectY - stampH) / (float)canvasH,
        stampW / (float)canvasW,
        stampH / (float)canvasH
    };
    SetShaderValue(brushBlendShader, locRectBounds, nRect, SHADER_UNIFORM_VEC4);

    SetShaderValue(brushBlendShader, locRadIn,  &brush->Realb.rad_in,  SHADER_UNIFORM_FLOAT);
    SetShaderValue(brushBlendShader, locRadOut, &radOut,               SHADER_UNIFORM_FLOAT);
    SetShaderValue(brushBlendShader, locOpacity,&brush->Realb.opacity, SHADER_UNIFORM_FLOAT);

    float col[4] = {
        brush->Realb.col.r / 255.0f,
        brush->Realb.col.g / 255.0f,
        brush->Realb.col.b / 255.0f,
        brush->Realb.col.a / 255.0f
    };
    SetShaderValue(brushBlendShader, locBrushColor, col, SHADER_UNIFORM_VEC4);

    float seed = (float)brush->Realb.seed + randf() + stampX * 0.01f + stampY * 7.13f;
    SetShaderValue(brushBlendShader, locSeed, &seed, SHADER_UNIFORM_FLOAT);

    SetShaderValue(brushBlendShader, locCrv,      &brush->Realb.crv,      SHADER_UNIFORM_FLOAT);
    SetShaderValue(brushBlendShader, locX2Y,      &brush->Realb.x2y,      SHADER_UNIFORM_FLOAT);
    float resangle = (float)brush->Realb.resangle;
    SetShaderValue(brushBlendShader, locResangle, &resangle,              SHADER_UNIFORM_FLOAT);
    SetShaderValue(brushBlendShader, locSol,      &brush->Realb.sol,      SHADER_UNIFORM_FLOAT);
    SetShaderValue(brushBlendShader, locSol2op,   &brush->Realb.sol2op,   SHADER_UNIFORM_FLOAT);
    int bmidx = (int)brush->Realb.bmidx;
    SetShaderValue(brushBlendShader, locBmidx,    &bmidx,                 SHADER_UNIFORM_INT);
    float preserveop = (brush->Realb.preserveop > 0) ? 1.0f : 0.0f;
    SetShaderValue(brushBlendShader, locPreserveOp, &preserveop,          SHADER_UNIFORM_FLOAT);

    float smudgeStrength = brush->Realb.cop;
    SetShaderValue(brushBlendShader, locSmudgeStrength, &smudgeStrength,  SHADER_UNIFORM_FLOAT);

    float offsetUV[2] = {
        (stampX - srcX) / (float)canvasW,
        -(stampY - srcY) / (float)canvasH
    };
    SetShaderValue(brushBlendShader, locSmudgeOffsetUV, offsetUV, SHADER_UNIFORM_VEC2);

    // ── Brush texture uniforms (brushmask) ──────────────────────────
    float tbv = (g_activeBrushTex.id > 0) ? brush->Realb.texBlendVal : -1.0f;
    SetShaderValue(brushBlendShader, locTexBlendVal, &tbv, SHADER_UNIFORM_FLOAT);
    SetShaderValue(brushBlendShader, locTexScale, &brush->Realb.texScale, SHADER_UNIFORM_FLOAT);
    float texOff[2] = {0.0f, 0.0f};
    if (brush->Realb.texNoisemode == 1 && g_activeBrushTex.id > 0) {
        texOff[0] = (randf() - 0.5f) * 0.3f;
        texOff[1] = (randf() - 0.5f) * 0.3f;
    }
    SetShaderValue(brushBlendShader, locTexOffset, texOff, SHADER_UNIFORM_VEC2);
    float tf = (g_activeBrushTex.id > 0) ? brush->Realb.texFeather : 0.0f;
    SetShaderValue(brushBlendShader, locTexFeather, &tf, SHADER_UNIFORM_FLOAT);
    float tt = (g_activeBrushTex.id > 0) ? brush->Realb.texThresh : 1.0f;
    SetShaderValue(brushBlendShader, locTexThresh, &tt, SHADER_UNIFORM_FLOAT);
    int useLum = (g_activeBrushTex.id > 0 && brush->Realb.useTexLumAsAlpha) ? 1 : 0;
    SetShaderValue(brushBlendShader, locUseLumAsAlpha, &useLum, SHADER_UNIFORM_INT);
    int useRGB = (g_activeBrushTex.id > 0 && brush->Realb.texUseRGB) ? 1 : 0;
    SetShaderValue(brushBlendShader, locTexUseRGB, &useRGB, SHADER_UNIFORM_INT);

    int tbm = (g_activeBrushTex.id > 0) ? (int)brush->Realb.texBlendMode : 0;
    int tnm = (g_activeBrushTex.id > 0) ? (int)brush->Realb.texNoisemode : 2;
    SetShaderValue(brushBlendShader, locTexBlendMode, &tbm, SHADER_UNIFORM_INT);
    SetShaderValue(brushBlendShader, locTexNoisemode, &tnm, SHADER_UNIFORM_INT);

    // Step 1: copy dstRT to tempRT (read from dstRT, write to tempRT)
    rlSetBlendMode(RL_BLEND_CUSTOM);
    rlSetBlendFactors(RL_ONE, RL_ZERO, RL_FUNC_ADD);
    BeginTextureMode(brushTempRT);
    DrawTextureRec(dstRT.texture, Rectangle{0, 0, (float)canvasW, (float)-canvasH}, Vector2{0, 0}, WHITE);
    EndTextureMode();

    // Step 2: apply brush stamp using tempRT as source (avoid feedback loop)
    // ONE,ZERO prevents double-blending: shader already computes fully blended result
    BeginTextureMode(dstRT);
    rlSetBlendMode(RL_BLEND_CUSTOM);
    rlSetBlendFactors(RL_ONE, RL_ZERO, RL_FUNC_ADD);
    BeginShaderMode(brushBlendShader);

    // Bind brush texture to unit 1 inside the shader block (unit 0 is texture0/canvas)
    if (g_activeBrushTex.id > 0 && locBrushTex >= 0) {
        rlActiveTextureSlot(1);
        rlEnableTexture(g_activeBrushTex.id);
    }

    DrawTextureRec(brushTempRT.texture, Rectangle{0, 0, (float)canvasW, (float)(-canvasH)}, Vector2{0, 0}, WHITE);

    if (g_activeBrushTex.id > 0 && locBrushTex >= 0) {
        rlActiveTextureSlot(0);
    }
    EndShaderMode();
    EndTextureMode();

    rlSetBlendMode(RL_BLEND_ALPHA);
}