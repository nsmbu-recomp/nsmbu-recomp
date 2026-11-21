

#pragma once
#include <cstdint>

struct ImDrawData;

namespace overlay {

bool is_open();
const char* settings_tab();
bool wants_text_input();
void set_open(bool open);

bool blocks_input();

bool captures();

bool alive();
bool perf_shown();
void set_perf_shown(bool on);
bool perf_timing_shown();
void set_perf_timing_shown(bool on);
bool perf_presentation_shown();
void set_perf_presentation_shown(bool on);
bool perf_detail_shown();
void set_perf_detail_shown(bool on);
bool perf_overlay_active();

enum Mods : int { kShift = 1, kCtrl = 2, kAlt = 4, kSuper = 8 };

bool key(int code, bool down, bool repeat, int mods);

bool mouse_move(float nx, float ny, bool touch = false);
bool mouse_button(int button, bool down, bool touch = false);
// The X drawn at the top right of the settings window, in 0..1 display space.
bool close_button(float& x0, float& y0, float& x1, float& y1);
// Presses that X with the pointer. The overlay has to be open and the button drawn.
bool click_close(float& x, float& y);
bool mouse_wheel(float dx, float dy);
void set_density(float pixels_per_point);
bool text(const char* utf8);

ImDrawData* frame(float pw, float ph, void (*renderer_init)());
void undo_frame_sample();

void linearize_colors(ImDrawData* d);

}
