#include "repaint.h"
#include "rlgl.h"

static Shader brushBlendShader = {0};
static Shader brushGeoShader   = {0};

static int locUAngle  = -1, locUSquish = -1, locUSize = -1;
static int locOpacity = -1, locRadIn   = -1;
static int locUseTex  = -1, locBrushRGB = -1, locCurve = -1;
static int locCanvasTex = -1, locBrushTex = -1;

static bool inited = false;

static RenderTexture2D canvasCopyRT = {0};
static int canvasCopyW = 0, canvasCopyH = 0;

static RenderTexture2D geoUV_RT = {0};
static int geoUVSize = 0;

static Texture2D whiteTex = {0};

Texture2D g_activeBrushTex = {0};

void BrushBlend_Init(void) {
    if (inited) return;

    const char* ad = GetApplicationDirectory();
    char vs[512], fs[512];

    snprintf(vs, sizeof(vs), "%sshaders/brush_geo.vs",   ad);
    snprintf(fs, sizeof(fs), "%sshaders/brush_geo.fs",   ad);
    brushGeoShader = LoadShader(vs, fs);

    locUAngle  = GetShaderLocation(brushGeoShader, "uAngle");
    locUSquish = GetShaderLocation(brushGeoShader, "uSquish");
    locUSize   = GetShaderLocation(brushGeoShader, "uSize");

    snprintf(vs, sizeof(vs), "%sshaders/brush_blend.vs", ad);
    snprintf(fs, sizeof(fs), "%sshaders/brush_blend.fs", ad);
    brushBlendShader = LoadShader(vs, fs);

    locOpacity   = GetShaderLocation(brushBlendShader, "opacity");
    locRadIn     = GetShaderLocation(brushBlendShader, "radIn");
    locUseTex    = GetShaderLocation(brushBlendShader, "useTex");
    locBrushRGB  = GetShaderLocation(brushBlendShader, "brushRGB");
    locCurve     = GetShaderLocation(brushBlendShader, "curve");
    locCanvasTex = GetShaderLocation(brushBlendShader, "canvasTex");
    locBrushTex  = GetShaderLocation(brushBlendShader, "brushTex");

    Image img = GenImageColor(1, 1, WHITE);
    whiteTex = LoadTextureFromImage(img);
    UnloadImage(img);

    inited = true;
}

void BrushBlend_Shutdown(void) {
    if (!inited) return;
    UnloadShader(brushBlendShader);
    UnloadShader(brushGeoShader);
    if (canvasCopyRT.id > 0) UnloadRenderTexture(canvasCopyRT);
    if (geoUV_RT.id > 0)     UnloadRenderTexture(geoUV_RT);
    if (whiteTex.id > 0)     UnloadTexture(whiteTex);
    canvasCopyRT = (RenderTexture2D){0};
    geoUV_RT     = (RenderTexture2D){0};
    whiteTex     = (Texture2D){0};
    inited = false;
}
void BrushBlend_ApplyStamp(
    RenderTexture2D dstRT,
    d_Brush* brush,
    Texture2D brushTex,
    float stampX, float stampY,
    float srcX,   float srcY
) {
    (void)srcX; (void)srcY;
    if (!inited || dstRT.id == 0) return;

    int W = dstRT.texture.width;
    int H = dstRT.texture.height;

    float radOut = fmaxf(brush->Realb.rad_out, 0.5f);
    float radIn  = fmaxf(0.0f, fminf(1.0f, brush->Realb.rad_in));

    // Pass 0: copy canvas
    if (canvasCopyRT.id == 0 || canvasCopyW != W || canvasCopyH != H) {
        if (canvasCopyRT.id > 0) UnloadRenderTexture(canvasCopyRT);
        canvasCopyRT = Load16BitRT(W, H);
        canvasCopyW  = W;
        canvasCopyH  = H;
    }
    BeginTextureMode(canvasCopyRT);
    rlSetBlendMode(RL_BLEND_CUSTOM);
    rlSetBlendFactors(RL_ONE, RL_ZERO, RL_FUNC_ADD);
    DrawTextureRec(dstRT.texture,
        (Rectangle){0, 0, (float)W, (float)-H},
        (Vector2){0, 0}, WHITE);
    EndTextureMode();

    // Pass 1: geo UV
    float angleRad = (float)brush->Realb.resangle * (float)(M_PI / 180.0);
    float squish   = fmaxf((float)brush->Realb.x2y, 0.01f);

    float bboxHalf = radOut * 1.41421356f;
    int sz = (int)ceilf(bboxHalf * 2.0f);
    if (sz < 32) sz = 32;

    if (geoUV_RT.id == 0 || geoUVSize != sz) {
        if (geoUV_RT.id > 0) UnloadRenderTexture(geoUV_RT);
        geoUV_RT  = LoadRenderTexture(sz, sz);
        geoUVSize = sz;
    }

    float size = radOut / bboxHalf; // = 1/sqrt(2), constant but keep it explicit

    SetShaderValue(brushGeoShader, locUAngle,  &angleRad, SHADER_UNIFORM_FLOAT);
    SetShaderValue(brushGeoShader, locUSquish, &squish,   SHADER_UNIFORM_FLOAT);
    SetShaderValue(brushGeoShader, locUSize,   &size,     SHADER_UNIFORM_FLOAT);

    BeginTextureMode(geoUV_RT);
    ClearBackground((Color){0, 0, 0, 0});
    BeginShaderMode(brushGeoShader);
    DrawTexturePro(whiteTex,
        (Rectangle){0, 0, 1, 1},
        (Rectangle){0, 0, (float)sz, -(float)sz},
        (Vector2){0, 0}, 0.0f, WHITE);
    EndShaderMode();
    EndTextureMode();

    // Pass 2: blend onto dstRT
    float opacity    = fmaxf(0.0f, fminf(1.0f, (float)brush->Realb.opacity));
    float curve      = fmaxf(0.0f, fminf(1.0f, (float)brush->Realb.crv));
    float useTexVal  = (brushTex.id > 0) ? 1.0f : 0.0f;
    float brushRGB[3] = {
        brush->Realb.col.r / 255.0f,
        brush->Realb.col.g / 255.0f,
        brush->Realb.col.b / 255.0f
    };

    SetShaderValue(brushBlendShader, locOpacity,  &opacity,   SHADER_UNIFORM_FLOAT);
    SetShaderValue(brushBlendShader, locRadIn,    &radIn,     SHADER_UNIFORM_FLOAT);
    SetShaderValue(brushBlendShader, locUseTex,   &useTexVal, SHADER_UNIFORM_FLOAT);
    SetShaderValue(brushBlendShader, locCurve,    &curve,     SHADER_UNIFORM_FLOAT);
    SetShaderValue(brushBlendShader, locBrushRGB, brushRGB,   SHADER_UNIFORM_VEC3);

    if (locCanvasTex >= 0)
        SetShaderValueTexture(brushBlendShader, locCanvasTex, canvasCopyRT.texture);
    if (brushTex.id > 0 && locBrushTex >= 0)
        SetShaderValueTexture(brushBlendShader, locBrushTex, brushTex);

    BeginTextureMode(dstRT);
    rlSetBlendMode(RL_BLEND_CUSTOM);
    rlSetBlendFactors(RL_ONE, RL_ZERO, RL_FUNC_ADD);
    BeginShaderMode(brushBlendShader);

    float x0 = stampX - bboxHalf;
    float y0 = stampY - bboxHalf;
    DrawTexturePro(geoUV_RT.texture,
        (Rectangle){0, 0, (float)sz, (float)-sz},
        (Rectangle){x0, y0, bboxHalf * 2.0f, bboxHalf * 2.0f},
        (Vector2){0, 0}, 0.0f, WHITE);

    EndShaderMode();
    EndTextureMode();

    rlSetBlendMode(RL_BLEND_ALPHA);
}
