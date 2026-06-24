#pragma once
#include <stddef.h>
#include <stdint.h>

int matte_request(
    const uint8_t* rgb_png,  size_t rgb_size,
    const uint8_t* tri_png,  size_t tri_size,
    uint8_t** out_png, size_t* out_size);
