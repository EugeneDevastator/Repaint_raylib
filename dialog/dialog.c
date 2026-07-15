/*
 * dialog.c - raylib modal dialogs: Open, SaveAs, YesNo
 */
#include "dialog.h"
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <time.h>
#include <stdarg.h>
#include <math.h>

typedef struct {
    int type;
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
    int _sortColumn;
    int _sortDesc;
    char _filterBuf[256];
    int _filterLen;
    bool _filterActive;
    int   repeatKey;
    float repeatTimer;
    int _btnCount;
    char _btnLabels[8][64];
    Texture2D _previewTex;
    char _previewPath[DIALOG_PATH_MAX];
} DialogState;

static DialogState g_dlg = {0};

#define ITEM_H      44
#define TITLE_H     40
#define DIRBAR_H    36
#define PATHBAR_H   32
#define BOTTOM_H    60
#define PAD          6
#define BTN_W      100
#define BTN_H       34
#define SCROLL_W    12
#define LEFT_PANE  260
#define DBLCK_TIME   0.3
#define PREVIEW_W  180

static Color _overlay = {  0,  0,  0,120};
static Color _winBg   = {240,240,240,255};
static Color _titleBg = { 60, 80,160,255};
static Color _selBg   = {200,210,240,255};
static Color _hovBg   = {220,225,240,255};
static Color _btnBg   = {225,225,225,255};
static Color _btnHov  = {240,240,240,255};
static Color _btnPrs  = {200,200,200,255};
static Color _border  = {160,160,160,255};
static Color _inpBg   = {255,255,255,255};
static Color _inpBd   = {180,180,180,255};
static Color _scTrack = {200,200,200,255};
static Color _scThumb = {150,150,150,255};
static Color _text    = { 20, 20, 30,255};
static Color _textDim = {120,120,120,255};
static Color _white   = {255,255,255,255};

static Font _persistFont     = {0};
static int  _persistFontSize = 26;

/* ── Helpers ── */

static void _trimSlash(char* p) {
    size_t n = strlen(p);
    while (n > 0 && (p[n-1] == '/' || p[n-1] == '\\')) p[--n] = '\0';
}

static int _sortCol = 0;   // set per-sort by loadDir
static int _sortDesc = 0;

static int _sortPath(const void* a, const void* b) {
    const char* pa = *(const char**)a;
    const char* pb = *(const char**)b;
    int da = DirectoryExists(pa), db = DirectoryExists(pb);
    if (da != db) return _sortDesc ? da - db : db - da;

    int r;
    if (_sortCol == 1) {
        long ta = GetFileModTime(pa);
        long tb = GetFileModTime(pb);
        r = (ta > tb) - (ta < tb);
    } else
        r = strcmp(GetFileName(pa), GetFileName(pb));
    return _sortDesc ? -r : r;
}

static bool _matchExt(const char* path, const char* filter) {
    if (!filter || !filter[0]) return true;
    const char* name = GetFileName(path);
    size_t nl = strlen(name);
    // Handle multi-extension filters separated by '/'
    const char* sep = filter;
    while (*sep) {
        const char* end = sep;
        while (*end && *end != '/') end++;
        size_t fl = (size_t)(end - sep);
        if (fl > 0 && nl >= fl && strcmp(name + nl - fl, sep) == 0)
            return true;
        sep = *end ? end + 1 : end;
    }
    return false;
}

static bool _matchFilter(const char* path, const char* filter) {
    if (!filter || !filter[0]) return true;
    const char* name = GetFileName(path);
    return strstr(name, filter) != NULL;
}

static bool _itemVisible(int i) {
    if (DirectoryExists(g_dlg._files.paths[i])) return false;
    if (g_dlg._filter[0] == '.' && g_dlg.type == 1  // open dialog: filter by extension
        && !_matchExt(g_dlg._files.paths[i], g_dlg._filter))
        return false;
    // Text input IS the filter — no separate extension matching
    return _matchFilter(g_dlg._files.paths[i], g_dlg._filterBuf);
}

static void _visPath(int idx, char* out, int sz) {
    int v = 0;
    for (int i = 0; i < (int)g_dlg._files.count; i++) {
        if (!_itemVisible(i)) continue;
        if (v == idx) { snprintf(out, sz, "%s", g_dlg._files.paths[i]); return; }
        v++;
    }
    out[0] = '\0';
}

static int _visCount(void) {
    int c = 0;
    for (int i = 0; i < (int)g_dlg._files.count; i++)
        if (_itemVisible(i)) c++;
    return c;
}

static void _unloadPreview(void) {
    if (g_dlg._previewTex.id > 0) { UnloadTexture(g_dlg._previewTex); g_dlg._previewTex = (Texture2D){0}; }
    g_dlg._previewPath[0] = '\0';
}

static void _loadPreview(const char* path) {
    _unloadPreview();
    if (!path || !path[0]) return;
    const char* ext = GetFileExtension(path);
    if (!ext || strcmp(ext, ".png") != 0) return;

    Image img = LoadImage(path);
    if (!img.data) return;

    int maxH = 300;
    float scale = fminf(PREVIEW_W / (float)img.width, maxH / (float)img.height);
    if (scale < 1.0f) {
        int nw = (int)(img.width * scale);
        int nh = (int)(img.height * scale);
        if (nw < 1) nw = 1; if (nh < 1) nh = 1;
        ImageResize(&img, nw, nh);
    }

    g_dlg._previewTex = LoadTextureFromImage(img);
    UnloadImage(img);
    g_dlg._previewPath[0] = '\0';
}

static void _loadDir(void) {
    _unloadPreview();
    if (g_dlg._files.paths) { UnloadDirectoryFiles(g_dlg._files); g_dlg._files.paths = NULL; }
    g_dlg._files = LoadDirectoryFilesEx(g_dlg._currentDir, "*.*", false);
    _sortCol = g_dlg._sortColumn;
    _sortDesc = g_dlg._sortDesc;
    if (g_dlg._files.count > 1)
        qsort(g_dlg._files.paths, g_dlg._files.count, sizeof(char*), _sortPath);
    g_dlg._scrollOffset = g_dlg._leftScrollOffset = 0;
    g_dlg._selectedIndex = -1;
    snprintf(g_dlg._pathPreview, DIALOG_PATH_MAX, "%s", g_dlg._currentDir);
}


static void _navUp(void) {
    const char* p = GetPrevDirectoryPath(g_dlg._currentDir);
    if (p && p[0]) { snprintf(g_dlg._currentDir, DIALOG_PATH_MAX, "%s", p); _trimSlash(g_dlg._currentDir); _loadDir(); }
}

static void _navInto(const char* path) {
    if (DirectoryExists(path)) { snprintf(g_dlg._currentDir, DIALOG_PATH_MAX, "%s", path); _trimSlash(g_dlg._currentDir); _loadDir(); }
}

static void _initDir(void) {
    char app[DIALOG_PATH_MAX];
    snprintf(app, sizeof(app), "%s", GetApplicationDirectory());
    _trimSlash(app);

    /* Build Saves/ path safely without triggering truncation warnings */
    char saves[DIALOG_PATH_MAX + 8];
    saves[0] = '\0';
    size_t al = strlen(app);
    if (al + 7 <= sizeof(saves)) {
        memcpy(saves, app, al);
        memcpy(saves + al, "/Saves", 7);
    }

    const char* dir = (app[0] && DirectoryExists(saves)) ? saves : app;
    size_t dl = strlen(dir);
    if (dl >= DIALOG_PATH_MAX) dl = DIALOG_PATH_MAX - 1;
    memcpy(g_dlg._currentDir, dir, dl);
    g_dlg._currentDir[dl] = '\0';
    _loadDir();
}

/* ── Draw helpers ── */

/* spacing=2 for readability; raised from 0 for default font */
static float _sp(Font f) {
    return 2.0f;
}


static void _drawText(Font f, const char* t, int x, int y, int sz, Color c) {
    DrawTextEx(f, t, (Vector2){(float)x,(float)y}, (float)sz, _sp(f), c);
}

static float _measureText(Font f, const char* t, int sz) {
    return MeasureTextEx(f, t, (float)sz, _sp(f)).x;
}

static DialogResult _makeResult(void) {
    DialogResult r; memset(&r, 0, sizeof(r)); r.wasClosed = true; return r;
}

static void _closeOk(const char* path) {
    _unloadPreview();
    DialogResult r = _makeResult(); r.success = true;
    snprintf(r.output, DIALOG_PATH_MAX, "%s", path);
    g_dlg.type = 0;
    if (g_dlg._callback) g_dlg._callback(r);
}

static void _closeCancel(void) {
    _unloadPreview();
    DialogResult r = _makeResult(); g_dlg.type = 0;
    if (g_dlg._callback) g_dlg._callback(r);
}

static int _btn(Rectangle r, const char* label) {
    Vector2 mp = GetMousePosition();
    bool hov = CheckCollisionPointRec(mp, r);
    bool dn  = hov && IsMouseButtonDown(MOUSE_LEFT_BUTTON);
    DrawRectangleRec(r, dn ? _btnPrs : hov ? _btnHov : _btnBg);
    DrawRectangleLinesEx(r, 1, _border);
    float tw = _measureText(g_dlg._font, label, g_dlg._fontSize);
    _drawText(g_dlg._font, label,
              (int)(r.x + (r.width  - tw)          / 2),
              (int)(r.y + (r.height - g_dlg._fontSize) / 2),
              g_dlg._fontSize, _text);
    return hov && IsMouseButtonReleased(MOUSE_LEFT_BUTTON);
}

static void _scrollbar(int x, int y, int h, int total, int view,
                       int* off, bool* grab, int* grabY, int* startOff, bool pressed) {
    if (total <= view) return;
    float th = (float)view / total * h; if (th < 20) th = 20;
    float maxOff = (float)(total - view);
    float t  = maxOff > 0 ? (float)(*off) / maxOff : 0;
    float ty = y + t * (h - th);
    DrawRectangle(x, y, SCROLL_W, h, _scTrack);
    DrawRectangle(x, (int)ty, SCROLL_W, (int)th, _scThumb);
    Vector2 mp = GetMousePosition();
    bool md = IsMouseButtonDown(MOUSE_LEFT_BUTTON);
    if (!(*grab)) {
        if (pressed && mp.x >= x && mp.x < x+SCROLL_W && mp.y >= ty && mp.y < ty+th)
            { *grab=true; *grabY=(int)mp.y; *startOff=*off; }
    } else {
        if (!md) *grab = false;
        else {
            float dy = (float)(mp.y - *grabY) / (h - th) * maxOff;
            *off = *startOff + (int)dy;
            if (*off < 0) *off = 0;
            if (*off > (int)maxOff) *off = (int)maxOff;
        }
    }
}
static void _handleKey(char* buf, int* cur, int* len, int k) {
    bool ctrl = IsKeyDown(KEY_LEFT_CONTROL) || IsKeyDown(KEY_RIGHT_CONTROL);
    if (k == KEY_BACKSPACE && *cur > 0) {
        if (ctrl) {
            int end = *cur;
            while (*cur > 0 && buf[*cur-1] == ' ') (*cur)--;
            while (*cur > 0 && buf[*cur-1] != ' ') (*cur)--;
            int removed = end - *cur;
            memmove(buf + *cur, buf + end, *len - end + 1);
            *len -= removed;
        } else {
            memmove(buf + *cur - 1, buf + *cur, *len - *cur + 1);
            (*cur)--; (*len)--;
        }
    } else if (k == KEY_DELETE && *cur < *len) {
        memmove(buf + *cur, buf + *cur + 1, *len - *cur);
        (*len)--;
    } else if (k == KEY_LEFT  && *cur > 0)   (*cur)--;
    else if (k == KEY_RIGHT && *cur < *len)   (*cur)++;
    else if (k == KEY_HOME)                   *cur = 0;
    else if (k == KEY_END)                    *cur = *len;
}

static void _textField(Rectangle r, char* buf, int maxLen,
                       int* cur, int* len, bool active) {
    DrawRectangleRec(r, _inpBg);
    DrawRectangleLinesEx(r, 1, active ? _titleBg : _inpBd);

    if (active) {
        // chars
        int c;
        while ((c = GetCharPressed()) > 0)
            if (c >= 32 && c < 127 && *len < maxLen - 1) {
                memmove(buf + *cur + 1, buf + *cur, *len - *cur + 1);
                buf[(*cur)++] = (char)c;
                (*len)++;
            }

        // keys with repeat
        static const int keys[] = {
            KEY_BACKSPACE, KEY_DELETE,
            KEY_LEFT, KEY_RIGHT,
            KEY_HOME, KEY_END
        };
        for (int i = 0; i < 6; i++) {
            if (IsKeyPressed(keys[i])) {
                _handleKey(buf, cur, len, keys[i]);
                g_dlg.repeatKey   = keys[i];
                g_dlg.repeatTimer = 0.5f;
            }
        }
        if (g_dlg.repeatKey && IsKeyDown(g_dlg.repeatKey)) {
            g_dlg.repeatTimer -= GetFrameTime();
            if (g_dlg.repeatTimer <= 0.0f) {
                _handleKey(buf, cur, len, g_dlg.repeatKey);
                g_dlg.repeatTimer = 0.05f;
            }
        } else {
            g_dlg.repeatKey = 0;
        }
    }

    int tx = (int)r.x + 6;
    int ty = (int)r.y + ((int)r.height - g_dlg._fontSize) / 2;
    _drawText(g_dlg._font, buf, tx, ty, g_dlg._fontSize, _text);

    if (active && (int)(GetTime() * 2) % 2 == 0) {
        char tmp[DIALOG_PATH_MAX];
        snprintf(tmp, sizeof(tmp), "%.*s", *cur, buf);
        DrawRectangle(tx + (int)_measureText(g_dlg._font, tmp, g_dlg._fontSize),
                      ty, 1, g_dlg._fontSize, _text);
    }
}

/* ── Left pane: folders only ── */

static void _drawLeftPane(Rectangle a) {
    Vector2 mp = GetMousePosition();
    bool md = IsMouseButtonDown(MOUSE_LEFT_BUTTON);
    bool pressed = md && !g_dlg._leftPrevMouseDown;
    g_dlg._leftPrevMouseDown = md;

    int dirs = 0;
    for (int i = 0; i < (int)g_dlg._files.count; i++)
        if (DirectoryExists(g_dlg._files.paths[i])) dirs++;
    int totalH = (1 + dirs) * ITEM_H;
    int viewH = (int)a.height;
    int maxOff = totalH > viewH ? totalH - viewH : 0;

    float wheel = GetMouseWheelMove();
    if (wheel && CheckCollisionPointRec(mp, a)) {
        g_dlg._leftScrollOffset -= (int)(wheel * ITEM_H * 3);
        if (g_dlg._leftScrollOffset < 0) g_dlg._leftScrollOffset = 0;
        if (g_dlg._leftScrollOffset > maxOff) g_dlg._leftScrollOffset = maxOff;
    }

    BeginScissorMode((int)a.x, (int)a.y, (int)a.width, viewH);
    int y0 = (int)a.y - g_dlg._leftScrollOffset;

    /* [..] row */
    Rectangle ir = {a.x, (float)y0, a.width - SCROLL_W, ITEM_H};
    if (CheckCollisionPointRec(mp, ir)) DrawRectangleRec(ir, _hovBg);
    _drawText(g_dlg._font, "[..]", (int)a.x+6, y0+(ITEM_H-g_dlg._fontSize)/2, g_dlg._fontSize, _textDim);
    if (pressed && CheckCollisionPointRec(mp, ir)) { EndScissorMode(); _navUp(); return; }

    /* Directory rows */
    int di = 0;
    for (int i = 0; i < (int)g_dlg._files.count; i++) {
        if (!DirectoryExists(g_dlg._files.paths[i])) continue;
        int y = y0 + (1 + di) * ITEM_H; di++;
        if (y + ITEM_H <= (int)a.y || y >= (int)(a.y + viewH)) continue;
        Rectangle ir2 = {a.x, (float)y, a.width - SCROLL_W, ITEM_H};
        if (CheckCollisionPointRec(mp, ir2)) DrawRectangleRec(ir2, _hovBg);
        _drawText(g_dlg._font, GetFileName(g_dlg._files.paths[i]),
                  (int)a.x+6, y+(ITEM_H-g_dlg._fontSize)/2, g_dlg._fontSize, _text);
        if (pressed && CheckCollisionPointRec(mp, ir2)) { EndScissorMode(); _navInto(g_dlg._files.paths[i]); return; }
    }
    EndScissorMode();

    _scrollbar((int)(a.x + a.width - SCROLL_W), (int)a.y, viewH, totalH, viewH,
               &g_dlg._leftScrollOffset, &g_dlg._leftScrollGrabbed,
               &g_dlg._leftScrollGrabY, &g_dlg._leftScrollStartOff, pressed);
}

/* ── Right pane: files only ── */

static void _fmtDate(long ts_raw, char* out, int sz) {
    time_t ts = (time_t)ts_raw;
    if (ts <= 0) { snprintf(out, sz, "---"); return; }
    struct tm* t = localtime(&ts);
    if (t)
        snprintf(out, sz, "%04d-%02d-%02d %02d:%02d",
                 t->tm_year + 1900, t->tm_mon + 1, t->tm_mday,
                 t->tm_hour, t->tm_min);
    else
        snprintf(out, sz, "---");
}

static void _drawRightPane(Rectangle a) {
    Vector2 mp = GetMousePosition();
    bool md = IsMouseButtonDown(MOUSE_LEFT_BUTTON);
    bool pressed = md && !g_dlg._prevMouseDown;
    g_dlg._prevMouseDown = md;

    /* ── Filter text field (same height as file entries) ── */
    Rectangle fr = {a.x + 2, a.y + 2, a.width - SCROLL_W - 4, ITEM_H};
    bool filterHov = CheckCollisionPointRec(mp, fr);
    if (g_dlg.type == 1) g_dlg._filterActive = true;  // open dialog: always capture
    if (g_dlg._filterActive && pressed && !filterHov && !g_dlg._textActive) {
        // clicking elsewhere keeps it active, but Escape or Tab deactivates
        g_dlg._filterActive = (g_dlg.type == 2);
    }
    if (g_dlg._filterActive) {
        int c;
        while ((c = GetCharPressed()) > 0) {
            if (c >= 32 && c < 127 && g_dlg._filterLen < 255) {
                g_dlg._filterBuf[g_dlg._filterLen++] = (char)c;
                g_dlg._filterBuf[g_dlg._filterLen] = '\0';
                g_dlg._scrollOffset = 0;
                g_dlg._selectedIndex = -1;
            }
        }
        if (IsKeyPressed(KEY_BACKSPACE) && g_dlg._filterLen > 0) {
            g_dlg._filterBuf[--g_dlg._filterLen] = '\0';
            g_dlg._scrollOffset = 0;
            g_dlg._selectedIndex = -1;
        }
        if (IsKeyPressed(KEY_ESCAPE)) { g_dlg._filterActive = false; g_dlg._filterBuf[0] = '\0'; g_dlg._filterLen = 0; }
    }
    DrawRectangleRec(fr, _white);
    DrawRectangleLinesEx(fr, 1, _titleBg);
    int fty = (int)fr.y + ((int)fr.height - g_dlg._fontSize) / 2;
    if (g_dlg._filterLen > 0)
        _drawText(g_dlg._font, g_dlg._filterBuf, (int)fr.x + 4, fty, g_dlg._fontSize, _text);
    else
        _drawText(g_dlg._font, "filter...", (int)fr.x + 4, fty, g_dlg._fontSize, _textDim);

    /* ── Column headers ── */
    int headY = (int)(a.y + fr.height + 4);
    int nameW = (int)(a.width - SCROLL_W) * 3 / 5;
    int dateW = (int)(a.width - SCROLL_W) - nameW;
    Rectangle nameH = {a.x + 2, (float)headY, (float)nameW, ITEM_H};
    Rectangle dateH = {a.x + 2 + nameW, (float)headY, (float)dateW, ITEM_H};

    if (pressed && CheckCollisionPointRec(mp, nameH)) {
        if (g_dlg._sortColumn == 0) g_dlg._sortDesc = !g_dlg._sortDesc;
        else { g_dlg._sortColumn = 0; g_dlg._sortDesc = 0; }
        _loadDir(); return;
    }
    if (pressed && CheckCollisionPointRec(mp, dateH)) {
        if (g_dlg._sortColumn == 1) g_dlg._sortDesc = !g_dlg._sortDesc;
        else { g_dlg._sortColumn = 1; g_dlg._sortDesc = 0; }
        _loadDir(); return;
    }

    DrawRectangleRec(nameH, _hovBg);
    DrawRectangleRec(dateH, _hovBg);
    DrawLine((int)nameH.x, (int)(nameH.y + nameH.height), (int)(nameH.x + nameH.width) - 1, (int)(nameH.y + nameH.height), _border);
    DrawLine((int)dateH.x, (int)(dateH.y + dateH.height), (int)(dateH.x + dateH.width) - 1, (int)(dateH.y + dateH.height), _border);
    DrawLine((int)(nameH.x + nameH.width), (int)nameH.y, (int)(nameH.x + nameH.width), (int)(nameH.y + nameH.height), _border);

    char nameLabel[48];
    snprintf(nameLabel, sizeof(nameLabel), "Name%s", (g_dlg._sortColumn == 0) ? (g_dlg._sortDesc ? " \xe2\x96\xb4" : " \xe2\x96\xbe") : "");
    _drawText(g_dlg._font, nameLabel, (int)nameH.x + 4, (int)nameH.y + ((int)nameH.height - g_dlg._fontSize) / 2, g_dlg._fontSize, _text);

    char dateLabel[48];
    snprintf(dateLabel, sizeof(dateLabel), "Date%s", (g_dlg._sortColumn == 1) ? (g_dlg._sortDesc ? " \xe2\x96\xb4" : " \xe2\x96\xbe") : "");
    _drawText(g_dlg._font, dateLabel, (int)dateH.x + 4, (int)dateH.y + ((int)dateH.height - g_dlg._fontSize) / 2, g_dlg._fontSize, _text);

    /* ── File list area ── */
    int listY = (int)(headY + nameH.height + 2);
    int listH = (int)(a.y + a.height - listY);
    int totalH = _visCount() * ITEM_H;
    int viewH = listH;
    int maxOff = totalH > viewH ? totalH - viewH : 0;

    float wheel = GetMouseWheelMove();
    if (wheel && CheckCollisionPointRec(mp, (Rectangle){a.x, (float)listY, a.width - SCROLL_W, (float)listH})) {
        g_dlg._scrollOffset -= (int)(wheel * ITEM_H * 3);
        if (g_dlg._scrollOffset < 0) g_dlg._scrollOffset = 0;
        if (g_dlg._scrollOffset > maxOff) g_dlg._scrollOffset = maxOff;
    }

    /* Keyboard navigation */
    int tv = _visCount();
    if (tv > 0) {
        if (IsKeyPressed(KEY_DOWN) && g_dlg._selectedIndex < tv-1) {
            g_dlg._selectedIndex++;
            _visPath(g_dlg._selectedIndex, g_dlg._pathPreview, DIALOG_PATH_MAX);
            _loadPreview(g_dlg._pathPreview);
            int sy = g_dlg._selectedIndex * ITEM_H - g_dlg._scrollOffset;
            if (sy + ITEM_H > viewH) g_dlg._scrollOffset = g_dlg._selectedIndex * ITEM_H - viewH + ITEM_H;
        }
        if (IsKeyPressed(KEY_UP) && g_dlg._selectedIndex > 0) {
            g_dlg._selectedIndex--;
            _visPath(g_dlg._selectedIndex, g_dlg._pathPreview, DIALOG_PATH_MAX);
            _loadPreview(g_dlg._pathPreview);
            int sy = g_dlg._selectedIndex * ITEM_H - g_dlg._scrollOffset;
            if (sy < 0) g_dlg._scrollOffset = g_dlg._selectedIndex * ITEM_H;
        }
    }

    BeginScissorMode((int)a.x, listY, (int)(a.width - SCROLL_W), listH);
    int y0 = listY - g_dlg._scrollOffset;
    int vi = 0;

    for (int i = 0; i < (int)g_dlg._files.count; i++) {
        if (!_itemVisible(i)) continue;
        int y = y0 + vi * ITEM_H;
        if (y + ITEM_H > listY && y < listY + listH) {
            Rectangle ir = {a.x, (float)y, a.width - SCROLL_W, ITEM_H};
            bool hov = CheckCollisionPointRec(mp, ir);
            bool sel = (vi == g_dlg._selectedIndex);
            if (sel) DrawRectangleRec(ir, _selBg);
            else if (hov) DrawRectangleRec(ir, _hovBg);

            /* Name column */
            char dateStr[48];
            _fmtDate(GetFileModTime(g_dlg._files.paths[i]), dateStr, sizeof(dateStr));
            _drawText(g_dlg._font, GetFileName(g_dlg._files.paths[i]),
                      (int)a.x + 6, y + (ITEM_H - g_dlg._fontSize) / 2, g_dlg._fontSize, _text);
            _drawText(g_dlg._font, dateStr,
                      (int)(a.x + nameW + 2), y + (ITEM_H - g_dlg._fontSize) / 2, g_dlg._fontSize, _textDim);

            if (pressed && hov) {
                double now = GetTime();
                bool dbl = (now - g_dlg._lastClickTime < DBLCK_TIME && g_dlg._lastClickedIdx == vi);
                g_dlg._lastClickTime = now; g_dlg._lastClickedIdx = vi;
                if (dbl) {
                    char path[DIALOG_PATH_MAX];
                    _visPath(vi, path, DIALOG_PATH_MAX);
                    EndScissorMode(); _closeOk(path); return;
                }
                g_dlg._selectedIndex = vi;
                _visPath(vi, g_dlg._pathPreview, DIALOG_PATH_MAX);
                _loadPreview(g_dlg._pathPreview);
                if (g_dlg.type == 2 && g_dlg._pathPreview[0]) {
                    snprintf(g_dlg._textInput, DIALOG_PATH_MAX, "%s",
                             GetFileName(g_dlg._pathPreview));
                    g_dlg._textLen = (int)strlen(g_dlg._textInput);
                    g_dlg._cursorPos = g_dlg._textLen; g_dlg._textActive = true;
                }
            }
        }
        vi++;
    }
    EndScissorMode();

    _scrollbar((int)(a.x + a.width - SCROLL_W), listY, viewH, totalH, viewH,
               &g_dlg._scrollOffset, &g_dlg._scrollGrabbed,
               &g_dlg._scrollGrabY, &g_dlg._scrollStartOff, pressed);
}

/* ── File dialog ── */

static void _drawFileDialog(void) {
    int sw = GetScreenWidth(), sh = GetScreenHeight();
    int winW = sw*8/10, winH = sh*8/10;
    int wx = (sw - winW)/2, wy = (sh - winH)/2;
    Rectangle win = {(float)wx, (float)wy, (float)winW, (float)winH};
    g_dlg._bounds = win;

    DrawRectangleRec(win, _winBg);
    DrawRectangleLinesEx(win, 1, _border);
    DrawRectangle(wx, wy, winW, TITLE_H, _titleBg);
    _drawText(g_dlg._font, g_dlg._title, wx+PAD, wy+(TITLE_H-g_dlg._fontSize)/2, g_dlg._fontSize, _white);

    Rectangle xR = {win.x + win.width - TITLE_H, win.y, TITLE_H, TITLE_H};
    if (_btn(xR, "X")) { _closeCancel(); return; }

    /* Overwrite confirm overlay */
    if (g_dlg._confirmOverwrite) {
        Rectangle ov = {win.x+1, (float)(wy+TITLE_H), win.width-2, win.height-TITLE_H-2};
        DrawRectangleRec(ov, (Color){232,232,232,240});
        int bw=360, bh=120, bx=wx+(winW-bw)/2, by=wy+(winH-bh)/2-10;
        Rectangle box = {(float)bx, (float)by, (float)bw, (float)bh};
        DrawRectangleRec(box, _winBg); DrawRectangleLinesEx(box, 1, _border);
        char msg[512];
        snprintf(msg, sizeof(msg), "Overwrite \"%s\"?", GetFileName(g_dlg._overwritePath));
        float tw = _measureText(g_dlg._font, msg, g_dlg._fontSize);
        _drawText(g_dlg._font, msg, bx+(int)(bw-tw)/2, by+12, g_dlg._fontSize, _text);
        int btnY = by + bh - BTN_H - 10;
        Rectangle yR = {(float)(bx+bw/2-BTN_W-6), (float)btnY, BTN_W, BTN_H};
        Rectangle nR = {(float)(bx+bw/2+6),        (float)btnY, BTN_W, BTN_H};
        if (_btn(yR, "Yes") || IsKeyPressed(KEY_ENTER)) {
            g_dlg._confirmOverwrite = false;
            _closeOk(g_dlg._overwritePath);
        } else if (_btn(nR, "No") || IsKeyPressed(KEY_ESCAPE))
            g_dlg._confirmOverwrite = false;
        return;
    }

    /* Dir bar */
    int dirY = wy + TITLE_H;
    Rectangle upR = {(float)wx+PAD, (float)dirY+(DIRBAR_H-BTN_H)/2, 38, BTN_H};
    if (_btn(upR, "^")) { _navUp(); return; }
    int dtx = (int)upR.x + (int)upR.width + PAD;
    BeginScissorMode(dtx, dirY, winW-(dtx-wx)-PAD, DIRBAR_H);
    _drawText(g_dlg._font, g_dlg._currentDir, dtx, dirY+(DIRBAR_H-g_dlg._fontSize)/2, g_dlg._fontSize, _textDim);
    EndScissorMode();
    DrawLine(wx, dirY+DIRBAR_H, wx+winW, dirY+DIRBAR_H, _border);

    int contentY = dirY + DIRBAR_H + 2;
    int contentH = winH - (contentY - wy) - PATHBAR_H - BOTTOM_H - 4;

    Rectangle leftR = {(float)wx+2, (float)contentY, (float)LEFT_PANE-2, (float)contentH};
    DrawRectangleRec(leftR, _winBg); DrawRectangleLinesEx(leftR, 1, _border);
    _drawLeftPane((Rectangle){leftR.x+1, leftR.y+1, leftR.width-2, leftR.height-2});
    DrawLine(wx+LEFT_PANE, contentY, wx+LEFT_PANE, contentY+contentH, _border);

    Rectangle rightR = {(float)(wx+LEFT_PANE+1), (float)contentY,
                        (float)(winW-LEFT_PANE-3-PREVIEW_W), (float)contentH};
    _drawRightPane(rightR);

    // Preview area on the right
    float prevX = rightR.x + rightR.width + 1;
    Rectangle prevR = {(float)prevX, (float)contentY, (float)(PREVIEW_W-2), (float)contentH};
    DrawRectangleLinesEx(prevR, 1, _border);
    if (g_dlg._previewTex.id > 0) {
        float psx = (float)g_dlg._previewTex.width;
        float psy = (float)g_dlg._previewTex.height;
        float sc = fminf((prevR.width-4)/psx, (prevR.height-4)/psy);
        float dw = psx * sc, dh = psy * sc;
        float dx = prevR.x + (prevR.width-dw)*0.5f;
        float dy = prevR.y + (prevR.height-dh)*0.5f;
        DrawTexturePro(g_dlg._previewTex,
            (Rectangle){0,0,psx,psy},
            (Rectangle){dx,dy,dw,dh},
            (Vector2){0,0}, 0, WHITE);
    } else if (g_dlg._pathPreview[0]) {
        _drawText(g_dlg._font, "No preview", (int)prevR.x+6, (int)(prevR.y+prevR.height*0.5f-g_dlg._fontSize*0.5f),
                  g_dlg._fontSize, _textDim);
    }

    int pathY = contentY + contentH + 2;
    DrawLine(wx, pathY, wx+winW, pathY, _border);
    BeginScissorMode(wx+PAD, pathY+2, winW-PAD*2, PATHBAR_H-4);
    _drawText(g_dlg._font, g_dlg._pathPreview, wx+PAD,
              pathY+2+(PATHBAR_H-4-g_dlg._fontSize)/2, g_dlg._fontSize, _textDim);
    EndScissorMode();

    int botY = pathY + PATHBAR_H;
    DrawLine(wx, botY, wx+winW, botY, _border);

    if (g_dlg.type == 2) {
        int inpW = winW - (BTN_W*2 + PAD*4);
        Rectangle inpR  = {(float)(wx+PAD),              (float)botY+(BOTTOM_H-BTN_H)/2, (float)inpW, BTN_H};
        Rectangle saveR = {(float)(wx+PAD+inpW+PAD),     (float)botY+(BOTTOM_H-BTN_H)/2, BTN_W, BTN_H};
        Rectangle canR  = {(float)(wx+winW-BTN_W-PAD),   (float)botY+(BOTTOM_H-BTN_H)/2, BTN_W, BTN_H};
        _textField(inpR, g_dlg._textInput, DIALOG_PATH_MAX,
                   &g_dlg._cursorPos, &g_dlg._textLen, g_dlg._textActive);
        if (IsMouseButtonPressed(MOUSE_LEFT_BUTTON))
            g_dlg._textActive = CheckCollisionPointRec(GetMousePosition(), inpR);
        if (_btn(saveR, "Save") || (g_dlg._textActive && IsKeyPressed(KEY_ENTER))) {
            char fname[DIALOG_PATH_MAX];
            snprintf(fname, sizeof(fname), "%s", g_dlg._textInput);
            if (g_dlg._filter[0] == '.' && !_matchExt(fname, g_dlg._filter)) {
                int fl = (int)strlen(fname);
                snprintf(fname+fl, DIALOG_PATH_MAX-fl, "%s", g_dlg._filter);
            }
            if (fname[0]) {
				char full[DIALOG_PATH_MAX * 2 + 1];
				snprintf(full, sizeof(full), "%s/%s", g_dlg._currentDir, fname);
				if (FileExists(full)) {
					strncpy(g_dlg._overwritePath, full, DIALOG_PATH_MAX-1);
					g_dlg._overwritePath[DIALOG_PATH_MAX-1] = '\0';
					g_dlg._confirmOverwrite = true;
				} else if (full[0]) {
					_closeOk(full);
				}
            }
            return;
        }
        if (_btn(canR, "Cancel") || IsKeyPressed(KEY_ESCAPE)) { _closeCancel(); return; }
    } else {
        int bax = winW - (BTN_W*2 + PAD*3);
        Rectangle openR = {(float)(wx+bax),          (float)botY+(BOTTOM_H-BTN_H)/2, BTN_W, BTN_H};
        Rectangle canR  = {(float)(wx+bax+BTN_W+PAD),(float)botY+(BOTTOM_H-BTN_H)/2, BTN_W, BTN_H};
        if (_btn(canR, "Cancel") || IsKeyPressed(KEY_ESCAPE)) { _closeCancel(); return; }
        if (_btn(openR, "Open") || IsKeyPressed(KEY_ENTER)) {
            if (g_dlg._selectedIndex >= 0) {
                char path[DIALOG_PATH_MAX];
                _visPath(g_dlg._selectedIndex, path, DIALOG_PATH_MAX);
                if (path[0] && DirectoryExists(path)) { _navInto(path); return; }
                if (path[0]) { _closeOk(path); return; }
            }
        }
    }
}

/* ── YesNo dialog ── */

static void _drawYesNo(void) {
    int sw = GetScreenWidth(), sh = GetScreenHeight();
    int ww = 440, wh = 180, wx = (sw-ww)/2, wy = (sh-wh)/2;
    Rectangle win = {(float)wx, (float)wy, (float)ww, (float)wh};
    DrawRectangleRec(win, _winBg); DrawRectangleLinesEx(win, 1, _border);
    DrawRectangle(wx, wy, ww, TITLE_H, _titleBg);
    _drawText(g_dlg._font, "Confirm", wx+PAD, wy+(TITLE_H-g_dlg._fontSize)/2, g_dlg._fontSize, _white);
    Rectangle xR = {win.x + win.width - TITLE_H, win.y, TITLE_H, TITLE_H};
    if (_btn(xR, "X")) { _closeCancel(); return; }
    float tw = _measureText(g_dlg._font, g_dlg._message, g_dlg._fontSize);
    _drawText(g_dlg._font, g_dlg._message, wx+(int)(ww-tw)/2, wy+TITLE_H+12, g_dlg._fontSize, _text);
    int btnY = wy + wh - BTN_H - 12;
    Rectangle yR = {(float)(wx+ww/2-BTN_W-6), (float)btnY, BTN_W, BTN_H};
    Rectangle nR = {(float)(wx+ww/2+6),        (float)btnY, BTN_W, BTN_H};
    if (_btn(yR, "Yes") || IsKeyPressed(KEY_ENTER)) {
        DialogResult r = _makeResult(); r.success = true; g_dlg.type = 0;
        if (g_dlg._callback) g_dlg._callback(r);
    } else if (_btn(nR, "No") || IsKeyPressed(KEY_ESCAPE)) {
        _closeCancel();
    }
}

/* ── ButtonChoice dialog ── */

static void _drawButtonChoice(void) {
    int sw = GetScreenWidth(), sh = GetScreenHeight();
    int ww = 440, wh = 180, wx = (sw-ww)/2, wy = (sh-wh)/2;
    Rectangle win = {(float)wx, (float)wy, (float)ww, (float)wh};
    DrawRectangleRec(win, _winBg); DrawRectangleLinesEx(win, 1, _border);
    DrawRectangle(wx, wy, ww, TITLE_H, _titleBg);
    _drawText(g_dlg._font, g_dlg._title, wx+PAD, wy+(TITLE_H-g_dlg._fontSize)/2, g_dlg._fontSize, _white);
    Rectangle xR = {win.x + win.width - TITLE_H, win.y, TITLE_H, TITLE_H};
    if (_btn(xR, "X")) { _closeCancel(); return; }
    float tw = _measureText(g_dlg._font, g_dlg._message, g_dlg._fontSize);
    _drawText(g_dlg._font, g_dlg._message, wx+(int)(ww-tw)/2, wy+TITLE_H+12, g_dlg._fontSize, _text);
    int btnY = wy + wh - BTN_H - 12;
    int btnW = 100;
    int gap = 6;
    int totalW = g_dlg._btnCount * btnW + (g_dlg._btnCount - 1) * gap;
    int bx = wx + (ww - totalW) / 2;
    for (int i = 0; i < g_dlg._btnCount; i++) {
        Rectangle bR = {(float)(bx + i * (btnW + gap)), (float)btnY, (float)btnW, BTN_H};
        if (_btn(bR, g_dlg._btnLabels[i])) {
            DialogResult r = _makeResult(); r.success = true;
            snprintf(r.output, DIALOG_PATH_MAX, "%s", g_dlg._btnLabels[i]);
            g_dlg.type = 0;
            if (g_dlg._callback) g_dlg._callback(r);
            return;
        }
    }
    if (IsKeyPressed(KEY_ESCAPE)) { _closeCancel(); }
}

/* ── Public API ── */

void DialogSetFont(Font font, int sz) {
    _persistFont = font; _persistFontSize = sz > 0 ? sz : 20;
    g_dlg._font = font; g_dlg._fontSize = _persistFontSize;
}

static void _initCommon(void) {
    g_dlg._font = _persistFont.texture.id > 0 ? _persistFont : GetFontDefault();
    g_dlg._fontSize = _persistFontSize;
}

void DialogOpen_Init(const char* title, const char* filter,
                     const char* startDir, DialogCallback cb) {
    memset(&g_dlg, 0, sizeof(g_dlg)); g_dlg.type = 1; g_dlg._callback = cb;
    _initCommon();
    if (title)  snprintf(g_dlg._title,  sizeof(g_dlg._title),  "%s", title);
    if (filter) snprintf(g_dlg._filter, sizeof(g_dlg._filter), "%s", filter);
    g_dlg._filterActive = true;
    _initDir();
    if (startDir && startDir[0] && DirectoryExists(startDir)) {
        size_t dl = strlen(startDir);
        if (dl >= DIALOG_PATH_MAX) dl = DIALOG_PATH_MAX - 1;
        memcpy(g_dlg._currentDir, startDir, dl);
        g_dlg._currentDir[dl] = '\0';
        _loadDir();
    }
}

void DialogSaveAs_Init(const char* title, const char* filter,
                       const char* defaultName, const char* startDir,
                       DialogCallback cb) {
    memset(&g_dlg, 0, sizeof(g_dlg)); g_dlg.type = 2; g_dlg._callback = cb; g_dlg._textActive = true;
    _initCommon();
    if (title)  snprintf(g_dlg._title,  sizeof(g_dlg._title),  "%s", title);
    if (filter) snprintf(g_dlg._filter, sizeof(g_dlg._filter), "%s", filter);
    if (defaultName) {
        snprintf(g_dlg._textInput, sizeof(g_dlg._textInput), "%s", defaultName);
        g_dlg._textLen = (int)strlen(defaultName); g_dlg._cursorPos = g_dlg._textLen;
    }
    _initDir();
    if (startDir && startDir[0] && DirectoryExists(startDir)) {
        size_t dl = strlen(startDir);
        if (dl >= DIALOG_PATH_MAX) dl = DIALOG_PATH_MAX - 1;
        memcpy(g_dlg._currentDir, startDir, dl);
        g_dlg._currentDir[dl] = '\0';
        _loadDir();
    }
}

void DialogYesNo_Init(const char* message, DialogCallback cb) {
    memset(&g_dlg, 0, sizeof(g_dlg)); g_dlg.type = 3; g_dlg._callback = cb;
    _initCommon();
    if (message) snprintf(g_dlg._message, sizeof(g_dlg._message), "%s", message);
}

void DialogButtonChoice_Init(const char* title, const char* message,
                              DialogCallback cb, const char* btn1, ...) {
    memset(&g_dlg, 0, sizeof(g_dlg)); g_dlg.type = 4; g_dlg._callback = cb;
    _initCommon();
    if (title)   snprintf(g_dlg._title,   sizeof(g_dlg._title),   "%s", title);
    if (message) snprintf(g_dlg._message, sizeof(g_dlg._message), "%s", message);

    va_list args;
    va_start(args, btn1);
    const char* label = btn1;
    g_dlg._btnCount = 0;
    while (label != NULL && g_dlg._btnCount < 8) {
        snprintf(g_dlg._btnLabels[g_dlg._btnCount], sizeof(g_dlg._btnLabels[0]), "%s", label);
        g_dlg._btnCount++;
        label = va_arg(args, const char*);
    }
    va_end(args);
}

void Dialog_Draw(void) {
    if (!g_dlg.type) return;
    DrawRectangle(0, 0, GetScreenWidth(), GetScreenHeight(), _overlay);
    if (g_dlg.type == 3) _drawYesNo();
    else if (g_dlg.type == 4) _drawButtonChoice();
    else _drawFileDialog();
}

bool Dialog_IsActive(void) {
    return g_dlg.type != 0;
}
