#include "sd_module.h"
#include "stable-diffusion.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <vector>
#include "stb_image_write.h"

static void write_png_cb(void* context, void* data, int size) {
    auto* out = (std::vector<uint8_t>*)context;
    out->insert(out->end(), (uint8_t*)data, (uint8_t*)data + size);
}

static sd_ctx_t* g_ctx = nullptr;

bool sd_init(const char* model_path) {
    fprintf(stderr, "[sd] library: stable-diffusion.cpp commit %s\n", sd_commit());
    fprintf(stderr, "[sd] loading: %s\n", model_path);

    sd_ctx_params_t params;
    sd_ctx_params_init(&params);
    params.model_path = model_path;
    params.n_threads  = 8;
    params.wtype      = SD_TYPE_F32;

    g_ctx = new_sd_ctx(&params);
    if (!g_ctx) {
        fprintf(stderr, "[sd] init failed\n");
        return false;
    }

    fprintf(stderr, "[sd] init OK\n");
    return true;
}

bool sd_generate(const char* prompt, int w, int h, std::vector<uint8_t>& out_png) {
    if (!g_ctx) {
        fprintf(stderr, "[sd] not initialized\n");
        return false;
    }

    sd_img_gen_params_t p;
    sd_img_gen_params_init(&p);
    p.prompt        = prompt;
    p.negative_prompt = "";
    p.width         = w;
    p.height        = h;
    p.seed          = -1;
    p.batch_count   = 1;

    p.sample_params.sample_steps       = 4;
    p.sample_params.sample_method      = LCM_SAMPLE_METHOD;
    p.sample_params.scheduler          = LCM_SCHEDULER;
    p.sample_params.guidance.txt_cfg   = 2.0f;

    fprintf(stderr, "[sd] generating \"%s\" %dx%d...\n", prompt, w, h);
    sd_image_t* result = generate_image(g_ctx, &p);
    if (!result) {
        fprintf(stderr, "[sd] generation failed\n");
        return false;
    }
    fprintf(stderr, "[sd] result %dx%d %d channels\n",
            result->width, result->height, result->channel);

    /* extract blue channel (index 2) → grayscale */
    int pw = (int)result->width, ph = (int)result->height;
    std::vector<uint8_t> blue((size_t)pw * ph);
    for (int y = 0; y < ph; y++)
        for (int x = 0; x < pw; x++)
            blue[y * pw + x] = result->data[(y * pw + x) * result->channel + 2];

    stbi_write_png_to_func(write_png_cb, &out_png, pw, ph, 1, blue.data(), pw);
    free_sd_images(result, 1);

    if (out_png.empty()) { fprintf(stderr, "[sd] PNG encode failed\n"); return false; }
    fprintf(stderr, "[sd] PNG %zu bytes\n", out_png.size());
    return true;
}
