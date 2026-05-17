#include "repaint.h"
#include "rlgl.h"

static Shader brushBlendShader = {0};
static Shader brushGeoShader   = {0};

static int locUAngle  = -1, locUSquish = -1, locUSize = -1;
static int locOpacity = -1, locRadIn   = -1;
static int locBrushColor = -1, locCurve = -1;
static int locSol = -1, locSol2op = -1, locSeed = -1;
static int locBmidx = -1, locPreserveOp = -1;
static int locSmudgeStrength = -1, locSmudgeOffsetUV = -1;
static int locTexBlendVal = -1, locTexScale = -1, locTexOffset = -1;
static int locTexFeather = -1, locTexThresh = -1;
static int locUseLumAsAlpha = -1, locTexColorMode = -1;
static int locTexBlendMode = -1, locTexNoisemode = -1;
static int locCanvasTex = -1, locBrushTex = -1;
static int locStampCenter = -1;
static int locRadOut = -1;
static int locCanvasSize = -1;
static int locStampOffset = -1;
static bool inited = false;

static RenderTexture2D canvasCopyRT = {0};
static int canvasCopyW = 0, canvasCopyH = 0;

static RenderTexture2D geoUV_RT = {0};
static int geoUVSize = 0;   // currently allocated atlas side (power of two)

static Texture2D whiteTex = {0};

static inline int next_pow2_min32(int v) {
    int r = 32;
    while (r < v) r <<= 1;
    return r;
}

static inline float clampf(float v, float lo, float hi) {
    return v < lo ? lo : (v > hi ? hi : v);
}

void BrushBlend_Init(void) {
    if (inited) return;

    const char* ad = GetApplicationDirectory();
    char vs[512], fs[512];

    snprintf(vs, sizeof(vs), "%sshaders/brush_geo.vs", ad);
    snprintf(fs, sizeof(fs), "%sshaders/brush_geo.fs", ad);
    brushGeoShader = LoadShader(vs, fs);
    locUAngle  = GetShaderLocation(brushGeoShader, "uAngle");
    locUSquish = GetShaderLocation(brushGeoShader, "uSquish");
    locUSize   = GetShaderLocation(brushGeoShader, "uSize");

    snprintf(vs, sizeof(vs), "%sshaders/brush_blend.vs", ad);
    snprintf(fs, sizeof(fs), "%sshaders/brush_blend.fs", ad);
    brushBlendShader = LoadShader(vs, fs);
    locCanvasSize     = GetShaderLocation(brushBlendShader, "canvasSize");
    locOpacity        = GetShaderLocation(brushBlendShader, "opacity");
    locRadIn          = GetShaderLocation(brushBlendShader, "radIn");
    locBrushColor     = GetShaderLocation(brushBlendShader, "brushColor");
    locCurve          = GetShaderLocation(brushBlendShader, "curve");
    locSol            = GetShaderLocation(brushBlendShader, "sol");
    locSol2op         = GetShaderLocation(brushBlendShader, "sol2op");
    locSeed           = GetShaderLocation(brushBlendShader, "seed");
    locBmidx          = GetShaderLocation(brushBlendShader, "bmidx");
    locPreserveOp     = GetShaderLocation(brushBlendShader, "preserveop");
    locSmudgeStrength = GetShaderLocation(brushBlendShader, "smudgeStrength");
    locSmudgeOffsetUV = GetShaderLocation(brushBlendShader, "smudgeOffsetUV");
    locTexBlendVal    = GetShaderLocation(brushBlendShader, "texBlendVal");
    locTexScale       = GetShaderLocation(brushBlendShader, "texScale");
    locTexOffset      = GetShaderLocation(brushBlendShader, "texOffset");
    locTexFeather     = GetShaderLocation(brushBlendShader, "texFeather");
    locTexThresh      = GetShaderLocation(brushBlendShader, "texThresh");
    locUseLumAsAlpha  = GetShaderLocation(brushBlendShader, "useLumAsAlpha");
    locTexColorMode   = GetShaderLocation(brushBlendShader, "texColorMode");
    locTexBlendMode   = GetShaderLocation(brushBlendShader, "texBlendMode");
    locTexNoisemode   = GetShaderLocation(brushBlendShader, "texNoisemode");
    locCanvasTex      = GetShaderLocation(brushBlendShader, "canvasTex");
    locBrushTex       = GetShaderLocation(brushBlendShader, "brushTex");
    locStampCenter    = GetShaderLocation(brushBlendShader, "stampCenter");
    locStampOffset    = GetShaderLocation(brushBlendShader, "stampOffset");
    locRadOut         = GetShaderLocation(brushBlendShader, "radOut");

    if (locCanvasTex >= 0) { int u = 1; SetShaderValue(brushBlendShader, locCanvasTex, &u, SHADER_UNIFORM_INT); }
    if (locBrushTex  >= 0) { int u = 2; SetShaderValue(brushBlendShader, locBrushTex,  &u, SHADER_UNIFORM_INT); }

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
    canvasCopyW = canvasCopyH = 0;
    geoUVSize   = 0;
    inited = false;
}

void BrushBlend_ApplyStamp(
    RenderTexture2D dstRT,
    d_Brush* brush,
    Texture2D brushTex,
    float stampX, float stampY,
    float srcX,   float srcY
) {
    if (!inited || dstRT.id == 0) return;

    int W = dstRT.texture.width;
    int H = dstRT.texture.height;

    float radOut   = fmaxf(brush->Realb.rad_out, 0.001f);
    float radIn    = (float)brush->Realb.rad_in;
    float angleRad = (float)brush->Realb.resangle * (float)(M_PI / 180.0);
    float squish   = fmaxf((float)brush->Realb.x2y, 0.01f);

    // -------- Pass 0: full canvas copy (canvas dimensions change rarely; full copy keeps shader UVs simple)
    if (canvasCopyRT.id == 0 || canvasCopyW != W || canvasCopyH != H) {
        if (canvasCopyRT.id > 0) UnloadRenderTexture(canvasCopyRT);
        canvasCopyRT = Load16BitRT(W, H);
        canvasCopyW  = W;
        canvasCopyH  = H;
    }
    BeginTextureMode(canvasCopyRT);
    rlSetBlendMode(RL_BLEND_CUSTOM);
    rlSetBlendFactors(RL_ONE, RL_ZERO, RL_FUNC_ADD);
    ClearBackground((Color){0,0,0,0});
    DrawTexturePro(
        dstRT.texture,
        (Rectangle){0, 0, (float)W, (float)H},
        (Rectangle){0, 0, (float)W, (float)H},
        (Vector2){0, 0}, 0.0f, WHITE);
    EndTextureMode();

    // -------- Pass 1: geo UV  (BUCKETED, GROW-ONLY cache)
    float bboxHalf = radOut * 1.41421356f;
    int sz = (int)ceilf(bboxHalf * 2.0f);
    if (sz < 32) sz = 32;
    int bucket = next_pow2_min32(sz);

    if (geoUV_RT.id == 0 || geoUVSize < bucket) {
        if (geoUV_RT.id > 0) UnloadRenderTexture(geoUV_RT);
        geoUV_RT  = LoadRenderTexture(bucket, bucket);
        geoUVSize = bucket;
    }

    // Render the geo into the FULL bucket size so the entire cached atlas is the active stamp.
    // Slightly wasted fill on small brushes vs. bucket, but no reallocations during size scrubbing.
    int drawSz = geoUVSize;
    // recompute size factor against drawSz so the ellipse still matches radOut in canvas pixels
    float drawBboxHalf = (float)drawSz * 0.5f;
    float size = radOut / drawBboxHalf;

    SetShaderValue(brushGeoShader, locUAngle,  &angleRad, SHADER_UNIFORM_FLOAT);
    SetShaderValue(brushGeoShader, locUSquish, &squish,   SHADER_UNIFORM_FLOAT);
    SetShaderValue(brushGeoShader, locUSize,   &size,     SHADER_UNIFORM_FLOAT);

    BeginTextureMode(geoUV_RT);
    ClearBackground((Color){0, 0, 0, 0});
    BeginShaderMode(brushGeoShader);
    DrawTexturePro(whiteTex,
        (Rectangle){0, 0, 1, 1},
        (Rectangle){0, 0, (float)drawSz, -(float)drawSz},
        (Vector2){0, 0}, 0.0f, WHITE);
    EndShaderMode();
    EndTextureMode();

    // -------- Pass 2: blend
    float opacity    = clampf((float)brush->Realb.opacity, 0.0f, 1.0f);
    float curve      = clampf((float)brush->Realb.crv,     0.0f, 1.0f);
    float sol        = (float)brush->Realb.sol;
    float sol2op     = (float)brush->Realb.sol2op;
    float seed       = (float)brush->Realb.seed + (float)rand()/(float)RAND_MAX
                       + stampX * 0.01f + stampY * 7.13f;
    int   bmidx      = (int)brush->Realb.bmidx;
    float preserveop = (brush->Realb.preserveop > 0) ? 1.0f : 0.0f;
    float smudge     = brush->Realb.cop;
    float offsetUV[2] = {
        (stampX - srcX) / (float)W,
        -(stampY - srcY) / (float)H
    };
    float col[4] = {
        brush->Realb.col.r / 255.0f,
        brush->Realb.col.g / 255.0f,
        brush->Realb.col.b / 255.0f,
        brush->Realb.col.a / 255.0f
    };
    float tbv = brush->Realb.texBlendVal;
    float tf  = brush->Realb.texFeather;
    float tt  = brush->Realb.texThresh;
    float ts  = brush->Realb.texScale;
    float texOff[2] = {0.0f, 0.0f};
    if (brush->Realb.texNoisemode == 1) {
        texOff[0] = ((float)rand()/(float)RAND_MAX - 0.5f) * 0.3f;
        texOff[1] = ((float)rand()/(float)RAND_MAX - 0.5f) * 0.3f;
    }
    int useLum = brush->Realb.useTexLumAsAlpha ? 1 : 0;
    int cm  = (int)brush->Realb.texColorMode;
    int tbm = (int)brush->Realb.texBlendMode;
    int tnm = (int)brush->Realb.texNoisemode;
    float sc[2] = { stampX / (float)W, (float)(H - stampY) / (float)H };

    SetShaderValue(brushBlendShader, locOpacity,        &opacity,    SHADER_UNIFORM_FLOAT);
    SetShaderValue(brushBlendShader, locRadIn,          &radIn,      SHADER_UNIFORM_FLOAT);
    SetShaderValue(brushBlendShader, locBrushColor,     col,         SHADER_UNIFORM_VEC4);
    SetShaderValue(brushBlendShader, locCurve,          &curve,      SHADER_UNIFORM_FLOAT);
    SetShaderValue(brushBlendShader, locSol,            &sol,        SHADER_UNIFORM_FLOAT);
    SetShaderValue(brushBlendShader, locSol2op,         &sol2op,     SHADER_UNIFORM_FLOAT);
    SetShaderValue(brushBlendShader, locSeed,           &seed,       SHADER_UNIFORM_FLOAT);
    SetShaderValue(brushBlendShader, locBmidx,          &bmidx,      SHADER_UNIFORM_INT);
    SetShaderValue(brushBlendShader, locPreserveOp,     &preserveop, SHADER_UNIFORM_FLOAT);
    SetShaderValue(brushBlendShader, locSmudgeStrength, &smudge,     SHADER_UNIFORM_FLOAT);
    SetShaderValue(brushBlendShader, locSmudgeOffsetUV, offsetUV,    SHADER_UNIFORM_VEC2);
    SetShaderValue(brushBlendShader, locTexBlendVal,    &tbv,        SHADER_UNIFORM_FLOAT);
    SetShaderValue(brushBlendShader, locTexScale,       &ts,         SHADER_UNIFORM_FLOAT);
    SetShaderValue(brushBlendShader, locTexOffset,      texOff,      SHADER_UNIFORM_VEC2);
    SetShaderValue(brushBlendShader, locTexFeather,     &tf,         SHADER_UNIFORM_FLOAT);
    SetShaderValue(brushBlendShader, locTexThresh,      &tt,         SHADER_UNIFORM_FLOAT);
    SetShaderValue(brushBlendShader, locUseLumAsAlpha,  &useLum,     SHADER_UNIFORM_INT);
    SetShaderValue(brushBlendShader, locTexColorMode,   &cm,         SHADER_UNIFORM_INT);
    SetShaderValue(brushBlendShader, locTexBlendMode,   &tbm,        SHADER_UNIFORM_INT);
    SetShaderValue(brushBlendShader, locTexNoisemode,   &tnm,        SHADER_UNIFORM_INT);
    SetShaderValue(brushBlendShader, locStampCenter,    sc,          SHADER_UNIFORM_VEC2);
    SetShaderValue(brushBlendShader, locRadOut,         &radOut,     SHADER_UNIFORM_FLOAT);

    float csz[2] = { (float)W, (float)H };
    SetShaderValue(brushBlendShader, locCanvasSize, csz, SHADER_UNIFORM_VEC2);

    // Final stamp bbox in canvas pixels must match the size we actually rendered the geo at
    // (drawSz x drawSz), otherwise the bbox in canvas space and the UV atlas disagree and
    // the shader's canvasFragUV will be off.
    float stampSizePx = (float)drawSz;
    float x0 = stampX - stampSizePx * 0.5f;
    float y0 = stampY - stampSizePx * 0.5f;

    float so[2] = { x0, y0 };
    SetShaderValue(brushBlendShader, locStampOffset, so, SHADER_UNIFORM_VEC2);

    // radOut passed to vs is used only to compute bboxSize = radOut*sqrt(2)*2.
    // We override that by passing an "effective radOut" so bboxSize == drawSz.
    // bboxSize = radOut_eff * 1.41421356 * 2  =>  radOut_eff = drawSz / (2*sqrt(2))
    float radOutEff = stampSizePx / (2.0f * 1.41421356f);
    SetShaderValue(brushBlendShader, locRadOut, &radOutEff, SHADER_UNIFORM_FLOAT);

    // ------------- final blit
    BeginTextureMode(dstRT);
    rlSetBlendMode(RL_BLEND_CUSTOM);
    rlSetBlendFactors(RL_ONE, RL_ZERO, RL_FUNC_ADD);

    rlActiveTextureSlot(1);
    rlEnableTexture(canvasCopyRT.texture.id);
    rlActiveTextureSlot(2);
    rlEnableTexture(brushTex.id > 0 ? brushTex.id : whiteTex.id);
    rlActiveTextureSlot(0);

    BeginShaderMode(brushBlendShader);

    DrawTexturePro(geoUV_RT.texture,
        (Rectangle){0, 0, (float)drawSz, (float)-drawSz},
        (Rectangle){x0, y0, stampSizePx, stampSizePx},
        (Vector2){0, 0}, 0.0f, WHITE);

    EndShaderMode();

    rlActiveTextureSlot(1); rlDisableTexture();
    rlActiveTextureSlot(2); rlDisableTexture();
    rlActiveTextureSlot(0);

    EndTextureMode();

    rlSetBlendMode(RL_BLEND_ALPHA);
}