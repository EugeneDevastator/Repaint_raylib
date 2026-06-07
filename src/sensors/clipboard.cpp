#include "platform_clipboard.h"
#include "repaint.h"
#include <cstdlib>
#include <cstring>

// ── Platform backend (implemented in clipboard_win.cpp / clipboard_lnx.cpp) ──
bool ClipboardPlatform_GetImage(int* w, int* h, void** rgba);
bool ClipboardPlatform_GetFilePath(char* path, int maxLen);
bool ClipboardPlatform_GetText(char* text, int maxLen);
bool ClipboardPlatform_SetImage(int w, int h, const void* rgba);

// ── Static state ──
static ClipboardImageCallback s_imageCb = NULL;
static ClipboardFileCallback s_fileCb = NULL;

void Clipboard_SetImageCallback(ClipboardImageCallback cb) { s_imageCb = cb; }
void Clipboard_SetFileCallback(ClipboardFileCallback cb) { s_fileCb = cb; }

void Clipboard_Update(void) {
    if (!IsKeyDown(KEY_LEFT_CONTROL) || !IsKeyPressed(KEY_V)) return;

    // Priority: Image > File > Text (as file path)
    int w, h;
    void* rgba = NULL;
    if (ClipboardPlatform_GetImage(&w, &h, &rgba)) {
        if (s_imageCb) s_imageCb(w, h, (const unsigned char*)rgba);
        free(rgba);
        return;
    }

    char path[1024];
    if (ClipboardPlatform_GetFilePath(path, sizeof(path))) {
        if (s_fileCb) s_fileCb(path);
        return;
    }

    if (ClipboardPlatform_GetText(path, sizeof(path))) {
        if (s_fileCb) s_fileCb(path);
    }
}

void Clipboard_CopyTexture(Texture2D tex) {
    Image img = LoadImageFromTexture(tex);
    if (img.data) {
        ImageFlipVertical(&img);
        ClipboardPlatform_SetImage(img.width, img.height, img.data);
        UnloadImage(img);
    }
}
