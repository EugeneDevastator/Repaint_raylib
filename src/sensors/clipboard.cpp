#include "platform_clipboard.h"
#include "repaint.h"
#include "rlgl.h"
#include "external/glad.h"
#include <cstdlib>
#include <cstring>

// ── Platform backend (implemented in clipboard_win.cpp / clipboard_lnx.cpp) ──
bool ClipboardPlatform_GetImage(int* w, int* h, void** rgba);
bool ClipboardPlatform_GetFilePath(char* path, int maxLen);
bool ClipboardPlatform_GetText(char* text, int maxLen);
bool ClipboardPlatform_SetImage(int w, int h, const void* rgba);
bool ClipboardPlatform_SetFilePath(const char* path);
bool ClipboardPlatform_SetImageWithCustom(int w, int h, const void* rgba8,
    const char* customName, const void* customData, size_t customSize);
bool ClipboardPlatform_GetCustomData(const char* name, void** data, size_t* size);

// ── Static state ──
static ClipboardImageCallback  s_imageCb = NULL;
static ClipboardImage16Callback s_image16Cb = NULL;
static ClipboardFileCallback  s_fileCb = NULL;

void Clipboard_SetImageCallback(ClipboardImageCallback cb) { s_imageCb = cb; }
void Clipboard_SetImage16Callback(ClipboardImage16Callback cb) { s_image16Cb = cb; }
void Clipboard_SetFileCallback(ClipboardFileCallback cb) { s_fileCb = cb; }

void Clipboard_Update(void) {
    if (!IsKeyDown(KEY_LEFT_CONTROL) || !IsKeyPressed(KEY_V)) return;

    // Priority: 16-bit custom format > standard 8-bit image > file path > text
    void* customData = NULL;
    size_t customSize = 0;
    if (s_image16Cb && ClipboardPlatform_GetCustomData("RePaint16BitImage", &customData, &customSize)) {
        // Decode: [magic:4][w:4][h:4][pixels:w*h*8]
        if (customSize >= 12) {
            uint32_t magic; int w, h;
            memcpy(&magic, customData, 4);
            memcpy(&w, (uint8_t*)customData + 4, 4);
            memcpy(&h, (uint8_t*)customData + 8, 4);
            size_t expectedPix = (size_t)w * h * 4 * sizeof(uint16_t);
            if (magic == 0x52503136 && customSize == 12 + expectedPix && w > 0 && h > 0) {
                uint16_t* pixels16 = (uint16_t*)malloc(expectedPix);
                if (pixels16) {
                    memcpy(pixels16, (uint8_t*)customData + 12, expectedPix);
                    s_image16Cb(w, h, pixels16);
                    free(pixels16);
                }
            }
        }
        free(customData);
        return;
    }

    int w, h;
    void* rgba = NULL;
    if (ClipboardPlatform_GetImage(&w, &h, &rgba)) {
        if (s_imageCb) s_imageCb(w, h, (const unsigned char*)rgba);
        free(rgba);
        return;
    }

    char path[1024];
    if (ClipboardPlatform_GetFilePath(path, sizeof(path))) {
        if (s_fileCb) s_fileCb(path);
        return;
    }

    if (ClipboardPlatform_GetText(path, sizeof(path))) {
        if (s_fileCb) s_fileCb(path);
    }
}

// ── 16-bit encode/decode ──────────────────────────────────────────────
#define RP16_MAGIC 0x52503136  // "RP16"

static bool Encode16BitData(int w, int h, const uint16_t* pixels,
    void** outData, size_t* outSize)
{
    size_t pixBytes = (size_t)w * h * 4 * sizeof(uint16_t);
    size_t total = 12 + pixBytes;
    uint8_t* data = (uint8_t*)malloc(total);
    if (!data) return false;
    uint32_t magic = RP16_MAGIC;
    memcpy(data, &magic, 4);
    memcpy(data + 4, &w, 4);
    memcpy(data + 8, &h, 4);
    memcpy(data + 12, pixels, pixBytes);
    *outData = data;
    *outSize = total;
    return true;
}

// ── Copy 16-bit RT to clipboard (CF_DIB for external + custom format for the app) ──
void Clipboard_CopyRT16(RenderTexture2D rt) {
    if (rt.id == 0 || rt.texture.id == 0) return;
    int w = rt.texture.width, h = rt.texture.height;
    if (w < 1 || h < 1) return;

    Image img = LoadImageFromTexture(rt.texture);
    if (!img.data) return;
    ImageFlipVertical(&img);

    size_t pixelCount = (size_t)w * h;
    bool is16 = (img.format == PIXELFORMAT_UNCOMPRESSED_R16G16B16A16);

    // --- 8-bit RGBA for CF_DIB (external apps) ---
    // raylib returns half-float data on desktop GL for 16-bit textures;
    // convert to 8-bit so external apps receive valid pixel data.
    Image dibImg = {0};
    if (is16) {
        dibImg = ImageCopy(img);
        ImageFormat(&dibImg, PIXELFORMAT_UNCOMPRESSED_R8G8B8A8);
    }

    // --- 16-bit data for custom format (internal paste) ---
    // Use the GPU-native data as-is.  On desktop GL this is half-float,
    // matching what LoadTextureFromImage expects for R16G16B16A16, so
    // the data round-trips correctly through paste.
    // On GLES where the readback is 8-bit, upscale to uint16.
    const uint16_t* custom16 = NULL;
    uint16_t* upscaleBuf = NULL;
    if (is16) {
        custom16 = (const uint16_t*)img.data;
    } else {
        upscaleBuf = (uint16_t*)malloc(pixelCount * 4 * sizeof(uint16_t));
        if (upscaleBuf) {
            uint8_t* s = (uint8_t*)img.data;
            for (size_t i = 0; i < pixelCount; i++) {
                upscaleBuf[i * 4 + 0] = (uint16_t)s[i * 4 + 0] * 257;
                upscaleBuf[i * 4 + 1] = (uint16_t)s[i * 4 + 1] * 257;
                upscaleBuf[i * 4 + 2] = (uint16_t)s[i * 4 + 2] * 257;
                upscaleBuf[i * 4 + 3] = (uint16_t)s[i * 4 + 3] * 257;
            }
            custom16 = upscaleBuf;
        }
    }

    if (custom16) {
        void* encData = NULL;
        size_t encSize = 0;
        if (Encode16BitData(w, h, custom16, &encData, &encSize)) {
            ClipboardPlatform_SetImageWithCustom(w, h,
                is16 ? dibImg.data : img.data,
                "RePaint16BitImage", encData, encSize);
            free(encData);
        }
    }

    free(upscaleBuf);
    if (dibImg.data) UnloadImage(dibImg);
    UnloadImage(img);
}

// ── 8-bit fallback (non-16-bit RTs, external compatibility) ───────────
void Clipboard_CopyTexture(Texture2D tex) {
    Image img = LoadImageFromTexture(tex);
    if (img.data) {
        ImageFlipVertical(&img);
        ClipboardPlatform_SetImage(img.width, img.height, img.data);
        UnloadImage(img);
    }
}

void Clipboard_CopyFile(const char* path) {
    ClipboardPlatform_SetFilePath(path);
}
