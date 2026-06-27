#pragma once
#include <vector>
#include <stdint.h>
#include <stddef.h>
#include "nn_onnx.h"

struct MatteModel {
    OnnxModel* onnx;
};

typedef void (*matte_progress_t)(const char* msg, void* user);

bool matte_init(MatteModel* model, OnnxModel* onnx);

bool matte_process(MatteModel* model,
                   const std::vector<uint8_t>& rgb_png,
                   const std::vector<uint8_t>& tri_png,
                   std::vector<uint8_t>& out_alpha_png,
                   matte_progress_t progress,
                   void* progress_user);
