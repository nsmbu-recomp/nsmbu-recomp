#include "mods/mods.h"

#include "crash/crashrec.h"
#include "core/runtime.h"
#include "input/input.h"
#include "input/rumble.h"
#include "../input/motion/motion.h"
#include "../mods/lua_press.h"

#include "core/env.h"
namespace interp { bool repeat_input(); void trace_read(const char*); uint64_t logic_steps(); }

static constexpr uint32_t kResultOk = 0;
static constexpr uint32_t kResultFail = 0xA0000000;

HLE(nn_act, Initialize__Q2_2nn3actFv) { ret(c, kResultOk); }
HLE(nn_act, Finalize__Q2_2nn3actFv) { ret(c, kResultOk); }
HLE(nn_act, GetSlotNo__Q2_2nn3actFv) { ret(c, 1); }
HLE(nn_act, GetPrincipalId__Q2_2nn3actFv) { ret(c, 0); }
HLE(nn_act, GetParentalControlSlotNoEx__Q2_2nn3actFPUcUc) { st8(arg(c, 0), 1); ret(c, kResultOk); }
HLE(nn_act, IsSlotOccupied__Q2_2nn3actFUc) { ret(c, arg(c, 0) == 1 ? 1 : 0); }
HLE(nn_act, GetNumOfAccounts__Q2_2nn3actFv) { ret(c, 1); }
HLE(nn_act, GetMiiEx__Q2_2nn3actFP12FFLStoreDataUc) {
    if (arg(c, 0)) memset(mem::ptr(arg(c, 0)), 0, 96);
    ret(c, kResultFail);
}
HLE(nn_act, GetTransferableIdEx__Q2_2nn3actFPULUiUc) {
    if (arg(c, 0)) st64(arg(c, 0), 1);
    ret(c, kResultOk);
}

HLE(nn_ac, Initialize__Q2_2nn2acFv) { ret(c, kResultOk); }
HLE(nn_ac, Finalize__Q2_2nn2acFv) { ret(c, kResultOk); }
HLE(nn_ac, Connect__Q2_2nn2acFv) { ret(c, kResultFail); }
HLE(nn_ac, Close__Q2_2nn2acFv) { ret(c, kResultOk); }
HLE(nn_ac, GetLastErrorCode__Q2_2nn2acFPUi) { st32(arg(c, 0), 1100); ret(c, kResultOk); }

HLE(nn_boss, Initialize__Q2_2nn4bossFv) { ret(c, kResultFail); }
HLE(nn_boss, IsInitialized__Q2_2nn4bossFv) { ret(c, 0); }
HLE(nn_boss, Finalize__Q2_2nn4bossFv) {}

HLE(nn_boss, __ct__Q3_2nn4boss17PlayReportSettingFv) { ret(c, arg(c, 0)); }
HLE(nn_boss, __dt__Q3_2nn4boss17PlayReportSettingFv) {}
HLE(nn_boss, Initialize__Q3_2nn4boss17PlayReportSettingFPvUi) {}
HLE(nn_boss, Set__Q3_2nn4boss17PlayReportSettingFUiT1) { ret(c, 1); }
HLE(nn_boss, __ct__Q3_2nn4boss4TaskFv) { ret(c, arg(c, 0)); }
HLE(nn_boss, __dt__Q3_2nn4boss4TaskFv) {}
HLE(nn_boss, Initialize__Q3_2nn4boss4TaskFPCcUi) { ret(c, kResultFail); }
HLE(nn_boss, IsRegistered__Q3_2nn4boss4TaskCFv) { ret(c, 0); }
HLE(nn_boss, Register__Q3_2nn4boss4TaskFRQ3_2nn4boss11TaskSetting) { ret(c, kResultFail); }
HLE(nn_boss, StartScheduling__Q3_2nn4boss4TaskFb) { ret(c, kResultFail); }
HLE(nn_boss, Run__Q3_2nn4boss4TaskFb) { ret(c, kResultFail); }
HLE(nn_boss, Unregister__Q3_2nn4boss4TaskFv) { ret(c, kResultFail); }
HLE(nn_boss, UpdateLifeTimeSec__Q3_2nn4boss4TaskFL) { ret(c, kResultFail); }
HLE(nn_boss, __ct__Q3_2nn4boss10TaskResultFQ2_2nn6Result) { ret(c, arg(c, 0)); }
HLE(nn_boss, ErrorCode__Q3_2nn4boss10TaskResultCFv) { ret(c, 0); }
HLE(nn_boss, __ct__Q3_2nn4boss15NbdlTaskSettingFv) { ret(c, arg(c, 0)); }
HLE(nn_boss, __dt__Q3_2nn4boss15NbdlTaskSettingFv) {}
HLE(nn_boss, Initialize__Q3_2nn4boss15NbdlTaskSettingFPCcLT1) { ret(c, kResultFail); }
HLE(nn_boss, __ct__Q3_2nn4boss7TitleIDFUL) { ret(c, arg(c, 0)); }
HLE(nn_boss, __ct__Q3_2nn4boss5TitleFUiQ3_2nn4boss7TitleID) { ret(c, arg(c, 0)); }
HLE(nn_boss, __dt__Q3_2nn4boss5TitleFv) {}
HLE(nn_boss, GetOptoutFlag__Q3_2nn4boss5TitleCFv) { ret(c, 1); }
HLE(nn_boss, SetOptoutFlag__Q3_2nn4boss5TitleFb) { ret(c, kResultOk); }

HLE(nn_olv, Initialize__Q2_2nn3olvFPCQ3_2nn3olv15InitializeParam) { ret(c, kResultFail); }
HLE(nn_olv, IsInitialized__Q2_2nn3olvFv) { ret(c, 0); }
HLE(nn_olv, Finalize__Q2_2nn3olvFv) { ret(c, kResultOk); }

// Stubs, not implementations. Miiverse (nn_olv) is offline here, so the post objects get
// constructed empty and reports go nowhere. These replace the generated "unimplemented"
// fallback, so they no longer log to [hle].
HLE(nn_olv, __ct__Q3_2nn3olv18DownloadedPostDataFv) { ret(c, arg(c, 0)); }
HLE(nn_olv, SetReportTypes__Q3_2nn3olv6ReportFUi) {}

// Stubs, not implementations. They report "no DLC installed", which is correct offline.
// Return values match Cemu's nn_aoc (0x80 + min(count, 256) * 0x61 work buffer, 0 titles).
HLE(nn_aoc, AOC_Initialize) { ret(c, kResultOk); }
HLE(nn_aoc, AOC_CalculateWorkBufferSize) { ret(c, 0x80 + std::min<uint32_t>(arg(c, 0), 256) * 0x61); }
HLE(nn_aoc, AOC_ListTitle) {
    if (arg(c, 0)) st32(arg(c, 0), 0);
    ret(c, kResultOk);
}
HLE(nn_aoc, AOC_OpenTitle) { ret(c, kResultOk); }

extern "C" void f_0203DEEC_orig(Cpu* c);
extern "C" void hook_0203DEEC(Cpu* c) {
    threads::park_sleep_until(std::chrono::steady_clock::now() + std::chrono::milliseconds(1));
    f_0203DEEC_orig(c);
}

HLE(nsysnet, socket_lib_init) { ret(c, 0); }
HLE(nsysnet, socket_lib_finish) { ret(c, 0); }
HLE(nsysnet, NSSLInit) { ret(c, 0); }
HLE(nsysnet, NSSLFinish) { ret(c, 0); }
HLE(nlibcurl, curl_global_init_mem) { ret(c, 0); }
HLE(nlibcurl, curl_global_cleanup) {}

HLE(proc_ui, ProcUIInit) {}
HLE(proc_ui, ProcUIShutdown) {}
HLE(proc_ui, ProcUIRegisterCallback) {}
HLE(proc_ui, ProcUIDrawDoneRelease) {}
HLE(proc_ui, ProcUIProcessMessages) { ret(c, 0); }

HLE(vpad, VPADRead) {

    uint32_t chan = arg(c, 0), st = arg(c, 1), count = arg(c, 2), err = arg(c, 3);

    static int trace = env::TRACE_VPAD.integer_or(0);
    if (trace > 0) {
        trace--;
        char buf[256];
        int n = snprintf(buf, sizeof buf, "[vpad] read from lr=%08X", c->lr);
        for (uint32_t sp = c->r[1], i = 0; i < 8 && sp; i++) {
            uint32_t prev = ld32(sp);
            if (!prev || prev <= sp) break;
            n += snprintf(buf + n, sizeof buf - n, " <- %08X", ld32(prev + 4));
            sp = prev;
        }
        LOG("%s", buf);
    }
    if (chan != 0 || !st || (int32_t)count <= 0) {
        if (err) st32(err, (uint32_t)-2);
        ret(c, 0);
        return;
    }
    static uint32_t last_hold = 0;
    interp::trace_read("VPAD");

    static input::PadState last_p;
    const bool repeat = interp::repeat_input();
    input::PadState p = repeat ? last_p : crashrec::read(0);
    last_p = p;
    if (input::pro_controller()) {
        p.buttons = 0;
        p.lx = p.ly = p.rx = p.ry = 0;
    } else if (!repeat) {
        motion::right_stick(p.rx, p.ry);
    }
    if (!repeat) p.buttons |= mods::lua_press::active(interp::logic_steps());
    uint32_t hold = p.buttons;
    auto stick_dirs = [&](float x, float y, uint32_t up, uint32_t down, uint32_t left, uint32_t right) {

        auto dir = [&](float v, bool held, uint32_t bit) { if (v >= 0.5f || (held && v >= 0.1f)) hold |= bit; };
        dir(-x, last_hold & left, left); if (!(hold & left)) dir(x, last_hold & right, right);
        dir(-y, last_hold & down, down); if (!(hold & down)) dir(y, last_hold & up, up);
    };
    stick_dirs(p.lx, p.ly, 0x10000000, 0x08000000, 0x40000000, 0x20000000);
    stick_dirs(p.rx, p.ry, 0x01000000, 0x00800000, 0x04000000, 0x02000000);
    if (repeat) hold = last_hold;

    memset(mem::ptr(st), 0, 0xAC);
    st32(st + 0x00, hold);
    st32(st + 0x04, hold & ~last_hold);
    st32(st + 0x08, last_hold & ~hold);
    last_hold = hold;
    {
        static FILE* pt = env::PAD_TRACE() ? fopen(env::PAD_TRACE(), "w") : nullptr;
        if (pt) { fprintf(pt, "%llu %d %08X %08X\n", (unsigned long long)interp::logic_steps(), (int)repeat, hold, ld32(st + 4)); fflush(pt); }
    }
    stf32(st + 0x0C, p.lx); stf32(st + 0x10, p.ly);
    stf32(st + 0x14, p.rx); stf32(st + 0x18, p.ry);
    {

        const motion::VpadMotion m = motion::vpad(repeat);
        auto vec = [&](uint32_t at, const motion::Vec3& v) { stf32(at, v.x); stf32(at + 4, v.y); stf32(at + 8, v.z); };
        vec(st + 0x1C, m.acc);
        stf32(st + 0x28, m.acc_magnitude);
        stf32(st + 0x2C, m.acc_variation);
        stf32(st + 0x30, m.acc_xy[0]); stf32(st + 0x34, m.acc_xy[1]);
        vec(st + 0x38, m.gyro);
        vec(st + 0x44, m.angle);
        for (int i = 0; i < 3; i++) vec(st + 0x6C + i * 0xC, m.dir[i]);
    }

    static uint16_t last_tx = 0, last_ty = 0;
    if (p.touch) {
        last_tx = (uint16_t)(p.tx * 3883.0f + 92.0f);
        last_ty = (uint16_t)(4095.0f - p.ty * 3694.0f - 254.0f);
    }
    for (uint32_t tp = 0x52; tp <= 0x62; tp += 8) {
        st16(st + tp + 0, last_tx);
        st16(st + tp + 2, last_ty);
        st16(st + tp + 4, p.touch ? 1 : 0);
        st16(st + tp + 6, p.touch ? 0 : 3);
    }
    st8(st + 0xA0, 0xFF); st8(st + 0xA3, 0xFF);
    st8(st + 0xA1, 0xC0);
    if (err) st32(err, 0);
    ret(c, 1);
}

static void tp_to_screen(uint32_t out, uint32_t raw, int w, int h) {
    int x = std::max<int>(ld16(raw) - 92, 0), y = std::max<int>(4095 - (int)ld16(raw + 2) - 254, 0);
    st16(out, (uint16_t)(x / 3883.0 * w));
    st16(out + 2, (uint16_t)(y / 3694.0 * h));
    st16(out + 4, ld16(raw + 4));
    st16(out + 6, ld16(raw + 6));
}
HLE(vpad, VPADGetTPCalibratedPoint) { tp_to_screen(arg(c, 1), arg(c, 2), 1280, 720); }
HLE(vpad, VPADGetTPCalibratedPointEx) {
    int res = (int)arg(c, 1);
    tp_to_screen(arg(c, 2), arg(c, 3), res == 0 ? 1920 : res == 2 ? 854 : 1280, res == 0 ? 1080 : res == 2 ? 480 : 720);
}

HLE(vpad, VPADControlMotor) {

    uint32_t chan = arg(c, 0), pattern = arg(c, 1), nbits = std::min<uint32_t>(arg(c, 2) & 0xFF, rumble::Motor::kMaxBits);
    uint8_t bits[rumble::Motor::kMaxBits / 8] = {};
    for (uint32_t i = 0; pattern && i < (nbits + 7) / 8; i++) bits[i] = ld8(pattern + i);
    TRACE("[pad] VPADControlMotor(%u, %u bits)", chan, nbits);
    rumble::gamepad_pattern(chan, pattern ? bits : nullptr, nbits);
    ret(c, 0);
}
HLE(vpad, VPADStopMotor) { rumble::gamepad_stop(arg(c, 0)); }
HLE(vpadbase, VPADBASEGetHeadphoneStatus) { ret(c, 0); }

HLE(sysapp, SYSLaunchSettings) { ret(c, 0); }
HLE(sysapp, SYSLaunchAccount) { ret(c, 0); }
HLE(sysapp, SYSSwitchToSyncControllerOnHBM) {}
HLE(sysapp, _SYSGetSystemApplicationTitleId) { ret64(c, 0); }
