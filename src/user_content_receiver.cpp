#include "repaint.h"
#include "dialog.h"
#include "replay_recorder.h"
#include <string.h>

extern DialogState g_fileDlg;

static char s_pendingPath[1024] = "";
static AppState* s_state = NULL;

/* ── Import an image as new doc or new layer ────────────────────────── */
static void ImportImage(Image img) {
    if (img.data == NULL) return;

    if (img.format != PIXELFORMAT_UNCOMPRESSED_R16G16B16A16)
        ImageFormat(&img, PIXELFORMAT_UNCOMPRESSED_R16G16B16A16);

    int w = img.width, h = img.height;
    int idx = LayerStack_Add(w, h);
    Image* layerImg = LayerStack_GetImage(idx);
    UnloadImage(*layerImg);
    *layerImg = img;
    LayerStack_SyncRTFromImage(idx);
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
                LayerStack_Shutdown();
                LayerStack_Init();
                s_state->doc = Doc_New(w, h);
                s_state->activeLayer = 0;
                s_state->camera.target = Vector2{(float)w * 0.5f, (float)h * 0.5f};
                s_state->camera.zoom = 1.0f;
                LayerStack_SetRenderWindow(w, h);
                ImportImage(img);
            } else {
                ImportImage(img);
            }
            layersDirty = true;
            if (g_recorder) g_recorder->Reset(s_state->doc.width, s_state->doc.height);
        } else {
            UnloadImage(img);
        }
    }
    s_pendingPath[0] = '\0';
}

/* ── Public API ── */
void UserContent_Init(void) {
    s_pendingPath[0] = '\0';
    s_state = NULL;
}

void UserContent_Update(AppState* state) {
    s_state = state;

    if (g_fileDlg.type != 0) return;

    if (s_pendingPath[0] == '\0') {
        if (IsFileDropped()) {
            FilePathList files = LoadDroppedFiles();
            if (files.count > 0 && files.paths[0] && files.paths[0][0]) {
                strncpy(s_pendingPath, files.paths[0], sizeof(s_pendingPath) - 1);
                DialogButtonChoice_Init(&g_fileDlg, "Open File",
                    "file format incorrect",
                    OnDropResult, "New Doc", "Add Layer", "Cancel", NULL);
            }
            UnloadDroppedFiles(files);
        }
    }
}

bool UserContent_IsActive(void) {
    return s_pendingPath[0] != '\0';
}
