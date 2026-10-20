#include "mods/mods.h"

#include "crash/crashrec.h"
#include "core/runtime.h"
#include "input/input.h"
#include "../input/motion/motion.h"
#include "input/rumble.h"
#include "../mods/lua_press.h"

namespace interp { bool repeat_input(); uint64_t logic_steps(); }

namespace {
constexpr int32_t kWpadErrNone = 0, kWpadErrNoController = -1;
constexpr int32_t kKpadErrNone = 0, kKpadErrNoController = -2;
constexpr uint8_t kDevURCC = 31, kDevNone = 253, kFormatURCC = 22;

bool connected(uint32_t chan) { return chan == 0 && input::pro_controller(); }

uint32_t pro_buttons(uint32_t v) {
    static const uint32_t map[][2] = {
        {input::kUp, 0x1},     {input::kLeft, 0x2},    {input::kZR, 0x4},     {input::kX, 0x8},
        {input::kA, 0x10},     {input::kY, 0x20},      {input::kB, 0x40},     {input::kZL, 0x80},
        {input::kR, 0x200},    {input::kPlus, 0x400},  {input::kHome, 0x800}, {input::kMinus, 0x1000},
        {input::kL, 0x2000},   {input::kDown, 0x4000}, {input::kRight, 0x8000},
        {input::kStickR, 0x10000}, {input::kStickL, 0x20000},
    };
    uint32_t out = 0;
    for (auto& m : map)
        if (v & m[0]) out |= m[1];
    return out;
}
}

HLE(padscore, KPADInitEx) {}
HLE(padscore, KPADGetMplsWorkSize) { ret(c, 0x5FE0); }
HLE(padscore, KPADSetMplsWorkarea) {}
HLE(padscore, WPADEnableURCC) {}
HLE(padscore, WPADEnableWiiRemote) {}
HLE(padscore, WPADDisconnect) {}

HLE(padscore, WPADControlMotor) {
    uint32_t chan = arg(c, 0), cmd = arg(c, 1);
    if (connected(chan)) {
        TRACE("[pad] WPADControlMotor(%u, %u) -> %s", chan, cmd, cmd ? "rumble" : "stop");
        rumble::pro_motor(chan, cmd != 0);
    }
    ret(c, (uint32_t)kWpadErrNone);
}
HLE(padscore, WPADGetBatteryLevel) { ret(c, 4); }
HLE(padscore, WPADCanSendStreamData) { ret(c, 0); }
HLE(padscore, WPADSendStreamData) { ret(c, (uint32_t)kWpadErrNoController); }
HLE(padscore, WPADControlSpeaker) { ret(c, (uint32_t)kWpadErrNoController); }

HLE(padscore, WPADProbe) {

    uint32_t chan = arg(c, 0), type = arg(c, 1);
    bool on = connected(chan);
    TRACE("[pad] WPADProbe(%u) -> %s", chan, on ? "pro" : "none");
    if (type) st32(type, on ? kDevURCC : kDevNone);
    ret(c, (uint32_t)(on ? kWpadErrNone : kWpadErrNoController));
}

HLE(padscore, KPADReadEx) {

    uint32_t chan = arg(c, 0), st = arg(c, 1), count = arg(c, 2), err = arg(c, 3);
    TRACE("[pad] KPADReadEx(%u, %08X, %u) connected=%d", chan, st, count, connected(chan));
    if (!connected(chan) || !st || (int32_t)count <= 0) {
        if (err) st32(err, (uint32_t)(connected(chan) ? kKpadErrNone : kKpadErrNoController));
        ret(c, 0);
        return;
    }
    static uint32_t last = 0;
    static input::PadState last_p;
    const bool repeat = interp::repeat_input();
    input::PadState p = repeat ? last_p : crashrec::read(1);
    if (!repeat) p.buttons |= mods::lua_press::active(interp::logic_steps());
    last_p = p;
    if (!repeat) motion::right_stick(p.rx, p.ry);
    uint32_t hold = pro_buttons(p.buttons);
    memset(mem::ptr(st), 0, 0xF0);
    st8(st + 0x5C, kDevURCC);
    st8(st + 0x5D, 0);
    st8(st + 0x5F, kFormatURCC);
    st32(st + 0x60, hold);
    st32(st + 0x64, hold & ~last);
    st32(st + 0x68, last & ~hold);
    last = hold;
    stf32(st + 0x6C, p.lx); stf32(st + 0x70, p.ly);
    stf32(st + 0x74, p.rx); stf32(st + 0x78, p.ry);
    st32(st + 0x7C, 1);
    st32(st + 0x80, 1);
    if (err) st32(err, kKpadErrNone);
    ret(c, 1);
}

HLE(padscore, KPADDisableDPD) {}
HLE(padscore, WENCGetEncodeData) { ret(c, (uint32_t)kWpadErrNoController); }
HLE(padscore, WPADGetSpeakerVolume) { ret(c, 0); }
HLE(padscore, WPADResetAutoSleepTimeCount) {}
HLE(padscore, WPADSetAutoSleepTime) {}
