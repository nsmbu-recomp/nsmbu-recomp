#include "hooks/cheats.h"

#include "core/runtime.h"
#include "core/write_watch.h"
#include "guest_addr.h"

#include <atomic>

namespace cheats {
namespace {

std::atomic<bool> g_enabled{true};
std::atomic<bool> g_lives{false};
std::atomic<bool> g_star{false};
std::atomic<int> g_power_req{-1};
std::atomic<int> g_power{-1};
bool g_star_held = false;

const uint32_t kCourse = GD(0x101D1598);
const uint32_t kField = GD(0x101D1604);
const uint32_t kPlayerMgr = GD(0x101E6994);
const uint32_t kSetPlayerMode = GC(0x02924E98);

constexpr uint32_t kPlayerData = 0x24u;
constexpr uint32_t kPlayerDataSize = 0x48u;
constexpr uint32_t kLife = 0x4u;
constexpr uint32_t kMode = 0x500u;
constexpr uint32_t kModel = 0x275Cu;
constexpr uint32_t kStarTimer = 0x277Cu;
constexpr int kLives = 99;
constexpr int kStarKeep = 180;

uint32_t player_actor(int player) {
    const uint32_t mgr = ld32(kPlayerMgr);
    const uint32_t slot = 0x20u + (uint32_t)player * 4u;
    if (!mgr || !wwatch::readable(mgr + slot, 4)) return 0;
    const uint32_t actor = ld32(mgr + slot);
    if (!actor || !wwatch::readable(actor + kStarTimer, 4)) return 0;
    return actor;
}

void keep_lives(uint32_t field) {
    if (!field) return;
    for (int player = 0; player < 4; player++) {
        const uint32_t slot = field + kPlayerData + (uint32_t)player * kPlayerDataSize;
        if (!wwatch::readable(slot, kPlayerDataSize) || ld8(slot) == 0) continue;
        if ((int32_t)ld32(slot + kLife) < kLives) st32(slot + kLife, (uint32_t)kLives);
    }
}

void keep_star(bool on, bool course) {
    if (!course) {
        if (!on) g_star_held = false;
        return;
    }
    if (!on && !g_star_held) return;
    for (int player = 0; player < 4; player++) {
        const uint32_t actor = player_actor(player);
        if (!actor) continue;
        if (on) {
            if ((int32_t)ld32(actor + kStarTimer) < kStarKeep) st32(actor + kStarTimer, (uint32_t)kStarKeep);
        } else {
            st32(actor + kStarTimer, 0);
        }
    }
    g_star_held = on;
}

void give_power(Cpu* cpu, bool course) {
    const int want = g_power_req.load(std::memory_order_relaxed);
    if (!course) return;
    const uint32_t actor = player_actor(0);
    if (!actor) return;
    const int mode = (int32_t)ld32(actor + kMode);
    if (mode >= 0 && mode <= 8) g_power.store(mode, std::memory_order_relaxed);
    if (want < 0 || want > 8) return;
    if (mode == want) {
        int expected = want;
        g_power_req.compare_exchange_strong(expected, -1, std::memory_order_relaxed);
        return;
    }
    if (!ld32(actor + kModel)) return;
    guest_call(cpu, kSetPlayerMode, {actor, (uint32_t)want, 0});
    int expected = want;
    if (g_power_req.compare_exchange_strong(expected, -1, std::memory_order_relaxed))
        g_power.store(want, std::memory_order_relaxed);
}

}

bool enabled() { return g_enabled.load(std::memory_order_relaxed); }
void set_enabled(bool on) { g_enabled.store(on, std::memory_order_relaxed); }
bool infinite_lives() { return g_lives.load(std::memory_order_relaxed); }
void set_infinite_lives(bool on) { g_lives.store(on, std::memory_order_relaxed); }
bool invincible() { return g_star.load(std::memory_order_relaxed); }
void set_invincible(bool on) { g_star.store(on, std::memory_order_relaxed); }
void request_power(int mode) {
    if (mode < 0 || mode > 8) return;
    g_power_req.store(mode, std::memory_order_relaxed);
}
int pending_power() { return g_power_req.load(std::memory_order_relaxed); }
int power() { return g_power.load(std::memory_order_relaxed); }

void before_logic(Cpu* cpu) {
    if (!cpu || !g_enabled.load(std::memory_order_relaxed)) return;
    uint32_t gpr[13];
    for (int i = 0; i < 13; i++) gpr[i] = cpu->r[i];
    const bool course = ld32(kCourse) != 0;
    if (g_lives.load(std::memory_order_relaxed)) keep_lives(ld32(kField));
    keep_star(g_star.load(std::memory_order_relaxed), course);
    give_power(cpu, course);
    for (int i = 0; i < 13; i++) cpu->r[i] = gpr[i];
}

}
