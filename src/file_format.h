#pragma once
#include "repaint.h"

// ── .re.png native file format ──────────────────────────────────────
//
// Binary layout (all integers little-endian):
//
//   [0 … N)        Composite preview PNG (8-bit dithered, for file thumbnails)
//   [N … N+8)      MAGIC "REPAINT"
//   [N+8 … N+12)   FILE_VER (uint32)
//                     3 = original (8-bit PNG per layer, no transforms)
//                     4 = added mat[6] transform per layer
//                     5 = added layerW/H, pixelDepth field
//   [N+12 … N+16)   Canvas width  (uint32)
//   [N+16 … N+20)   Canvas height (uint32)
//   [N+20 … N+24)   Layer count   (uint32)
//   [N+24 … N+28)   pixelDepth    (uint32, v5+)
//                       0 = legacy 8-bit PNG per layer  (v3/v4)
//                       1 = raw R16G16B16A16 per layer   (v5)
//
//   For each layer (bottom → top):
//     [0 … 4)       Props byte count P (uint32)
//     [4 … 4+P)     sLayerProps data (via _writeProps)
//     [4+P … 8+P)   Pixel data byte count D (uint32)
//     [8+P … 8+P+D) Pixel data:
//                     pixelDepth==0 → D = PNG compressed bytes (8-bit)
//                     pixelDepth==1 → D = layerW × layerH × 8 raw R16G16B16A16
//
// Notes:
//   - The composite preview at offset 0 makes .re.png viewable in any
//     image viewer as a flat preview of the full canvas.
//   - v3/v4 files have pixelDepth=0 by omission. v5+ files with
//     pixelDepth=1 store exact 16-bit GPU-precision pixel data with
//     no 16→8→16 round-trip loss.

bool SaveRePaint(const char* path, Canvas* canvas, AppState* state);
bool LoadRePaint(const char* path, Canvas* canvas, AppState* state);
