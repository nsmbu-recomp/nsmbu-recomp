#include "overlay/overlay_click.h"

#include <cmath>
#include <cstdio>

static int fails = 0;
static void check(bool cond, const char* what) {
    if (cond) return;
    std::fprintf(stderr, "FAIL %s\n", what);
    ++fails;
}

int main() {
    const OverlayClick hit = overlay_close_click(0.90f, 0.04f, 0.94f, 0.08f);
    check(hit.ok, "the close button has a click point");
    check(std::fabs(hit.x - 0.92f) < 1e-5f && std::fabs(hit.y - 0.06f) < 1e-5f,
          "the click is the center of the close button");
    check(hit.x > 0.90f && hit.x < 0.94f && hit.y > 0.04f && hit.y < 0.08f, "the click lands on the close button");
    check(!overlay_close_click(0.f, 0.f, 0.f, 0.f).ok, "a missing close button has no click");
    check(!overlay_close_click(0.2f, 0.2f, 0.1f, 0.3f).ok, "an empty close button has no click");
    check(!overlay_close_click(-0.1f, 0.04f, 0.1f, 0.08f).ok, "a close button off the display has no click");
    if (fails) {
        std::fprintf(stderr, "%d failed\n", fails);
        return 1;
    }
    return 0;
}
