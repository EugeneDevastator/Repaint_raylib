#include "nn_download.h"
#include <windows.h>
#include <winhttp.h>
#include <stdio.h>
#include <stdlib.h>
#include <string>
#include <vector>

bool nn_download(const char* url, const char* dest_path, nn_download_progress_t progress) {
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
    int64_t received = 0;
    int64_t totalSize = 0;

    do {
        if (!WinHttpSendRequest(hRequest, WINHTTP_NO_ADDITIONAL_HEADERS, 0, NULL, 0, 0, 0))
            break;
        if (!WinHttpReceiveResponse(hRequest, NULL))
            break;

        WCHAR clenStr[32] = {0};
        DWORD clenSize = sizeof(clenStr);
        if (WinHttpQueryHeaders(hRequest, WINHTTP_QUERY_CONTENT_LENGTH,
                NULL, clenStr, &clenSize, WINHTTP_NO_HEADER_INDEX))
            totalSize = _wtoi64(clenStr);

        fp = fopen(dest_path, "wb");
        if (!fp) break;

        if (progress) progress(0, totalSize);

        char buf[65536];

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

            if (progress) progress(received, totalSize);
        }
        ok = true;
    } while (false);

    if (fp) fclose(fp);
    WinHttpCloseHandle(hRequest);
    WinHttpCloseHandle(hConnect);
    WinHttpCloseHandle(hSession);

    if (!ok) { remove(dest_path); return false; }
    return true;
}
