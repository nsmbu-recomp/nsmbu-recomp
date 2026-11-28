#pragma once
#include <cstdint>

namespace interp {

// agl::lyr::Renderer layer slots that Renderer::calcForAreaTask writes.
// Slot 9 is the TV 3D layer and slot 0x17 is the GamePad 3D layer.
// Each store is Layer+0x48. A null slot writes guest address 0x48.
inline bool area_camera_layers_ready(uint32_t count, uint32_t tv, uint32_t drc) {
    if (count <= 0x17) return false;
    return tv != 0 && drc != 0;
}

}
