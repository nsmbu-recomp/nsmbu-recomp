#pragma once
#include <cstdint>

struct Cpu;

namespace decomp {
struct Slot {
    uint32_t addr;
    uint8_t on;
};
extern Slot g_slots[];
extern const unsigned g_slot_count;
void init();
void bind(Cpu* cpu);
inline bool on(unsigned i) { return g_slots[i].on != 0; }
}
