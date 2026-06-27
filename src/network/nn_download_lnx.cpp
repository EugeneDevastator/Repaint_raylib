#include "nn_download.h"
#include <curl/curl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

struct WriteBuf {
    FILE* fp;
    int64_t received;
    int64_t total;
    nn_download_progress_t progress;
};

static size_t write_cb(void* ptr, size_t size, size_t nmemb, void* userdata) {
    WriteBuf* buf = (WriteBuf*)userdata;
    size_t n = fwrite(ptr, size, nmemb, buf->fp);
    buf->received += (int64_t)n;
    if (buf->progress)
        buf->progress(buf->received, buf->total);
    return n;
}

static int progress_cb(void* clientp, curl_off_t dltotal, curl_off_t dlnow,
                        curl_off_t ultotal, curl_off_t ulnow) {
    (void)ultotal; (void)ulnow;
    WriteBuf* buf = (WriteBuf*)clientp;
    if (dltotal > 0) buf->total = (int64_t)dltotal;
    return 0;
}

bool nn_download(const char* url, const char* dest_path, nn_download_progress_t progress) {
    CURL* curl = curl_easy_init();
    if (!curl) return false;

    FILE* fp = fopen(dest_path, "wb");
    if (!fp) { curl_easy_cleanup(curl); return false; }

    WriteBuf buf = {fp, 0, 0, progress};

    curl_easy_setopt(curl, CURLOPT_URL, url);
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, write_cb);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, &buf);
    curl_easy_setopt(curl, CURLOPT_NOPROGRESS, 0L);
    curl_easy_setopt(curl, CURLOPT_XFERINFOFUNCTION, progress_cb);
    curl_easy_setopt(curl, CURLOPT_XFERINFODATA, &buf);
    curl_easy_setopt(curl, CURLOPT_FOLLOWLOCATION, 1L);
    curl_easy_setopt(curl, CURLOPT_CONNECTTIMEOUT, 30L);
    curl_easy_setopt(curl, CURLOPT_USERAGENT, "nnserver/1.0");

    if (progress) progress(0, 0);

    CURLcode res = curl_easy_perform(curl);
    fclose(fp);
    curl_easy_cleanup(curl);

    if (res != CURLE_OK) {
        remove(dest_path);
        return false;
    }

    return true;
}
