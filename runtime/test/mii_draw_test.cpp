#include "hooks/mii_draw.h"

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
    CHECK(kMiiShaderProgram == kMiiShaderObject + 4);
    CHECK(mii_model_draw_ready(0x1000, 0x2000));
    CHECK(!mii_model_draw_ready(0, 0x2000));
    CHECK(!mii_model_draw_ready(0x1000, 0));
    CHECK(!mii_model_draw_ready(0, 0));
    if (g_failed) {
        std::fprintf(stderr, "mii_draw_test: %d check(s) failed\n", g_failed);
        return 1;
    }
    std::printf("mii_draw_test: all checks passed\n");
    return 0;
}
