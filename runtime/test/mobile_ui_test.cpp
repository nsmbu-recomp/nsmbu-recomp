#include "overlay/mobile_ui.h"

#include <cmath>
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
    const mobile_ui::Layout phone = mobile_ui::layout(450, 450 / 160.0f, mobile_ui::kAutomatic, 1080, 2400);
    check(phone.phone, "tall phone");
    check(phone.scale >= 1.0f, "phone scale");
    check(phone.control_mm(450) >= 7.0f, "phone control height");
    const mobile_ui::Layout tablet = mobile_ui::layout(320, 2.0f, mobile_ui::kAutomatic, 1920, 1200);
    check(!tablet.phone, "tablet");
    const mobile_ui::Layout handheld = mobile_ui::android_settings(440, 2.75f, 1920, 1080);
    check(handheld.pad_y == mobile_ui::kPhonePad, "android settings padding");
    check(handheld.control_mm(440) >= 10.0f, "android settings control height");
    check(handheld.scale > tablet.scale, "android settings larger than tablet automatic");
    check(std::fabs(mobile_ui::desktop_scale(2.0f, 1504) - 2.0f) < 1e-5f, "desktop hidpi");
    std::printf("mobile_ui_test: %d passed, %d failed\n", g_pass, g_fail);
    return g_fail ? 1 : 0;
}
