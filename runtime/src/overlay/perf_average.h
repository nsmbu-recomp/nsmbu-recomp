#pragma once
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstring>

namespace overlay {

struct FrameHistory {
    struct Sample {
        double t = 0;
        float ms = 0;
        uint32_t logic = 0;
    };
    static constexpr int kGraphPoints = 120;
    static constexpr int kCap = 512;
    static constexpr double kWindowSec = 2.0;

    Sample ring[kCap]{};
    int start = 0;
    int count = 0;
    bool undo_ready = false;
    Sample undo{};
    float line[kGraphPoints]{};
    int line_i = 0;

    static double bucket_sec() { return kWindowSec / kGraphPoints; }

    void reset() {
        start = count = 0;
        line_i = 0;
        undo_ready = false;
        std::memset(ring, 0, sizeof ring);
        std::memset(line, 0, sizeof line);
    }

    void prune(double now) {
        const double min_t = now - kWindowSec;
        while (count && ring[start].t < min_t) {
            start = (start + 1) % kCap;
            --count;
        }
    }

    void push(double t, float ms, uint32_t logic) {
        prune(t);
        if (count == kCap) {
            start = (start + 1) % kCap;
            --count;
        }
        const int i = (start + count) % kCap;
        ring[i] = {t, ms, logic};
        ++count;
        undo = ring[i];
        undo_ready = true;
        line[line_i] = ms;
        line_i = (line_i + 1) % kGraphPoints;
    }

    void undo_push() {
        if (!undo_ready || !count) return;
        const int last = (start + count - 1) % kCap;
        if (ring[last].t != undo.t || ring[last].ms != undo.ms) return;
        --count;
        line_i = (line_i - 1 + kGraphPoints) % kGraphPoints;
        undo_ready = false;
    }

    float line_ms(int point) const {
        if (point < 0 || point >= kGraphPoints) return 0;
        return line[(line_i + point) % kGraphPoints];
    }

    float graph_ms(int point, double now) const {
        if (point < 0 || point >= kGraphPoints || count <= 0) return 0;
        const double step = bucket_sec();
        const double t0 = now - kWindowSec + point * step;
        const double t1 = t0 + step;
        float peak = 0;
        for (int n = 0; n < count; ++n) {
            const Sample& s = ring[(start + n) % kCap];
            if (s.t >= t0 && s.t < t1) peak = std::max(peak, s.ms);
        }
        return peak;
    }

    static float percentile_ms(const float* ms, int n, float p) {
        if (!n) return 0;
        float sorted[kCap];
        int m = 0;
        for (int i = 0; i < n; ++i)
            if (ms[i] > 0) sorted[m++] = ms[i];
        if (!m) return 0;
        std::sort(sorted, sorted + m);
        const int idx = std::max(0, (int)std::ceil(m * p) - 1);
        return sorted[idx];
    }

    void compute(double now, float& avg_ms, float& worst_ms, float& fps, double& logic_rate, float& low1_fps) const {
        avg_ms = worst_ms = fps = low1_fps = 0;
        logic_rate = 0;
        if (count <= 0) return;
        const double min_t = now - kWindowSec;
        double sum = 0;
        uint64_t logic_sum = 0;
        int frames = 0;
        float scratch[kCap];
        int scratch_n = 0;
        worst_ms = 0;
        for (int n = 0; n < count; ++n) {
            const Sample& s = ring[(start + n) % kCap];
            if (s.t < min_t) continue;
            sum += s.ms;
            worst_ms = std::max(worst_ms, s.ms);
            logic_sum += s.logic;
            ++frames;
            if (scratch_n < kCap) scratch[scratch_n++] = s.ms;
        }
        if (!frames) return;
        avg_ms = (float)(sum / frames);
        fps = (float)(frames / kWindowSec);
        logic_rate = logic_sum / kWindowSec;
        const float p99 = percentile_ms(scratch, scratch_n, 0.99f);
        if (p99 > 0) low1_fps = 1000.0f / p99;
    }
};

struct FrameHistoryAverages {
    static constexpr int kFrames = FrameHistory::kGraphPoints;
    static float low1_fps(const float ms[kFrames]) {
        return FrameHistory::percentile_ms(ms, kFrames, 0.99f) > 0
                   ? 1000.0f / FrameHistory::percentile_ms(ms, kFrames, 0.99f)
                   : 0;
    }
    static void compute(const float ms[kFrames], const uint32_t logic[kFrames], float& avg_ms, float& worst_ms,
                        float& fps, double& logic_rate) {
        double sum = 0;
        uint64_t logic_sum = 0;
        worst_ms = 0;
        int frames = 0;
        for (int i = 0; i < kFrames; ++i) {
            if (ms[i] <= 0) continue;
            sum += ms[i];
            worst_ms = std::max(worst_ms, ms[i]);
            logic_sum += logic[i];
            ++frames;
        }
        avg_ms = frames ? (float)(sum / frames) : 0;
        fps = frames ? (float)(frames / FrameHistory::kWindowSec) : 0;
        logic_rate = frames ? logic_sum / FrameHistory::kWindowSec : 0;
    }
};

struct PerfAverage {
    bool started = false;
    double since = 0, fps = 0, logic = 0;
    uint64_t first_frames = 0, first_steps = 0, last_frames = 0, last_steps = 0;
    int renderer = 0, mode = 0, target = 0;
    float scale = 0;
    void reset() { started = false; fps = logic = 0; }
    void sample(double now, uint64_t frames, uint64_t steps, int api, int rate_mode, int rate, float resolution) {
        if (!started || api != renderer || rate_mode != mode || rate != target || resolution != scale ||
            frames < last_frames || steps < last_steps || now < since) {
            started = true;
            since = now;
            first_frames = frames;
            first_steps = steps;
            renderer = api;
            mode = rate_mode;
            target = rate;
            scale = resolution;
            fps = logic = 0;
        } else if (now > since) {
            fps = double(frames - first_frames) / (now - since);
            logic = double(steps - first_steps) / (now - since);
        }
        last_frames = frames;
        last_steps = steps;
    }
};
}
