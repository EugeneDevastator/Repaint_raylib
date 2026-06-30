#include "sock_platform.h"
#include "nn_download.h"
#include "nn_onnx.h"
#include "matte_module.h"
#include "sd_module.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <vector>
#include <string>

#ifdef _WIN32
# include <windows.h>
# include <conio.h>
#else
# include <unistd.h>
# include <termios.h>
#endif

#ifdef _MSC_VER
# define strdup _strdup
#endif

/* ── constants ────────────────────────────────────────────────────────────── */

#define MODEL_FILENAME       "model.onnx"
#define MODEL_EXPECTED_SIZE  103885865ULL

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

/* ── filesystem helpers ───────────────────────────────────────────────────── */

static bool file_exists(const char* path) {
    FILE* f = fopen(path, "rb");
    if (f) { fclose(f); return true; }
    return false;
}

static bool mkdir_recursive(const char* path) {
#ifdef _WIN32
    int len = (int)strlen(path);
    char tmp[1024];
    for (int i = 0; i <= len; i++) {
        if (i == len || path[i] == '\\' || path[i] == '/') {
            if (i == 0) continue;
            memcpy(tmp, path, i); tmp[i] = '\0';
            CreateDirectoryA(tmp, NULL);
        }
    }
    return true;
#else
    char cmd[1024];
    snprintf(cmd, sizeof(cmd), "mkdir -p \"%s\"", path);
    return system(cmd) == 0;
#endif
}

static void get_exe_dir(char* buf, size_t size) {
#ifdef _WIN32
    GetModuleFileNameA(NULL, buf, (DWORD)size);
    char* last = strrchr(buf, '\\');
    if (last) *last = '\0';
#else
    ssize_t n = readlink("/proc/self/exe", buf, size - 1);
    if (n > 0) {
        buf[n] = '\0';
        char* last = strrchr(buf, '/');
        if (last) *last = '\0';
    } else {
        strcpy(buf, ".");
    }
#endif
}

static std::string get_cache_dir() {
    char exe_dir[1024];
    get_exe_dir(exe_dir, sizeof(exe_dir));
    return std::string(exe_dir) + "/nnmodels";
}

static bool read_url_file(const char* path, std::string& url) {
    FILE* f = fopen(path, "rb");
    if (!f) return false;
    char buf[2048];
    if (fgets(buf, sizeof(buf), f)) {
        size_t len = strlen(buf);
        while (len > 0 && (buf[len-1] == '\n' || buf[len-1] == '\r')) buf[--len] = '\0';
        if (len > 0) url = buf;
    }
    fclose(f);
    return !url.empty();
}

/* ── prompt & globals ─────────────────────────────────────────────────────── */

static bool g_no_prompt = false;

static bool prompt_continue() {
    if (g_no_prompt) return true;
    fprintf(stderr, "  [nnserver] Press any key to start download, ESC to cancel\n");
#ifdef _WIN32
    int c = _getch();
    return c != 27;
#else
    struct termios old, newt;
    tcgetattr(STDIN_FILENO, &old);
    newt = old;
    newt.c_lflag &= ~(ICANON | ECHO);
    tcsetattr(STDIN_FILENO, TCSANOW, &newt);
    int c = getchar();
    tcsetattr(STDIN_FILENO, TCSANOW, &old);
    return c != 27;
#endif
}

/* ── download progress callback ───────────────────────────────────────────── */

static void download_progress(int64_t received, int64_t total) {
    static int last_pct = -1;
    if (received == 0) {
        last_pct = -1;
    } else if (total > 0) {
        int pct = (int)(received * 100 / total);
        if (pct != last_pct) {
            last_pct = pct;
            fprintf(stderr, "\r  [download] %lld / %lld MB (%d%%)",
                    (long long)(received / 1048576),
                    (long long)(total / 1048576), pct);
        }
        if (received == total)
            fprintf(stderr, "\n");
    }
}

/* ── model resolution (cache → download → path) ──────────────────────────── */

static std::string resolve_model(const char* model_arg, const char* url_arg) {
    if (model_arg) {
        if (file_exists(model_arg)) {
            fprintf(stderr, "[nnserver] model: %s\n", model_arg);
            return model_arg;
        }
        fprintf(stderr, "[nnserver] --model path not found: %s\n", model_arg);
    }

    if (file_exists(MODEL_FILENAME)) {
        fprintf(stderr, "[nnserver] model: %s\n", MODEL_FILENAME);
        return MODEL_FILENAME;
    }

    std::string cache_dir = get_cache_dir();
    std::string cache_path = cache_dir + "/" + MODEL_FILENAME;
    if (file_exists(cache_path.c_str())) {
        fprintf(stderr, "[nnserver] model: %s\n", cache_path.c_str());
        return cache_path;
    }

    std::string url;
    if (url_arg) {
        url = url_arg;
    } else {
        char exe_dir[1024];
        get_exe_dir(exe_dir, sizeof(exe_dir));
        std::string url_path = std::string(exe_dir) + "/model_url.txt";
        if (!read_url_file(url_path.c_str(), url)) {
            url = "https://media.githubusercontent.com/media/EugeneDevastator/repaint_models/main/matte/vitmatte_model_vitsmall_dist646.onnx";
        }
    }

    fprintf(stderr, "[nnserver] model not found in cache\n");
    fprintf(stderr, "[nnserver] one-time download required (%lld MB)\n",
            (long long)(MODEL_EXPECTED_SIZE / 1048576));

    if (!prompt_continue()) {
        fprintf(stderr, "[nnserver] download cancelled\n");
        return "";
    }

    mkdir_recursive(cache_dir.c_str());
    fprintf(stderr, "[nnserver] downloading...\n");

    if (!nn_download(url.c_str(), cache_path.c_str(), download_progress)) {
        fprintf(stderr, "\n[nnserver] download failed!\n");
        return "";
    }

    fprintf(stderr, "[nnserver] download complete\n");
    fprintf(stderr, "[nnserver] model: %s\n", cache_path.c_str());
    return cache_path;
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

    /* reconstruct first 4 bytes of length prefix (1 byte consumed for mode) */
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
    /* read header lines until empty line */
    std::string prompt;
    std::string sparam;
    int    steps    = 4;
    float  cfg      = 2.0f;
    float  strength = 1.0f;
    int    w        = 512, h = 512;

    while (read_line(client, sparam)) {
        if (sparam.empty()) break;  /* empty line = end of header */
        if (sparam.find("prompt=") == 0)    prompt   = sparam.substr(7);
        if (sparam.find("steps=") == 0)     steps    = atoi(sparam.c_str() + 6);
        if (sparam.find("cfg=") == 0)       cfg      = (float)atof(sparam.c_str() + 4);
        if (sparam.find("strength=") == 0)  strength = (float)atof(sparam.c_str() + 9);
        if (sparam.find("width=") == 0)     w        = atoi(sparam.c_str() + 6);
        if (sparam.find("height=") == 0)    h        = atoi(sparam.c_str() + 7);
    }

    fprintf(stderr, "[SD] request: \"%s\" target=%dx%d steps=%d cfg=%.1f strength=%.1f\n",
            prompt.c_str(), w, h, steps, cfg, strength);

    /* optional source image (txt2img — none expected) */
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

/* ── globals ────────────────────────────────────────────────────────── */

static bool g_sd_available = false;

/* ── main ─────────────────────────────────────────────────────────────────── */

int main(int argc, char** argv) {
    setvbuf(stderr, nullptr, _IONBF, 0);
    fprintf(stderr, "[nnserver] starting...\n");

    const char* model_path  = nullptr;
    const char* model_url   = nullptr;
    const char* sd_model    = nullptr;
    int         port        = 8000;

    for (int i = 1; i < argc; i++) {
        if      (strcmp(argv[i], "--port")      == 0 && i+1 < argc) port = atoi(argv[++i]);
        else if (strcmp(argv[i], "--model")     == 0 && i+1 < argc) model_path = argv[++i];
        else if (strcmp(argv[i], "--model-url") == 0 && i+1 < argc) model_url = argv[++i];
        else if (strcmp(argv[i], "--sd-model")  == 0 && i+1 < argc) sd_model = argv[++i];
        else if (strcmp(argv[i], "--no-prompt") == 0) g_no_prompt = true;
        else if (strcmp(argv[i], "--help")      == 0) {
            fprintf(stderr, "Usage: nnserver [--port PORT] [--model PATH] [--model-url URL] [--sd-model PATH] [--no-prompt]\n");
            return 0;
        }
    }

    if (sd_model) g_sd_available = sd_init(sd_model);

    std::string resolved = resolve_model(model_path, model_url);
    if (resolved.empty()) { fprintf(stderr, "[nnserver] no model available\n"); return 1; }

    OnnxModel* onnx = onnx_load(resolved.c_str());
    if (!onnx) { fprintf(stderr, "[nnserver] failed to load model\n"); return 1; }

    MatteModel matte;
    matte_init(&matte, onnx);

    if (!sock_init()) { fprintf(stderr, "sock_init: %s\n", sock_last_error()); onnx_unload(onnx); return 1; }

    sock_t listener = sock_listen(port);
    if (listener == SOCK_INVALID) {
        fprintf(stderr, "listen(%d): %s\n", port, sock_last_error());
        sock_shutdown(); onnx_unload(onnx); return 1;
    }
    fprintf(stderr, "[nnserver] listening on 127.0.0.1:%d\n", port);

    while (true) {
        sock_t client = sock_accept(listener);
        if (client == SOCK_INVALID) { fprintf(stderr, "accept: %s\n", sock_last_error()); continue; }

        fprintf(stderr, "[accept] new connection\n");

        uint8_t mode_byte;
        if (!sock_recv_all(client, &mode_byte, 1)) { sock_close(client); continue; }

        if (mode_byte == 'G') {
            if (!g_sd_available) {
                fprintf(stderr, "[accept] SD request — loading model...\n");
                static const char* sd_paths[] = {
                    "dreamshaper_8LCM.safetensors",
                    "../NNModelServer/onnx/dreamshaper_8LCM.safetensors",
                    nullptr
                };
                for (int i = 0; sd_paths[i]; i++) {
                    if (file_exists(sd_paths[i])) {
                        g_sd_available = sd_init(sd_paths[i]);
                        if (g_sd_available) break;
                    }
                }
                if (!g_sd_available)
                    fprintf(stderr, "[accept] SD model not found (tried dreamshaper_8LCM.safetensors)\n");
            }
            if (g_sd_available) {
                fprintf(stderr, "[accept] SD request\n");
                handle_sd(client);
            } else {
                handle_matte(client, &matte, mode_byte);
            }
        } else {
            handle_matte(client, &matte, mode_byte);
        }

        sock_close(client);
    }
}
