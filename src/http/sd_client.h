#pragma once
#include <stddef.h>
#include <stdint.h>

typedef void (*SDProgressFn)(const char* msg);

int sd_request(const char* prompt, float strength, float cfg, int steps,
               int w, int h,
               uint8_t** out_png, size_t* out_size,
               SDProgressFn progress_fn);
