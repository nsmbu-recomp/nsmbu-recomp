#include "gfx/color_seed.h"

#include <cstdio>

static int g_failed = 0;
#define CHECK(cond)                                                                \
    do {                                                                           \
        if (!(cond)) {                                                             \
            std::fprintf(stderr, "%s:%d: CHECK failed: %s\n", __FILE__, __LINE__, #cond); \
            g_failed++;                                                            \
        }                                                                          \
    } while (0)

int main() {
    CHECK(gfx::seed_new_color_target(true, false, 1, true, false, 1280, 720, 1920, 1080));
    CHECK(!gfx::seed_new_color_target(true, true, 1, true, false, 1280, 720, 1920, 1080));
    CHECK(!gfx::seed_new_color_target(true, false, 2, true, false, 1280, 720, 1920, 1080));
    CHECK(!gfx::seed_new_color_target(true, false, 0, true, false, 1280, 720, 1920, 1080));
    CHECK(!gfx::seed_new_color_target(true, false, 1, false, false, 1280, 720, 1920, 1080));
    CHECK(!gfx::seed_new_color_target(true, false, 1, true, true, 1280, 720, 1920, 1080));
    CHECK(!gfx::seed_new_color_target(false, false, 1, true, false, 1280, 720, 1920, 1080));
    CHECK(!gfx::seed_new_color_target(true, false, 1, true, false, 0, 720, 1920, 1080));
    CHECK(!gfx::seed_new_color_target(true, false, 1, true, false, 1280, 0, 1920, 1080));
    CHECK(!gfx::seed_new_color_target(true, false, 1, true, false, 1280, 720, 0, 1080));
    CHECK(!gfx::seed_new_color_target(true, false, 1, true, false, 1280, 720, 1920, 0));
    if (g_failed) {
        std::fprintf(stderr, "color_seed_test: %d check(s) failed\n", g_failed);
        return 1;
    }
    std::printf("color_seed_test: all checks passed\n");
    return 0;
}
