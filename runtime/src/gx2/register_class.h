#pragma once
#include <cstdint>
#include <vector>

namespace gx2 {

#ifndef GX2_KNUMREGS_READY
constexpr uint32_t kNumRegs = 0x10000;
#endif

inline bool shader_irrelevant_reg(uint32_t reg, bool addressMemo) {
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

using ShaderKeyMaskFn = bool (*)(uint32_t reg, uint32_t& mask);

struct RegisterClasses {
    bool shaderIrrelevant[kNumRegs];
    bool profFast[kNumRegs];
    std::vector<uint32_t> bits;
    template <class ProfFast>
    void build(bool addressMemo, ProfFast fastReg, ShaderKeyMaskFn keyMask) {
        for (uint32_t r = 0; r < kNumRegs; ++r) {
            shaderIrrelevant[r] = shader_irrelevant_reg(r, addressMemo);
            profFast[r] = fastReg(r) ? true : false;
        }
        bits.clear();
        if (!keyMask) return;
        bits.assign(kNumRegs, ~0u);
        for (uint32_t r = 0; r < kNumRegs; ++r) {
            uint32_t m;
            if (keyMask(r, m)) bits[r] = m;
        }
    }
    bool changed(uint32_t reg, uint32_t old, uint32_t value) const {
        return bits.empty() || ((old ^ value) & bits[reg]) != 0;
    }
};

}
