#pragma once
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <vector>

namespace interp::picture {

struct Submitted {
    std::vector<uint8_t> bytes;
};

inline void snapshot(Submitted& pic, const void* live, size_t n) {
    const auto* p = static_cast<const uint8_t*>(live);
    if (!p || n == 0) {
        pic.bytes.clear();
        return;
    }
    pic.bytes.assign(p, p + n);
}

inline bool copied_is(const Submitted& pic, const void* submitted, size_t n) {
    if (!submitted || pic.bytes.size() != n) return false;
    return std::memcmp(pic.bytes.data(), submitted, n) == 0;
}

}
