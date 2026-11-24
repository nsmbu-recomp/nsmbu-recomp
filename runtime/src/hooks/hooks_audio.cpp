

#include <cstdlib>
#include <cstring>
#include <vector>

#include "guest_addr.h"
#include "core/runtime.h"

#include "core/env.h"
extern "C" void f_0281B4EC_orig(Cpu* c);
extern "C" void f_0281B970_orig(Cpu* c);

namespace {
inline double ldf32d(uint32_t ea) { return (double)u32_as_f32(ld32(ea)); }
inline int16_t lds16(uint32_t ea) { return (int16_t)ld16(ea); }
inline uint16_t low16(uint64_t fctiw) { return (uint16_t)(uint32_t)fctiw; }

void filter8(Cpu* c) {
    uint32_t in = c->r[4];
    const uint32_t coef = c->r[5];
    const double maxv = ldf32d(GD(0x10170620)), minv = ldf32d(GD(0x10170624)), start = ldf32d(GD(0x10170628)),
                 scale = round25(ldf32d(GD(0x1017062C)));
    double k[8];
    for (int t = 0; t < 8; t++) k[t] = round25(to_single((double)lds16(coef + 2 * t)));
    for (int n = 0; n < 80; n++, in += 2) {
        double acc = start;
        for (int t = 0; t < 8; t++) acc = to_single(to_single((double)lds16(in + 2 * t)) * k[t] + acc);
        double v = to_single(acc * scale);
        uint64_t r;
        if (v < minv)
            r = ppc_fctiwz(minv);
        else {
            if (v > maxv) v = maxv;
            r = ppc_fctiwz(v);
        }
        st16(in, low16(r));
    }
}

void mix_ramp(Cpu* c) {
    const double zero = ldf32d(GD(0x10170840));
    if (c->f[1].ps0 == zero && c->f[2].ps0 == zero) {
        c->f[1].ps0 = zero;
        return;
    }
    const double maxv = ldf32d(GD(0x10170848)), minv = ldf32d(GD(0x1017084C));
    const double step = to_single(to_single(c->f[2].ps0 - c->f[1].ps0) / ldf32d(GD(0x10170844)));
    double vol = c->f[1].ps0;
    uint32_t dst = c->r[4], src = c->r[5];
    for (int n = 0; n < 80; n++, dst += 2, src += 2) {
        double v = to_single(to_single((double)lds16(src)) * round25(vol) + to_single((double)lds16(dst)));
        uint64_t r;
        if (v < minv)
            r = ppc_fctiwz(minv);
        else {
            if (v > maxv) v = maxv;
            r = ppc_fctiwz(v);
        }
        vol = to_single(vol + step);
        st16(dst, low16(r));
    }
    c->f[1].ps0 = vol;
    c->f[1].ps1 = vol;
}

const bool g_check = [] {
    const char* e = env::HOOK_CHECK();
    return e && *e && strcmp(e, "0") != 0;
}();

template <class Native>
void checked(Cpu* c, void (*orig)(Cpu*), Native native, const char* name, uint32_t outAddr, uint32_t outBytes,
             bool fpResult) {
    static uint64_t calls = 0, mismatches = 0;
    const uint32_t lo = outAddr, hi = outAddr + outBytes;
    std::vector<uint8_t> before(mem::ptr(lo), mem::ptr(hi));
    Cpu saved = *c;
    orig(c);
    std::vector<uint8_t> expect(mem::ptr(lo), mem::ptr(hi));
    const double expectF1 = c->f[1].ps0;
    const uint32_t expectSp = c->r[1];
    memcpy(mem::ptr(lo), before.data(), before.size());
    *c = saved;
    native(c);
    calls++;
    bool ok = !memcmp(mem::ptr(lo), expect.data(), expect.size()) && c->r[1] == expectSp &&
              (!fpResult || f64_as_u64(c->f[1].ps0) == f64_as_u64(expectF1));
    if (!ok && mismatches++ < 20) LOG("[hook check] %s MISMATCH (call %llu)", name, (unsigned long long)calls);
    if (calls % 20000 == 0) LOG("[hook check] %s: %llu calls, %llu mismatches", name, (unsigned long long)calls,
                                (unsigned long long)mismatches);
}
}

extern "C" void hook_0281B4EC(Cpu* c) {
    if (g_check) return checked(c, f_0281B4EC_orig, filter8, "filter8 (0281B4EC)", c->r[4], 2 * (80 + 8), false);
    filter8(c);
}

extern "C" void hook_0281B970(Cpu* c) {
    if (g_check) return checked(c, f_0281B970_orig, mix_ramp, "mix_ramp (0281B970)", c->r[4], 2 * 80, true);
    mix_ramp(c);
}
