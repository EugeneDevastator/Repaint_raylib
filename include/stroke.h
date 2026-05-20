#ifndef STROKE_H
#define STROKE_H

#include "repaint.h"

// Linear interpolation: from + (to - from) * k
float Stroke_Lerp(float from, float to, float k);

// Blend all interpolatable fields between two RealBrush states
d_RealBrush Stroke_BlendBrushes(d_RealBrush from, d_RealBrush to, float k);

// Deterministic pseudo-random value in [0, 1) from seed
float Stroke_RawRnd(uint16_t seed, float range);

#endif
