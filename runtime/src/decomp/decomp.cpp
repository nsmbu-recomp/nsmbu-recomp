#include "decomp/decomp.h"

#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <unordered_set>

#include "core/env.h"
#include "core/runtime.h"

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

}
