#ifndef RENDER_UTILS_H
#define RENDER_UTILS_H

#include "raylib.h"

unsigned int CreateTexRGBA16(int w, int h);

RenderTexture2D Load16BitRT(int width, int height);

#endif
