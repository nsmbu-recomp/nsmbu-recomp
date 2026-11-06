#pragma once

namespace map_menu {

constexpr float kBtnW = 416.f;
constexpr float kBtnH = 268.f;
constexpr float kColLeft = -424.f;
constexpr float kColMid = 0.f;
constexpr float kColRight = 424.f;
constexpr float kRowTopY = 114.f;
constexpr float kRowBotY = -114.f;
constexpr float kGroupX = 0.f;
constexpr float kGroupY = 25.f;

struct Rect {
    float x, y, w, h;
};

inline Rect tile_from_center(float cx, float cy, float w, float h) {
    Rect r;
    r.w = w;
    r.h = h;
    r.x = (cx + 640.f) - w * 0.5f;
    r.y = (360.f - cy) - h * 0.5f;
    return r;
}

inline float bottom_column(float current_x) {
    return current_x < -50.f ? kColLeft : kColMid;
}

inline Rect column_tile(float col, float row_y) {
    return tile_from_center(kGroupX + col, kGroupY + row_y, kBtnW, kBtnH);
}

inline Rect settings_tile() { return column_tile(kColRight, kRowBotY); }

inline bool overlap(Rect a, Rect b) {
    return a.x < b.x + b.w && b.x < a.x + a.w && a.y < b.y + b.h && b.y < a.y + a.h;
}

inline bool contains(Rect r, float x, float y) {
    return x >= r.x && x < r.x + r.w && y >= r.y && y < r.y + r.h;
}

inline bool window_to_screen(float nx, float ny, float win_w, float win_h, float ox, float oy, float ow, float oh,
                             float& sx, float& sy) {
    if (!(win_w > 1.f && win_h > 1.f && ow > 1.f && oh > 1.f)) return false;
    const float px = nx * win_w;
    const float py = ny * win_h;
    if (px < ox || py < oy || px >= ox + ow || py >= oy + oh) return false;
    sx = (px - ox) / ow * 1280.f;
    sy = (py - oy) / oh * 720.f;
    return true;
}

struct ButtonProbe {
    int index = -1;
    bool selected = false;
    bool rect = false;
    float x0 = 0, y0 = 0, x1 = 0, y1 = 0;
    char name[48] = {};
};

struct MenuProbe {
    bool open = false;
    int cursor = -1;
    int count = 0;
    ButtonProbe buttons[16];
};

void probe(MenuProbe& out);

bool enabled();
void set_enabled(bool on);

bool visible();
void prepare(float win_w, float win_h, bool overlay_open);
void draw();
bool take_open();
bool pointer(float nx, float ny);
bool click();

}
