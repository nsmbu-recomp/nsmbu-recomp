#include "overlay/gamepad_nav.h"
#include "input/input_map.h"

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
    overlay::GamepadNavigation nav;
    float v[input_map::kPadCount] = {}, p[input_map::kPadCount] = {};
    check(!nav.pressed(v, p, input_map::kPadA), "idle");
    v[input_map::kPadA] = 1;
    check(nav.pressed(v, p, input_map::kPadA), "A edge");
    p[input_map::kPadA] = 1;
    check(!nav.pressed(v, p, input_map::kPadA), "A held");
    nav.reset();
    check(nav.controller_active(), "controller active after reset");
    std::printf("gamepad_nav_test: %d passed, %d failed\n", g_pass, g_fail);
    return g_fail ? 1 : 0;
}
