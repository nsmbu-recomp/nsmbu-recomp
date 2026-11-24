
#pragma once
#include <cstdint>
#include <functional>
#include <string>

namespace input {

constexpr int kMaxPlayers = 4;

enum : uint32_t {
    kA = 0x8000, kB = 0x4000, kX = 0x2000, kY = 0x1000,
    kL = 0x0020, kR = 0x0010, kZL = 0x0080, kZR = 0x0040,
    kPlus = 0x0008, kMinus = 0x0004, kHome = 0x0002,
    kUp = 0x0200, kDown = 0x0100, kLeft = 0x0800, kRight = 0x0400,
    kStickR = 0x00020000, kStickL = 0x00040000,
};

struct PadState {
    uint32_t buttons = 0;
    float lx = 0, ly = 0, rx = 0, ry = 0;
    bool touch = false;
    float tx = 0, ty = 0;
};

void set_touch(bool down, float x, float y);

bool pro_controller();
void set_pro_controller(bool on);

void init();
PadState read();
PadState read(int player);
void release_keys();
void controller_values(float* v);
void controller_values(int player, float* v);
void host_controller_values(float* v);
void host_controller_values(int player, float* v);
bool controller_connected(int player);
std::string controller_name(int player);
void held_keys(bool* keys);

void prompt_text(const std::u16string& initial, int max_len,
                 std::function<void(bool ok, std::u16string text)> done);

bool has_rumble();

void stop_rumble_now();

}
