#pragma once

#include <cstdint>

namespace mods::lua_press {

void press(const void* owner, uint32_t mask, uint64_t steps, uint64_t now_step);
void drop(const void* owner);
uint32_t active(uint64_t step);

}
