#include "gfx/aspect_ui.h"
#include <cmath>
#include <cstdio>
#include <cstdlib>

static void check(bool ok) {
    if (!ok) {
        std::fprintf(stderr, "aspect ui check failed\n");
        std::exit(1);
    }
}

int main() {
    using namespace aspect::ui;
    const float w = 1280.0f, h = 720.0f;
    float diagonal[8] = {2.0f / w, 0, 0, 0, 0, 2.0f / h, 0, 0};
    check(screen_matrix(diagonal, 8, w, h));

    float packed[8] = {2.0f / w, -1.0f, 0, 0, 0, 2.0f / h, -1.0f, 0};
    check(screen_matrix(packed, 8, w, h));

    float vec2[2] = {2.0f / w, 2.0f / h};
    check(screen_matrix(vec2, 2, w, h));

    float persp[8] = {1.2f, 0.3f, 0.1f, 0, 0.2f, 0.9f, 0.4f, 1.0f};
    check(!screen_matrix(persp, 8, w, h));

    float noise[8] = {0.1f, 0.2f, 0.3f, 0.4f, 0.5f, 0.6f, 0.7f, 0.8f};
    check(!screen_matrix(noise, 8, w, h));

    return 0;
}
