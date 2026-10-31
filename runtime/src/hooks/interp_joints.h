#pragma once
#include <cmath>
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

inline bool to_quat(const float* m, float* s, float* q) {
    float r[3][3];
    for (int j = 0; j < 3; j++) {
        s[j] = std::sqrt(m[j] * m[j] + m[4 + j] * m[4 + j] + m[8 + j] * m[8 + j]);
        if (s[j] < 1e-6f) return false;
        for (int i = 0; i < 3; i++) r[i][j] = m[4 * i + j] / s[j];
    }
    for (int a = 0; a < 3; a++)
        for (int b = a + 1; b < 3; b++)
            if (std::fabs(r[0][a] * r[0][b] + r[1][a] * r[1][b] + r[2][a] * r[2][b]) > 0.02f) return false;
    float det = r[0][0] * (r[1][1] * r[2][2] - r[1][2] * r[2][1]) - r[0][1] * (r[1][0] * r[2][2] - r[1][2] * r[2][0]) +
                r[0][2] * (r[1][0] * r[2][1] - r[1][1] * r[2][0]);
    if (det < 0.9f) return false;
    float tr = r[0][0] + r[1][1] + r[2][2];
    if (tr > 0) {
        float k = 0.5f / std::sqrt(tr + 1.0f);
        q[0] = 0.25f / k;
        q[1] = (r[2][1] - r[1][2]) * k;
        q[2] = (r[0][2] - r[2][0]) * k;
        q[3] = (r[1][0] - r[0][1]) * k;
    } else if (r[0][0] > r[1][1] && r[0][0] > r[2][2]) {
        float k = 2.0f * std::sqrt(1.0f + r[0][0] - r[1][1] - r[2][2]);
        q[0] = (r[2][1] - r[1][2]) / k;
        q[1] = 0.25f * k;
        q[2] = (r[0][1] + r[1][0]) / k;
        q[3] = (r[0][2] + r[2][0]) / k;
    } else if (r[1][1] > r[2][2]) {
        float k = 2.0f * std::sqrt(1.0f + r[1][1] - r[0][0] - r[2][2]);
        q[0] = (r[0][2] - r[2][0]) / k;
        q[1] = (r[0][1] + r[1][0]) / k;
        q[2] = 0.25f * k;
        q[3] = (r[1][2] + r[2][1]) / k;
    } else {
        float k = 2.0f * std::sqrt(1.0f + r[2][2] - r[0][0] - r[1][1]);
        q[0] = (r[1][0] - r[0][1]) / k;
        q[1] = (r[0][2] + r[2][0]) / k;
        q[2] = (r[1][2] + r[2][1]) / k;
        q[3] = 0.25f * k;
    }
    return true;
}

inline void blend_mtx(const float* a, const float* b, float* out, float t) {
    float sa[3], sb[3], qa[4], qb[4];
    for (int i = 0; i < 3; i++) out[4 * i + 3] = pacing::lerp_f(a[4 * i + 3], b[4 * i + 3], t);
    if (!to_quat(a, sa, qa) || !to_quat(b, sb, qb)) {
        for (int i = 0; i < 3; i++)
            for (int j = 0; j < 3; j++) out[4 * i + j] = pacing::lerp_f(a[4 * i + j], b[4 * i + j], t);
        return;
    }
    float d = qa[0] * qb[0] + qa[1] * qb[1] + qa[2] * qb[2] + qa[3] * qb[3];
    float sg = d < 0 ? -1.0f : 1.0f, q[4], n = 0;
    float wa, wb;
    pacing::slerp_weights(std::fabs(d), t, wa, wb);
    wb *= sg;
    for (int k = 0; k < 4; k++) {
        q[k] = wa * qa[k] + wb * qb[k];
        n += q[k] * q[k];
    }
    n = 1.0f / std::sqrt(n);
    float w = q[0] * n, x = q[1] * n, y = q[2] * n, z = q[3] * n;
    const float r[3][3] = {{1 - 2 * (y * y + z * z), 2 * (x * y - w * z), 2 * (x * z + w * y)},
                           {2 * (x * y + w * z), 1 - 2 * (x * x + z * z), 2 * (y * z - w * x)},
                           {2 * (x * z - w * y), 2 * (y * z + w * x), 1 - 2 * (x * x + y * y)}};
    for (int j = 0; j < 3; j++) {
        float s = pacing::lerp_f(sa[j], sb[j], t);
        for (int i = 0; i < 3; i++) out[4 * i + j] = r[i][j] * s;
    }
}

}
