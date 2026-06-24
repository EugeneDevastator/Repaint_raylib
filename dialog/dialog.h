#pragma once
/*
 * dialog.h - C-compatible C++ code.
 * Compiles under both C and C++ with no changes.
 *
 * Simple modal dialog library using raylib 6.0 only.
 * Provides: Open, SaveAs, YesNo dialogs with callback pattern.
 */

#include "raylib.h"

#ifdef __cplusplus
extern "C" {
#endif

#define DIALOG_PATH_MAX 1024

typedef struct {
    bool wasClosed;
    bool success;
    char output[DIALOG_PATH_MAX];
} DialogResult;

typedef void (*DialogCallback)(DialogResult result);

void DialogOpen_Init(const char* title, const char* filter,
                     const char* startDir, DialogCallback cb);
void DialogSaveAs_Init(const char* title, const char* filter,
                       const char* defaultName, const char* startDir,
                       DialogCallback cb);
void DialogYesNo_Init(const char* message, DialogCallback cb);
void DialogButtonChoice_Init(const char* title, const char* message,
                              DialogCallback cb, const char* btn1, ...);
void DialogSetFont(Font font, int defaultSize);
void Dialog_Draw(void);
bool Dialog_IsActive(void);

#ifdef __cplusplus
}
#endif
