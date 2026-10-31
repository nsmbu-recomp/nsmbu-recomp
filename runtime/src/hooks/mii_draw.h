#pragma once

#include <cstdint>

constexpr uint32_t kMiiShaderObject = 0x3CCu;
constexpr uint32_t kMiiShaderProgram = 0x3D0u;

inline bool mii_model_draw_ready(uint32_t central, uint32_t program) {
    return central != 0 && program != 0;
}
