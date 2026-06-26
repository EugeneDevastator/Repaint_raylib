#include "sock_platform.h"
#ifdef __MINGW32__
# define _Return_type_success_(x)
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
# include <winhttp.h>
# include <conio.h>
#else
# include <curl/curl.h>
# include <unistd.h>
# include <termios.h>
#endif

#ifdef _MSC_VER
# define strdup _strdup
#endif

/* ── model constants ──────────────────────────────────────────────────────── */

#define MODEL_FILENAME       "model.onnx"
#define DEFAULT_MODEL_SIZE   99100000ULL
#define MODEL_EXPECTED_SIZE  103885865ULL

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

static bool file_size(const char* path, long long* out) {
    FILE* f = fopen(path, "rb");
    if (!f) return false;
    fseek(f, 0, SEEK_END);
    *out = (long long)ftell(f);
    fclose(f);
    return true;
}

/* ── model URL / cache path resolution ────────────────────────────────────── */

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
#ifdef _WIN32
    return std::string(exe_dir) + "\\nnmodels";
#else
    return std::string(exe_dir) + "/nnmodels";
#endif
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

/* ── "press any key" helper ──────────────────────────────────────────────── */

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

/* ── cross-platform download (WinHTTP / libcurl) ──────────────────────────── */

#ifdef _WIN32

static bool download_file(const char* url, const char* dest_path, long long expected_size) {
    int wlen = MultiByteToWideChar(CP_UTF8, 0, url, -1, nullptr, 0);
    std::wstring wurl((size_t)wlen, L'\0');
    MultiByteToWideChar(CP_UTF8, 0, url, -1, &wurl[0], wlen);

    URL_COMPONENTSW comp = {sizeof(comp)};
    comp.dwSchemeLength    = (DWORD)-1;
    comp.dwHostNameLength  = (DWORD)-1;
    comp.dwUrlPathLength   = (DWORD)-1;
    comp.dwExtraInfoLength = (DWORD)-1;

    if (!WinHttpCrackUrl(wurl.c_str(), (DWORD)wurl.size(), 0, &comp))
        return false;

    std::wstring host(comp.lpszHostName, comp.dwHostNameLength);
    std::wstring path(comp.lpszUrlPath, comp.dwUrlPathLength);
    if (comp.dwExtraInfoLength > 0)
        path += std::wstring(comp.lpszExtraInfo, comp.dwExtraInfoLength);
    bool secure = comp.nScheme == INTERNET_SCHEME_HTTPS;
    INTERNET_PORT port = comp.nPort;

    HINTERNET hSession = WinHttpOpen(L"nnserver/1.0",
        WINHTTP_ACCESS_TYPE_DEFAULT_PROXY, NULL, NULL, 0);
    if (!hSession) return false;

    HINTERNET hConnect = WinHttpConnect(hSession, host.c_str(), port, 0);
    if (!hConnect) { WinHttpCloseHandle(hSession); return false; }

    DWORD flags = secure ? WINHTTP_FLAG_SECURE : 0;
    HINTERNET hRequest = WinHttpOpenRequest(hConnect, L"GET", path.c_str(),
        NULL, NULL, NULL, flags);
    if (!hRequest) { WinHttpCloseHandle(hConnect); WinHttpCloseHandle(hSession); return false; }

    DWORD secFlags = SECURITY_FLAG_IGNORE_UNKNOWN_CA |
                     SECURITY_FLAG_IGNORE_CERT_WRONG_USAGE;
    WinHttpSetOption(hRequest, WINHTTP_OPTION_SECURITY_FLAGS, &secFlags, sizeof(secFlags));

    bool ok = false;
    FILE* fp = nullptr;
    long long received = 0;

    do {
        if (!WinHttpSendRequest(hRequest, WINHTTP_NO_ADDITIONAL_HEADERS, 0, NULL, 0, 0, 0))
            break;
        if (!WinHttpReceiveResponse(hRequest, NULL))
            break;

        WCHAR clenStr[32] = {0};
        DWORD clenSize = sizeof(clenStr);
        long long totalSize = 0;
        if (WinHttpQueryHeaders(hRequest, WINHTTP_QUERY_CONTENT_LENGTH,
                NULL, clenStr, &clenSize, WINHTTP_NO_HEADER_INDEX))
            totalSize = _wtoi64(clenStr);
        if (totalSize == 0) totalSize = expected_size;

        fp = fopen(dest_path, "wb");
        if (!fp) break;

        fprintf(stderr, "  [download] %s\n", url);

        char buf[65536];
        int last_pct = -1;

        while (true) {
            DWORD available = 0;
            if (!WinHttpQueryDataAvailable(hRequest, &available)) break;
            if (available == 0) break;

            DWORD read = 0;
            DWORD to_read = available < sizeof(buf) ? available : (DWORD)sizeof(buf);
            if (!WinHttpReadData(hRequest, buf, to_read, &read)) break;
            if (read == 0) break;

            fwrite(buf, 1, read, fp);
            received += read;

            if (totalSize > 0) {
                int pct = (int)(received * 100 / totalSize);
                if (pct != last_pct) {
                    last_pct = pct;
                    fprintf(stderr, "\r  [download] %lld / %lld MB (%d%%)",
                            (long long)(received / 1048576),
                            (long long)(totalSize / 1048576), pct);
                }
            }
        }
        ok = true;
    } while (false);

    if (fp) fclose(fp);
    WinHttpCloseHandle(hRequest);
    WinHttpCloseHandle(hConnect);
    WinHttpCloseHandle(hSession);
    fprintf(stderr, "\n");

    return ok;
}

#else /* POSIX — libcurl */

struct WriteBuf {
    FILE* fp;
    long long received;
    long long total;
    int last_pct;
};

static size_t write_cb(void* ptr, size_t size, size_t nmemb, void* userdata) {
    WriteBuf* buf = (WriteBuf*)userdata;
    size_t n = fwrite(ptr, size, nmemb, buf->fp);
    buf->received += (long long)n;
    return n;
}

static int progress_cb(void* clientp, curl_off_t dltotal, curl_off_t dlnow,
                        curl_off_t ultotal, curl_off_t ulnow) {
    (void)ultotal; (void)ulnow;
    WriteBuf* buf = (WriteBuf*)clientp;
    if (dltotal > 0) {
        buf->total = (long long)dltotal;
        int pct = (int)(dlnow * 100 / dltotal);
        if (pct != buf->last_pct) {
            buf->last_pct = pct;
            fprintf(stderr, "\r  [download] %lld / %lld MB (%d%%)",
                    (long long)(dlnow / 1048576),
                    (long long)(dltotal / 1048576), pct);
        }
    }
    return 0;
}

static bool download_file(const char* url, const char* dest_path, long long expected_size) {
    CURL* curl = curl_easy_init();
    if (!curl) return false;

    FILE* fp = fopen(dest_path, "wb");
    if (!fp) { curl_easy_cleanup(curl); return false; }

    WriteBuf buf = {fp, 0, expected_size, -1};

    curl_easy_setopt(curl, CURLOPT_URL, url);
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, write_cb);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, &buf);
    curl_easy_setopt(curl, CURLOPT_NOPROGRESS, 0L);
    curl_easy_setopt(curl, CURLOPT_XFERINFOFUNCTION, progress_cb);
    curl_easy_setopt(curl, CURLOPT_XFERINFODATA, &buf);
    curl_easy_setopt(curl, CURLOPT_FOLLOWLOCATION, 1L);
    curl_easy_setopt(curl, CURLOPT_CONNECTTIMEOUT, 30L);
    curl_easy_setopt(curl, CURLOPT_USERAGENT, "nnserver/1.0");

    fprintf(stderr, "  [download] %s\n", url);
    CURLcode res = curl_easy_perform(curl);
    fclose(fp);
    curl_easy_cleanup(curl);
    fprintf(stderr, "\n");

    if (res != CURLE_OK) {
        fprintf(stderr, "  [nnserver] download failed: %s\n", curl_easy_strerror(res));
        remove(dest_path);
        return false;
    }

    return true;
}
#endif

/* ── model discovery (cache → download → load) ────────────────────────────── */

static std::string resolve_model(const char* model_arg, const char* url_arg) {
    /* 1. explicit --model path */
    if (model_arg) {
        if (file_exists(model_arg)) {
            fprintf(stderr, "[nnserver] model: %s\n", model_arg);
            return model_arg;
        }
        fprintf(stderr, "[nnserver] --model path not found: %s\n", model_arg);
    }

    /* 2. local file in CWD */
    if (file_exists(MODEL_FILENAME)) {
        fprintf(stderr, "[nnserver] model: %s\n", MODEL_FILENAME);
        return MODEL_FILENAME;
    }

    /* 3. cache dir */
    std::string cache_dir = get_cache_dir();
    std::string cache_path = cache_dir + "/" + MODEL_FILENAME;
    if (file_exists(cache_path.c_str())) {
        fprintf(stderr, "[nnserver] model: %s\n", cache_path.c_str());
        return cache_path;
    }

    /* 4. not found — resolve URL and download */
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
    if (!download_file(url.c_str(), cache_path.c_str(), MODEL_EXPECTED_SIZE)) {
        fprintf(stderr, "[nnserver] download failed!\n");
        return "";
    }

    fprintf(stderr, "[nnserver] download complete\n");
    fprintf(stderr, "[nnserver] model: %s\n", cache_path.c_str());
    return cache_path;
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
    if (!g_api) { fprintf(stderr, "[nnserver] ORT GetApi failed\n"); return false; }

    OrtStatus* st = nullptr;

    st = g_api->CreateEnv(ORT_LOGGING_LEVEL_WARNING, "nnserver", &g_env);
    if (st) { fprintf(stderr, "[nnserver] ORT env: %s\n", g_api->GetErrorMessage(st)); g_api->ReleaseStatus(st); return false; }

#ifdef _WIN32
    int wlen = MultiByteToWideChar(CP_UTF8, 0, path, -1, nullptr, 0);
    std::wstring wpath((size_t)wlen, L'\0');
    MultiByteToWideChar(CP_UTF8, 0, path, -1, &wpath[0], wlen);
#else
    const char* wpath = path;
#endif

    OrtSessionOptions* opts = nullptr;
    st = g_api->CreateSessionOptions(&opts);
    if (st) { fprintf(stderr, "[nnserver] ORT opts: %s\n", g_api->GetErrorMessage(st)); g_api->ReleaseStatus(st); return false; }
    g_api->SetSessionGraphOptimizationLevel(opts, ORT_ENABLE_BASIC);

    st = g_api->CreateSession(g_env, wpath.c_str(), opts, &g_sess);
    g_api->ReleaseSessionOptions(opts);
    if (st) { fprintf(stderr, "[nnserver] ORT sess: %s\n", g_api->GetErrorMessage(st)); g_api->ReleaseStatus(st); return false; }

    st = g_api->CreateMemoryInfo("Cpu", OrtDeviceAllocator, -1, OrtMemTypeDefault, &g_mem);
    if (st) { fprintf(stderr, "[nnserver] ORT mem: %s\n", g_api->GetErrorMessage(st)); g_api->ReleaseStatus(st); return false; }

    OrtAllocator* alloc = nullptr;
    st = g_api->GetAllocatorWithDefaultOptions(&alloc);
    if (st) { fprintf(stderr, "[nnserver] ORT alloc: %s\n", g_api->GetErrorMessage(st)); g_api->ReleaseStatus(st); return false; }

    g_api->SessionGetInputCount(g_sess, &g_nin);
    g_api->SessionGetOutputCount(g_sess, &g_nout);

    g_inames = (const char**)calloc(g_nin,  sizeof(char*));
    g_onames = (const char**)calloc(g_nout, sizeof(char*));

    for (size_t i = 0; i < g_nin; i++) {
        char* n; g_api->SessionGetInputName(g_sess, i, alloc, &n);
        g_inames[i] = strdup(n);

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

    if (!recv_blob(client, rgb_blob) || !recv_blob(client, tri_blob)) {
        fprintf(stderr, "[handler] recv failed\n"); return;
    }
    fprintf(stderr, "[handler] rgb %zu bytes, trimap %zu bytes\n",
            rgb_blob.size(), tri_blob.size());

    auto progress = [&](const char* msg) {
        fprintf(stderr, "  %s\n", msg);
        send_msg(client, 'P', msg, (uint32_t)strlen(msg));
    };

    progress("Step 1/4: Decoding images...");
    if (!decode_png(rgb_blob, rgb_w, rgb_h, rgb_rgba) ||
        !decode_png(tri_blob, tri_w, tri_h, tri_rgba)) {
        fprintf(stderr, "[handler] decode failed\n"); return;
    }
    orig_w = rgb_w; orig_h = rgb_h;
    fprintf(stderr, "  image size: %dx%d\n", orig_w, orig_h);

    progress("Step 2/4: Posterizing trimap...");
    trimap.assign((size_t)tri_w * tri_h, 0);
    for (int i = 0; i < tri_w * tri_h; i++) {
        uint8_t v = tri_rgba[i*4];
        trimap[i] = (v >= 192) ? 255 : (v >= 64) ? 128 : 0;
    }

    mw = (orig_w / 32) * 32; if (mw < 32) mw = 32;
    mh = (orig_h / 32) * 32; if (mh < 32) mh = 32;

    rsz_rgb.assign((size_t)mw * mh * 4, 0);
    rsz_tri.assign((size_t)mw * mh, 0);
    resize_rgba(rgb_rgba.data(), rgb_w, rgb_h, rsz_rgb.data(), mw, mh);
    resize_gray(trimap.data(), tri_w, tri_h, rsz_tri.data(), mw, mh);
    fprintf(stderr, "  resized to: %dx%d\n", mw, mh);

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

    const char* model_path  = nullptr;
    const char* model_url   = nullptr;
    int         port        = 8000;

    for (int i = 1; i < argc; i++) {
        if      (strcmp(argv[i], "--port")  == 0 && i+1 < argc) port = atoi(argv[++i]);
        else if (strcmp(argv[i], "--model") == 0 && i+1 < argc) model_path = argv[++i];
        else if (strcmp(argv[i], "--model-url") == 0 && i+1 < argc) model_url = argv[++i];
        else if (strcmp(argv[i], "--no-prompt") == 0) g_no_prompt = true;
        else if (strcmp(argv[i], "--help")  == 0) {
            fprintf(stderr, "Usage: nnserver [--port PORT] [--model PATH] [--model-url URL] [--no-prompt]\n");
            return 0;
        }
    }

    std::string resolved = resolve_model(model_path, model_url);
    if (resolved.empty()) {
        fprintf(stderr, "[nnserver] no model available, bailing out\n");
        return 1;
    }

    if (!sock_init()) { fprintf(stderr, "sock_init: %s\n", sock_last_error()); return 1; }
    if (!ort_load(resolved.c_str())) { sock_shutdown(); return 1; }

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
}
