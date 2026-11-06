#include "overlay/perf_average.h"
#include "overlay/android_telemetry.h"
#include <cassert>
#include <cmath>
#include <filesystem>
#include <fstream>
int main() {
    overlay::PerfAverage a;
    a.sample(0, 100, 50, 0, 0, 30, 1);
    a.sample(2, 160, 110, 0, 0, 30, 1);
    assert(a.fps == 30 && a.logic == 30);
    a.sample(4, 340, 170, 0, 0, 30, 1);
    assert(a.fps == 60 && a.logic == 30);
    for (int change = 0; change < 4; ++change) {
        a.sample(5 + change, 400, 200, change > 0, change > 1, change > 2 ? 120 : 30, 2);
        assert(a.fps == 0 && a.logic == 0);
    }
    a.sample(10, 640, 320, 1, 1, 120, 2);
    assert(a.fps == 120 && a.logic == 60);
    a.sample(11, 0, 0, 1, 1, 120, 2);
    assert(a.fps == 0);
    overlay::FrameHistory h;
    for (int i = 0; i < 120; ++i) h.push(1.0 + i * (overlay::FrameHistory::kWindowSec / 120.0), 1000.0f / 60.0f, 1);
    float avg_ms = 0, worst = 0, fps = 0, low1 = 0;
    double logic_rate = 0;
    h.compute(3.0, avg_ms, worst, fps, logic_rate, low1);
    assert(fps > 59.9f && fps < 60.1f);
    assert(logic_rate > 59.9 && logic_rate < 60.1);
    assert(low1 > 59.9f && low1 < 60.1f);
    h.reset();
    for (int i = 0; i < 10; ++i) h.push(1.1 + i * 0.01, 50.0f, 1);
    h.compute(1.2, avg_ms, worst, fps, logic_rate, low1);
    assert(low1 > 19.9f && low1 < 20.1f);
    assert(h.graph_ms(overlay::FrameHistory::kGraphPoints - 1, 1.2) > 49.0f);
    h.reset();
    for (int i = 0; i < 119; ++i) h.push(1.0 + i * 0.001, 16.0f, 1);
    h.push(2.0, 50.0f, 1);
    assert(h.line_ms(119) > 49.0f);
    h.push(2.001, 16.0f, 1);
    assert(h.line_ms(118) > 49.0f && h.line_ms(119) < 20.0f);
    a.reset();
    assert(!a.started);
    auto root = std::filesystem::temp_directory_path() / "nsmbu-telemetry-test";
    std::filesystem::create_directories(root / "class/kgsl/kgsl-3d0");
    std::ofstream(root / "class/kgsl/kgsl-3d0/gpubusy") << "25 100";
    overlay::AndroidTelemetry telemetry;
    telemetry.read(root);
    assert(telemetry.busy == 25);
    std::ofstream(root / "class/kgsl/kgsl-3d0/gpubusy") << "25 0";
    telemetry.read(root);
    assert(telemetry.busy == -1);
    std::filesystem::remove_all(root);
}
