#include "repaint.h"
#include "rlgl.h"

static RenderTexture2D accumA = {0};
static RenderTexture2D accumB = {0};
static bool accumInited = false;
static Texture2D checkerTex = {0};
static bool checkerValid = false;
static Shader layerBlendShader = {0};
static bool shaderInited = false;
static int locLayerTex = -1;
static int locLayerAlpha = -1;
static int locBmIdx = -1;
static int curCanvasW = 0;
static int curCanvasH = 0;
static RenderTexture2D* finalAcc = NULL;

bool layersDirty = true;

static void EnsureChecker(int w, int h) {
    if (checkerValid && checkerTex.width == w && checkerTex.height == h) return;
    if (checkerTex.id > 0) UnloadTexture(checkerTex);

    Image img = GenImageColor(w, h, BLANK);
    for (int y = 0; y < h; y += 8) {
        for (int x = 0; x < w; x += 8) {
            bool light = ((x / 8) + (y / 8)) % 2 == 0;
            Color col = light ? Color{70, 70, 75, 255} : Color{55, 55, 60, 255};
            ImageDrawRectangle(&img, x, y, 8, 8, col);
        }
    }
    checkerTex = LoadTextureFromImage(img);
    UnloadImage(img);
    checkerValid = true;
}

static void EnsureAccumulators(int w, int h) {
    if (accumInited && curCanvasW == w && curCanvasH == h) return;
    if (accumInited) {
        UnloadRenderTexture(accumA);
        UnloadRenderTexture(accumB);
    }
    accumA = LoadRenderTexture(w, h);
    accumB = LoadRenderTexture(w, h);
    curCanvasW = w;
    curCanvasH = h;
    accumInited = true;
    finalAcc = NULL;
    layersDirty = true;
}

static void EnsureShader(void) {
    if (shaderInited) return;
    layerBlendShader = LoadShader(0, "shaders/layer_blend.fs");
    if (layerBlendShader.id == 0) {
        TraceLog(LOG_ERROR, "layer_blend.fs failed to load/compile");
        return;
    }
    locLayerTex   = GetShaderLocation(layerBlendShader, "layerTex");
    locLayerAlpha = GetShaderLocation(layerBlendShader, "layerAlpha");
    locBmIdx      = GetShaderLocation(layerBlendShader, "bmidx");
    TraceLog(LOG_INFO, "Shader locs: layerTex=%d alpha=%d bm=%d", locLayerTex, locLayerAlpha, locBmIdx);
    shaderInited = true;
}


void DrawViewport(AppState* state, Rectangle screenRect, Camera2D camera) {
    int cw = state->canvas.width;
    int ch = state->canvas.height;
    if (cw < 1 || ch < 1) return;

    EnsureAccumulators(cw, ch);
    EnsureChecker(cw, ch);
    EnsureShader();

    if (layersDirty && shaderInited) {
        RenderTexture2D* src = &accumA;
        RenderTexture2D* dst = &accumB;

        BeginTextureMode(*src);
        ClearBackground(BLANK);
        DrawTexture(checkerTex, 0, 0, WHITE);
        EndTextureMode();

        for (int i = 0; i < state->canvas.layerCount; i++) {
            if (!state->canvas.layerProps[i].visible) continue;
            if (!(state->texCount > i && state->layerRTs[i].id > 0)) continue;

            float alpha = state->canvas.layerProps[i].op;
            int bmidx = state->canvas.layerProps[i].blendmode;

            BeginTextureMode(*dst);
            ClearBackground(BLANK);
            BeginShaderMode(layerBlendShader);

            SetShaderValueTexture(layerBlendShader, locLayerTex, state->layerRTs[i].texture);
            SetShaderValue(layerBlendShader, locLayerAlpha, &alpha, SHADER_UNIFORM_FLOAT);
            SetShaderValue(layerBlendShader, locBmIdx, &bmidx, SHADER_UNIFORM_INT);

            DrawTextureRec(src->texture,
                Rectangle{0, 0, (float)cw, (float)-ch},
                Vector2{0, 0}, WHITE);

            EndShaderMode();
            EndTextureMode();

            RenderTexture2D* tmp = src;
            src = dst;
            dst = tmp;
        }

        finalAcc = src;
        layersDirty = false;
    }

    if (!accumInited || finalAcc == NULL) return;

    int sw = (int)screenRect.width;
    int sh = (int)screenRect.height;
    if (sw < 1 || sh < 1) return;

    float dstX = -camera.target.x * camera.zoom + camera.offset.x;
    float dstY = -camera.target.y * camera.zoom + camera.offset.y;
    float dstW = cw * camera.zoom;
    float dstH = ch * camera.zoom;

    Rectangle srcRect = {0, 0, (float)cw, (float)-ch};
    Rectangle dstRect = {dstX, dstY, dstW, dstH};
    DrawTexturePro(finalAcc->texture, srcRect, dstRect, Vector2{0, 0}, 0.0f, WHITE);
}

void UnloadViewportRenderer(void) {
    if (accumInited) {
        UnloadRenderTexture(accumA);
        UnloadRenderTexture(accumB);
        accumInited = false;
    }
    if (checkerValid) {
        UnloadTexture(checkerTex);
        checkerValid = false;
    }
    if (shaderInited) {
        UnloadShader(layerBlendShader);
        shaderInited = false;
    }
}
