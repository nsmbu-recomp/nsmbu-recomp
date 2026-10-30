

#pragma once
#include <cstdint>

namespace interp {
void record_executed_step();
uint64_t executed_steps();
bool course_play_active();
bool input_enabled();
bool player_witness(float& x, float& y, float& z);

struct PlayerSnapshot {
    int player = 0;
    int32_t lives = 0;
    int32_t coins = 0;
    int32_t course_coins = 0;
    int32_t mode = 0;
    float x = 0;
    float y = 0;
    float z = 0;
};
bool player_snapshot(PlayerSnapshot& out, int player = 0);
bool set_player_mode(int player, int mode);
uint64_t logic_steps();
bool hold_pass();
float pass_t();
int mode();
void set_mode(int m);
int fps();
void set_fps(int f);
bool custom_on();
int custom_fps();
void set_custom_fps(int f);
void set_fast_forward(bool on);
bool fast_forward();
void set_unlocked(bool on);
bool unlocked();
void toggle_fps(int f);
const char* mode_name();
int output_fps();
int frames_per_step();
bool holds_shed();
bool refresh_boost();
int refresh_request_hz(int max_hz);
void begin_course_setup();
void end_course_setup();
bool course_setup_active();

bool paced_interpolation();
void set_paced_interpolation(bool on);
bool paced_interpolation_at(int fps);
void set_paced_interpolation_at(int fps, bool on);
float paced_drawn_share();

void set_display_hz(int hz);
int display_hz();
void set_present_vsync(bool on);
}
