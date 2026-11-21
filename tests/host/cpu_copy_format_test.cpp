#include "formats.h"

#include <cassert>

int main() {
    using namespace gfxvk;
    auto f823 = cpu_copy_format(0x823);
    assert(f823.pixel == VK_FORMAT_R32G32B32A32_SFLOAT);
    assert(f823.bytesPerBlock == 16);
    assert(f823.compressed == false);
    auto f806 = cpu_copy_format(0x806);
    assert(f806.pixel == VK_FORMAT_R16_SFLOAT);
    assert(f806.bytesPerBlock == 2);
    auto f810 = cpu_copy_format(0x810);
    assert(f810.pixel == VK_FORMAT_R16G16_SFLOAT);
    assert(f810.bytesPerBlock == 4);
    auto f81E = cpu_copy_format(0x81E);
    assert(f81E.pixel == VK_FORMAT_R32G32_SFLOAT);
    assert(f81E.bytesPerBlock == 8);
    auto f820 = cpu_copy_format(0x820);
    assert(f820.pixel == VK_FORMAT_R16G16B16A16_SFLOAT);
    assert(f820.bytesPerBlock == 8);
    auto f1A = cpu_copy_format(0x1A);
    assert(f1A.pixel == VK_FORMAT_R8G8B8A8_UNORM);
    assert(f1A.bytesPerBlock == 4);
    assert(format_info(0x823, true).pixel == VK_FORMAT_UNDEFINED);
    return 0;
}
