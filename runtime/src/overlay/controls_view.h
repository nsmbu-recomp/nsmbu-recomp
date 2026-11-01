

#pragma once
#include "input/input_map.h"

#include <algorithm>

namespace overlay {

constexpr float kControlsRefW = 1060.0f;
constexpr float kControlsRefH = 380.0f;

inline float controls_fit_scale(float w, float h) {
    if (!(w > 1.0f) || !(h > 1.0f)) return 0.05f;
    return std::clamp(std::min(w / kControlsRefW, h / kControlsRefH), 0.05f, 1.3f);
}

enum { kColKey0, kColKey1, kColPad, kColAny };

struct ControlsView {

    input_map::Mapping m;
    bool pro = false;
    bool keys_enabled = true;
    int cap_action = -1, cap_col = 0;

    const float* pad = nullptr;
    const bool* keys = nullptr;
    float act[input_map::kActionCount] = {};
    float stick[2][2] = {};
    double t = 0;

    int hover_action = -1, hover_col = -1;
    int start_action = -1, start_col = 0;
    int clear_action = -1, clear_col = 0;
    bool compact_side = false;
};

void draw_controls(ControlsView& v, float w, float h);

}
