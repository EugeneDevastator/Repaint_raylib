#include "platform_clipboard.h"
#include <cstdio>
#include <cstdlib>
#include <cstring>

bool ClipboardPlatform_GetImage(int* w, int* h, void** rgba) {
    (void)w; (void)h; (void)rgba;
    return false;
}

bool ClipboardPlatform_GetFilePath(char* path, int maxLen) {
    FILE* f = popen("xclip -selection clipboard -o -t text/uri-list 2>/dev/null", "r");
    if (!f) return false;
    bool ok = fgets(path, maxLen, f) != NULL;
    pclose(f);
    if (ok) {
        size_t len = strlen(path);
        while (len > 0 && (path[len-1] == '\n' || path[len-1] == '\r'))
            path[--len] = '\0';
        if (strncmp(path, "file://", 7) == 0)
            memmove(path, path + 7, len - 6);
    }
    return ok && path[0] != '\0';
}

bool ClipboardPlatform_GetText(char* text, int maxLen) {
    FILE* f = popen("xclip -selection clipboard -o 2>/dev/null", "r");
    if (!f) return false;
    bool ok = fgets(text, maxLen, f) != NULL;
    pclose(f);
    if (ok) {
        size_t len = strlen(text);
        while (len > 0 && (text[len-1] == '\n' || text[len-1] == '\r'))
            text[--len] = '\0';
    }
    return ok && text[0] != '\0';
}

bool ClipboardPlatform_SetImage(int w, int h, const void* rgba) {
    (void)w; (void)h; (void)rgba;
    return false;
}

bool ClipboardPlatform_SetFilePath(const char* path) {
    (void)path;
    return false;
}
