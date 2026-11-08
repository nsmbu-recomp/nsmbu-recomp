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

static float rf32(const uint8_t* p) {
    uint32_t u = ((uint32_t)p[0] << 24) | ((uint32_t)p[1] << 16) | ((uint32_t)p[2] << 8) | p[3];
    float v = 0;
    std::memcpy(&v, &u, 4);
    return v;
}

static void add_name(std::vector<uint8_t>& sec, const char* name) {
    const size_t at = sec.size();
    sec.resize(at + 24, 0);
    std::memcpy(sec.data() + at, name, std::strlen(name));
}

static std::vector<uint8_t> part(const char* name, float y) {
    std::vector<uint8_t> sec(0x54, 0);
    std::memcpy(sec.data(), "prt1", 4);
    wu32(sec.data() + 4, (uint32_t)sec.size());
    std::memcpy(sec.data() + 0xC, name, std::strlen(name));
    wf32(sec.data() + 0x30, y);
    wf32(sec.data() + 0x4C, pause_menu::kBtnW);
    wf32(sec.data() + 0x50, pause_menu::kBtnH);
    const uint8_t ja[] = {0x30, 0x5D, 0x30, 0x46, 0x30, 0x55, 0x30, 0x92, 0x59, 0x09, 0x30, 0x48, 0x30, 0x8B, 0x00, 0x00};
    sec.insert(sec.end(), ja, ja + sizeof ja);
    const char* id = "PauseGame_PlayStyle_00";
    sec.insert(sec.end(), (const uint8_t*)id, (const uint8_t*)id + std::strlen(id) + 1);
    wu32(sec.data() + 4, (uint32_t)sec.size());
    return sec;
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

    std::vector<uint8_t> win = part("Pa_Win_00", 0);
    wf32(win.data() + 0x4C, pause_menu::kWinW);
    wf32(win.data() + 0x50, 557.f);
    auto cont = part("Pa_Continue_00", 155.f);
    auto quit = part("Pa_Exit_00", 14.f);
    auto play = part("Pa_PlayStyle_00", -149.f);

    std::vector<uint8_t> file(0x14, 0);
    std::memcpy(file.data(), "FLYT", 4);
    file.insert(file.end(), group.begin(), group.end());
    file.insert(file.end(), win.begin(), win.end());
    file.insert(file.end(), cont.begin(), cont.end());
    file.insert(file.end(), quit.begin(), quit.end());
    file.insert(file.end(), play.begin(), play.end());
    wu32(file.data() + 0xC, (uint32_t)file.size());
    file[0x10] = 0;
    file[0x11] = 5;
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
    const Rect rows[] = {continue_tile(), quit_tile(), controller_tile(), settings_tile()};
    check(near(settings_tile().x + settings_tile().w * 0.5f, controller_tile().x + controller_tile().w * 0.5f),
          "Settings sits under Change Controller");
    check(settings_tile().y < controller_tile().y, "Settings is below Change Controller");
    check(!overlap(rows[0], rows[1]) && !overlap(rows[1], rows[2]) && !overlap(rows[2], rows[3]),
          "the four pause buttons do not overlap");
    check(rows[0].y + rows[0].h <= kWinH * 0.5f && rows[3].y >= -kWinH * 0.5f,
          "the four buttons sit inside the pause window");

    const char both[] = "Pa_Continue_00\0Pa_Exit_00\0Pa_PlayStyle_00\0Pa_Win_00\0Pa_SetBtn_00";
    const char other[] = "Pa_TitleBtn_00";
    check(pause_menu_flyt(reinterpret_cast<const uint8_t*>("Pa_Continue_00\0Pa_Exit_00\0Pa_PlayStyle_00\0Pa_Win_00"), 52),
          "the three pause buttons mark the pause menu");
    check(!pause_menu_flyt(reinterpret_cast<const uint8_t*>(both), sizeof both),
          "a file that already has Settings is left alone");
    check(!pause_menu_flyt(reinterpret_cast<const uint8_t*>(other), sizeof other),
          "another layout is not the pause menu");

    auto file = pause_file();
    auto already = file;
    already.insert(already.end(), (const uint8_t*)"Pa_SetBtn_00", (const uint8_t*)"Pa_SetBtn_00" + 13);
    const auto already_copy = already;
    check(!patch_pause_flyt(already), "a pause file that already has Settings is not patched");
    check(already == already_copy, "that file's bytes stay as they were");

    check(patch_pause_flyt(file), "the pause menu gains a Settings button");
    const uint8_t* set = find_part(file, "Pa_SetBtn_00");
    const uint8_t* play = find_part(file, "Pa_PlayStyle_00");
    const uint8_t* win = find_part(file, "Pa_Win_00");
    const uint8_t* cont = find_part(file, "Pa_Continue_00");
    const uint8_t* quit = find_part(file, "Pa_Exit_00");
    check(set && play && win && cont && quit, "the patched file still has the pause panes");
    if (set && play && win && cont && quit) {
        check(near(rf32(cont + 0x30), kContinueY), "Continue moves up");
        check(near(rf32(quit + 0x30), kQuitY), "Quit moves up");
        check(near(rf32(play + 0x30), kControllerY), "Change Controller moves up");
        check(near(rf32(set + 0x30), kSettingsY), "Settings takes the bottom row");
        check(near(rf32(win + 0x50), kWinH), "only the story pause window grows");
        const uint8_t en[] = {0x00, 'S', 0x00, 'e', 0x00, 't', 0x00, 't', 0x00, 'i', 0x00, 'n', 0x00, 'g', 0x00, 's'};
        bool labeled = false;
        const uint32_t sz = ((uint32_t)set[4] << 24) | ((uint32_t)set[5] << 16) | ((uint32_t)set[6] << 8) | set[7];
        for (uint32_t i = 0; i + sizeof en <= sz; i++) {
            if (std::memcmp(set + i, en, sizeof en) == 0) labeled = true;
        }
        check(labeled, "the Settings button is labeled Settings");
        check(std::memcmp(set + 0xC, "Pa_SetBtn_00", 12) == 0, "the new pane is Pa_SetBtn_00");
    }
    bool grouped = false;
    for (uint32_t off = 0x14; off + 8 <= file.size();) {
        const uint32_t sz = ((uint32_t)file[off + 4] << 24) | ((uint32_t)file[off + 5] << 16) |
                            ((uint32_t)file[off + 6] << 8) | file[off + 7];
        if (sz < 8 || off + sz > file.size()) break;
        if (std::memcmp(file.data() + off, "grp1", 4) == 0 && std::strcmp((const char*)file.data() + off + 8, "G_InOut_00") == 0) {
            const int kids = (file[off + 0x20] << 8) | file[off + 0x21];
            for (int i = 0; i < kids; i++) {
                if (std::strcmp((const char*)file.data() + off + 0x24 + (uint32_t)i * 24u, "Pa_SetBtn_00") == 0)
                    grouped = true;
            }
        }
        off += sz;
    }
    check(grouped, "Settings is in the open and close group");

    check(kStartsEnabled, "the pause Settings button starts on");

    const SettingsClickEnd mine = settings_click_end(true);
    const SettingsClickEnd other_end = settings_click_end(false);
    check(mine.keep_menu && mine.open_overlay, "a Settings confirm keeps the pause menu and opens the overlay");
    check(!other_end.keep_menu && !other_end.open_overlay, "another pause button keeps its own action");

    if (fails) {
        std::fprintf(stderr, "%d checks failed\n", fails);
        return 1;
    }
    return 0;
}
