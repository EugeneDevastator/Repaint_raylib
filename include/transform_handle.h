#ifndef TRANSFORM_HANDLE_H
#define TRANSFORM_HANDLE_H

#include "xform.h"
#include "raylib.h"
#include "ui_rect.h"

// ── Shared transform handle for both layer transform and canvas crop ──
// Operates on a RectXform* (matrix + world-unit extent) and a world-space
// cursor point.  Handles translate, scale, rotate, and cursor drag.
//
// Internal drag state is static (managed by the module).  Call exactly once
// per frame in each phase.
//
// The "cursor" is a user-draggable visual handle that serves as the center
// of rotation (both modes) and scaling (layer mode).  It is independent of
// the xform's mathematical pivot (always local (0,0)).

// ── Input ────────────────────────────────────────────────────────────
// cursor      : in/out — world-space rotation center / visual handle.
// scaleProportionalToCursor : false = corner-extent scale (crop),
//                              true  = scale proportional to cursor (layer)
// lockAspect  : if true, locks the width/height ratio during resize
// Returns true if xform was modified.
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
                           const DrawRect* rect);

// ── Draw ─────────────────────────────────────────────────────────────
void TransformHandle_Draw(const RectXform* xform,
                          Vector2 cursor,
                          const Camera2D* camera);

// ── Reset internal drag state ───────────────────────────────────────
void TransformHandle_ResetState(void);

// ── Repeat last applied transform around a world-space pivot ───────
void TransformHandle_RepeatLast(RectXform* xform, Vector2 pivot);

// ── Get / set the stored transform matrix (last repeat) ────────────
void  TransformHandle_GetStore(float mat[6]);
void  TransformHandle_SetStore(const float mat[6]);

#endif
