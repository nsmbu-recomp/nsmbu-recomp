#include "gfx/vulkan/rendering_path.h"
#include <cassert>

int main() {
    using namespace gfxvk;
    RenderingCapabilities caps;
    caps.api = VK_API_VERSION_1_3;
    caps.core = true;
    const uint32_t hd520 = intel_windows_driver(101, 2115);
    const uint32_t newer = intel_windows_driver(101, 5762);
    const auto oldIntel = choose_rendering_path(caps, 0x8086, hd520, true, nullptr);
    assert(oldIntel.path == RenderingPath::RenderPass);
    assert(choose_rendering_path(caps, 0x8086, intel_windows_driver(101, 4999), true, nullptr).path ==
           RenderingPath::RenderPass);
    assert(choose_rendering_path(caps, 0x8086, intel_windows_driver(101, 5000), true, nullptr).path ==
           RenderingPath::Core);
    assert(choose_rendering_path(caps, 0x8086, newer, true, nullptr).path == RenderingPath::Core);
    assert(choose_rendering_path(caps, 0x8086, hd520, false, nullptr).path == RenderingPath::Core);
    assert(choose_rendering_path(caps, 0x10DE, hd520, true, nullptr).path == RenderingPath::Core);
    assert(choose_rendering_path(caps, 0x10DE, hd520, true, "1").path == RenderingPath::RenderPass);
    assert(choose_rendering_path(caps, 0x8086, hd520, true, "0").path == RenderingPath::Core);
    caps.api = VK_API_VERSION_1_0;
    assert(choose_rendering_path(caps, 0x8086, hd520, true, nullptr).path == RenderingPath::None);
}
