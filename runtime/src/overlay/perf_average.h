#pragma once
#include <algorithm>
#include <cmath>
#include <cstdint>

namespace overlay {

struct FrameHistoryAverages {
    static constexpr int kFrames = 120;
    static float percentile_ms(const float ms[kFrames], float p) {
        float sorted[kFrames];
        int n = 0;
        for (int i = 0; i < kFrames; ++i) {
            if (ms[i] > 0) sorted[n++] = ms[i];
        }
        if (!n) return 0;
        std::sort(sorted, sorted + n);
        const int idx = std::max(0, (int)std::ceil(n * p) - 1);
        return sorted[idx];
    }
    static float low1_fps(const float ms[kFrames]) {
        const float p99 = percentile_ms(ms, 0.99f);
        return p99 > 0 ? 1000.0f / p99 : 0;
    }
    static void compute(const float ms[kFrames], const uint32_t logic[kFrames], float& avg_ms, float& worst_ms,
                        float& fps, double& logic_rate) {
        double sum = 0;
        uint64_t logic_sum = 0;
        worst_ms = 0;
        for (int i = 0; i < kFrames; ++i) {
            sum += ms[i];
            worst_ms = std::max(worst_ms, ms[i]);
            logic_sum += logic[i];
        }
        avg_ms = (float)(sum / kFrames);
        fps = 0;
        logic_rate = 0;
        if (sum > 0) {
            fps = (float)(1000.0 * kFrames / sum);
            logic_rate = logic_sum / (sum / 1000.0);
        }
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
            started = true; since = now; first_frames = frames; first_steps = steps;
            renderer = api; mode = rate_mode; target = rate; scale = resolution;
            fps = logic = 0;
        } else if (now > since) {
            fps = double(frames - first_frames) / (now - since);
            logic = double(steps - first_steps) / (now - since);
        }
        last_frames = frames; last_steps = steps;
    }
};
}
