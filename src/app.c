#include "repaint.h"
#include "rlgl.h"

BParam bpOpacity;
BParam bpSize;
BParam bpHardness;
UIButton btnBrush = {{10, 60, 180, 40}, GRAY, LIGHTGRAY, false, false, "Brush", 0};
UIButton btnSmudge = {{10, 110, 180, 40}, GRAY, LIGHTGRAY, false, false, "Smudge", 1};
UIButton btnLine = {{10, 160, 180, 40}, GRAY, LIGHTGRAY, false, false, "Line", 2};
UIButton btnEraser = {{10, 210, 180, 40}, GRAY, LIGHTGRAY, false, false, "Eraser", 3};

Viewport viewport;
static RenderTexture2D stampPrev = {0};
static bool stampPrevInited = false;

void EnsureRTs(AppState* state) {
    int newCount = state->canvas.layerCount;
    if (state->texCount == newCount) return;
    int old = state->texCount;
    state->layerRTs = realloc(state->layerRTs, newCount * sizeof(RenderTexture2D));
    state->layerTextures = realloc(state->layerTextures, newCount * sizeof(Texture2D));
    state->texDirty = realloc(state->texDirty, newCount * sizeof(bool));
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
    rlSetBlendMode(RL_BLEND_ALPHA);   // restore INSIDE the texture mode
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
    for (int i = 0; i < state->texCount; i++)
        SyncRTFromImage(state, i);
}

void UpdateUI(AppState* state) {
    Vector2 mousePos = GetMousePosition();
    bool mousePressed = IsMouseButtonDown(MOUSE_LEFT_BUTTON);

    UIButton_Update(&btnBrush, mousePos, mousePressed);
    UIButton_Update(&btnSmudge, mousePos, mousePressed);
    UIButton_Update(&btnLine, mousePos, mousePressed);
    UIButton_Update(&btnEraser, mousePos, mousePressed);

    BParam_HandleInput(&bpOpacity, mousePos);
    BParam_HandleInput(&bpSize, mousePos);
    BParam_HandleInput(&bpHardness, mousePos);

    if (btnBrush.clicked) { state->mode = eBrush; btnBrush.clicked = false; }
    if (btnSmudge.clicked) { state->mode = eSmudge; btnSmudge.clicked = false; }
    if (btnLine.clicked) { state->mode = eLine; btnLine.clicked = false; }
    if (btnEraser.clicked) { state->mode = eBrush; btnEraser.clicked = false; state->currentBrush.Realb.col.a = 0; }

    LayerPanel_HandleInput(state, mousePos);

    state->currentBrush.Realb.opacity = BParam_GetValue(&bpOpacity);
    state->currentBrush.Realb.rad_out = BParam_GetValue(&bpSize);
    state->currentBrush.Realb.rad_in = BParam_GetValue(&bpSize) * BParam_GetValue(&bpHardness);

    gizmoShow = IsKeyDown(KEY_TAB);

    if (gizmoShow)
        Gizmo_HandleInput(state, mousePos);
    else
        gizmoMouseMode = 0;

    if (IsKeyPressed(KEY_ONE)) state->mode = eBrush;
    if (IsKeyPressed(KEY_TWO)) state->mode = eSmudge;
    if (IsKeyPressed(KEY_THREE)) state->mode = eLine;
    if (IsKeyPressed(KEY_FOUR)) state->mode = eDisp;
    if (IsKeyPressed(KEY_FIVE)) state->mode = eCont;
    if (IsKeyDown(KEY_LEFT_CONTROL) && IsKeyPressed(KEY_Z)) {
    }

    state->currentBrush.Realb.col = HSLToRGB(colorHue, colorSat, colorLit);
}

void App_Init(AppState* state) {
    InitWindow(SCREEN_WIDTH, SCREEN_HEIGHT, "RePaint - Pure C Port");
    SetTargetFPS(60);

    Painter_Init();
    BrushBlend_Init();
    LoadPenIcons();

    BParam_Init(&bpOpacity, 0, "Opacity", 0.0f, 1.0f, 1.0f);
    bpOpacity.slider.rect = (Rectangle){40, 420, 124, 30};
    bpOpacity.slider.gradStart = (Color){30, 30, 80, 255};
    bpOpacity.slider.gradEnd = (Color){100, 180, 255, 255};
    bpOpacity.penRect = (Rectangle){168, 420 + 1, 28, 28};
    BParam_SetIcon(&bpOpacity, "ctlop");

    BParam_Init(&bpSize, 1, "Size", 1.0f, 100.0f, 20.0f);
    bpSize.slider.rect = (Rectangle){40, 475, 124, 30};
    bpSize.slider.gradStart = (Color){100, 40, 10, 255};
    bpSize.slider.gradEnd = (Color){255, 180, 60, 255};
    bpSize.slider.clipmaxF = 19.0f / 99.0f;
    bpSize.penRect = (Rectangle){168, 475 + 1, 28, 28};
    BParam_SetIcon(&bpSize, "ctlrad");

    BParam_Init(&bpHardness, 2, "Hardness", 0.0f, 1.0f, 0.5f);
    bpHardness.slider.rect = (Rectangle){40, 530, 124, 30};
    bpHardness.slider.gradStart = (Color){40, 100, 20, 255};
    bpHardness.slider.gradEnd = (Color){180, 255, 100, 255};
    bpHardness.slider.clipmaxF = 0.5f;
    bpHardness.penRect = (Rectangle){168, 530 + 1, 28, 28};
    BParam_SetIcon(&bpHardness, "ctlrrel");

    UISlider_Init(&sliderHue);
    sliderHue.rect = (Rectangle){RIGHT_PANEL_X + 10, 626, 180, 24};
    sliderHue.showValue = false;
    sliderHue.noGradient = true;
    sliderHue.DsRange = 0.0833333f;
    sliderHue.clipminF = 0.0f;
    sliderHue.clipmaxF = colorHue;

    UISlider_Init(&sliderSat);
    sliderSat.rect = (Rectangle){RIGHT_PANEL_X + 10, 654, 180, 24};
    sliderSat.showValue = false;
    sliderSat.noGradient = true;
    sliderSat.clipminF = 0.0f;
    sliderSat.clipmaxF = 1.0f;

    UISlider_Init(&sliderLit);
    sliderLit.rect = (Rectangle){RIGHT_PANEL_X + 10, 682, 180, 24};
    sliderLit.showValue = false;
    sliderLit.noGradient = true;
    strcpy(sliderHue.label, "Hue");
    strcpy(sliderSat.label, "Sat");
    strcpy(sliderLit.label, "Lit");
    sliderLit.clipminF = 0.0f;
    sliderLit.clipmaxF = 0.5f;

    UISlider_Init(&layerOpSlider);
    layerOpSlider.rect = (Rectangle){RIGHT_PANEL_X + 10, 86, 180, 24};
    layerOpSlider.showValue = false;
    layerOpSlider.noGradient = false;
    layerOpSlider.clipminF = 0.0f;
    layerOpSlider.clipmaxF = 1.0f;
    strcpy(layerOpSlider.label, "Layer Op");
    layerOpSlider.gradStart = (Color){40, 40, 80, 255};
    layerOpSlider.gradEnd = (Color){180, 180, 255, 255};

    state->canvas = Canvas_Create(800, 600, WHITE);
    state->activeLayer = 0;
	state->camera = (Camera2D){
		.target   = (Vector2){0, 0},  // shift world left
		.offset   = (Vector2){0, 0},
		.rotation = 0.0f,
		.zoom     = 1.0f
	};

    state->mode = eBrush;

    Rectangle viewportBounds = {
        (float)UI_PANEL_WIDTH, 0,
        (float)(SCREEN_WIDTH - UI_PANEL_WIDTH - RIGHT_PANEL_WIDTH),
        (float)SCREEN_HEIGHT
    };
    Viewport_Init(&viewport, viewportBounds);

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
    sliderHue.clipmaxF = colorHue;
    sliderSat.clipmaxF = colorSat;
    sliderLit.clipmaxF = colorLit;

    state->layerTextures = NULL;
    state->layerRTs = NULL;
    state->texDirty = NULL;
    state->texCount = 0;

    EnsureRTs(state);
    for (int i = 0; i < state->texCount; i++) {
        SyncRTFromImage(state, i);
        state->layerTextures[i] = LoadTextureFromImage(state->canvas.layerImages[i]);
    }

    // Init stamp preview render texture
    if (!stampPrevInited) {
        stampPrev = LoadRenderTexture(100, 100);
        stampPrevInited = true;
    }

}

void App_Draw(AppState* state) {
    EnsureRTs(state);

    BeginDrawing();
    ClearBackground((Color){30, 30, 30, 255});

    Viewport_Draw(&viewport, state);

    Gizmo_Draw(state);

    DrawRectangle(0, 0, UI_PANEL_WIDTH, SCREEN_HEIGHT, (Color){50, 50, 50, 255});
    DrawRectangle(UI_PANEL_WIDTH, 0, 1, SCREEN_HEIGHT, DARKGRAY);

    DrawText("RePaint", 10, 10, 24, WHITE);
    DrawText("Tools", 10, 40, 20, LIGHTGRAY);

    UIButton_Draw(&btnBrush);
    UIButton_Draw(&btnSmudge);
    UIButton_Draw(&btnLine);
    UIButton_Draw(&btnEraser);

    DrawText("Settings", 10, 280, 20, LIGHTGRAY);

    // Update stamp preview
    if (stampPrevInited && stampPrev.id > 0) {
        BeginTextureMode(stampPrev);
        ClearBackground(BLANK);
        EndTextureMode();

        d_Brush pb = state->currentBrush;
        Color pc = pb.Realb.col;
        pb.Realb.col = WHITE;
        pb.Realb.opacity = 1.0f;
        float prevRadOut = pb.Realb.rad_out;
        if (prevRadOut > 45.0f) {
            pb.Realb.rad_out = 45.0f;
            pb.Realb.rad_in = pb.Realb.rad_in * (45.0f / fmaxf(prevRadOut, 1.0f));
        }
        BrushBlend_ApplyStamp(stampPrev, &pb, 50, 50);
        pb.Realb.rad_out = prevRadOut;
        pb.Realb.col = pc;

        int prevX = 50;
        int prevY = 305;
        int prevSize = 100;
        DrawRectangle(prevX - 2, prevY - 2, prevSize + 4, prevSize + 4, (Color){40, 40, 45, 255});
        DrawRectangleLines(prevX - 2, prevY - 2, prevSize + 4, prevSize + 4, (Color){80, 80, 90, 255});
        Rectangle src = {0, 0, 100, -100};
        Rectangle dst = {(float)prevX, (float)prevY, (float)prevSize, (float)prevSize};
        DrawTexturePro(stampPrev.texture, src, dst, (Vector2){0, 0}, 0, WHITE);
    }

    BParam_Draw(&bpOpacity);
    BParam_Draw(&bpSize);
    BParam_Draw(&bpHardness);

    LayerPanel_Draw(state);

    char zoomInfo[32];
    sprintf(zoomInfo, "Zoom: %.0f%%", state->camera.zoom * 100.0f);
    DrawText(zoomInfo, 10, SCREEN_HEIGHT - 40, 16, WHITE);

    const char* modeNames[] = {"None", "Brush", "Smudge", "Disp", "Cont", "Line"};
    DrawText(modeNames[state->mode > 5 ? 0 : state->mode], 10, SCREEN_HEIGHT - 20, 16, GREEN);

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
    if (stampPrevInited && stampPrev.id > 0) {
        UnloadRenderTexture(stampPrev);
        stampPrevInited = false;
    }
    UnloadViewportRenderer();
    if (bpOpacity.iconLoaded) UnloadTexture(bpOpacity.iconTex);
    if (bpSize.iconLoaded) UnloadTexture(bpSize.iconTex);
    if (bpHardness.iconLoaded) UnloadTexture(bpHardness.iconTex);
    UnloadPenIcons();
    Painter_Shutdown();
    BrushBlend_Shutdown();
    CloseWindow();
}
