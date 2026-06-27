#pragma once
#include <stdint.h>
#include <stddef.h>

typedef struct OnnxModel OnnxModel;

OnnxModel* onnx_load(const char* path);
void onnx_unload(OnnxModel* model);
void onnx_print_info(OnnxModel* model);

bool onnx_run(OnnxModel* model,
              const char* const* in_names,
              const float* const* in_data,
              const int64_t* in_shapes,
              int num_in,
              const char* const* out_names,
              float** out_data,
              int64_t* out_shapes,
              int num_out);
