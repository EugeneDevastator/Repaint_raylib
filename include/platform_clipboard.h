#pragma once
#include <stdint.h>
#include "raylib.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef void (*ClipboardImageCallback)(int width, int height, const unsigned char* rgba);
typedef void (*ClipboardFileCallback)(const char* path);

void Clipboard_SetImageCallback(ClipboardImageCallback cb);
void Clipboard_SetFileCallback(ClipboardFileCallback cb);
void Clipboard_Update(void);
void Clipboard_CopyTexture(Texture2D tex);

#ifdef __cplusplus
}
#endif
