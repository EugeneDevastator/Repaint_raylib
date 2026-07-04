#include "sd_module.h"
#include "stable-diffusion.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <vector>
#include <string>
#include "stb_image.h"
#include "stb_image_write.h"

static sd_ctx_t* g_ctx = nullptr;

static void write_png_cb(void* context, void* data, int size) {
    auto* out = (std::vector<uint8_t>*)context;
    out->insert(out->end(), (uint8_t*)data, (uint8_t*)data + size);
}

bool sd_init(const char* model_path) {
    fprintf(stderr, "[sd] library: stable-diffusion.cpp commit %s\n", sd_commit());
    fprintf(stderr, "[sd] loading: %s\n", model_path);

    const char* backends[] = {"vulkan", "cpu"};
    g_ctx = nullptr;

    for (int i = 0; i < 2 && !g_ctx; i++) {
        sd_ctx_params_t params;
        sd_ctx_params_init(&params);
        params.model_path = model_path;
        params.n_threads  = 8;
        params.wtype      = SD_TYPE_F32;
        params.backend    = backends[i];
        params.params_backend = backends[i];
        g_ctx = new_sd_ctx(&params);
        if (g_ctx)
            fprintf(stderr, "[sd] backend: %s\n", backends[i]);
    }

    if (!g_ctx) {
        fprintf(stderr, "[sd] init failed\n");
        return false;
    }

    fprintf(stderr, "[sd] system: %s\n", sd_get_system_info());
    return true;
}

bool sd_generate(const std::string& prompt,
                 const std::vector<uint8_t>& source_png,
                 float strength, float cfg, int steps,
                 int w, int h,
                 std::vector<uint8_t>& out_png) {
    if (!g_ctx) {
        fprintf(stderr, "[sd] not initialized\n");
        return false;
    }

    sd_img_gen_params_t p;
    sd_img_gen_params_init(&p);
    p.prompt        = prompt.c_str();
    p.negative_prompt = "";
    p.width         = w;
    p.height        = h;
    p.strength      = strength;
    p.seed          = -1;
    p.batch_count   = 1;

    p.sample_params.sample_steps       = steps;
    p.sample_params.sample_method      = LCM_SAMPLE_METHOD;
    p.sample_params.scheduler          = LCM_SCHEDULER;
    p.sample_params.guidance.txt_cfg   = cfg;

    /* decode source image for img2img */
    sd_image_t src_img = {0, 0, 0, nullptr};
    if (!source_png.empty()) {
        int ch;
        unsigned char* data = stbi_load_from_memory(
            source_png.data(), (int)source_png.size(),
            (int*)&src_img.width, (int*)&src_img.height,
            &ch, 3);
        if (data) {
            src_img.channel = 3;
            src_img.data    = data;
            p.init_image    = src_img;
            fprintf(stderr, "[sd] img2img %dx%d strength=%.1f\n",
                    src_img.width, src_img.height, strength);
        } else {
            fprintf(stderr, "[sd] failed to decode source PNG\n");
        }
    }

    fprintf(stderr, "[sd] txt2img \"%s\" %dx%d steps=%d cfg=%.1f\n",
            prompt.c_str(), w, h, steps, cfg);

    sd_image_t* images = nullptr;
    int num_images = 0;
    bool ok = false;
#if SD_PREBUILT
    /* prebuilt library — old API */
    images = generate_image(g_ctx, &p);
    ok = (images != nullptr);
    num_images = ok ? 1 : 0;
#else
    /* FetchContent — new API */
    ok = generate_image(g_ctx, &p, &images, &num_images);
#endif
    if (src_img.data) stbi_image_free(src_img.data);

    if (!ok) {
        fprintf(stderr, "[sd] generation failed\n");
        return false;
    }
    sd_image_t* result = &images[0];
    fprintf(stderr, "[sd] result %dx%d %dch\n",
            result->width, result->height, result->channel);

    stbi_write_png_to_func(write_png_cb, &out_png,
                           (int)result->width, (int)result->height,
                           result->channel, result->data, 0);
    free_sd_images(images, num_images);

    if (out_png.empty()) {
        fprintf(stderr, "[sd] PNG encode failed\n");
        return false;
    }
    fprintf(stderr, "[sd] PNG %zu bytes\n", out_png.size());
    return true;
}
