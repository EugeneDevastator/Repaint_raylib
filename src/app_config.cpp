#include "app_config.h"
#include <stdio.h>
#include <string.h>

void AppConfig_Load(AppConfig* cfg, const char* path) {
    cfg->lastServer[0] = '\0';
    cfg->lastUsername[0] = '\0';

    FILE* f = fopen(path, "r");
    if (!f) return;

    char line[512];
    while (fgets(line, sizeof(line), f)) {
        size_t len = strlen(line);
        while (len > 0 && (line[len - 1] == '\n' || line[len - 1] == '\r'))
            line[--len] = '\0';

        char key[128], val[384];
        if (sscanf(line, "%127[^=]=%383[^\n]", key, val) == 2) {
            if (strcmp(key, "lastserver") == 0)
                strncpy(cfg->lastServer, val, sizeof(cfg->lastServer) - 1);
            else if (strcmp(key, "lastusername") == 0)
                strncpy(cfg->lastUsername, val, sizeof(cfg->lastUsername) - 1);
        }
    }
    fclose(f);
}

bool AppConfig_Save(AppConfig* cfg, const char* path) {
    FILE* f = fopen(path, "w");
    if (!f) return false;
    fprintf(f, "lastserver=%s\n", cfg->lastServer);
    fprintf(f, "lastusername=%s\n", cfg->lastUsername);
    fclose(f);
    return true;
}
