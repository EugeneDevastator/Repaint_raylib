#pragma once
#include <vector>
#include <string>
#include <stdint.h>

bool sd_init(const char* model_path);

bool sd_generate(const std::string& prompt,
                 const std::vector<uint8_t>& source_png,
                 float strength, float cfg, int steps,
                 int w, int h,
                 std::vector<uint8_t>& out_png);
