#pragma once

#include <atomic>

// The settings close button is the X in the top right.
// A test click uses the center of that button. The rect is in 0..1 display space.
// An agent close is not a mouse click, so ImGui does not focus the window.
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

inline std::atomic<bool>& agent_close_request() {
    static std::atomic<bool> request{false};
    return request;
}

inline void request_agent_close() { agent_close_request().store(true, std::memory_order_relaxed); }

inline bool take_agent_close() { return agent_close_request().exchange(false, std::memory_order_relaxed); }
