#include "lua_press.h"

#include <algorithm>
#include <mutex>
#include <vector>

namespace mods::lua_press {
namespace {

struct Entry {
    const void* owner;
    uint64_t until;
    uint32_t mask;
};

std::mutex g_mutex;
std::vector<Entry> g_entries;

}

void press(const void* owner, uint32_t mask, uint64_t steps, uint64_t now_step) {
    if (!mask || !owner || now_step >= UINT64_MAX - steps) return;
    std::lock_guard guard(g_mutex);
    g_entries.push_back({owner, now_step + steps, mask});
}

void drop(const void* owner) {
    std::lock_guard guard(g_mutex);
    g_entries.erase(std::remove_if(g_entries.begin(), g_entries.end(),
                                   [owner](const Entry& e) { return e.owner == owner; }),
                    g_entries.end());
}

uint32_t active(uint64_t step) {
    std::lock_guard guard(g_mutex);
    g_entries.erase(std::remove_if(g_entries.begin(), g_entries.end(),
                                   [step](const Entry& e) { return e.until < step; }),
                    g_entries.end());
    uint32_t mask = 0;
    for (const Entry& e : g_entries) mask |= e.mask;
    return mask;
}

}
