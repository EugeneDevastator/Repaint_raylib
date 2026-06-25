#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <shellapi.h>
#include <cstdlib>
#include <cstring>

// DROPFILES is missing in some MinGW builds — define it manually
typedef struct {
    DWORD pFiles;
    POINT pt;
    BOOL  fNC;
    BOOL  fWide;
} _DROPFILES;

// ── Image paste ───────────────────────────────────────────────────────
bool ClipboardPlatform_GetImage(int* outW, int* outH, void** outRGBA) {
    if (!OpenClipboard(NULL)) return false;

    UINT fmt = IsClipboardFormatAvailable(CF_DIBV5) ? CF_DIBV5 : CF_DIB;
    if (!IsClipboardFormatAvailable(fmt)) { CloseClipboard(); return false; }

    HANDLE h = GetClipboardData(fmt);
    if (!h) { CloseClipboard(); return false; }

    BITMAPINFO* bmi = (BITMAPINFO*)GlobalLock(h);
    if (!bmi) { CloseClipboard(); return false; }

    int w = bmi->bmiHeader.biWidth;
    int hAbs = abs(bmi->bmiHeader.biHeight);
    int bpp = bmi->bmiHeader.biBitCount;
    DWORD comp = bmi->bmiHeader.biCompression;

    if (bpp != 32 && bpp != 24) { GlobalUnlock(h); CloseClipboard(); return false; }

    DWORD headerSize = bmi->bmiHeader.biSize;
    const BYTE* base = (const BYTE*)bmi;

    // Pixel data offset: skip header + optional bitfield masks
    DWORD pixOff = headerSize;
    if (comp == BI_BITFIELDS && headerSize == 40) pixOff += 12;

    int stride = ((w * bpp + 31) / 32) * 4;
    int rowBytes = w * 4;
    BYTE* rgba = (BYTE*)malloc(hAbs * rowBytes);
    if (!rgba) { GlobalUnlock(h); CloseClipboard(); return false; }

    bool bottomUp = (bmi->bmiHeader.biHeight > 0);

    for (int y = 0; y < hAbs; y++) {
        int srcY = bottomUp ? (hAbs - 1 - y) : y;
        const BYTE* src = base + pixOff + srcY * stride;
        BYTE* dst = rgba + y * rowBytes;

        if (bpp == 32) {
            for (int x = 0; x < w; x++) {
                dst[x * 4 + 0] = src[x * 4 + 2];
                dst[x * 4 + 1] = src[x * 4 + 1];
                dst[x * 4 + 2] = src[x * 4 + 0];
                dst[x * 4 + 3] = src[x * 4 + 3];
            }
        } else {
            for (int x = 0; x < w; x++) {
                dst[x * 4 + 0] = src[x * 3 + 2];
                dst[x * 4 + 1] = src[x * 3 + 1];
                dst[x * 4 + 2] = src[x * 3 + 0];
                dst[x * 4 + 3] = 255;
            }
        }
    }

    GlobalUnlock(h);
    CloseClipboard();

    *outW = w;
    *outH = hAbs;
    *outRGBA = rgba;
    return true;
}

// ── File path paste (CF_HDROP) ───────────────────────────────────────
bool ClipboardPlatform_GetFilePath(char* path, int maxLen) {
    if (!OpenClipboard(NULL)) return false;

    HANDLE h = GetClipboardData(CF_HDROP);
    if (!h) { CloseClipboard(); return false; }

    HDROP drop = (HDROP)GlobalLock(h);
    if (!drop) { CloseClipboard(); return false; }

    bool ok = false;
    UINT count = DragQueryFileW(drop, 0xFFFFFFFF, NULL, 0);
    if (count > 0) {
        wchar_t wbuf[1024];
        if (DragQueryFileW(drop, 0, wbuf, 1024) > 0) {
            WideCharToMultiByte(CP_UTF8, 0, wbuf, -1, path, maxLen, NULL, NULL);
            ok = true;
        }
    }

    GlobalUnlock(drop);
    CloseClipboard();
    return ok;
}

// ── Text paste (CF_UNICODETEXT / CF_TEXT) ────────────────────────────
bool ClipboardPlatform_GetText(char* text, int maxLen) {
    if (!OpenClipboard(NULL)) return false;

    bool ok = false;

    // Try Unicode first
    HANDLE h = GetClipboardData(CF_UNICODETEXT);
    if (h) {
        wchar_t* wstr = (wchar_t*)GlobalLock(h);
        if (wstr) {
            WideCharToMultiByte(CP_UTF8, 0, wstr, -1, text, maxLen, NULL, NULL);
            ok = true;
            GlobalUnlock(h);
        }
    }

    // Fall back to ANSI
    if (!ok) {
        HANDLE h2 = GetClipboardData(CF_TEXT);
        if (h2) {
            char* str = (char*)GlobalLock(h2);
            if (str) {
                strncpy(text, str, maxLen - 1);
                text[maxLen - 1] = '\0';
                ok = true;
                GlobalUnlock(h2);
            }
        }
    }

    CloseClipboard();
    return ok;
}

// ── File path copy (CF_HDROP) ──────────────────────────────────────────
bool ClipboardPlatform_SetFilePath(const char* path) {
    int wlen = MultiByteToWideChar(CP_UTF8, 0, path, -1, NULL, 0);
    if (wlen <= 0) return false;

    DWORD headerSize = sizeof(_DROPFILES);
    DWORD pathBytes = wlen * sizeof(WCHAR);
    DWORD totalSize = headerSize + pathBytes + sizeof(WCHAR); // extra null for double-null terminator

    HANDLE hGlobal = GlobalAlloc(GMEM_MOVEABLE | GMEM_ZEROINIT, totalSize);
    if (!hGlobal) return false;

    BYTE* dst = (BYTE*)GlobalLock(hGlobal);
    if (!dst) { GlobalFree(hGlobal); return false; }

    _DROPFILES* df = (_DROPFILES*)dst;
    df->pFiles = headerSize;
    df->pt.x = 0;
    df->pt.y = 0;
    df->fNC = FALSE;
    df->fWide = TRUE;

    MultiByteToWideChar(CP_UTF8, 0, path, -1, (LPWSTR)(dst + headerSize), wlen);
    // dst + headerSize + pathBytes already zeroed by ZEROINIT → double null

    GlobalUnlock(hGlobal);

    if (!OpenClipboard(NULL)) { GlobalFree(hGlobal); return false; }
    EmptyClipboard();
    HANDLE result = SetClipboardData(CF_HDROP, hGlobal);
    CloseClipboard();

    if (!result) { GlobalFree(hGlobal); return false; }
    return true;
}

// ── Helper: allocate a CF_DIBV5 handle from BGRA pixel data ───────────
static HANDLE CreateDibV5(int w, int h, DWORD pixelBytes, const BYTE* pixBuf) {
    DWORD v5HdrSize = sizeof(BITMAPV5HEADER);
    HANDLE hDib = GlobalAlloc(GMEM_MOVEABLE | GMEM_ZEROINIT, v5HdrSize + pixelBytes);
    if (!hDib) return NULL;
    BYTE* d = (BYTE*)GlobalLock(hDib);
    if (!d) { GlobalFree(hDib); return NULL; }
    BITMAPV5HEADER* v5 = (BITMAPV5HEADER*)d;
    v5->bV5Size          = v5HdrSize;
    v5->bV5Width         = w;
    v5->bV5Height        = h;
    v5->bV5Planes        = 1;
    v5->bV5BitCount      = 32;
    v5->bV5Compression   = BI_RGB;
    v5->bV5SizeImage     = pixelBytes;
    v5->bV5AlphaMask     = 0xFF000000;
    memcpy(d + v5HdrSize, pixBuf, pixelBytes);
    GlobalUnlock(hDib);
    return hDib;
}

// ── Custom data (private clipboard format) ────────────────────────────
bool ClipboardPlatform_GetCustomData(const char* name, void** data, size_t* size) {
    UINT fmt = RegisterClipboardFormatA(name);
    if (!fmt || !IsClipboardFormatAvailable(fmt)) return false;

    if (!OpenClipboard(NULL)) return false;
    HANDLE h = GetClipboardData(fmt);
    if (!h) { CloseClipboard(); return false; }

    void* src = GlobalLock(h);
    if (!src) { CloseClipboard(); return false; }

    SIZE_T sz = GlobalSize(h);
    *data = malloc((size_t)sz);
    if (*data) {
        memcpy(*data, src, (size_t)sz);
        *size = (size_t)sz;
    }
    GlobalUnlock(h);
    CloseClipboard();
    return *data != NULL;
}

// ── Combined image + custom data copy (single OpenClipboard cycle) ────
bool ClipboardPlatform_SetImageWithCustom(int w, int h, const void* rgba8,
    const char* customName, const void* customData, size_t customSize)
{
    int bpp = 32;
    int stride = ((w * bpp + 31) / 32) * 4;
    DWORD pixelBytes = h * stride;

    // ── Fill one pixel buffer, share it between DIB and DIBV5 ──
    // (BGRA bottom-up bytes)
    BYTE* pixBuf = (BYTE*)malloc(pixelBytes);
    if (!pixBuf) return false;
    const BYTE* src = (const BYTE*)rgba8;
    int rowBytes = w * 4;
    for (int y = 0; y < h; y++) {
        int dstY = h - 1 - y;
        BYTE* dRow = pixBuf + dstY * stride;
        const BYTE* sRow = src + y * rowBytes;
        for (int x = 0; x < w; x++) {
            dRow[x * 4 + 0] = sRow[x * 4 + 2];  // B
            dRow[x * 4 + 1] = sRow[x * 4 + 1];  // G
            dRow[x * 4 + 2] = sRow[x * 4 + 0];  // R
            dRow[x * 4 + 3] = sRow[x * 4 + 3];  // A
        }
    }

    // ── CF_DIB (backward compat, no explicit alpha) ──
    DWORD dibHdrSize = sizeof(BITMAPINFOHEADER);
    HANDLE hDib = GlobalAlloc(GMEM_MOVEABLE | GMEM_ZEROINIT, dibHdrSize + pixelBytes);
    if (!hDib) { free(pixBuf); return false; }
    {
        BYTE* d = (BYTE*)GlobalLock(hDib);
        if (!d) { GlobalFree(hDib); free(pixBuf); return false; }
        BITMAPINFOHEADER* bi = (BITMAPINFOHEADER*)d;
        bi->biSize        = dibHdrSize;
        bi->biWidth       = w;
        bi->biHeight      = h;
        bi->biPlanes      = 1;
        bi->biBitCount    = (WORD)bpp;
        bi->biCompression = BI_RGB;
        bi->biSizeImage   = pixelBytes;
        memcpy(d + dibHdrSize, pixBuf, pixelBytes);
        GlobalUnlock(hDib);
    }

    HANDLE hDibV5 = CreateDibV5(w, h, pixelBytes, pixBuf);

    free(pixBuf);

    // ── Custom format data ──
    HANDLE hCustom = NULL;
    UINT customFmt = 0;
    if (customName && customData && customSize > 0) {
        customFmt = RegisterClipboardFormatA(customName);
        if (customFmt) {
            hCustom = GlobalAlloc(GMEM_MOVEABLE, customSize);
            if (hCustom) {
                void* cDst = GlobalLock(hCustom);
                if (cDst) { memcpy(cDst, customData, customSize); GlobalUnlock(hCustom); }
                else { GlobalFree(hCustom); hCustom = NULL; }
            }
        }
    }

    if (!OpenClipboard(NULL)) {
        GlobalFree(hDib);
        if (hDibV5) GlobalFree(hDibV5);
        if (hCustom) GlobalFree(hCustom);
        return false;
    }
    EmptyClipboard();
    if (hDibV5) SetClipboardData(CF_DIBV5, hDibV5);
    SetClipboardData(CF_DIB, hDib);
    if (hCustom && customFmt) SetClipboardData(customFmt, hCustom);
    CloseClipboard();
    return true;
}

// ── Image copy (8-bit only — used when no 16-bit data) ────────────────
bool ClipboardPlatform_SetImage(int w, int h, const void* rgba) {
    int bpp = 32;
    int stride = ((w * bpp + 31) / 32) * 4;
    DWORD pixelBytes = h * stride;
    const BYTE* src = (const BYTE*)rgba;
    int rowBytes = w * 4;

    // ── Build BGRA bottom-up pixel buffer ──
    BYTE* pixBuf = (BYTE*)malloc(pixelBytes);
    if (!pixBuf) return false;
    for (int y = 0; y < h; y++) {
        int dstY = h - 1 - y;
        BYTE* dRow = pixBuf + dstY * stride;
        const BYTE* sRow = src + y * rowBytes;
        for (int x = 0; x < w; x++) {
            dRow[x * 4 + 0] = sRow[x * 4 + 2];
            dRow[x * 4 + 1] = sRow[x * 4 + 1];
            dRow[x * 4 + 2] = sRow[x * 4 + 0];
            dRow[x * 4 + 3] = sRow[x * 4 + 3];
        }
    }

    // ── CF_DIB ──
    DWORD dibHdrSize = sizeof(BITMAPINFOHEADER);
    HANDLE hDib = GlobalAlloc(GMEM_MOVEABLE | GMEM_ZEROINIT, dibHdrSize + pixelBytes);
    if (!hDib) { free(pixBuf); return false; }
    {
        BYTE* d = (BYTE*)GlobalLock(hDib);
        if (!d) { GlobalFree(hDib); free(pixBuf); return false; }
        BITMAPINFOHEADER* bi = (BITMAPINFOHEADER*)d;
        bi->biSize        = dibHdrSize;
        bi->biWidth       = w;
        bi->biHeight      = h;
        bi->biPlanes      = 1;
        bi->biBitCount    = (WORD)bpp;
        bi->biCompression = BI_RGB;
        bi->biSizeImage   = pixelBytes;
        memcpy(d + dibHdrSize, pixBuf, pixelBytes);
        GlobalUnlock(hDib);
    }

    HANDLE hDibV5 = CreateDibV5(w, h, pixelBytes, pixBuf);

    free(pixBuf);

    if (!OpenClipboard(NULL)) { GlobalFree(hDib); if (hDibV5) GlobalFree(hDibV5); return false; }
    EmptyClipboard();
    if (hDibV5) SetClipboardData(CF_DIBV5, hDibV5);
    SetClipboardData(CF_DIB, hDib);
    CloseClipboard();
    return true;
}

// ── PNG clipboard format (raw PNG data, for apps that prefer it) ──────
void ClipboardPlatform_SetPNG(const void* data, size_t size) {
    HANDLE h = GlobalAlloc(GMEM_MOVEABLE, (DWORD)size);
    if (!h) return;
    void* dst = GlobalLock(h);
    if (dst) { memcpy(dst, data, size); GlobalUnlock(h); }
    else { GlobalFree(h); return; }

    UINT fmt = RegisterClipboardFormatA("PNG");
    if (fmt && OpenClipboard(NULL)) {
        SetClipboardData(fmt, h);
        CloseClipboard();
    } else {
        GlobalFree(h);
    }
}
