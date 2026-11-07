#include "overlay/controller_shortcuts.h"
#include "input/input_map.h"
#include "platform/keycodes.h"

#include <cstdio>

static int g_pass = 0, g_fail = 0;

static void check(bool ok, const char* msg) {
    if (ok) ++g_pass;
    else {
        ++g_fail;
        std::printf("FAIL: %s\n", msg);
    }
}

int main() {
    using namespace overlay_shortcuts;
    check(!settings_controller_press(false, false), "idle");
    check(settings_controller_press(true, false), "home");
    check(settings_controller_press(false, true), "r3");
    check(settings_controller_press(true, true), "both");

    check(!combo_edge(false, false, false, false), "idle");
    check(combo_edge(true, true, false, false), "both pressed");
    check(!combo_edge(true, true, true, true), "both held");
    check(combo_edge(true, true, true, false), "second button joins");
    check(!combo_edge(true, false, false, false), "start only");

    const input_map::Mapping m = input_map::Mapping::defaults();
    bool keys[256] = {};
    check(!keyboard_start_select(m, keys), "no keys");
    keys[kVK_Return] = true;
    check(!keyboard_start_select(m, keys), "start key only");
    keys[kVK_Tab] = true;
    check(keyboard_start_select(m, keys), "start and select keys");

    std::printf("controller_shortcuts_test: %d passed, %d failed\n", g_pass, g_fail);
    return g_fail ? 1 : 0;
}
