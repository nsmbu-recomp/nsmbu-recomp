#include "gfx/vulkan/shader_present_gate.h"
#include <cstdio>
#include <cstdlib>

static int fails = 0;
static void check(bool cond, const char* what) {
    if (cond) return;
    std::fprintf(stderr, "FAIL %s\n", what);
    ++fails;
}

int main() {
    gfxvk::ShaderPresentGate gate;
    const int64_t period = 16666667;
    check(!gate.due(1, 1000, period), "first demand in a frame waits one period");
    check(!gate.due(1, 1000 + period - 1, period), "still inside the first period");
    check(gate.due(1, 1000 + period, period), "due after one period");
    gate.presented(1000 + period, period);
    check(!gate.due(1, 1000 + period, period), "a present starts the next period");
    check(gate.due(1, 1000 + period * 2, period), "the next repeat is one period later");
    gate.missed(5000);
    check(!gate.due(1, 5000, period), "a missed present waits before retrying");
    check(gate.due(1, 5000 + 2000000, period), "a missed present retries after 2 ms");
    check(!gate.due(2, 9000, period), "a new frame waits a fresh period");
    check(gate.due(2, 9000 + period, period), "the new frame is due after that period");
    gfxvk::ShaderPresentGate fallback;
    check(!fallback.due(3, 0, 0), "a missing period uses one 60 Hz frame");
    check(fallback.due(3, 16666667, 0), "the 60 Hz fallback elapses");
    if (fails) {
        std::fprintf(stderr, "%d shader present gate checks failed\n", fails);
        return 1;
    }
    std::printf("shader present gate checks passed\n");
    return 0;
}
