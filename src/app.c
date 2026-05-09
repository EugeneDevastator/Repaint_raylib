#include "repaint.h"
#include "rlgl.h"

int uiPanelWidth = 250;
bool panelResizing = false;

BParam bpOpacity;
BParam bpSize;
BParam bpHardness;
BParam bpSpacing;
BParam bpCurvature;

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
    for (int i = 0; i < state->texCount; i++)
        SyncRTFromImage(state, i);
}

void UpdateUI(AppState* state) {
    Vector2 mousePos = GetMousePosition();

    {
        int handleX = uiPanelWidth;
        Rectangle handleRect = {(float)handleX - 3, 0, 7, (float)SCREEN_HEIGHT};
        if (IsMouseButtonPressed(MOUSE_LEFT_BUTTON) && CheckCollisionPointRec(mousePos, handleRect))
            panelResizing = true;
        if (panelResizing) {
            if (IsMouseButtonDown(MOUSE_LEFT_BUTTON)) {
                uiPanelWidth = (int)fmaxf(120.0f, fminf(mousePos.x, SCREEN_WIDTH - RIGHT_PANEL_WIDTH - 100));
                Rectangle vb = {(float)uiPanelWidth, 0,
                    (float)(SCREEN_WIDTH - uiPanelWidth - RIGHT_PANEL_WIDTH), (float)SCREEN_HEIGHT};
                Viewport_SetBounds(&viewport, vb);
            } else {
                panelResizing = false;
            }
        }
    }

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
    if (IsKeyDown(KEY_LEFT_CONTROL) && IsKeyPressed(KEY_Z)) {
    }

    state->currentBrush.Realb.rad_out = BParam_GetValue(&bpSize);
    state->currentBrush.Realb.rad_in = state->currentBrush.Realb.rad_out * BParam_GetValue(&bpHardness);
    state->currentBrush.Realb.crv = BParam_GetValue(&bpCurvature);
    state->currentBrush.Realb.opacity = BParam_GetValue(&bpOpacity);

    state->currentBrush.Realb.col = HSLToRGB(colorHue, colorSat, colorLit);
}

void App_Init(AppState* state) {
    InitWindow(SCREEN_WIDTH, SCREEN_HEIGHT, "RePaint");
    SetTargetFPS(60);

    GuiLoadStyleDefault();
    GuiSetStyle(DEFAULT, TEXT_COLOR_NORMAL, 0x1a1a1aff);
    GuiSetStyle(DEFAULT, BASE_COLOR_NORMAL, 0xe0e0e0ff);
    GuiSetStyle(DEFAULT, BORDER_COLOR_NORMAL, 0x666666ff);
    GuiSetStyle(DEFAULT, TEXT_COLOR_FOCUSED, 0x000000ff);
    if (FileExists("resources/Cadman_Bold.otf")) {
        Font cadman = LoadFontEx("resources/Cadman_Bold.otf", 28, 0, 0);
        GuiSetFont(cadman);
    }
    GuiSetStyle(DEFAULT, TEXT_SIZE, 28);

    Painter_Init();
    BrushBlend_Init();
    LoadPenIcons();

    BParam_Init(&bpOpacity, 0, "Opacity", 0.0f, 1.0f, 1.0f);
    bpOpacity.slider.rect = (Rectangle){40, 420, 124, 30};
    bpOpacity.slider.gradStart = BLACK;
    bpOpacity.slider.gradEnd = WHITE;
    BParam_SetIcon(&bpOpacity, "ctlop");

    BParam_Init(&bpSize, 1, "Size", 1.0f, 100.0f, 20.0f);
    bpSize.slider.rect = (Rectangle){40, 475, 124, 30};
    bpSize.slider.gradStart = BLACK;
    bpSize.slider.gradEnd = WHITE;
    bpSize.slider.clipmaxF = 19.0f / 99.0f;
    BParam_SetIcon(&bpSize, "ctlrad");

    BParam_Init(&bpHardness, 2, "Hardness", 0.0f, 1.0f, 0.5f);
    bpHardness.slider.rect = (Rectangle){40, 530, 124, 30};
    bpHardness.slider.gradStart = BLACK;
    bpHardness.slider.gradEnd = WHITE;
    bpHardness.slider.clipmaxF = 0.5f;
    BParam_SetIcon(&bpHardness, "ctlrrel");

    BParam_Init(&bpSpacing, 3, "Spacing", 0.01f, 1.0f, 0.15f);
    bpSpacing.slider.rect = (Rectangle){40, 585, 124, 30};
    bpSpacing.slider.gradStart = BLACK;
    bpSpacing.slider.gradEnd = WHITE;
    BParam_SetIcon(&bpSpacing, "ctlrad");

    BParam_Init(&bpCurvature, 4, "Curve", 0.0f, 1.0f, 0.0f);
    bpCurvature.slider.rect = (Rectangle){40, 640, 124, 30};
    bpCurvature.slider.gradStart = BLACK;
    bpCurvature.slider.gradEnd = WHITE;
    BParam_SetIcon(&bpCurvature, "ctlrad");

    state->canvas = Canvas_Create(800, 600, WHITE);
    state->activeLayer = 0;
    state->camera = (Camera2D){
        .target   = (Vector2){0, 0},
        .offset   = (Vector2){0, 0},
        .rotation = 0.0f,
        .zoom     = 1.0f
    };

    state->mode = eBrush;

    Rectangle viewportBounds = {
        (float)uiPanelWidth, 0,
        (float)(SCREEN_WIDTH - uiPanelWidth - RIGHT_PANEL_WIDTH),
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

    state->layerTextures = NULL;
    state->layerRTs = NULL;
    state->texDirty = NULL;
    state->texCount = 0;

    EnsureRTs(state);
    for (int i = 0; i < state->texCount; i++) {
        SyncRTFromImage(state, i);
        state->layerTextures[i] = LoadTextureFromImage(state->canvas.layerImages[i]);
    }

    if (!stampPrevInited) {
        stampPrev = LoadRenderTexture(100, 100);
        stampPrevInited = true;
    }
}

void App_Draw(AppState* state) {
    EnsureRTs(state);

    BeginDrawing();
    ClearBackground((Color){220, 220, 220, 255});

    Viewport_Draw(&viewport, state);
    Gizmo_Draw(state);

    int px = 10;
    int pw = uiPanelWidth - px * 2;
    int bh = 56;
    int by = bh + 8;

    DrawRectangle(0, 0, uiPanelWidth, SCREEN_HEIGHT, (Color){230, 230, 230, 255});
    DrawRectangle(uiPanelWidth, 0, 1, SCREEN_HEIGHT, (Color){120, 120, 120, 255});

    DrawText("RePaint", px, 10, 28, (Color){20, 20, 20, 255});
    DrawText("Tools", px, 50, 22, (Color){40, 40, 40, 255});

    int ty = 80;
    if (GuiButton((Rectangle){px, ty, pw, bh}, "Brush")) state->mode = eBrush; ty += by;
    if (GuiButton((Rectangle){px, ty, pw, bh}, "Smudge")) state->mode = eSmudge; ty += by;
    if (GuiButton((Rectangle){px, ty, pw, bh}, "Line")) state->mode = eLine; ty += by;
    if (GuiButton((Rectangle){px, ty, pw, bh}, "Eraser")) {
        state->mode = eBrush;
        state->currentBrush.Realb.col.a = 0;
    } ty += by;

    ty += 20;
    DrawText("Settings", px, ty, 22, (Color){40, 40, 40, 255}); ty += 30;

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

        DrawTexturePro(stampPrev.texture,
            (Rectangle){0, 0, 100, -100},
            (Rectangle){px + (pw - 100) / 2, ty, 100, 100},
            (Vector2){0, 0}, 0, WHITE);
        ty += 110;
    }

    LayerPanel_HandleInput(state, GetMousePosition());
    LayerPanel_Draw(state);

    int sliderH = 42;
    int sliderGap = 50;
    int sliderX = px + 34;
    int sliderW = pw - 34 - 4 - 4;
    int penW = uiPanelWidth - 4 - (sliderX + sliderW + 4);
    if (penW < 60) { sliderW -= 30; penW = uiPanelWidth - 4 - (sliderX + sliderW + 4); }

    BParam* bps[] = {&bpSize, &bpHardness, &bpCurvature, &bpSpacing, &bpOpacity};
    for (int i = 0; i < 5; i++) {
        bps[i]->slider.rect = (Rectangle){sliderX, ty, sliderW, sliderH};
        BParam_Draw(bps[i]);
        ty += sliderGap;
    }

    // Dropdown pending states (deferred open to avoid same-frame auto-close)
    static bool bmPending = false, pipePending = false;
    bool bmOpen = bmPending, pipeOpen = pipePending;
    for (int i = 0; i < 5; i++) bps[i]->penEdit = bps[i]->penPending;

    ty += 8;
    // --- Blend collapsed button ---
    int bmY = ty;
    {
        static const char* bmNames[] = {"Normal","Add","Dodge","Screen","Lighten","Burn","Multiply","Darken","Overlay","Highlight","Shadowlight","Xor","Diff","Exclusion"};
        int bw = (int)state->currentBrush.Realb.bmidx;
        const char* label = (bw >= 0 && bw < 14) ? bmNames[bw] : "Normal";
        char buf[48];
        snprintf(buf, sizeof(buf), "%s #120#", label);
        Rectangle r = {(float)px, (float)ty, (float)pw, 36};
        if (GuiButton(r, buf)) bmPending = !bmPending;
        ty += 44;
    }
    // --- Pipeline collapsed button ---
    int pipeY = ty;
    {
        int pv = (int)state->currentBrush.Realb.pipeID;
        const char* label = pv == 0 ? "CFNSR" : (pv == 1 ? "RS" : "CFNSR");
        char buf[32];
        snprintf(buf, sizeof(buf), "%s #120#", label);
        Rectangle r = {(float)px, (float)ty, (float)pw, 36};
        if (GuiButton(r, buf)) pipePending = !pipePending;
    }

    // --- Pen collapsed buttons ---
    for (int i = 0; i < 5; i++)
        BParam_DrawPen(bps[i]);

    int sz = uiPanelWidth;
    DrawRectangle(sz - 3, 0, 7, SCREEN_HEIGHT, panelResizing ? (Color){80, 120, 200, 255} : (Color){160, 160, 160, 255});

    char zoomInfo[32];
    sprintf(zoomInfo, "Zoom: %.0f%%", state->camera.zoom * 100.0f);
    DrawText(zoomInfo, 10, SCREEN_HEIGHT - 48, 20, DARKGRAY);

    const char* modeNames[] = {"None", "Brush", "Smudge", "Disp", "Cont", "Line"};
    DrawText(modeNames[state->mode > 5 ? 0 : state->mode], 10, SCREEN_HEIGHT - 24, 20, (Color){0, 100, 0, 255});

    // === Phase 2: expanded lists (drawn last, on top of EVERYTHING) ===
    if (bmOpen) {
        Rectangle wide = {(float)px, (float)bmY, (float)pw, 36};
        int bw = (int)state->currentBrush.Realb.bmidx;
        if (GuiDropdownBox(wide,
            "Normal;Add;Dodge;Screen;Lighten;Burn;Multiply;Darken;Overlay;Highlight;Shadowlight;Xor;Diff;Exclusion",
            &bw, true)) {
            bmPending = false;
            if (bw >= 0 && bw < 14) state->currentBrush.Realb.bmidx = (uint8_t)bw;
        }
    }
    if (pipeOpen) {
        Rectangle wide = {(float)px, (float)pipeY, (float)pw, 36};
        int pv = (int)state->currentBrush.Realb.pipeID;
        if (GuiDropdownBox(wide, "CFNSR;RS", &pv, true)) {
            pipePending = false;
            if (pv >= 0 && pv < 2) state->currentBrush.Realb.pipeID = (uint8_t)pv;
        }
    }
    for (int i = 0; i < 5; i++) {
        if (bps[i]->penEdit) {
            int pm = bps[i]->penMode;
            Rectangle r = bps[i]->slider.rect;
            int btnX = (int)(r.x + r.width + 4);
            int ddW = 180;
            int maxW = GetScreenWidth() - btnX - 4;
            if (ddW > maxW) ddW = maxW;
            if (ddW < 60) ddW = GetScreenWidth() - 4 - (int)r.x;
            int ddH = 18;
            int spacing = GuiGetStyle(DROPDOWNBOX, DROPDOWN_ITEMS_SPACING);
            int totalH = 14 * (ddH + spacing);
            int prevRoll = GuiGetStyle(DROPDOWNBOX, DROPDOWN_ROLL_UP);
            if ((int)r.y + totalH > GetScreenHeight() - 4)
                GuiSetStyle(DROPDOWNBOX, DROPDOWN_ROLL_UP, 1);
            Rectangle wide = {(float)btnX, r.y, (float)ddW, (float)ddH};
            if (GuiDropdownBox(wide,
                "Off;Pressure;Velocity;Direction;Rotation;Tilt;Rel Ang;H-Tilt;V-Tilt;Length;Accel;X-Tilt;Y-Tilt",
                &pm, true)) {
                bps[i]->penPending = false;
                bps[i]->penMode = pm;
            }
            GuiSetStyle(DROPDOWNBOX, DROPDOWN_ROLL_UP, prevRoll);
        }
    }

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
    if (bpSpacing.iconLoaded) UnloadTexture(bpSpacing.iconTex);
    if (bpCurvature.iconLoaded) UnloadTexture(bpCurvature.iconTex);
    UnloadPenIcons();
    Painter_Shutdown();
    BrushBlend_Shutdown();
    CloseWindow();
}
