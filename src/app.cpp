#include "repaint.h"
#include "rlgl.h"
#include "stroke.h"
#include "rlImGui.h"
#include "imgui.h"
#include "imgui_internal.h"
#include "dialog.h"
#include "network_broker.h"
#include <time.h>

int uiPanelWidth = 250;
bool panelResizing = false;
bool g_panelsVisible = true;

BParam bpOpacity;
BParam bpSize;
BParam bpHardness;
BParam bpSpacing;
BParam bpCurvature;
BParam bpScatter;
BParam bpCloneOpacity;
BParam bpQuickHue;

float g_velocity = 0.0f;
BParam bpQuickSat;
BParam bpQuickLit;

Viewport viewport;

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
    ClearBackground(BLANK);
    rlSetBlendFactors(RL_ONE, RL_ZERO, RL_FUNC_ADD);
    rlSetBlendMode(RL_BLEND_CUSTOM);
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

float GetModVal(BParam* bp) {
    float cpar = 1.0f;
    if (bp->penMode == csVel)
        cpar = g_velocity;
    else if (bp->penMode == csNone)
        cpar = 1.0f;
    return GetModValFor(bp, cpar);
}

float GetModValFor(BParam* bp, float cpar) {
    float rng = bp->slider.clipmaxF - bp->slider.clipminF;
    float respar = cpar * rng + bp->slider.clipminF;
    float randm = (((float)rand() / (float)RAND_MAX) - 0.5f) * 2.0f * bp->slider.jitter;
    float res = fminf(fmaxf(respar + randm, 0.0f), 1.0f);
    return res * (bp->outMax - bp->outMin) + bp->outMin;
}

void UpdateUI(AppState* state) {
    // ── Track mouse velocity first (before brush reads g_velocity) ──
    {
        static Vector2 lastVelPos = {0, 0};
        Vector2 mp = GetMousePosition();
        float dxv = mp.x - lastVelPos.x;
        float dyv = mp.y - lastVelPos.y;
        lastVelPos = mp;
        float distV = sqrtf(dxv * dxv + dyv * dyv);
        float rawVel = fminf(distV / 50.0f, 1.0f);
        g_velocity = g_velocity * 0.7f + rawVel * 0.3f;
    }

    Vector2 mousePos = GetMousePosition();

    if (IsKeyPressed(KEY_TAB))
        g_panelsVisible = !g_panelsVisible;

    if (IsKeyPressed(KEY_LEFT_SHIFT) || IsKeyPressed(KEY_RIGHT_SHIFT))
        quickPanelShow = !quickPanelShow;

    if (quickPanelShow)
        ; // QuickPanel handles input internally
    else
        quickPanelMouseMode = 0;

    if (IsKeyPressed(KEY_ONE)) state->mode = eBrush;
    if (IsKeyPressed(KEY_TWO)) state->mode = eSmudge;
    if (IsKeyPressed(KEY_THREE)) state->mode = eLine;
    if (IsKeyPressed(KEY_FOUR)) state->mode = eDisp;
    if (IsKeyPressed(KEY_FIVE)) state->mode = eCont;

    state->currentBrush.Realb.rad_out = GetModVal(&bpSize);
    state->currentBrush.Realb.rad_in = state->currentBrush.Realb.rad_out * GetModVal(&bpHardness);
    state->currentBrush.Realb.crv = GetModVal(&bpCurvature);
    state->currentBrush.Realb.opacity = GetModVal(&bpOpacity);

    state->currentBrush.Realb.cop = (state->mode == eSmudge)
        ? GetModVal(&bpCloneOpacity) : 0.0f;

    colorHue = bpQuickHue.slider.clipmaxF;
    colorSat = bpQuickSat.slider.clipmaxF;
    colorLit = bpQuickLit.slider.clipmaxF;
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
        if (LoadRePaint(r.output, &g_state->canvas)) {
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
        if (SaveRePaint(r.output, &g_state->canvas)) {
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
        SaveRePaint(g_currentFilePath, &g_state->canvas);
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
    SaveRePaint(backupPath, &g_state->canvas);

    // Reload from original
    if (LoadRePaint(g_currentFilePath, &g_state->canvas)) {
        g_state->activeLayer = 0;
        g_state->texCount = 0;
        SyncAllRTs(g_state);
        layersDirty = true;
    }
}

void App_FileSnap(void) {
    /* flatten all visible layers and save to Snaps/ */
    Image flat = GenImageColor(g_state->canvas.width, g_state->canvas.height, BLANK);
    Color* dst = (Color*)flat.data;
    for (int i = 0; i < g_state->canvas.layerCount; i++) {
        if (!g_state->canvas.layerProps[i].visible) continue;
        Color* src = (Color*)g_state->canvas.layerImages[i].data;
        float alpha = g_state->canvas.layerProps[i].op;
        int n = g_state->canvas.width * g_state->canvas.height;
        for (int j = 0; j < n; j++) {
            float sa = src[j].a / 255.0f * alpha;
            float da = dst[j].a / 255.0f;
            float outa = sa + da * (1.0f - sa);
            if (outa > 0.0f) {
                dst[j].r = (uint8_t)((src[j].r * sa + dst[j].r * da * (1.0f - sa)) / outa);
                dst[j].g = (uint8_t)((src[j].g * sa + dst[j].g * da * (1.0f - sa)) / outa);
                dst[j].b = (uint8_t)((src[j].b * sa + dst[j].b * da * (1.0f - sa)) / outa);
                dst[j].a = (uint8_t)(outa * 255.0f);
            }
        }
    }

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

    UIStyle::Init();
    LeftPanel_Init();
    SetTargetFPS(60);

    Painter_Init();
    BrushBlend_Init();
    LoadPenIcons();
    QuickPanel_Init();

    networkBroker.appState = state;

    // load persistent config
    networkBroker.LoadConfig("repaint.ini");

    BParam_Init(&bpOpacity, 0, "Opacity", 0.0f, 1.0f, 1.0f);
    BParam_SetIcon(&bpOpacity, "ctlop");

    BParam_Init(&bpSize, 1, "Size", 1.0f, 4096.0f, 128.0f);
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

    BParam_Init(&bpCloneOpacity, 6, "Clone", 0.7f, 1.0f, 1.0f);
    BParam_SetIcon(&bpCloneOpacity, "ctlcop");

    BParam_Init(&bpQuickHue, 10, "Hue", 0.0f, 1.0f, 0.35f);
    bpQuickHue.slider.clipmaxF = 0.35f;
    BParam_SetIcon(&bpQuickHue, "ctlhue");
    BParam_Init(&bpQuickSat, 11, "Sat", 0.0f, 1.0f, 1.0f);
    bpQuickSat.slider.clipmaxF = 1.0f;
    BParam_SetIcon(&bpQuickSat, "ctlsat");
    BParam_Init(&bpQuickLit, 12, "Lit", 0.0f, 1.0f, 0.5f);
    bpQuickLit.slider.clipmaxF = 0.5f;
    BParam_SetIcon(&bpQuickLit, "ctllit");

    state->canvas = Canvas_Create(800, 600, WHITE);
    state->activeLayer = 0;

    Rectangle viewportBounds = {
        (float)uiPanelWidth, 0,
        (float)(SCREEN_WIDTH - uiPanelWidth - RIGHT_PANEL_WIDTH),
        (float)SCREEN_HEIGHT
    };
    Viewport_Init(&viewport, viewportBounds);
    viewport.broker = &networkBroker;

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

    if (viewport.broker) viewport.broker->poll(state);
    if (viewport.strokeEnded) {
        SyncLayerTexture(state, viewport.endLayer);
        viewport.strokeEnded = false;
    }

    Viewport_Draw(&viewport, state);

    // XOR overlay for gizmo lines (between canvas and ImGui content)
    if (quickPanelShow) {
        BrushGizmo_DrawXOROverlay(state);
    }

    rlImGuiBegin();

    if (g_panelsVisible)
        networkBroker.DrawConnectionUI();
    QuickPanel_Draw(state);
    if (g_panelsVisible) {
        LeftPanel_Draw(state);
        LayerPanel_Draw(state);
    }

    // ── Color picker preview swatch (always visible during Alt+click) ──
    if (g_colorPicking) {
        ImDrawList* fdl = ImGui::GetForegroundDrawList();
        ImVec2 mp = ImGui::GetMousePos();
        Color pc = HSLToRGB(colorHue, colorSat, colorLit);
        ImU32 pCol = IM_COL32(pc.r, pc.g, pc.b, 255);
        float r = 12.0f;
        fdl->AddCircleFilled(ImVec2(mp.x + 20, mp.y + 20), r, pCol, 24);
        fdl->AddCircle(ImVec2(mp.x + 20, mp.y + 20), r, IM_COL32_BLACK, 24, 3.0f);
        fdl->AddCircle(ImVec2(mp.x + 20, mp.y + 20), r + 1, IM_COL32_WHITE, 24, 1.0f);
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
    if (bpOpacity.iconLoaded) UnloadTexture(bpOpacity.iconTex);
    if (bpSize.iconLoaded) UnloadTexture(bpSize.iconTex);
    if (bpHardness.iconLoaded) UnloadTexture(bpHardness.iconTex);
    if (bpSpacing.iconLoaded) UnloadTexture(bpSpacing.iconTex);
    if (bpCurvature.iconLoaded) UnloadTexture(bpCurvature.iconTex);
    if (bpScatter.iconLoaded) UnloadTexture(bpScatter.iconTex);
    if (bpCloneOpacity.iconLoaded) UnloadTexture(bpCloneOpacity.iconTex);
    UnloadPenIcons();
    QuickPanel_Shutdown();
    if (g_dialogFont.texture.id > 0) UnloadFont(g_dialogFont);
    UIStyle::Shutdown();
    Painter_Shutdown();
    BrushBlend_Shutdown();
    CloseWindow();
}
