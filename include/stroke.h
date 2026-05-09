#ifndef STROKE_H
#define STROKE_H

#include "repaint.h"

// Unpack a d_Section into individual dabs with interpolated brush parameters
// along the stroke segment. Handles dynamic spacing based on changing radius,
// scatter jitter, and per-dab noise seeding.
//
// section: stroke segment with BrushFrom->Brush interpolation range
// out_positions: output array of dab positions (max max_dabs)
// out_brushes: output array of per-dab brush states (max max_dabs)
// max_dabs: capacity of output arrays
// Returns number of dabs generated.
int Stroke_UnpackSection(
    d_Section* section,
    Vector2* out_positions,
    d_Brush* out_brushes,
    int max_dabs
);

// Linear interpolation: from + (to - from) * k
float Stroke_Lerp(float from, float to, float k);

// Blend all interpolatable fields between two RealBrush states
d_RealBrush Stroke_BlendBrushes(d_RealBrush from, d_RealBrush to, float k);

// Deterministic pseudo-random value in [0, 1) from seed
float Stroke_RawRnd(uint16_t seed, float range);

#endif
