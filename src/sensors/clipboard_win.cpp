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

// ── Image copy ────────────────────────────────────────────────────────
bool ClipboardPlatform_SetImage(int w, int h, const void* rgba) {
    int bpp = 32;
    int stride = ((w * bpp + 31) / 32) * 4;   // padded byte width per row
    DWORD headerSize = sizeof(BITMAPINFOHEADER);
    DWORD pixelBytes = h * stride;
    DWORD totalSize = headerSize + pixelBytes;

    HANDLE hGlobal = GlobalAlloc(GMEM_MOVEABLE, totalSize);
    if (!hGlobal) return false;

    BYTE* dst = (BYTE*)GlobalLock(hGlobal);
    if (!dst) { GlobalFree(hGlobal); return false; }

    // BITMAPINFOHEADER (bottom-up: positive height)
    BITMAPINFOHEADER* bi = (BITMAPINFOHEADER*)dst;
    bi->biSize          = headerSize;
    bi->biWidth         = w;
    bi->biHeight        = h;   // positive = bottom-up
    bi->biPlanes        = 1;
    bi->biBitCount      = (WORD)bpp;
    bi->biCompression   = BI_RGB;
    bi->biSizeImage     = pixelBytes;
    bi->biXPelsPerMeter = 0;
    bi->biYPelsPerMeter = 0;
    bi->biClrUsed       = 0;
    bi->biClrImportant  = 0;

    // Convert RGBA top-down → BGRA bottom-up
    BYTE* pixels = dst + headerSize;
    const BYTE* src = (const BYTE*)rgba;
    int rowBytes = w * 4;

    for (int y = 0; y < h; y++) {
        int dstY = h - 1 - y;   // bottom-up
        BYTE* dRow = pixels + dstY * stride;
        const BYTE* sRow = src + y * rowBytes;
        for (int x = 0; x < w; x++) {
            dRow[x * 4 + 0] = sRow[x * 4 + 2];  // B
            dRow[x * 4 + 1] = sRow[x * 4 + 1];  // G
            dRow[x * 4 + 2] = sRow[x * 4 + 0];  // R
            dRow[x * 4 + 3] = sRow[x * 4 + 3];  // A
        }
    }

    GlobalUnlock(hGlobal);

    if (!OpenClipboard(NULL)) { GlobalFree(hGlobal); return false; }
    EmptyClipboard();
    HANDLE result = SetClipboardData(CF_DIB, hGlobal);
    CloseClipboard();

    if (!result) { GlobalFree(hGlobal); return false; }
    return true;
}
