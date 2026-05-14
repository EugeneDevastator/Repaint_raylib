/*
 * dialog.c - raylib modal dialogs: Open, SaveAs, YesNo
 */
#include "dialog.h"
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <time.h>

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
    size_t nl = strlen(name), fl = strlen(filter);
    return nl >= fl && strcmp(name + nl - fl, filter) == 0;
}

static bool _matchFilter(const char* path, const char* filter) {
    if (!filter || !filter[0]) return true;
    const char* name = GetFileName(path);
    return strstr(name, filter) != NULL;
}

static bool _itemVisible(DialogState* d, int i) {
    if (DirectoryExists(d->_files.paths[i])) return false;
    if (!_matchFilter(d->_files.paths[i], d->_filterBuf)) return false;
    if (d->type == 1) return _matchExt(d->_files.paths[i], d->_filter);
    return d->type == 2;
}

static void _visPath(DialogState* d, int idx, char* out, int sz) {
    int v = 0;
    for (int i = 0; i < (int)d->_files.count; i++) {
        if (!_itemVisible(d, i)) continue;
        if (v == idx) { snprintf(out, sz, "%s", d->_files.paths[i]); return; }
        v++;
    }
    out[0] = '\0';
}

static int _visCount(DialogState* d) {
    int c = 0;
    for (int i = 0; i < (int)d->_files.count; i++)
        if (_itemVisible(d, i)) c++;
    return c;
}

static void _loadDir(DialogState* d) {
    if (d->_files.paths) { UnloadDirectoryFiles(d->_files); d->_files.paths = NULL; }
    d->_files = LoadDirectoryFilesEx(d->_currentDir, "*.*", false);
    _sortCol = d->_sortColumn;
    _sortDesc = d->_sortDesc;
    if (d->_files.count > 1)
        qsort(d->_files.paths, d->_files.count, sizeof(char*), _sortPath);
    d->_scrollOffset = d->_leftScrollOffset = 0;
    d->_selectedIndex = -1;
    snprintf(d->_pathPreview, DIALOG_PATH_MAX, "%s", d->_currentDir);
}


static void _navUp(DialogState* d) {
    const char* p = GetPrevDirectoryPath(d->_currentDir);
    if (p && p[0]) { snprintf(d->_currentDir, DIALOG_PATH_MAX, "%s", p); _trimSlash(d->_currentDir); _loadDir(d); }
}

static void _navInto(DialogState* d, const char* path) {
    if (DirectoryExists(path)) { snprintf(d->_currentDir, DIALOG_PATH_MAX, "%s", path); _trimSlash(d->_currentDir); _loadDir(d); }
}

static void _initDir(DialogState* d) {
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
    memcpy(d->_currentDir, dir, dl);
    d->_currentDir[dl] = '\0';
    _loadDir(d);
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

static void _closeOk(DialogState* d, const char* path) {
    DialogResult r = _makeResult(); r.success = true;
    snprintf(r.output, DIALOG_PATH_MAX, "%s", path);
    d->type = 0;
    if (d->_callback) d->_callback(r);
}

static void _closeCancel(DialogState* d) {
    DialogResult r = _makeResult(); d->type = 0;
    if (d->_callback) d->_callback(r);
}

static int _btn(DialogState* d, Rectangle r, const char* label) {
    Vector2 mp = GetMousePosition();
    bool hov = CheckCollisionPointRec(mp, r);
    bool dn  = hov && IsMouseButtonDown(MOUSE_LEFT_BUTTON);
    DrawRectangleRec(r, dn ? _btnPrs : hov ? _btnHov : _btnBg);
    DrawRectangleLinesEx(r, 1, _border);
    float tw = _measureText(d->_font, label, d->_fontSize);
    _drawText(d->_font, label,
              (int)(r.x + (r.width  - tw)          / 2),
              (int)(r.y + (r.height - d->_fontSize) / 2),
              d->_fontSize, _text);
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

static void _textField(DialogState* d, Rectangle r, char* buf, int maxLen,
                       int* cur, int* len, bool active) {
    DrawRectangleRec(r, _inpBg);
    DrawRectangleLinesEx(r, 1, active ? _titleBg : _inpBd);
    if (active) {
        int c;
        while ((c = GetCharPressed()) > 0)
            if (c >= 32 && c < 127 && *len < maxLen-1) {
                for (int i = *len; i > *cur; i--) buf[i] = buf[i-1];
                buf[(*cur)++] = (char)c; buf[++(*len)] = '\0';
            }
        if (IsKeyPressed(KEY_BACKSPACE) && *cur > 0)
            { for (int i=*cur-1; i<*len; i++) buf[i]=buf[i+1]; (*cur)--; (*len)--; }
        if (IsKeyPressed(KEY_DELETE) && *cur < *len)
            { for (int i=*cur; i<*len; i++) buf[i]=buf[i+1]; (*len)--; }
        if (IsKeyPressed(KEY_LEFT)  && *cur > 0)    (*cur)--;
        if (IsKeyPressed(KEY_RIGHT) && *cur < *len) (*cur)++;
        if (IsKeyPressed(KEY_HOME)) *cur = 0;
        if (IsKeyPressed(KEY_END))  *cur = *len;
    }
    int tx = (int)r.x + 6;
    int ty = (int)r.y + ((int)r.height - d->_fontSize) / 2;
    _drawText(d->_font, buf, tx, ty, d->_fontSize, _text);
    if (active && (int)(GetTime()*2) % 2 == 0) {
        char tmp[DIALOG_PATH_MAX];
        snprintf(tmp, sizeof(tmp), "%.*s", *cur, buf);
        DrawRectangle(tx + (int)_measureText(d->_font, tmp, d->_fontSize),
                      ty, 1, d->_fontSize, _text);
    }
}

/* ── Left pane: folders only ── */

static void _drawLeftPane(DialogState* d, Rectangle a) {
    Vector2 mp = GetMousePosition();
    bool md = IsMouseButtonDown(MOUSE_LEFT_BUTTON);
    bool pressed = md && !d->_leftPrevMouseDown;
    d->_leftPrevMouseDown = md;

    int dirs = 0;
    for (int i = 0; i < (int)d->_files.count; i++)
        if (DirectoryExists(d->_files.paths[i])) dirs++;
    int totalH = (1 + dirs) * ITEM_H;
    int viewH = (int)a.height;
    int maxOff = totalH > viewH ? totalH - viewH : 0;

    float wheel = GetMouseWheelMove();
    if (wheel && CheckCollisionPointRec(mp, a)) {
        d->_leftScrollOffset -= (int)(wheel * ITEM_H * 3);
        if (d->_leftScrollOffset < 0) d->_leftScrollOffset = 0;
        if (d->_leftScrollOffset > maxOff) d->_leftScrollOffset = maxOff;
    }

    BeginScissorMode((int)a.x, (int)a.y, (int)a.width, viewH);
    int y0 = (int)a.y - d->_leftScrollOffset;

    /* [..] row */
    Rectangle ir = {a.x, (float)y0, a.width - SCROLL_W, ITEM_H};
    if (CheckCollisionPointRec(mp, ir)) DrawRectangleRec(ir, _hovBg);
    _drawText(d->_font, "[..]", (int)a.x+6, y0+(ITEM_H-d->_fontSize)/2, d->_fontSize, _textDim);
    if (pressed && CheckCollisionPointRec(mp, ir)) { EndScissorMode(); _navUp(d); return; }

    /* Directory rows */
    int di = 0;
    for (int i = 0; i < (int)d->_files.count; i++) {
        if (!DirectoryExists(d->_files.paths[i])) continue;
        int y = y0 + (1 + di) * ITEM_H; di++;
        if (y + ITEM_H <= (int)a.y || y >= (int)(a.y + viewH)) continue;
        Rectangle ir2 = {a.x, (float)y, a.width - SCROLL_W, ITEM_H};
        if (CheckCollisionPointRec(mp, ir2)) DrawRectangleRec(ir2, _hovBg);
        _drawText(d->_font, GetFileName(d->_files.paths[i]),
                  (int)a.x+6, y+(ITEM_H-d->_fontSize)/2, d->_fontSize, _text);
        if (pressed && CheckCollisionPointRec(mp, ir2)) { EndScissorMode(); _navInto(d, d->_files.paths[i]); return; }
    }
    EndScissorMode();

    _scrollbar((int)(a.x + a.width - SCROLL_W), (int)a.y, viewH, totalH, viewH,
               &d->_leftScrollOffset, &d->_leftScrollGrabbed,
               &d->_leftScrollGrabY, &d->_leftScrollStartOff, pressed);
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

static void _drawRightPane(DialogState* d, Rectangle a) {
    Vector2 mp = GetMousePosition();
    bool md = IsMouseButtonDown(MOUSE_LEFT_BUTTON);
    bool pressed = md && !d->_prevMouseDown;
    d->_prevMouseDown = md;

    /* ── Filter text field (same height as file entries) ── */
    Rectangle fr = {a.x + 2, a.y + 2, a.width - SCROLL_W - 4, ITEM_H};
    bool filterHov = CheckCollisionPointRec(mp, fr);
    if (d->type == 1) d->_filterActive = true;  // open dialog: always capture
    if (d->_filterActive && pressed && !filterHov && !d->_textActive) {
        // clicking elsewhere keeps it active, but Escape or Tab deactivates
        d->_filterActive = (d->type == 2);
    }
    if (d->_filterActive) {
        int c;
        while ((c = GetCharPressed()) > 0) {
            if (c >= 32 && c < 127 && d->_filterLen < 255) {
                d->_filterBuf[d->_filterLen++] = (char)c;
                d->_filterBuf[d->_filterLen] = '\0';
                d->_scrollOffset = 0;
                d->_selectedIndex = -1;
            }
        }
        if (IsKeyPressed(KEY_BACKSPACE) && d->_filterLen > 0) {
            d->_filterBuf[--d->_filterLen] = '\0';
            d->_scrollOffset = 0;
            d->_selectedIndex = -1;
        }
        if (IsKeyPressed(KEY_ESCAPE)) { d->_filterActive = false; d->_filterBuf[0] = '\0'; d->_filterLen = 0; }
    }
    DrawRectangleRec(fr, _white);
    DrawRectangleLinesEx(fr, 1, _titleBg);
    int fty = (int)fr.y + ((int)fr.height - d->_fontSize) / 2;
    if (d->_filterLen > 0)
        _drawText(d->_font, d->_filterBuf, (int)fr.x + 4, fty, d->_fontSize, _text);
    else
        _drawText(d->_font, "filter...", (int)fr.x + 4, fty, d->_fontSize, _textDim);

    /* ── Column headers ── */
    int headY = (int)(a.y + fr.height + 4);
    int nameW = (int)(a.width - SCROLL_W) * 3 / 5;
    int dateW = (int)(a.width - SCROLL_W) - nameW;
    Rectangle nameH = {a.x + 2, (float)headY, (float)nameW, ITEM_H};
    Rectangle dateH = {a.x + 2 + nameW, (float)headY, (float)dateW, ITEM_H};

    if (pressed && CheckCollisionPointRec(mp, nameH)) {
        if (d->_sortColumn == 0) d->_sortDesc = !d->_sortDesc;
        else { d->_sortColumn = 0; d->_sortDesc = 0; }
        _loadDir(d); return;
    }
    if (pressed && CheckCollisionPointRec(mp, dateH)) {
        if (d->_sortColumn == 1) d->_sortDesc = !d->_sortDesc;
        else { d->_sortColumn = 1; d->_sortDesc = 0; }
        _loadDir(d); return;
    }

    DrawRectangleRec(nameH, _hovBg);
    DrawRectangleRec(dateH, _hovBg);
    DrawLine((int)nameH.x, (int)(nameH.y + nameH.height), (int)(nameH.x + nameH.width) - 1, (int)(nameH.y + nameH.height), _border);
    DrawLine((int)dateH.x, (int)(dateH.y + dateH.height), (int)(dateH.x + dateH.width) - 1, (int)(dateH.y + dateH.height), _border);
    DrawLine((int)(nameH.x + nameH.width), (int)nameH.y, (int)(nameH.x + nameH.width), (int)(nameH.y + nameH.height), _border);

    char nameLabel[48];
    snprintf(nameLabel, sizeof(nameLabel), "Name%s", (d->_sortColumn == 0) ? (d->_sortDesc ? " \xe2\x96\xb4" : " \xe2\x96\xbe") : "");
    _drawText(d->_font, nameLabel, (int)nameH.x + 4, (int)nameH.y + ((int)nameH.height - d->_fontSize) / 2, d->_fontSize, _text);

    char dateLabel[48];
    snprintf(dateLabel, sizeof(dateLabel), "Date%s", (d->_sortColumn == 1) ? (d->_sortDesc ? " \xe2\x96\xb4" : " \xe2\x96\xbe") : "");
    _drawText(d->_font, dateLabel, (int)dateH.x + 4, (int)dateH.y + ((int)dateH.height - d->_fontSize) / 2, d->_fontSize, _text);

    /* ── File list area ── */
    int listY = (int)(headY + nameH.height + 2);
    int listH = (int)(a.y + a.height - listY);
    int totalH = _visCount(d) * ITEM_H;
    int viewH = listH;
    int maxOff = totalH > viewH ? totalH - viewH : 0;

    float wheel = GetMouseWheelMove();
    if (wheel && CheckCollisionPointRec(mp, (Rectangle){a.x, (float)listY, a.width - SCROLL_W, (float)listH})) {
        d->_scrollOffset -= (int)(wheel * ITEM_H * 3);
        if (d->_scrollOffset < 0) d->_scrollOffset = 0;
        if (d->_scrollOffset > maxOff) d->_scrollOffset = maxOff;
    }

    /* Keyboard navigation */
    int tv = _visCount(d);
    if (tv > 0) {
        if (IsKeyPressed(KEY_DOWN) && d->_selectedIndex < tv-1) {
            d->_selectedIndex++;
            _visPath(d, d->_selectedIndex, d->_pathPreview, DIALOG_PATH_MAX);
            int sy = d->_selectedIndex * ITEM_H - d->_scrollOffset;
            if (sy + ITEM_H > viewH) d->_scrollOffset = d->_selectedIndex * ITEM_H - viewH + ITEM_H;
        }
        if (IsKeyPressed(KEY_UP) && d->_selectedIndex > 0) {
            d->_selectedIndex--;
            _visPath(d, d->_selectedIndex, d->_pathPreview, DIALOG_PATH_MAX);
            int sy = d->_selectedIndex * ITEM_H - d->_scrollOffset;
            if (sy < 0) d->_scrollOffset = d->_selectedIndex * ITEM_H;
        }
    }

    BeginScissorMode((int)a.x, listY, (int)(a.width - SCROLL_W), listH);
    int y0 = listY - d->_scrollOffset;
    int vi = 0;

    for (int i = 0; i < (int)d->_files.count; i++) {
        if (!_itemVisible(d, i)) continue;
        int y = y0 + vi * ITEM_H;
        if (y + ITEM_H > listY && y < listY + listH) {
            Rectangle ir = {a.x, (float)y, a.width - SCROLL_W, ITEM_H};
            bool hov = CheckCollisionPointRec(mp, ir);
            bool sel = (vi == d->_selectedIndex);
            if (sel) DrawRectangleRec(ir, _selBg);
            else if (hov) DrawRectangleRec(ir, _hovBg);

            /* Name column */
            char dateStr[48];
            _fmtDate(GetFileModTime(d->_files.paths[i]), dateStr, sizeof(dateStr));
            _drawText(d->_font, GetFileName(d->_files.paths[i]),
                      (int)a.x + 6, y + (ITEM_H - d->_fontSize) / 2, d->_fontSize, _text);
            _drawText(d->_font, dateStr,
                      (int)(a.x + nameW + 2), y + (ITEM_H - d->_fontSize) / 2, d->_fontSize, _textDim);

            if (pressed && hov) {
                double now = GetTime();
                bool dbl = (now - d->_lastClickTime < DBLCK_TIME && d->_lastClickedIdx == vi);
                d->_lastClickTime = now; d->_lastClickedIdx = vi;
                if (dbl) {
                    char path[DIALOG_PATH_MAX];
                    _visPath(d, vi, path, DIALOG_PATH_MAX);
                    EndScissorMode(); _closeOk(d, path); return;
                }
                d->_selectedIndex = vi;
                _visPath(d, vi, d->_pathPreview, DIALOG_PATH_MAX);
                if (d->type == 2 && d->_pathPreview[0]) {
                    snprintf(d->_textInput, DIALOG_PATH_MAX, "%s",
                             GetFileName(d->_pathPreview));
                    d->_textLen = (int)strlen(d->_textInput);
                    d->_cursorPos = d->_textLen; d->_textActive = true;
                }
            }
        }
        vi++;
    }
    EndScissorMode();

    _scrollbar((int)(a.x + a.width - SCROLL_W), listY, viewH, totalH, viewH,
               &d->_scrollOffset, &d->_scrollGrabbed,
               &d->_scrollGrabY, &d->_scrollStartOff, pressed);
}

/* ── File dialog ── */

static void _drawFileDialog(DialogState* d) {
    int sw = GetScreenWidth(), sh = GetScreenHeight();
    int winW = sw*8/10, winH = sh*8/10;
    int wx = (sw - winW)/2, wy = (sh - winH)/2;
    Rectangle win = {(float)wx, (float)wy, (float)winW, (float)winH};
    d->_bounds = win;

    DrawRectangleRec(win, _winBg);
    DrawRectangleLinesEx(win, 1, _border);
    DrawRectangle(wx, wy, winW, TITLE_H, _titleBg);
    _drawText(d->_font, d->_title, wx+PAD, wy+(TITLE_H-d->_fontSize)/2, d->_fontSize, _white);

    Rectangle xR = {win.x + win.width - TITLE_H, win.y, TITLE_H, TITLE_H};
    if (_btn(d, xR, "X")) { _closeCancel(d); return; }

    /* Overwrite confirm overlay */
    if (d->_confirmOverwrite) {
        Rectangle ov = {win.x+1, (float)(wy+TITLE_H), win.width-2, win.height-TITLE_H-2};
        DrawRectangleRec(ov, (Color){232,232,232,240});
        int bw=360, bh=120, bx=wx+(winW-bw)/2, by=wy+(winH-bh)/2-10;
        Rectangle box = {(float)bx, (float)by, (float)bw, (float)bh};
        DrawRectangleRec(box, _winBg); DrawRectangleLinesEx(box, 1, _border);
        char msg[512];
        snprintf(msg, sizeof(msg), "Overwrite \"%s\"?", GetFileName(d->_overwritePath));
        float tw = _measureText(d->_font, msg, d->_fontSize);
        _drawText(d->_font, msg, bx+(int)(bw-tw)/2, by+12, d->_fontSize, _text);
        int btnY = by + bh - BTN_H - 10;
        Rectangle yR = {(float)(bx+bw/2-BTN_W-6), (float)btnY, BTN_W, BTN_H};
        Rectangle nR = {(float)(bx+bw/2+6),        (float)btnY, BTN_W, BTN_H};
        if (_btn(d, yR, "Yes") || IsKeyPressed(KEY_ENTER)) {
            d->_confirmOverwrite = false;
            _closeOk(d, d->_overwritePath);
        } else if (_btn(d, nR, "No") || IsKeyPressed(KEY_ESCAPE))
            d->_confirmOverwrite = false;
        return;
    }

    /* Dir bar */
    int dirY = wy + TITLE_H;
    Rectangle upR = {(float)wx+PAD, (float)dirY+(DIRBAR_H-BTN_H)/2, 38, BTN_H};
    if (_btn(d, upR, "^")) { _navUp(d); return; }
    int dtx = (int)upR.x + (int)upR.width + PAD;
    BeginScissorMode(dtx, dirY, winW-(dtx-wx)-PAD, DIRBAR_H);
    _drawText(d->_font, d->_currentDir, dtx, dirY+(DIRBAR_H-d->_fontSize)/2, d->_fontSize, _textDim);
    EndScissorMode();
    DrawLine(wx, dirY+DIRBAR_H, wx+winW, dirY+DIRBAR_H, _border);

    int contentY = dirY + DIRBAR_H + 2;
    int contentH = winH - (contentY - wy) - PATHBAR_H - BOTTOM_H - 4;

    Rectangle leftR = {(float)wx+2, (float)contentY, (float)LEFT_PANE-2, (float)contentH};
    DrawRectangleRec(leftR, _winBg); DrawRectangleLinesEx(leftR, 1, _border);
    _drawLeftPane(d, (Rectangle){leftR.x+1, leftR.y+1, leftR.width-2, leftR.height-2});
    DrawLine(wx+LEFT_PANE, contentY, wx+LEFT_PANE, contentY+contentH, _border);

    Rectangle rightR = {(float)(wx+LEFT_PANE+1), (float)contentY,
                        (float)(winW-LEFT_PANE-3), (float)contentH};
    _drawRightPane(d, rightR);

    int pathY = contentY + contentH + 2;
    DrawLine(wx, pathY, wx+winW, pathY, _border);
    BeginScissorMode(wx+PAD, pathY+2, winW-PAD*2, PATHBAR_H-4);
    _drawText(d->_font, d->_pathPreview, wx+PAD,
              pathY+2+(PATHBAR_H-4-d->_fontSize)/2, d->_fontSize, _textDim);
    EndScissorMode();

    int botY = pathY + PATHBAR_H;
    DrawLine(wx, botY, wx+winW, botY, _border);

    if (d->type == 2) {
        int inpW = winW - (BTN_W*2 + PAD*4);
        Rectangle inpR  = {(float)(wx+PAD),              (float)botY+(BOTTOM_H-BTN_H)/2, (float)inpW, BTN_H};
        Rectangle saveR = {(float)(wx+PAD+inpW+PAD),     (float)botY+(BOTTOM_H-BTN_H)/2, BTN_W, BTN_H};
        Rectangle canR  = {(float)(wx+winW-BTN_W-PAD),   (float)botY+(BOTTOM_H-BTN_H)/2, BTN_W, BTN_H};
        _textField(d, inpR, d->_textInput, DIALOG_PATH_MAX,
                   &d->_cursorPos, &d->_textLen, d->_textActive);
        if (IsMouseButtonPressed(MOUSE_LEFT_BUTTON))
            d->_textActive = CheckCollisionPointRec(GetMousePosition(), inpR);
        if (_btn(d, saveR, "Save") || (d->_textActive && IsKeyPressed(KEY_ENTER))) {
            char fname[DIALOG_PATH_MAX];
            snprintf(fname, sizeof(fname), "%s", d->_textInput);
            if (d->_filter[0] == '.' && !_matchExt(fname, d->_filter)) {
                int fl = (int)strlen(fname);
                snprintf(fname+fl, DIALOG_PATH_MAX-fl, "%s", d->_filter);
            }
            if (fname[0]) {
				char full[DIALOG_PATH_MAX * 2 + 1];
				snprintf(full, sizeof(full), "%s/%s", d->_currentDir, fname);
				if (FileExists(full)) {
					strncpy(d->_overwritePath, full, DIALOG_PATH_MAX-1);
					d->_overwritePath[DIALOG_PATH_MAX-1] = '\0';
					d->_confirmOverwrite = true;
				} else if (full[0]) {
					_closeOk(d, full);
				}
            }
            return;
        }
        if (_btn(d, canR, "Cancel") || IsKeyPressed(KEY_ESCAPE)) { _closeCancel(d); return; }
    } else {
        int bax = winW - (BTN_W*2 + PAD*3);
        Rectangle openR = {(float)(wx+bax),          (float)botY+(BOTTOM_H-BTN_H)/2, BTN_W, BTN_H};
        Rectangle canR  = {(float)(wx+bax+BTN_W+PAD),(float)botY+(BOTTOM_H-BTN_H)/2, BTN_W, BTN_H};
        if (_btn(d, canR, "Cancel") || IsKeyPressed(KEY_ESCAPE)) { _closeCancel(d); return; }
        if (_btn(d, openR, "Open") || IsKeyPressed(KEY_ENTER)) {
            if (d->_selectedIndex >= 0) {
                char path[DIALOG_PATH_MAX];
                _visPath(d, d->_selectedIndex, path, DIALOG_PATH_MAX);
                if (path[0] && DirectoryExists(path)) { _navInto(d, path); return; }
                if (path[0]) { _closeOk(d, path); return; }
            }
        }
    }
}

/* ── YesNo dialog ── */

static void _drawYesNo(DialogState* d) {
    int sw = GetScreenWidth(), sh = GetScreenHeight();
    int ww = 440, wh = 180, wx = (sw-ww)/2, wy = (sh-wh)/2;
    Rectangle win = {(float)wx, (float)wy, (float)ww, (float)wh};
    DrawRectangleRec(win, _winBg); DrawRectangleLinesEx(win, 1, _border);
    DrawRectangle(wx, wy, ww, TITLE_H, _titleBg);
    _drawText(d->_font, "Confirm", wx+PAD, wy+(TITLE_H-d->_fontSize)/2, d->_fontSize, _white);
    Rectangle xR = {win.x + win.width - TITLE_H, win.y, TITLE_H, TITLE_H};
    if (_btn(d, xR, "X")) { _closeCancel(d); return; }
    float tw = _measureText(d->_font, d->_message, d->_fontSize);
    _drawText(d->_font, d->_message, wx+(int)(ww-tw)/2, wy+TITLE_H+12, d->_fontSize, _text);
    int btnY = wy + wh - BTN_H - 12;
    Rectangle yR = {(float)(wx+ww/2-BTN_W-6), (float)btnY, BTN_W, BTN_H};
    Rectangle nR = {(float)(wx+ww/2+6),        (float)btnY, BTN_W, BTN_H};
    if (_btn(d, yR, "Yes") || IsKeyPressed(KEY_ENTER)) {
        DialogResult r = _makeResult(); r.success = true; d->type = 0;
        if (d->_callback) d->_callback(r);
    } else if (_btn(d, nR, "No") || IsKeyPressed(KEY_ESCAPE)) {
        _closeCancel(d);
    }
}

/* ── Public API ── */

void DialogSetFont(DialogState* dlg, Font font, int sz) {
    _persistFont = font; _persistFontSize = sz > 0 ? sz : 20;
    dlg->_font = font; dlg->_fontSize = _persistFontSize;
}

static void _initCommon(DialogState* d) {
    d->_font = _persistFont.texture.id > 0 ? _persistFont : GetFontDefault();
    d->_fontSize = _persistFontSize;
}

void DialogOpen_Init(DialogState* d, const char* title, const char* filter, DialogCallback cb) {
    memset(d, 0, sizeof(*d)); d->type = 1; d->_callback = cb;
    _initCommon(d);
    if (title)  snprintf(d->_title,  sizeof(d->_title),  "%s", title);
    if (filter) snprintf(d->_filter, sizeof(d->_filter), "%s", filter);
    _initDir(d);
}

void DialogSaveAs_Init(DialogState* d, const char* title, const char* filter,
                       const char* defaultName, DialogCallback cb) {
    memset(d, 0, sizeof(*d)); d->type = 2; d->_callback = cb; d->_textActive = true;
    _initCommon(d);
    if (title)  snprintf(d->_title,  sizeof(d->_title),  "%s", title);
    if (filter) snprintf(d->_filter, sizeof(d->_filter), "%s", filter);
    if (defaultName) {
        snprintf(d->_textInput, sizeof(d->_textInput), "%s", defaultName);
        d->_textLen = (int)strlen(defaultName); d->_cursorPos = d->_textLen;
    }
    _initDir(d);
}

void DialogYesNo_Init(DialogState* d, const char* message, DialogCallback cb) {
    memset(d, 0, sizeof(*d)); d->type = 3; d->_callback = cb;
    _initCommon(d);
    if (message) snprintf(d->_message, sizeof(d->_message), "%s", message);
}

void Dialog_Draw(DialogState* d) {
    if (!d->type) return;
    DrawRectangle(0, 0, GetScreenWidth(), GetScreenHeight(), _overlay);
    if (d->type == 3) _drawYesNo(d);
    else _drawFileDialog(d);
}

void Dialog_MakeDir(const char* path) { MakeDirectory(path); }
