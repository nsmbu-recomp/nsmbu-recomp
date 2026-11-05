#pragma once
#include <algorithm>
#include <cstdint>

namespace gfxvk {
// When a shader or pipeline compile starts, wait one display period before repeating the last
// finished picture. A short compile then adds no extra present. Later repeats in that same frame
// stay on the display period.
struct ShaderPresentGate {
    uint64_t frame = ~uint64_t{0};
    int64_t nextNs = 0;
    static int64_t period_or(int64_t periodNs) {
        return periodNs < 1000000 ? 16666667 : periodNs;
    }
    bool due(uint64_t frameNow, int64_t nowNs, int64_t periodNs) {
        periodNs = period_or(periodNs);
        if (frame != frameNow) {
            frame = frameNow;
            nextNs = std::max(nextNs, nowNs + periodNs);
        }
        return nowNs >= nextNs;
    }
    void presented(int64_t nowNs, int64_t periodNs) { nextNs = nowNs + period_or(periodNs); }
    void missed(int64_t nowNs) { nextNs = nowNs + 2000000; }
};
}
