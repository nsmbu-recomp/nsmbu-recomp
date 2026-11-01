#pragma once
#include <cstdint>

namespace overlay {
struct PerfAverage;
}

extern "C" {
void nsmbu_perf_average_reset(overlay::PerfAverage* avg);
void nsmbu_perf_average_sample(overlay::PerfAverage* avg, double now, uint64_t frames, uint64_t steps, int api,
                               int rate_mode, int rate, float resolution);
void nsmbu_frame_history_compute(const float* ms, const uint32_t* logic, float* avg_ms, float* worst_ms, float* fps,
                                 double* logic_rate);
float nsmbu_frame_history_percentile_ms(const float* ms, float p);
float nsmbu_frame_history_low1_fps(const float* ms);
}

namespace overlay {

struct FrameHistoryAverages {
    static constexpr int kFrames = 120;
    static float percentile_ms(const float ms[kFrames], float p) {
        return nsmbu_frame_history_percentile_ms(ms, p);
    }
    static float low1_fps(const float ms[kFrames]) {
        return nsmbu_frame_history_low1_fps(ms);
    }
    static void compute(const float ms[kFrames], const uint32_t logic[kFrames], float& avg_ms, float& worst_ms,
                        float& fps, double& logic_rate) {
        nsmbu_frame_history_compute(ms, logic, &avg_ms, &worst_ms, &fps, &logic_rate);
    }
};

struct PerfAverage {
    bool started = false;
    double since = 0, fps = 0, logic = 0;
    uint64_t first_frames = 0, first_steps = 0, last_frames = 0, last_steps = 0;
    int renderer = 0, mode = 0, target = 0;
    float scale = 0;
    void reset() {
        nsmbu_perf_average_reset(this);
    }
    void sample(double now, uint64_t frames, uint64_t steps, int api, int rate_mode, int rate, float resolution) {
        nsmbu_perf_average_sample(this, now, frames, steps, api, rate_mode, rate, resolution);
    }
};

static_assert(sizeof(PerfAverage) == 80);
static_assert(alignof(PerfAverage) == 8);

}
