/*
 * RaylibUtils.c — raylib utilities: shader #include preprocessor.
 */
#include "RaylibUtils.h"
#include <stdlib.h>
#include <string.h>
#include <stdio.h>

#define MAX_INCLUDE_DEPTH 16

/* ── Helpers ── */

/* Extract the directory component from filePath into buf (max sz).
   Includes trailing '/' or '\\'. Returns buf, or "" on failure. */
static const char* _getDir(const char* path, char* buf, int sz) {
    if (!path || !path[0]) { buf[0] = '\0'; return buf; }
    const char* s = strrchr(path, '/');
    const char* bs = strrchr(path, '\\');
    if (bs > s) s = bs;
    if (s) {
        int len = (int)(s - path) + 1;
        if (len < sz) {
            memcpy(buf, path, (size_t)len);
            buf[len] = '\0';
            return buf;
        }
    }
    buf[0] = '\0';
    return buf;
}

/* Recursively preprocess a shader file, resolving #include directives.
   Returns a heap-allocated string; caller must free(). */
static char* _preprocessFile(const char* filePath, int depth) {
    if (depth > MAX_INCLUDE_DEPTH) {
        TraceLog(LOG_WARNING,
                 "RaylibUtils: max include depth (%d) exceeded for '%s'",
                 MAX_INCLUDE_DEPTH, filePath);
        return NULL;
    }

    char* src = LoadFileText(filePath);
    if (!src) {
        TraceLog(LOG_WARNING, "RaylibUtils: cannot read '%s'", filePath);
        return NULL;
    }

    char baseDir[1024];
    _getDir(filePath, baseDir, sizeof(baseDir));

    size_t cap = strlen(src) * 2 + 2048;
    char* result = (char*)malloc(cap);
    if (!result) { UnloadFileText(src); return NULL; }
    result[0] = '\0';
    size_t pos = 0;

    char* p = src;
    while (p && *p) {
        char* nl = strchr(p, '\n');
        size_t lineLen = nl ? (size_t)(nl - p) : strlen(p);

        /* #include at column 0 */
        if (lineLen > 9 && p[0] == '#' && strncmp(p, "#include ", 9) == 0) {
            const char* fname = p + 9;
            while (*fname == ' ') fname++;

            char incName[1024];
            int incLen = 0;

            if (*fname == '"') {
                fname++;
                const char* endq = strchr(fname, '"');
                if (endq) {
                    incLen = (int)(endq - fname);
                    if (incLen >= (int)sizeof(incName)) incLen = (int)sizeof(incName) - 1;
                    memcpy(incName, fname, (size_t)incLen);
                    incName[incLen] = '\0';
                }
            } else {
                int maxLen = (int)(lineLen - (fname - p));
                if (maxLen > (int)sizeof(incName) - 1) maxLen = (int)sizeof(incName) - 1;
                while (maxLen > 0 && (fname[maxLen-1] == ' ' || fname[maxLen-1] == '\r'))
                    maxLen--;
                memcpy(incName, fname, (size_t)maxLen);
                incName[maxLen] = '\0';
            }

            if (incName[0]) {
                char incPath[2048];
                snprintf(incPath, sizeof(incPath), "%s%s", baseDir, incName);
                char* incContent = _preprocessFile(incPath, depth + 1);
                if (incContent) {
                    size_t incLen = strlen(incContent);
                    if (pos + incLen + 1 > cap) {
                        cap = pos + incLen + 2048;
                        result = (char*)realloc(result, cap);
                    }
                    memcpy(result + pos, incContent, incLen);
                    pos += incLen;
                    free(incContent);
                }
                /* If include failed, line is silently skipped (error already logged) */
            }
        } else {
            size_t copyLen = nl ? lineLen + 1 : lineLen;
            if (pos + copyLen + 1 > cap) {
                cap = pos + copyLen + 2048;
                result = (char*)realloc(result, cap);
            }
            memcpy(result + pos, p, copyLen);
            pos += copyLen;
        }

        p = nl ? nl + 1 : NULL;
    }

    result[pos] = '\0';
    UnloadFileText(src);
    return result;
}

/* ── Public API ── */

Shader LoadShaderWithIncludes(const char *vsFileName, const char *fsFileName) {
    char *vsCode = NULL, *fsCode = NULL;

    if (vsFileName && vsFileName[0]) {
        vsCode = _preprocessFile(vsFileName, 0);
        if (!vsCode)
            TraceLog(LOG_WARNING, "RaylibUtils: failed to preprocess VS '%s'", vsFileName);
    }
    if (fsFileName && fsFileName[0]) {
        fsCode = _preprocessFile(fsFileName, 0);
        if (!fsCode)
            TraceLog(LOG_WARNING, "RaylibUtils: failed to preprocess FS '%s'", fsFileName);
    }

    Shader shader = LoadShaderFromMemory(vsCode, fsCode);

    if (vsCode) free(vsCode);
    if (fsCode) free(fsCode);

    return shader;
}
