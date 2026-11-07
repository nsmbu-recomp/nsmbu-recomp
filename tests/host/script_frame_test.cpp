#include "test/script_frame.h"

#include <cstdio>
#include <cstdlib>

static int failures = 0;

static void check(bool ok, const char* what) {
    if (!ok) {
        std::fprintf(stderr, "FAIL: %s\n", what);
        failures++;
    }
}

int main() {
    using namespace script_frame;

    check(!enabled_value(nullptr), "null is off");
    check(!enabled_value(""), "empty is off");
    check(!enabled_value("0"), "zero is off");
    check(enabled_value("1"), "one is on");
    check(enabled_value("yes"), "text is on");
    check(enabled_value("00"), "00 is on");

    check(rebase(500, -1) == 500, "no anchor keeps the tv frame");
    check(rebase(99, 100) == 99, "before the anchor keeps the tv frame");
    check(rebase(100, 100) == kAlignFrame, "anchor frame counts as the align frame");
    check(rebase(160, 100) == kAlignFrame + 60, "frames after the anchor are relative");
    check(rebase(5000, 4000) == kAlignFrame + 1000, "a longer boot gives the same script frame");
    check(rebase(3000, 2000) == rebase(4000, 3000), "two boot lengths agree one second in");
    check(rebase(0, 0) == kAlignFrame, "anchor at frame zero");

    const bool expect = enabled_value(std::getenv("NSMBU_TEST_FIXED_SEED"));
    check(enabled() == expect, "switch follows NSMBU_TEST_FIXED_SEED");
    check(frame(1234) == 1234, "frame is the tv frame before the anchor");
    check(!aligned(), "not aligned at start");
    set_align_frame(500);
    check(aligned(), "aligned after set");
    set_align_frame(900);
    check(frame(500) == (expect ? kAlignFrame : 500), "first anchor wins");
    check(frame(499) == 499, "before the anchor keeps the tv frame");
    check(frame(560) == (expect ? kAlignFrame + 60 : 560), "after the anchor");

    if (failures) return 1;
    std::printf("script_frame_test: all checks passed\n");
    return 0;
}
