#include "gx2/register_class.h"
#include <cstdio>

using namespace gx2;

static int failures = 0;
static void fail(const char* what, uint32_t reg) {
    if (++failures <= 20) std::printf("register %04X: %s\n", reg, what);
}

static bool local_irrelevant(uint32_t reg, bool addressMemo) {
    if (addressMemo)
        for (uint32_t base : {uint32_t(0xE000), uint32_t(0xE460), uint32_t(0xE930)})
            if (reg >= base && reg < base + 7 * 18) {
                uint32_t word = (reg - base) % 7;
                if (word == 2 || word == 3) return true;
            }
    if (reg >= 0xC000 && reg < 0xC000 + 0x1000) return true;
    for (uint32_t base : {uint32_t(0xE7E0), uint32_t(0xE250), uint32_t(0xECB0)})
        if (reg >= base && reg < base + 7 * 16) return true;
    if (reg >= 0xE8C0 && reg < 0xE8C0 + 7 * 16) {
        uint32_t w = (reg - 0xE8C0) % 7;
        return w != 2;
    }
    return false;
}

static bool mask_reg5(uint32_t reg, uint32_t& mask) {
    if (reg != 5) return false;
    mask = 0xFF;
    return true;
}

int main() {
    static RegisterClasses table;
    for (bool memo : {false, true}) {
        table.build(memo, [](uint32_t) { return false; }, nullptr);
        for (uint32_t r = 0; r < kNumRegs; ++r) {
            if (shader_irrelevant_reg(r, memo) != local_irrelevant(r, memo))
                fail("shader relevance differs", r);
            if (table.shaderIrrelevant[r] != shader_irrelevant_reg(r, memo))
                fail("table shader bit differs", r);
        }
    }
    if (!shader_irrelevant_reg(0xC000, false) || !shader_irrelevant_reg(0xC000 + 0xFFF, true))
        fail("ALU constant is relevant", 0xC000);
    if (shader_irrelevant_reg(0xC000 + 0x1000, false))
        fail("register after ALU constants is irrelevant", 0xD000);
    for (uint32_t base : {uint32_t(0xE7E0), uint32_t(0xE250), uint32_t(0xECB0)})
        for (uint32_t i = 0; i < 7 * 16; ++i)
            if (!shader_irrelevant_reg(base + i, false) || !shader_irrelevant_reg(base + i, true))
                fail("uniform block is relevant", base + i);
    for (uint32_t a = 0; a < 16; ++a)
        for (uint32_t w = 0; w < 7; ++w) {
            uint32_t r = 0xE8C0 + a * 7 + w;
            bool irr = shader_irrelevant_reg(r, false);
            if (w == 2) {
                if (irr) fail("vertex attribute word 2 is irrelevant", r);
            } else if (!irr) fail("vertex attribute word is relevant", r);
        }
    const uint32_t texAddress = 0xE000 + 2;
    if (shader_irrelevant_reg(texAddress, false) || !shader_irrelevant_reg(texAddress, true))
        fail("texture address memo rule", texAddress);
    if (shader_irrelevant_reg(0xE000, true)) fail("texture word 0 is irrelevant", 0xE000);

    table.build(false, [](uint32_t) { return false; }, nullptr);
    if (!table.bits.empty() || !table.changed(6, 0, 1)) fail("empty mask counts every change", 6);
    table.build(false, [](uint32_t) { return false; }, mask_reg5);
    if (table.changed(5, 0, 0x100)) fail("masked bit counted", 5);
    if (!table.changed(5, 0, 1)) fail("key bit ignored", 5);
    if (!table.changed(6, 0, 1)) fail("uncovered register ignored", 6);

    if (failures) {
        std::printf("register_class_test: %d failure(s)\n", failures);
        return 1;
    }
    std::printf("register_class_test: ok\n");
    return 0;
}
