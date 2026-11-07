#include "gfx/vulkan/device_choice.h"

#include <cstdio>
#include <string>

static int failures = 0;

static void check(bool ok, const char* what) {
    if (!ok) {
        std::fprintf(stderr, "FAIL: %s\n", what);
        failures++;
    }
}

static gfxvk::GpuCandidate gpu(const char* name, const char* id, int kind, int rank = 0, const char* unavailable = "") {
    gfxvk::GpuCandidate g;
    g.name = name;
    g.identity = id;
    g.kind = kind;
    g.rank = rank;
    g.unavailable = unavailable;
    return g;
}

int main() {
    using namespace gfxvk;
    const std::vector<GpuCandidate> both = {
        gpu("Intel UHD", "8086:9a49", kGpuIntegrated),
        gpu("NVIDIA GTX 1660 Ti", "10de:2184", kGpuDiscrete),
    };
    auto dedicated = choose_gpu(both, "", "");
    check(dedicated.index == 1 && dedicated.reason == "dedicated", "default prefers dedicated");
    auto integrated = choose_gpu(both, "integrated", "");
    check(integrated.index == 0 && integrated.reason == "integrated", "saved integrated");
    auto named = choose_gpu(both, "10de:2184", "");
    check(named.index == 1 && named.reason == "setting", "saved identity");
    auto env = choose_gpu(both, "10de:2184", "integrated");
    check(env.index == 0 && env.reason == "env", "env overrides the saved card");
    auto fragment = choose_gpu(both, "", "gtx");
    check(fragment.index == 1 && fragment.reason == "env", "env matches the name");

    const std::vector<GpuCandidate> only = {gpu("Intel UHD", "8086:9a49", kGpuIntegrated)};
    auto fallback = choose_gpu(only, "", "");
    check(fallback.index == 0 && fallback.reason == "dedicated unavailable", "dedicated falls back to the only GPU");

    const std::vector<GpuCandidate> missing = both;
    auto gone = choose_gpu(missing, "1002:73bf", "");
    check(gone.index == 1 && !gone.fallback.empty(), "missing card falls back to dedicated");

    const std::vector<GpuCandidate> ranks = {
        gpu("Intel UHD", "8086:9a49", kGpuIntegrated, 0),
        gpu("Old discrete", "10de:1c82", kGpuDiscrete, 1),
    };
    auto older = choose_gpu(ranks, "dedicated", "");
    check(older.index == 1, "a dedicated GPU wins over a newer integrated GPU");

    check(!gpu_restart_pending("", "10de:2184", kGpuDiscrete, true, true), "dedicated in use needs no restart");
    check(gpu_restart_pending("integrated", "10de:2184", kGpuDiscrete, true, true), "integrated choice waits for restart");
    check(gpu_restart_pending("8086:9a49", "10de:2184", kGpuDiscrete, true, true), "another card waits for restart");
    check(!gpu_restart_pending("dedicated", "8086:9a49", kGpuIntegrated, false, true), "no dedicated GPU stays on integrated");

    return failures ? 1 : 0;
}
