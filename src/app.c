#include "repaint.h"

BParam bpOpacity;
BParam bpSize;
BParam bpHardness;
UIButton btnBrush = {{10, 60, 180, 40}, GRAY, LIGHTGRAY, false, false, "Brush", 0};
UIButton btnSmudge = {{10, 110, 180, 40}, GRAY, LIGHTGRAY, false, false, "Smudge", 1};
UIButton btnLine = {{10, 160, 180, 40}, GRAY, LIGHTGRAY, false, false, "Line", 2};
UIButton btnEraser = {{10, 210, 180, 40}, GRAY, LIGHTGRAY, false, false, "Eraser", 3};

Vector2 strokePts[MAX_STROKE_PTS];
int strokeLen = 0;
bool wasMouseDown = false;
Vector2 lastDabPos = {0, 0};

static void MarkDirty(AppState* state) {
    for (int i = 0; i < state->canvas.layerCount; i++)
        state->texDirty[i] = true;
}

static void MarkOneDirty(AppState* state, int layer) {
    if (layer >= 0 && layer < state->canvas.layerCount)
        state->texDirty[layer] = true;
}

static void SyncTextureCache(AppState* state) {
    if (state->texCount != state->canvas.layerCount) {
        for (int i = 0; i < state->texCount; i++)
            UnloadTexture(state->layerTextures[i]);
        free(state->layerTextures);
        free(state->texDirty);
        state->texCount = state->canvas.layerCount;
        state->layerTextures = calloc(state->texCount, sizeof(Texture2D));
        state->texDirty = calloc(state->texCount, sizeof(bool));
        MarkDirty(state);
    }

    for (int i = 0; i < state->texCount; i++) {
        if (state->texDirty[i]) {
            if (state->layerTextures[i].id > 0)
                UnloadTexture(state->layerTextures[i]);
            state->layerTextures[i] = LoadTextureFromImage(state->canvas.layerImages[i]);
            state->texDirty[i] = false;
        }
    }
}

static void FinalizeStroke(AppState* state) {
    if (strokeLen < 1) return;

    d_Brush* brush = &state->currentBrush;
    float rad = brush->Realb.rad_out;
    float op = brush->Realb.opacity;
    Color bcol = brush->Realb.col;

    float minX = 1e9f, minY = 1e9f, maxX = -1e9f, maxY = -1e9f;
    for (int i = 0; i < strokeLen; i++) {
        float px = strokePts[i].x, py = strokePts[i].y;
        if (px < minX) minX = px;
        if (py < minY) minY = py;
        if (px > maxX) maxX = px;
        if (py > maxY) maxY = py;
    }
    minX -= rad; minY -= rad;
    maxX += rad; maxY += rad;

    int bw = (int)(maxX - minX) + 1;
    int bh = (int)(maxY - minY) + 1;
    if (bw < 1) bw = 1;
    if (bh < 1) bh = 1;

    Image stampImg = Painter_GenBMask(brush, 0, 0);
    Texture2D stampTex = LoadTextureFromImage(stampImg);
    UnloadImage(stampImg);

    float stampW = (float)stampTex.width;
    float stampH = (float)stampTex.height;

    RenderTexture2D rt = LoadRenderTexture(bw, bh);
    BeginTextureMode(rt);
    ClearBackground((Color){0, 0, 0, 0});

    int tintA = (int)(op * 255.0f);
    Color tint = {bcol.r, bcol.g, bcol.b, (unsigned char)tintA};

    float spacing = fmaxf(rad * 0.15f, 1.0f);
    float lastX = -9999, lastY = -9999;

    for (int i = (strokeLen == 1) ? 0 : 1; i < strokeLen; i++) {
        if (i == 1 && strokeLen == 2) {
            float px = strokePts[0].x - minX;
            float py = strokePts[0].y - minY;
            float dist = Dist2D(strokePts[0], strokePts[1]);
            int steps = (int)(dist / spacing) + 1;
            if (steps < 1) steps = 1;
            for (int s = 0; s <= steps; s++) {
                float t = (float)s / (float)steps;
                float ix = strokePts[0].x + (strokePts[1].x - strokePts[0].x) * t - minX;
                float iy = strokePts[0].y + (strokePts[1].y - strokePts[0].y) * t - minY;
                Rectangle dst = {ix - rad, iy - rad, rad * 2, rad * 2};
                DrawTexturePro(stampTex, (Rectangle){0, 0, stampW, stampH}, dst, (Vector2){0, 0}, 0, tint);
            }
            continue;
        }

        float px = strokePts[i].x - minX;
        float py = strokePts[i].y - minY;

        if (Dist2D((Vector2){px, py}, (Vector2){lastX, lastY}) < spacing && i < strokeLen - 1)
            continue;

        if (lastX > -9990) {
            float segLen = Dist2D((Vector2){px, py}, (Vector2){lastX, lastY});
            int steps = (int)(segLen / spacing) + 1;
            if (steps > 1) {
                float stepX0 = lastX, stepY0 = lastY;
                for (int s = 1; s < steps; s++) {
                    float t = (float)s / (float)steps;
                    float ix = lastX + (px - lastX) * t;
                    float iy = lastY + (py - lastY) * t;
                    Rectangle dst = {ix - rad, iy - rad, rad * 2, rad * 2};
                    DrawTexturePro(stampTex, (Rectangle){0, 0, stampW, stampH}, dst, (Vector2){0, 0}, 0, tint);
                }
            }
        }

        lastX = px; lastY = py;

        Rectangle dst = {px - rad, py - rad, rad * 2, rad * 2};
        DrawTexturePro(stampTex, (Rectangle){0, 0, stampW, stampH}, dst, (Vector2){0, 0}, 0, tint);
    }
    if (strokeLen == 1) {
        float px = strokePts[0].x - minX;
        float py = strokePts[0].y - minY;
        Rectangle dst = {px - rad, py - rad, rad * 2, rad * 2};
        DrawTexturePro(stampTex, (Rectangle){0, 0, stampW, stampH}, dst, (Vector2){0, 0}, 0, tint);
    }

    EndTextureMode();

    Image strokeImg = LoadImageFromTexture(rt.texture);
    ImageFormat(&strokeImg, PIXELFORMAT_UNCOMPRESSED_R8G8B8A8);

    Image* layer = &state->canvas.layerImages[state->activeLayer];
    int bmidx = brush->Realb.bmidx;
    bool presop = brush->Realb.preserveop;
    int lw = layer->width, lh = layer->height;

    for (int y = 0; y < bh; y++) {
        int ly = (int)minY + y;
        if (ly < 0 || ly >= lh) continue;
        for (int x = 0; x < bw; x++) {
            int lx = (int)minX + x;
            if (lx < 0 || lx >= lw) continue;

            Color src = ((Color*)strokeImg.data)[y * bw + x];
            if (src.a == 0) continue;

            Color* dst = &((Color*)layer->data)[ly * lw + lx];
            Color res = BlendColors(*dst, src, bmidx);
            if (presop) res.a = dst->a;
            *dst = res;
        }
    }

    UnloadImage(strokeImg);
    UnloadRenderTexture(rt);
    UnloadTexture(stampTex);

    MarkOneDirty(state, state->activeLayer);
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

void HandleCanvasInput(AppState* state) {
    Vector2 mousePos = GetMousePosition();
    bool inCanvas = mousePos.x > UI_PANEL_WIDTH && mousePos.x < RIGHT_PANEL_X &&
                    mousePos.y > 0 && mousePos.y < SCREEN_HEIGHT;

    if (gizmoShow) {
        int gcx = UI_PANEL_WIDTH + (RIGHT_PANEL_X - UI_PANEL_WIDTH) / 2;
        int gcy = SCREEN_HEIGHT / 2;
        int gizR = 100;
        Rectangle overlayRect = {(float)gcx - 270, (float)gcy - gizR, 540, 480};
        if (gizmoMouseMode > 0 || CheckCollisionPointRec(mousePos, overlayRect))
            return;
    }

    Vector2 canvasPos = {
        (mousePos.x - UI_PANEL_WIDTH - state->scrollPos.x) / state->zoomK,
        (mousePos.y - state->scrollPos.y) / state->zoomK
    };

    bool leftDown = IsMouseButtonDown(MOUSE_LEFT_BUTTON);

    if (state->mode == eBrush && inCanvas && leftDown) {
        if (strokeLen == 0) {
            strokePts[0] = canvasPos;
            strokeLen = 1;
        } else {
            float minDist = fmaxf(state->currentBrush.Realb.rad_out * 0.15f, 1.0f);
            if (Dist2D(strokePts[strokeLen - 1], canvasPos) >= minDist) {
                if (strokeLen < MAX_STROKE_PTS)
                    strokePts[strokeLen++] = canvasPos;
            }
        }
    } else if (strokeLen > 0) {
        FinalizeStroke(state);
        strokeLen = 0;
    }

    if (!leftDown) strokeLen = 0;

    if (state->mode != eBrush && inCanvas && leftDown &&
        state->activeLayer >= 0 && state->activeLayer < state->canvas.layerCount) {
        Image* layer = &state->canvas.layerImages[state->activeLayer];
        float spacing = state->currentBrush.Realb.rad_out * 0.2f;
        if (spacing < 2.0f) spacing = 2.0f;
        bool didPaint = false;

        if (state->mode == eLine) {
            if (!wasMouseDown) {
                lastDabPos = canvasPos;
                wasMouseDown = true;
            }
            if (Dist2D(lastDabPos, canvasPos) > spacing) {
                Painter_DrawLine(layer, lastDabPos, canvasPos, &state->currentBrush);
                lastDabPos = canvasPos;
                didPaint = true;
            }
        } else if (state->mode == eSmudge || state->mode == eDisp || state->mode == eCont) {
            if (!wasMouseDown) {
                Painter_DrawDab(layer, canvasPos, &state->currentBrush, state->mode);
                lastDabPos = canvasPos;
                wasMouseDown = true;
                didPaint = true;
            } else {
                if (Dist2D(lastDabPos, canvasPos) >= spacing) {
                    Painter_DrawLine(layer, lastDabPos, canvasPos, &state->currentBrush);
                    lastDabPos = canvasPos;
                    didPaint = true;
                }
            }
        }
        if (didPaint) MarkOneDirty(state, state->activeLayer);
    } else {
        wasMouseDown = false;
    }

    if (IsMouseButtonDown(MOUSE_RIGHT_BUTTON)) {
        if (state->rightMouseDown) {
            Vector2 delta = {mousePos.x - state->lastMousePos.x, mousePos.y - state->lastMousePos.y};
            state->scrollPos.x += delta.x;
            state->scrollPos.y += delta.y;
        }
        state->rightMouseDown = true;
    } else {
        state->rightMouseDown = false;
    }

    float wheel = GetMouseWheelMove();
    if (wheel != 0) {
        state->zoomK += wheel * 0.1f;
        state->zoomK = fmaxf(0.1f, fminf(5.0f, state->zoomK));
    }

    state->lastMousePos = mousePos;
}

void App_Init(AppState* state) {
    InitWindow(SCREEN_WIDTH, SCREEN_HEIGHT, "RePaint - Pure C Port");
    SetTargetFPS(60);

    Painter_Init();
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
    state->scrollPos = (Vector2){0, 0};
    state->zoomK = 1.0f;
    state->mode = eBrush;
    state->leftMouseDown = false;
    state->rightMouseDown = false;
    state->lastMousePos = (Vector2){0, 0};

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
    state->texDirty = NULL;
    state->texCount = 0;
}

void App_Draw(AppState* state) {
    SyncTextureCache(state);

    BeginDrawing();
    ClearBackground((Color){30, 30, 30, 255});

    Rectangle canvasArea = {
        (float)UI_PANEL_WIDTH, 0,
        (float)(SCREEN_WIDTH - UI_PANEL_WIDTH - RIGHT_PANEL_WIDTH),
        (float)SCREEN_HEIGHT
    };
    DrawRectangleRec(canvasArea, (Color){55, 55, 55, 255});

    if (state->canvas.layerCount > 0) {
        int canvasX = UI_PANEL_WIDTH + (int)state->scrollPos.x;
        int canvasY = (int)state->scrollPos.y;
        float cw = state->canvas.width * state->zoomK;
        float ch = state->canvas.height * state->zoomK;

        DrawRectangle(canvasX, canvasY, (int)cw, (int)ch, state->canvas.backgroundColor);

        for (int i = 0; i < state->canvas.layerCount; i++) {
            if (!state->canvas.layerProps[i].visible) continue;
            float alpha = state->canvas.layerProps[i].op;
            Color tint = {255, 255, 255, (unsigned char)(alpha * 255.0f)};
            Rectangle src = {0, 0, (float)state->canvas.width, (float)state->canvas.height};
            Rectangle dst = {(float)canvasX, (float)canvasY, cw, ch};
            DrawTexturePro(state->layerTextures[i], src, dst, (Vector2){0, 0}, 0.0f, tint);
        }
    }

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
    BParam_Draw(&bpOpacity);
    BParam_Draw(&bpSize);
    BParam_Draw(&bpHardness);

    LayerPanel_Draw(state);

    char zoomInfo[32];
    sprintf(zoomInfo, "Zoom: %.0f%%", state->zoomK * 100.0f);
    DrawText(zoomInfo, 10, SCREEN_HEIGHT - 40, 16, WHITE);

    const char* modeNames[] = {"None", "Brush", "Smudge", "Disp", "Cont", "Line"};
    DrawText(modeNames[state->mode > 5 ? 0 : state->mode], 10, SCREEN_HEIGHT - 20, 16, GREEN);

    EndDrawing();
}

void App_Close(AppState* state) {
    Canvas_Destroy(&state->canvas);
    for (int i = 0; i < state->texCount; i++)
        if (state->layerTextures[i].id > 0) UnloadTexture(state->layerTextures[i]);
    free(state->layerTextures);
    free(state->texDirty);
    if (bpOpacity.iconLoaded) UnloadTexture(bpOpacity.iconTex);
    if (bpSize.iconLoaded) UnloadTexture(bpSize.iconTex);
    if (bpHardness.iconLoaded) UnloadTexture(bpHardness.iconTex);
    UnloadPenIcons();
    Painter_Shutdown();
    CloseWindow();
}
