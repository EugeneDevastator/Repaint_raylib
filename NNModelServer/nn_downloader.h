#pragma once
#include <string>
#include <stdint.h>

struct ModelInfo {
    const char* name;
    const char* filename;   // local file name (CWD or cache/<filename>)
    const char* url;        // download URL
    long long   size;       // 0 = unknown
};

/* global no-prompt mode (matches --no-prompt flag) */
void dl_set_no_prompt(bool v);

/* persistent cache directory (exe_dir/nnmodels/) */
std::string dl_cache_dir();

/* check if a file exists on disk */
bool dl_file_exists(const char* path);

/* timed prompt — returns true if user pressed a key within seconds */
bool dl_prompt_timed(const char* msg, int seconds);

/* download a URL to a local file path with progress bar */
bool dl_download(const std::string& url, const std::string& path);

/* reseolve matte model: CWD → cache → prompt → download */
std::string dl_resolve_matte(const char* model_arg, const char* url_arg);

/* resolve optional model: CWD → cache → timed prompt → download */
std::string dl_resolve_optional(const ModelInfo& info);

/* predefined models */
extern const ModelInfo DL_SD_MODEL;
extern const ModelInfo DL_UPSCALER_MODEL;
