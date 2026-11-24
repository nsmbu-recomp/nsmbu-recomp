#include "hooks/cannon_guard.h"

#include <cstdio>

// Issue #82: the World 1 castle cannon passes a null-page pointer and the game closes.

static int g_failed = 0;
#define CHECK(cond)                                                                \
    do {                                                                           \
        if (!(cond)) {                                                             \
            std::fprintf(stderr, "%s:%d: CHECK failed: %s\n", __FILE__, __LINE__, #cond); \
            g_failed++;                                                            \
        }                                                                          \
    } while (0)

int main() {
    CHECK(cannon_guest_absent(0));
    CHECK(cannon_guest_absent(0x11E));
    CHECK(cannon_guest_absent(0xFFFF));
    CHECK(!cannon_guest_absent(0x10000));
    CHECK(!cannon_guest_absent(0x024E25F4));
    if (g_failed) {
        std::fprintf(stderr, "cannon_guard_test: %d check(s) failed\n", g_failed);
        return 1;
    }
    std::printf("cannon_guard_test: all checks passed\n");
    return 0;
}
