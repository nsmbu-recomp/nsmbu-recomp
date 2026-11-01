#pragma once
#include "imgui.h"
#include <algorithm>

namespace overlay::flow {
inline float text(const char* s) { return ImGui::CalcTextSize(s, nullptr, true).x; }
inline float labeled(float item, const char* label) {
    const float t = text(label);
    return t > 0 ? item + ImGui::GetStyle().ItemInnerSpacing.x + t : item;
}
inline float check(const char* label) { return labeled(ImGui::GetFrameHeight(), label); }
inline float button(const char* label) { return text(label) + ImGui::GetStyle().FramePadding.x * 2; }
inline void next(float width, float spacing = -1.0f) {
    ImGui::SameLine(0, spacing);
    if (ImGui::GetContentRegionAvail().x < width) ImGui::NewLine();
}
inline float item_width(float wanted, const char* label, float least) {
    const float room = ImGui::GetContentRegionAvail().x - labeled(0, label);
    return std::max(least, std::min(wanted, room));
}
}
