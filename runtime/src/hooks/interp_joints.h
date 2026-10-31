#pragma once
#include <cstdint>
#include <cstring>

#include "hooks/interp_pacing.h"

namespace interp::joints {

constexpr uint32_t kWords = 12;
constexpr float kCutDist = 400.f;

inline bool stale(bool hidden, const uint32_t* rec, const uint32_t* before) {
    return hidden && (!before || std::memcmp(rec, before, 4 * kWords) == 0);
}

inline float word_f(uint32_t w) {
    w = __builtin_bswap32(w);
    float f;
    std::memcpy(&f, &w, 4);
    return f;
}
inline uint32_t f_word(float f) {
    uint32_t w;
    std::memcpy(&w, &f, 4);
    return __builtin_bswap32(w);
}

struct Result {
    bool cut = false;
    uint32_t cut_joint = 0;
    uint32_t shown = 0;
};

template <class Blend>
Result blend(const uint32_t* prev, const uint32_t* cur, const uint8_t* stale_flags, uint32_t n, float cut_dist, float t,
             uint32_t* out, Blend&& blend_mtx) {
    Result r;
    for (uint32_t m = 0; m < n; m++) {
        const uint32_t* pa = prev + kWords * m;
        const uint32_t* pb = cur + kWords * m;
        uint32_t* po = out + kWords * m;
        const bool moved = std::memcmp(pa, pb, 4 * kWords) != 0;
        if (!moved || (stale_flags && stale_flags[m])) {
            if (moved) r.shown++;
            std::memcpy(po, pb, 4 * kWords);
            continue;
        }
        float a[kWords], b[kWords], o[kWords];
        for (uint32_t k = 0; k < kWords; k++) {
            a[k] = word_f(pa[k]);
            b[k] = word_f(pb[k]);
        }
        const float dx = a[3] - b[3], dy = a[7] - b[7], dz = a[11] - b[11];
        if (!(dx * dx + dy * dy + dz * dz <= cut_dist * cut_dist)) {
            r.cut = true;
            r.cut_joint = m;
            return r;
        }
        blend_mtx(a, b, o, t);
        for (uint32_t k = 0; k < kWords; k++) po[k] = f_word(o[k]);
    }
    return r;
}

inline void blend_mtx(const float* a, const float* b, float* out, float t) {
    for (int k = 0; k < 12; k++) out[k] = pacing::lerp_f(a[k], b[k], t);
}

}
