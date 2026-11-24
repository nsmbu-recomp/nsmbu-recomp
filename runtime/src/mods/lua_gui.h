#pragma once

#include <cstdint>
#include <string>
#include <vector>

struct ImDrawList;

namespace mods::lua_gui {

enum class Kind : uint8_t {
    Rect,
    RectFilled,
    Line,
    Circle,
    CircleFilled,
    TriangleFilled,
    Text,
};

struct Command {
    Kind kind = Kind::Rect;
    float x0 = 0, y0 = 0;
    float x1 = 0, y1 = 0;
    float x2 = 0, y2 = 0;
    uint32_t color = 0;
    float thickness = 1;
    float rounding = 0;
    float scale = 1;
    std::string text;
};

void clear(const std::string& owner);
void push(const std::string& owner, const Command& command);
void drop(const std::string& owner);
bool active();
std::vector<Command> commands();
void draw(ImDrawList* dl, float display_w, float display_h);

}
