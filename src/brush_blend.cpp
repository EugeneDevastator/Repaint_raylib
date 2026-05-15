#include "repaint.h"
#include "rlgl.h"
// At top, toggle to see intermediate stamp RT blitted onto canvas
#define DEBUG_STAMP_BLIT 0

static Shader brushBlendShader  = {0};
static Shader stampXformShader  = {0};

static int locRadIn = -1, locRadOut = -1, locOpacity = -1;
static int locBrushColor = -1, locSeed = -1, locCrv = -1;
static int locStampCenter = -1, locStampHalf = -1;
static int locSol = -1, locSol2op = -1, locBmidx = -1;
static int locPreserveOp = -1;
static int locSmudgeStrength = -1, locSmudgeOffsetUV = -1;
static int locBrushTex = -1;
static int locTexBlendVal = -1, locTexScale = -1, locTexOffset = -1;
static int locTexFeather = -1, locTexThresh = -1;
static int locUseLumAsAlpha = -1, locTexUseRGB = -1;
static int locTexBlendMode = -1, locTexNoisemode = -1;
static int locBrushGeoUvTex = -1;
static bool brushBlendInited = false;

Texture2D g_activeBrushTex = {0};

static RenderTexture2D brushTempRT  = {0};
static RenderTexture2D brushStampRT = {0};
static int brushTempW = 0, brushTempH = 0;
static int brushStampSz = 0;
void BrushBlend_Init(void) {
    if (brushBlendInited) return;

    const char* ad = GetApplicationDirectory();
    char vsPath[512], fsPath[512];

    snprintf(vsPath, sizeof(vsPath), "%sshaders/brush_blend.vs", ad);
    snprintf(fsPath, sizeof(fsPath), "%sshaders/brush_blend.fs", ad);
    brushBlendShader = LoadShader(vsPath, fsPath);
    if (brushBlendShader.id == 0) { printf("BRUSH BLEND SHADER FAILED\n"); return; }

    snprintf(vsPath, sizeof(vsPath), "%sshaders/brush_stamp_xform.vs", ad);
    snprintf(fsPath, sizeof(fsPath), "%sshaders/brush_stamp_xform.fs", ad);
    stampXformShader = LoadShader(vsPath, fsPath);
    if (stampXformShader.id == 0) { printf("STAMP XFORM SHADER FAILED\n"); return; }

    locRadIn        = GetShaderLocation(brushBlendShader, "radIn");
    locRadOut       = GetShaderLocation(brushBlendShader, "radOut");
    locOpacity      = GetShaderLocation(brushBlendShader, "opacity");
    locBrushColor   = GetShaderLocation(brushBlendShader, "brushColor");
    locSeed         = GetShaderLocation(brushBlendShader, "seed");
    locCrv          = GetShaderLocation(brushBlendShader, "crv");
    locSol          = GetShaderLocation(brushBlendShader, "sol");
    locSol2op       = GetShaderLocation(brushBlendShader, "sol2op");
    locBmidx        = GetShaderLocation(brushBlendShader, "bmidx");
    locPreserveOp   = GetShaderLocation(brushBlendShader, "preserveop");
    locStampCenter  = GetShaderLocation(brushBlendShader, "stampCenter");
    locStampHalf    = GetShaderLocation(brushBlendShader, "stampHalf");
    locSmudgeStrength = GetShaderLocation(brushBlendShader, "smudgeStrength");
    locSmudgeOffsetUV = GetShaderLocation(brushBlendShader, "smudgeOffsetUV");
    locBrushTex     = GetShaderLocation(brushBlendShader, "brushTex");
    locBrushGeoUvTex= GetShaderLocation(brushBlendShader, "brushGeoUvTex");
    locTexBlendVal  = GetShaderLocation(brushBlendShader, "texBlendVal");
    locTexScale     = GetShaderLocation(brushBlendShader, "texScale");
    locTexOffset    = GetShaderLocation(brushBlendShader, "texOffset");
    locTexFeather   = GetShaderLocation(brushBlendShader, "texFeather");
    locTexThresh    = GetShaderLocation(brushBlendShader, "texThresh");
    locUseLumAsAlpha= GetShaderLocation(brushBlendShader, "useLumAsAlpha");
    locTexUseRGB    = GetShaderLocation(brushBlendShader, "texUseRGB");
    locTexBlendMode = GetShaderLocation(brushBlendShader, "texBlendMode");
    locTexNoisemode = GetShaderLocation(brushBlendShader, "texNoisemode");

    // Bind texture units
    if (locBrushGeoUvTex >= 0) {
        int u = 1;
        SetShaderValue(brushBlendShader, locBrushGeoUvTex, &u, SHADER_UNIFORM_INT);
    }
    if (locBrushTex >= 0) {
        int u = 2;
        SetShaderValue(brushBlendShader, locBrushTex, &u, SHADER_UNIFORM_INT);
    }

    brushBlendInited = true;
}


void BrushBlend_Shutdown(void) {
    if (!brushBlendInited) return;
    UnloadShader(brushBlendShader);
    UnloadShader(stampXformShader);
    if (brushTempRT.id  > 0) UnloadRenderTexture(brushTempRT);
    if (brushStampRT.id > 0) UnloadRenderTexture(brushStampRT);
    brushTempRT  = RenderTexture2D{0};
    brushStampRT = RenderTexture2D{0};
    brushBlendInited = false;
}

static float randf(void) { return (float)rand() / (float)RAND_MAX; }

// Draw a rotated+squeezed quad into brushStampRT.
// quad covers full RT, UV maps brush texture through rotation+x2y.
static void DrawXformQuad(float angle, float x2y) {
    int locAngle = GetShaderLocation(stampXformShader, "angle");
    int locX2Y   = GetShaderLocation(stampXformShader, "x2y");
    SetShaderValue(stampXformShader, locAngle, &angle, SHADER_UNIFORM_FLOAT);
    SetShaderValue(stampXformShader, locX2Y,   &x2y,   SHADER_UNIFORM_FLOAT);

    float sz = (float)brushStampSz;
    rlBegin(RL_QUADS);
    rlColor4f(1,1,1,1);
    rlTexCoord2f(0,0); rlVertex2f(0,  sz);
    rlTexCoord2f(1,0); rlVertex2f(sz, sz);
    rlTexCoord2f(1,1); rlVertex2f(sz, 0);
    rlTexCoord2f(0,1); rlVertex2f(0,  0);
    rlEnd();
}

void BrushBlend_ApplyStamp(
    RenderTexture2D dstRT,
    d_Brush* brush,
    float stampX, float stampY,
    float srcX,   float srcY
) {
    if (!brushBlendInited || brushBlendShader.id == 0) return;
    if (dstRT.id == 0) return;

    int canvasW = dstRT.texture.width;
    int canvasH = dstRT.texture.height;

    float radOut  = brush->Realb.rad_out;
    if (radOut < 0.5f) radOut = 0.5f;
    float aspect  = fmaxf((float)brush->Realb.x2y, 0.01f);
    float resangle = (float)brush->Realb.resangle;
    float ang     = resangle * (float)(M_PI / 180.0);

    // ── Canvas temp RT ────────────────────────────────────────────
    if (brushTempRT.id == 0 || brushTempW != canvasW || brushTempH != canvasH) {
        if (brushTempRT.id > 0) UnloadRenderTexture(brushTempRT);
        brushTempRT = Load16BitRT(canvasW, canvasH);
        brushTempW  = canvasW;
        brushTempH  = canvasH;
    }

    // ── Stamp RT: sqrt(2)*diameter covers worst-case 45deg rotation
    //    scale by max(aspect,1/aspect) to cover both squeeze directions
    float maxAspect = fmaxf(aspect, 1.0f / aspect);
    int neededSz = (int)ceilf(1.4143f * radOut * 2.0f * maxAspect);
    if (neededSz < 4) neededSz = 4;
    if (brushStampRT.id == 0 || brushStampSz != neededSz) {
        if (brushStampRT.id > 0) UnloadRenderTexture(brushStampRT);
        brushStampRT = LoadRenderTexture(neededSz, neededSz);
        brushStampSz = neededSz;
    }
    // ── Pass 0: fill stampRT with rotated/squeezed UV coords ──────
    BeginTextureMode(brushStampRT);
    ClearBackground((Color){0, 0, 0, 0});
    rlSetBlendMode(RL_BLEND_CUSTOM);
    rlSetBlendFactors(RL_ONE, RL_ZERO, RL_FUNC_ADD);
    rlDisableBackfaceCulling();
    rlMatrixMode(RL_PROJECTION);
    rlPushMatrix();
    rlLoadIdentity();
    rlOrtho(0, brushStampSz, brushStampSz, 0, -1, 1);
    rlMatrixMode(RL_MODELVIEW);
    rlPushMatrix();
    rlLoadIdentity();

    BeginShaderMode(stampXformShader);
    DrawXformQuad(ang, aspect);
    EndShaderMode();

    rlMatrixMode(RL_PROJECTION);
    rlPopMatrix();
    rlMatrixMode(RL_MODELVIEW);
    rlPopMatrix();
    EndTextureMode();


#if DEBUG_STAMP_BLIT
    // Blit stampRT directly onto canvas at stamp position — debug only
    BeginTextureMode(dstRT);
    float dx = stampX - brushStampSz * 0.5f;
    float dy = stampY - brushStampSz * 0.5f;
    DrawTextureRec(brushStampRT.texture,
        Rectangle{0, 0, (float)brushStampSz, (float)-brushStampSz},
        Vector2{dx, dy}, WHITE);
    EndTextureMode();
    rlSetBlendMode(RL_BLEND_ALPHA);
    return;
#else

    // ── Pass 1: copy dstRT → brushTempRT ─────────────────────────
    rlSetBlendMode(RL_BLEND_CUSTOM);
    rlSetBlendFactors(RL_ONE, RL_ZERO, RL_FUNC_ADD);
    BeginTextureMode(brushTempRT);
    DrawTextureRec(dstRT.texture,
        Rectangle{0, 0, (float)canvasW, (float)-canvasH},
        Vector2{0, 0}, WHITE);
    EndTextureMode();
#endif
    // ── Uniforms ──────────────────────────────────────────────────
    SetShaderValue(brushBlendShader, locRadIn,   &brush->Realb.rad_in,  SHADER_UNIFORM_FLOAT);
    SetShaderValue(brushBlendShader, locRadOut,  &radOut,               SHADER_UNIFORM_FLOAT);
    SetShaderValue(brushBlendShader, locOpacity, &brush->Realb.opacity, SHADER_UNIFORM_FLOAT);

    float col[4] = {
        brush->Realb.col.r / 255.0f,
        brush->Realb.col.g / 255.0f,
        brush->Realb.col.b / 255.0f,
        brush->Realb.col.a / 255.0f
    };
    SetShaderValue(brushBlendShader, locBrushColor, col, SHADER_UNIFORM_VEC4);

    float seed = (float)brush->Realb.seed + randf() + stampX * 0.01f + stampY * 7.13f;
    SetShaderValue(brushBlendShader, locSeed,     &seed,                SHADER_UNIFORM_FLOAT);
    SetShaderValue(brushBlendShader, locCrv,      &brush->Realb.crv,    SHADER_UNIFORM_FLOAT);
    SetShaderValue(brushBlendShader, locSol,      &brush->Realb.sol,    SHADER_UNIFORM_FLOAT);
    SetShaderValue(brushBlendShader, locSol2op,   &brush->Realb.sol2op, SHADER_UNIFORM_FLOAT);

    int bmidx = (int)brush->Realb.bmidx;
    SetShaderValue(brushBlendShader, locBmidx,    &bmidx,               SHADER_UNIFORM_INT);
    float preserveop = (brush->Realb.preserveop > 0) ? 1.0f : 0.0f;
    SetShaderValue(brushBlendShader, locPreserveOp, &preserveop,        SHADER_UNIFORM_FLOAT);

    float smudgeStrength = brush->Realb.cop;
    SetShaderValue(brushBlendShader, locSmudgeStrength, &smudgeStrength, SHADER_UNIFORM_FLOAT);
    float offsetUV[2] = {
        (stampX - srcX) / (float)canvasW,
        -(stampY - srcY) / (float)canvasH
    };
    SetShaderValue(brushBlendShader, locSmudgeOffsetUV, offsetUV, SHADER_UNIFORM_VEC2);

    float tbv = brush->Realb.texBlendVal;  // remove the brushStampRT.id check

    SetShaderValue(brushBlendShader, locTexBlendVal,  &tbv,                      SHADER_UNIFORM_FLOAT);
    SetShaderValue(brushBlendShader, locTexScale,     &brush->Realb.texScale,    SHADER_UNIFORM_FLOAT);
    float texOff[2] = {0.0f, 0.0f};
    if (brush->Realb.texNoisemode == 1) {
        texOff[0] = (randf() - 0.5f) * 0.3f;
        texOff[1] = (randf() - 0.5f) * 0.3f;
    }
    SetShaderValue(brushBlendShader, locTexOffset,    texOff,                    SHADER_UNIFORM_VEC2);
    float tf = brush->Realb.texFeather;
    float tt = brush->Realb.texThresh;
    SetShaderValue(brushBlendShader, locTexFeather,   &tf,                       SHADER_UNIFORM_FLOAT);
    SetShaderValue(brushBlendShader, locTexThresh,    &tt,                       SHADER_UNIFORM_FLOAT);
    int useLum = brush->Realb.useTexLumAsAlpha ? 1 : 0;
    int useRGB = brush->Realb.texUseRGB        ? 1 : 0;
    SetShaderValue(brushBlendShader, locUseLumAsAlpha, &useLum,                  SHADER_UNIFORM_INT);
    SetShaderValue(brushBlendShader, locTexUseRGB,     &useRGB,                  SHADER_UNIFORM_INT);
    int tbm = (int)brush->Realb.texBlendMode;
    int tnm = (int)brush->Realb.texNoisemode;
    SetShaderValue(brushBlendShader, locTexBlendMode, &tbm,                      SHADER_UNIFORM_INT);
    SetShaderValue(brushBlendShader, locTexNoisemode, &tnm,                      SHADER_UNIFORM_INT);

    float sc[2] = {
        stampX / (float)canvasW,
        (float)(canvasH - stampY) / (float)canvasH
    };
    float sh[2] = {
        radOut / (float)canvasW,
        radOut / (float)canvasH
    };
    SetShaderValue(brushBlendShader, locStampCenter, sc, SHADER_UNIFORM_VEC2);
    SetShaderValue(brushBlendShader, locStampHalf,   sh, SHADER_UNIFORM_VEC2);

    // stampRT size in UV space (for sampling brushStampRT in localUV space)
    float stampRTSizeUV[2] = {
        (float)brushStampSz / (float)canvasW,
        (float)brushStampSz / (float)canvasH
    };
    int locStampRTSize = GetShaderLocation(brushBlendShader, "stampRTSizeUV");
    SetShaderValue(brushBlendShader, locStampRTSize, stampRTSizeUV, SHADER_UNIFORM_VEC2);

    // ── Pass 2: blend onto dstRT ──────────────────────────────────
    BeginTextureMode(dstRT);
    rlSetBlendMode(RL_BLEND_CUSTOM);
    rlSetBlendFactors(RL_ONE, RL_ZERO, RL_FUNC_ADD);
    BeginShaderMode(brushBlendShader);

    // unit 1: stampRT (rotated geo UVs)
    if (locBrushGeoUvTex >= 0) {
        rlActiveTextureSlot(1);
        rlEnableTexture(brushStampRT.texture.id);
    }
    // unit 2: user brush mask texture
    if (locBrushTex >= 0 && g_activeBrushTex.id > 0) {
        rlActiveTextureSlot(2);
        rlEnableTexture(g_activeBrushTex.id);
    }

    DrawTextureRec(brushTempRT.texture,
        Rectangle{0, 0, (float)canvasW, (float)-canvasH},
        Vector2{0, 0}, WHITE);

    rlActiveTextureSlot(1);
    rlDisableTexture();
    if (g_activeBrushTex.id > 0) {
        rlActiveTextureSlot(2);
        rlDisableTexture();
    }
    rlActiveTextureSlot(0);
    EndShaderMode();
    EndTextureMode();

    rlSetBlendMode(RL_BLEND_ALPHA);
}
