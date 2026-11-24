
#include "input/rumble.h"
#include "input/input.h"
#include "core/runtime.h"
#include <algorithm>
#include <atomic>
#include <chrono>
#include <cstdlib>
#include <cstring>
#include <mutex>

#include "core/env.h"
namespace rumble {

void Motor::gamepad_pattern(const uint8_t* bits, uint32_t nbits, uint64_t now) {
    nbits_ = bits ? std::min(nbits, kMaxBits) : 0;
    memset(pattern_, 0, sizeof pattern_);
    if (nbits_) memcpy(pattern_, bits, (nbits_ + 7) / 8);
    start_ = now;
}

void Motor::gamepad_stop() { nbits_ = 0; }

void Motor::pro_motor(bool on, uint64_t now) {
    pro_on_ = on;
    pro_time_ = now;
}

void Motor::reset() {
    nbits_ = 0;
    pro_on_ = false;
}

float Motor::level(uint64_t now, uint64_t window) const {
    if (pro_on_ && now - pro_time_ < kProStaleUs) return 1.0f;
    if (!nbits_ || now < start_) return 0.0f;

    const uint64_t t = now - start_;
    const uint64_t first = t / kBitUs, last = std::max(first + 1, (t + window + kBitUs - 1) / kBitUs);
    if (first >= nbits_) return 0.0f;
    uint32_t on = 0;
    for (uint64_t i = first; i < last && i < nbits_; i++) on += (pattern_[i >> 3] >> (i & 7)) & 1;
    return float(on) / float(last - first);
}

bool Motor::active(uint64_t now) const {
    return (pro_on_ && now - pro_time_ < kProStaleUs) || (nbits_ && now - start_ < uint64_t(nbits_) * kBitUs);
}

namespace {
std::mutex g_mu;
Motor g_vpad;
Motor g_pro[input::kMaxPlayers];

uint64_t now_us() {
    return std::chrono::duration_cast<std::chrono::microseconds>(std::chrono::steady_clock::now().time_since_epoch()).count();
}

const char* env() { return env::RUMBLE(); }
std::atomic<bool> g_enabled{!env() || atoi(env()) != 0};
}

bool log_enabled() {
    static const bool on = env::LOG_RUMBLE() || env::RUMBLE_LOG();
    return on;
}

void gamepad_pattern(uint32_t chan, const uint8_t* bits, uint32_t nbits) {
    if (chan != 0) return;
    if (log_enabled()) {
        char hex[2 * Motor::kMaxBits / 8 + 1] = {};
        for (uint32_t i = 0; bits && i < (std::min(nbits, Motor::kMaxBits) + 7) / 8; i++) snprintf(hex + 2 * i, 3, "%02x", bits[i]);
        LOG("[rumble] %.3f game: GamePad pattern of %u bits (%s), %.0f ms", now_us() / 1e6, nbits, hex, std::min(nbits, Motor::kMaxBits) * Motor::kBitUs / 1000.0);
    }
    std::lock_guard lk(g_mu);
    g_vpad.gamepad_pattern(bits, nbits, now_us());
}

void gamepad_stop(uint32_t chan) {
    if (chan != 0) return;
    if (log_enabled()) LOG("[rumble] %.3f game: GamePad motor stop", now_us() / 1e6);
    std::lock_guard lk(g_mu);
    g_vpad.gamepad_stop();
}

void pro_motor(uint32_t chan, bool on) {
    if (chan >= input::kMaxPlayers) return;
    const uint64_t now = now_us();
    std::lock_guard lk(g_mu);

    static bool logged[input::kMaxPlayers] = {};
    if (log_enabled() && on != logged[chan])
        LOG("[rumble] %.3f game: controller %u motor %s", now / 1e6, chan + 1, on ? "on" : "off");
    logged[chan] = on;
    g_pro[chan].pro_motor(on, now);
}

void reset() {
    if (log_enabled()) LOG("[rumble] save state loaded: motors still");
    std::lock_guard lk(g_mu);
    g_vpad.reset();
    for (auto& m : g_pro) m.reset();
}

bool enabled() { return g_enabled.load(std::memory_order_relaxed); }
void set_enabled(bool on) {
    if (g_enabled.exchange(on) != on) LOG("[rumble] controller rumble %s", on ? "on" : "off");
}
bool env_override() { return env() != nullptr; }

float host_level(uint64_t window_us) {
    if (!enabled()) return 0.0f;
    std::lock_guard lk(g_mu);
    const uint64_t now = now_us();
    float level = g_vpad.level(now, window_us);
    for (auto& m : g_pro) level = std::max(level, m.level(now, window_us));
    return level;
}

float host_level(int player, uint64_t window_us) {
    if (!enabled()) return 0.0f;
    if (player < 0 || player >= input::kMaxPlayers) return 0.0f;
    std::lock_guard lk(g_mu);
    const uint64_t now = now_us();
    float level = g_pro[player].level(now, window_us);
    if (player == 0) level = std::max(level, g_vpad.level(now, window_us));
    return level;
}

}
