#pragma once
#include <stddef.h>
#include <stdint.h>

int upscale_request(const uint8_t* src_png, size_t src_size,
                    uint8_t** out_png, size_t* out_size);
