#include "gfx/res_presets.h"

#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>

static int failures = 0;

static void check(bool ok, const char* what) {
    if (!ok) {
        std::fprintf(stderr, "FAIL: %s\n", what);
        failures++;
    }
}

int main() {
    check(gfx::res_preset_count() == 6, "six presets");
    check(std::strcmp(gfx::kResPresets[0].label, "360p") == 0, "360p label");
    check(std::strcmp(gfx::kResPresets[3].label, "1080p") == 0, "1080p label");
    check(std::strcmp(gfx::kResPresets[5].label, "2160p/4K") == 0, "4K label");
    for (const gfx::ResPreset& p : gfx::kResPresets) {
        const int labeled = std::atoi(p.label);
        const int height = (int)std::lround(720.0 * p.scale);
        const int width = (int)std::lround(1280.0 * p.scale);
        check(labeled == height, p.label);
        check(width * 9 == height * 16, "16:9");
    }
    check(std::fabs(gfx::clamp_res_scale(0.25f) - 0.5f) < 1e-6f, "floor");
    check(std::fabs(gfx::clamp_res_scale(0.75f) - 0.75f) < 1e-6f, "540p scale");
    check(std::fabs(gfx::clamp_res_scale(1.5f) - 1.5f) < 1e-6f, "1080p scale");
    check(std::fabs(gfx::clamp_res_scale(8.0f) - 4.0f) < 1e-6f, "ceiling");
    check(std::fabs(gfx::clamp_res_scale(NAN) - 1.0f) < 1e-6f, "nan");
    check(std::fabs(gfx::next_res_preset(0.5f) - 0.75f) < 1e-6f, "next after 360p");
    check(std::fabs(gfx::next_res_preset(3.0f) - 0.5f) < 1e-6f, "wrap after 4K");
    check(std::fabs(gfx::next_res_preset(4.0f) - 0.5f) < 1e-6f, "wrap above the list");
    return failures ? 1 : 0;
}
