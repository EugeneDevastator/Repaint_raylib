#include "repaint.h"
#include "rlgl.h"
#include "stroke.h"
#include "rlImGui.h"
#include "imgui.h"

int uiPanelWidth = 250;
bool panelResizing = false;

BParam bpOpacity;
BParam bpSize;
BParam bpHardness;
BParam bpSpacing;
BParam bpCurvature;
BParam bpScatter;
BParam bpQuickHue;
BParam bpQuickSat;
BParam bpQuickLit;

Viewport viewport;

void EnsureRTs(AppState* state) {
    int newCount = state->canvas.layerCount;
    if (state->texCount == newCount) return;
    int old = state->texCount;
    state->layerRTs = (RenderTexture2D*)realloc(state->layerRTs, newCount * sizeof(RenderTexture2D));
    state->layerTextures = (Texture2D*)realloc(state->layerTextures, newCount * sizeof(Texture2D));
    state->texDirty = (bool*)realloc(state->texDirty, newCount * sizeof(bool));
    if (newCount > old) {
        memset(&state->layerRTs[old], 0, (newCount - old) * sizeof(RenderTexture2D));
        memset(&state->layerTextures[old], 0, (newCount - old) * sizeof(Texture2D));
        for (int i = old; i < newCount; i++) {
            state->layerRTs[i] = LoadRenderTexture(state->canvas.width, state->canvas.height);
            BeginTextureMode(state->layerRTs[i]);
            ClearBackground(BLANK);
            EndTextureMode();
        }
    }
    state->texCount = newCount;
}

void SyncRTFromImage(AppState* state, int layer) {
    if (layer < 0 || layer >= state->texCount) return;
    Image* img = &state->canvas.layerImages[layer];
    if (state->layerRTs[layer].id == 0) {
        state->layerRTs[layer] = LoadRenderTexture(img->width, img->height);
    }
    Texture2D tmp = LoadTextureFromImage(*img);
    BeginTextureMode(state->layerRTs[layer]);
    rlSetBlendMode(RL_BLEND_CUSTOM);
    rlSetBlendFactors(RL_ONE, RL_ZERO, RL_FUNC_ADD);
    DrawTexture(tmp, 0, 0, WHITE);
    rlSetBlendMode(RL_BLEND_ALPHA);
    EndTextureMode();
    UnloadTexture(tmp);
}

void SyncImageFromRT(AppState* state, int layer) {
    if (layer < 0 || layer >= state->texCount) return;
    if (state->layerRTs[layer].id == 0) return;
    Image cap = LoadImageFromTexture(state->layerRTs[layer].texture);
    ImageFlipVertical(&cap);
    Image* dst = &state->canvas.layerImages[layer];
    UnloadImage(*dst);
    *dst = cap;
}

void SyncAllImages(AppState* state) {
    for (int i = 0; i < state->texCount; i++)
        SyncImageFromRT(state, i);
}

void SyncAllRTs(AppState* state) {
    EnsureRTs(state);
    for (int i = 0; i < state->texCount; i++) {
        SyncRTFromImage(state, i);
        if (state->layerTextures[i].id > 0) UnloadTexture(state->layerTextures[i]);
        state->layerTextures[i] = LoadTextureFromImage(state->canvas.layerImages[i]);
    }
}

void UpdateUI(AppState* state) {
    Vector2 mousePos = GetMousePosition();

    gizmoShow = IsKeyDown(KEY_LEFT_SHIFT) || IsKeyDown(KEY_RIGHT_SHIFT);

    if (gizmoShow)
        Gizmo_HandleInput(state, mousePos);
    else
        gizmoMouseMode = 0;

    if (IsKeyPressed(KEY_ONE)) state->mode = eBrush;
    if (IsKeyPressed(KEY_TWO)) state->mode = eSmudge;
    if (IsKeyPressed(KEY_THREE)) state->mode = eLine;
    if (IsKeyPressed(KEY_FOUR)) state->mode = eDisp;
    if (IsKeyPressed(KEY_FIVE)) state->mode = eCont;

    state->currentBrush.Realb.rad_out = BParam_GetValue(&bpSize);
    state->currentBrush.Realb.rad_in = state->currentBrush.Realb.rad_out * BParam_GetValue(&bpHardness);
    state->currentBrush.Realb.crv = BParam_GetValue(&bpCurvature);
    state->currentBrush.Realb.opacity = BParam_GetValue(&bpOpacity);

    colorHue = bpQuickHue.slider.clipmaxF;
    colorSat = bpQuickSat.slider.clipmaxF;
    colorLit = bpQuickLit.slider.clipmaxF;
    state->currentBrush.Realb.col = HSLToRGB(colorHue, colorSat, colorLit);
}

static LocalBroker localBroker;

void App_Init(AppState* state) {
    SetConfigFlags(FLAG_WINDOW_RESIZABLE);
    InitWindow(SCREEN_WIDTH, SCREEN_HEIGHT, "RePaint");
    MaximizeWindow();

    UIStyle::Init();
    LeftPanel_Init();
    SetTargetFPS(60);

    Painter_Init();
    BrushBlend_Init();
    LoadPenIcons();
    LoadGizmoIcons();

    localBroker.appState = state;

    BParam_Init(&bpOpacity, 0, "Opacity", 0.0f, 1.0f, 1.0f);
    BParam_SetIcon(&bpOpacity, "ctlop");

    BParam_Init(&bpSize, 1, "Size", 1.0f, 100.0f, 20.0f);
    bpSize.slider.clipmaxF = 19.0f / 99.0f;
    BParam_SetIcon(&bpSize, "ctlrad");

    BParam_Init(&bpHardness, 2, "Hardness", 0.0f, 1.0f, 0.5f);
    bpHardness.slider.clipmaxF = 0.5f;
    BParam_SetIcon(&bpHardness, "ctlrrel");

    BParam_Init(&bpSpacing, 3, "Spacing", 0.05f, 2.0f, 0.3f);
    BParam_SetIcon(&bpSpacing, "ctlspc");

    BParam_Init(&bpCurvature, 4, "Curve", 0.0f, 1.0f, 0.0f);
    BParam_SetIcon(&bpCurvature, "ctlcrv");

    BParam_Init(&bpScatter, 5, "Scatter", 0.0f, 5.0f, 0.0f);
    BParam_SetIcon(&bpScatter, "ctlspcjit");

    BParam_Init(&bpQuickHue, 10, "Hue", 0.0f, 1.0f, 0.35f);
    bpQuickHue.slider.clipmaxF = 0.35f;
    BParam_Init(&bpQuickSat, 11, "Sat", 0.0f, 1.0f, 1.0f);
    bpQuickSat.slider.clipmaxF = 1.0f;
    BParam_Init(&bpQuickLit, 12, "Lit", 0.0f, 1.0f, 0.5f);
    bpQuickLit.slider.clipmaxF = 0.5f;

    state->canvas = Canvas_Create(800, 600, WHITE);
    state->activeLayer = 0;

    Rectangle viewportBounds = {
        (float)uiPanelWidth, 0,
        (float)(SCREEN_WIDTH - uiPanelWidth - RIGHT_PANEL_WIDTH),
        (float)SCREEN_HEIGHT
    };
    Viewport_Init(&viewport, viewportBounds);
    viewport.broker = &localBroker;

    state->camera = Camera2D{};
    state->camera.target = Vector2{(float)state->canvas.width * 0.5f, (float)state->canvas.height * 0.5f};
    state->camera.offset = Vector2{viewportBounds.x + viewportBounds.width * 0.5f, viewportBounds.y + viewportBounds.height * 0.5f};
    state->camera.rotation = 0.0f;
    state->camera.zoom = 1.0f;

    state->mode = eBrush;

    state->currentBrush.Realb.rad_in = 1.0f;
    state->currentBrush.Realb.rad_out = 20.0f;
    state->currentBrush.Realb.opacity = 1.0f;
    state->currentBrush.Realb.resangle = 0.0f;
    state->currentBrush.Realb.crv = 0.0f;
    state->currentBrush.Realb.x2y = 1.0f;
    state->currentBrush.Realb.scale = 1.0f;
    state->currentBrush.Realb.cop = 0.0f;
    state->currentBrush.Realb.pwr = 0.0f;
    state->currentBrush.Realb.sol = 1.0f;
    state->currentBrush.Realb.sol2op = 0.0f;
    state->currentBrush.Realb.seed = 0;
    state->currentBrush.Realb.bmidx = bmNormal;
    state->currentBrush.Realb.pipeID = plCFNSR;
    state->currentBrush.Realb.preserveop = 0;
    state->currentBrush.Realb.col = BLACK;

    colorHue = 0.35f;
    colorSat = 1.0f;
    colorLit = 0.5f;

    state->layerTextures = NULL;
    state->layerRTs = NULL;
    state->texDirty = NULL;
    state->texCount = 0;

    SyncAllRTs(state);
}

void App_Draw(AppState* state) {
    EnsureRTs(state);

    int sw = GetScreenWidth();
    int sh = GetScreenHeight();

    Rectangle viewportBounds = {
        (float)uiPanelWidth, 0,
        (float)(sw - uiPanelWidth - RIGHT_PANEL_WIDTH),
        (float)sh
    };
    viewport.bounds = viewportBounds;
    state->camera.offset = Vector2{
        viewportBounds.x + viewportBounds.width * 0.5f,
        viewportBounds.y + viewportBounds.height * 0.5f
    };

    if (viewport.broker) viewport.broker->poll(state);
    if (viewport.strokeEnded) {
        SyncLayerTexture(state, viewport.endLayer);
        viewport.strokeEnded = false;
    }

    BeginDrawing();
    ClearBackground(Color{220, 220, 220, 255});

    Viewport_Draw(&viewport, state);

    rlImGuiBegin();
    Gizmo_Draw(state);
    LeftPanel_Draw(state);
    LayerPanel_Draw(state);
    Gizmo_DrawPenPopups(state);
    rlImGuiEnd();

    EndDrawing();
}

void App_Close(AppState* state) {
    SyncAllImages(state);
    Canvas_Destroy(&state->canvas);
    for (int i = 0; i < state->texCount; i++) {
        if (state->layerRTs[i].id > 0) UnloadRenderTexture(state->layerRTs[i]);
        if (state->layerTextures[i].id > 0) UnloadTexture(state->layerTextures[i]);
    }
    free(state->layerTextures);
    free(state->layerRTs);
    free(state->texDirty);

    LeftPanel_Shutdown();
    UnloadViewportRenderer();
    if (bpOpacity.iconLoaded) UnloadTexture(bpOpacity.iconTex);
    if (bpSize.iconLoaded) UnloadTexture(bpSize.iconTex);
    if (bpHardness.iconLoaded) UnloadTexture(bpHardness.iconTex);
    if (bpSpacing.iconLoaded) UnloadTexture(bpSpacing.iconTex);
    if (bpCurvature.iconLoaded) UnloadTexture(bpCurvature.iconTex);
    if (bpScatter.iconLoaded) UnloadTexture(bpScatter.iconTex);
    UnloadPenIcons();
    UnloadGizmoIcons();
    Painter_Shutdown();
    BrushBlend_Shutdown();
    CloseWindow();
    UIStyle::Shutdown();
}
