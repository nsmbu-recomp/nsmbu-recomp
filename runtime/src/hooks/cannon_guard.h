#pragma once

#include <cstdint>

// A guest pointer in the first 64 KiB is the null page.
// The World 1 castle cannon passes those pointers into light copies and course teardown (#82).
inline bool cannon_guest_absent(uint32_t addr) { return addr < 0x10000u; }
