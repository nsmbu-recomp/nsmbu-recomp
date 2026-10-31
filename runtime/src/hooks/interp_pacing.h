
#pragma once
#include <algorithm>
#include <cmath>
#include <cstdint>

namespace interp {
namespace pacing {

inline float lerp_f(float a, float b, float t) { return t == 0.5f ? 0.5f * (a + b) : (1.0f - t) * a + t * b; }

inline int16_t lerp_s16(int16_t a, int16_t b, float t) {
    const int16_t d = (int16_t)(b - a);
    return t == 0.5f ? (int16_t)(a + d / 2) : (int16_t)(a + (int)std::lround(d * t));
}
inline uint8_t lerp_u8(uint8_t a, uint8_t b, float t) {
    return t == 0.5f ? (uint8_t)((a + b + 1) / 2) : (uint8_t)std::lround(a + (b - a) * t);
}

inline void slerp_weights(float cosang, float t, float& wa, float& wb) {
    if (t == 0.5f) {
        wa = wb = 1.0f;
        return;
    }
    const float ang = std::acos(std::min(1.0f, std::max(-1.0f, cosang)));
    if (ang > 1e-4f) {
        const float s = std::sin(ang);
        wa = std::sin((1 - t) * ang) / s;
        wb = std::sin(t * ang) / s;
    } else {
        wa = 1 - t;
        wb = t;
    }
}

inline const int* fps_ladder(int& n) {
    static const int rates[] = {30, 60, 120, 144, 165, 240};
    n = int(sizeof rates / sizeof rates[0]);
    return rates;
}

constexpr int kCustomFpsMin = 60;
constexpr int kCustomFpsMax = 1000;

inline int clamp_custom_fps(int f) {
    if (f < kCustomFpsMin) return kCustomFpsMin;
    if (f > kCustomFpsMax) return kCustomFpsMax;
    return f;
}

inline int valid_fps(int f) {
    if (f <= 0) return 60;
    int n = 0;
    const int* rates = fps_ladder(n);
    int best = 60;
    int best_d = std::abs(best - f);
    for (int i = 0; i < n; i++) {
        const int d = std::abs(rates[i] - f);
        if (d < best_d) {
            best = rates[i];
            best_d = d;
        }
    }
    return best;
}

inline int display_fps(int hz) {
    int n = 0;
    const int* rates = fps_ladder(n);
    int shown = rates[0];
    for (int i = 0; i < n; i++)
        if (rates[i] <= hz + 2) shown = rates[i];
    return shown;
}

inline int cap_fps(int fps, int hz, bool vsync) {
    fps = valid_fps(fps);
    if (!vsync || hz <= 0) return fps;
    return std::min(fps, display_fps(hz));
}

constexpr int64_t kLogicStepNs = 1'000'000'000LL / 60;
constexpr int64_t kMaxDtNs = 100'000'000LL;
constexpr int kMaxLogicStepsPerWake = 4;

inline int64_t clamp_dt_ns(int64_t dt_ns) { return std::clamp(dt_ns, int64_t{0}, kMaxDtNs); }

inline float blend_t(int64_t acc_ns) {
    if (acc_ns <= 0) return 0.f;
    if (acc_ns >= kLogicStepNs) return 1.f;
    return float(double(acc_ns) / double(kLogicStepNs));
}

inline float present_t(int logic_steps, float open_t) { return logic_steps > 0 ? 1.f : open_t; }

inline float present_sample_t(bool logic_step, float hold_t) {
    if (logic_step) return 0.f;
    if (hold_t < 0.f) return 0.f;
    if (hold_t > 1.f) return 1.f;
    return hold_t;
}

struct WakePlan {
    int logic_steps = 0;
    float t = 1.f;
    int64_t acc_ns = 0;
};

inline WakePlan advance_accumulator(int64_t acc_ns, int64_t dt_ns, bool interpolate, int max_steps = kMaxLogicStepsPerWake) {
    WakePlan out;
    out.acc_ns = acc_ns;
    if (!interpolate) {
        out.logic_steps = 1;
        out.t = 1.f;
        out.acc_ns = 0;
        return out;
    }
    out.acc_ns += clamp_dt_ns(dt_ns);
    max_steps = std::max(1, max_steps);
    while (out.acc_ns >= kLogicStepNs && out.logic_steps < max_steps) {
        out.acc_ns -= kLogicStepNs;
        out.logic_steps++;
    }
    out.t = present_t(out.logic_steps, blend_t(out.acc_ns));
    return out;
}

inline int64_t present_period_ns(int present_cap) {
    present_cap = std::max(1, present_cap);
    return 1'000'000'000LL / present_cap;
}

constexpr int kShedResumeFrames = 8;
constexpr int kRefreshResumeFrames = 48;
constexpr int kLoadLatchFrames = 300;

inline bool skip_extra_course_draw(bool hold, bool course_play, int frames_queued) {
    return hold && course_play && frames_queued > 1;
}

inline int logic_step_budget(int frames_queued, int max_steps = kMaxLogicStepsPerWake) {
    if (frames_queued > 0) return 1;
    return std::max(1, max_steps);
}

inline int64_t shed_slot_ns(int chosen_fps) {
    if (chosen_fps <= 60) return 0;
    return present_period_ns(60);
}

struct HoldShed {
    bool shed = false;
    int cheap = 0;
};

struct ShedUpdate {
    bool shed = false;
    bool changed = false;
};

inline ShedUpdate note_logic_work(HoldShed& h, int64_t work_ns, int64_t slot_ns, bool force = false) {
    const bool before = h.shed;
    if (force) {
        h.cheap = 0;
        h.shed = true;
    } else if (slot_ns <= 0) {
        h.shed = false;
        h.cheap = 0;
    } else if (work_ns > slot_ns) {
        h.cheap = 0;
        h.shed = true;
    } else if (h.shed) {
        if (++h.cheap >= kShedResumeFrames) {
            h.shed = false;
            h.cheap = 0;
        }
    } else {
        h.cheap = 0;
    }
    ShedUpdate u;
    u.shed = h.shed;
    u.changed = h.shed != before;
    return u;
}

struct RefreshHold {
    bool high = false;
    int on = 0;
};

struct LoadLatch {
    int left = 0;
};

inline bool note_load_latch(LoadLatch& l, bool trip) {
    if (trip) l.left = kLoadLatchFrames;
    if (l.left <= 0) return false;
    l.left--;
    return true;
}

inline bool note_refresh_hold(RefreshHold& r, bool shed) {
    if (shed) {
        r.on = 0;
        r.high = false;
        return false;
    }
    if (r.high) return true;
    if (++r.on >= kRefreshResumeFrames) r.high = true;
    return r.high;
}

inline int present_granule(int present_cap) {
    present_cap = std::max(1, present_cap);
    if (present_cap <= 60) return 4;
    return std::max(1, (4 * 60 + present_cap / 2) / present_cap);
}

inline int64_t repeat_period_ns(bool course_play, int output_fps) {
    (void)course_play;
    (void)output_fps;
    return 0;
}

inline int64_t bunched_present_delay_ns(bool course_play, int output_fps, int64_t since_ns) {
    (void)course_play;
    (void)output_fps;
    (void)since_ns;
    return 0;
}
inline bool skip_bunched_present(bool course_play, int output_fps, int64_t since_ns) {
    return bunched_present_delay_ns(course_play, output_fps, since_ns) > 0;
}

inline bool collapse_duplicate_vsync(bool presented_since_wake, bool wake_valid, int64_t since_wake_ns) {
    (void)since_wake_ns;
    if (presented_since_wake || !wake_valid) return false;
    return true;
}

inline bool yield_blocked_draw_lock(bool course_setup, int64_t wait_ns) {
    if (course_setup) return true;
    return wait_ns >= 32'000'000;
}

inline int64_t absorb_late_wake(int64_t last_ns, int64_t now_ns, int64_t period_ns) {
    if (period_ns <= 0 || now_ns <= last_ns) return last_ns;
    const int64_t slots = (now_ns - last_ns) / period_ns;
    return last_ns + slots * period_ns;
}

struct SwapAlign {
    int64_t sleep_ns = 0;
    int64_t offset_ns = 0;
};

struct PresentGap {
    int64_t sleep_ns = 0;
    int64_t swap_ns = 0;
};

inline PresentGap pace_present(int64_t now_ns, int64_t last_swap_ns, bool last_valid, int64_t period_ns) {
    PresentGap out;
    out.swap_ns = now_ns;
    if (!last_valid || period_ns <= 0 || now_ns < 0) return out;
    const int64_t target = last_swap_ns + period_ns;
    if (now_ns < target) {
        out.sleep_ns = target - now_ns;
        out.swap_ns = target;
    }
    return out;
}

inline SwapAlign align_swap(int64_t elapsed_ns, int64_t period_ns, int64_t offset_ns) {
    SwapAlign out;
    out.offset_ns = offset_ns;
    if (period_ns <= 0 || elapsed_ns < 0 || elapsed_ns > period_ns / 2) return out;
    int64_t offset_us = offset_ns / 1000;
    const int64_t want_us = elapsed_ns / 1000 + 1800;
    if (want_us > offset_us) offset_us = want_us;
    const int64_t cap_us = (period_ns / 3) / 1000;
    if (offset_us < 1500) offset_us = 1500;
    if (cap_us > 0 && offset_us > cap_us) offset_us = cap_us;
    out.offset_ns = offset_us * 1000;
    const int64_t elapsed_us = elapsed_ns / 1000;
    if (elapsed_us < offset_us) out.sleep_ns = (offset_us - elapsed_us) * 1000;
    return out;
}

}
}
