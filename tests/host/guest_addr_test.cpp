// SPDX-License-Identifier: MPL-2.0
#include "guest_addr.h"
#include <cassert>

int main() {
    assert(GC(0x02715310) == 0x02715310);
    assert(GD(0x101F84DC) == 0x101F84DC);
    assert(guest_code_valid(0x02593B18));
    assert(guest_code_valid(0x02593B10));
    assert(guest_data_valid(0x101F84DC));
    assert(guest_code_range_ok(0x02593B10, 0x02593B18));
}
