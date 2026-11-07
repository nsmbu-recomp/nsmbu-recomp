

#pragma once
#include <cstdint>
#include <initializer_list>

namespace gx2 {

enum Op : uint32_t {
    OP_NOP = 0,
    OP_SET_REGS,
    OP_DRAW,
    OP_DRAW_INDEXED,
    OP_CLEAR_COLOR,
    OP_CLEAR_DEPTH,
    OP_CLEAR_BUFFERS,
    OP_COPY_SURFACE,
    OP_COPY_TO_SCAN,
    OP_CALL,
    OP_SET_CONTEXT,
    OP_INVALIDATE,
    OP_EXPAND_COLOR,
    OP_EXPAND_DEPTH,

    OP_FLUSH,
    OP_DRAW_DONE,
    OP_SWAP,
    OP_SETUP_CONTEXT,
    OP_FENCE,

    OP_LAYOUT_ROOT,
    OP_PEEK_Z,
    OP_LAYOUT_CONTENT,
    OP_COVER_DRAW,
    OP_COUNT
};

void emit(Op op, const uint32_t* payload, uint32_t n);
inline void emit(Op op, std::initializer_list<uint32_t> payload) { emit(op, payload.begin(), (uint32_t)payload.size()); }

void set_reg(uint32_t reg, uint32_t value);
void mirror_reg(uint32_t reg, uint32_t value);
void set_regs(uint32_t first, const uint32_t* values, uint32_t count);

uint32_t* regs();

void execute(const uint32_t* words, uint32_t count);

inline uint32_t fbits(float f) { uint32_t u; __builtin_memcpy(&u, &f, 4); return u; }
inline float bitsf(uint32_t u) { float f; __builtin_memcpy(&f, &u, 4); return f; }

}
