#pragma once

#include <cstdint>

namespace gfx {

inline bool seed_new_color_target(bool forRendering, bool isDepth, uint32_t slices, bool is2d, bool compressed,
                                  uint32_t guestWidth, uint32_t guestHeight, uint32_t texWidth, uint32_t texHeight) {
    const bool sized = guestWidth > 0 && guestHeight > 0 && texWidth > 0 && texHeight > 0;
    return forRendering && !isDepth && slices == 1 && is2d && !compressed && sized;
}

}
