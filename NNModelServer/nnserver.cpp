#include "sock_platform.h"
#include "nn_downloader.h"
#include "nn_onnx.h"
#include "matte_module.h"
#include "sd_module.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <math.h>
#include <vector>
#include <string>

#include "stb_image.h"
#include "stb_image_write.h"

/* ── protocol helpers ──────────────────────────────────────────────────────── */

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

/* ── matte progress callback ──────────────────────────────────────────────── */

static void matte_progress(const char* msg, void* user) {
    sock_t client = (sock_t)(uintptr_t)user;
    fprintf(stderr, "  %s\n", msg);
    send_msg(client, 'P', msg, (uint32_t)strlen(msg));
}

/* ── matte handler ────────────────────────────────────────────────────────── */

static void handle_matte(sock_t client, MatteModel* matte, uint8_t first_len_byte) {
    std::vector<uint8_t> rgb_blob, tri_blob, alpha_png;

    uint8_t len_hdr[4] = {first_len_byte, 0, 0, 0};
    if (!sock_recv_all(client, &len_hdr[1], 3)) return;
    uint32_t rgb_len = read32be(len_hdr);
    rgb_blob.resize(rgb_len);
    if (rgb_len > 0 && !sock_recv_all(client, &rgb_blob[0], rgb_len)) return;

    if (!recv_blob(client, tri_blob)) {
        fprintf(stderr, "[handler] recv trimap failed\n"); return;
    }
    fprintf(stderr, "[handler] rgb %zu bytes, trimap %zu bytes\n",
            rgb_blob.size(), tri_blob.size());

    if (!matte_process(matte, rgb_blob, tri_blob, alpha_png,
                       matte_progress, (void*)(uintptr_t)client)) {
        fprintf(stderr, "[handler] matte_process failed\n"); return;
    }

    if (!send_msg(client, 'R', alpha_png.data(), (uint32_t)alpha_png.size())) {
        fprintf(stderr, "[handler] send failed\n"); return;
    }
    fprintf(stderr, "[handler] done — sent %zu bytes\n", alpha_png.size());
}

/* ── SD handler (text protocol) ───────────────────────────────────────────── */

static bool read_line(sock_t s, std::string& line) {
    line.clear();
    char c;
    while (sock_recv_all(s, (uint8_t*)&c, 1)) {
        if (c == '\n') return true;
        line += c;
    }
    return !line.empty();
}

static void handle_sd(sock_t client) {
    std::string prompt;
    std::string sparam;
    int    steps    = 4;
    float  cfg      = 2.0f;
    float  strength = 1.0f;
    int    w        = 512, h = 512;

    while (read_line(client, sparam)) {
        if (sparam.empty()) break;
        if (sparam.find("prompt=") == 0)    prompt   = sparam.substr(7);
        if (sparam.find("steps=") == 0)     steps    = atoi(sparam.c_str() + 6);
        if (sparam.find("cfg=") == 0)       cfg      = (float)atof(sparam.c_str() + 4);
        if (sparam.find("strength=") == 0)  strength = (float)atof(sparam.c_str() + 9);
        if (sparam.find("width=") == 0)     w        = atoi(sparam.c_str() + 6);
        if (sparam.find("height=") == 0)    h        = atoi(sparam.c_str() + 7);
    }

    fprintf(stderr, "[SD] request: \"%s\" target=%dx%d steps=%d cfg=%.1f strength=%.1f\n",
            prompt.c_str(), w, h, steps, cfg, strength);

    std::vector<uint8_t> source_png;
    recv_blob(client, source_png);

    fprintf(stderr, "[SD] starting generation...\n");
    send_msg(client, 'P', "Generating...", 13);

    std::vector<uint8_t> out_png;
    if (!sd_generate(prompt, source_png, strength, cfg, steps, w, h, out_png)) {
        fprintf(stderr, "[SD] generation failed\n");
        send_msg(client, 'P', "Generation failed", 17);
        return;
    }

    if (!send_msg(client, 'R', out_png.data(), (uint32_t)out_png.size())) {
        fprintf(stderr, "[SD] send failed\n"); return;
    }
    fprintf(stderr, "[SD] done — sent %zu bytes\n", out_png.size());
}

/* ── upscaler handler ─────────────────────────────────────────────────── */

static void upscale_write_cb(void* context, void* data, int size) {
    auto* out = (std::vector<uint8_t>*)context;
    out->insert(out->end(), (uint8_t*)data, (uint8_t*)data + size);
}

static void handle_upscale(sock_t client, OnnxModel* upscaler) {
    std::vector<uint8_t> src_png;
    if (!recv_blob(client, src_png)) {
        fprintf(stderr, "[upscale] recv failed\n"); return;
    }
    fprintf(stderr, "[upscale] source PNG %zu bytes\n", src_png.size());

    /* Decode PNG → RGBA → float RGB NCHW */
    int w = 0, h = 0, ch = 0;
    unsigned char* rgba = stbi_load_from_memory(
        src_png.data(), (int)src_png.size(), &w, &h, &ch, 4);
    if (!rgba) { fprintf(stderr, "[upscale] decode failed\n"); return; }

    size_t np = (size_t)w * h;
    std::vector<float> input(np * 3);
    for (size_t i = 0; i < np; i++) {
        input[i]        = rgba[i*4]   / 255.0f;
        input[np+i]     = rgba[i*4+1] / 255.0f;
        input[np*2+i]   = rgba[i*4+2] / 255.0f;
    }
    stbi_image_free(rgba);

    int64_t in_shape[4] = {1, 3, h, w};
    const float* in_data[1] = {input.data()};
    const char* in_names[1] = {"input"};

    float* out_data[1] = {nullptr};
    int64_t out_shape[4] = {0};
    const char* out_names[1] = {"output"};

    send_msg(client, 'P', "Upscaling...", 12);
    fprintf(stderr, "[upscale] starting %dx%d → %dx%d\n", w, h, w*4, h*4);

    if (!onnx_run(upscaler, in_names, in_data, in_shape, 1,
                  out_names, out_data, out_shape, 1)) {
        fprintf(stderr, "[upscale] inference failed\n"); return;
    }

    int ow = (int)out_shape[3], oh = (int)out_shape[2];
    fprintf(stderr, "[upscale] result %dx%d\n", ow, oh);

    /* Convert float output to RGBA PNG */
    std::vector<uint8_t> rgb((size_t)ow * oh * 3);
    for (int i = 0; i < ow * oh; i++) {
        rgb[i*3]   = (uint8_t)(fmaxf(0.0f, fminf(1.0f, out_data[0][i])) * 255.0f + 0.5f);
        rgb[i*3+1] = (uint8_t)(fmaxf(0.0f, fminf(1.0f, out_data[0][ow*oh + i])) * 255.0f + 0.5f);
        rgb[i*3+2] = (uint8_t)(fmaxf(0.0f, fminf(1.0f, out_data[0][ow*oh*2 + i])) * 255.0f + 0.5f);
    }
    free(out_data[0]);

    int png_len = 0;
    std::vector<uint8_t> png_buf;
    stbi_write_png_to_func(upscale_write_cb, &png_buf, ow, oh, 3, rgb.data(), ow * 3);
    if (png_buf.empty()) { fprintf(stderr, "[upscale] PNG encode failed\n"); return; }

    if (!send_msg(client, 'R', png_buf.data(), (uint32_t)png_buf.size()))
        fprintf(stderr, "[upscale] send failed\n");
    else
        fprintf(stderr, "[upscale] done — sent %zu bytes\n", png_buf.size());
}

/* ── globals ────────────────────────────────────────────────────────── */

static bool g_sd_available = false;

/* ── main ─────────────────────────────────────────────────────────────────── */

int main(int argc, char** argv) {
    setvbuf(stderr, nullptr, _IONBF, 0);
    fprintf(stderr, "[nnserver] starting...\n");

    const char* model_path  = nullptr;
    const char* model_url   = nullptr;
    int         port        = 8000;

    for (int i = 1; i < argc; i++) {
        if      (strcmp(argv[i], "--port")      == 0 && i+1 < argc) port = atoi(argv[++i]);
        else if (strcmp(argv[i], "--model")     == 0 && i+1 < argc) model_path = argv[++i];
        else if (strcmp(argv[i], "--model-url") == 0 && i+1 < argc) model_url = argv[++i];
        else if (strcmp(argv[i], "--no-prompt") == 0) dl_set_no_prompt(true);
        else if (strcmp(argv[i], "--help")      == 0) {
            fprintf(stderr, "Usage: nnserver [--port PORT] [--model PATH] [--model-url URL] [--no-prompt]\n");
            return 0;
        }
    }

    /* ── Resolve matte model (required) ────────────────────────────────── */
    std::string matte_path = dl_resolve_matte(model_path, model_url);
    if (matte_path.empty()) { fprintf(stderr, "[nnserver] no matte model\n"); return 1; }

    OnnxModel* onnx = onnx_load(matte_path.c_str());
    if (!onnx) { fprintf(stderr, "[nnserver] failed to load matte model\n"); return 1; }
    MatteModel matte;
    matte_init(&matte, onnx);

    /* ── Resolve optional models ──────────────────────────────────────── */
    std::string sd_path = dl_resolve_optional(DL_SD_MODEL);
    if (!sd_path.empty())
        g_sd_available = sd_init(sd_path.c_str());
    fprintf(stderr, "[nnserver] SD available: %s\n", g_sd_available ? "yes" : "no");

    std::string upscale_path = dl_resolve_optional(DL_UPSCALER_MODEL);
    OnnxModel* upscaler = nullptr;
    if (!upscale_path.empty())
        upscaler = onnx_load(upscale_path.c_str());
    if (upscaler)
        fprintf(stderr, "[nnserver] upscaler ready\n");
    else
        fprintf(stderr, "[nnserver] upscaler not available\n");

    /* ── Network ──────────────────────────────────────────────────────── */
    if (!sock_init()) { fprintf(stderr, "sock_init: %s\n", sock_last_error()); return 1; }

    sock_t listener = sock_listen(port);
    if (listener == SOCK_INVALID) {
        fprintf(stderr, "listen(%d): %s\n", port, sock_last_error());
        sock_shutdown(); return 1;
    }
    fprintf(stderr, "[nnserver] listening on 127.0.0.1:%d\n", port);

    /* ── Main loop ────────────────────────────────────────────────────── */
    while (true) {
        sock_t client = sock_accept(listener);
        if (client == SOCK_INVALID) { fprintf(stderr, "accept: %s\n", sock_last_error()); continue; }

        fprintf(stderr, "[accept] new connection\n");

        uint8_t mode_byte;
        if (!sock_recv_all(client, &mode_byte, 1)) { sock_close(client); continue; }

        if (mode_byte == 'U' && upscaler) {
            fprintf(stderr, "[accept] upscale request\n");
            handle_upscale(client, upscaler);
        } else if (mode_byte == 'G') {
            if (!g_sd_available && dl_file_exists(sd_path.c_str()))
                g_sd_available = sd_init(sd_path.c_str());

            if (g_sd_available) {
                fprintf(stderr, "[accept] SD request\n");
                handle_sd(client);
            } else {
                fprintf(stderr, "[accept] SD not available\n");
                handle_matte(client, &matte, mode_byte);
            }
        } else {
            handle_matte(client, &matte, mode_byte);
        }

        sock_close(client);
    }
}
