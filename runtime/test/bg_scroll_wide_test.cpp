#include "gfx/bg_scroll_wide.h"
#include <cmath>
#include <cstdio>
#include <cstdlib>

static void check(bool ok) {
    if (!ok) {
        std::fprintf(stderr, "bg_scroll_wide check failed\n");
        std::exit(1);
    }
}

static bool same(float a, float b) { return std::fabs(a - b) <= 1e-4f * std::fmax(1.0f, std::fabs(a)); }

int main() {
    using aspect::scroll::Edges;
    using aspect::scroll::widen;

    Edges unit = widen(-320.0f, 320.0f, 0.0f, 1.0f);
    check(unit.left == -320.0f && unit.right == 320.0f && unit.width == 640.0f);

    const float kx = 4.0f / 3.0f;
    Edges wide = widen(-640.0f, 640.0f, 0.0f, kx);
    check(same(wide.left, -853.333333f) && same(wide.right, 853.333333f));
    check(same(wide.width, wide.right - wide.left));

    const float pivot = 80.0f;
    Edges skew = widen(0.0f, 100.0f, pivot, 2.0f);
    check(same(skew.left, pivot + (0.0f - pivot) * 2.0f));
    check(same(skew.right, pivot + (100.0f - pivot) * 2.0f));
    check(!same((skew.left + skew.right) * 0.5f, pivot));

    Edges narrow = widen(-400.0f, 400.0f, 0.0f, 0.75f);
    check(narrow.left == -400.0f && narrow.right == 400.0f && narrow.width == 800.0f);

    const float base_w = 796.444458f;
    const float base_h = 448.0f;
    check(aspect::scroll::should_widen(base_w, base_h, kx));
    check(!aspect::scroll::should_widen(base_w * kx, base_h, kx));
    check(!aspect::scroll::should_widen(base_w, base_h, 1.0f));
    check(!aspect::scroll::should_widen(200.0f, 448.0f, kx));

    return 0;
}
