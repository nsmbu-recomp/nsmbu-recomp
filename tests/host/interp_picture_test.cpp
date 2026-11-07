#include "hooks/interp_picture.h"

#include <cassert>
#include <cstdint>
#include <vector>

using interp::picture::Submitted;
using interp::picture::copied_is;
using interp::picture::snapshot;

int main() {
    std::vector<uint8_t> live = {1, 2, 3, 4, 5, 6, 7, 8};
    const std::vector<uint8_t> submitted = live;
    Submitted pic;
    snapshot(pic, live.data(), live.size());
    for (uint8_t& b : live) b = static_cast<uint8_t>(b + 10);
    assert(copied_is(pic, submitted.data(), submitted.size()));
    assert(!copied_is(pic, live.data(), live.size()));

    const std::vector<uint8_t> next = live;
    snapshot(pic, live.data(), live.size());
    live[0] = 0;
    assert(copied_is(pic, next.data(), next.size()));
    assert(!copied_is(pic, live.data(), live.size()));

    snapshot(pic, nullptr, live.size());
    assert(pic.bytes.empty());
    assert(!copied_is(pic, next.data(), next.size()));
    return 0;
}
