#include "repaint.h"
#include "rlgl.h"
#include "stroke.h"
#include "tablet.h"
#include "rlImGui.h"
#include "imgui.h"
#include "imgui_internal.h"
#include "dialog.h"
#include "network_broker.h"
#include "test_broker.h"
#include "ui_leftpanel.h"
#include "external/glad.h"
#include <time.h>

int uiPanelWidth = 250;
bool panelResizing = false;
bool g_panelsVisible = true;

Viewport viewport;

float g_splashAlpha = 1.0f;
static Texture2D g_splashTex = {0};
bool g_layerTransformMode = false;
float g_layerPivotX = 0.0f, g_layerPivotY = 0.0f;

static void DrawSplash(const char* msg) {
    int sw = GetScreenWidth(), sh = GetScreenHeight();
    if (g_splashTex.id == 0 && FileExists("resources/splash.png")) {
        Image img = LoadImage("resources/splash.png");
        if (img.data) g_splashTex = LoadTextureFromImage(img);
        UnloadImage(img);
    }
    BeginDrawing();
    ClearBackground((Color){35, 35, 40, 255});
    if (g_splashTex.id > 0) {
        float scale = fminf(sw / (float)g_splashTex.width, sh / (float)g_splashTex.height) * 0.7f;
        float x = (sw - g_splashTex.width * scale) * 0.5f;
        float y = (sh - g_splashTex.height * scale) * 0.5f - 30;
        DrawTextureEx(g_splashTex, Vector2{x, y}, 0.0f, scale, WHITE);
    }
    DrawText(msg, sw / 2 - MeasureText(msg, 20) / 2, sh / 2 + 80, 20, (Color){230, 230, 240, 255});
    EndDrawing();
}

/* ── File dialog / path state ──────────────────────────────────────────── */
static AppState* g_state = NULL;
DialogState g_fileDlg;
char g_currentFilePath[1024] = "";
Font g_dialogFont = {0};

/* ── New canvas dialog state ───────────────────────────────────────────── */
static bool g_newCanvasActive = false;
static bool g_newCanvasConfirm = false;
static int g_newW = 800;
static int g_newH = 600;
static int g_newPreset = 0;
static const char* g_presets[] = {
    "800 x 600", "1024 x 768", "1280 x 720",
    "1920 x 1080", "2560 x 1440", "3840 x 2160"
};
static int g_presetW[] = { 800, 1024, 1280, 1920, 2560, 3840 };
static int g_presetH[] = { 600, 768,  720,  1080, 1440, 2160 };

void UpdateUI(AppState* state) {
    // ── Track mouse velocity (canvas-space) ──
    {
        static Vector2 lastVelPos = {0, 0};
        Vector2 mp = GetMousePosition();
        float dz = state->camera.zoom;
        float dxv = (mp.x - lastVelPos.x) / dz;
        float dyv = (mp.y - lastVelPos.y) / dz;
        lastVelPos = mp;
        float distV = sqrtf(dxv * dxv + dyv * dyv);
        float rawVel = fminf(distV / 20.0f, 1.0f);
        g_velocity = g_velocity * 0.7f + rawVel * 0.3f;
    }
    // Refresh g_modPars for non-tablet modulators each frame
    g_modPars.Pars[csVel] = g_velocity;

    Vector2 mousePos = GetMousePosition();

    if (IsKeyPressed(KEY_TAB))
        g_panelsVisible = !g_panelsVisible;

    if (IsKeyPressed(KEY_LEFT_SHIFT) || IsKeyPressed(KEY_RIGHT_SHIFT))
        quickPanelShow = !quickPanelShow;

    if (quickPanelShow)
        ; // QuickPanel handles input internally
    else
        quickPanelMouseMode = 0;

    if (IsKeyPressed(KEY_ONE)) {
        g_layerTransformMode = !g_layerTransformMode;
        if (g_layerTransformMode && state->activeLayer >= 0) {
            sLayerProps* lp = &state->canvas.layerProps[state->activeLayer];
            g_layerPivotX = lp->tx + state->canvas.width * 0.5f;
            g_layerPivotY = lp->ty + state->canvas.height * 0.5f;
        }
    }
    if (IsKeyPressed(KEY_TWO)) state->mode = eSmudge;
    if (IsKeyPressed(KEY_THREE)) state->mode = eLine;
    if (IsKeyPressed(KEY_FOUR)) state->mode = eDisp;
    if (IsKeyPressed(KEY_FIVE)) state->mode = eCont;

    // Toggle texture editing mode with T key
    if (IsKeyPressed(KEY_T)) {
        if (state->editTexMode) {
            state->editTexMode = 0;
            state->activeBrushTex = -1;
        } else if (state->brushTexCount > 0) {
            state->editTexMode = 1;
            if (state->activeBrushTex < 0) state->activeBrushTex = 0;
        }
    }

    state->currentBrush.Realb.rad_out  = GetModVal(&bpSize);
    state->currentBrush.Realb.radInRatio = GetModVal(&bpHardness);
    state->currentBrush.Realb.crv      = GetModVal(&bpCurvature);
    state->currentBrush.Realb.opacity  = GetModVal(&bpOpacity);
    state->currentBrush.Realb.resangle = fmodf(state->initialAngle + GetModVal(&bpAngle), 360.0f);
    state->currentBrush.Realb.x2y      = GetModVal(&bpScaleRel);

    float sizeMulFactor = powf(16.0f, BParam_GetValue(&bpSizeMul) / 128.0f - 1.0f);
    state->currentBrush.Realb.rad_out *= sizeMulFactor;

    state->currentBrush.Realb.cop = (state->mode == eSmudge)
        ? GetModVal(&bpCloneOpacity) : 0.0f;

    state->currentBrush.Realb.texScale   = GetModVal(&bpTexScale);
    state->currentBrush.Realb.texFeather = GetModVal(&bpTexFeather);
    state->currentBrush.Realb.texThresh  = GetModVal(&bpTexThresh);
    state->currentBrush.Realb.texBlendVal = GetModVal(&bpTexBlendVal);
    state->currentBrush.Realb.pwr        = GetModVal(&bpPower);
    state->currentBrush.Realb.eraseMode  = state->eraseMode;
    state->currentBrush.Realb.perspective = GetModVal(&bpPerspective);

    colorHue = bpQuickHue.user.clipmaxF;
    colorSat = bpQuickSat.user.clipmaxF;
    colorLit = bpQuickLit.user.clipmaxF;
    // Apply pen mode modulation to color channels for the brush
    float colH = GetModVal(&bpQuickHue);
    float colS = GetModVal(&bpQuickSat);
    float colL = GetModVal(&bpQuickLit);
    state->currentBrush.Realb.col = HSLToRGB(colH, colS, colL);
}

NetworkBroker networkBroker;

bool App_IsDialogActive(void) {
    return g_fileDlg.type != 0;
}

/* ── Callbacks ─────────────────────────────────────────────────────────── */

static void OnOpenResult(DialogResult r) {
    if (r.wasClosed && r.success && r.output[0]) {
        SyncAllImages(g_state);
        if (LoadRePaint(r.output, &g_state->canvas, g_state)) {
            int len = (int)strlen(r.output);
            if (len < (int)sizeof(g_currentFilePath) - 1)
                memcpy(g_currentFilePath, r.output, len + 1);
            g_state->activeLayer = 0;
            g_state->texCount = 0;
            SyncAllRTs(g_state);
            layersDirty = true;
        }
    }
}

static void OnSaveResult(DialogResult r) {
    if (r.wasClosed && r.success && r.output[0]) {
        SyncAllImages(g_state);
        if (SaveRePaint(r.output, &g_state->canvas, g_state)) {
            int len = (int)strlen(r.output);
            if (len < (int)sizeof(g_currentFilePath) - 1)
                memcpy(g_currentFilePath, r.output, len + 1);
        }
    }
}

static void DoCreateNew(void) {
    SyncAllImages(g_state);
    Canvas_Destroy(&g_state->canvas);
    for (int i = 0; i < g_state->texCount; i++) {
        if (g_state->layerRTs[i].id > 0) UnloadRenderTexture(g_state->layerRTs[i]);
        if (g_state->layerTextures[i].id > 0) UnloadTexture(g_state->layerTextures[i]);
    }
    free(g_state->layerTextures); g_state->layerTextures = NULL;
    free(g_state->layerRTs);     g_state->layerRTs = NULL;
    free(g_state->texDirty);     g_state->texDirty = NULL;
    g_state->texCount = 0;

    g_state->canvas = Canvas_Create(g_newW, g_newH, WHITE);
    g_state->activeLayer = 0;
    g_state->camera.target = Vector2{(float)g_newW * 0.5f, (float)g_newH * 0.5f};
    g_state->camera.zoom = 1.0f;
    SyncAllRTs(g_state);
    layersDirty = true;
    g_currentFilePath[0] = '\0';
    g_newCanvasActive = false;
    ImGui::CloseCurrentPopup();
}

void App_FileNew(void) {
    g_newCanvasActive = true;
    g_newCanvasConfirm = false;
}

void App_FileOpen(void) {
    DialogOpen_Init(&g_fileDlg, "Open", ".re.png", OnOpenResult);
}

void App_FileSave(void) {
    if (g_currentFilePath[0]) {
        SyncAllImages(g_state);
        SaveRePaint(g_currentFilePath, &g_state->canvas, g_state);
    } else {
        App_FileSaveAs();
    }
}

void App_FileSaveAs(void) {
    const char* name = "untitled";
    if (g_currentFilePath[0]) {
        name = GetFileNameWithoutExt(g_currentFilePath);
    }
    DialogSaveAs_Init(&g_fileDlg, "Save As", ".re.png", name, OnSaveResult);
}

void App_FileReload(void) {
    if (!g_currentFilePath[0]) return;
    SyncAllImages(g_state);

    // Backup current file with random hash
    char backupPath[1048];
    const char* base = GetFileNameWithoutExt(g_currentFilePath);
    const char* ext = GetFileExtension(g_currentFilePath);
    unsigned int hash = (unsigned int)time(NULL) ^ (unsigned int)(uintptr_t)g_state;
    hash = hash * 1103515245 + 12345;
    const char* dir = GetDirectoryPath(g_currentFilePath);
    const char* fname = GetFileNameWithoutExt(g_currentFilePath);
    snprintf(backupPath, sizeof(backupPath), "%s/%s_backup_%08x%s",
             dir, fname, (hash / 65536) % 0xFFFFFFFFu, ext);
    SaveRePaint(backupPath, &g_state->canvas, g_state);

    // Reload from original
    if (LoadRePaint(g_currentFilePath, &g_state->canvas, g_state)) {
        g_state->activeLayer = 0;
        g_state->texCount = 0;
        SyncAllRTs(g_state);
        layersDirty = true;
    }
}

void App_FileSnap(void) {
    /* GPU composite + dither → 8-bit snapshot */
    Image flat = CompositeLayersWithDither(g_state);

    /* build path: Snaps/snap_YYYYMMDD_HHMMSS.png */
    time_t now = time(NULL);
    struct tm* t = localtime(&now);
    const char* appDir = GetApplicationDirectory();
    char path[1024];
    snprintf(path, sizeof(path), "%sSnaps/snap_%04d%02d%02d_%02d%02d%02d.png",
             appDir,
             t->tm_year + 1900, t->tm_mon + 1, t->tm_mday,
              t->tm_hour, t->tm_min, t->tm_sec);

    ExportImage(flat, path);
    UnloadImage(flat);
}

/* ── App_Init ──────────────────────────────────────────────────────────── */

void App_Init(AppState* state) {
    g_state = state;

    SetConfigFlags(FLAG_WINDOW_RESIZABLE);
    InitWindow(SCREEN_WIDTH, SCREEN_HEIGHT, "RePaint");
    MaximizeWindow();
    DrawSplash("Initializing...");

    if (FileExists("resources/icon.png")) {
        Image icon = LoadImage("resources/icon.png");
        if (icon.data) {
            SetWindowIcon(icon);
            UnloadImage(icon);
        }
    }

    UIStyle::Init();
    LeftPanel_Init();
    SetTargetFPS(60);
    DrawSplash("Loading brushes...");

    Painter_Init();
    BrushBlend_Init();
    LoadPenIcons();
    BrushTex_Init(state);
    UserTexture_Init();
    QuickPanel_Init();
    DrawSplash("Starting network...");

    networkBroker.appState = state;
    g_testBroker.appState = state;

    // load persistent config
    networkBroker.LoadConfig("repaint.ini");

    Modulators_Init();
    Changelog_Init();
    DrawSplash("Creating canvas...");

    state->canvas = Canvas_Create(800, 600, WHITE);
    state->activeLayer = 0;

    Rectangle viewportBounds = {
        (float)uiPanelWidth, 0,
        (float)(SCREEN_WIDTH - uiPanelWidth - RIGHT_PANEL_WIDTH),
        (float)SCREEN_HEIGHT
    };
    Viewport_Init(&viewport, viewportBounds);
    viewport.broker = g_useTestBroker ? (ICommandBroker*)&g_testBroker : (ICommandBroker*)&networkBroker;

    state->camera = Camera2D{};
    state->camera.target = Vector2{(float)state->canvas.width * 0.5f, (float)state->canvas.height * 0.5f};
    state->camera.offset = Vector2{viewportBounds.x + viewportBounds.width * 0.5f, viewportBounds.y + viewportBounds.height * 0.5f};
    state->camera.rotation = 0.0f;
    state->camera.zoom = 1.0f;

    state->mode = eBrush;
    state->eraseMode = eEraseNone;

    state->initialAngle = 0.0f;

    state->currentBrush.Realb.radInRatio = 1.0f;
    state->currentBrush.Realb.rad_out = 20.0f;
    state->currentBrush.Realb.opacity = 1.0f;
    state->currentBrush.Realb.resangle = 0.0f;
    state->currentBrush.Realb.crv = 0.0f;
    state->currentBrush.Realb.x2y = 0.8f;
    state->currentBrush.Realb.cop = 0.0f;
    state->currentBrush.Realb.pwr = 0.0f;
    state->currentBrush.Realb.sol = 1.0f;
    state->currentBrush.Realb.sol2op = 0.0f;
    state->currentBrush.Realb.seed = 0;
    state->currentBrush.Realb.bmidx = bmGamma;
    state->currentBrush.Realb.pipeID = plCFNSR;
    state->currentBrush.Realb.preserveop = 0;
    state->currentBrush.Realb.texId = -1;
    state->currentBrush.Realb.texScale = 1.0f;
    state->currentBrush.Realb.texFeather = 0.05f;
    state->currentBrush.Realb.texThresh = 1.0f;
    state->currentBrush.Realb.useTexLumAsAlpha = false;
    state->currentBrush.Realb.texUseRGB = true;
    state->currentBrush.Realb.texBlendVal = 1.0f;
    state->currentBrush.Realb.texBlendMode = 0;
    state->currentBrush.Realb.texNoisemode = 2;
    state->currentBrush.Realb.texColorMode = 0;
    state->currentBrush.Realb.col = BLACK;

    colorHue = 0.35f;
    colorSat = 1.0f;
    colorLit = 0.5f;

    state->layerTextures = NULL;
    state->layerRTs = NULL;
    state->texDirty = NULL;
    state->texCount = 0;

    SyncAllRTs(state);

    /* Create default directories */
    const char* ad = GetApplicationDirectory();
    char p[1024];
    snprintf(p, sizeof(p), "%sSaves", ad); Dialog_MakeDir(p);
    snprintf(p, sizeof(p), "%sSnaps", ad); Dialog_MakeDir(p);

    /* Load custom dialog font — bilinear filter for smooth OTF rendering */
    g_dialogFont = LoadFontEx("resources/Cadman_Bold.otf", 28, 0, 0);
    SetTextureFilter(g_dialogFont.texture, TEXTURE_FILTER_BILINEAR);
    DialogSetFont(&g_fileDlg, g_dialogFont, 26);

    g_currentFilePath[0] = '\0';
}

/* ── App_Draw ──────────────────────────────────────────────────────────── */

void App_Draw(AppState* state) {
    int sw = GetScreenWidth();
    int sh = GetScreenHeight();

    Rectangle viewportBounds;
    if (g_panelsVisible) {
        viewportBounds = {
            (float)uiPanelWidth, 0,
            (float)(sw - uiPanelWidth - RIGHT_PANEL_WIDTH),
            (float)sh
        };
    } else {
        viewportBounds = {0, 0, (float)sw, (float)sh};
    }
    viewport.bounds = viewportBounds;
    state->camera.offset = Vector2{
        viewportBounds.x + viewportBounds.width * 0.5f,
        viewportBounds.y + viewportBounds.height * 0.5f
    };

    BeginDrawing();
    ClearBackground(Color{220, 220, 220, 255});

    /* If dialog active, draw it modelly — skip viewport/imgui entirely */
    if (g_fileDlg.type != 0) {
        Dialog_Draw(&g_fileDlg);
        EndDrawing();
        return;
    }

    /* Normal rendering path */
    EnsureRTs(state);

    // toggle network UI
    if (IsKeyDown(KEY_LEFT_CONTROL) && IsKeyPressed(KEY_N))
        networkBroker.showUI = !networkBroker.showUI;

    UserTexture_Update(state);

    if (viewport.broker) viewport.broker->poll(state);
    if (viewport.strokeEnded) {
        SyncLayerTexture(state, viewport.endLayer);
        viewport.strokeEnded = false;
    }

    // ── Draw viewport: composite layers → stamp preview → screen ──
    ViewportHUD_Draw(state);

    // Debug stamp overlays (after canvas, before UI)
    Viewport_DrawDebugOverlays(&viewport, state);

    // Gizmo visual drawn early (raylib XOR, before ImGui)
    XORgizmo_DrawVisual(state);

    // ── Layer transform XOR gizmo ──────────────────────────────────────
    if (g_layerTransformMode && state->activeLayer >= 0) {
        sLayerProps* lp = &state->canvas.layerProps[state->activeLayer];
        float cw = (float)state->canvas.width, ch = (float)state->canvas.height;

        auto ws = [&](Vector2 wp) -> Vector2 {
            return GetWorldToScreen2D(wp, state->camera);
        };

        rlDrawRenderBatchActive();
        glEnable(GL_COLOR_LOGIC_OP);
        glLogicOp(GL_XOR);

        // UI pivot crosshair (floating UI element, does not affect transform)
        Vector2 uip = ws(Vector2{g_layerPivotX, g_layerPivotY});
        float chLen = 12.0f;
        DrawLine(uip.x - chLen, uip.y, uip.x + chLen, uip.y, WHITE);
        DrawLine(uip.x, uip.y - chLen, uip.x, uip.y + chLen, WHITE);
        DrawCircle(uip.x, uip.y, 3.0f, WHITE);

        // Layer outline — rotate each corner around visual pivot
        float cosR = cosf(lp->rot * (float)M_PI / 180.0f);
        float sinR = sinf(lp->rot * (float)M_PI / 180.0f);
        Vector2 pts[4] = {
            {lp->tx,            lp->ty},
            {lp->tx + cw,       lp->ty},
            {lp->tx + cw,       lp->ty + ch},
            {lp->tx,            lp->ty + ch}
        };
        Vector2 corners[5];
        float vpX = g_layerPivotX, vpY = g_layerPivotY;
        for (int ci = 0; ci < 4; ci++) {
            float dx = pts[ci].x - vpX, dy = pts[ci].y - vpY;
            float rx = vpX + dx * cosR - dy * sinR;
            float ry = vpY + dx * sinR + dy * cosR;
            corners[ci] = ws(Vector2{rx, ry});
        }
        corners[4] = corners[0];
        DrawLineStrip(corners, 5, WHITE);

        rlDrawRenderBatchActive();
        glDisable(GL_COLOR_LOGIC_OP);
    }

    rlImGuiBegin();

    rlSetBlendMode(RL_BLEND_ALPHA);
    if (g_panelsVisible)
        networkBroker.DrawConnectionUI();
    rlSetBlendMode(RL_BLEND_ALPHA);
    QuickPanel_DrawUI(state);
    if (g_panelsVisible) {
        rlSetBlendMode(RL_BLEND_ALPHA);
        LeftPanel_Draw(state);
        rlSetBlendMode(RL_BLEND_ALPHA);
        LayerPanel_Draw(state);
    }
    rlSetBlendMode(RL_BLEND_ALPHA);
    Changelog_Draw();

    // Gizmo input handled last — after UI consumed its own clicks
    XORgizmo_HandleInput(state);

    // ── Color picker 3×3 magnifier ──────────────────────────────────
    if (g_colorPicking) {
        ImDrawList* fdl = ImGui::GetForegroundDrawList();
        ImVec2 mp = ImGui::GetMousePos();
        int sz = 32;
        ImVec2 org(mp.x + 12, mp.y + 12);
        for (int i = 0; i < 9; i++) {
            int gx = i % 3, gy = i / 3;
            Color c = g_colorPickGrid[i];
            ImU32 col = IM_COL32(c.r, c.g, c.b, 255);
            fdl->AddRectFilled(ImVec2(org.x + gx * sz, org.y + gy * sz),
                ImVec2(org.x + (gx + 1) * sz, org.y + (gy + 1) * sz), col);
            fdl->AddRect(ImVec2(org.x + gx * sz, org.y + gy * sz),
                ImVec2(org.x + (gx + 1) * sz, org.y + (gy + 1) * sz),
                IM_COL32(100, 100, 100, 200));
        }
    }

    /* New canvas dialog (imgui modal) */
    if (g_newCanvasActive) {
        ImGui::OpenPopup("New Canvas");
        if (ImGui::BeginPopupModal("New Canvas", NULL, ImGuiWindowFlags_AlwaysAutoResize)) {
            ImGui::Text("Canvas Size");
            ImGui::Separator();

            /* Preset combo */
            int prev = g_newPreset;
            ImGui::SetNextItemWidth(180);
            ImGui::Combo("Preset", &g_newPreset, g_presets, 6);
            if (prev != g_newPreset) {
                g_newW = g_presetW[g_newPreset];
                g_newH = g_presetH[g_newPreset];
            }

            ImGui::InputInt("Width", &g_newW, 1, 100);
            ImGui::InputInt("Height", &g_newH, 1, 100);
            if (g_newW < 1) g_newW = 1;
            if (g_newH < 1) g_newH = 1;
            if (g_newW > 16384) g_newW = 16384;
            if (g_newH > 16384) g_newH = 16384;

            ImGui::Separator();
            if (ImGui::Button("Create", ImVec2(100, 0))) {
                if (g_currentFilePath[0] && !g_newCanvasConfirm) {
                    ImGui::OpenPopup("Confirm New");
                } else {
                    DoCreateNew();
                }
            }

            /* Confirmation popup (within modal) */
            if (ImGui::BeginPopupModal("Confirm New", NULL, ImGuiWindowFlags_AlwaysAutoResize)) {
                ImGui::Text("Clear current canvas and create new?");
                ImGui::Separator();
                if (ImGui::Button("Yes", ImVec2(80, 0))) {
                    DoCreateNew();
                    ImGui::CloseCurrentPopup();
                }
                ImGui::SameLine();
                if (ImGui::Button("No", ImVec2(80, 0))) {
                    g_newCanvasConfirm = false;
                    ImGui::CloseCurrentPopup();
                }
                ImGui::EndPopup();
            }

            ImGui::SameLine();
            if (ImGui::Button("Cancel", ImVec2(100, 0))) {
                g_newCanvasActive = false;
                ImGui::CloseCurrentPopup();
            }
            ImGui::EndPopup();
        }
        /* if popup closed via X/escape, reset flag */
        if (!ImGui::IsPopupOpen("New Canvas"))
            g_newCanvasActive = false;
    }

    rlImGuiEnd();

    // Splash overlay: fades out over ~15 frames
    if (g_splashAlpha > 0.001f) {
        int sw = GetScreenWidth(), sh = GetScreenHeight();
        Color bg = {35, 35, 40, (uint8_t)(g_splashAlpha * 255)};
        DrawRectangle(0, 0, sw, sh, bg);
        if (g_splashTex.id > 0) {
            float scale = fminf(sw / (float)g_splashTex.width, sh / (float)g_splashTex.height) * 0.7f;
            float x = (sw - g_splashTex.width * scale) * 0.5f;
            float y = (sh - g_splashTex.height * scale) * 0.5f - 30;
            DrawTextureEx(g_splashTex, Vector2{x, y}, 0.0f, scale, Color{255, 255, 255, (uint8_t)(g_splashAlpha * 255)});
        }
        Color tc = {230, 230, 240, (uint8_t)(g_splashAlpha * 255)};
        DrawText("Ready", sw / 2 - MeasureText("Ready", 20) / 2, sh / 2 + 80, 20, tc);
        g_splashAlpha -= 0.07f;
        if (g_splashAlpha < 0.0f) {
            g_splashAlpha = 0.0f;
            if (g_splashTex.id > 0) { UnloadTexture(g_splashTex); g_splashTex = Texture2D{0}; }
        }
    }

    EndDrawing();

    /* Check file rename result after dialog callback has a chance to fire */
    /* (dialog draws at beginning of next frame) */
}

/* ── App_Close ─────────────────────────────────────────────────────────── */

void App_Close(AppState* state) {
    networkBroker.SaveConfig();
    networkBroker.Disconnect();
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
    ViewportHUD_Shutdown();
    Modulators_Shutdown();
    UnloadPenIcons();
    QuickPanel_Shutdown();
    // Cleanup brush textures
    for (int i = 0; i < state->brushTexCount; i++) {
        if (state->brushTex[i].rt.id > 0) UnloadRenderTexture(state->brushTex[i].rt);
        if (state->brushTex[i].cpuImage.data) UnloadImage(state->brushTex[i].cpuImage);
    }
    if (g_dialogFont.texture.id > 0) UnloadFont(g_dialogFont);
    UIStyle::Shutdown();
    Painter_Shutdown();
    BrushBlend_Shutdown();
    Tablet_Shutdown();
    UserTexture_Shutdown();
    CloseWindow();
}
