#include "mods/lua_gui.h"
#include "overlay/game_rect.h"

#include "imgui.h"

namespace mods::lua_gui {

void draw(ImDrawList* dl, float display_w, float display_h) {
    if (!dl || display_w <= 1 || display_h <= 1) return;
    const std::vector<Command> list = commands();
    if (list.empty()) return;
    float ox = 0, oy = 0, ow = display_w, oh = display_h;
    if (!overlay::game_content_rect(display_w, display_h, ox, oy, ow, oh)) return;

    auto sx = [&](float x) { return ox + x / 1280.0f * ow; };
    auto sy = [&](float y) { return oy + y / 720.0f * oh; };
    auto point = [&](float x, float y) { return ImVec2(sx(x), sy(y)); };
    auto color = [](uint32_t c) {
        return IM_COL32((c >> 24) & 0xFF, (c >> 16) & 0xFF, (c >> 8) & 0xFF, c & 0xFF);
    };
    const float kx = ow / 1280.0f, ky = oh / 720.0f;
    const float k = kx < ky ? kx : ky;

    dl->PushClipRect(ImVec2(ox, oy), ImVec2(ox + ow, oy + oh), true);
    for (const Command& c : list) {
        const ImU32 col = color(c.color);
        switch (c.kind) {
        case Kind::Rect:
            dl->AddRect(point(c.x0, c.y0), point(c.x1, c.y1), col, c.rounding * k, c.thickness * k, 0);
            break;
        case Kind::RectFilled:
            dl->AddRectFilled(point(c.x0, c.y0), point(c.x1, c.y1), col, c.rounding * k);
            break;
        case Kind::Line:
            dl->AddLine(point(c.x0, c.y0), point(c.x1, c.y1), col, c.thickness * k);
            break;
        case Kind::Circle:
            dl->AddCircle(point(c.x0, c.y0), c.x1 * k, col, 0, c.thickness * k);
            break;
        case Kind::CircleFilled:
            dl->AddCircleFilled(point(c.x0, c.y0), c.x1 * k, col);
            break;
        case Kind::TriangleFilled:
            dl->AddTriangleFilled(point(c.x0, c.y0), point(c.x1, c.y1), point(c.x2, c.y2), col);
            break;
        case Kind::Text: {
            ImFont* font = ImGui::GetFont();
            const float size = ImGui::GetFontSize() * c.scale * k;
            dl->AddText(font, size, point(c.x0, c.y0), col, c.text.c_str());
            break;
        }
        }
    }
    dl->PopClipRect();
}

}
