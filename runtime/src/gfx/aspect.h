
#pragma once
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>

namespace aspect {
enum Mode : int { kOriginal = 0, kWindow, k16x10, k21x9, k32x9, kCustom, kModes };
int mode();
void set_mode(int m);
const char* mode_name(int m);
float mode_value(int m);
inline int mode_from_text(const char* text, float& custom) {
    if (!text || !*text) return kOriginal;
    if (!strcmp(text, "window") || !strcmp(text, "match")) return kWindow;
    if (!strcmp(text, "16:9")) return kOriginal;
    if (!strcmp(text, "16:10")) return k16x10;
    if (!strcmp(text, "21:9")) return k21x9;
    if (!strcmp(text, "32:9")) return k32x9;
    float a = 0;
    if (const char* c = strchr(text, ':')) a = (float)atof(text) / (float)atof(c + 1);
    else a = (float)atof(text);
    if (!(a > 0.5f && a < 8.0f)) return kOriginal;
    custom = a;
    return kCustom;
}
inline void text_from_mode(int m, float custom, char* out, size_t n) {
    if (!out || !n) return;
    if (m == kCustom) {
        snprintf(out, n, "%.6g", custom);
        return;
    }
    static const char* token[] = {"16:9", "window", "16:10", "21:9", "32:9"};
    snprintf(out, n, "%s", m >= 0 && m < kCustom ? token[m] : "16:9");
}
void apply_choice(const char* text);
void choice_text(char* out, size_t n);
bool stretch_image();
void set_stretch_image(bool stretch);
void set_window_aspect(float a);
float requested();

float on_swap();
float game();
uint64_t game_frame();

bool tagged_projection();
void layout_root_target(uint32_t root, bool tv);
void set_content_clip(bool clip);
bool content_clip();
void set_edge_hud(bool edge);
bool edge_hud();
void set_cover_draw(bool cover);
bool cover_draw();
void ss_reset();
}
