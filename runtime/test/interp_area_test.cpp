#include "hooks/interp_area.h"

#include <cassert>

int main() {
    using interp::area_camera_layers_ready;
    assert(!area_camera_layers_ready(0, 0, 0));
    assert(!area_camera_layers_ready(0x17, 0x10000000, 0x10000000));
    assert(!area_camera_layers_ready(0x18, 0, 0x10000000));
    assert(!area_camera_layers_ready(0x18, 0x10000000, 0));
    assert(area_camera_layers_ready(0x1F, 0x10000000, 0x20000000));
    return 0;
}
