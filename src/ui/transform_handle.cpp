#include "transform_handle.h"
#include "rlgl.h"
#include "external/glad.h"
#include <math.h>
#include <string.h>

// ── Drag state ───────────────────────────────────────────────────────
static int      s_dragAction = 0;  // 0=none, 1=translate, 2=scale, 3=rotate, 4=cursor
static int      s_dragCorner = -1;
static Vector2  s_dragStart = {0, 0};
static RectXform s_savedXform = {};
static Vector2  s_savedCursor = {0, 0};

static void ResetState() {
    s_dragAction = 0;
    s_dragCorner = -1;
    memset(&s_savedXform, 0, sizeof(s_savedXform));
}

// ── Helpers ─────────────────────────────────────────────────────────
static void Corners(const float mat[6], float w, float h,
                    Vector2 out[4]) {
    float a = mat[0], b = mat[1], tx = mat[2];
    float c = mat[3], d = mat[4], ty = mat[5];
    out[0] = Vector2{tx,     ty};
    out[1] = Vector2{a*w+tx, c*w+ty};
    out[2] = Vector2{a*w+b*h+tx, c*w+d*h+ty};
    out[3] = Vector2{b*h+tx, d*h+ty};
}

static float Dist2D(Vector2 a, Vector2 b) {
    float dx = a.x - b.x, dy = a.y - b.y;
    return sqrtf(dx*dx + dy*dy);
}

// ── Input ────────────────────────────────────────────────────────────
bool TransformHandle_Input(RectXform* xform,
                           Vector2* cursor,
                           bool scaleProportionalToCursor,
                           const Camera2D* camera,
                           Vector2 mousePos,
                           bool leftDown,
                           bool leftPressed,
                           bool rightDown,
                           bool rightPressed,
                           const DrawRect* rect)
{
    if (!rect || !rect->Contains(mousePos)) {
        if (!leftDown && !rightPressed) ResetState();
        return false;
    }

    Vector2 canvasPos = GetScreenToWorld2D(mousePos, *camera);
    Vector2 corners[4];
    Corners(xform->mat, xform->w, xform->h, corners);

    Vector2 sc[4];
    for (int i = 0; i < 4; i++)
        sc[i] = GetWorldToScreen2D(corners[i], *camera);

    Vector2 cursorSc = GetWorldToScreen2D(*cursor, *camera);

    // Hit-test
    bool nearCursor = Dist2D(mousePos, cursorSc) < 12.0f;
    int nearCorner = -1;
    for (int i = 0; i < 4; i++) {
        if (Dist2D(mousePos, sc[i]) < 12.0f) { nearCorner = i; break; }
    }

    // ── Drag start (left button) ────────────────────────────────────
    if (leftPressed) {
        if (nearCorner >= 0) {
            s_dragAction = 2;
            s_dragCorner = nearCorner;
        } else if (nearCursor) {
            s_dragAction = 4;
        } else {
            // Inside body → translate
            float a = xform->mat[0], b = xform->mat[1];
            float c = xform->mat[3], d = xform->mat[4];
            float det = a*d - b*c;
            if (fabsf(det) > 0.0001f) {
                float invDet = 1.0f/det;
                float lx = (canvasPos.x - xform->mat[2])*d*invDet
                           - (canvasPos.y - xform->mat[5])*b*invDet;
                float ly = -(canvasPos.x - xform->mat[2])*c*invDet
                           + (canvasPos.y - xform->mat[5])*a*invDet;
                if (lx >= 0 && lx <= xform->w && ly >= 0 && ly <= xform->h)
                    s_dragAction = 1;
            }
        }
        s_dragStart = canvasPos;
        s_savedXform = *xform;
        s_savedCursor = *cursor;
    }

    // ── Start rotate (right button) ─────────────────────────────────
    if (rightPressed) {
        s_dragAction = 3;
        s_dragStart = canvasPos;
        s_savedXform = *xform;
        s_savedCursor = *cursor;
    }

    // ── Translate ────────────────────────────────────────────────────
    if (s_dragAction == 1 && leftDown) {
        float mdx = canvasPos.x - s_dragStart.x;
        float mdy = canvasPos.y - s_dragStart.y;
        xform->mat[2] = s_savedXform.mat[2] + mdx;
        xform->mat[5] = s_savedXform.mat[5] + mdy;
        return true;
    }

    // ── Drag cursor ─────────────────────────────────────────────────
    if (s_dragAction == 4 && leftDown) {
        cursor->x = canvasPos.x;
        cursor->y = canvasPos.y;
        return true;
    }

    // ── Rotate around cursor ────────────────────────────────────────
    if (s_dragAction == 3 && (leftDown || rightDown)) {
        float cx = s_savedCursor.x, cy = s_savedCursor.y;
        float startAng = atan2f(s_dragStart.y - cy, s_dragStart.x - cx);
        float curAng   = atan2f(canvasPos.y - cy, canvasPos.x - cx);
        float deltaAng = curAng - startAng;
        if (deltaAng > (float)M_PI) deltaAng -= 2.0f*(float)M_PI;
        else if (deltaAng < -(float)M_PI) deltaAng += 2.0f*(float)M_PI;

        float cosD = cosf(deltaAng), sinD = sinf(deltaAng);
        float sa = s_savedXform.mat[0], sb = s_savedXform.mat[1], stx = s_savedXform.mat[2];
        float sc_ = s_savedXform.mat[3], sd = s_savedXform.mat[4], sty = s_savedXform.mat[5];
        xform->mat[0] = cosD*sa  + -sinD*sc_;
        xform->mat[1] = cosD*sb  + -sinD*sd;
        xform->mat[2] = cosD*stx + -sinD*sty + (cx - cx*cosD + cy*sinD);
        xform->mat[3] = sinD*sa  +  cosD*sc_;
        xform->mat[4] = sinD*sb  +  cosD*sd;
        xform->mat[5] = sinD*stx +  cosD*sty + (cy - cx*sinD - cy*cosD);
        return true;
    }

    // ── Scale ──────────────────────────────────────────────────────
    if (s_dragAction == 2 && leftDown) {
        if (!scaleProportionalToCursor) {
            // Extent-scale (crop): each corner moves independently;
            // the opposite diagonal corner stays fixed in world space.
            float sa = s_savedXform.mat[0], sb = s_savedXform.mat[1];
            float sc_ = s_savedXform.mat[3], sd = s_savedXform.mat[4];
            float stx = s_savedXform.mat[2], sty = s_savedXform.mat[5];
            float sw = s_savedXform.w, sh = s_savedXform.h;
            float dx = canvasPos.x - stx;
            float dy = canvasPos.y - sty;
            float det = sa*sd - sb*sc_;
            if (fabsf(det) < 0.0001f) return false;
            float invDet = 1.0f/det;
            float curLx = (dx*sd - dy*sb)*invDet;
            float curLy = (-dx*sc_ + dy*sa)*invDet;
            float nw, nh;
            switch (s_dragCorner) {
            case 0: // TL → opposite BR stays at (sw, sh)
                nw = sw - curLx;
                nh = sh - curLy;
                xform->mat[2] = stx + sa*curLx + sb*curLy;
                xform->mat[5] = sty + sc_*curLx + sd*curLy;
                break;
            case 1: // TR → opposite BL stays at (0, sh)
                nw = curLx;
                nh = sh - curLy;
                xform->mat[2] = stx + sb*curLy;
                xform->mat[5] = sty + sd*curLy;
                break;
            case 2: // BR → opposite TL stays at (0, 0)
                nw = curLx;
                nh = curLy;
                break;
            case 3: // BL → opposite TR stays at (sw, 0)
                nw = sw - curLx;
                nh = curLy;
                xform->mat[2] = stx + sa*curLx;
                xform->mat[5] = sty + sc_*curLx;
                break;
            }
            xform->w = nw;
            xform->h = nh;
            if (fabsf(xform->w) < 1.0f) xform->w = (xform->w < 0) ? -1.0f : 1.0f;
            if (fabsf(xform->h) < 1.0f) xform->h = (xform->h < 0) ? -1.0f : 1.0f;
        } else {
            // Scale proportional to cursor (layer): scale matrix around cursor
            float as = s_savedXform.mat[0], bs = s_savedXform.mat[1];
            float ts = s_savedXform.mat[2], cs = s_savedXform.mat[3];
            float ds = s_savedXform.mat[4], tys = s_savedXform.mat[5];
            float cx = s_savedCursor.x, cy = s_savedCursor.y;
            float dx = canvasPos.x - cx;
            float dy = canvasPos.y - cy;
            float det = as*ds - bs*cs;
            if (fabsf(det) > 0.0001f) {
                float invDet = 1.0f/det;
                float ia = ds*invDet, ib = -bs*invDet;
                float ic = -cs*invDet, id = as*invDet;
                float pcx = (cx-ts)*ia + (cy-tys)*ib;
                float pcy = (cx-ts)*ic + (cy-tys)*id;
                float lx = dx*ia + dy*ib;
                float ly = dx*ic + dy*id;
                int gc = s_dragCorner;
                float grabLx = (gc==0||gc==3) ? 0.0f : s_savedXform.w;
                float grabLy = (gc==0||gc==1) ? 0.0f : s_savedXform.h;
                float initDx = grabLx - pcx;
                float initDy = grabLy - pcy;
                float sx_f = (fabsf(initDx)>0.001f) ? lx/initDx : 1.0f;
                float sy_f = (fabsf(initDy)>0.001f) ? ly/initDy : 1.0f;
                if (fabsf(sx_f)<0.01f) sx_f = (sx_f<0) ? -0.01f : 0.01f;
                if (fabsf(sy_f)<0.01f) sy_f = (sy_f<0) ? -0.01f : 0.01f;
                float oldSx = sqrtf(as*as + cs*cs);
                float oldSy = sqrtf(bs*bs + ds*ds);
                float cosR = (oldSx>0.0001f) ? as/oldSx : 1.0f;
                float sinR = (oldSx>0.0001f) ? cs/oldSx : 0.0f;
                float newSx = oldSx*sx_f, newSy = oldSy*sy_f;
                float m0 = cosR*newSx, m1 = -sinR*newSy;
                float m3_ = sinR*newSx, m4 = cosR*newSy;
                xform->mat[0] = m0; xform->mat[1] = m1;
                xform->mat[2] = cx - (m0*pcx + m1*pcy);
                xform->mat[3] = m3_; xform->mat[4] = m4;
                xform->mat[5] = cy - (m3_*pcx + m4*pcy);
            }
        }
        return true;
    }

    // ── Release (any held button) ────────────────────────────────────
    if (s_dragAction != 0 && !leftDown && !rightDown) {
        // Snap cursor to world-space mouse position on release
        // (but not after corner scale — cursor stays where it is)
        if (s_dragAction != 2) {
            cursor->x = canvasPos.x;
            cursor->y = canvasPos.y;
        }
        // Normalize flips: transfer negative w/h to matrix rows
        if (xform->w < 0) { xform->mat[0] = -xform->mat[0]; xform->mat[1] = -xform->mat[1]; xform->w = -xform->w; }
        if (xform->h < 0) { xform->mat[3] = -xform->mat[3]; xform->mat[4] = -xform->mat[4]; xform->h = -xform->h; }
        ResetState();
    }

    return false;
}

// ── Draw ─────────────────────────────────────────────────────────────
void TransformHandle_Draw(const RectXform* xform,
                          Vector2 cursor,
                          const Camera2D* camera)
{
    auto ws = [&](Vector2 wp) -> Vector2 {
        return GetWorldToScreen2D(wp, *camera);
    };

    Vector2 corners[4];
    Corners(xform->mat, xform->w, xform->h, corners);

    rlDrawRenderBatchActive();
    glEnable(GL_COLOR_LOGIC_OP);
    glLogicOp(GL_XOR);

    // Cursor crosshair
    Vector2 uic = ws(cursor);
    float chLen = 12.0f;
    DrawLine(uic.x - chLen, uic.y, uic.x + chLen, uic.y, WHITE);
    DrawLine(uic.x, uic.y - chLen, uic.x, uic.y + chLen, WHITE);
    DrawCircle(uic.x, uic.y, 3.0f, WHITE);

    // Rectangle outline
    Vector2 scrn[5];
    for (int i = 0; i < 4; i++)
        scrn[i] = ws(corners[i]);
    scrn[4] = scrn[0];
    DrawLineStrip(scrn, 5, WHITE);

    // Corner handles
    for (int i = 0; i < 4; i++) {
        float hx = scrn[i].x, hy = scrn[i].y;
        float hs = 10.0f;
        DrawRectangleLinesEx(Rectangle{hx - hs, hy - hs, hs * 2, hs * 2}, 2.0f, WHITE);
    }

    rlDrawRenderBatchActive();
    glDisable(GL_COLOR_LOGIC_OP);
}
