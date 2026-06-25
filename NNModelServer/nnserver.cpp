#include "sock_platform.h"
#ifdef __MINGW32__
# define _Return_type_success_(x)  /* SAL annotation, not needed for GCC */
#endif
#include "onnxruntime_c_api.h"

#define STB_IMAGE_IMPLEMENTATION
#define STB_IMAGE_WRITE_IMPLEMENTATION
#include "stb_image.h"
#include "stb_image_write.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <math.h>
#include <vector>
#include <string>

#ifdef _WIN32
# include <windows.h>
#endif

#ifdef _MSC_VER
# define strdup _strdup
#endif

/* ── protocol helpers (big-endian length prefixes) ──────────────────────────── */

static void write32be(uint8_t* buf, uint32_t v) {
    buf[0] = (uint8_t)(v >> 24);
    buf[1] = (uint8_t)(v >> 16);
    buf[2] = (uint8_t)(v >> 8);
    buf[3] = (uint8_t)(v);
}

static uint32_t read32be(const uint8_t* buf) {
    return ((uint32_t)buf[0] << 24) |
           ((uint32_t)buf[1] << 16) |
           ((uint32_t)buf[2] << 8)  |
           (uint32_t)buf[3];
}

static bool recv_blob(sock_t s, std::vector<uint8_t>& out) {
    uint8_t hdr[4];
    if (!sock_recv_all(s, hdr, 4)) return false;
    uint32_t len = read32be(hdr);
    out.resize(len);
    if (len > 0 && !sock_recv_all(s, &out[0], len)) return false;
    return true;
}

static bool send_msg(sock_t s, uint8_t type, const void* data, uint32_t len) {
    uint8_t hdr[4];
    write32be(hdr, len);
    if (!sock_send_all(s, &type, 1))     return false;
    if (!sock_send_all(s, hdr, 4))       return false;
    if (len > 0 && !sock_send_all(s, (const uint8_t*)data, len)) return false;
    return true;
}

/* ── ONNX Runtime (C API) globals ──────────────────────────────────────────── */

static const OrtApi*   g_api     = nullptr;
static OrtEnv*         g_env     = nullptr;
static OrtMemoryInfo*  g_mem     = nullptr;
static OrtSession*     g_sess    = nullptr;
static const char**    g_inames  = nullptr;
static const char**    g_onames  = nullptr;
static size_t          g_nin     = 0;
static size_t          g_nout    = 0;

static bool ort_load(const char* path) {
    g_api = OrtGetApiBase()->GetApi(ORT_API_VERSION);
    if (!g_api) { fprintf(stderr, "ORT GetApi failed\n"); return false; }

    OrtStatus* st = nullptr;

    st = g_api->CreateEnv(ORT_LOGGING_LEVEL_WARNING, "nnserver", &g_env);
    if (st) { fprintf(stderr, "ORT env: %s\n", g_api->GetErrorMessage(st)); g_api->ReleaseStatus(st); return false; }

    /* load model — on Windows ORTCHAR_T is wchar_t, convert path */
#ifdef _WIN32
    int wlen = MultiByteToWideChar(CP_UTF8, 0, path, -1, nullptr, 0);
    std::wstring wpath((size_t)wlen, L'\0');
    MultiByteToWideChar(CP_UTF8, 0, path, -1, &wpath[0], wlen);
#else
    const char* wpath = path;
#endif

    OrtSessionOptions* opts = nullptr;
    st = g_api->CreateSessionOptions(&opts);
    if (st) { fprintf(stderr, "ORT opts: %s\n", g_api->GetErrorMessage(st)); g_api->ReleaseStatus(st); return false; }
    g_api->SetSessionGraphOptimizationLevel(opts, ORT_ENABLE_BASIC);

    st = g_api->CreateSession(g_env, wpath.c_str(), opts, &g_sess);
    g_api->ReleaseSessionOptions(opts);
    if (st) { fprintf(stderr, "ORT sess: %s\n", g_api->GetErrorMessage(st)); g_api->ReleaseStatus(st); return false; }

    st = g_api->CreateMemoryInfo("Cpu", OrtDeviceAllocator, -1, OrtMemTypeDefault, &g_mem);
    if (st) { fprintf(stderr, "ORT mem: %s\n", g_api->GetErrorMessage(st)); g_api->ReleaseStatus(st); return false; }

    OrtAllocator* alloc = nullptr;
    st = g_api->GetAllocatorWithDefaultOptions(&alloc);
    if (st) { fprintf(stderr, "ORT alloc: %s\n", g_api->GetErrorMessage(st)); g_api->ReleaseStatus(st); return false; }

    g_api->SessionGetInputCount(g_sess, &g_nin);
    g_api->SessionGetOutputCount(g_sess, &g_nout);

    g_inames = (const char**)calloc(g_nin,  sizeof(char*));
    g_onames = (const char**)calloc(g_nout, sizeof(char*));

    for (size_t i = 0; i < g_nin; i++) {
        char* n; g_api->SessionGetInputName(g_sess, i, alloc, &n);
        g_inames[i] = strdup(n);
        /* print input shape info */
        OrtTypeInfo* ti = nullptr;
        g_api->SessionGetInputTypeInfo(g_sess, i, &ti);
        const OrtTensorTypeAndShapeInfo* tsi = nullptr;
        g_api->CastTypeInfoToTensorInfo(ti, &tsi);
        size_t nd = 0;
        g_api->GetDimensionsCount(tsi, &nd);
        int64_t dims[4] = {0};
        g_api->GetDimensions(tsi, dims, 4);
        g_api->ReleaseTypeInfo(ti);
        fprintf(stderr, "  input[%zu] \"%s\" shape=[%ld,%ld,%ld,%ld] ndims=%zu\n",
                i, n, (long)dims[0], (long)dims[1], (long)dims[2], (long)dims[3], nd);
        g_api->AllocatorFree(alloc, n);
    }
    for (size_t i = 0; i < g_nout; i++) {
        char* n; g_api->SessionGetOutputName(g_sess, i, alloc, &n);
        g_onames[i] = strdup(n);
        OrtTypeInfo* ti = nullptr;
        g_api->SessionGetOutputTypeInfo(g_sess, i, &ti);
        const OrtTensorTypeAndShapeInfo* tsi = nullptr;
        g_api->CastTypeInfoToTensorInfo(ti, &tsi);
        size_t nd = 0;
        g_api->GetDimensionsCount(tsi, &nd);
        int64_t dims[4] = {0};
        g_api->GetDimensions(tsi, dims, 4);
        g_api->ReleaseTypeInfo(ti);
        fprintf(stderr, "  output[%zu] \"%s\" shape=[%ld,%ld,%ld,%ld] ndims=%zu\n",
                i, n, (long)dims[0], (long)dims[1], (long)dims[2], (long)dims[3], nd);
        g_api->AllocatorFree(alloc, n);
    }

    fprintf(stderr, "[nnserver] model loaded\n");
    return true;
}

static void ort_unload() {
    if (g_inames) { for (size_t i = 0; i < g_nin; i++) free((void*)g_inames[i]); free(g_inames); }
    if (g_onames) { for (size_t i = 0; i < g_nout; i++) free((void*)g_onames[i]); free(g_onames); }
    if (g_sess) g_api->ReleaseSession(g_sess);
    if (g_mem)  g_api->ReleaseMemoryInfo(g_mem);
    if (g_env)  g_api->ReleaseEnv(g_env);
}

/* ── image helpers ──────────────────────────────────────────────────────────── */

static bool decode_png(const std::vector<uint8_t>& in, int& w, int& h,
                       std::vector<uint8_t>& out_rgba) {
    int ch;
    unsigned char* p = stbi_load_from_memory(in.data(), (int)in.size(), &w, &h, &ch, 4);
    if (!p) return false;
    out_rgba.assign(p, p + (size_t)w * h * 4);
    stbi_image_free(p);
    return true;
}

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

/* ── ImageNet normalisation (VitMatteImageProcessor) ─────────────────────────── */

static const float MEAN[3] = {0.485f, 0.456f, 0.406f};
static const float STD[3]  = {0.229f, 0.224f, 0.225f};

/* ── connection handler (runs entire pipeline for one client) ───────────────── */

static void handle_client(sock_t client) {
    /* all scoped variables declared upfront so goto never crosses initialisation */
    std::vector<uint8_t> rgb_blob, tri_blob, rgb_rgba, tri_rgba;
    std::vector<uint8_t> trimap, rsz_rgb, rsz_tri;
    std::vector<float> pixels;
    std::vector<uint8_t> alpha, final_alpha;
    unsigned char* png = nullptr;
    int png_len = 0;
    int rgb_w = 0, rgb_h = 0, tri_w = 0, tri_h = 0;
    int orig_w = 0, orig_h = 0;
    int mw = 0, mh = 0;
    size_t np = 0, oh = 0, ow = 0;
    OrtValue *in_img = nullptr, *out = nullptr;
    float* out_data = nullptr;
    OrtTensorTypeAndShapeInfo* info = nullptr;
    bool ok = false;
    int64_t img_shape[4] = {0}, out_shape[4] = {0};

    /* 1. receive both blobs */
    if (!recv_blob(client, rgb_blob) || !recv_blob(client, tri_blob)) {
        fprintf(stderr, "[handler] recv failed\n"); return;
    }
    fprintf(stderr, "[handler] rgb %zu bytes, trimap %zu bytes\n",
            rgb_blob.size(), tri_blob.size());

    auto progress = [&](const char* msg) {
        fprintf(stderr, "  %s\n", msg);
        send_msg(client, 'P', msg, (uint32_t)strlen(msg));
    };

    /* 2. decode PNGs */
    progress("Step 1/4: Decoding images...");
    if (!decode_png(rgb_blob, rgb_w, rgb_h, rgb_rgba) ||
        !decode_png(tri_blob, tri_w, tri_h, tri_rgba)) {
        fprintf(stderr, "[handler] decode failed\n"); return;
    }
    orig_w = rgb_w; orig_h = rgb_h;
    fprintf(stderr, "  image size: %dx%d\n", orig_w, orig_h);

    /* 3. posterize trimap (R channel → 0/128/255) */
    progress("Step 2/4: Posterizing trimap...");
    trimap.assign((size_t)tri_w * tri_h, 0);
    for (int i = 0; i < tri_w * tri_h; i++) {
        uint8_t v = tri_rgba[i*4];
        trimap[i] = (v >= 192) ? 255 : (v >= 64) ? 128 : 0;
    }

    /* 4. resize to multiple-of-32 */
    mw = (orig_w / 32) * 32; if (mw < 32) mw = 32;
    mh = (orig_h / 32) * 32; if (mh < 32) mh = 32;

    rsz_rgb.assign((size_t)mw * mh * 4, 0);
    rsz_tri.assign((size_t)mw * mh, 0);
    resize_rgba(rgb_rgba.data(), rgb_w, rgb_h, rsz_rgb.data(), mw, mh);
    resize_gray(trimap.data(), tri_w, tri_h, rsz_tri.data(), mw, mh);
    fprintf(stderr, "  resized to: %dx%d\n", mw, mh);

    /* 5. build 4-channel float input (RGB normalized + trimap) */
    progress("Step 3/4: Running model inference...");
    np = (size_t)mw * mh;
    pixels.assign(np * 4, 0);

    for (size_t i = 0; i < np; i++) {
        float r = rsz_rgb[i*4]   / 255.0f;
        float g = rsz_rgb[i*4+1] / 255.0f;
        float b = rsz_rgb[i*4+2] / 255.0f;
        pixels[i]          = (r - MEAN[0]) / STD[0];
        pixels[np+i]       = (g - MEAN[1]) / STD[1];
        pixels[np*2+i]     = (b - MEAN[2]) / STD[2];
        pixels[np*3+i]     = rsz_tri[i] / 255.0f;
    }

    /* 6. run ONNX session */
    img_shape[0] = 1; img_shape[1] = 4; img_shape[2] = mh; img_shape[3] = mw;

    {   OrtStatus* st;
        if ((st = g_api->CreateTensorWithDataAsOrtValue(
                g_mem, pixels.data(), np * 4 * sizeof(float),
                img_shape, 4, ONNX_TENSOR_ELEMENT_DATA_TYPE_FLOAT, &in_img))) {
            fprintf(stderr, "ORT: CreateTensor: %s\n", g_api->GetErrorMessage(st));
            g_api->ReleaseStatus(st); goto cleanup; }
    }

    {
        const OrtValue* ort_in[1] = {in_img};
        OrtValue* ort_out[1] = {nullptr};
        OrtStatus* st;
        if ((st = g_api->Run(g_sess, nullptr, g_inames, ort_in, g_nin,
                             g_onames, 1, ort_out))) {
            fprintf(stderr, "ORT: Run: %s\n", g_api->GetErrorMessage(st));
            g_api->ReleaseStatus(st); goto cleanup;
        }
        out = ort_out[0];
    }

    /* 7. read output tensor */
    {   OrtStatus* st;
        if ((st = g_api->GetTensorMutableData(out, (void**)&out_data))) {
            fprintf(stderr, "ORT: GetTensorMutableData: %s\n", g_api->GetErrorMessage(st));
            g_api->ReleaseStatus(st); goto cleanup; }
        if ((st = g_api->GetTensorTypeAndShape(out, &info))) {
            fprintf(stderr, "ORT: GetTensorTypeAndShape: %s\n", g_api->GetErrorMessage(st));
            g_api->ReleaseStatus(st); goto cleanup; }
    }

    g_api->GetDimensions(info, out_shape, 4);
    g_api->ReleaseTensorTypeAndShapeInfo(info); info = nullptr;
    oh = (size_t)out_shape[2]; ow = (size_t)out_shape[3];

    /* 8. extract alpha, rescale to original size */
    progress("Step 4/4: Encoding result...");
    alpha.assign((size_t)ow * oh, 0);
    for (size_t i = 0; i < (size_t)ow * oh; i++) {
        float v = out_data[i];
        if (v < 0.0f) v = 0.0f;
        if (v > 1.0f) v = 1.0f;
        alpha[i] = (uint8_t)(v * 255.0f + 0.5f);
    }

    final_alpha.assign((size_t)orig_w * orig_h, 0);
    resize_gray(alpha.data(), (int)ow, (int)oh, final_alpha.data(), orig_w, orig_h);

    /* 9. encode result PNG and send */
    png = stbi_write_png_to_mem(final_alpha.data(), orig_w, orig_w, orig_h, 1, &png_len);
    if (!png) { fprintf(stderr, "[handler] PNG encode failed\n"); goto cleanup; }

    ok = send_msg(client, 'R', png, (uint32_t)png_len);
    STBIW_FREE(png); png = nullptr;
    if (ok) fprintf(stderr, "[handler] done — sent %d bytes\n", png_len);

cleanup:
    if (info)   g_api->ReleaseTensorTypeAndShapeInfo(info);
    if (in_img) g_api->ReleaseValue(in_img);
    if (out)    g_api->ReleaseValue(out);
}

/* ── main ───────────────────────────────────────────────────────────────────── */

int main(int argc, char** argv) {
    setvbuf(stderr, nullptr, _IONBF, 0);
    fprintf(stderr, "[nnserver] starting...\n");

    const char* model_path = nullptr;
    int         port       = 8000;

    for (int i = 1; i < argc; i++) {
        if      (strcmp(argv[i], "--port")  == 0 && i+1 < argc) port = atoi(argv[++i]);
        else if (strcmp(argv[i], "--model") == 0 && i+1 < argc) model_path = argv[++i];
        else if (strcmp(argv[i], "--help")  == 0) {
            fprintf(stderr, "Usage: nnserver [--port PORT] [--model PATH]\n"); return 0;
        }
    }

    if (!model_path) {
        const char* try_paths[] = {
            "NNModelServer/models/vitmatte_model_vitsmall_dist646.onnx",
            "../NNModelServer/models/vitmatte_model_vitsmall_dist646.onnx",
            "vitmatte_model_vitsmall_dist646.onnx",
            "models/vitmatte_model_vitsmall_dist646.onnx",
        };
        for (auto c : try_paths) {
            FILE* f = fopen(c, "rb");
            if (f) { fclose(f); model_path = c; break; }
        }
        if (!model_path) {
            fprintf(stderr, "[nnserver] model not found, use --model PATH\n");
            return 1;
        }
    }
    fprintf(stderr, "[nnserver] model: %s\n", model_path);

    if (!sock_init()) { fprintf(stderr, "sock_init: %s\n", sock_last_error()); return 1; }
    if (!ort_load(model_path)) { sock_shutdown(); return 1; }

    sock_t listener = sock_listen(port);
    if (listener == SOCK_INVALID) {
        fprintf(stderr, "listen(%d): %s\n", port, sock_last_error());
        ort_unload(); sock_shutdown(); return 1;
    }
    fprintf(stderr, "[nnserver] listening on 127.0.0.1:%d\n", port);

    while (true) {
        sock_t client = sock_accept(listener);
        if (client == SOCK_INVALID) { fprintf(stderr, "accept: %s\n", sock_last_error()); continue; }
        handle_client(client);
        sock_close(client);
    }

    /* unreachable */
}
