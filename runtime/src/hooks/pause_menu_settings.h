#pragma once

#include <cstdint>
#include <cstring>
#include <vector>

namespace pause_menu {

// Four story-pause rows. Y is up. The step matches the button height so the boxes meet and do not overlap.
constexpr float kBtnW = 444.96f;
constexpr float kBtnH = 156.f;
constexpr float kContinueY = 234.f;
constexpr float kQuitY = 78.f;
constexpr float kControllerY = -78.f;
constexpr float kSettingsY = -234.f;
constexpr float kWinW = 580.f;
constexpr float kWinH = 720.f;

struct Rect {
    float x, y, w, h;
};

inline Rect tile_at(float cy) {
    Rect r;
    r.w = kBtnW;
    r.h = kBtnH;
    r.x = -kBtnW * 0.5f;
    r.y = cy - kBtnH * 0.5f;
    return r;
}

inline Rect continue_tile() { return tile_at(kContinueY); }
inline Rect quit_tile() { return tile_at(kQuitY); }
inline Rect controller_tile() { return tile_at(kControllerY); }
inline Rect settings_tile() { return tile_at(kSettingsY); }

inline bool overlap(Rect a, Rect b) {
    return a.x < b.x + b.w && b.x < a.x + a.w && a.y < b.y + b.h && b.y < a.y + a.h;
}

inline bool contains_cstr(const uint8_t* data, uint32_t size, const char* name) {
    const size_t n = std::strlen(name);
    if (!data || size < n + 1) return false;
    for (uint32_t i = 0; i + n < size; i++) {
        if (std::memcmp(data + i, name, n) == 0 && data[i + n] == 0) return true;
    }
    return false;
}

// PauseGame.bflyt and PauseGameDL.bflyt both carry these three panes.
// A file that already has Pa_SetBtn_00 is left alone.
inline bool pause_menu_flyt(const uint8_t* data, uint32_t size) {
    return contains_cstr(data, size, "Pa_Continue_00") && contains_cstr(data, size, "Pa_Exit_00") &&
           contains_cstr(data, size, "Pa_PlayStyle_00") && contains_cstr(data, size, "Pa_Win_00") &&
           !contains_cstr(data, size, "Pa_SetBtn_00");
}

// A Settings confirm stays on this menu and opens the overlay.
// Continue, Quit, and Change Controller keep the game's own confirm.
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

inline uint16_t ru16(const uint8_t* p) {
    return (uint16_t)(((uint16_t)p[0] << 8) | p[1]);
}

inline uint32_t ru32(const uint8_t* p) {
    return ((uint32_t)p[0] << 24) | ((uint32_t)p[1] << 16) | ((uint32_t)p[2] << 8) | p[3];
}

inline void wu16(uint8_t* p, uint16_t v) {
    p[0] = (uint8_t)(v >> 8);
    p[1] = (uint8_t)v;
}

inline void wu32(uint8_t* p, uint32_t v) {
    p[0] = (uint8_t)(v >> 24);
    p[1] = (uint8_t)(v >> 16);
    p[2] = (uint8_t)(v >> 8);
    p[3] = (uint8_t)v;
}

inline void wf32(uint8_t* p, float v) {
    uint32_t u = 0;
    std::memcpy(&u, &v, 4);
    wu32(p, u);
}

inline bool pane_named(const uint8_t* s, uint32_t n, const char* name) {
    const size_t len = std::strlen(name);
    if (n < 0xC + len + 1) return false;
    return std::memcmp(s + 0xC, name, len) == 0 && s[0xC + len] == 0;
}

// The baked label sits in the text box just before PauseGame_PlayStyle_00.
// The television string is 16 bytes. The GamePad string is 12. "Settings" is 16 bytes of UTF-16.
// A short field grows. The message id is renamed so the game does not put Change Controller back.
inline void write_settings_label(std::vector<uint8_t>& part) {
    const uint8_t ja[] = {0x30, 0x5D, 0x30, 0x46, 0x30, 0x55, 0x30, 0x92, 0x59, 0x09, 0x30, 0x48, 0x30, 0x8B, 0x00, 0x00};
    const uint8_t en[] = {0x00, 'S', 0x00, 'e', 0x00, 't', 0x00, 't', 0x00, 'i', 0x00, 'n', 0x00, 'g', 0x00, 's'};
    const char* from = "PauseGame_PlayStyle_00";
    const char* to = "PauseGame_SetButton_00";
    const size_t idn = std::strlen(from);
    for (size_t i = 0; i + 8 <= part.size(); i++) {
        if (std::memcmp(part.data() + i, "txt1", 4) != 0) continue;
        const uint32_t tsz = ru32(part.data() + i + 4);
        if (tsz < 0x84 || i + tsz > part.size()) continue;
        size_t id = i + tsz;
        for (size_t s = i; s + idn <= i + tsz; s++) {
            if (std::memcmp(part.data() + s, from, idn) == 0) {
                id = s;
                break;
            }
        }
        if (id >= i + tsz) continue;
        const uint16_t len = ru16(part.data() + i + 0x54);
        const uint32_t text_off = ru32(part.data() + i + 0x64);
        if (text_off + len != (uint32_t)(id - i)) continue;
        if (len < 16) {
            const uint32_t delta = 16u - len;
            const size_t at = i + text_off + len;
            part.insert(part.begin() + (std::vector<uint8_t>::difference_type)at, delta, 0);
            wu32(part.data() + 4, (uint32_t)part.size());
            wu32(part.data() + i + 4, tsz + delta);
            wu16(part.data() + i + 0x54, 16);
            wu16(part.data() + i + 0x56, 16);
            const uint32_t msg = ru32(part.data() + i + 0x80);
            if (msg >= text_off + len) wu32(part.data() + i + 0x80, msg + delta);
        }
        if (text_off + 16 <= part.size() - i) std::memcpy(part.data() + i + text_off, en, sizeof en);
        break;
    }
    for (size_t i = 0; i + sizeof ja <= part.size(); i++) {
        if (std::memcmp(part.data() + i, ja, sizeof ja) != 0) continue;
        std::memcpy(part.data() + i, en, sizeof en);
        break;
    }
    for (size_t i = 0; i + idn <= part.size(); i++) {
        if (std::memcmp(part.data() + i, from, idn) != 0) continue;
        std::memcpy(part.data() + i, to, idn);
        break;
    }
}

// Inserts Pa_SetBtn_00 under Change Controller and grows only Pa_Win_00.
// Returns false when the file is not the story pause menu or already has the button.
inline bool patch_pause_flyt(std::vector<uint8_t>& file) {
    if (file.size() < 0x20 || std::memcmp(file.data(), "FLYT", 4) != 0) return false;
    if (!pause_menu_flyt(file.data(), (uint32_t)file.size())) return false;
    std::vector<uint8_t> out(file.begin(), file.begin() + 0x14);
    bool inserted = false;
    bool placed = false;
    uint32_t off = 0x14;
    while (off + 8 <= file.size()) {
        const uint32_t sz = ru32(file.data() + off + 4);
        if (sz < 8 || off + sz > file.size()) return false;
        std::vector<uint8_t> sec(file.begin() + off, file.begin() + off + sz);
        if (std::memcmp(sec.data(), "grp1", 4) == 0 && sec.size() >= 0x24) {
            char group[25] = {};
            std::memcpy(group, sec.data() + 8, 24);
            if (std::strcmp(group, "G_InOut_00") == 0) {
                const uint16_t kids = ru16(sec.data() + 0x20);
                const bool packed = 0x24u + (uint32_t)kids * 24u == sec.size();
                bool has = false;
                if (0x24u + (uint32_t)kids * 24u <= sec.size()) {
                    for (uint16_t i = 0; i < kids; i++) {
                        if (std::memcmp(sec.data() + 0x24 + (uint32_t)i * 24u, "Pa_SetBtn_00", 12) == 0) has = true;
                    }
                }
                if (!has && packed) {
                    sec.resize(sec.size() + 24, 0);
                    std::memcpy(sec.data() + sec.size() - 24, "Pa_SetBtn_00", 12);
                    wu16(sec.data() + 0x20, (uint16_t)(kids + 1));
                    wu32(sec.data() + 4, (uint32_t)sec.size());
                }
            }
        }
        if (std::memcmp(sec.data(), "prt1", 4) == 0 && sec.size() >= 0x54) {
            if (pane_named(sec.data(), (uint32_t)sec.size(), "Pa_Continue_00")) wf32(sec.data() + 0x30, kContinueY);
            if (pane_named(sec.data(), (uint32_t)sec.size(), "Pa_Exit_00")) wf32(sec.data() + 0x30, kQuitY);
            if (pane_named(sec.data(), (uint32_t)sec.size(), "Pa_PlayStyle_00")) wf32(sec.data() + 0x30, kControllerY);
            if (pane_named(sec.data(), (uint32_t)sec.size(), "Pa_Win_00")) wf32(sec.data() + 0x50, kWinH);
            placed = placed || pane_named(sec.data(), (uint32_t)sec.size(), "Pa_PlayStyle_00");
        }
        out.insert(out.end(), sec.begin(), sec.end());
        if (!inserted && std::memcmp(sec.data(), "prt1", 4) == 0 &&
            pane_named(sec.data(), (uint32_t)sec.size(), "Pa_PlayStyle_00")) {
            std::vector<uint8_t> copy = sec;
            std::memset(copy.data() + 0xC, 0, 24);
            std::memcpy(copy.data() + 0xC, "Pa_SetBtn_00", 12);
            wf32(copy.data() + 0x30, kSettingsY);
            write_settings_label(copy);
            out.insert(out.end(), copy.begin(), copy.end());
            inserted = true;
        }
        off += sz;
    }
    if (!inserted || !placed || off != file.size()) return false;
    uint32_t count = 0;
    for (uint32_t at = 0x14; at + 8 <= out.size();) {
        const uint32_t sz = ru32(out.data() + at + 4);
        if (sz < 8 || at + sz > out.size()) return false;
        count++;
        at += sz;
    }
    wu32(out.data() + 0xC, (uint32_t)out.size());
    out[0x10] = (uint8_t)(count >> 8);
    out[0x11] = (uint8_t)count;
    file.swap(out);
    return true;
}

// On until Settings, Debug turns the pause Settings button off.
constexpr bool kStartsEnabled = true;

bool enabled();
void set_enabled(bool on);
bool take_open();

}
