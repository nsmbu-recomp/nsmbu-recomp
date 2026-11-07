#include "hooks/map_menu_settings.h"

#include <cmath>
#include <cstdio>

static int fails = 0;
static void check(bool cond, const char* what) {
    if (cond) return;
    std::fprintf(stderr, "FAIL %s\n", what);
    ++fails;
}

static bool near(float a, float b) { return std::fabs(a - b) < 0.5f; }

int main() {
    using namespace map_menu;
    const Rect star = column_tile(kColLeft, kRowTopY);
    const Rect players = column_tile(kColMid, kRowTopY);
    const Rect controller = column_tile(kColRight, kRowTopY);
    const Rect save = column_tile(kColLeft, kRowBotY);
    const Rect main_menu = column_tile(kColMid, kRowBotY);
    const Rect settings = settings_tile();

    check(near(settings.x + settings.w * 0.5f, controller.x + controller.w * 0.5f),
          "Settings sits under Change Controller");
    check(near(save.x + save.w * 0.5f, star.x + star.w * 0.5f), "Quick Save sits under Star Coins");
    check(near(main_menu.x + main_menu.w * 0.5f, players.x + players.w * 0.5f),
          "Main Menu sits under Number of Players");
    check(!overlap(save, main_menu) && !overlap(main_menu, settings) && !overlap(save, settings),
          "the bottom row does not overlap");
    check(!overlap(star, players) && !overlap(players, controller), "the top row does not overlap");
    check(settings.y > controller.y, "Settings is below Change Controller");
    check(bottom_column(-213.f) == kColLeft && bottom_column(-424.f) == kColLeft, "Quick Save stays in the left column");
    check(bottom_column(212.f) == kColMid && bottom_column(0.f) == kColMid, "Main Menu stays in the middle column");

    float sx = 0, sy = 0;
    const float cx = settings.x + settings.w * 0.5f;
    const float cy = settings.y + settings.h * 0.5f;
    check(window_to_screen(cx / 1280.f, cy / 720.f, 1280.f, 720.f, 0, 0, 1280.f, 720.f, sx, sy),
          "the settings center maps into the picture");
    check(contains(settings, sx, sy), "a click on the settings center hits the tile");
    check(!contains(settings, star.x + 8.f, star.y + 8.f), "Star Coins is not the settings tile");

    const char title[] = "xxPa_TitleBtn_00";
    const char both[] = "Pa_TitleBtn_00\0Pa_SetBtn_00";
    const char other[] = "Pa_NumBtn_00";
    check(start_menu_flyt(reinterpret_cast<const uint8_t*>(title), sizeof title),
          "Main Menu still in the file marks the start menu");
    check(!start_menu_flyt(reinterpret_cast<const uint8_t*>(both), sizeof both),
          "a file that already has Settings is left alone");
    check(!start_menu_flyt(reinterpret_cast<const uint8_t*>(other), sizeof other),
          "another layout is not the start menu");

    const SettingsRepair idle = settings_repair(6, 6, false, 0);
    const SettingsRepair live = settings_repair(6, 6, false, 1);
    const SettingsRepair pressed = settings_repair(6, 6, true, 2);
    const SettingsRepair neighbor = settings_repair(2, 6, true, 2);
    check(!idle.hold && idle.word == 0, "an idle Settings button is left alone");
    check(!live.hold && live.word == 1, "a Settings highlight does not rewrite the cursor word");
    check(pressed.hold && pressed.word == 0, "a Settings press returns the cursor word to idle");
    check(!neighbor.hold && neighbor.word == 2, "another button keeps its press");
    check(settings_open_delay(true) == kSettingsClickFrames && kSettingsClickFrames > 8,
          "a Settings press waits until the click can play");
    check(settings_open_delay(false) == 0, "a Settings highlight does not open the overlay");
    const SettingsClick first = settings_click(true, true);
    const SettingsClick later = settings_click(true, false);
    const SettingsClick idle_click = settings_click(false, true);
    check(first.block_menu && first.pass_pad,
          "the first Settings frame blocks Button Settings and still reaches the button");
    check(later.block_menu && !later.pass_pad, "later Settings frames keep Button Settings closed");
    check(!idle_click.block_menu && !idle_click.pass_pad, "a quiet frame does not rewrite the cursor");
    uint8_t icon[0x54] = {};
    std::memcpy(icon, "pic1", 4);
    icon[8] = 1;
    icon[0xA] = 255;
    std::memcpy(icon + 0xC, "P_Image_00", 11);
    std::memset(icon + 0x4C, 0x3F, 8);
    uint8_t label[0x54] = {};
    std::memcpy(label, "txt1", 4);
    label[8] = 1;
    std::memcpy(label + 0xC, "T_Btn_00", 9);
    check(settings_icon_name(icon, sizeof icon), "the arrow picture is the Settings icon");
    check(!settings_icon_name(label, sizeof label), "the Settings label stays");
    hide_settings_icon(icon);
    check((icon[8] & 1u) == 0 && icon[0xA] == 0 && icon[0x4C] == 0 && icon[0x50] == 0,
          "the Settings icon is hidden");

    if (fails) {
        std::fprintf(stderr, "%d map menu settings checks failed\n", fails);
        return 1;
    }
    std::printf("map menu settings checks passed\n");
    return 0;
}
