#include "nn_downloader.h"
#include "nn_download.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifdef _WIN32
# include <windows.h>
# include <conio.h>
#else
# include <unistd.h>
# include <sys/select.h>
#endif

/* ── URLs ──────────────────────────────────────────────────────────────────── */

#define URL_MATTE    "https://media.githubusercontent.com/media/EugeneDevastator/repaint_models/main/matte/vitmatte_model_vitsmall_dist646.onnx"
#define URL_SD       "https://media.githubusercontent.com/media/EugeneDevastator/repaint_models/main/SD/Dreamshaper8/dreamshaper_8LCM.safetensors"
#define URL_UPSCALER "https://media.githubusercontent.com/media/EugeneDevastator/repaint_models/main/upscaler/4x-UltraSharpV2_Lite_fp32_op17.onnx"

const ModelInfo DL_SD_MODEL = {
    "SD (LCM8)",
    "dreamshaper_8LCM.safetensors",
    URL_SD,
    2600000000LL
};

const ModelInfo DL_UPSCALER_MODEL = {
    "Upscaler (4x-UltraSharp)",
    "4x-UltraSharpV2_Lite_fp32_op17.onnx",
    URL_UPSCALER,
    28500000LL
};

/* ── state ─────────────────────────────────────────────────────────────────── */

static bool g_no_prompt = false;

void dl_set_no_prompt(bool v) { g_no_prompt = v; }

/* ── filesystem ────────────────────────────────────────────────────────────── */

bool dl_file_exists(const char* path) {
    FILE* f = fopen(path, "rb");
    if (f) { fclose(f); return true; }
    return false;
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

std::string dl_cache_dir() {
    char buf[1024];
    get_exe_dir(buf, sizeof(buf));
    return std::string(buf) + "/nnmodels";
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

/* ── timed prompt ──────────────────────────────────────────────────────────── */

bool dl_prompt_timed(const char* msg, int seconds) {
    if (g_no_prompt) return false;
    fprintf(stderr, "\n[nnserver] %s\n", msg);
    fprintf(stderr, "  [nnserver] Press any key to download, timeout %ds... ", seconds);

#ifdef _WIN32
    DWORD start = GetTickCount();
    while (GetTickCount() - start < (DWORD)(seconds * 1000)) {
        if (_kbhit()) {
            _getch();
            fprintf(stderr, "OK\n\n");
            return true;
        }
        Sleep(50);
    }
    fprintf(stderr, "timeout\n\n");
    return false;
#else
    struct timeval tv;
    tv.tv_sec = seconds;
    tv.tv_usec = 0;
    fd_set fds;
    FD_ZERO(&fds);
    FD_SET(STDIN_FILENO, &fds);
    int ret = select(STDIN_FILENO + 1, &fds, NULL, NULL, &tv);
    if (ret > 0) {
        char c;
        if (read(STDIN_FILENO, &c, 1) > 0) {
            fprintf(stderr, "OK\n\n");
            return c != 27;
        }
    }
    fprintf(stderr, "timeout\n\n");
    return false;
#endif
}

/* ── download progress ─────────────────────────────────────────────────────── */

static void progress_cb(int64_t received, int64_t total) {
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

/* ── download helper ───────────────────────────────────────────────────────── */

bool dl_download(const std::string& url, const std::string& path) {
    fprintf(stderr, "  [nnserver] downloading...\n");
    if (!nn_download(url.c_str(), path.c_str(), progress_cb)) {
        fprintf(stderr, "\n  [nnserver] download failed!\n");
        remove(path.c_str());
        return false;
    }
    fprintf(stderr, "\n  [nnserver] download complete\n");
    return true;
}

/* ── model resolution ──────────────────────────────────────────────────────── */

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

std::string dl_resolve_matte(const char* model_arg, const char* url_arg) {
    const char* model_file = "model.onnx";
    const long long expected_size = 103885865ULL;

    if (model_arg) {
        if (dl_file_exists(model_arg)) {
            fprintf(stderr, "[nnserver] model: %s\n", model_arg);
            return model_arg;
        }
        fprintf(stderr, "[nnserver] --model path not found: %s\n", model_arg);
    }

    if (dl_file_exists(model_file)) {
        fprintf(stderr, "[nnserver] model: %s\n", model_file);
        return model_file;
    }

    std::string cache_dir = dl_cache_dir();
    std::string cache_path = cache_dir + "/" + model_file;
    if (dl_file_exists(cache_path.c_str())) {
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
        if (!read_url_file(url_path.c_str(), url))
            url = URL_MATTE;
    }

    fprintf(stderr, "[nnserver] matte model not found\n");
    fprintf(stderr, "[nnserver] one-time download required (%lld MB)\n",
            (long long)(expected_size / 1048576));

    if (!dl_prompt_timed("Download matte model?", 3)) {
        fprintf(stderr, "[nnserver] matte model download skipped\n");
        return "";
    }

    mkdir_recursive(cache_dir.c_str());
    if (!dl_download(url, cache_path)) return "";
    fprintf(stderr, "[nnserver] model: %s\n", cache_path.c_str());
    return cache_path;
}

std::string dl_resolve_optional(const ModelInfo& info) {
    /* Check CWD */
    if (dl_file_exists(info.filename)) {
        fprintf(stderr, "[nnserver] %s: %s\n", info.name, info.filename);
        return info.filename;
    }
    /* Check cache */
    std::string cache_dir = dl_cache_dir();
    std::string cache_path = cache_dir + "/" + info.filename;
    if (dl_file_exists(cache_path.c_str())) {
        fprintf(stderr, "[nnserver] %s: %s\n", info.name, cache_path.c_str());
        return cache_path;
    }

    /* Prompt */
    char msg[256];
    if (info.size > 0)
        snprintf(msg, sizeof(msg), "Download %s? (%lld MB)", info.name,
                 (long long)(info.size / 1048576));
    else
        snprintf(msg, sizeof(msg), "Download %s? (size unknown, may be large)", info.name);

    if (!dl_prompt_timed(msg, 3)) {
        fprintf(stderr, "[nnserver] %s download skipped\n", info.name);
        return "";
    }

    mkdir_recursive(cache_dir.c_str());
    if (!dl_download(info.url, cache_path)) return "";
    fprintf(stderr, "[nnserver] %s: %s\n", info.name, cache_path.c_str());
    return cache_path;
}
