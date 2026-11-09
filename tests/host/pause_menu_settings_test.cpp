#include "hooks/pause_menu_settings.h"

#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <vector>

static int fails = 0;
static void check(bool cond, const char* what) {
    if (cond) return;
    std::fprintf(stderr, "FAIL %s\n", what);
    ++fails;
}

static bool near(float a, float b) { return std::fabs(a - b) < 0.5f; }

static void wu32(uint8_t* p, uint32_t v) {
    p[0] = (uint8_t)(v >> 24);
    p[1] = (uint8_t)(v >> 16);
    p[2] = (uint8_t)(v >> 8);
    p[3] = (uint8_t)v;
}

static void wf32(uint8_t* p, float v) {
    uint32_t u = 0;
    std::memcpy(&u, &v, 4);
    wu32(p, u);
}

static void add_name(std::vector<uint8_t>& sec, const char* name) {
    const size_t at = sec.size();
    sec.resize(at + 24, 0);
    std::memcpy(sec.data() + at, name, std::strlen(name));
}

static void wu16(uint8_t* p, uint16_t v) {
    p[0] = (uint8_t)(v >> 8);
    p[1] = (uint8_t)v;
}

static std::vector<uint8_t> part(const char* name, float y) {
    std::vector<uint8_t> sec(0x54, 0);
    std::memcpy(sec.data(), "prt1", 4);
    wu32(sec.data() + 4, (uint32_t)sec.size());
    std::memcpy(sec.data() + 0xC, name, std::strlen(name));
    wf32(sec.data() + 0x30, y);
    wf32(sec.data() + 0x4C, 444.96f);
    wf32(sec.data() + 0x50, 156.f);
    wu32(sec.data() + 4, (uint32_t)sec.size());
    return sec;
}

// A 16-byte text box shows seven characters. Settings needs the NUL as well.
static void append_text_box(std::vector<uint8_t>& sec) {
    const uint8_t ja[] = {0x30, 0x5D, 0x30, 0x46, 0x30, 0x55, 0x30, 0x92, 0x59, 0x09, 0x30, 0x48, 0x30, 0x8B, 0x00, 0x00};
    const char* id = "PauseGame_PlayStyle_00";
    std::vector<uint8_t> txt(0xD8, 0);
    std::memcpy(txt.data(), "txt1", 4);
    wu32(txt.data() + 4, (uint32_t)txt.size());
    wu16(txt.data() + 0x54, 16);
    wu16(txt.data() + 0x56, 16);
    wu32(txt.data() + 0x64, 0xA0);
    wu32(txt.data() + 0x80, 0xB0);
    std::memcpy(txt.data() + 0xA0, ja, sizeof ja);
    std::memcpy(txt.data() + 0xB0, id, std::strlen(id) + 1);
    sec.insert(sec.end(), txt.begin(), txt.end());
    wu32(sec.data() + 4, (uint32_t)sec.size());
}

static std::vector<uint8_t> pause_file() {
    std::vector<uint8_t> group(0x24, 0);
    std::memcpy(group.data(), "grp1", 4);
    std::memcpy(group.data() + 8, "G_InOut_00", 11);
    group[0x20] = 0;
    group[0x21] = 3;
    add_name(group, "Pa_Continue_00");
    add_name(group, "Pa_Exit_00");
    add_name(group, "Pa_PlayStyle_00");
    wu32(group.data() + 4, (uint32_t)group.size());

    std::vector<uint8_t> win(0x164, 0);
    std::memcpy(win.data(), "prt1", 4);
    wu32(win.data() + 4, (uint32_t)win.size());
    std::memcpy(win.data() + 0xC, "Pa_Win_00", 9);
    wf32(win.data() + 0x4C, 580.f);
    wf32(win.data() + 0x50, 557.f);
    wu32(win.data() + 0x54, 2);
    wf32(win.data() + 0x58, 1.f);
    wf32(win.data() + 0x5C, 1.f);
    std::memcpy(win.data() + 0x60, "W_WinFrame_00", 13);
    win[0x60 + 25] = 0x10;
    wu32(win.data() + 0x60 + 36, 0xB0);
    std::memcpy(win.data() + 0x88, "P_Dot_00", 8);
    win[0x88 + 25] = 0x08;
    wu32(win.data() + 0x88 + 36, 0xE0);
    wf32(win.data() + 0xB0 + 44, 500.f);
    wf32(win.data() + 0xE0 + 12, 215.f);
    std::memcpy(win.data() + 0x110, "wnd1", 4);
    wu32(win.data() + 0x110 + 4, 0x54);
    std::memcpy(win.data() + 0x110 + 0xC, "W_WinSha_00", 11);
    wf32(win.data() + 0x110 + 0x50, 557.f);
    wu32(win.data() + 0x60 + 28, 0x110);

    std::vector<uint8_t> other = win;
    std::memset(other.data() + 0xC, 0, 24);
    std::memcpy(other.data() + 0xC, "Pa_Win_01", 9);
    wf32(other.data() + 0x50, 440.f);
    wf32(other.data() + 0xB0 + 44, 360.f);

    auto cont = part("Pa_Continue_00", 155.f);
    auto quit = part("Pa_Exit_00", 14.f);
    auto play = part("Pa_PlayStyle_00", -149.f);
    append_text_box(play);

    std::vector<uint8_t> file(0x14, 0);
    std::memcpy(file.data(), "FLYT", 4);
    file.insert(file.end(), group.begin(), group.end());
    file.insert(file.end(), win.begin(), win.end());
    file.insert(file.end(), other.begin(), other.end());
    file.insert(file.end(), cont.begin(), cont.end());
    file.insert(file.end(), quit.begin(), quit.end());
    file.insert(file.end(), play.begin(), play.end());
    wu32(file.data() + 0xC, (uint32_t)file.size());
    file[0x10] = 0;
    file[0x11] = 6;
    return file;
}

static const uint8_t* find_part(const std::vector<uint8_t>& file, const char* name) {
    uint32_t off = 0x14;
    while (off + 8 <= file.size()) {
        const uint32_t sz = ((uint32_t)file[off + 4] << 24) | ((uint32_t)file[off + 5] << 16) |
                            ((uint32_t)file[off + 6] << 8) | file[off + 7];
        if (sz < 8 || off + sz > file.size()) return nullptr;
        if (std::memcmp(file.data() + off, "prt1", 4) == 0 &&
            std::strcmp((const char*)file.data() + off + 0xC, name) == 0)
            return file.data() + off;
        off += sz;
    }
    return nullptr;
}

int main() {
    using namespace pause_menu;
    const char mark[] = "Pa_Continue_00\0Pa_Exit_00\0Pa_PlayStyle_00\0Pa_Win_00\0PauseGame_PlayStyle_00";
    const char done[] = "Pa_Continue_00\0Pa_Exit_00\0Pa_PlayStyle_00\0Pa_Win_00\0PauseGame_SetButton_00";
    const char other[] = "Pa_TitleBtn_00";
    check(pause_menu_flyt(reinterpret_cast<const uint8_t*>(mark), sizeof mark),
          "the three pause buttons mark the pause menu");
    check(!pause_menu_flyt(reinterpret_cast<const uint8_t*>(done), sizeof done),
          "a file that already says Settings is left alone");
    check(!pause_menu_flyt(reinterpret_cast<const uint8_t*>(other), sizeof other),
          "another layout is not the pause menu");

    auto file = pause_file();
    check(patch_pause_flyt(file), "Change Controller becomes Settings");
    const uint8_t* play = find_part(file, "Pa_PlayStyle_00");
    const uint8_t* win = find_part(file, "Pa_Win_00");
    const uint8_t* cont = find_part(file, "Pa_Continue_00");
    const uint8_t* quit = find_part(file, "Pa_Exit_00");
    check(!find_part(file, "Pa_SetBtn_00"), "the menu does not gain a fourth button");
    check(play && win && cont && quit, "the patched file still has the pause panes");
    if (play && win && cont && quit) {
        check(near(rf32(cont + 0x30), kContinueY), "Continue stays on its row");
        check(near(rf32(quit + 0x30), kQuitY), "Quit stays on its row");
        check(near(rf32(play + 0x30), kSettingsY), "Settings keeps the Change Controller row");
        check(near(rf32(win + 0x50), kWinH), "the pause window keeps its height");
        check(near(rf32(win + 0xB0 + 44), 500.f), "the blue frame keeps its height");
        const uint8_t* untouched = find_part(file, "Pa_Win_01");
        check(untouched && near(rf32(untouched + 0x50), 440.f) && near(rf32(untouched + 0xB0 + 44), 360.f),
              "the two-button pause window stays the same");
        const uint8_t word[] = {
            0x00, 'S', 0x00, 'e', 0x00, 't', 0x00, 't', 0x00, 'i', 0x00, 'n', 0x00, 'g', 0x00, 's', 0x00, 0x00};
        bool labeled = false;
        const uint32_t sz = ((uint32_t)play[4] << 24) | ((uint32_t)play[5] << 16) | ((uint32_t)play[6] << 8) | play[7];
        for (uint32_t i = 0; i + 0x56 <= sz; i++) {
            if (std::memcmp(play + i, "txt1", 4) != 0) continue;
            const uint16_t buf = (uint16_t)((play[i + 0x54] << 8) | play[i + 0x55]);
            const uint32_t off = ((uint32_t)play[i + 0x64] << 24) | ((uint32_t)play[i + 0x65] << 16) |
                                 ((uint32_t)play[i + 0x66] << 8) | play[i + 0x67];
            if (buf == 18 && i + off + sizeof word <= sz && std::memcmp(play + i + off, word, sizeof word) == 0)
                labeled = true;
        }
        check(labeled, "the button says Settings");
    }
    const auto once = file;
    check(!patch_pause_flyt(file), "a second patch does nothing");
    check(file == once, "the patched bytes stay as they were");

    check(kStartsEnabled, "the pause Settings button starts on");

    const SettingsClickEnd mine = settings_click_end(true);
    const SettingsClickEnd other_end = settings_click_end(false);
    check(mine.keep_menu && mine.open_overlay, "a Settings confirm keeps the pause menu and opens the overlay");
    check(!other_end.keep_menu && !other_end.open_overlay, "another pause button keeps its own action");
    check(story_settings_confirm(0, 2, 2, true), "Decide on the bottom story row opens Settings");
    check(!story_settings_confirm(0, 2, 0, true), "Decide on Continue keeps Continue");
    check(!story_settings_confirm(0, 2, 1, true), "Decide on Quit keeps Quit");
    check(!story_settings_confirm(3, 4, 4, true), "another pause layout keeps its own confirm");
    check(settings_confirm_pad(1u << 11), "Enter is Plus and confirms Settings");
    check(settings_confirm_pad(1u << 13), "A is Decide and confirms Settings");
    check(!settings_confirm_pad(1u << 14), "B stays Back and does not confirm Settings");
    check(settings_button_name("Pa_PlayStyle_00"), "the story Change Controller row is Settings");
    check(!settings_button_name("Pa_ChallengePlayStyle_00"), "a challenge row keeps its own confirm");
    check(pause_menu_shown(0, 1), "the course pause stays open when +0xA90 is set");
    check(!pause_menu_shown(0, 0), "a clear +0x250 and +0xA90 leaves the pause menu closed");
    check(!pause_settings_ready(false), "the overlay stays shut while Out is still playing");
    check(pause_settings_ready(true), "the overlay opens when Out has finished");
    check(pause_settings_idle_pad(kControllerEnterBits) == 0, "the controller window bits stay clear");
    check(pause_settings_settle(true, false) == kPauseSettingsSettle,
          "closing the overlay keeps the controller window shut");
    check(pause_settings_settle(true, true) == 0, "an open overlay does not start that shut time");
    check(pause_press_again(true, true, true, false), "A after the overlay closes plays the press again");
    check(!pause_press_again(true, true, true, true), "A while the overlay is open does not replay the press");
    check(!pause_press_again(true, true, false, false), "holding A does not replay the press");
    const PausePress again = pause_press(0x20000000u, true);
    check(again.play && again.step == 0 && again.current == 0x20000000u && again.decide == 0 &&
              again.frame == 0.f,
          "a second press clears the busy step and plays that button from the start");
    check(!pause_press(0x20000000u, false).play, "the first press is not a replay");
    check(pause_click_missed(1, 0.f), "a later A has to play the click itself");
    check(pause_click_missed(0, 1.f), "a press anim that is still running has to play the click");
    check(!pause_click_missed(0, 0.f), "the first A still uses the button click");
    check(pause_click_needed(true, 0, 0.f), "A after the overlay closes plays the click");
    check(!pause_click_needed(false, 0, 0.f), "a clear decide byte is not itself a second click");
    check(!pause_settings_plays_click(true), "the Settings click is not a second sound after the anim");
    check(pause_replay_frame(false, 3.f) == 0.f, "the first Settings press starts from frame 0 with the click");
    check(pause_replay_frame(true, 3.f) == 0.f, "a later Settings press starts from frame 0 with the click");
    check(pause_press_replays_anim(false), "the first Settings press uses the same anim and click");
    check(pause_press_replays_anim(true), "a later Settings press uses the same anim and click");
    check(pause_menu_fades_in(true, false), "closing the overlay fades the pause menu in");
    check(!pause_menu_fades_in(true, true), "an open overlay keeps the pause menu hidden");
    check(pause_settings_blocks_back(true, true, false), "B closes the overlay and does not leave the pause menu");
    check(pause_settings_blocks_back(false, true, true), "that B stays out of the pause menu until it is released");
    check(!pause_settings_blocks_back(false, false, true), "a later B leaves the pause menu");
    check(pause_settings_locks_row(false), "the cursor is placed on Settings once when the overlay closes");
    check(!pause_settings_locks_row(true), "Up can move from Settings to Continue after that");
    check(!pause_settings_hands_back(false, 2), "the fade back in finishes before the menu is used");
    check(pause_settings_hands_back(true, 2), "a finished In leaves the normal pause menu");
    check(pause_settings_hands_back(false, 45), "a long fade still hands the pause menu back");
    check(!pause_settings_pins_out(true), "the restored pause menu is not left on the Out anim");
    check(pause_settings_strips_other_back(true), "Back is taken off the other pause object before it steps");
    check(!pause_settings_strips_other_back(false), "a later Back still reaches that pause object");
    check(pause_settings_cancels_back_out(true, 0x10216DF4u), "Back's fade is undone while that B is still held");
    check(!pause_settings_cancels_back_out(true, 0x10216DD4u), "Settings still uses the Continue fade");
    check(!pause_settings_cancels_back_out(false, 0x10216DF4u), "a later B can leave the pause menu");
    check(pause_settings_keeps_mode(3u), "a decide word of 3 stays so the next step can enter Out");
    check(!pause_settings_keeps_mode(2u), "a leftover word of 2 does not block the next A");
    check(!pause_settings_clears_mode(2u, 1), "a fresh decide word of 2 can still become Out");
    check(pause_settings_clears_mode(2u, 3), "a decide word of 2 that stays 2 does not block the pad");
    check(!pause_settings_clears_mode(3u, 3), "a decide word of 3 stays until Out starts");
    check(pause_settings_arms(0x10216DD4u, 2u, true), "Out on the Settings row arms Settings");
    check(pause_settings_arms(0x10216DB4u, 3u, true), "a stored decide on the Settings row arms Settings");
    check(!pause_settings_arms(0x10216DB4u, 2u, true), "a confirm while the word is 2 does not arm");
    check(!pause_settings_arms(0x10216DD4u, 3u, false), "Continue's fade does not arm Settings");
    check(!pause_settings_arms(0x10216DF4u, 0u, true), "Back on the Settings row does not arm Settings");
    check(pause_settings_drops_hold(true, false, 0x10216DD4u), "Continue after the overlay is the game's own leave");
    check(pause_settings_drops_hold(true, false, 0x10216D94u), "the state after Continue is the game's own leave");
    check(pause_settings_drops_hold(true, true, 0x10216DF4u), "Back after the overlay leaves the pause menu");
    check(!pause_settings_drops_hold(true, true, 0x10216DD4u), "Settings still owns its own fade");
    check(!pause_settings_drops_hold(false, false, 0x10216DD4u), "Continue before a Settings visit is left alone");
    check(pause_out_state(0x10216DD4u) && pause_out_state(0x10216DF4u), "Settings uses the same Out fade as Continue");
    check(!pause_controller_state(0x10216DD4u), "the Out fade is not the controller window");
    check(pause_controller_state(0x10216D74u) && pause_controller_state(0x10216D94u),
          "the controller window is the state after Out");
    check(!pause_controller_state(0x10216DB4u), "the button list is not the controller window");
    const PauseFade first = pause_settings_fade(0x10216DD4u, false, false, false, false);
    check(!first.leave && !first.open_overlay && first.play_press,
          "a decided Settings row plays Out instead of bouncing on the first frame");
    check(pause_settings_closes_window(0, 1, 0), "a requested controller window closes behind Settings");
    check(!pause_settings_closes_window(0, 0, 0), "a shut controller window stays shut");
    const PauseFade window = pause_settings_fade(0x10216D74u, true, false, false, true);
    check(window.leave && window.open_overlay && !window.play_press,
          "the overlay opens when that fade would enter the controller window");
    check(!pause_settings_hides_menu_items(false, false, true), "Out keeps the pause items on screen");
    check(pause_settings_hides_menu_items(true, true, false), "the overlay hides the pause items");
    check(pause_settings_hides_menu_items(false, true, false),
          "the controller page does not stay up in place of those items");
    check(!pause_settings_hides_menu_items(false, false, false),
          "the pause items come back after the overlay closes");
    check(!pause_settings_controller_pane_visible(true, true),
          "the controller page stays hidden while the overlay is open");
    check(!pause_settings_controller_pane_visible(false, true),
          "closing the overlay leaves the controller page hidden");
    check(pause_settings_skips_layout_draw(true), "a hidden pause layout does not draw");
    check(!pause_settings_skips_layout_draw(false), "the pause layout draws after the overlay closes");
    check(pause_settings_skips_pause_root(false, true, false),
          "the controller layout stays skipped after the overlay closes");
    check(!pause_settings_skips_pause_root(true, true, false),
          "the button list draws after the overlay closes");
    check(pause_settings_skips_pause_root(true, true, true),
          "the button list stays hidden while the overlay is open");
    check(!pause_settings_skips_pause_root(false, false, false),
          "both pause layouts draw before Settings");
    check(pause_settings_places_cursor(true, 2),
          "the cursor is placed once the button list is full size");
    check(!pause_settings_places_cursor(true, 1),
          "the cursor stays hidden on the rewind frame");
    check(!pause_settings_places_cursor(false, 2),
          "the cursor stays hidden while Out still scales the button");
    check(pause_settings_holds_menu(true, false),
          "the button list stays up after the overlay closes");
    check(!pause_settings_holds_menu(true, true),
          "the next Settings press is not rewound out of Out");
    check(pause_settings_restarts_out(false),
          "the first hold frame puts the button list at the start of Out");
    check(!pause_settings_restarts_out(true),
          "a later hold frame does not replay Out");
    check(pause_settings_releases_press(true),
          "a restored menu lets the next A start Out");
    check(!pause_settings_releases_press(false),
          "the Settings press stays blocked until the menu is back");

    if (fails) {
        std::fprintf(stderr, "%d checks failed\n", fails);
        return 1;
    }
    return 0;
}
