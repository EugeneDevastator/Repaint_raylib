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
static bool brushBlendInited = false;

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
    float stampX, float stampY
) {
    if (!brushBlendInited || brushBlendShader.id == 0) return;
    if (dstRT.id == 0) return;

    int canvasW = dstRT.texture.width;
    int canvasH = dstRT.texture.height;

    // Ensure temp RT matches canvas size (avoids feedback loop)
    if (brushTempRT.id == 0 || brushTempW != canvasW || brushTempH != canvasH) {
        if (brushTempRT.id > 0) UnloadRenderTexture(brushTempRT);
        brushTempRT = LoadRenderTexture(canvasW, canvasH);
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

    // Step 1: copy dstRT to tempRT (read from dstRT, write to tempRT)
    rlSetBlendMode(RL_BLEND_CUSTOM);
    rlSetBlendFactors(RL_ONE, RL_ZERO, RL_FUNC_ADD);
    BeginTextureMode(brushTempRT);
    DrawTextureRec(dstRT.texture, Rectangle{0, 0, (float)canvasW, (float)-canvasH}, Vector2{0, 0}, WHITE);
    EndTextureMode();

    // Step 2: apply brush stamp using tempRT as source (avoid feedback loop)
    BeginTextureMode(dstRT);
    BeginShaderMode(brushBlendShader);
    DrawTextureRec(brushTempRT.texture, Rectangle{0, 0, (float)canvasW, (float)-canvasH}, Vector2{0, 0}, WHITE);
    EndShaderMode();
    EndTextureMode();

    rlSetBlendMode(RL_BLEND_ALPHA);
}
