#pragma once
#include <vector>
#include <stdint.h>

bool sd_init(const char* model_path);
bool sd_generate(const char* prompt, int w, int h, std::vector<uint8_t>& out_png);
