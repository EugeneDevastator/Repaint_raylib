#include "repaint.h"
#include "raylib.h"
#include <stdio.h>
#include <math.h>
#include <ctype.h>

// ── Internal state ────────────────────────────────────────────────────
static struct {
    char text[256];
    double endTime;
    double initDuration;
} s_state = {};

static int _wordCount(const char* s) {
    int n = 0, in = 0;
    for (; *s; s++) { if (*s > ' ') { if (!in) { n++; in = 1; } } else { in = 0; } }
    return n > 0 ? n : 1;
}

void InfoText_Show(const char* text) {
    snprintf(s_state.text, sizeof(s_state.text), "%s", text ? text : "");
    float dur = fmaxf((float)_wordCount(text) / 5.0f, 1.5f);
    s_state.initDuration = dur;
    s_state.endTime = GetTime() + dur;
}

// ── Line wrapping helpers ─────────────────────────────────────────────

// Returns true if c is alphanumeric (letter or digit).
static int _isAlphaNum(int c) {
    return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9');
}

// Measure pixel width of a string at the current font setting.
static float _textWidth(const char* s, float sz, float spacing) {
    return MeasureTextEx(g_dialogFont, s, sz, spacing).x;
}

// Find rightmost non-alphanumeric break point where the left portion fits within maxW.
// Returns the index to break AFTER (keep the break char on current line), or 0 if none fits.
static int _findBreak(const char* s, int maxLen, float maxW, float sz, float spacing) {
    int best = 0;
    for (int i = 0; i < maxLen && s[i]; i++) {
        if (!_isAlphaNum((unsigned char)s[i])) {
            // test if breaking here keeps left part within maxW
            char left[256];
            int leftLen = i + 1 < 255 ? i + 1 : 255;
            strncpy(left, s, leftLen); left[leftLen] = '\0';
            if (_textWidth(left, sz, spacing) <= maxW)
                best = leftLen;
        }
    }
    return best;
}

// Build up to 2 wrapped lines from `src` into lines[0] and lines[1].
// Returns the number of lines written (1 or 2). If text is truncated, "..." is appended.
static int _wrapText(const char* src, float maxW, float sz, float spacing,
                     char lines[2][256]) {
    lines[0][0] = lines[1][0] = '\0';
    int curLine = 0;
    int srcPos = 0;
    int srcLen = (int)strlen(src);

    while (srcPos < srcLen && curLine < 2) {
        // skip leading spaces
        while (srcPos < srcLen && src[srcPos] == ' ') srcPos++;
        if (srcPos >= srcLen) break;

        // find end of this whitespace-delimited token
        int tokStart = srcPos;
        while (srcPos < srcLen && src[srcPos] != ' ') srcPos++;
        int tokLen = srcPos - tokStart;

        // copy token into a temp buffer
        char tok[256];
        int tl = tokLen < 255 ? tokLen : 255;
        strncpy(tok, src + tokStart, tl);
        tok[tl] = '\0';

        // measure current line + token with a space separator
        int lineLen = (int)strlen(lines[curLine]);
        char test[512];
        if (lineLen > 0)
            snprintf(test, sizeof(test), "%s %s", lines[curLine], tok);
        else
            snprintf(test, sizeof(test), "%s", tok);

        if (_textWidth(test, sz, spacing) <= maxW) {
            // fits — append to current line
            strncpy(lines[curLine], test, 255);
            lines[curLine][255] = '\0';
        } else if (lineLen == 0) {
            // token alone exceeds width — try to break it
            int breakAt = _findBreak(tok, tl, maxW, sz, spacing);
            if (breakAt > 0 && breakAt < tl) {
                // break at the non-alnum character
                char part1[256], part2[256];
                strncpy(part1, tok, breakAt); part1[breakAt] = '\0';
                snprintf(part2, sizeof(part2), "%s", tok + breakAt);
                // part1 should fit (it's shorter than the full token)
                strncpy(lines[curLine], part1, 255);
                lines[curLine][255] = '\0';
                // part2 goes to next line
                curLine++;
                if (curLine < 2) {
                    strncpy(lines[curLine], part2, 255);
                    lines[curLine][255] = '\0';
                }
            } else {
                // no break point — just place what fits
                // find how many chars of tok fit
                int ci = 0;
                while (ci < tl) {
                    char tmp[2] = {tok[ci], 0};
                    char t[8]; snprintf(t, sizeof(t), "%s%s", lines[curLine], tmp);
                    if (_textWidth(t, sz, spacing) > maxW) break;
                    strncat(lines[curLine], tmp, 1);
                    ci++;
                }
                if (ci < tl) {
                    curLine++;
                    if (curLine < 2)
                        strncpy(lines[curLine], tok + ci, 255);
                }
            }
        } else {
            // token doesn't fit on current line — move to next
            curLine++;
            if (curLine < 2) {
                strncpy(lines[curLine], tok, 255);
                lines[curLine][255] = '\0';
            }
        }
    }

    // If we ran out of lines but text remains, append "..."
    if (srcPos < srcLen && curLine >= 2) {
        int l = (int)strlen(lines[1]);
        if (l + 4 < 256) {
            strcat(lines[1], " ...");
        }
    }

    return curLine + 1;  // line count
}

void InfoText_Draw(void) {
    if (s_state.text[0] == '\0') return;

    double now = GetTime();
    double remaining = s_state.endTime - now;
    if (remaining <= 0.0) { s_state.text[0] = '\0'; return; }

    float textScale = 1.3f;
    float sz = g_dialogFont.baseSize * textScale;
    float spacing = 2.0f;

    int sw = GetScreenWidth();
    float maxW = sw * (3.0f / 5.0f);

    // Wrap into up to 2 lines
    char lines[2][256];
    int lineCount = _wrapText(s_state.text, maxW, sz, spacing, lines);

    // Compute bounding box for hover and drawing
    float lineH = MeasureTextEx(g_dialogFont, "Wy", sz, spacing).y;
    float totalH = lineCount * lineH;
    float maxLineW = fmaxf(
        lineCount > 0 ? _textWidth(lines[0], sz, spacing) : 0,
        lineCount > 1 ? _textWidth(lines[1], sz, spacing) : 0
    );

    float bx = (sw - maxLineW) * 0.5f;
    float by = 16.0f;

    // ── Hover pause — reset timer while mouse is over text area ──────
    if (remaining > 0.5) {
        Vector2 mp = GetMousePosition();
        if (mp.x >= bx && mp.x <= bx + maxLineW &&
            mp.y >= by && mp.y <= by + totalH)
            s_state.endTime = now + s_state.initDuration;
    }

    // ── Fade alpha ───────────────────────────────────────────────────
    double remaining2 = s_state.endTime - now;
    float alpha = remaining2 > 0.5 ? 1.0f : (float)(remaining2 / 0.5);
    alpha = fmaxf(alpha, 0.0f);

    Color shadowCol = ColorAlpha(BLACK, alpha * 0.6f);
    Color textCol   = ColorAlpha(WHITE, alpha);

    // ── Draw each line ──────────────────────────────────────────────
    for (int i = 0; i < lineCount; i++) {
        float lw  = _textWidth(lines[i], sz, spacing);
        float lx  = (sw - lw) * 0.5f;
        float ly  = by + i * lineH;

        DrawTextEx(g_dialogFont, lines[i], Vector2{lx + 1, ly + 1}, sz, spacing, shadowCol);
        DrawTextEx(g_dialogFont, lines[i], Vector2{lx,     ly    }, sz, spacing, textCol);
    }
}
