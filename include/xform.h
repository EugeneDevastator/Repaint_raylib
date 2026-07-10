#ifndef XFORM_H
#define XFORM_H

#include "raylib.h"

// ── World units ───────────────────────────────────────────────────────
// 1 world unit = 256 pixels (the canvas PPU)
#define WORLD_UNIT_PX 256.0f

// ── 2×3 affine matrix (row-major: [a, b, tx, c, d, ty]) ─────────
// Pivot is always at (0,0). Matrix maps from local space to the
// coordinate space of its parent, independent of any w/h extent.

// out = a × b  (2×3 matrix multiplication)
void Xform_Mul     (float out[6], const float a[6], const float b[6]);
void Xform_MulInv  (float out[6], const float a[6], const float b[6]); // out = a × b^(-1)
void Xform_Identity(float out[6]);
void Xform_SetTrans(float out[6], float tx, float ty);
void Xform_SetRot  (float out[6], float angle);  // CCW radians
void Xform_SetScale(float out[6], float sx, float sy);

// unsigned_float — documents a float that must always be ≥ 0
typedef float unsigned_float;

// ── RectXform — oriented rectangle in world-space ────────────────
// mat maps from local space (pivot at origin) to world space.
// ww,wh are the extent in world units — purely metadata, the matrix
// does not depend on them.  When rot=0 and pivot is top-left:
// the rectangle covers (cx,cy) .. (cx+ww, cy+wh).
typedef struct {
    float mat[6];
    unsigned_float ww, wh;
} RectXform;

// Build mat = translate(cx,cy) · rotate(rot).  (cx,cy) is the
// pivot (local origin) position in world space.
RectXform RectXform_Pivot(float cx, float cy, float w, float h, float rot);

// Center = mat applied to local origin (0,0) = (mat[2], mat[5])
static inline void RectXform_GetCenter(const RectXform* rx, float* cx, float* cy) {
    *cx = rx->mat[2]; *cy = rx->mat[5];
}

// World-space centre of the rectangle: mat * (ww/2, wh/2) + mat[2,5]
static inline Vector2 RectXform_GetExtentCenter(const RectXform* rx) {
    return Vector2{
        rx->mat[0]*rx->ww*0.5f + rx->mat[1]*rx->wh*0.5f + rx->mat[2],
        rx->mat[3]*rx->ww*0.5f + rx->mat[4]*rx->wh*0.5f + rx->mat[5]
    };
}

// Rotation angle = angle of mat's x-axis in world space
float RectXform_GetRot(const RectXform* rx);

// ── Quad — xform + render texture pair for compositing ───────────────
// First-class operational object: carries both spatial context and pixel
// data. Constructed in-place, passed by pointer, modifications persist.
struct Quad {
    RectXform       xform;
    RenderTexture2D rt;
};

// ── World-space helpers ──────────────────────────────────────────

// Axis-aligned bounding box of an oriented rectangle in world space
Rectangle GetWorldAABB(const RectXform* rx);

// Intersection of two AABBs (quick overlap check for oriented rects).
// Returns true if they intersect, optionally filling the intersection rect.
bool GetWorldIntersectionAABB(const RectXform* a, const RectXform* b, Rectangle* out);

#endif
