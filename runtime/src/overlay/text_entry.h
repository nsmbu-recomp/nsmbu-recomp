

#pragma once
#include <functional>
#include <memory>
#include <string>

#include "game_font.h"

namespace text_entry {

struct Request {
    std::u16string initial;
    std::u16string hint;
    int max_len = 0;
    int mode = 0;
    int language = 1;

    std::function<void(const std::u16string&)> changed;
    std::shared_ptr<const game_font::Glyphs> glyphs;
};
using Done = std::function<void(bool ok, std::u16string text)>;

bool start(const Request& r, Done done);

void dismiss();
bool active();

void key(int code, bool down, bool repeat);
void text(const char* utf8);
void preedit(const char* utf8);

bool draw(const float* pad);

}
