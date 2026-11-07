#pragma once
#include "gfx/display_modes.h"

#include <algorithm>
#include <cmath>

namespace gfx {

inline Box fit_picture(float dw, float dh, float tw, float th, int filter, float* scale) {
    float s = std::min(dw / tw, dh / th);
    if (filter == kInteger && s >= 1) s = std::floor(s + 1e-3f);
    *scale = s;
    float w = std::round(tw * s), h = std::round(th * s);
    return {std::floor((dw - w) / 2), std::floor((dh - h) / 2), w, h};
}

inline Box tv_picture(float dw, float dh, float tw, float th, bool stretch, bool match_window, float chosen, int filter, float* scale) {
    if (!stretch) return fit_picture(dw, dh, tw, th, filter, scale);
    float a = match_window && dw > 0.0f && dh > 0.0f ? dw / dh : chosen;
    if (!(a > 0.1f) || !(th > 0.0f)) return fit_picture(dw, dh, tw, th, filter, scale);
    if (dh > 0.0f && std::fabs(dw / dh - a) < 0.006f) {
        *scale = 1.0f;
        return {0, 0, dw, dh};
    }
    return fit_picture(dw, dh, a * th, th, filter, scale);
}

}
