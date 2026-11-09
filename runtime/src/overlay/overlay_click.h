#pragma once

// The settings close button is the X in the top right.
// A test click uses the center of that button. The rect is in 0..1 display space.
struct OverlayClick {
    float x = 0;
    float y = 0;
    bool ok = false;
};

inline OverlayClick overlay_close_click(float x0, float y0, float x1, float y1) {
    OverlayClick click;
    if (!(x1 > x0) || !(y1 > y0)) return click;
    if (x0 < 0.f || y0 < 0.f || x1 > 1.f || y1 > 1.f) return click;
    click.x = (x0 + x1) * 0.5f;
    click.y = (y0 + y1) * 0.5f;
    click.ok = true;
    return click;
}
