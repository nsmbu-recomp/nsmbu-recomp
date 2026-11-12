#pragma once

#include "guest_addr.h"

#include <cstdint>
#include <cstring>
#include <vector>

namespace pause_menu {

// Story pause rows stay where the game drew them. Change Controller becomes Settings.
constexpr float kContinueY = 155.f;
constexpr float kQuitY = 14.f;
constexpr float kSettingsY = -149.f;
constexpr float kWinH = 557.f;

inline bool contains_cstr(const uint8_t* data, uint32_t size, const char* name) {
    const size_t n = std::strlen(name);
    if (!data || size < n + 1) return false;
    for (uint32_t i = 0; i + n < size; i++) {
        if (std::memcmp(data + i, name, n) == 0 && data[i + n] == 0) return true;
    }
    return false;
}

// PauseGame.bflyt and PauseGameDL.bflyt both carry these three panes.
// A file whose Change Controller label is already Settings is left alone.
inline bool pause_menu_flyt(const uint8_t* data, uint32_t size) {
    return contains_cstr(data, size, "Pa_Continue_00") && contains_cstr(data, size, "Pa_Exit_00") &&
           contains_cstr(data, size, "Pa_PlayStyle_00") && contains_cstr(data, size, "Pa_Win_00") &&
           contains_cstr(data, size, "PauseGame_PlayStyle_00");
}

// A Settings confirm stays on this menu and opens the overlay.
// Continue and Quit keep the game's own confirm.
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

// The story pause uses indexes 0 through 2. Index 2 is the Settings row.
inline bool story_settings_row(int mn, int mx, int idx) {
    return mn == 0 && mx == 2 && idx == mx;
}

// Plus is bit 11 and that is Enter. Touch is bit 12. Decide is bit 13 and that is A.
// Back is bit 14 and stays with the game, so B still leaves the pause menu.
constexpr uint32_t kSettingsConfirmBits = (1u << 11) | (1u << 12) | (1u << 13);

inline bool settings_confirm_pad(uint32_t pad) {
    return (pad & kSettingsConfirmBits) != 0;
}

// A confirm on the story Settings row opens this overlay. Continue and Quit keep their own confirms.
inline bool story_settings_confirm(int mn, int mx, int idx, bool decide) {
    return story_settings_row(mn, mx, idx) && decide;
}

// Pa_PlayStyle_00 is the story Change Controller row. Challenge rows keep their own names.
inline bool settings_button_name(const char* name) {
    return name && std::strcmp(name, "Pa_PlayStyle_00") == 0;
}

// The course pause object at 0x101D830C is on screen when +0xA90 is set.
// +0x250 gates a different object, so a clear +0x250 still leaves this menu open.
inline bool pause_menu_shown(uint8_t word_250, uint8_t word_a90) {
    return word_250 != 0 || word_a90 != 0;
}

// The overlay opens when the whole-menu Out fade has finished.
// A fixed frame count opens while that fade is still running.
inline bool pause_settings_ready(bool out_finished) {
    return out_finished;
}

// Plus and Back enter the controller window. Decide is the Settings press.
// While Settings owns the pause menu, those bits stay clear.
constexpr uint32_t kControllerEnterBits = kSettingsConfirmBits | 0x4800u;

inline uint32_t pause_settings_idle_pad(uint32_t pad) {
    return pad & ~kControllerEnterBits;
}

// B closes the settings overlay. That same press must not also leave the pause menu.
// The pause menu sees B again only after that press is released.
inline bool pause_settings_blocks_back(bool overlay_open, bool back_held, bool already_blocking) {
    if (overlay_open) return true;
    return already_blocking && back_held;
}

// Plus or Back enters 0x10216DF4 and plays Out. While the overlay's B is still held, undo that leave.
inline bool pause_settings_cancels_back_out(bool blocking, uint32_t state) {
    return blocking && state == GD(0x10216DF4u);
}

// 0x025F3834 steps the pause object at 0x101D7FE0 after this hook.
// Strip Back before that step, or the overlay close starts Out and leaves the yellow cursor up.
inline bool pause_settings_strips_other_back(bool blocking) {
    return blocking;
}

// Closing the overlay arms this many frames. The controller window stays shut for all of them.
constexpr int kPauseSettingsSettle = 8;

// The click sets a flag only while the button-manager step is 0.
// The press itself plays the click only while the anim frame at button+0x30 is 0.
// A later A clears the step and that frame, then plays the button again.
struct PausePress {
    uint32_t step = 2;
    uint32_t current = 0;
    uint8_t decide = 1;
    float frame = 1.f;
    bool play = false;
};

inline PausePress pause_press(uint32_t button, bool play) {
    PausePress press;
    if (!play || button == 0) return press;
    press.step = 0;
    press.current = button;
    press.decide = 0;
    press.frame = 0.f;
    press.play = true;
    return press;
}

// Holding A is one press. A new A after the button is idle plays it again.
// An open overlay does not take that press.
inline bool pause_press_again(bool pressed, bool confirm, bool idle_since, bool overlay_open) {
    return pressed && confirm && idle_since && !overlay_open;
}

// Closing the overlay plays the pause menu In animation. Showing the draw byte alone pops the menu in.
inline bool pause_menu_fades_in(bool was_hidden, bool overlay_open) {
    return was_hidden && !overlay_open;
}

// A later A leaves button+0x84 set, or leaves the press anim off frame 0.
// The manager then skips the click, so the click has to be played here.
// A press after the overlay has closed plays the click even when both are clear.
inline bool pause_click_missed(uint8_t decide_byte, float frame) {
    return decide_byte != 0 || frame != 0.f;
}

// The press virtual plays the click and starts the decide anim only when this frame is 0.
// Every Settings press uses 0. A different frame plays an anim and leaves the click out.
inline float pause_replay_frame(bool replay, float current) {
    (void)replay;
    (void)current;
    return 0.f;
}

// The first confirm and a later confirm are the same press.
// The game's own press plus a second press plays the click and the anim apart.
inline bool pause_press_replays_anim(bool played_before) {
    (void)played_before;
    return true;
}

inline bool pause_click_needed(bool again, uint8_t decide_byte, float frame) {
    return again || pause_click_missed(decide_byte, frame);
}

// The press call already plays the click. A second click after that call starts late.
inline bool pause_settings_plays_click(bool settings_press) {
    (void)settings_press;
    return false;
}

// The game press sets this speed below -0.5 and plays the click with it.
// Calling the press virtual again rewinds that anim and plays the click again.
inline bool pause_settings_replays_press(float speed) {
    return !(speed < -0.5f);
}

// Out leaves layout anim 0 finished. In clears that bit while it runs.
// A finished bit that never cleared is still Out, so the fade-in has not played.
inline bool pause_settings_fade_in_done(bool saw_running, bool finished, int held_frames) {
    if (held_frames >= 45) return true;
    return saw_running && finished;
}

// A decided pause button sets +0x1C to 3. The open menu then enters 0x10216DD4.
// Plus or Back enters 0x10216DF4. Both play the layout anim named Out.
// 0x10216D74 and 0x10216D94 are the controller window after that fade.
inline bool pause_out_state(uint32_t id) {
    return id == GD(0x10216DD4u) || id == GD(0x10216DF4u);
}

inline bool pause_controller_state(uint32_t id) {
    return id == GD(0x10216D74u) || id == GD(0x10216D94u);
}

// Out and the controller window clear those draw bytes while the pause is still up.
// Treat that as gone only once those states are gone too.
inline bool pause_settings_treat_menu_gone(uint8_t word_250, uint8_t word_a90, uint32_t state) {
    if (pause_menu_shown(word_250, word_a90)) return false;
    return !pause_out_state(state) && !pause_controller_state(state);
}

// The open menu reads the pad only while +0x1C is 0.
// A word of 2 is left after a press and blocks the next A.
// A word of 3 is the decide. The next step enters the Out fade. Leave that word.
inline bool pause_settings_keeps_mode(uint32_t mode) {
    return mode == 3u;
}

// A sets +0x1C to 2. The next step stores 3 there and enters Out.
// A 2 that is still here after that step did not become a decide.
inline bool pause_settings_clears_mode(uint32_t mode, int seen_frames) {
    return mode == 2u && seen_frames >= 3;
}

// Arm when the Settings row is in Out, or when its decide is stored and the next step will enter it.
// Continue and Quit use that same Out. Arming those rows cancels their leave.
// Back is 0x10216DF4. That leave stays with the game, including on the Settings row.
// A confirm while +0x1C is 2 has not started that fade. Arming there clears the pad.
inline bool pause_settings_arms(uint32_t state, uint32_t mode, bool on_settings) {
    if (!on_settings || state == GD(0x10216DF4u)) return false;
    return pause_out_state(state) || pause_controller_state(state) || pause_settings_keeps_mode(mode);
}

// The controller window checks whether layout anim 0 has finished. Clear that bit before a real leave.
inline bool pause_settings_clears_leave_anim(uint32_t flags) {
    return (flags & 1u) != 0;
}

// D94 snaps back to the open menu when that bit is set. A real leave clears it each frame there.
inline bool pause_settings_blocks_controller_rewind(bool real_leave, uint32_t state) {
    return real_leave && pause_controller_state(state);
}

// Continue after Settings skips the controller page and uses the same unpause call as the game.
inline bool pause_settings_skips_controller_window(bool real_leave, uint32_t state) {
    return real_leave && (pause_out_state(state) || pause_controller_state(state));
}

// 0x026446D4 shows the switch-controller screen and leaves it up.
// Continue hides that screen. Settings does not, until Continue is chosen.
inline bool pause_settings_hides_switch_screen(bool course_resumed) {
    return course_resumed;
}

// 0x025FE554(pause, 0) clears the course node. The menu nodes at +0x258 are a different set.
inline bool pause_settings_course_pause_node(uint32_t offset) {
    return offset == 0x234u || offset == 0x23Cu || offset == 0x254u || offset == 0x264u || offset == 0x298u ||
           offset == 0x29Cu;
}

// The pause menu keeps a list of method-tree nodes at +0x234. The count is at +0x1AA8.
// Continue clears the pause bit on each of those nodes.
inline int pause_settings_pause_node_count(int stored) {
    if (stored < 1) return 0;
    return stored > 17 ? 17 : stored;
}

// AreaTask at 0x101D1598 steps the course only while +0x730 is 0.
// Opening this menu sets that word to 1. The frame tail writes 3 when +0x74D is set.
// Continue puts the word back to 0. Quit still leaves the course.
inline uint32_t pause_settings_course_out_kind(bool resume, bool quit_row = false) {
    if (!resume || quit_row) return 1u;
    return 0u;
}

// The other pause object steps after the resume call and draws the switch page again.
// Hold the resume across that step.
inline int pause_settings_resume_hold(bool resumed, int frames_left) {
    if (resumed) return 8;
    return frames_left > 0 ? frames_left - 1 : 0;
}

// The controller execute draws the switch page. 0x02676E74 only resets button anims.
// 0x025FBA74(pause, 0) clears the course pause flags. That runs once Out has finished.
// Quit uses the same window and still leaves the course. Out itself keeps playing.
inline bool pause_settings_direct_unpause(bool real_leave, uint32_t state, bool menu_hidden = false,
                                          bool quit_row = false) {
    if (!real_leave || quit_row || pause_out_state(state) || state == GD(0x10216DB4u)) return false;
    return pause_controller_state(state) || menu_hidden;
}

// The virtual state id can stay on the open menu while the state object is already Out.
// Continue has to watch the state object, or the course never gets the resume call.
inline uint32_t pause_settings_watched_state(uint32_t state_id, uint32_t virtual_id) {
    if (pause_out_state(state_id) || pause_controller_state(state_id)) return state_id;
    if (pause_out_state(virtual_id) || pause_controller_state(virtual_id)) return virtual_id;
    return state_id != 0 ? state_id : virtual_id;
}

// After the overlay closes, Continue, Quit, and Back are the game's own leaves.
// Holding the button list through that leave freezes Out and leaves the course paused with no menu.
inline bool pause_settings_drops_hold(bool page_closed, bool on_settings, uint32_t state) {
    if (!page_closed) return false;
    if (state == GD(0x10216DF4u)) return true;
    if (!pause_out_state(state) && !pause_controller_state(state)) return false;
    return !on_settings;
}

// Leaving 0x10216DD4 on its first frame cancels Out.
// Leave on the frame that wait would enter the controller window.
struct PauseFade {
    bool leave = false;
    bool open_overlay = false;
    bool block_press = false;
    bool play_press = false;
};

// The change-controller window can be open before its type word is 5.
// A set request or a set draw byte is enough to close it behind Settings.
inline bool pause_settings_closes_window(uint32_t type, uint8_t request, uint8_t visible) {
    return type == 5u || request != 0 || visible != 0;
}

// 0x10216D94 sets the visible bit on the panes at pause +0x25C and +0x260.
// Those panes are the controller page. Settings keeps that bit clear.
// Closing the overlay restores the pause items. It does not show this page.
inline bool pause_settings_controller_pane_visible(bool overlay_open, bool controller_page) {
    (void)overlay_open;
    (void)controller_page;
    return false;
}

// Out stays on screen so the shrink-and-fade can play.
// The overlay and the controller-page state hide the pause items.
// After the overlay closes, those items come back.
inline bool pause_settings_hides_menu_items(bool overlay_open, bool controller_page, bool playing_out) {
    return !playing_out && (overlay_open || controller_page);
}

// The pause step can turn the controller page back on before the draw.
// A hidden pause layout stays out of that draw.
inline bool pause_settings_skips_layout_draw(bool items_hidden) {
    return items_hidden;
}

// Continue leaves the controller page in the pause layout. Skip that draw while the course is resumed.
inline bool pause_settings_skips_pause_draw(bool resume_hold, bool pause_root) {
    return resume_hold && pause_root;
}

// The button list is the pause layout that was on screen before Settings.
// The layout that appears with the controller page stays skipped after the overlay closes.
// The button list draws again once those items are no longer hidden.
inline bool pause_settings_skips_pause_root(bool menu_root, bool page_closed, bool items_hidden) {
    if (!page_closed) return false;
    if (!menu_root) return true;
    return items_hidden;
}

// The row is forced to Settings only until the cursor has been placed there.
// After that, Up and Down move the selection.
inline bool pause_settings_locks_row(bool cursor_placed) {
    return !cursor_placed;
}

// 0x025BE1F0 sizes the cursor from the selected button.
// The rewind frame still has Out's scale. Placing there makes the corners huge.
// Place on a later hold frame, after that rewind has been applied.
inline bool pause_settings_places_cursor(bool menu_restored, int held_frames) {
    return menu_restored && held_frames >= 2;
}

// After the overlay closes, hold the button list until the next Out starts.
// Holding through that next Out rewinds the shrink and leaves the cursor broken.
inline bool pause_settings_holds_menu(bool restore_pending, bool out_now) {
    return restore_pending && !out_now;
}

// Playing Out again on a later hold frame steps that anim.
// The button list then shrinks away and the cursor is left in the middle.
inline bool pause_settings_restarts_out(bool already_pinned) {
    return !already_pinned;
}

// The restored pause menu uses the In anim. Leaving it on Out freezes the buttons.
// In has to finish before the row can be used. A long In still hands the menu back.
inline bool pause_settings_hands_back(bool in_finished, int held_frames) {
    return in_finished || held_frames >= 45;
}

// Out stays at frame 0 only while that anim is being rewound. The restored menu does not keep it there.
inline bool pause_settings_pins_out(bool handed_back) {
    (void)handed_back;
    return false;
}

// The press byte stays set after the first Settings confirm.
// The next A can start Out only after that byte is clear.
inline bool pause_settings_releases_press(bool menu_restored) {
    return menu_restored;
}

inline PauseFade pause_settings_fade(uint32_t state, bool out_finished, bool overlay_open, bool opened, bool pressed) {
    PauseFade fade;
    const bool window = pause_controller_state(state);
    const bool out = pause_out_state(state);
    const bool done = window || (out && out_finished);
    fade.leave = done;
    fade.open_overlay = done && pause_settings_ready(out_finished || window) && !opened && !overlay_open;
    fade.block_press = pressed || out || window;
    fade.play_press = (out || window) && !pressed && !overlay_open;
    return fade;
}

inline int pause_settings_settle(bool saw_overlay, bool overlay_open) {
    return saw_overlay && !overlay_open ? kPauseSettingsSettle : 0;
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

inline float rf32(const uint8_t* p) {
    uint32_t u = ru32(p);
    float v = 0;
    std::memcpy(&v, &u, 4);
    return v;
}

inline bool pane_named(const uint8_t* s, uint32_t n, const char* name) {
    const size_t len = std::strlen(name);
    if (n < 0xC + len + 1) return false;
    return std::memcmp(s + 0xC, name, len) == 0 && s[0xC + len] == 0;
}

// A text box shows (buffer bytes / 2 - 1) characters. The pause button's box is 16 bytes, so it shows
// seven characters and cuts Settings to Setting. Settings plus the NUL needs 18 bytes.
// The message id is renamed so the game does not put Change Controller back.
inline bool write_settings_label(std::vector<uint8_t>& part) {
    bool wrote = false;
    const uint8_t text[] = {
        0x00, 'S', 0x00, 'e', 0x00, 't', 0x00, 't', 0x00, 'i', 0x00, 'n', 0x00, 'g', 0x00, 's', 0x00, 0x00};
    constexpr uint16_t kBytes = 18;
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
        const uint16_t buf = ru16(part.data() + i + 0x54);
        const uint32_t text_off = ru32(part.data() + i + 0x64);
        if (text_off + buf != (uint32_t)(id - i)) continue;
        if (buf < kBytes) {
            const uint32_t delta = (uint32_t)kBytes - buf;
            if (part.size() >= 0x60) {
                const uint32_t count = ru32(part.data() + 0x54);
                if (count > 0 && count <= 16 && 0x60u + count * 40u <= part.size()) {
                    for (uint32_t n = 0; n < count; n++) {
                        const uint32_t prop = 0x60u + n * 40u;
                        for (int k = 0; k < 3; k++) {
                            const uint32_t off = ru32(part.data() + prop + 28 + k * 4);
                            if (off >= (uint32_t)id) wu32(part.data() + prop + 28 + k * 4, off + delta);
                        }
                    }
                }
            }
            part.insert(part.begin() + (std::vector<uint8_t>::difference_type)id, delta, 0);
            wu32(part.data() + 4, (uint32_t)part.size());
            wu32(part.data() + i + 4, tsz + delta);
            const uint32_t msg = ru32(part.data() + i + 0x80);
            if (msg >= (uint32_t)(id - i)) wu32(part.data() + i + 0x80, msg + delta);
        }
        wu16(part.data() + i + 0x54, kBytes);
        wu16(part.data() + i + 0x56, kBytes);
        if (i + text_off + kBytes <= part.size()) {
            std::memcpy(part.data() + i + text_off, text, kBytes);
            wrote = true;
        }
        break;
    }
    if (!wrote) return false;
    for (size_t i = 0; i + idn <= part.size(); i++) {
        if (std::memcmp(part.data() + i, from, idn) != 0) continue;
        std::memcpy(part.data() + i, to, idn);
        break;
    }
    return true;
}

// Writes Settings onto Pa_PlayStyle_00. The blue panel and the other rows stay as they are.
// Returns false when the file is not the story pause menu or the label is already Settings.
inline bool patch_pause_flyt(std::vector<uint8_t>& file) {
    if (file.size() < 0x20 || std::memcmp(file.data(), "FLYT", 4) != 0) return false;
    if (!pause_menu_flyt(file.data(), (uint32_t)file.size())) return false;
    std::vector<uint8_t> out(file.begin(), file.begin() + 0x14);
    bool labeled = false;
    uint32_t off = 0x14;
    while (off + 8 <= file.size()) {
        const uint32_t sz = ru32(file.data() + off + 4);
        if (sz < 8 || off + sz > file.size()) return false;
        std::vector<uint8_t> sec(file.begin() + off, file.begin() + off + sz);
        if (!labeled && std::memcmp(sec.data(), "prt1", 4) == 0 &&
            pane_named(sec.data(), (uint32_t)sec.size(), "Pa_PlayStyle_00"))
            labeled = write_settings_label(sec);
        out.insert(out.end(), sec.begin(), sec.end());
        off += sz;
    }
    if (!labeled || off != file.size()) return false;
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
// The pause layout draws after the pause step. Skip that tree while Settings has it hidden.
bool skip_pane(uint32_t pane);

}
