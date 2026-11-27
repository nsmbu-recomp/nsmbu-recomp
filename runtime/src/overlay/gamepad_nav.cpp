#include "gamepad_nav.h"
#include "imgui.h"

#include <algorithm>

namespace overlay {

void GamepadNavigation::feed(ImGuiIO& io, const float* values, const float* previous, bool enabled, bool mouse_used,
                             const int* face) {
    using namespace input_map;
    static constexpr int kByLabel[4] = {kPadA, kPadB, kPadX, kPadY};
    if (!face) face = kByLabel;
    constexpr int navigation[] = {kPadA, kPadB, kPadX, kPadY, kPadDUp, kPadDDown, kPadDLeft, kPadDRight,
                                  kPadLSUp, kPadLSDown, kPadLSLeft, kPadLSRight, kPadLT, kPadRT, kPadLB,
                                  kPadRB, kPadMenu, kPadOptions};
    bool nav_press = false;
    if (enabled)
        for (int p : navigation) nav_press |= values[p] > 0.5f && previous[p] <= 0.5f;
    if (mouse_used) mouse_owned_ = true;
    else if (nav_press) mouse_owned_ = false;
    const bool active = enabled && controller_active();
    if (active) io.ConfigFlags |= ImGuiConfigFlags_NavEnableGamepad;
    else io.ConfigFlags &= ~ImGuiConfigFlags_NavEnableGamepad;
    auto key = [&](ImGuiKey k, float v) {
        if (!active || v < 0.35f) v = 0.0f;
        io.AddKeyAnalogEvent(k, v > 0.5f, v);
    };
    auto pad = [&](ImGuiKey k, int p) { key(k, values[p]); };
    // ImGui moves focus from the D-pad keys only. The left stick uses those same keys so it walks the menu instead of scrolling it.
    auto dir = [&](ImGuiKey k, int dpad, int stick) { key(k, std::max(values[dpad], values[stick])); };
    key(ImGuiKey_GamepadFaceDown, face[0] > 0 ? values[face[0]] : 0.0f);
    key(ImGuiKey_GamepadFaceRight, face[1] > 0 ? values[face[1]] : 0.0f);
    key(ImGuiKey_GamepadFaceLeft, face[2] > 0 ? values[face[2]] : 0.0f);
    key(ImGuiKey_GamepadFaceUp, face[3] > 0 ? values[face[3]] : 0.0f);
    dir(ImGuiKey_GamepadDpadUp, kPadDUp, kPadLSUp);
    dir(ImGuiKey_GamepadDpadDown, kPadDDown, kPadLSDown);
    dir(ImGuiKey_GamepadDpadLeft, kPadDLeft, kPadLSLeft);
    dir(ImGuiKey_GamepadDpadRight, kPadDRight, kPadLSRight);
    pad(ImGuiKey_GamepadL2, kPadLT);
    pad(ImGuiKey_GamepadR2, kPadRT);
    pad(ImGuiKey_GamepadStart, kPadMenu);
    pad(ImGuiKey_GamepadBack, kPadOptions);
}

}
