#include "decomp/decomp.h"

#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <unordered_set>

#include "core/env.h"
#include "core/runtime.h"
#include "decomp_guest.h"
#include "ppc.h"

extern "C" {
extern const uint32_t g_recomp_decomp_kept[];
extern const unsigned g_recomp_decomp_kept_count;
}

namespace decomp {
namespace {

void add_addrs(const char* text, std::unordered_set<uint32_t>& out) {
    if (!text) return;
    while (*text) {
        while (*text == ',' || *text == ' ' || *text == '\t') text++;
        if (!*text) break;
        char* end = nullptr;
        unsigned long value = std::strtoul(text, &end, 16);
        if (end == text) break;
        out.insert((uint32_t)value);
        text = end;
    }
}

}

void init() {
    const char* mode = env::DECOMP();
    const bool none = mode && *mode && std::strcmp(mode, "all") != 0;
    std::unordered_set<uint32_t> only, exclude, kept;
    const char* funcs = env::DECOMP_FUNCS();
    const bool listed = funcs && *funcs;
    add_addrs(funcs, only);
    add_addrs(env::DECOMP_EXCLUDE(), exclude);
    for (unsigned i = 0; i < g_recomp_decomp_kept_count; i++) kept.insert(g_recomp_decomp_kept[i]);
    unsigned enabled = 0;
    for (unsigned i = 0; i < g_slot_count; i++) {
        Slot& slot = g_slots[i];
        bool on = !none && (!listed || only.count(slot.addr));
        if (exclude.count(slot.addr) || kept.count(slot.addr)) on = false;
        slot.on = on ? 1 : 0;
        enabled += on;
    }
    LOG("[decomp] %u functions, %u on, %u kept", g_slot_count, enabled, g_recomp_decomp_kept_count);
}

namespace {
thread_local Cpu* g_cpu;
}

void bind(Cpu* cpu) { g_cpu = cpu; }

CallFrame call_begin() {
    CallFrame frame{};
    if (!g_cpu) return frame;
    frame.lr = g_cpu->lr;
    frame.ctr = g_cpu->ctr;
    frame.sp = g_cpu->r[1];
    frame.live = true;
    g_cpu->r[1] -= 0x40;
    st32(g_cpu->r[1], frame.sp);
    return frame;
}

void arg_u32(int slot, uint32_t value) {
    if (g_cpu && slot >= 0 && slot < 8) g_cpu->r[3 + slot] = value;
}

void arg_f32(int slot, float value) {
    if (!g_cpu || slot < 0 || slot >= 8) return;
    g_cpu->f[1 + slot].ps0 = value;
    g_cpu->f[1 + slot].ps1 = value;
}

static void finish(CallFrame frame, uint32_t fn) {
    if (!frame.live || !g_cpu) return;
    g_cpu->lr = 0;
    g_cpu->pc = fn;
    ppc_dispatch(g_cpu);
    g_cpu->r[1] = frame.sp;
    g_cpu->lr = frame.lr;
    g_cpu->ctr = frame.ctr;
}

uint32_t call_end(CallFrame frame, uint32_t fn) {
    finish(frame, fn);
    return g_cpu ? g_cpu->r[3] : 0;
}

float call_end_f32(CallFrame frame, uint32_t fn) {
    finish(frame, fn);
    return g_cpu ? static_cast<float>(g_cpu->f[1].ps0) : 0;
}

}
