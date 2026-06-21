#include "repaint.h"
#include "brush_blend.h"
#include "brush_preset.h"
#include "rlgl.h"
#include "imgui.h"
#include "stroke.h"
#include "tablet.h"
#include "rlImGui.h"
#include "imgui.h"
#include "imgui_internal.h"
#include "dialog.h"
#include "network_broker.h"
#include "test_broker.h"
#include "ui_leftpanel.h"
#include "ui_texpanel.h"
#include "layerstack.h"
#include "undo.h"
#include "replay_recorder.h"
#include "user_content_receiver.h"
#include "StrokeEmitter.h"
#include "StrokeThrottle.h"
#include "external/glad.h"
#include <time.h>

int uiPanelWidth = 250;
bool panelResizing = false;
bool g_panelsVisible = true;

Viewport viewport;

ModuleStack g_moduleStack;

// ── Notification state ─────────────────────────────────────────────────
static struct {
    char text[256];
    double endTime;
} g_notif = {};

void ShowNotification(const char* text, float duration) {
    snprintf(g_notif.text, sizeof(g_notif.text), "%s", text ? text : "");
    g_notif.endTime = GetTime() + duration;
}

void DisplayInfoText(const char* text) {
    ShowNotification(text, 2.0f);
}

static void DrawNotification(void) {
    if (g_notif.text[0] == '\0' || GetTime() >= g_notif.endTime) {
        g_notif.text[0] = '\0';
        return;
    }
    int sw = GetScreenWidth();
    Font f = GetFontDefault();
    int sz = 24;
    float tw = MeasureTextEx(f, g_notif.text, (float)sz, 2).x;
    float tx = (sw - tw) * 0.5f;
    float ty = 16.0f;
    // Shadow (offset by 1px)
    DrawTextEx(f, g_notif.text, Vector2{tx + 1, ty + 1}, (float)sz, 2, BLACK);
    DrawTextEx(f, g_notif.text, Vector2{tx, ty}, (float)sz, 2, WHITE);
}

void SyncImGuiInput(void) {
    ImGuiIO& io = ImGui::GetIO();
    io.MousePos = ImVec2(GetMousePosition().x, GetMousePosition().y);

    // Forward raylib mouse events that GLFW callbacks might have missed
    for (int b = 0; b < 3; b++) {
        int mb = (b == 0) ? MOUSE_BUTTON_LEFT : (b == 1) ? MOUSE_BUTTON_RIGHT : MOUSE_BUTTON_MIDDLE;
        if (IsMouseButtonPressed(mb))
            io.AddMouseButtonEvent(b, true);
        if (IsMouseButtonReleased(mb))
            io.AddMouseButtonEvent(b, false);
    }

    // Forward tablet pen buttons to ImGui (XInput2 doesn't reach GLFW callbacks)
    // bit 0 = tip → ImGui left (0), bit 1 = BTN_STYLUS → ImGui middle (2), bit 2 = BTN_STYLUS2 → ImGui right (1)
    static int g_prevButtons = 0;
    static const int btnMap[3] = {0, 2, 1};
    int buttons = Tablet_GetButtons();
    for (int b = 0; b < 3; b++) {
        bool down = (buttons >> b) & 1;
        bool prev = (g_prevButtons >> b) & 1;
        if (down && !prev) io.AddMouseButtonEvent(btnMap[b], true);
        else if (!down && prev) io.AddMouseButtonEvent(btnMap[b], false);
    }
    g_prevButtons = buttons;
}

float g_splashAlpha = 1.0f;
static Texture2D g_splashTex = {0};
int g_activeHud = HUD_NONE;

void HudSetActive(AppState* state, int newHud) {
    if (newHud == g_activeHud) return;
    const char* name = nullptr;
    switch (g_activeHud) {
        case HUD_CANVAS_XFORM: name = "CanvasXform"; break;
        case HUD_LAYER_XFORM:  name = "LayerXform";  break;
        case HUD_QUICK:        name = "QuickHud";    break;
    }
    if (name) {
        IModule* mod = g_moduleStack.Find(name);
        if (mod) mod->OnExit();
    }
    g_activeHud = newHud;
}

bool g_seamlessPaint = false;
bool g_seamlessPreview = false;
int g_texScaleMode = 0;
int g_texPanelAreaY = 0;
bool g_useViewRes = false;
UndoManager* g_undoManager = nullptr;
ReplayRecorder* g_recorder = nullptr;
bool g_replayPopupActive = false;
bool g_pixelPerfect = false;
float g_pivotCursorX = 0.0f, g_pivotCursorY = 0.0f;

// ── Helper: sync the canvas-window matrix + render window from Document ──
// Returns the output pixel size via outW/outH.
static void SyncCanvasFromDoc(const Document* doc, int* outW, int* outH) {
    int cw = DocOutPxW(doc), ch = DocOutPxH(doc);
    if (outW) *outW = cw; if (outH) *outH = ch;
    float cv[6];
    ComputeCanvasMatrix(doc->ppu, &doc->window, cw, ch, cv);
    LayerStack_SetCanvasView(cv);
    LayerStack_SetRenderWindow(cw, ch);
}

static void DrawSplash(const char* msg) {
    int sw = GetScreenWidth(), sh = GetScreenHeight();
    if (g_splashTex.id == 0 && FileExists("resources/splash.png")) {
        Image img = LoadImage("resources/splash.png");
        if (img.data) g_splashTex = LoadTextureFromImage(img);
        UnloadImage(img);
    }
    BeginDrawing();
    ClearBackground(Color{35, 35, 40, 255});
    if (g_splashTex.id > 0) {
        float scale = fminf(sw / (float)g_splashTex.width, sh / (float)g_splashTex.height) * 0.7f;
        float x = (sw - g_splashTex.width * scale) * 0.5f;
        float y = (sh - g_splashTex.height * scale) * 0.5f - 30;
        DrawTextureEx(g_splashTex, Vector2{x, y}, 0.0f, scale, WHITE);
    }
    DrawText(msg, sw / 2 - MeasureText(msg, 20) / 2, sh / 2 + 80, 20, Color{230, 230, 240, 255});
    EndDrawing();
}

/* ── File dialog / path state ──────────────────────────────────────────── */
static AppState* g_state = NULL;

/* ── New canvas dialog state ───────────────────────────────────────────── */
static bool g_newCanvasActive = false;
static bool g_newCanvasConfirm = false;
static int g_newW = 800;
static int g_newH = 600;
static int g_newPreset = 0;
static int g_presetW[] = { 800, 1024, 1280, 1920, 2560, 3840 };
static int g_presetH[] = { 600, 768,  720,  1080, 1440, 2160 };
static const char* g_presets[] = { "800x600", "1024x768", "1280x720", "1920x1080", "2560x1440", "3840x2160" };
static char g_currentFilePath[1024] = "";
static Font g_dialogFont = {0};

void UpdateUI(AppState* state) {
    Vector2 mousePos = GetMousePosition();

    if (IsKeyPressed(KEY_TAB))
        g_panelsVisible = !g_panelsVisible;

    // Shift toggles quick HUD (not Ctrl+Shift which is undo/redo)
    if (!IsKeyDown(KEY_LEFT_CONTROL) && (IsKeyPressed(KEY_LEFT_SHIFT) || IsKeyPressed(KEY_RIGHT_SHIFT))) {
        if (g_activeHud == HUD_QUICK)
            g_activeHud = HUD_NONE;
        else
            g_activeHud = HUD_QUICK;
    }

    if (g_activeHud != HUD_QUICK)
        quickPanelMouseMode = 0;


    if (IsKeyPressed(KEY_TWO)) state->mode = eSmudge;
    if (IsKeyPressed(KEY_THREE)) state->mode = ePolyStripe;
    if (IsKeyPressed(KEY_FOUR)) state->mode = eDistort;
    if (IsKeyPressed(KEY_FIVE)) state->mode = eContrast;

    // Toggle framing mode (C key — "crop" framing)
    if (IsKeyPressed(KEY_C)) {
        bool enteringCrop = (state->framingMode == FRAME_DEFAULT);
        if (enteringCrop) {
            HudSetActive(state, HUD_CANVAS_XFORM);
            state->framingMode = FRAME_CROP;
            // Frame camera to show all layers
            Rectangle sb; LayerStack_GetSceneBounds(&sb);
            if (sb.width > 0 && sb.height > 0) {
                float cx = sb.x + sb.width/2, cy = sb.y + sb.height/2;
                state->camera.target = Vector2{cx, cy};
                float pad = 1.1f;
                float zoomX = viewport.bounds.width / (sb.width * pad);
                float zoomY = viewport.bounds.height / (sb.height * pad);
                state->camera.zoom = fminf(zoomX, zoomY);
            } else {
                state->camera.target = Vector2{0,0};
                state->camera.zoom = 1.0f;
            }
        } else {
            HudSetActive(state, HUD_NONE);
            state->framingMode = FRAME_DEFAULT;
            state->camera.target = Vector2{0, 0};
            state->camera.zoom = 1.0f;
        }
        layersDirty = true;
    }

    // Undo / Redo
    if (IsKeyDown(KEY_LEFT_CONTROL) && !IsKeyDown(KEY_LEFT_SHIFT) && IsKeyPressed(KEY_Z)) {
        if (state->undo) {
            TexSlotID slot = state->editTexMode ? state->editTexSlot : LayerStack_GetSlotID(state->activeLayer);
            if (TM_IsValid(slot))
                state->undo->Undo(state, slot);
        }
    }
    if (IsKeyDown(KEY_LEFT_CONTROL) && IsKeyDown(KEY_LEFT_SHIFT) && IsKeyPressed(KEY_Z)) {
        if (state->undo) {
            TexSlotID slot = state->editTexMode ? state->editTexSlot : LayerStack_GetSlotID(state->activeLayer);
            if (TM_IsValid(slot))
                state->undo->Redo(state, slot);
        }
    }

    // Replay confirmation popup
    if (IsKeyDown(KEY_LEFT_CONTROL) && IsKeyDown(KEY_LEFT_SHIFT) && IsKeyPressed(KEY_R)) {
        if (g_recorder) g_replayPopupActive = true;
    }

    // Toggle texture editing mode with T key
    if (IsKeyPressed(KEY_T)) {
        if (state->editTexMode) {
            state->editTexMode = 0;
            state->editTexSlot = TM_INVALID_SLOT;
        } else if (TM_Count(TM_BUCKET_USER) > 0) {
            state->editTexMode = 1;
            if (!TM_IsValid(state->editTexSlot)) {
                // Find first valid slot
                for (int s = 0; s < TM_SLOTS_PER_BUCKET; s++) {
                    TexSlotID id = {TM_BUCKET_USER, (uint8_t)s};
                    if (TM_IsValid(id)) { state->editTexSlot = id; break; }
                }
            }
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

    if (g_texScaleMode == 1) {
        // Global scale: 1 UV = 256 canvas px (stamp diameter / 256 = base texScale)
        float s = GetModVal(&bpTexScale);
        float r = state->currentBrush.Realb.rad_out;
        state->currentBrush.Realb.texScale = s * r / 128.0f;
    } else {
        state->currentBrush.Realb.texScale = GetModVal(&bpTexScale);
    }
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
    return Dialog_IsActive() || g_newCanvasActive;
}

char g_fileWorkingDir[1024] = "";

static void _updateWorkingDir(const char* path) {
    if (!path || !path[0]) return;
    const char* sep = strrchr(path, '/');
    const char* sep2 = strrchr(path, '\\');
    if (sep2 > sep) sep = sep2;
    if (!sep) return;
    size_t len = sep - path;
    if (len >= sizeof(g_fileWorkingDir)) len = sizeof(g_fileWorkingDir) - 1;
    memcpy(g_fileWorkingDir, path, len);
    g_fileWorkingDir[len] = '\0';
}

/* ── Callbacks ─────────────────────────────────────────────────────────── */

static void OnOpenResult(DialogResult r) {
    if (r.wasClosed && r.success && r.output[0]) {
        LayerStack_Shutdown(); LayerStack_Init();
        if (LoadRePaint(r.output, &g_state->doc, g_state)) {
            int len = (int)strlen(r.output);
            if (len < (int)sizeof(g_currentFilePath) - 1)
                memcpy(g_currentFilePath, r.output, len + 1);
            g_state->activeLayer = 0;
            SyncCanvasFromDoc(&g_state->doc, NULL, NULL);
            g_state->camera.target = Vector2{0, 0};
            layersDirty = true;
        } else {
            // Try as a standard image (PNG, JPEG, BMP, GIF, etc.)
            Image img = LoadImage(r.output);
            if (img.data != NULL) {
                int w = img.width, h = img.height;
                if (img.format != PIXELFORMAT_UNCOMPRESSED_R16G16B16A16) {
                    ImageFormat(&img, PIXELFORMAT_UNCOMPRESSED_R16G16B16A16);
                } else {
                    // Already 16-bit; ensure proper byte order / no extra work
                }
                g_state->doc = Doc_New(w, h);
                g_state->activeLayer = 0;
                SyncCanvasFromDoc(&g_state->doc, NULL, NULL);
                g_state->camera.target = Vector2{0, 0};
                g_state->camera.zoom = 1.0f;
                int idx = LayerStack_Add(w, h);
                LayerStack_UploadToGPU(idx, img);
                layersDirty = true;
                // Don't set g_currentFilePath — imported images should be
                // saved as .re.png via Save As before they can be reloaded.
            } else {
                // Failed to load as repaint or image — try replay, then make default canvas
                int w = 800, h = 600;
                g_state->doc = Doc_New(w, h);
                g_state->activeLayer = 0;
                SyncCanvasFromDoc(&g_state->doc, NULL, NULL);
                g_state->camera.target = Vector2{0, 0};
                g_state->camera.zoom = 1.0f;
                int idx = LayerStack_Add(w, h);
                Image fillImg = GenImageColor(w, h, WHITE);
                ImageFormat(&fillImg, PIXELFORMAT_UNCOMPRESSED_R16G16B16A16);
                LayerStack_UploadToGPU(idx, fillImg);
                layersDirty = true;
                if (g_recorder) {
                    g_recorder->Reset(w, h);
                    g_recorder->Load(r.output);
                }
            }
        }
        _updateWorkingDir(r.output);
    }
}

static void OnSaveResult(DialogResult r) {
    if (r.wasClosed && r.success && r.output[0]) {
        if (SaveRePaint(r.output, &g_state->doc, g_state)) {
            int len = (int)strlen(r.output);
            if (len < (int)sizeof(g_currentFilePath) - 1)
                memcpy(g_currentFilePath, r.output, len + 1);
            // Auto-save replay
            if (g_recorder) {
                char rpPath[1024];
                snprintf(rpPath, sizeof(rpPath), "%s.re.play", r.output);
                g_recorder->Save(rpPath);
            }
            _updateWorkingDir(r.output);
        }
    }
}

void app_new_document(int w, int h, Color fill) {
    g_state->doc = Doc_New(w, h);
    g_state->activeLayer = 0;
    SyncCanvasFromDoc(&g_state->doc, NULL, NULL);
    g_state->camera.target = Vector2{0, 0};
    int idx = LayerStack_Add(w, h);
    RenderTexture2D rt = LayerStack_GetRT(idx);
    BeginTextureMode(rt);
    ClearBackground(fill);
    EndTextureMode();
    g_currentFilePath[0] = '\0';
    if (g_recorder) g_recorder->Reset(w, h);
}

static void DoCreateNew(void) {
    LayerStack_Shutdown(); LayerStack_Init();
    app_new_document(g_newW, g_newH, WHITE);
    layersDirty = true;
    g_newCanvasActive = false;
    ImGui::CloseCurrentPopup();
}

void App_FileNew(void) {
    g_newCanvasActive = true;
    g_newCanvasConfirm = false;
}

void App_FileOpen(void) {
    DialogOpen_Init("Open", ".re.png/.png",
                    g_fileWorkingDir[0] ? g_fileWorkingDir : NULL, OnOpenResult);
}

void App_FileSave(void) {
    if (g_currentFilePath[0]) {
        SaveRePaint(g_currentFilePath, &g_state->doc, g_state);
        if (g_recorder) {
            char rpPath[1024];
            snprintf(rpPath, sizeof(rpPath), "%s.re.play", g_currentFilePath);
            g_recorder->Save(rpPath);
        }
    } else {
        App_FileSaveAs();
    }
}

void App_FileSaveAs(void) {
    const char* name = "untitled";
    if (g_currentFilePath[0]) name = GetFileNameWithoutExt(g_currentFilePath);
    DialogSaveAs_Init("Save As", ".re.png", name,
                      g_fileWorkingDir[0] ? g_fileWorkingDir : NULL, OnSaveResult);
}

void App_FileReload(void) {
    if (!g_currentFilePath[0]) return;
    char backupPath[1048];
    unsigned int hash = (unsigned int)time(NULL) ^ (unsigned int)(uintptr_t)g_state;
    hash = hash * 1103515245 + 12345;
    const char* dir = GetDirectoryPath(g_currentFilePath);
    const char* fname = GetFileNameWithoutExt(g_currentFilePath);
    snprintf(backupPath, sizeof(backupPath), "%s/%s_backup_%08x%s",
             dir, fname, (hash / 65536) % 0xFFFFFFFFu, ".re.png");
    SaveRePaint(backupPath, &g_state->doc, g_state);
    LayerStack_Shutdown(); LayerStack_Init();
    if (LoadRePaint(g_currentFilePath, &g_state->doc, g_state)) {
        g_state->activeLayer = 0;
        SyncCanvasFromDoc(&g_state->doc, NULL, NULL);
        g_state->camera.target = Vector2{0, 0};
        layersDirty = true;
    }
}



void App_FileSnap(void) {
    /* If this is an unsaved new document, auto-save it first */
    if (!g_currentFilePath[0]) {
        time_t now = time(NULL);
        struct tm* t = localtime(&now);
        const char* appDir = GetApplicationDirectory();
        char savePath[1024];
        snprintf(savePath, sizeof(savePath), "%sSaves/auto_%04d%02d%02d_%02d%02d%02d.re.png",
                 appDir,
                 t->tm_year + 1900, t->tm_mon + 1, t->tm_mday,
                 t->tm_hour, t->tm_min, t->tm_sec);

        if (SaveRePaint(savePath, &g_state->doc, g_state)) {
            int len = (int)strlen(savePath);
            if (len < (int)sizeof(g_currentFilePath) - 1)
                memcpy(g_currentFilePath, savePath, len + 1);
            if (g_recorder) {
                char rpPath[1024];
                snprintf(rpPath, sizeof(rpPath), "%s.re.play", savePath);
                g_recorder->Save(rpPath);
            }
            _updateWorkingDir(savePath);
        }
    }

    /* GPU composite + dither → 8-bit snapshot */
    Image flat = CompositeLayersWithDither(g_state);

    /* build path: Snaps/snap_YYYYMMDD_HHMMSS.png */
    {
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
}

/* ── App_Init ──────────────────────────────────────────────────────────── */

void App_Init(AppState* state) {
    g_state = state;

    SetConfigFlags(FLAG_WINDOW_RESIZABLE);
    SetTraceLogLevel(LOG_WARNING);
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

    LayerStack_Init();
    app_new_document(1024, 768, WHITE);

    Rectangle viewportBounds = {
        (float)uiPanelWidth, 0,
        (float)(SCREEN_WIDTH - uiPanelWidth - RIGHT_PANEL_WIDTH),
        (float)SCREEN_HEIGHT
    };
    Viewport_Init(&viewport, viewportBounds);
    viewport.broker = g_useTestBroker ? (ICommandBroker*)&g_testBroker : (ICommandBroker*)&networkBroker;
    g_broker = viewport.broker;

    // LocalPlayer modules
    static StrokeThrottle s_throttle;
    static StrokeEmitter s_emitter(&s_throttle);
    g_throttle = &s_throttle;
    g_emitter = &s_emitter;

    state->undo = new UndoManager();
    g_undoManager = state->undo;

    g_recorder = new ReplayRecorder();
    g_recorder->Reset(DocOutPxW(&state->doc), DocOutPxH(&state->doc));

    state->camera = Camera2D{};
    state->camera.target = Vector2{0, 0};
    state->camera.offset = Vector2{viewportBounds.x + viewportBounds.width * 0.5f, viewportBounds.y + viewportBounds.height * 0.5f};

    // ── Module stack ──
    // Add order = bottom-up for DrawGL; reverse for HandleInput
    int sw = GetScreenWidth(), sh = GetScreenHeight();
    DrawRect vpRect{(float)uiPanelWidth, 0, (float)(sw - uiPanelWidth - RIGHT_PANEL_WIDTH), (float)sh};
    g_moduleStack.Add(std::unique_ptr<IModule>(new ViewportModule(state)), vpRect);
    g_moduleStack.Add(std::unique_ptr<IModule>(new QuickHudModule(state)), vpRect);
    g_moduleStack.Add(std::unique_ptr<IModule>(new LayerXformModule(state)), vpRect);
    g_moduleStack.Add(std::unique_ptr<IModule>(new CanvasXformModule(state)), vpRect);
    g_moduleStack.Add(std::unique_ptr<IModule>(new RightPanelModule(state)),
        DrawRect{(float)(sw - RIGHT_PANEL_WIDTH), 0, (float)RIGHT_PANEL_WIDTH, (float)sh});
    g_moduleStack.Add(std::unique_ptr<IModule>(new LeftPanelModule(state)),
        DrawRect{0, 0, (float)uiPanelWidth, (float)sh});
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
    state->currentBrush.Realb.texBlendMode = 2;
    state->currentBrush.Realb.texNoisemode = 2;
    state->currentBrush.Realb.texColorMode = 0;
    state->currentBrush.Realb.col = BLACK;
    state->currentBrush.Realb.userTexOriginX = 0.5f;
    state->currentBrush.Realb.userTexOriginY = 0.5f;
    state->currentBrush.Realb.userTexDirection = 0.0f;

    colorHue = 0.35f;
    colorSat = 1.0f;
    colorLit = 0.5f;

    /* Create default directories */
    const char* ad = GetApplicationDirectory();
    char p[1024];
    snprintf(p, sizeof(p), "%sSaves", ad); MakeDirectory(p);
    snprintf(p, sizeof(p), "%sSnaps", ad); MakeDirectory(p);

    /* Load custom dialog font — bilinear filter for smooth OTF rendering */
    g_dialogFont = LoadFontEx("resources/Cadman_Roman.otf", 28, 0, 0);
    SetTextureFilter(g_dialogFont.texture, TEXTURE_FILTER_BILINEAR);
    DialogSetFont(g_dialogFont, 26);

    g_currentFilePath[0] = '\0';

    UserContent_Init();

    /* Load default brush preset */
    Preset_ApplyDefault(state);
}

/* ── App_Draw ──────────────────────────────────────────────────────────── */

void App_Draw(AppState* state) {
    int sw = GetScreenWidth();
    int sh = GetScreenHeight();

    // ── Compute module rects ──
    DrawRect leftRect, vpRect, rightRect;
    if (g_panelsVisible) {
        leftRect  = DrawRect{0, 0, (float)uiPanelWidth, (float)sh};
        vpRect    = DrawRect{(float)uiPanelWidth, 0, (float)(sw - uiPanelWidth - RIGHT_PANEL_WIDTH), (float)sh};
        rightRect = DrawRect{(float)(sw - RIGHT_PANEL_WIDTH), 0, (float)RIGHT_PANEL_WIDTH, (float)sh};
    } else {
        leftRect  = DrawRect{0, 0, 0, 0};
        vpRect    = DrawRect{0, 0, (float)sw, (float)sh};
        rightRect = DrawRect{0, 0, 0, 0};
    }
    g_moduleStack.SetRect("LeftPanel",   leftRect);
    g_moduleStack.SetRect("Viewport",    vpRect);
    g_moduleStack.SetRect("QuickHud",    vpRect);
    g_moduleStack.SetRect("LayerXform",  vpRect);
    g_moduleStack.SetRect("CanvasXform", vpRect);
    g_moduleStack.SetRect("RightPanel",  rightRect);

    BeginDrawing();
    ClearBackground(Color{220, 220, 220, 255});

    /* Check dropped files */
    UserContent_Update(state);

    /* If dialog active, draw it modelly — skip viewport/imgui entirely */
    if (Dialog_IsActive()) {
        Dialog_Draw();
        EndDrawing();
        return;
    }

    /* Normal rendering path */

    // toggle network UI
    if (IsKeyDown(KEY_LEFT_CONTROL) && IsKeyPressed(KEY_N))
        networkBroker.showUI = !networkBroker.showUI;

    UserTexture_Update(state);

    // Process user input → segments
    g_emitter->isFirstDabPainted = false;
    g_emitter->ProcessInputQueue();

    // Unpack + render dabs with per-frame pixel budget
    if (g_throttle) g_throttle->DrawPending(state, 65536*32);

    if (viewport.broker) viewport.broker->poll(state);
    if (g_recorder) g_recorder->poll(state);

    // ── Module GL draws (viewport canvas + overlays) ──
    g_moduleStack.DrawGL();

    // ── Update layer preview thumbnails ─────────────────────────────
    LayerPanel_UpdatePreviews(state);

    // ── Color picker readback from GPU composite ────────────────────
    if (g_colorPicking) {
        const int ps = 5;
        static RenderTexture2D pickSub = {0};
        if (pickSub.id == 0 || pickSub.texture.width != ps || pickSub.texture.height != ps) {
            if (pickSub.id != 0) UnloadRenderTexture(pickSub);
            pickSub = LoadRenderTexture(ps, ps);
        }

        Color picked = {0,0,0,0};
        int gi = 0;

        Image screen = LoadImageFromScreen();
        if (screen.data) {
            int sw = screen.width, sh = screen.height;
            int sx = g_colorPickScreenX - 2, sy = g_colorPickScreenY - 2;
            if (sx < 0) sx = 0; if (sy < 0) sy = 0;
            if (sx + ps > sw) sx = sw - ps; if (sy + ps > sh) sy = sh - ps;
            for (int py = 0; py < ps; py++)
                for (int px = 0; px < ps; px++) {
                    Color c = GetImageColor(screen, sx + px, sy + py);
                    g_colorPickGrid[gi++] = c;
                    if (px == 2 && py == 2) picked = c;
                }
        }
        UnloadImage(screen);

        if (gi > 0 && picked.a > 0) {
            float tH, tS, tL;
            RGBToHSL(picked, tH, tS, tL);
            float spd = 0.5f;
            float dh = tH - colorHue;
            if (dh > 0.5f) dh -= 1.0f; else if (dh < -0.5f) dh += 1.0f;
            colorHue += dh * spd;
            if (colorHue < 0.0f) colorHue += 1.0f; else if (colorHue > 1.0f) colorHue -= 1.0f;
            colorSat += (tS - colorSat) * spd;
            colorLit += (tL - colorLit) * spd;
            bpQuickHue.user.clipmaxF = colorHue;
            bpQuickSat.user.clipmaxF = colorSat;
            bpQuickLit.user.clipmaxF = colorLit;
        }
    }

    rlImGuiBegin();
    SyncImGuiInput();

    rlSetBlendMode(RL_BLEND_ALPHA);
    if (g_panelsVisible)
        networkBroker.DrawConnectionUI();
    rlSetBlendMode(RL_BLEND_ALPHA);
    // Module GUI draws (quick HUD, then left + right panels)
    g_moduleStack.DrawGUI();

    rlSetBlendMode(RL_BLEND_ALPHA);
    Changelog_Draw();

    // ── Color picker 3×3 magnifier ──────────────────────────────────
    if (g_colorPicking) {
        ImDrawList* fdl = ImGui::GetForegroundDrawList();
        ImVec2 mp = ImGui::GetMousePos();
        int sz = 28;
        ImVec2 org(mp.x + 12, mp.y + 12);
        for (int i = 0; i < 25; i++) {
            int gx = i % 5, gy = i / 5;
            Color c = g_colorPickGrid[i];
            ImU32 col = IM_COL32(c.r, c.g, c.b, 255);
            fdl->AddRectFilled(ImVec2(org.x + gx * sz, org.y + gy * sz),
                ImVec2(org.x + (gx + 1) * sz, org.y + (gy + 1) * sz), col);
            fdl->AddRect(ImVec2(org.x + gx * sz, org.y + gy * sz),
                ImVec2(org.x + (gx + 1) * sz, org.y + (gy + 1) * sz),
                IM_COL32(100, 100, 100, 200));
        }
    }

    /* Replay confirmation popup */
    if (g_replayPopupActive) {
        g_replayPopupActive = false;
        ImGui::OpenPopup("Replay Recording");
    }
    if (ImGui::BeginPopupModal("Replay Recording", NULL, ImGuiWindowFlags_AlwaysAutoResize)) {
        int n = g_recorder ? (int)g_recorder->m_segs.size() : -1;
        printf("[REPLAYPOPUP] g_recorder=%p segs=%d\n", (void*)g_recorder, n); fflush(stdout);
        ImGui::Text("Replay %d recorded strokes on the current canvas?", n);
        ImGui::Separator();
        if (ImGui::Button("Yes", ImVec2(80, 0))) {
            if (g_recorder) g_recorder->m_playing = true;
            ImGui::CloseCurrentPopup();
        }
        ImGui::SameLine();
        if (ImGui::Button("No", ImVec2(80, 0)))
            ImGui::CloseCurrentPopup();
        ImGui::EndPopup();
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

    DrawNotification();

    EndDrawing();

    /* Check file rename result after dialog callback has a chance to fire */
    /* (dialog draws at beginning of next frame) */
}

/* ── App_Close ─────────────────────────────────────────────────────────── */

void App_Close(AppState* state) {
    networkBroker.SaveConfig();
    networkBroker.Disconnect();
    LayerStack_Shutdown();

    LeftPanel_Shutdown();
    UnloadViewportRenderer();
    ViewportHUD_Shutdown();
    Modulators_Shutdown();
    UnloadPenIcons();
    QuickPanel_Shutdown();
    // Cleanup all textures via TextureManager
    TM_Shutdown();
    if (g_dialogFont.texture.id > 0) UnloadFont(g_dialogFont);
    UIStyle::Shutdown();
    BrushBlend_Shutdown();
    Tablet_Shutdown();
    UserTexture_Shutdown();
    CloseWindow();
}
