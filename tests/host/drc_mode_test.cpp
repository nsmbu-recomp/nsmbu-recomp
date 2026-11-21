#include "gfx/display_modes.h"

#include <cstdio>

static int failures = 0;

static void check(bool ok, const char* what) {
    if (!ok) {
        std::fprintf(stderr, "FAIL: %s\n", what);
        failures++;
    }
}

int main() {
    check(gfx::kDrcDefaultMode == gfx::kDrcOff, "default off");
    check(gfx::kDrcUiOrder[0] == gfx::kDrcOff, "off is first");
    check(sizeof gfx::kDrcUiOrder / sizeof gfx::kDrcUiOrder[0] == gfx::kDrcModeCount, "every mode");
    bool seen[gfx::kDrcModeCount] = {};
    for (gfx::DrcMode m : gfx::kDrcUiOrder) {
        check(m >= 0 && m < gfx::kDrcModeCount, "mode in range");
        if (m >= 0 && m < gfx::kDrcModeCount) seen[m] = true;
    }
    for (bool on : seen) check(on, "mode listed once");
    return failures ? 1 : 0;
}
