#include "platform/phone_rumble.h"
#include "platform/touch_buttons.h"

#include <cassert>
#include <cstdio>
#include <cstring>
#include <initializer_list>

int main() {
    using namespace touch_buttons;
    assert(visible(Automatic, false, false));
    assert(!visible(Automatic, true, false));
    assert(visible(Automatic, true, true));
    assert(visible(AlwaysShow, true, false));
    assert(!visible(Hide, false, false));
    assert(visible(Hide, true, true));
    controller = true;
    reveal_until = 0;
    assert(!visible_now());
    reveal();
    assert(visible_now());
    controller = false;
    reveal_until = 0;
    assert(visible_now());

    using namespace phone_rumble;
    assert(parse(nullptr) == kTouch && parse("1") == kTouch && parse("touch") == kTouch);
    assert(parse("0") == kOff && parse("garbage") == kOff);
    assert(parse("2") == kFallback && parse("fallback") == kFallback);
    for (bool touch : {false, true})
        for (bool motor : {false, true}) {
            assert(!allowed(kOff, touch, motor));
            assert(allowed(kTouch, touch, motor) == touch);
            assert(allowed(kFallback, touch, motor) == (touch || !motor));
        }
    assert(!touch_selected(true, false));
    assert(touch_selected(true, true));
    assert(!touch_selected(false, true));
    Refresh lease;
    assert(!lease.send(0, 0));
    assert(lease.send(65535, 1));
    assert(!lease.send(65535, 100));
    assert(lease.send(65535, 101));
    assert(lease.send(0, 102));
    assert(!lease.send(0, 1000));
    assert(lease.send(32768, 1001));
    assert(lease.send(10000, 1002));
    assert(!std::strcmp(env::PHONE_RUMBLE.name(), "NSMBU_PHONE_RUMBLE"));
    std::puts("touch controls policy: ok");
    return 0;
}
