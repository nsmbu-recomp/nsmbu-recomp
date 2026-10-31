

#pragma once
#include <Metal/Metal.h>

#include <cstdint>

namespace gfx {

enum class Convert : uint8_t {
    NONE,
    RGB565,
    RGBA5551,
    ABGR1555,
    RGBA4,
    RG4,
    D24S8,
    D24_R32F,
    X24_8_32F,
};

struct FormatInfo {
    MTLPixelFormat pixel = MTLPixelFormatInvalid;
    uint32_t bytesPerBlock = 0;
    uint32_t hostBytesPerBlock = 0;
    bool compressed = false;
    // BC1-5 the GPU cannot sample (iOS Simulator, some iPhones): RGBA8 texture, blocks decoded on upload
    // (gfx/vulkan/bc_reference.h mode: BC type | 256 when signed); 0: native BC or not compressed
    uint16_t bcDecode = 0;
    bool depth = false;
    bool stencil = false;
    Convert convert = Convert::NONE;
    enum Kind : uint8_t { FLOAT, UINT, SINT } kind = FLOAT;
};

FormatInfo format_info(uint32_t gx2Format, bool isDepth);
// the device samples BC1-5 textures (gfx::init sets it)
extern bool g_bc_native;

void convert_row(Convert c, const uint8_t* src, uint8_t* dst, uint32_t count);

}
