#ifndef XFORM_H
#define XFORM_H

#include "raylib.h"

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

// ── RectXform — oriented rectangle in world-space ────────────────
// mat maps from local space (pivot at origin) to world space.
// w,h are the extent in world units — purely metadata, the matrix
// does not depend on them.  When rot=0 and pivot is top-left:
// the rectangle covers (cx,cy) .. (cx+w, cy+h).
typedef struct {
    float mat[6];
    float w, h;
} RectXform;

// Build mat = translate(cx,cy) · rotate(rot).  (cx,cy) is the
// pivot (local origin) position in world space.
RectXform RectXform_Pivot(float cx, float cy, float w, float h, float rot);

// Center = mat applied to local origin (0,0) = (mat[2], mat[5])
static inline void RectXform_GetCenter(const RectXform* rx, float* cx, float* cy) {
    *cx = rx->mat[2]; *cy = rx->mat[5];
}

// Rotation angle = angle of mat's x-axis in world space
float RectXform_GetRot(const RectXform* rx);

// ── World-space helpers ──────────────────────────────────────────

// Axis-aligned bounding box of an oriented rectangle in world space
Rectangle GetWorldAABB(const RectXform* rx);

// Intersection of two AABBs (quick overlap check for oriented rects).
// Returns true if they intersect, optionally filling the intersection rect.
bool GetWorldIntersectionAABB(const RectXform* a, const RectXform* b, Rectangle* out);

// Convert world coordinates to pixel coordinates at the world origin.
// pixel = world * ppu
static inline Vector2 GetPixelCoord(float worldX, float worldY, float ppu) {
    return Vector2{worldX * ppu, worldY * ppu};
}

#endif
