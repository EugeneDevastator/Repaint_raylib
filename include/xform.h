#ifndef XFORM_H
#define XFORM_H

// ── 2×3 affine matrix (row-major: [a, b, tx, c, d, ty]) ─────────
// Pivot is always at (0,0). Matrix maps from local space to the
// coordinate space of its parent, independent of any w/h extent.

// out = a × b  (2×3 matrix multiplication)
void Xform_Mul     (float out[6], const float a[6], const float b[6]);
void Xform_Identity(float out[6]);
void Xform_SetTrans(float out[6], float tx, float ty);
void Xform_SetRot  (float out[6], float angle);  // CCW radians
void Xform_SetScale(float out[6], float sx, float sy);

// ── RectXform — oriented rectangle in world-space ────────────────
// mat maps from local space (pivot at origin) to world space.
// w,h are the extent in world units — purely metadata, the matrix
// does not depend on them.
typedef struct {
    float mat[6];
    float w, h;
} RectXform;

// Build mat = translate(cx,cy) · rotate(rot).  When rot=0:
//   mat = {1,0,cx, 0,1,cy}   — identity orientation at (cx,cy)
RectXform RectXform_Center(float cx, float cy, float w, float h, float rot);

// Center = mat applied to local origin (0,0) = (mat[2], mat[5])
static inline void RectXform_GetCenter(const RectXform* rx, float* cx, float* cy) {
    *cx = rx->mat[2]; *cy = rx->mat[5];
}

// Rotation angle = angle of mat's x-axis in world space
float RectXform_GetRot(const RectXform* rx);

#endif
