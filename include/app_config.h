#ifndef APP_CONFIG_H
#define APP_CONFIG_H

struct AppConfig {
    char lastServer[256];
    char lastUsername[64];
};

void AppConfig_Load(AppConfig* cfg, const char* path);
bool AppConfig_Save(AppConfig* cfg, const char* path);

#endif
