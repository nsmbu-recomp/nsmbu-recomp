#pragma once

#include <cstdint>
#include <cstring>

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

// The layout load returns the file bytes and does not keep the file name in a register.
// The start menu is the layout that still has Main Menu and does not yet have Settings.
inline bool contains_cstr(const uint8_t* data, uint32_t size, const char* name) {
    const size_t n = std::strlen(name);
    if (!data || size < n + 1) return false;
    for (uint32_t i = 0; i + n < size; i++) {
        if (std::memcmp(data + i, name, n) == 0 && data[i + n] == 0) return true;
    }
    return false;
}

inline bool start_menu_flyt(const uint8_t* data, uint32_t size) {
    return contains_cstr(data, size, "Pa_TitleBtn_00") && !contains_cstr(data, size, "Pa_SetBtn_00");
}

// PaCursor keeps a normal button word at 0. A finished decide writes another word and sets +0x268.
// The port puts the Settings word back to 0 for a few frames after the press. It does not write 1.
struct SettingsRepair {
    bool hold = false;
    uint32_t word = 0;
};

inline SettingsRepair settings_repair(int index, int settings_index, bool confirm, uint32_t word) {
    SettingsRepair repair;
    repair.word = word;
    if (settings_index < 0 || index != settings_index || !confirm) return repair;
    repair.hold = true;
    repair.word = 0;
    return repair;
}

// The menu plays the click, then leaves that state. This is only a cap if that end never arrives.
constexpr int kSettingsClickFrames = 60;

inline int settings_open_delay(bool confirm) {
    return confirm ? kSettingsClickFrames : 0;
}

// A press clears the decide byte so the button can play. Other frames keep the byte set.
inline uint8_t settings_decide_byte(bool pressed) {
    return pressed ? 0 : 1;
}

// InBtn brings the six buttons back. It runs after the overlay closes.
constexpr uint32_t kSettingsMenuReturn = 0x10211DF4u;

inline uint32_t settings_return_state() { return kSettingsMenuReturn; }

// The menu pad and the cursor pad both carry directions. Either one can move the cursor.
inline uint32_t settings_dir_pad(uint32_t cursor_pad, uint32_t menu_pad) {
    return cursor_pad | menu_pad;
}

// Hover copies the previous index over the current one, so a move writes both.
struct SettingsCursorPair {
    int current = -1;
    int previous = -1;
};

inline SettingsCursorPair settings_cursor_pair(int index) {
    SettingsCursorPair pair;
    pair.current = index;
    pair.previous = index;
    return pair;
}

// Button-manager step 2 moves the yellow cursor onto the new button.
inline uint32_t settings_cursor_step() { return 2; }

// The yellow cursor follows the button at this offset, not the manager's current slot at +0x40.
inline uint32_t settings_yellow_target() { return 0x3C; }

// The previous button keeps its selected look until a different button replaces it.
inline bool settings_release_previous(bool previous_valid, bool same_button) {
    return previous_valid && !same_button;
}

// A clear focus byte returns before that step runs. A move sets focus for that frame.
inline bool settings_focus_for_move(bool choosing) { return choosing; }

// After the overlay closes, a direction is handled even when the cursor focus is clear.
// Opening and closing the menu leave that focus clear, so this stays false until the overlay has closed.
inline bool settings_move_without_focus(bool after_overlay, bool blank, bool clicking) {
    return after_overlay && !blank && !clicking;
}

// The yellow cursor stays off the blank page. After the overlay closes it uses the hover state.
inline bool settings_hide_cursor(bool blank) { return blank; }

constexpr uint32_t kSettingsCursorHover = 0x102169B8u;

inline uint32_t settings_cursor_state() { return kSettingsCursorHover; }

// The buttons stay on the blank frame while the overlay is open.
struct SettingsBlank {
    bool hold = false;
    bool restore = false;
};

inline SettingsBlank settings_blank(bool waiting, bool saw_overlay, bool overlay_open) {
    SettingsBlank blank;
    if (!waiting) return blank;
    if (saw_overlay && !overlay_open) {
        blank.restore = true;
        return blank;
    }
    blank.hold = true;
    return blank;
}

// The click ends in the button action. Settings stays on this menu and opens the overlay.
struct SettingsClickEnd {
    bool keep_menu = false;
    bool open_overlay = false;
};

inline SettingsClickEnd settings_click_end(bool settings_button) {
    SettingsClickEnd end;
    if (!settings_button) return end;
    end.keep_menu = true;
    end.open_overlay = true;
    return end;
}

// P_Image_00 and P_Image_01 are the Main Menu pictures inside the copied button.
inline bool settings_icon_name(const uint8_t* pane, uint32_t size) {
    if (!pane || size < 0x54 || std::memcmp(pane, "pic1", 4) != 0) return false;
    const char* names[] = {"P_Image_00", "P_Image_01"};
    for (const char* name : names) {
        const size_t len = std::strlen(name);
        if (size < 0xC + len + 1) continue;
        if (std::memcmp(pane + 0xC, name, len) == 0 && pane[0xC + len] == 0) return true;
    }
    return false;
}

inline void hide_settings_icon(uint8_t* pane) {
    pane[8] = (uint8_t)(pane[8] & ~1u);
    pane[0xA] = 0;
    std::memset(pane + 0x4C, 0, 8);
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
