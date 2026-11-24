

#pragma once
#include <cstddef>
#include <cstdint>
#include <functional>
#include <string>
#include <vector>

namespace rtl {

enum class Joining : uint8_t { None, Right, Dual, Transparent, Causing };
Joining joining_type(uint32_t cp);

enum class Form : uint8_t { Isolated, Final, Initial, Medial };

uint32_t presentation_form(uint32_t cp, Form form);

uint32_t lam_alef(uint32_t alef, bool final_form);

bool is_rtl_letter(uint32_t cp);

bool is_rtl_mark(uint32_t cp);

enum class Bidi : uint8_t { L, R, AL, EN, ES, ET, AN, CS, NSM, BN, B, S, WS, ON };
Bidi bidi_class(uint32_t cp);

uint32_t mirror(uint32_t cp);

using HasGlyph = std::function<bool(uint32_t)>;

struct Shaped {
    uint32_t code;
    bool skip;
};

Shaped shape_at(const std::function<uint32_t(size_t)>& at, size_t i, size_t n, const HasGlyph& has);

std::vector<uint32_t> shape(const std::u16string& s, const HasGlyph& has);

std::vector<uint8_t> resolve_levels(const std::vector<Bidi>& cls, int para_level);

std::vector<size_t> visual_order(std::vector<uint8_t> levels, const std::vector<bool>& ws, int para_level);

struct Plan {
    bool rtl = false;
    std::vector<uint32_t> code;
    std::vector<uint8_t> skip;
    std::vector<uint8_t> level;
    std::vector<uint8_t> ws;
};
Plan plan(const uint16_t* text, size_t n, const HasGlyph& has);

struct Glyph {
    float pen;
    float advance;
    uint8_t level;
    bool ws;
};

std::vector<float> reorder_line(const std::vector<Glyph>& line, int para_level);

}

namespace rtl_text {
void language_pack_opened(const std::string& host_path);
}
