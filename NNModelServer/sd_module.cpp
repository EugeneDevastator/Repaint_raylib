#include "sd_module.h"
#include "stable-diffusion.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static bool file_exists(const char* path) {
    FILE* f = fopen(path, "rb");
    if (f) { fclose(f); return true; }
    return false;
}

static bool try_init(const char* model_path) {
    fprintf(stderr, "[sd] trying model: %s\n", model_path);

    sd_ctx_params_t params;
    sd_ctx_params_init(&params);
    params.model_path = model_path;
    params.n_threads  = 4;
    params.wtype      = SD_TYPE_F32;

    sd_ctx_t* ctx = new_sd_ctx(&params);
    if (!ctx) {
        fprintf(stderr, "[sd] init failed\n");
        return false;
    }

    fprintf(stderr, "[sd] init OK — handshake successful\n");
    free_sd_ctx(ctx);
    return true;
}

void sd_try_load() {
    fprintf(stderr, "[sd] library: stable-diffusion.cpp commit %s\n", sd_commit());
    fprintf(stderr, "[sd] looking for dreamshaper_8LCM.safetensors...\n");

    const char* search_paths[] = {
        "../NNModelServer/onnx/dreamshaper_8LCM.safetensors",
        "../NNModelServer/onnx/vitmatte_model_vitsmall_dist646/dreamshaper_8LCM.safetensors",
        "dreamshaper_8LCM.safetensors",
        nullptr
    };

    for (int i = 0; search_paths[i]; i++) {
        if (file_exists(search_paths[i])) {
            try_init(search_paths[i]);
            return;
        }
    }

    fprintf(stderr, "[sd] dreamshaper_8LCM.safetensors not found\n");
    fprintf(stderr, "[sd] place it in NNModelServer/onnx/ to test SD init\n");
}
