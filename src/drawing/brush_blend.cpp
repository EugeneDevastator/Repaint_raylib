
#include <cmath>
#include <cstdio>

#include "brush_draw.h"
#include "RaylibUtils.h"
#include "rlgl.h"
#include "brush_blend.h"
#include "external/glad.h"

static Shader brushBlendShader = {0};
static Shader brushGeoShader   = {0};

static int locUAngle  = -1, locUSquish = -1, locUSize = -1;
static int locUPerspective = -1;
static int locURadIn  = -1, locUCurve = -1;
static int locOpacity = -1, locRadIn   = -1;
static int locBrushColor = -1, locCurve = -1;
static int locSol = -1, locSol2op = -1, locSeed = -1;
static int locBmidx = -1, locPreserveOp = -1;
static int locSmudgeStrength = -1, locSmudgeOffsetUV = -1;
static int locSmudgeSrcRad = -1, locSmudgeAngleDelta = -1, locRadOut = -1;
static int locTexBlendVal = -1, locTexScale = -1, locTexOffset = -1;
static int locUserTexOrigin = -1;
static int locHasTexture = -1;
static int locTexFeather = -1, locTexThresh = -1;
static int locUseLumAsAlpha = -1, locTexColorMode = -1;
static int locTexNoisemode = -1;
static int locDstTex = -1, locBrushTex = -1;
static int locStampCenter = -1;
static int locCanvasSize = -1;
static int locBlitOrigin = -1;
static int locBlitSize   = -1;
static int locFracShift  = -1;
static int locPwr = -1;
static int locEraseMode = -1;
static int locSeamless = -1;
static int locPixelPerfect = -1;
static int locFocalOffset = -1;
static int locGeoTex = -1;
static bool inited = false;

#define POOL_COUNT 64
static RenderTexture2D geoPool[POOL_COUNT] = {0};
static RenderTexture2D intermediatePool[POOL_COUNT] = {0};
static Texture2D whiteTex = {0};

static inline int next_mult32(int v) {
    if (v <= 32) return 32;
    return ((v + 31) / 32) * 32;
}
static inline float clampf(float v, float lo, float hi) {
    return v < lo ? lo : (v > hi ? hi : v);
}
static inline int pool_index(int bucket) { return (bucket / 32) - 1; }

// Create a render-target texture with GL_RGBA16 (0-65535 uniform precision).
// Raylib's RL_PIXELFORMAT_UNCOMPRESSED_R16G16B16A16 maps to GL_RGBA16F
// (half-float) internally, so we bypass rlLoadTexture and call OpenGL directly.
static unsigned int CreateTexRGBA16(int w, int h) {
    unsigned int id;
    glGenTextures(1, &id);
    glBindTexture(GL_TEXTURE_2D, id);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA16, w, h, 0, GL_RGBA, GL_UNSIGNED_SHORT, NULL);
    return id;
}

static RenderTexture2D* AllocPoolRT(RenderTexture2D* pool, int bucket, bool pointFilter) {
    int pidx = pool_index(bucket);
    if (pool[pidx].id == 0) {
        unsigned int fboId = rlLoadFramebuffer();
        rlEnableFramebuffer(fboId);
        unsigned int texId = CreateTexRGBA16(bucket, bucket);
        unsigned int depId = rlLoadTextureDepth(bucket, bucket, true);
        rlFramebufferAttach(fboId, texId, RL_ATTACHMENT_COLOR_CHANNEL0, RL_ATTACHMENT_TEXTURE2D, 0);
        rlFramebufferAttach(fboId, depId, RL_ATTACHMENT_DEPTH, RL_ATTACHMENT_RENDERBUFFER, 0);
        rlFramebufferComplete(fboId);
        rlDisableFramebuffer();
        RenderTexture2D rt = {0};
        rt.id = fboId; rt.texture.id = texId;
        rt.texture.width = bucket; rt.texture.height = bucket;
        rt.texture.format = RL_PIXELFORMAT_UNCOMPRESSED_R16G16B16A16;
        rt.texture.mipmaps = 1;
        rt.depth.id = depId; rt.depth.width = bucket; rt.depth.height = bucket;
        rt.depth.format = 19; rt.depth.mipmaps = 1;
        SetTextureFilter(rt.texture, pointFilter ? TEXTURE_FILTER_POINT : TEXTURE_FILTER_BILINEAR);
        SetTextureWrap(rt.texture, TEXTURE_WRAP_CLAMP);
        pool[pidx] = rt;
    }
    return &pool[pidx];
}

static void FreePool(RenderTexture2D* pool) {
    for (int i = 0; i < POOL_COUNT; i++) {
        if (pool[i].id > 0) { UnloadRenderTexture(pool[i]); pool[i] = (RenderTexture2D){0}; }
    }
}

void BrushBlend_Init(void) {
    if (inited) return;
    const char* ad = GetApplicationDirectory();
    char vs[512], fs[512];

    snprintf(vs, sizeof(vs), "%sshaders/brush_geo.vs", ad);
    snprintf(fs, sizeof(fs), "%sshaders/brush_geo.fs", ad);
    brushGeoShader = LoadShaderWithIncludes(vs, fs);
    locUAngle  = GetShaderLocation(brushGeoShader, "uAngle");
    locUSquish = GetShaderLocation(brushGeoShader, "uSquish");
    locUSize   = GetShaderLocation(brushGeoShader, "uSize");
    locUPerspective = GetShaderLocation(brushGeoShader, "uPerspective");
    locURadIn  = GetShaderLocation(brushGeoShader, "uRadIn");
    locUCurve  = GetShaderLocation(brushGeoShader, "uCurve");
    locFocalOffset = GetShaderLocation(brushGeoShader, "uFocalOffset");

    snprintf(vs, sizeof(vs), "%sshaders/brush_blend.vs", ad);
    snprintf(fs, sizeof(fs), "%sshaders/brush_blend.fs", ad);
    brushBlendShader = LoadShaderWithIncludes(vs, fs);
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
    locSmudgeSrcRad    = GetShaderLocation(brushBlendShader, "smudgeSrcRad");
    locSmudgeAngleDelta = GetShaderLocation(brushBlendShader, "smudgeAngleDelta");
    locRadOut          = GetShaderLocation(brushBlendShader, "radOut");
    locTexBlendVal    = GetShaderLocation(brushBlendShader, "texBlendVal");
    locTexScale       = GetShaderLocation(brushBlendShader, "texScale");
    locTexOffset      = GetShaderLocation(brushBlendShader, "texOffset");
    locUserTexOrigin  = GetShaderLocation(brushBlendShader, "userTexOrigin");
    locHasTexture     = GetShaderLocation(brushBlendShader, "hasTexture");
    locTexFeather     = GetShaderLocation(brushBlendShader, "texFeather");
    locTexThresh      = GetShaderLocation(brushBlendShader, "texThresh");
    locUseLumAsAlpha  = GetShaderLocation(brushBlendShader, "useLumAsAlpha");
    locTexColorMode   = GetShaderLocation(brushBlendShader, "texColorMode");
    locTexNoisemode   = GetShaderLocation(brushBlendShader, "texNoisemode");
    locDstTex         = GetShaderLocation(brushBlendShader, "dstTex");
    locBrushTex       = GetShaderLocation(brushBlendShader, "brushTex");
    locStampCenter    = GetShaderLocation(brushBlendShader, "stampCenter");
    locBlitOrigin     = GetShaderLocation(brushBlendShader, "blitOrigin");
    locBlitSize       = GetShaderLocation(brushBlendShader, "blitSize");
    locFracShift      = GetShaderLocation(brushBlendShader, "fracShift");
    locPwr            = GetShaderLocation(brushBlendShader, "pwr");
    locEraseMode      = GetShaderLocation(brushBlendShader, "eraseMode");
    locSeamless       = GetShaderLocation(brushBlendShader, "uSeamless");
    locPixelPerfect   = GetShaderLocation(brushBlendShader, "uPixelPerfect");

    if (locDstTex   >= 0) { int u = 1; SetShaderValue(brushBlendShader, locDstTex,   &u, SHADER_UNIFORM_INT); }
    if (locBrushTex >= 0) { int u = 2; SetShaderValue(brushBlendShader, locBrushTex, &u, SHADER_UNIFORM_INT); }

    locGeoTex = GetShaderLocation(brushBlendShader, "geoTex");
    if (locGeoTex >= 0) { int u = 0; SetShaderValue(brushBlendShader, locGeoTex, &u, SHADER_UNIFORM_INT); }

    Image img = GenImageColor(4, 4, WHITE);
    whiteTex = LoadTextureFromImage(img);
    UnloadImage(img);
    inited = true;
}

void BrushBlend_Shutdown(void) {
    if (!inited) return;
    UnloadShader(brushBlendShader);
    UnloadShader(brushGeoShader);
    FreePool(geoPool);
    FreePool(intermediatePool);
    if (whiteTex.id > 0) UnloadTexture(whiteTex);
    whiteTex = (Texture2D){0};
    inited = false;
}

void BrushBlend_ApplyStamp(
    RenderTexture2D dstRT,
    const CollapsedBrush& brush,
    Texture2D brushTex, bool useTexture,
    float stampX, float stampY,
    float srcX,   float srcY,
    float srcRad, float srcAngleDeg,
    bool seamless, bool pixelPerfect
) {
    if (!inited || dstRT.id == 0) return;

    int W = dstRT.texture.width;
    int H = dstRT.texture.height;

    float radOut   = fmaxf(brush.rad_out_px, 0.001f);
    float angleRad = (float)brush.resangle * (3.14159265f / 180.0f);
    float squish   = fmaxf((float)brush.scale_y, 0.01f);

    if (fabsf(brush.focalOffset) > 0.0001f && radOut > 0.001f) {
        float shift = brush.focalOffset * radOut; // *squish; need if squishing from other side
        stampX -= cosf(angleRad- PI*0.5) * shift;

        stampY -= sinf(angleRad- PI*0.5) * shift;
        srcX -= cosf(angleRad  - PI*0.5) * shift;
        srcY -= sinf(angleRad  - PI*0.5) * shift;
    }

    float radOutForGeo = brush.rad_out_px;
    if (radOutForGeo < 1.0f) radOutForGeo = 1.0f;

    float bboxHalf = radOutForGeo * 1.41421356f;
    int sz = (int)ceilf(bboxHalf * 2.0f);
    if (sz < 4) sz = 4;
    int bucket = next_mult32(sz);
    if (bucket > 2048) bucket = 2048;
    int drawSz = bucket;

    float stampSizePx = (float)drawSz;
    float actualRadOut = brush.rad_out_px;
    if (actualRadOut <= 1.0f && actualRadOut > 0.0f) {
        float ratio = actualRadOut / radOutForGeo;
        stampSizePx = (float)drawSz * ratio;
        if (stampSizePx < 1.0f) stampSizePx = 1.0f;
    }

    float x0 = stampX - stampSizePx * 0.5f;
    float y0 = stampY - stampSizePx * 0.5f;

    // -------- Pass 1: geo UV --------
    RenderTexture2D* geoRT = AllocPoolRT(geoPool, bucket, true);

    float drawBboxHalf = (float)drawSz * 0.5f;
    float size = radOutForGeo / drawBboxHalf;

    SetShaderValue(brushGeoShader, locUAngle,  &angleRad, SHADER_UNIFORM_FLOAT);
    SetShaderValue(brushGeoShader, locUSquish, &squish,   SHADER_UNIFORM_FLOAT);
    SetShaderValue(brushGeoShader, locUSize,   &size,     SHADER_UNIFORM_FLOAT);
    float persp = brush.perspective;
    SetShaderValue(brushGeoShader, locUPerspective, &persp, SHADER_UNIFORM_FLOAT);
    float radInRatio = brush.radInRatio;
    float curve      = clampf((float)brush.crv, 0.0f, 1.0f);
    SetShaderValue(brushGeoShader, locURadIn,  &radInRatio, SHADER_UNIFORM_FLOAT);
    SetShaderValue(brushGeoShader, locUCurve,  &curve,      SHADER_UNIFORM_FLOAT);
    float focalOff = brush.focalOffset;
    if (locFocalOffset >= 0) SetShaderValue(brushGeoShader, locFocalOffset, &focalOff, SHADER_UNIFORM_FLOAT);
    SetTextureFilter(geoRT->texture, TEXTURE_FILTER_POINT);
    BeginTextureMode(*geoRT);
    ClearBackground((Color){0, 0, 0, 0});
    rlSetBlendMode(RL_BLEND_CUSTOM);
    rlSetBlendFactors(RL_ONE, RL_ZERO, RL_FUNC_ADD);  // write rgba as-is
    BeginShaderMode(brushGeoShader);
    DrawTexturePro(whiteTex,
        (Rectangle){0, 0, (float)whiteTex.width, (float)whiteTex.height},
        (Rectangle){0, 0, (float)drawSz, -(float)drawSz},
        (Vector2){0, 0}, 0.0f, WHITE);
    EndShaderMode();
    rlSetBlendMode(RL_BLEND_ALPHA);  // restore
    EndTextureMode();

    // Switch geo to bilinear for sub-pixel blend sampling
    SetTextureFilter(geoRT->texture, TEXTURE_FILTER_BILINEAR);

    // -------- Pass 2a: blend to intermediate --------
    RenderTexture2D* intermediateRT = AllocPoolRT(intermediatePool, bucket, false);

    float opacity    = clampf((float)brush.opacity, 0.0f, 1.0f);
    float seed       = (float)brush.baseSeed + (float)rand()/(float)RAND_MAX
                       + stampX * 0.01f + stampY * 7.13f;
    int   bmidx      = (int)brush.bmidx;
    float preserveop = (brush.preserveop > 0) ? 1.0f : 0.0f;
    float smudge     = brush.cop;
    float odx = stampX - srcX, ody = stampY - srcY;
    float offsetUV[2] = { odx, ody };

    float col[4] = {
        brush.col.r / 255.0f,
        brush.col.g / 255.0f,
        brush.col.b / 255.0f,
        brush.col.a / 255.0f
    };
    float tbv = brush.texBlendVal;
    float tf  = brush.texFeather;
    float tt  = brush.texThresh;
    float ts  = brush.texScale;
    float texOff[2] = {0.0f, 0.0f};
    if (brush.texNoisemode == 1) {
        texOff[0] = ((float)rand()/(float)RAND_MAX - 0.5f) * 0.3f;
        texOff[1] = ((float)rand()/(float)RAND_MAX - 0.5f) * 0.3f;
    }
    int useLum = brush.useTexLumAsAlpha ? 1 : 0;
    int cm  = (int)brush.texColorMode;
    int tnm = (int)brush.texNoisemode;
    float sc[2] = { stampX / (float)W, (float)(H - stampY) / (float)H };

    SetShaderValue(brushBlendShader, locOpacity,        &opacity,    SHADER_UNIFORM_FLOAT);
    SetShaderValue(brushBlendShader, locBrushColor,     col,         SHADER_UNIFORM_VEC4);
    float zero = 0.0f;
    SetShaderValue(brushBlendShader, locSol,            &zero,       SHADER_UNIFORM_FLOAT);
    SetShaderValue(brushBlendShader, locSol2op,         &zero,       SHADER_UNIFORM_FLOAT);
    SetShaderValue(brushBlendShader, locSeed,           &seed,       SHADER_UNIFORM_FLOAT);
    SetShaderValue(brushBlendShader, locBmidx,          &bmidx,      SHADER_UNIFORM_INT);
    SetShaderValue(brushBlendShader, locPreserveOp,     &preserveop, SHADER_UNIFORM_FLOAT);
    SetShaderValue(brushBlendShader, locSmudgeStrength, &smudge,     SHADER_UNIFORM_FLOAT);
    SetShaderValue(brushBlendShader, locSmudgeOffsetUV, offsetUV,    SHADER_UNIFORM_VEC2);
    float deltaDeg = srcAngleDeg - brush.resangle;
    while (deltaDeg > 180.0f) deltaDeg -= 360.0f;
    while (deltaDeg < -180.0f) deltaDeg += 360.0f;
    float angleDelta = deltaDeg * (3.14159265f / 180.0f);
    SetShaderValue(brushBlendShader, locSmudgeSrcRad,    &srcRad,     SHADER_UNIFORM_FLOAT);
    SetShaderValue(brushBlendShader, locSmudgeAngleDelta, &angleDelta, SHADER_UNIFORM_FLOAT);
    SetShaderValue(brushBlendShader, locRadOut,          &radOut,     SHADER_UNIFORM_FLOAT);
    SetShaderValue(brushBlendShader, locTexBlendVal,    &tbv,        SHADER_UNIFORM_FLOAT);
    SetShaderValue(brushBlendShader, locTexScale,       &ts,         SHADER_UNIFORM_FLOAT);
    SetShaderValue(brushBlendShader, locTexOffset,      texOff,      SHADER_UNIFORM_VEC2);
    { float uo[2] = { brush.userTexOriginX, 1.0f - brush.userTexOriginY };
      SetShaderValue(brushBlendShader, locUserTexOrigin, uo,          SHADER_UNIFORM_VEC2); }
    { int hasTex = useTexture ? 1 : 0;
      SetShaderValue(brushBlendShader, locHasTexture, &hasTex,         SHADER_UNIFORM_INT); }
    SetShaderValue(brushBlendShader, locTexFeather,     &tf,         SHADER_UNIFORM_FLOAT);
    SetShaderValue(brushBlendShader, locTexThresh,      &tt,         SHADER_UNIFORM_FLOAT);
    SetShaderValue(brushBlendShader, locUseLumAsAlpha,  &useLum,     SHADER_UNIFORM_INT);
    SetShaderValue(brushBlendShader, locTexColorMode,   &cm,         SHADER_UNIFORM_INT);
    SetShaderValue(brushBlendShader, locTexNoisemode,   &tnm,        SHADER_UNIFORM_INT);
    SetShaderValue(brushBlendShader, locStampCenter,    sc,          SHADER_UNIFORM_VEC2);

    float csz[2] = { (float)W, (float)H };
    SetShaderValue(brushBlendShader, locCanvasSize, csz, SHADER_UNIFORM_VEC2);

    float so[2] = { x0, y0 };
    int   ix0 = (int)floorf(x0), iy0 = (int)floorf(y0);
    int   isz = (int)floorf(stampSizePx);
    float bo[2] = { (float)ix0, (float)iy0 };
    float bs[2] = { (float)isz, (float)isz };
    float fs[2] = { x0 - (float)ix0, y0 - (float)iy0 };
    if (pixelPerfect) { fs[0] = fs[1] = 0.0f; }
    SetShaderValue(brushBlendShader, locBlitOrigin, bo, SHADER_UNIFORM_VEC2);
    SetShaderValue(brushBlendShader, locBlitSize,   bs, SHADER_UNIFORM_VEC2);
    SetShaderValue(brushBlendShader, locFracShift,  fs, SHADER_UNIFORM_VEC2);

    float pwr = brush.pwr;
    SetShaderValue(brushBlendShader, locPwr, &pwr, SHADER_UNIFORM_FLOAT);

    int eraseMode = brush.eraseMode;
    SetShaderValue(brushBlendShader, locEraseMode, &eraseMode, SHADER_UNIFORM_INT);

    // Set dstRT.texture wrap for shader reads
    SetTextureFilter(dstRT.texture, TEXTURE_FILTER_POINT);
    if (seamless)
        SetTextureWrap(dstRT.texture, TEXTURE_WRAP_REPEAT);
    else
        SetTextureWrap(dstRT.texture, TEXTURE_WRAP_CLAMP);

    // Render stamp to intermediate RT
    SetTextureFilter(intermediateRT->texture, TEXTURE_FILTER_POINT);
    BeginTextureMode(*intermediateRT);
    rlSetBlendMode(RL_BLEND_CUSTOM);
    rlSetBlendFactors(RL_ONE, RL_ZERO, RL_FUNC_ADD);
    ClearBackground((Color){0,0,0,0});

    rlActiveTextureSlot(1);
    rlEnableTexture(dstRT.texture.id);
    rlActiveTextureSlot(2);
    if (brushTex.id > 0) SetTextureWrap(brushTex, TEXTURE_WRAP_REPEAT);
    rlEnableTexture(brushTex.id > 0 ? brushTex.id : whiteTex.id);
    rlActiveTextureSlot(0);

    BeginShaderMode(brushBlendShader);
    int uSeamlessVal = seamless ? 1 : 0;
    SetShaderValue(brushBlendShader, locSeamless, &uSeamlessVal, SHADER_UNIFORM_INT);
    int uPpVal = pixelPerfect ? 1 : 0;
    SetShaderValue(brushBlendShader, locPixelPerfect, &uPpVal, SHADER_UNIFORM_INT);

    DrawTexturePro(geoRT->texture,
        (Rectangle){0, 0, (float)drawSz, (float)-drawSz},
        (Rectangle){0, 0, (float)drawSz, (float)drawSz},
        (Vector2){0, 0}, 0.0f, WHITE);

    EndShaderMode();

    rlActiveTextureSlot(1); rlDisableTexture();
    rlActiveTextureSlot(2); rlDisableTexture();
    rlActiveTextureSlot(0);

    EndTextureMode();

    // Restore dstRT filter
    SetTextureFilter(dstRT.texture, TEXTURE_FILTER_POINT);

    // -------- Pass 2b: blit intermediate to dstRT --------
    BeginTextureMode(dstRT);
    rlSetBlendMode(RL_BLEND_CUSTOM);
    rlSetBlendFactors(RL_ONE, RL_ZERO, RL_FUNC_ADD);

    if (seamless) {
        for (int dy = -1; dy <= 1; dy++) {
            for (int dx = -1; dx <= 1; dx++) {
                int tx = ix0 + dx * W;
                int ty = iy0 + dy * H;
                if (tx >= W || ty >= H) continue;
                if (tx + isz <= 0 || ty + isz <= 0) continue;
                DrawTexturePro(intermediateRT->texture,
                    (Rectangle){0, 0, (float)drawSz, (float)-drawSz},
                    (Rectangle){(float)tx, (float)ty, (float)isz, (float)isz},
                    (Vector2){0, 0}, 0.0f, WHITE);
            }
        }
    } else {
        DrawTexturePro(intermediateRT->texture,
            (Rectangle){0, 0, (float)drawSz, (float)-drawSz},
            (Rectangle){(float)ix0, (float)iy0, (float)isz, (float)isz},
            (Vector2){0, 0}, 0.0f, WHITE);
    }

    EndTextureMode();
    rlSetBlendMode(RL_BLEND_ALPHA);
}
