#include "repaint.h"
#include "imgui.h"
#include <stdio.h>

static bool g_showChangelog = false;
static char g_changelogText[4096] = {0};
static bool g_changelogLoaded = false;

void Changelog_Init(void) {
    const char* ad = GetApplicationDirectory();
    char path[1024];
    snprintf(path, sizeof(path), "%sCHANGELOG.txt", ad);

    if (FileExists(path)) {
        long size = 0;
        FILE* f = fopen(path, "rb");
        if (f) {
            fseek(f, 0, SEEK_END);
            size = ftell(f);
            fseek(f, 0, SEEK_SET);
            if (size > 0 && size < (long)sizeof(g_changelogText) - 1) {
                size_t read = fread(g_changelogText, 1, size, f);
                g_changelogText[read] = '\0';
            }
            fclose(f);
        }
    }
    g_changelogLoaded = true;
}

void Changelog_Toggle(void) {
    g_showChangelog = !g_showChangelog;
}

void Changelog_Draw(void) {
    if (!g_showChangelog) return;

    ImGui::SetNextWindowSize(ImVec2(400, 300), ImGuiCond_FirstUseEver);
    if (ImGui::Begin("Changelog", &g_showChangelog)) {
        if (g_changelogLoaded && g_changelogText[0]) {
            ImGui::TextUnformatted(g_changelogText);
        } else {
            ImGui::Text("No changelog found.");
            ImGui::Text("AI can write changes to CHANGELOG.txt");
            ImGui::Text("next to the executable.");
        }
    }
    ImGui::End();
}
