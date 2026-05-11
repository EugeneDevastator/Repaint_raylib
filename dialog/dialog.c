/*
 * dialog.c - raylib modal dialogs: Open, SaveAs, YesNo
 */
#include "dialog.h"
#include <stdlib.h>
#include <string.h>
#include <stdio.h>

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

static int _sort(const void* a, const void* b) {
    const char* pa = *(const char**)a;
    const char* pb = *(const char**)b;
    int da = DirectoryExists(pa), db = DirectoryExists(pb);
    if (da != db) return db - da;
    return strcmp(GetFileName(pa), GetFileName(pb));
}

static bool _matchExt(const char* path, const char* filter) {
    if (!filter || !filter[0]) return true;
    const char* name = GetFileName(path);
    size_t nl = strlen(name), fl = strlen(filter);
    return nl >= fl && strcmp(name + nl - fl, filter) == 0;
}

static bool _itemVisible(DialogState* d, int i) {
    if (DirectoryExists(d->_files.paths[i])) return true;
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
    if (d->_files.count > 1)
        qsort(d->_files.paths, d->_files.count, sizeof(char*), _sort);
    d->_scrollOffset = d->_leftScrollOffset = 0;
    d->_selectedIndex = -1;
    snprintf(d->_pathPreview, DIALOG_PATH_MAX, "%s", d->_currentDir);
}

static void _navUp(DialogState* d) {
    const char* p = GetPrevDirectoryPath(d->_currentDir);
    if (p && p[0]) { snprintf(d->_currentDir, DIALOG_PATH_MAX, "%s", p); _loadDir(d); }
}

static void _navInto(DialogState* d, const char* path) {
    if (DirectoryExists(path)) { snprintf(d->_currentDir, DIALOG_PATH_MAX, "%s", path); _loadDir(d); }
}

static void _initDir(DialogState* d) {
    const char* app = GetApplicationDirectory();
    char saves[DIALOG_PATH_MAX];
    snprintf(saves, sizeof(saves), "%sSaves", app);
    snprintf(d->_currentDir, DIALOG_PATH_MAX, "%s",
             DirectoryExists(saves) ? saves : app);
    _loadDir(d);
}

/* ── Draw helpers ── */

/* spacing=1 avoids glyph bleed; raylib default font needs 0 */
static float _sp(Font f) {
    return f.texture.id == GetFontDefault().texture.id ? 0.0f : 1.0f;
}


static void _drawText(Font f, const char* t, int x, int y, int sz, Color c) {
    DrawTextEx(f, t, (Vector2){(float)x,(float)y}, (float)sz, _sp(sz), c);
}

static float _measureText(Font f, const char* t, int sz) {
    return MeasureTextEx(f, t, (float)sz, _sp(sz)).x;
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

/* ── Pane drawing ── */

static void _drawPane(DialogState* d, Rectangle area, bool leftOnly) {
    Vector2 mp = GetMousePosition();
    bool md = IsMouseButtonDown(MOUSE_LEFT_BUTTON);
    bool* prevDown  = leftOnly ? &d->_leftPrevMouseDown : &d->_prevMouseDown;
    bool  pressed   = md && !(*prevDown);
    *prevDown = md;

    int viewH = (int)area.height;
    int* scrollOff = leftOnly ? &d->_leftScrollOffset : &d->_scrollOffset;
    int totalItems;

    if (leftOnly) {
        int dirs = 0;
        for (int i = 0; i < (int)d->_files.count; i++)
            if (DirectoryExists(d->_files.paths[i])) dirs++;
        totalItems = (1 + dirs) * ITEM_H;
    } else {
        totalItems = _visCount(d) * ITEM_H;
    }

    int maxOff = totalItems > viewH ? totalItems - viewH : 0;
    float wheel = GetMouseWheelMove();
    if (wheel && CheckCollisionPointRec(mp, area)) {
        *scrollOff -= (int)(wheel * ITEM_H * 3);
        if (*scrollOff < 0) *scrollOff = 0;
        if (*scrollOff > maxOff) *scrollOff = maxOff;
    }

    if (!leftOnly) {
        int tv = _visCount(d);
        if (tv > 0) {
            if (IsKeyPressed(KEY_DOWN) && d->_selectedIndex < tv-1) {
                d->_selectedIndex++;
                _visPath(d, d->_selectedIndex, d->_pathPreview, DIALOG_PATH_MAX);
                int sy = d->_selectedIndex*ITEM_H - *scrollOff;
                if (sy + ITEM_H > viewH) *scrollOff = d->_selectedIndex*ITEM_H - viewH + ITEM_H;
            }
            if (IsKeyPressed(KEY_UP) && d->_selectedIndex > 0) {
                d->_selectedIndex--;
                _visPath(d, d->_selectedIndex, d->_pathPreview, DIALOG_PATH_MAX);
                int sy = d->_selectedIndex*ITEM_H - *scrollOff;
                if (sy < 0) *scrollOff = d->_selectedIndex*ITEM_H;
            }
        }
    }

    BeginScissorMode((int)area.x, (int)area.y, (int)area.width, viewH);
    int y0 = (int)area.y - *scrollOff;

    if (leftOnly) {
        /* ".." row */
        Rectangle ir = {area.x, (float)y0, area.width - SCROLL_W, ITEM_H};
        bool hov = CheckCollisionPointRec(mp, ir);
        if (hov) DrawRectangleRec(ir, _hovBg);
        _drawText(d->_font, "[..]", (int)area.x+6, y0+(ITEM_H-d->_fontSize)/2, d->_fontSize, _textDim);
        if (pressed && hov) { EndScissorMode(); _navUp(d); return; }

        int di = 0;
        for (int i = 0; i < (int)d->_files.count; i++) {
            if (!DirectoryExists(d->_files.paths[i])) continue;
            int y = y0 + (1 + di) * ITEM_H; di++;
            if (y + ITEM_H <= (int)area.y || y >= (int)(area.y + viewH)) continue;
            Rectangle ir2 = {area.x, (float)y, area.width - SCROLL_W, ITEM_H};
            bool hov2 = CheckCollisionPointRec(mp, ir2);
            if (hov2) DrawRectangleRec(ir2, _hovBg);
            _drawText(d->_font, GetFileName(d->_files.paths[i]),
                      (int)area.x+6, y+(ITEM_H-d->_fontSize)/2, d->_fontSize, _text);
            if (pressed && hov2) { EndScissorMode(); _navInto(d, d->_files.paths[i]); return; }
        }
    } else {
        int vi = 0;
        for (int i = 0; i < (int)d->_files.count; i++) {
            if (!_itemVisible(d, i)) continue;
            int y = y0 + vi * ITEM_H;
            if (y + ITEM_H > (int)area.y && y < (int)(area.y + viewH)) {
                bool isDir = DirectoryExists(d->_files.paths[i]);
                Rectangle ir = {area.x, (float)y, area.width - SCROLL_W, ITEM_H};
                bool hov = CheckCollisionPointRec(mp, ir);
                bool sel = (vi == d->_selectedIndex);
                if (sel) DrawRectangleRec(ir, _selBg);
                else if (hov) DrawRectangleRec(ir, _hovBg);
                char buf[512];
                snprintf(buf, sizeof(buf), isDir ? "[d] %s" : "    %s",
                         GetFileName(d->_files.paths[i]));
                _drawText(d->_font, buf, (int)area.x+6, y+(ITEM_H-d->_fontSize)/2,
                          d->_fontSize, isDir ? _textDim : _text);
                if (pressed && hov) {
                    double now = GetTime();
                    bool dbl = (now - d->_lastClickTime < DBLCK_TIME && d->_lastClickedIdx == vi);
                    d->_lastClickTime = now; d->_lastClickedIdx = vi;
                    if (dbl) {
                        if (isDir) { EndScissorMode(); _navInto(d, d->_files.paths[i]); return; }
                        char path[DIALOG_PATH_MAX];
                        _visPath(d, vi, path, DIALOG_PATH_MAX);
                        EndScissorMode(); _closeOk(d, path); return;
                    }
                    d->_selectedIndex = vi;
                    _visPath(d, vi, d->_pathPreview, DIALOG_PATH_MAX);
                    if (d->type == 2 && !isDir && d->_pathPreview[0]) {
                        snprintf(d->_textInput, DIALOG_PATH_MAX, "%s", GetFileName(d->_pathPreview));
                        d->_textLen = (int)strlen(d->_textInput);
                        d->_cursorPos = d->_textLen; d->_textActive = true;
                    }
                }
            }
            vi++;
        }
    }
    EndScissorMode();

    bool* grab     = leftOnly ? &d->_leftScrollGrabbed  : &d->_scrollGrabbed;
    int*  grabY    = leftOnly ? &d->_leftScrollGrabY    : &d->_scrollGrabY;
    int*  startOff = leftOnly ? &d->_leftScrollStartOff : &d->_scrollStartOff;
    _scrollbar((int)(area.x + area.width - SCROLL_W), (int)area.y, viewH,
               totalItems, viewH, scrollOff, grab, grabY, startOff, pressed);
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
    _drawPane(d, (Rectangle){leftR.x+1, leftR.y+1, leftR.width-2, leftR.height-2}, true);
    DrawLine(wx+LEFT_PANE, contentY, wx+LEFT_PANE, contentY+contentH, _border);

    Rectangle rightR = {(float)(wx+LEFT_PANE+1), (float)contentY,
                        (float)(winW-LEFT_PANE-3), (float)contentH};
    _drawPane(d, rightR, false);

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
                /* fix: use separate buffers to avoid truncation warning */
				// In _drawFileDialog, replace the full[] block:
				char full[DIALOG_PATH_MAX];
				snprintf(full, sizeof(full), "%s/%s", d->_currentDir, fname);
				if (FileExists(full)) {
					strncpy(d->_overwritePath, full, DIALOG_PATH_MAX-1);
					d->_overwritePath[DIALOG_PATH_MAX-1] = '\0';
					d->_confirmOverwrite = true;
				} else {
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
