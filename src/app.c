#include "repaint.h"
#include "rlgl.h"

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

static bool debugShowStamps = false;

void EnsureRTs(AppState* state) {
    int newCount = state->canvas.layerCount;
    if (state->texCount != newCount) {
        int old = state->texCount;
        state->layerRTs = realloc(state->layerRTs, newCount * sizeof(RenderTexture2D));
        state->layerTextures = realloc(state->layerTextures, newCount * sizeof(Texture2D));
        state->texDirty = realloc(state->texDirty, newCount * sizeof(bool));
        if (newCount > old) {
            memset(&state->layerRTs[old], 0, (newCount - old) * sizeof(RenderTexture2D));
            memset(&state->layerTextures[old], 0, (newCount - old) * sizeof(Texture2D));
        }
        state->texCount = newCount;
    }
}

void SyncAllRTs(AppState* state) {
    EnsureRTs(state);
    for (int i = 0; i < state->texCount; i++)
        SyncRTFromImage(state, i);
}

void SyncRTFromImage(AppState* state, int layer) {
    if (layer < 0 || layer >= state->texCount) return;
    Image* img = &state->canvas.layerImages[layer];
    if (state->layerRTs[layer].id == 0) {
        state->layerRTs[layer] = LoadRenderTexture(img->width, img->height);
    }
    Texture2D tmp = LoadTextureFromImage(*img);
    BeginTextureMode(state->layerRTs[layer]);
    DrawTexture(tmp, 0, 0, WHITE);
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

static void DrawStamp(Texture2D stampTex, Vector2 pos, d_Brush* brush) {
    float sw = (float)stampTex.width;
    float sh = (float)stampTex.height;
    float op = brush->Realb.opacity;
    Color bcol = brush->Realb.col;
    Color tint = {bcol.r, bcol.g, bcol.b, (unsigned char)(op * 255.0f)};
    Rectangle dst = {pos.x - sw/2.0f, pos.y - sh/2.0f, sw, sh};
    DrawTexturePro(stampTex, (Rectangle){0, 0, sw, sh}, dst, (Vector2){0, 0}, 0, tint);
}

static void BeginStampBlend(void) {
    rlSetBlendMode(RL_BLEND_CUSTOM_SEPARATE);
    rlSetBlendFactorsSeparate(RL_SRC_ALPHA, RL_ONE_MINUS_SRC_ALPHA,
                              RL_ONE, RL_ONE_MINUS_SRC_ALPHA,
                              RL_FUNC_ADD, RL_FUNC_ADD);
}

static void EndStampBlend(void) {
    rlSetBlendMode(RL_BLEND_ALPHA);
}

static void FinalizeStroke(AppState* state) {
    if (strokeLen < 1) return;

    d_Brush* brush = &state->currentBrush;
    int active = state->activeLayer;

    EnsureRTs(state);
    if (state->layerRTs[active].id == 0) {
        state->layerRTs[active] = LoadRenderTexture(
            state->canvas.width, state->canvas.height);
    }

    Image stampImg = Painter_GenBMask(brush, 0, 0);
    Texture2D stampTex = LoadTextureFromImage(stampImg);
    UnloadImage(stampImg);
    SetTextureFilter(stampTex, TEXTURE_FILTER_POINT);

    BeginTextureMode(state->layerRTs[active]);
    BeginStampBlend();

    float spacing = fmaxf(brush->Realb.rad_out * 0.15f, 1.0f);

    if (strokeLen == 1) {
        DrawStamp(stampTex, strokePts[0], brush);
    } else {
        for (int i = 1; i < strokeLen; i++) {
            Vector2 from = strokePts[i-1];
            Vector2 to = strokePts[i];
            float segLen = Dist2D(from, to);
            int segSteps = (int)(segLen / spacing) + 1;
            if (segSteps < 1) segSteps = 1;
            for (int s = 0; s < segSteps; s++) {
                float t = (float)s / (float)segSteps;
                Vector2 pos = {from.x + (to.x - from.x) * t, from.y + (to.y - from.y) * t};
                DrawStamp(stampTex, pos, brush);
            }
        }
        DrawStamp(stampTex, strokePts[strokeLen-1], brush);
    }

    EndStampBlend();
    EndTextureMode();
    UnloadTexture(stampTex);
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
    if (IsKeyPressed(KEY_F1)) debugShowStamps = !debugShowStamps;

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

    static Texture2D liveStamp = {0};
    static bool liveStampReady = false;
    static Vector2 liveLast = {0, 0};

    int active = state->activeLayer;

    if (state->mode == eBrush && inCanvas && leftDown) {
        if (active >= 0 && active < state->texCount) {
            EnsureRTs(state);
            if (state->layerRTs[active].id == 0) {
                state->layerRTs[active] = LoadRenderTexture(
                    state->canvas.width, state->canvas.height);
            }

            if (strokeLen == 0) {
                strokePts[0] = canvasPos;
                strokeLen = 1;

                if (liveStampReady) { UnloadTexture(liveStamp); liveStampReady = false; }
                Image stampImg = Painter_GenBMask(&state->currentBrush, 0, 0);
                liveStamp = LoadTextureFromImage(stampImg);
                liveStampReady = true;
                UnloadImage(stampImg);
                SetTextureFilter(liveStamp, TEXTURE_FILTER_POINT);
                liveLast = canvasPos;

                BeginTextureMode(state->layerRTs[active]);
                BeginStampBlend();
                DrawStamp(liveStamp, canvasPos, &state->currentBrush);
                EndStampBlend();
                EndTextureMode();
            } else {
                float minDist = fmaxf(state->currentBrush.Realb.rad_out * 0.15f, 1.0f);
                if (Dist2D(strokePts[strokeLen - 1], canvasPos) >= minDist) {
                    if (strokeLen < MAX_STROKE_PTS)
                        strokePts[strokeLen++] = canvasPos;
                }

                if (liveStampReady && Dist2D(liveLast, canvasPos) >= minDist) {
                    BeginTextureMode(state->layerRTs[active]);
                    BeginStampBlend();
                    float spacing = fmaxf(state->currentBrush.Realb.rad_out * 0.15f, 1.0f);
                    float segLen = Dist2D(liveLast, canvasPos);
                    int steps = (int)(segLen / spacing) + 1;
                    for (int s = 0; s < steps; s++) {
                        float t = (float)s / (float)steps;
                        Vector2 pos = {
                            liveLast.x + (canvasPos.x - liveLast.x) * t,
                            liveLast.y + (canvasPos.y - liveLast.y) * t
                        };
                        DrawStamp(liveStamp, pos, &state->currentBrush);
                    }
                    EndStampBlend();
                    EndTextureMode();
                    liveLast = canvasPos;
                }
            }
        }
    } else if (strokeLen > 0) {
        if (liveStampReady) { UnloadTexture(liveStamp); liveStampReady = false; liveStamp.id = 0; }
        SyncImageFromRT(state, active);
        strokeLen = 0;
    }

    if (!leftDown) {
        strokeLen = 0;
        if (liveStampReady) { UnloadTexture(liveStamp); liveStampReady = false; liveStamp.id = 0; }
    }

    if (state->mode != eBrush && inCanvas && leftDown &&
        active >= 0 && active < state->canvas.layerCount) {
        Image* layer = &state->canvas.layerImages[active];
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
        if (didPaint) {
            SyncRTFromImage(state, active);
        }
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
    state->layerRTs = NULL;
    state->texDirty = NULL;
    state->texCount = 0;

    EnsureRTs(state);
    for (int i = 0; i < state->texCount; i++)
        SyncRTFromImage(state, i);
}

void App_Draw(AppState* state) {
    EnsureRTs(state);

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
            Rectangle src = {0, 0, (float)state->canvas.width, (float)-state->canvas.height};
            Rectangle dst = {(float)canvasX, (float)canvasY, cw, ch};
            if (state->texCount > i && state->layerRTs && state->layerRTs[i].id > 0)
                DrawTexturePro(state->layerRTs[i].texture, src, dst, (Vector2){0, 0}, 0.0f, tint);
        }
    }

    if (debugShowStamps && strokeLen > 0) {
        float rad = state->currentBrush.Realb.rad_out;
        int cx = UI_PANEL_WIDTH + (int)state->scrollPos.x;
        int cy = (int)state->scrollPos.y;
        float zk = state->zoomK;
        int minX = 9999, minY = 9999, maxX = -9999, maxY = -9999;
        for (int i = 0; i < strokeLen; i++) {
            int sx = cx + (int)(strokePts[i].x * zk);
            int sy = cy + (int)(strokePts[i].y * zk);
            int r = (int)(rad * zk);
            DrawCircleLines(sx, sy, r, YELLOW);
            DrawRectangleLines(sx - r, sy - r, r * 2, r * 2, (Color){255, 255, 0, 80});
            DrawCircle(sx, sy, 2, RED);
            if (sx - r < minX) minX = sx - r;
            if (sy - r < minY) minY = sy - r;
            if (sx + r > maxX) maxX = sx + r;
            if (sy + r > maxY) maxY = sy + r;
        }
        DrawRectangleLines(minX, minY, maxX - minX, maxY - minY, (Color){255, 0, 255, 120});
        DrawText("DEBUG: stamp positions (F1 toggle)", UI_PANEL_WIDTH + 10, 10, 14, YELLOW);
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
    SyncAllImages(state);
    Canvas_Destroy(&state->canvas);
    for (int i = 0; i < state->texCount; i++) {
        if (state->layerRTs[i].id > 0) UnloadRenderTexture(state->layerRTs[i]);
        if (state->layerTextures[i].id > 0) UnloadTexture(state->layerTextures[i]);
    }
    free(state->layerTextures);
    free(state->layerRTs);
    free(state->texDirty);
    if (bpOpacity.iconLoaded) UnloadTexture(bpOpacity.iconTex);
    if (bpSize.iconLoaded) UnloadTexture(bpSize.iconTex);
    if (bpHardness.iconLoaded) UnloadTexture(bpHardness.iconTex);
    UnloadPenIcons();
    Painter_Shutdown();
    CloseWindow();
}
