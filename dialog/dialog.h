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

typedef struct {
    int type;  // 0=none, 1=open, 2=saveas, 3=yesno — public read-only
    DialogCallback _callback;
    char _currentDir[DIALOG_PATH_MAX];
    char _filter[64];
    FilePathList _files;
    int _scrollOffset;
    int _leftScrollOffset;
    int _selectedIndex;
    bool _dirDirty;
    char _textInput[DIALOG_PATH_MAX];
    int _cursorPos;
    int _textLen;
    bool _textActive;
    char _message[512];
    char _title[128];
    Rectangle _bounds;
    bool _prevMouseDown;
    bool _leftPrevMouseDown;
    double _lastClickTime;
    int _lastClickedIdx;
    bool _scrollGrabbed;
    int _scrollGrabY;
    int _scrollStartOff;
    bool _leftScrollGrabbed;
    int _leftScrollGrabY;
    int _leftScrollStartOff;
    Font _font;
    int _fontSize;
    char _pathPreview[DIALOG_PATH_MAX];
    bool _confirmOverwrite;
    char _overwritePath[DIALOG_PATH_MAX];
    int _sortColumn;   // 0=name, 1=date
    int _sortDesc;     // 0=asc, 1=desc
    char _filterBuf[256];
    int _filterLen;
    bool _filterActive;
} DialogState;

void DialogOpen_Init(DialogState* dlg, const char* title,
                     const char* filter, DialogCallback cb);
void DialogSaveAs_Init(DialogState* dlg, const char* title,
                       const char* filter, const char* defaultName,
                       DialogCallback cb);
void DialogYesNo_Init(DialogState* dlg, const char* message,
                      DialogCallback cb);
void DialogSetFont(DialogState* dlg, Font font, int defaultSize);
void Dialog_Draw(DialogState* dlg);
void Dialog_MakeDir(const char* path);

#ifdef __cplusplus
}
#endif
