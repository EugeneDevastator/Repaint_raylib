#pragma once
#include "repaint.h"

/* .re.png file format:
 *
 * [Composite PNG bytes]     ← all visible layers flattened (normal α-blend)
 * [Magic  "REPAINT" (8)]
 * [Version uint32_t]
 * [Width   uint32_t]
 * [Height  uint32_t]
 * [Layers  uint32_t]
 * [For each layer bottom→top]:
 *   [PropSize uint32_t][sLayerProps data]
 *   [PngSize  uint32_t][layer PNG data]
 *
 * Composite PNG makes the file viewable in any image viewer.
 * All integer values are little-endian.
 */

bool SaveRePaint(const char* path, Canvas* canvas);
bool LoadRePaint(const char* path, Canvas* canvas);
