

#pragma once
#include <cstdint>
#include <memory>
#include <string>
#include <unordered_set>

namespace game_font {

using Glyphs = std::unordered_set<uint32_t>;

std::shared_ptr<const Glyphs> pack_font(const std::string& pack_path, std::string* why);

bool parse_bffnt(const uint8_t* data, size_t size, Glyphs& out);

}
