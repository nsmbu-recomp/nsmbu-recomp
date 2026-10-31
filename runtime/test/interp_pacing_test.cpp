
#include "hooks/interp_pacing.h"

#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <random>

using namespace interp::pacing;

static int g_failed = 0;
#define CHECK(cond)                                                                \
    do {                                                                           \
        if (!(cond)) {                                                             \
            std::fprintf(stderr, "%s:%d: CHECK failed: %s\n", __FILE__, __LINE__, #cond); \
            g_failed++;                                                            \
        }                                                                          \
    } while (0)
#define CHECK_NEAR(a, b, eps) CHECK(std::fabs(double(a) - double(b)) <= (eps))

static void test_blends() {
    std::mt19937 rng(7);
    std::uniform_real_distribution<float> u(-1000.0f, 1000.0f);
    for (int i = 0; i < 10000; i++) {
        const float a = u(rng), b = u(rng);

        const float h = lerp_f(a, b, 0.5f), o = 0.5f * (a + b);
        CHECK(std::memcmp(&h, &o, 4) == 0);

        for (int k = 0; k <= 8; k++) {
            const float t = k / 8.0f;
            if (t == 0.5f) continue;
            CHECK_NEAR(lerp_f(a, b, t), a + (b - a) * double(t), 1e-3);
        }
        CHECK(lerp_f(a, b, 1.0f) == b);
    }
    for (int a = -32768; a < 32768; a += 997)
        for (int b = -32768; b < 32768; b += 1009) {
            const int16_t d = (int16_t)(b - a);
            CHECK(lerp_s16((int16_t)a, (int16_t)b, 0.5f) == (int16_t)(a + d / 2));
            CHECK(lerp_s16((int16_t)a, (int16_t)b, 1.0f) == (int16_t)b);
        }
    CHECK(lerp_s16(32000, -32000, 0.25f) == (int16_t)(32000 + 384));
    CHECK(lerp_s16(-32000, 32000, 0.75f) == (int16_t)(-32000 - 1152));

    CHECK(lerp_u8(0, 255, 0.5f) == 128);
    CHECK(lerp_u8(0, 255, 0.25f) == 64);
    CHECK(lerp_u8(200, 100, 0.125f) == 188);
    CHECK(lerp_u8(10, 20, 1.0f) == 20);
}

static void test_slerp() {
    float wa, wb;
    slerp_weights(0.3f, 0.5f, wa, wb);
    CHECK(wa == 1.0f && wb == 1.0f);
    for (double ang : {0.01, 0.3, 0.785, 1.5}) {
        const float a[2] = {1, 0}, b[2] = {(float)std::cos(ang), (float)std::sin(ang)};
        for (int k = 1; k < 8; k++) {
            const float t = k / 8.0f;
            slerp_weights(a[0] * b[0] + a[1] * b[1], t, wa, wb);
            const float x = wa * a[0] + wb * b[0], y = wa * a[1] + wb * b[1];
            CHECK_NEAR(std::atan2(y, x), t * ang, 1e-4);
            if (t != 0.5f) CHECK_NEAR(std::hypot(x, y), 1.0, 1e-4);
        }
    }
    slerp_weights(1.0f, 0.25f, wa, wb);
    CHECK_NEAR(wa, 0.75, 1e-6);
    CHECK_NEAR(wb, 0.25, 1e-6);
}

static void test_cap() {
    CHECK(cap_fps(240, 120, true) == 120);
    CHECK(cap_fps(240, 60, true) == 60);
    CHECK(cap_fps(120, 60, true) == 60);
    CHECK(cap_fps(120, 144, true) == 120);
    CHECK(cap_fps(240, 144, true) == 144);
    CHECK(cap_fps(240, 165, true) == 165);
    CHECK(cap_fps(165, 165, true) == 165);
    CHECK(cap_fps(165, 144, true) == 144);
    CHECK(cap_fps(240, 240, true) == 240);
    CHECK(cap_fps(240, 239, true) == 240);
    CHECK(cap_fps(120, 119, true) == 120);
    CHECK(cap_fps(120, 90, true) == 60);
    CHECK(cap_fps(60, 30, true) == 30);
    CHECK(cap_fps(30, 165, true) == 30);
    CHECK(cap_fps(30, 60, true) == 30);
    CHECK(cap_fps(240, 0, true) == 240);
    CHECK(cap_fps(240, 60, false) == 240);
    CHECK(cap_fps(100, 240, true) == 120);
    CHECK(clamp_custom_fps(1) == 60);
    CHECK(clamp_custom_fps(30) == 60);
    CHECK(clamp_custom_fps(59) == 60);
    CHECK(clamp_custom_fps(60) == 60);
    CHECK(clamp_custom_fps(90) == 90);
    CHECK(clamp_custom_fps(1000) == 1000);
    CHECK(clamp_custom_fps(1001) == 1000);
    CHECK(valid_fps(0) == 60);
    CHECK(valid_fps(30) == 30);
    CHECK(valid_fps(44) == 30);
    CHECK(valid_fps(45) == 60);
    CHECK(valid_fps(59) == 60);
    CHECK(valid_fps(100) == 120);
    CHECK(valid_fps(150) == 144);
    CHECK(valid_fps(360) == 240);
    CHECK(present_period_ns(30) == 1'000'000'000LL / 30);
    CHECK(present_period_ns(90) == 1'000'000'000LL / 90);
    CHECK(present_granule(90) == 3);
    CHECK(present_period_ns(120) == 1'000'000'000LL / 120);
    CHECK(present_period_ns(165) == 1'000'000'000LL / 165);
    CHECK(present_granule(30) == 4);
    CHECK(present_granule(60) == 4);
    CHECK(present_granule(120) == 2);
    CHECK(present_granule(240) == 1);
    CHECK(repeat_period_ns(false, 165) == 0);
    CHECK(repeat_period_ns(true, 165) == 0);
    CHECK(!skip_bunched_present(false, 165, 1000000));
    CHECK(!skip_bunched_present(false, 165, 5000000));
    CHECK(bunched_present_delay_ns(false, 165, 1000000) == 0);
    CHECK(collapse_duplicate_vsync(false, true, 1'000'000));
    CHECK(collapse_duplicate_vsync(false, true, 5'000'000));
    CHECK(!collapse_duplicate_vsync(true, true, 500'000));
    CHECK(!collapse_duplicate_vsync(false, false, 5'000'000));
    const int64_t period = present_period_ns(165);
    CHECK(absorb_late_wake(0, period / 2, period) == 0);
    CHECK(absorb_late_wake(0, period + period / 3, period) == period);
    CHECK(absorb_late_wake(0, period * 3 + 100, period) == period * 3);
    CHECK(yield_blocked_draw_lock(true, 0));
    CHECK(!yield_blocked_draw_lock(false, 0));
    CHECK(!yield_blocked_draw_lock(false, 8'000'000));
    CHECK(yield_blocked_draw_lock(false, 32'000'000));
    int64_t last = 0;
    for (int i = 0; i < 165; i++) {
        const int64_t now = last + period + period / 10;
        last = absorb_late_wake(last, now, period);
        CHECK(last % period == 0);
    }
    CHECK(last == period * 165);
}

static void test_accumulator() {
    CHECK(clamp_dt_ns(-1) == 0);
    CHECK(clamp_dt_ns(kMaxDtNs + 1) == kMaxDtNs);
    CHECK(clamp_dt_ns(kLogicStepNs) == kLogicStepNs);

    WakePlan off = advance_accumulator(0, kLogicStepNs * 3, false);
    CHECK(off.logic_steps == 1);
    CHECK(off.t == 1.f);
    CHECK(off.acc_ns == 0);

    WakePlan hold = advance_accumulator(0, kLogicStepNs / 4, true);
    CHECK(hold.logic_steps == 0);
    CHECK_NEAR(hold.t, 0.25, 1e-6);
    CHECK(hold.acc_ns == kLogicStepNs / 4);

    WakePlan one = advance_accumulator(hold.acc_ns, kLogicStepNs, true);
    CHECK(one.logic_steps == 1);
    CHECK(one.t == 1.f);
    CHECK_NEAR(blend_t(one.acc_ns), 0.25, 1e-6);

    WakePlan hitch = advance_accumulator(0, 200'000'000LL, true);
    CHECK(hitch.logic_steps == kMaxLogicStepsPerWake);
    CHECK(hitch.t == 1.f);
    CHECK(hitch.acc_ns == kMaxDtNs - kMaxLogicStepsPerWake * kLogicStepNs);

    WakePlan multi = advance_accumulator(0, kLogicStepNs * 2 + kLogicStepNs / 3, true);
    CHECK(multi.logic_steps == 2);
    CHECK(multi.t == 1.f);
    CHECK_NEAR(blend_t(multi.acc_ns), 1.0 / 3.0, 1e-6);

    CHECK(present_t(0, 0.4f) == 0.4f);
    CHECK(present_t(1, 0.4f) == 1.f);

    int64_t acc = 0;
    float prev_t = -1.f;
    int logics = 0, holds = 0;
    const int64_t present = present_period_ns(120);
    for (int i = 0; i < 120; i++) {
        WakePlan p = advance_accumulator(acc, present, true);
        acc = p.acc_ns;
        if (p.logic_steps) {
            logics += p.logic_steps;
            CHECK(p.t == 1.f);
            prev_t = -1.f;
        } else {
            holds++;
            CHECK(p.t + 1e-6f >= prev_t);
            prev_t = p.t;
        }
        CHECK(p.t >= 0.f);
        CHECK(p.t <= 1.f);
    }
    CHECK_NEAR(logics, 60, 2);
    CHECK(holds > 40);

    acc = 0;
    for (int i = 0; i < 165; i++) {
        WakePlan p = advance_accumulator(acc, present_period_ns(165), true);
        acc = p.acc_ns;
        if (p.logic_steps) CHECK(p.t == 1.f);
        else CHECK(p.t < 1.f);
    }
}

static void test_present_gap() {
    const int64_t period = present_period_ns(165);
    int64_t wake = 0, last = 0, prev = 0;
    bool valid = false;
    int steady = 0;
    for (int i = 0; i < 40; i++) {
        const int64_t work = (i % 3 == 2) ? 4'000'000 : 400'000;
        const PresentGap gap = pace_present(wake + work, last, valid, period);
        valid = true;
        if (i > 2) {
            CHECK(gap.swap_ns - prev == period);
            steady++;
        }
        prev = gap.swap_ns;
        last = gap.swap_ns;
        const int64_t target = wake + period;
        wake = gap.swap_ns < target ? target : gap.swap_ns;
    }
    CHECK(steady > 20);

    wake = last = prev = 0;
    valid = false;
    for (int i = 0; i < 24; i++) {
        const int64_t work = (i % 3 == 2) ? 8'000'000 : 400'000;
        const PresentGap gap = pace_present(wake + work, last, valid, period);
        valid = true;
        if (i > 2) CHECK(gap.swap_ns - prev >= period);
        prev = gap.swap_ns;
        last = gap.swap_ns;
        const int64_t target = wake + period;
        wake = gap.swap_ns < target ? target : gap.swap_ns;
    }

    const PresentGap first = pace_present(1000, 0, false, period);
    CHECK(first.sleep_ns == 0);
    CHECK(first.swap_ns == 1000);
    const PresentGap early = pace_present(1'000'000, 0, true, period);
    CHECK(early.swap_ns == period);
    CHECK(early.sleep_ns == period - 1'000'000);
    const PresentGap behind = pace_present(period + 2'000'000, 0, true, period);
    CHECK(behind.sleep_ns == 0);
    CHECK(behind.swap_ns == period + 2'000'000);
}

static void test_swap_align() {
    const int64_t p165 = present_period_ns(165);
    const int64_t offset = 2'500'000;
    SwapAlign hold = align_swap(400'000, p165, offset);
    SwapAlign logic = align_swap(4'000'000, p165, offset);
    CHECK(logic.sleep_ns == 0);
    CHECK(hold.sleep_ns == 1'620'000);

    const int64_t p60 = present_period_ns(60);
    SwapAlign heavy = align_swap(p60 / 2 + 1, p60, offset);
    CHECK(heavy.sleep_ns == 0);
    CHECK(heavy.offset_ns == offset);
    SwapAlign light = align_swap(1'000'000, p60, offset);
    CHECK(light.offset_ns == 2'800'000);
    CHECK(light.sleep_ns == 1'800'000);
}

static void test_hold_shed() {
    CHECK(kRefreshResumeFrames > kShedResumeFrames);
    CHECK(shed_slot_ns(30) == 0);
    CHECK(shed_slot_ns(60) == 0);
    CHECK(shed_slot_ns(120) == present_period_ns(60));
    CHECK(shed_slot_ns(240) == present_period_ns(60));
    CHECK(present_period_ns(30) > 20'000'000LL);

    const int64_t slot30 = present_period_ns(30);
    HoldShed at30;
    CHECK(!note_logic_work(at30, 20'000'000LL, slot30).shed);
    CHECK(!note_logic_work(at30, 20'000'000LL, shed_slot_ns(30)).shed);

    const int64_t slot = shed_slot_ns(120);
    const int64_t slow = slot + 1;
    HoldShed h;
    ShedUpdate first = note_logic_work(h, slow, slot);
    CHECK(first.shed && first.changed);
    CHECK(!note_logic_work(h, slow, slot).changed);
    for (int i = 0; i < kShedResumeFrames - 1; i++)
        CHECK(note_logic_work(h, slot, slot).shed);
    ShedUpdate resumed = note_logic_work(h, slot, slot);
    CHECK(!resumed.shed && resumed.changed);
    CHECK(!note_logic_work(h, slot, slot).shed);

    CHECK(note_logic_work(h, slow, slot).shed);
    for (int i = 0; i < 4; i++) CHECK(note_logic_work(h, slot, slot).shed);
    CHECK(note_logic_work(h, slow, slot).shed);
    CHECK(h.cheap == 0);
    for (int i = 0; i < kShedResumeFrames - 1; i++)
        CHECK(note_logic_work(h, slot, slot).shed);
    CHECK(!note_logic_work(h, slot, slot).shed);

    CHECK(note_logic_work(h, slow, slot).shed);
    ShedUpdate cleared = note_logic_work(h, slow, 0);
    CHECK(!cleared.shed && cleared.changed);
    CHECK(h.cheap == 0);

    RefreshHold r;
    CHECK(!note_refresh_hold(r, true));
    CHECK(!r.high);
    for (int i = 0; i < kRefreshResumeFrames - 1; i++)
        CHECK(!note_refresh_hold(r, false));
    CHECK(note_refresh_hold(r, false));
    CHECK(r.high);
    CHECK(note_refresh_hold(r, false));
    CHECK(!note_refresh_hold(r, true));
    CHECK(!r.high && r.on == 0);
    for (int i = 0; i < kShedResumeFrames; i++)
        CHECK(!note_refresh_hold(r, false));
    CHECK(!r.high);

    HoldShed forced;
    CHECK(note_logic_work(forced, 0, 0, true).shed);
    CHECK(note_logic_work(forced, slot, 0, true).shed);
    CHECK(!note_logic_work(forced, slot, 0).shed);

    LoadLatch latch;
    CHECK(note_load_latch(latch, true));
    for (int i = 0; i < kLoadLatchFrames - 2; i++)
        CHECK(note_load_latch(latch, false));
    CHECK(note_load_latch(latch, false));
    CHECK(!note_load_latch(latch, false));
    CHECK(note_load_latch(latch, true));
    CHECK(note_load_latch(latch, true));
    CHECK(latch.left == kLoadLatchFrames - 1);

    CHECK(!skip_extra_course_draw(false, true, 2));
    CHECK(!skip_extra_course_draw(true, false, 2));
    CHECK(!skip_extra_course_draw(true, true, 0));
    CHECK(!skip_extra_course_draw(true, true, 1));
    CHECK(skip_extra_course_draw(true, true, 2));
    CHECK(!skip_settled_map_draw(false, true, false));
    CHECK(!skip_settled_map_draw(true, false, false));
    CHECK(!skip_settled_map_draw(true, true, true));
    CHECK(skip_settled_map_draw(true, true, false));
    CHECK(logic_step_budget(0) == kMaxLogicStepsPerWake);
    CHECK(logic_step_budget(1) == 1);
    CHECK(logic_step_budget(4, 2) == 1);
}

static void test_present_sample_order() {
    CHECK(present_sample_t(true, 1.f) == 1.f);
    CHECK(present_sample_t(false, 0.5f) == 0.5f);
    CHECK(present_sample_t(false, -1.f) == 0.f);
    CHECK(present_sample_t(false, 2.f) == 1.f);
    float shown = 0.f;
    for (int step = 1; step <= 6; step++) {
        const float next = float(step);
        const float hold = lerp_f(shown, next, present_sample_t(false, 0.5f));
        CHECK(hold + 1e-4f >= shown);
        CHECK(hold <= next + 1e-4f);
        shown = next;
        CHECK(shown + 1e-4f >= hold);
    }
    CHECK_NEAR(shown, 6.f, 1e-3);
}

static void test_camera_hold_stays_ahead() {
    float prev = 0.f, cur = 0.f, shown = 0.f;
    bool valid = false, cur_valid = false;
    uint64_t stored = 0, steps = 0;
    for (int i = 0; i < 8; i++) {
        const bool hold = i % 2 == 1;
        const float live = float(steps);
        switch (camera_shift(valid, cur_valid, stored, steps, false)) {
        case CameraShift::Same:
            cur = live;
            break;
        case CameraShift::Adjacent:
            prev = cur;
            cur = live;
            stored = steps;
            valid = cur_valid = true;
            break;
        case CameraShift::Snap:
            prev = cur = live;
            stored = steps;
            valid = cur_valid = true;
            break;
        }
        const float drawn = lerp_f(prev, cur, present_sample_t(!hold, 0.5f));
        if (i) CHECK(drawn + 1e-4f >= shown);
        shown = drawn;
        if (!hold) steps++;
    }
    CHECK_NEAR(shown, 3.5f, 1e-3);
}

static void test_present_spacing() {
    const int caps[] = {30, 60, 120, 144, 165, 240};
    for (int cap : caps) {
        const int64_t period = present_period_ns(cap);
        CHECK(period > 0);
        CHECK_NEAR(1e9 / double(period), double(cap), 0.51);
        int64_t acc = 0;
        int logics = 0;
        const double seconds = 2.0;
        const int frames = int(cap * seconds + 0.5);
        for (int i = 0; i < frames; i++) {
            WakePlan p = advance_accumulator(acc, period, cap != 60);
            acc = p.acc_ns;
            logics += p.logic_steps;
            if (cap < 60 && p.logic_steps) CHECK(p.t == 1.f);
        }
        if (cap == 60) CHECK(logics == frames);
        else CHECK_NEAR(logics / seconds, 60.0, 1.0);
    }
}

static void test_blend_off_paces_at_60() {
    CHECK(pace_fps(120, true, false) == 120);
    CHECK(pace_fps(30, false, false) == 30);
    CHECK(pace_fps(120, false, false) == 60);
    CHECK(pace_fps(240, false, true) == 240);
    CHECK(wants_holds(false, false, true, false, true, 120));
    CHECK(wants_holds(false, false, false, false, true, 30));
    CHECK(!wants_holds(false, false, false, false, true, pace_fps(120, false, false)));
    CHECK(!wants_holds(false, false, false, true, true, 60));
    CHECK(wants_holds(false, false, true, true, true, 60));
    CHECK(!wants_holds(true, false, true, false, true, 120));
}

int main() {
    test_blends();
    test_blend_off_paces_at_60();
    test_slerp();
    test_cap();
    test_accumulator();
    test_present_gap();
    test_swap_align();
    test_hold_shed();
    test_present_sample_order();
    test_camera_hold_stays_ahead();
    test_present_spacing();
    if (g_failed) {
        std::fprintf(stderr, "interp_pacing_test: %d check(s) failed\n", g_failed);
        return 1;
    }
    std::printf("interp_pacing_test: all checks passed\n");
    return 0;
}
