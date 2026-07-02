#include "transform_handle.h"
#include "rlgl.h"
#include "external/glad.h"
#include <math.h>
#include <string.h>

// ── Drag state ───────────────────────────────────────────────────────
static int      s_dragAction = 0;  // 0=none, 1=translate, 2=scale, 3=rotate, 4=cursor, 5=edge
static int      s_dragCorner = -1;
static int      s_dragEdge   = -1; // 0=top,1=right,2=bottom,3=left
static Vector2  s_dragStart  = {0, 0};
static Vector2  s_startUV    = {0, 0};  // local UV at edge-drag start
static RectXform s_savedXform = {};
static Vector2  s_savedCursor = {0, 0};
static Vector2  s_savedLocalCursor = {0, 0}; // pivot local UV at drag start

static void ResetState() {
    s_dragAction = 0;
    s_dragCorner = -1;
    s_dragEdge   = -1;
    s_startUV    = {0, 0};
    memset(&s_savedXform, 0, sizeof(s_savedXform));
}

void TransformHandle_ResetState(void) { ResetState(); }

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

static float DistToSegment(Vector2 p, Vector2 a, Vector2 b) {
    float ex = b.x - a.x, ey = b.y - a.y;
    float lenSq = ex*ex + ey*ey;
    if (lenSq < 0.0001f) return Dist2D(p, a);
    float t = ((p.x - a.x)*ex + (p.y - a.y)*ey) / lenSq;
    t = fmaxf(0.0f, fminf(1.0f, t));
    return Dist2D(p, Vector2{a.x + t*ex, a.y + t*ey});
}

static void GetLocalUV(const float mat[6], Vector2 worldPt, float* u, float* v) {
    float a = mat[0], b = mat[1], tx = mat[2];
    float c = mat[3], d = mat[4], ty = mat[5];
    float det = a*d - b*c;
    if (fabsf(det) < 0.0001f) { *u = 0; *v = 0; return; }
    float invDet = 1.0f/det;
    float dx = worldPt.x - tx;
    float dy = worldPt.y - ty;
    *u = (dx*d - dy*b) * invDet;
    *v = (-dx*c + dy*a) * invDet;
}

// ── Input ────────────────────────────────────────────────────────────
bool TransformHandle_Input(RectXform* xform,
                           Vector2* cursor,
                           bool scaleProportionalToCursor,
                           bool lockAspect,
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

    // ── World-space axes from matrix (for handle geometry) ────────
    float* m = xform->mat;
    float ux = m[0], uy = m[3];
    float vx = m[1], vy = m[4];
    float tx = m[2], ty = m[5];
    float w_ = xform->w, h_ = xform->h;
    float uLen = sqrtf(ux*ux + uy*uy);
    float vLen = sqrtf(vx*vx + vy*vy);
    float uNx = (uLen > 0.0001f) ? ux/uLen : 1.0f;
    float uNy = (uLen > 0.0001f) ? uy/uLen : 0.0f;
    float vNx = (vLen > 0.0001f) ? vx/vLen : 0.0f;
    float vNy = (vLen > 0.0001f) ? vy/vLen : 1.0f;

    static const float HS  = 10.0f;   // corner handle half-size
    static const float EO  = 14.0f;   // edge handle offset outward
    static const float CM  = 4.0f;    // capture margin

    // ── Precompute edge normals ─────────────────────────────────────
    Vector2 centerSc = {(sc[0].x+sc[1].x+sc[2].x+sc[3].x)*0.25f,
                        (sc[0].y+sc[1].y+sc[2].y+sc[3].y)*0.25f};
    int edgeVert[4][2] = {{0,1},{1,2},{2,3},{3,0}};
    float edgeNx[4], edgeNy[4], edgeLen[4];
    for (int e = 0; e < 4; e++) {
        int i0 = edgeVert[e][0], i1 = edgeVert[e][1];
        float ex = sc[i1].x - sc[i0].x, ey = sc[i1].y - sc[i0].y;
        edgeLen[e] = sqrtf(ex*ex + ey*ey);
        if (edgeLen[e] < 0.0001f) { edgeNx[e] = 0; edgeNy[e] = 0; continue; }
        float nx = -ey / edgeLen[e], ny =  ex / edgeLen[e];
        if (nx*(sc[i0].x - centerSc.x) + ny*(sc[i0].y - centerSc.y) < 0) { nx = -nx; ny = -ny; }
        edgeNx[e] = nx; edgeNy[e] = ny;
    }

    // ── Hit-test: corner handles (same geometry as drawing) ─────────
    float pixScale = 1.0f / camera->zoom;
    float hw = HS * pixScale;
    int su[4] = {-1, 1, 1, -1};
    int sv[4] = {-1, -1, 1, 1};
    Vector2 wc[4] = {
        Vector2{tx,          ty},
        Vector2{tx+ux*w_,    ty+uy*w_},
        Vector2{tx+ux*w_+vx*h_, ty+uy*w_+vy*h_},
        Vector2{tx+vx*h_,    ty+vy*h_}
    };
    Vector2 ch[4];
    for (int i = 0; i < 4; i++) {
        Vector2 hwPt = {
            wc[i].x + (su[i]*uNx + sv[i]*vNx) * hw,
            wc[i].y + (su[i]*uNy + sv[i]*vNy) * hw
        };
        ch[i] = GetWorldToScreen2D(hwPt, *camera);
    }
    int nearCorner = -1;
    int nearEdge   = -1;
    for (int i = 0; i < 4; i++) {
        if (Dist2D(mousePos, ch[i]) < HS + CM) {
            nearCorner = i;
            break;
        }
    }

    // ── Hit-test: edge band (from rect edge outward to handle) ─────
    if (nearCorner < 0) {
        for (int e = 0; e < 4; e++) {
            if (edgeLen[e] < 0.0001f) continue;
            int i0 = edgeVert[e][0], i1 = edgeVert[e][1];
            float sd = (mousePos.x - sc[i0].x)*edgeNx[e]
                     + (mousePos.y - sc[i0].y)*edgeNy[e];
            if (sd < 0.0f || sd > EO + CM) continue;
            float ex = sc[i1].x - sc[i0].x;
            float ey = sc[i1].y - sc[i0].y;
            float t = ((mousePos.x - sc[i0].x)*ex + (mousePos.y - sc[i0].y)*ey)
                      / (edgeLen[e] * edgeLen[e]);
            if (t >= -0.05f && t <= 1.05f) {
                nearEdge = e;
                break;
            }
        }
    }

    // ── Cursor hit-test ─────────────────────────────────────────────
    bool nearCursor = (nearCorner < 0 && nearEdge < 0)
                      && Dist2D(mousePos, cursorSc) < 12.0f;

    // ── Drag start (left button) ────────────────────────────────────
    if (leftPressed) {
        if (nearCorner >= 0) {
            s_dragAction = 2;
            s_dragCorner = nearCorner;
        } else if (nearEdge >= 0) {
            s_dragAction = 5;
            s_dragEdge  = nearEdge;
            GetLocalUV(xform->mat, canvasPos, &s_startUV.x, &s_startUV.y);
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
        GetLocalUV(xform->mat, *cursor, &s_savedLocalCursor.x, &s_savedLocalCursor.y);
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
        // Update cursor visual to track the layer
        float* m = xform->mat;
        cursor->x = m[0]*s_savedLocalCursor.x + m[1]*s_savedLocalCursor.y + m[2];
        cursor->y = m[3]*s_savedLocalCursor.x + m[4]*s_savedLocalCursor.y + m[5];
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

    // ── Edge drag (axis-constrained) ───────────────────────────────
    if (s_dragAction == 5 && leftDown) {
        float sa = s_savedXform.mat[0], sb = s_savedXform.mat[1];
        float sc_ = s_savedXform.mat[3], sd = s_savedXform.mat[4];
        float stx = s_savedXform.mat[2], sty = s_savedXform.mat[5];
        float sw = s_savedXform.w, sh = s_savedXform.h;
        float curU, curV;
        GetLocalUV(s_savedXform.mat, canvasPos, &curU, &curV);
        float du = curU - s_startUV.x;
        float dv = curV - s_startUV.y;
        if (scaleProportionalToCursor) {
            // Layer mode — w/h are fixed pixel dimensions; scale matrix around opposite edge
            switch (s_dragEdge) {
            case 0: // top — scale V around bottom edge
            {
                float s = (sh - dv) / sh;
                xform->mat[1] = sb * s;
                xform->mat[4] = sd * s;
                xform->mat[2] = stx + sb*dv;
                xform->mat[5] = sty + sd*dv;
                break;
            }
            case 1: // right — scale U around left edge
            {
                float s = (sw + du) / sw;
                xform->mat[0] = sa * s;
                xform->mat[3] = sc_ * s;
                break;
            }
            case 2: // bottom — scale V around top edge
            {
                float s = (sh + dv) / sh;
                xform->mat[1] = sb * s;
                xform->mat[4] = sd * s;
                break;
            }
            case 3: // left — scale U around right edge
            {
                float s = (sw - du) / sw;
                xform->mat[0] = sa * s;
                xform->mat[3] = sc_ * s;
                xform->mat[2] = stx + sa*du;
                xform->mat[5] = sty + sc_*du;
                break;
            }
            }
            xform->w = sw;
            xform->h = sh;
        } else {
            // Crop mode — change w/h directly
            switch (s_dragEdge) {
            case 0: // top (y=0) — drag V, keep bottom (y=h) fixed
                xform->w = sw;
                xform->h = sh - dv;
                xform->mat[2] = stx + sb*dv;
                xform->mat[5] = sty + sd*dv;
                break;
            case 1: // right (x=w) — drag U, keep left (x=0) fixed
                xform->w = sw + du;
                xform->h = sh;
                break;
            case 2: // bottom (y=h) — drag V, keep top (y=0) fixed
                xform->w = sw;
                xform->h = sh + dv;
                break;
            case 3: // left (x=0) — drag U, keep right (x=w) fixed
                xform->w = sw - du;
                xform->h = sh;
                xform->mat[2] = stx + sa*du;
                xform->mat[5] = sty + sc_*du;
                break;
            }
            if (fabsf(xform->w) < 1.0f) xform->w = (xform->w < 0) ? -1.0f : 1.0f;
            if (fabsf(xform->h) < 1.0f) xform->h = (xform->h < 0) ? -1.0f : 1.0f;
            if (lockAspect) {
                float ratio = sw / sh;
                if (s_dragEdge == 0 || s_dragEdge == 2)
                    xform->w = xform->h * ratio;
                else
                    xform->h = xform->w / ratio;
            }
        }
        return true;
    }

    // ── Scale (corner drag) ──────────────────────────────────────────
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
            if (lockAspect) {
                float ratio = sw / sh;
                if (fabsf(nw - sw) > fabsf(nh - sh))
                    xform->h = xform->w / ratio;
                else
                    xform->w = xform->h * ratio;
            }
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
                if (lockAspect) {
                    float avg = (sx_f + sy_f) * 0.5f;
                    sx_f = sy_f = avg;
                }
                if (fabsf(sx_f)<0.01f) sx_f = (sx_f<0) ? -0.01f : 0.01f;
                if (fabsf(sy_f)<0.01f) sy_f = (sy_f<0) ? -0.01f : 0.01f;
                float oldSx = sqrtf(as*as + cs*cs);
                float oldSy = sqrtf(bs*bs + ds*ds);
                float ux = (oldSx>0.0001f) ? as/oldSx : 1.0f;
                float uy = (oldSx>0.0001f) ? cs/oldSx : 0.0f;
                float newSx = oldSx*sx_f, newSy = oldSy*sy_f;
                // V-axis: 90° rotation of U-axis, direction depends on handedness
                float vx, vy;
                if (det < 0) { vx =  uy; vy = -ux; }  // CW (flipped)
                else         { vx = -uy; vy =  ux; }  // CCW (normal)
                float m0 = ux*newSx, m1 = vx*newSy;
                float m3_ = uy*newSx, m4 = vy*newSy;
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
        if (s_dragAction != 2 && s_dragAction != 3 && s_dragAction != 5) {
            float dragDist = Dist2D(s_dragStart, canvasPos);
            if (dragDist < 5.0f) {
                // Single click → reposition pivot to click point
                cursor->x = canvasPos.x;
                cursor->y = canvasPos.y;
            } else {
                // Actual drag → restore pivot to its original local position
                float* m = xform->mat;
                float lu = s_savedLocalCursor.x, lv = s_savedLocalCursor.y;
                cursor->x = m[0]*lu + m[1]*lv + m[2];
                cursor->y = m[3]*lu + m[4]*lv + m[5];
            }
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

    // ── World-space axes from matrix ─────────────────────────────────
    const float* m = xform->mat;
    float ux = m[0], uy = m[3];    // U basis (full scale, not unit)
    float vx = m[1], vy = m[4];    // V basis (full scale, not unit)
    float tx = m[2], ty = m[5];
    float w_ = xform->w, h_ = xform->h;

    // World-space four corners (local → world through matrix)
    Vector2 wc[4];
    wc[0] = Vector2{tx,            ty};
    wc[1] = Vector2{tx+ux*w_,      ty+uy*w_};
    wc[2] = Vector2{tx+ux*w_+vx*h_, ty+uy*w_+vy*h_};
    wc[3] = Vector2{tx+vx*h_,      ty+vy*h_};

    // Unit U, V axes in world (direction only, for handle offsets)
    float uLen = sqrtf(ux*ux + uy*uy);
    float vLen = sqrtf(vx*vx + vy*vy);
    float uNx = (uLen > 0.0001f) ? ux/uLen : 1.0f;
    float uNy = (uLen > 0.0001f) ? uy/uLen : 0.0f;
    float vNx = (vLen > 0.0001f) ? vx/vLen : 0.0f;
    float vNy = (vLen > 0.0001f) ? vy/vLen : 1.0f;

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
        scrn[i] = ws(wc[i]);
    scrn[4] = scrn[0];
    DrawLineStrip(scrn, 5, WHITE);

    // ── Handle geometry in identity (local) space, transformed ──────
    static const float HS = 10.0f;   // corner handle half-size (pixels)
    static const float EO = 14.0f;   // edge handle offset (pixels)

    // Pixel handle parameters → world units at camera zoom
    float pixScale = 1.0f / camera->zoom;
    float hw = HS * pixScale;   // corner half-size in world
    float eo = EO * pixScale;   // edge offset in world

    // Outward-direction signs for each corner in local space
    int su[4] = {-1, 1, 1, -1};
    int sv[4] = {-1, -1, 1, 1};

    // Corner handle squares — defined with geometry derived from matrix axes
    Vector2 sq[5];
    for (int i = 0; i < 4; i++) {
        // Handle center in world: corner + outward_unit * hw
        // outward_unit = su[i]*uN + sv[i]*vN  (|outward| = √2 for orthogonal axes)
        Vector2 hwPt = {
            wc[i].x + (su[i]*uNx + sv[i]*vNx) * hw,
            wc[i].y + (su[i]*uNy + sv[i]*vNy) * hw
        };
        Vector2 h = ws(hwPt);

        // Screen-space U, V unit directions at this corner
        Vector2 sU_ = ws(Vector2{wc[i].x + uNx, wc[i].y + uNy});
        Vector2 sV_ = ws(Vector2{wc[i].x + vNx, wc[i].y + vNy});
        float sUx_ = sU_.x - scrn[i].x, sUy_ = sU_.y - scrn[i].y;
        float sVx_ = sV_.x - scrn[i].x, sVy_ = sV_.y - scrn[i].y;
        float sUlen = sqrtf(sUx_*sUx_ + sUy_*sUy_);
        float sVlen = sqrtf(sVx_*sVx_ + sVy_*sVy_);
        if (sUlen < 0.0001f || sVlen < 0.0001f) continue;
        float sUx = sUx_ / sUlen, sUy = sUy_ / sUlen;
        float sVx = sVx_ / sVlen, sVy = sVy_ / sVlen;

        sq[0] = Vector2{h.x - sUx*HS - sVx*HS, h.y - sUy*HS - sVy*HS};
        sq[1] = Vector2{h.x + sUx*HS - sVx*HS, h.y + sUy*HS - sVy*HS};
        sq[2] = Vector2{h.x + sUx*HS + sVx*HS, h.y + sUy*HS + sVy*HS};
        sq[3] = Vector2{h.x - sUx*HS + sVx*HS, h.y - sUy*HS + sVy*HS};
        sq[4] = sq[0];
        DrawLineStrip(sq, 5, WHITE);
    }

    // Edge handles — outward normals from matrix axes
    // Top: -vN, Right: +uN, Bottom: +vN, Left: -uN
    float enx[4] = {-vNx, uNx, vNx, -uNx};
    float eny[4] = {-vNy, uNy, vNy, -uNy};
    int edgeVert[4][2] = {{0,1},{1,2},{2,3},{3,0}};
    Vector2 el[2];
    for (int e = 0; e < 4; e++) {
        int i0 = edgeVert[e][0], i1 = edgeVert[e][1];
        Vector2 w0 = {wc[i0].x + enx[e]*eo, wc[i0].y + eny[e]*eo};
        Vector2 w1 = {wc[i1].x + enx[e]*eo, wc[i1].y + eny[e]*eo};
        el[0] = ws(w0);
        el[1] = ws(w1);
        DrawLineStrip(el, 2, WHITE);
    }

    rlDrawRenderBatchActive();
    glDisable(GL_COLOR_LOGIC_OP);
}
