#pragma once
#include <stdint.h>
#include <stddef.h>

typedef void (*nn_download_progress_t)(int64_t received, int64_t total);

bool nn_download(const char* url, const char* dest_path, nn_download_progress_t progress);
