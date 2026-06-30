#ifndef INFO_TEXT_H
#define INFO_TEXT_H

// ── Self-contained floating info line ─────────────────────────────────
// Displays a notification at the top-center of the screen with:
//   • Cadman font at 1.3× base size
//   • Auto-dismiss after (wordCount/5) seconds, min 1.5s
//   • Fades out over the last 0.5s
//   • Hover pauses the timer while text is still fully visible
//   • Never interferes with any input — uses raylib draw only
//
// Dependencies: raylib (Font, Vector2, Color), g_dialogFont must exist.
// Remove info_text.cpp from build if unused — no other changes needed.

void InfoText_Show(const char* text);
void InfoText_Draw(void);

#endif
