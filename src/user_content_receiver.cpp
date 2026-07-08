#include "repaint.h"
#include "compositor.h"
#include "dialog.h"
#include "replay_recorder.h"
#include "platform_clipboard.h"
#include <string.h>
#include <cstdlib>

extern char g_fileWorkingDir[1024];

static char s_pendingPath[1024] = "";
static AppState* s_state = NULL;

/* ── Clipboard paste state ─────────────────────────────────────────── */
static unsigned char* s_clipboardPixels = NULL;
static uint16_t* s_clipboardPixels16 = NULL;
static int s_clipboardW = 0, s_clipboardH = 0;

/* ── Import an image as new doc or new layer ────────────────────────── */
static void ImportImage(Image img) {
    if (img.data == NULL) return;

    if (img.format != PIXELFORMAT_UNCOMPRESSED_R16G16B16A16)
        ImageFormat(&img, PIXELFORMAT_UNCOMPRESSED_R16G16B16A16);

    int w = img.width, h = img.height;
    int idx = LayerStack_Add(w, h);
    LayerStack_UploadToGPU(idx, img);
    if (s_state) s_state->activeLayer = idx;
}

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

/* ── Drop callback ──────────────────────────────────────────────────── */
static void OnDropResult(DialogResult r) {
    if (r.wasClosed && r.success && r.output[0] && s_pendingPath[0] && s_state) {
        bool asNewDoc = (strcmp(r.output, "New Doc") == 0);

        Image img = LoadImage(s_pendingPath);
        if (img.data) {
            if (img.format != PIXELFORMAT_UNCOMPRESSED_R16G16B16A16)
                ImageFormat(&img, PIXELFORMAT_UNCOMPRESSED_R16G16B16A16);

            int w = img.width, h = img.height;

            if (asNewDoc) {
                Compositor_Shutdown(); Compositor_Init();
                LayerStack_Shutdown();
                LayerStack_Init();
                s_state->doc = Doc_New(w, h);
                s_state->activeLayer = 0;
                { int cw = DocOutPxW(&s_state->doc), ch = DocOutPxH(&s_state->doc);
                float cv[6]; ComputeCanvasMatrix(&s_state->doc.window, cw, ch, cv);
                LayerStack_SetCanvasView(cv); LayerStack_SetCanvasXform(&s_state->doc.window); LayerStack_InitCanvas(cw, ch); }
                s_state->camera.target = Vector2{s_state->doc.window.ww * 0.5f, s_state->doc.window.wh * 0.5f};
                s_state->camera.zoom = 1.0f;
                ImportImage(img);
            } else {
                ImportImage(img);
            }
            layersDirty = true;
            if (g_recorder) g_recorder->Reset(DocOutPxW(&s_state->doc), DocOutPxH(&s_state->doc));
        } else {
            UnloadImage(img);
        }
    }
    _updateWorkingDir(s_pendingPath);
    s_pendingPath[0] = '\0';
}

/* ── Paste callback (from clipboard image or file) ──────────────────── */
static void OnPasteResult(DialogResult r) {
    bool hasPixels16 = (s_clipboardPixels16 != NULL);
    bool hasPixels8  = !hasPixels16 && (s_clipboardPixels != NULL);
    bool hasPath     = (s_pendingPath[0] != '\0');

    if (!r.wasClosed || !r.success || !r.output[0] || !(hasPixels16 || hasPixels8 || hasPath)) {
        if (s_clipboardPixels)  { free(s_clipboardPixels);  s_clipboardPixels = NULL; }
        if (s_clipboardPixels16){ free(s_clipboardPixels16); s_clipboardPixels16 = NULL; }
        s_pendingPath[0] = '\0';
        return;
    }

    bool asNewDoc = (strcmp(r.output, "New Doc") == 0);
    Image img = {0};
    bool pastedAs16 = false;

    if (hasPixels16) {
        img.data     = s_clipboardPixels16;
        img.width    = s_clipboardW;
        img.height   = s_clipboardH;
        img.format   = PIXELFORMAT_UNCOMPRESSED_R16G16B16A16;
        img.mipmaps  = 1;
        s_clipboardPixels16 = NULL;
        pastedAs16 = true;
    } else if (hasPixels8) {
        img.data     = s_clipboardPixels;
        img.width    = s_clipboardW;
        img.height   = s_clipboardH;
        img.format   = PIXELFORMAT_UNCOMPRESSED_R8G8B8A8;
        img.mipmaps  = 1;
        s_clipboardPixels = NULL;   // img now owns the data
    } else {
        _updateWorkingDir(s_pendingPath);
        img = LoadImage(s_pendingPath);
        s_pendingPath[0] = '\0';
    }

    if (img.data) {
        if (img.format == PIXELFORMAT_UNCOMPRESSED_R16G16B16A16) {
            // Already 16-bit — no conversion needed
        } else if (img.format != PIXELFORMAT_UNCOMPRESSED_R16G16B16A16) {
            ImageFormat(&img, PIXELFORMAT_UNCOMPRESSED_R16G16B16A16);
        }

        int w = img.width, h = img.height;

        if (asNewDoc && s_state) {
            Compositor_Shutdown(); Compositor_Init();
            LayerStack_Shutdown();
            LayerStack_Init();
            s_state->doc = Doc_New(w, h);
            s_state->activeLayer = 0;
            { int cw = DocOutPxW(&s_state->doc), ch = DocOutPxH(&s_state->doc);
            float cv[6]; ComputeCanvasMatrix(&s_state->doc.window, cw, ch, cv);
            LayerStack_SetCanvasView(cv); LayerStack_SetCanvasXform(&s_state->doc.window); LayerStack_InitCanvas(cw, ch); }
            s_state->camera.target = Vector2{s_state->doc.window.ww * 0.5f, s_state->doc.window.wh * 0.5f};
            s_state->camera.zoom = 1.0f;
            ImportImage(img);
            if (g_recorder) g_recorder->Reset(DocOutPxW(&s_state->doc), DocOutPxH(&s_state->doc));
        } else {
            ImportImage(img);
            if (g_recorder) g_recorder->Reset(s_state ? DocOutPxW(&s_state->doc) : w,
                                               s_state ? DocOutPxH(&s_state->doc) : h);
        }
        layersDirty = true;
        DisplayInfoText(pastedAs16 ? "Pasted as 16-bit" : "Pasted as 8-bit");
    } else {
        UnloadImage(img);
    }
}

static void ClipboardImageHandler(int w, int h, const unsigned char* rgba) {
    if (Dialog_IsActive()) return;

    free(s_clipboardPixels); s_clipboardPixels = NULL;
    free(s_clipboardPixels16); s_clipboardPixels16 = NULL;
    s_clipboardPixels = (unsigned char*)malloc(w * h * 4);
    if (!s_clipboardPixels) return;
    memcpy(s_clipboardPixels, rgba, w * h * 4);
    s_clipboardW = w;
    s_clipboardH = h;

    DialogButtonChoice_Init("Pick your option",
        "Paste image content",
        OnPasteResult, "New Doc", "Add Layer", "Cancel", NULL);
}

static void ClipboardImage16Handler(int w, int h, const uint16_t* rgba16) {
    if (Dialog_IsActive()) return;

    free(s_clipboardPixels); s_clipboardPixels = NULL;
    free(s_clipboardPixels16); s_clipboardPixels16 = NULL;
    s_clipboardPixels16 = (uint16_t*)malloc((size_t)w * h * 4 * sizeof(uint16_t));
    if (!s_clipboardPixels16) return;
    memcpy(s_clipboardPixels16, rgba16, (size_t)w * h * 4 * sizeof(uint16_t));
    s_clipboardW = w;
    s_clipboardH = h;

    DialogButtonChoice_Init("Pick your option",
        "Paste image content",
        OnPasteResult, "New Doc", "Add Layer", "Cancel", NULL);
}

static void ClipboardFileHandler(const char* path) {
    if (Dialog_IsActive()) return;

    strncpy(s_pendingPath, path, sizeof(s_pendingPath) - 1);
    s_pendingPath[sizeof(s_pendingPath) - 1] = '\0';

    DialogButtonChoice_Init("Pick your option",
        "Open as file",
        OnPasteResult, "New Doc", "Add Layer", "Cancel", NULL);
}

/* ── Public API ── */
void UserContent_Init(void) {
    s_pendingPath[0] = '\0';
    s_state = NULL;
    s_clipboardPixels = NULL;
    s_clipboardPixels16 = NULL;

    Clipboard_SetImageCallback(ClipboardImageHandler);
    Clipboard_SetImage16Callback(ClipboardImage16Handler);
    Clipboard_SetFileCallback(ClipboardFileHandler);
}

void UserContent_Update(AppState* state) {
    s_state = state;

    if (Dialog_IsActive()) return;

    if (s_pendingPath[0] == '\0') {
        if (IsFileDropped()) {
            FilePathList files = LoadDroppedFiles();
            if (files.count > 0 && files.paths[0] && files.paths[0][0]) {
                strncpy(s_pendingPath, files.paths[0], sizeof(s_pendingPath) - 1);
                DialogButtonChoice_Init("Pick your option",
                    "Open as file",
                    OnDropResult, "New Doc", "Add Layer", "Cancel", NULL);
            }
            UnloadDroppedFiles(files);
        }
    }
}

bool UserContent_IsActive(void) {
    return s_pendingPath[0] != '\0';
}
