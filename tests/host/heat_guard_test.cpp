#include "platform/heat_guard.h"

#include <cassert>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <initializer_list>
#include <string>

using namespace heat_guard;

namespace {
std::string run(DeviceClass device, Setting setting, std::initializer_list<float> headrooms) {
    const Limits l = limits(device, setting);
    bool paused = false;
    std::string out;
    for (float h : headrooms) {
        paused = next_paused(paused, h, l);
        out += paused ? 'P' : '-';
    }
    return out;
}
void expect(const std::string& got, const char* want, const char* what) {
    if (got != want) {
        std::fprintf(stderr, "%s: got %s, want %s\n", what, got.c_str(), want);
        assert(false);
    }
}
}

int main() {
    {
        Limits l = limits(kPhone, kNormal);
        assert(l.active && l.pause == 0.72f && l.resume == 0.60f);
        l = limits(kFanHandheld, kNormal);
        assert(l.active && l.pause == 1.00f && l.resume == 0.90f);
        l = limits(kPhone, kRelaxed);
        assert(l.active && l.pause == 0.90f && l.resume == 0.80f);
        l = limits(kFanHandheld, kRelaxed);
        assert(l.active && l.pause == 1.10f && l.resume == 1.00f);
        assert(!limits(kPhone, kOff).active && !limits(kFanHandheld, kOff).active);
    }
    const auto s2 = {0.57f, 0.67f, 0.73f, 0.74f, 0.70f, 0.65f, 0.62f};
    expect(run(kPhone, kNormal, s2), "--PPPPP", "phone limits pause at 0.73 and never resume above 0.60");
    expect(run(kFanHandheld, kNormal, s2), "-------", "fan handheld keeps 60 fps at 0.73");
    expect(run(kPhone, kRelaxed, s2), "-------", "relaxed phone keeps 60 fps at 0.73");
    expect(run(kPhone, kOff, s2), "-------", "off never pauses");

    expect(run(kPhone, kNormal, {0.71f, 0.72f, 0.65f, 0.60f, 0.59f, 0.71f, 0.72f}), "-PPP--P", "phone hysteresis");
    expect(run(kFanHandheld, kNormal, {0.95f, 0.99f, 1.00f, 0.95f, 0.90f, 0.89f, 0.99f, 1.05f}), "--PPP--P",
           "handheld hysteresis");
    expect(run(kPhone, kRelaxed, {0.89f, 0.90f, 0.81f, 0.79f}), "-PP-", "relaxed phone hysteresis");
    expect(run(kFanHandheld, kRelaxed, {1.09f, 1.10f, 1.01f, 0.99f}), "-PP-", "relaxed handheld hysteresis");
    expect(run(kFanHandheld, kNormal, {0.90f, 0.92f, 0.94f, 0.96f, 0.93f, 0.89f}), "------", "handheld keeps 60 fps at 0.90-0.96");
    expect(run(kFanHandheld, kOff, {0.95f, 1.0f, 1.2f}), "---", "off on a handheld");

    expect(run(kPhone, kNormal, {NAN, 0.80f, NAN, NAN, 0.50f, NAN}), "-PPP--", "NaN keeps the state");
    assert(!next_paused(true, 0.95f, limits(kPhone, kOff)));
    assert(next_paused(true, 0.95f, limits(kFanHandheld, kNormal)));
    assert(!next_paused(true, 0.89f, limits(kFanHandheld, kNormal)));

    assert(parse(nullptr) == kNormal && parse("normal") == kNormal && parse("0") == kNormal);
    assert(parse("relaxed") == kRelaxed && parse("1") == kRelaxed);
    assert(parse("off") == kOff && parse("2") == kOff);
    assert(parse("garbage") == kNormal && parse("") == kNormal);
    for (Setting s : {kNormal, kRelaxed, kOff}) assert(parse(name(s)) == s);
    assert(to_device(1) == kFanHandheld && to_device(0) == kPhone && to_device(7) == kPhone);
    assert(to_setting(2) == kOff && to_setting(-1) == kNormal);
    assert(!std::strcmp(env::HEAT_GUARD.name(), "NSMBU_HEAT_GUARD"));
    std::puts("heat guard policy: ok");
    return 0;
}
