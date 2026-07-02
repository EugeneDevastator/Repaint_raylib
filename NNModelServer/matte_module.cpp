#include "matte_module.h"
#include "nn_onnx.h"

#define STB_IMAGE_IMPLEMENTATION
#define STB_IMAGE_WRITE_IMPLEMENTATION
#include "stb_image.h"
#include "stb_image_write.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include <vector>

/* ── ImageNet normalisation (used by current model) ─────────────────────────── */

static const float MEAN[3] = {0.485f, 0.456f, 0.406f};
static const float STD[3]  = {0.229f, 0.224f, 0.225f};

/* ── resize helpers ─────────────────────────────────────────────────────────── */

static void resize_rgba(const uint8_t* src, int sw, int sh,
                        uint8_t* dst, int dw, int dh) {
    float xr = (float)sw / dw, yr = (float)sh / dh;
    for (int dy = 0; dy < dh; dy++) {
        float sy = dy * yr;
        int iy0 = (int)sy, iy1 = (iy0 + 1 < sh) ? iy0 + 1 : iy0;
        float fy = sy - iy0;
        for (int dx = 0; dx < dw; dx++) {
            float sx = dx * xr;
            int ix0 = (int)sx, ix1 = (ix0 + 1 < sw) ? ix0 + 1 : ix0;
            float fx = sx - ix0;
            int di = (dy * dw + dx) * 4;
            for (int c = 0; c < 4; c++) {
                float v = src[(iy0*sw+ix0)*4+c] * (1-fx)*(1-fy)
                        + src[(iy0*sw+ix1)*4+c] * fx*(1-fy)
                        + src[(iy1*sw+ix0)*4+c] * (1-fx)*fy
                        + src[(iy1*sw+ix1)*4+c] * fx*fy;
                dst[di+c] = (uint8_t)(v + 0.5f);
            }
        }
    }
}

static void resize_gray(const uint8_t* src, int sw, int sh,
                        uint8_t* dst, int dw, int dh) {
    float xr = (float)sw / dw, yr = (float)sh / dh;
    for (int dy = 0; dy < dh; dy++) {
        float sy = dy * yr;
        int iy0 = (int)sy, iy1 = (iy0 + 1 < sh) ? iy0 + 1 : iy0;
        float fy = sy - iy0;
        for (int dx = 0; dx < dw; dx++) {
            float sx = dx * xr;
            int ix0 = (int)sx, ix1 = (ix0 + 1 < sw) ? ix0 + 1 : ix0;
            float fx = sx - ix0;
            float v = src[iy0*sw+ix0] * (1-fx)*(1-fy)
                    + src[iy0*sw+ix1] * fx*(1-fy)
                    + src[iy1*sw+ix0] * (1-fx)*fy
                    + src[iy1*sw+ix1] * fx*fy;
            dst[dy*dw+dx] = (uint8_t)(v + 0.5f);
        }
    }
}

/* ── public API ─────────────────────────────────────────────────────────────── */

bool matte_init(MatteModel* model, OnnxModel* onnx) {
    model->onnx = onnx;
    fprintf(stderr, "[matte] input: pixel_values, output: alphas\n");
    onnx_print_info(onnx);
    return true;
}

bool matte_process(MatteModel* model,
                   const std::vector<uint8_t>& rgb_png,
                   const std::vector<uint8_t>& tri_png,
                   std::vector<uint8_t>& out_alpha_png,
                   matte_progress_t progress,
                   void* progress_user) {
    int rgb_w = 0, rgb_h = 0, tri_w = 0, tri_h = 0;

    if (progress) progress("Step 1/4: Decoding images...", progress_user);

    std::vector<uint8_t> rgb_rgba, tri_rgba;
    {   int ch;
        unsigned char* p = stbi_load_from_memory(
            rgb_png.data(), (int)rgb_png.size(), &rgb_w, &rgb_h, &ch, 4);
        if (!p) { fprintf(stderr, "[matte] rgb decode failed\n"); return false; }
        rgb_rgba.assign(p, p + (size_t)rgb_w * rgb_h * 4);
        stbi_image_free(p);
    }
    {   int ch;
        unsigned char* p = stbi_load_from_memory(
            tri_png.data(), (int)tri_png.size(), &tri_w, &tri_h, &ch, 4);
        if (!p) { fprintf(stderr, "[matte] trimap decode failed\n"); return false; }
        tri_rgba.assign(p, p + (size_t)tri_w * tri_h * 4);
        stbi_image_free(p);
    }

    int orig_w = rgb_w, orig_h = rgb_h;
    fprintf(stderr, "  image size: %dx%d\n", orig_w, orig_h);

    if (progress) progress("Step 2/4: Posterizing trimap...", progress_user);

    std::vector<uint8_t> trimap((size_t)tri_w * tri_h, 0);
    for (int i = 0; i < tri_w * tri_h; i++) {
        uint8_t v = tri_rgba[i*4];
        trimap[i] = (v >= 192) ? 255 : (v >= 64) ? 128 : 0;
    }

    int mw = (orig_w / 32) * 32; if (mw < 32) mw = 32;
    int mh = (orig_h / 32) * 32; if (mh < 32) mh = 32;

    std::vector<uint8_t> rsz_rgb((size_t)mw * mh * 4, 0);
    std::vector<uint8_t> rsz_tri((size_t)mw * mh, 0);
    resize_rgba(rgb_rgba.data(), rgb_w, rgb_h, rsz_rgb.data(), mw, mh);
    resize_gray(trimap.data(), tri_w, tri_h, rsz_tri.data(), mw, mh);

    if (progress) progress("Step 3/4: Running model inference...", progress_user);

    size_t np = (size_t)mw * mh;
    std::vector<float> pixels(np * 4, 0);

    for (size_t i = 0; i < np; i++) {
        float r = rsz_rgb[i*4]   / 255.0f;
        float g = rsz_rgb[i*4+1] / 255.0f;
        float b = rsz_rgb[i*4+2] / 255.0f;
        pixels[i]          = (r - MEAN[0]) / STD[0];
        pixels[np+i]       = (g - MEAN[1]) / STD[1];
        pixels[np*2+i]     = (b - MEAN[2]) / STD[2];
        pixels[np*3+i]     = rsz_tri[i] / 255.0f;
    }

    const char* in_names[1]  = {"pixel_values"};
    int64_t     in_shapes[4] = {1, 4, mh, mw};
    const float* in_data[1]  = {pixels.data()};

    const char* out_names[1] = {"alphas"};
    float*      out_data[1]  = {nullptr};
    int64_t     out_shapes[4] = {0};

    if (!onnx_run(model->onnx, in_names, in_data, in_shapes, 1,
                  out_names, out_data, out_shapes, 1)) {
        fprintf(stderr, "[matte] inference failed\n");
        return false;
    }

    int64_t oh = out_shapes[2], ow = out_shapes[3];

    if (progress) progress("Step 4/4: Encoding result...", progress_user);

    std::vector<uint8_t> alpha((size_t)ow * oh, 0);
    for (int64_t i = 0; i < ow * oh; i++) {
        float v = out_data[0][i];
        if (v < 0.0f) v = 0.0f;
        if (v > 1.0f) v = 1.0f;
        alpha[i] = (uint8_t)(v * 255.0f + 0.5f);
    }
    free(out_data[0]);

    std::vector<uint8_t> final_alpha((size_t)orig_w * orig_h, 0);
    resize_gray(alpha.data(), (int)ow, (int)oh, final_alpha.data(), orig_w, orig_h);

    int png_len = 0;
    unsigned char* png = stbi_write_png_to_mem(
        final_alpha.data(), orig_w, orig_w, orig_h, 1, &png_len);
    if (!png) { fprintf(stderr, "[matte] PNG encode failed\n"); return false; }

    out_alpha_png.assign(png, png + png_len);
    STBIW_FREE(png);

    fprintf(stderr, "[matte] done — alpha %dx%d\n", orig_w, orig_h);
    return true;
}
