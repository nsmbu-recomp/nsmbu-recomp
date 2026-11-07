#include "gfx/picture_box.h"

#include <cmath>
#include <cstdio>

static int failures = 0;

static void check(bool ok, const char* what) {
    if (!ok) {
        std::fprintf(stderr, "FAIL: %s\n", what);
        failures++;
    }
}

static bool near(float a, float b) { return std::fabs(a - b) <= 1.5f; }

int main() {
    float scale = 0;
    const float wide = 64.0f / 27.0f;
    gfx::Box tv = gfx::tv_picture(1920, 1080, 1280, 720, true, false, wide, 0, &scale);
    check(near(tv.w, 1920) && near(tv.h, 810), "21:9 TV box in a 16:9 window");
    check(std::fabs(tv.w / tv.h - wide) < 0.01f, "TV box is 21:9");
    check(tv.y > 100, "TV box sits inside the window");

    gfx::Box pad = gfx::fit_picture(1920, 1080, 854, 480, 0, &scale);
    check(std::fabs(pad.w / pad.h - 854.0f / 480.0f) < 0.02f, "GamePad picture keeps its shape");
    check(pad.h > 1000, "GamePad picture uses the window height");

    gfx::Box original = gfx::tv_picture(2560, 1080, 1280, 720, true, false, 16.0f / 9.0f, 0, &scale);
    check(near(original.w, 1920) && near(original.h, 1080), "16:9 choice stays 16:9 on a wide window");

    gfx::Box matched = gfx::tv_picture(2560, 1080, 1280, 720, true, true, 16.0f / 9.0f, 0, &scale);
    check(near(matched.w, 2560) && near(matched.h, 1080), "match window fills the TV window");

    gfx::Box fitted = gfx::tv_picture(1920, 1080, 1707, 720, false, false, wide, 0, &scale);
    check(near(fitted.w, 1920) && near(fitted.h, 810), "stretch off fits the wide TV picture");

    gfx::Box tall = gfx::tv_picture(1920, 1080, 1280, 720, true, false, 16.0f / 10.0f, 0, &scale);
    check(near(tall.w, 1728) && near(tall.h, 1080), "16:10 TV box is taller than 16:9");

    return failures ? 1 : 0;
}
