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

static void ResetState() {
    s_dragAction = 0;
    s_dragCorner = -1;
    s_dragEdge   = -1;
    s_startUV    = {0, 0};
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

    // Compute screen-space center
    Vector2 centerSc = {0, 0};
    for (int i = 0; i < 4; i++) {
        centerSc.x += sc[i].x;
        centerSc.y += sc[i].y;
    }
    centerSc.x /= 4.0f;
    centerSc.y /= 4.0f;

    static const float HS  = 10.0f;   // corner handle half-size
    static const float EO  = 14.0f;   // edge handle offset outward
    static const float CM  = 4.0f;    // capture margin

    // ── Precompute edge normals ─────────────────────────────────────
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

    // ── Hit-test: corner squares (distance to handle center) ──────
    float HS_SQRT2 = HS * 1.41421356f;
    Vector2 ch[4];
    for (int i = 0; i < 4; i++) {
        float dx = sc[i].x - sc[(i+2)%4].x;
        float dy = sc[i].y - sc[(i+2)%4].y;
        float len = sqrtf(dx*dx + dy*dy);
        if (len < 0.0001f) { ch[i] = sc[i]; continue; }
        ch[i].x = sc[i].x + (dx/len) * HS_SQRT2;
        ch[i].y = sc[i].y + (dy/len) * HS_SQRT2;
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
        // Snap cursor to world-space mouse position on release
        // (but not after corner scale, rotation, or edge drag — cursor stays where it is)
        if (s_dragAction != 2 && s_dragAction != 3 && s_dragAction != 5) {
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

    // Compute screen-space center
    Vector2 centerSc = {0, 0};
    for (int i = 0; i < 4; i++) {
        centerSc.x += scrn[i].x;
        centerSc.y += scrn[i].y;
    }
    centerSc.x /= 4.0f;
    centerSc.y /= 4.0f;

    static const float HS = 10.0f;   // corner handle half-size
    static const float EO = 14.0f;   // edge handle offset outward

    // Corner handles — outward from opposite corner, aligned with rect edges
    float HS_SQRT2 = HS * 1.41421356f;
    Vector2 sq[5];
    for (int i = 0; i < 4; i++) {
        float dx = scrn[i].x - scrn[(i+2)%4].x;
        float dy = scrn[i].y - scrn[(i+2)%4].y;
        float len = sqrtf(dx*dx + dy*dy);
        if (len < 0.0001f) continue;
        float odx = dx/len, ody = dy/len;
        float hx = scrn[i].x + odx * HS_SQRT2;
        float hy = scrn[i].y + ody * HS_SQRT2;
        int ni = (i+1)%4, pi = (i+3)%4;
        float ux = scrn[ni].x - scrn[i].x, uy = scrn[ni].y - scrn[i].y;
        float vx = scrn[pi].x - scrn[i].x, vy = scrn[pi].y - scrn[i].y;
        float ulen = sqrtf(ux*ux + uy*uy);
        float vlen = sqrtf(vx*vx + vy*vy);
        if (ulen < 0.0001f || vlen < 0.0001f) continue;
        ux /= ulen; uy /= ulen; vx /= vlen; vy /= vlen;
        sq[0] = Vector2{hx - ux*HS - vx*HS, hy - uy*HS - vy*HS};
        sq[1] = Vector2{hx + ux*HS - vx*HS, hy + uy*HS - vy*HS};
        sq[2] = Vector2{hx + ux*HS + vx*HS, hy + uy*HS + vy*HS};
        sq[3] = Vector2{hx - ux*HS + vx*HS, hy - uy*HS + vy*HS};
        sq[4] = sq[0];
        DrawLineStrip(sq, 5, WHITE);
    }

    // Edge handles — offset outward along edge normal
    int edgeVert[4][2] = {{0,1},{1,2},{2,3},{3,0}};
    Vector2 el[2];
    for (int e = 0; e < 4; e++) {
        int i0 = edgeVert[e][0], i1 = edgeVert[e][1];
        float ex = scrn[i1].x - scrn[i0].x, ey = scrn[i1].y - scrn[i0].y;
        float elen = sqrtf(ex*ex + ey*ey);
        if (elen < 0.0001f) continue;
        float ndx = -ey / elen, ndy =  ex / elen;
        if (ndx*(scrn[i0].x - centerSc.x) + ndy*(scrn[i0].y - centerSc.y) < 0) {
            ndx = -ndx; ndy = -ndy;
        }
        float ox = ndx * EO, oy = ndy * EO;
        el[0] = Vector2{scrn[i0].x + ox, scrn[i0].y + oy};
        el[1] = Vector2{scrn[i1].x + ox, scrn[i1].y + oy};
        DrawLineStrip(el, 2, WHITE);
    }

    rlDrawRenderBatchActive();
    glDisable(GL_COLOR_LOGIC_OP);
}
