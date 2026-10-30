#pragma once

#include "core/env.h"
#include "core/runtime.h"
#include "guest_addr.h"
#include "ppc.h"

#include <atomic>
#include <cstdint>
#include <cstdlib>
#include <cstddef>
#include <deque>
#include <mutex>
#include <vector>

namespace frame_dump {

inline std::vector<uint64_t> parse_list(const char* e) {
    std::vector<uint64_t> frames;
    if (!e) return frames;
    while (*e) {
        char* end = nullptr;
        const unsigned long long start = strtoull(e, &end, 10);
        if (end == e) break;
        e = end;
        if (*e == '-') {
            const unsigned long long stop = strtoull(e + 1, &end, 10);
            if (end == e + 1) break;
            e = end;
            if (stop >= start && stop - start < 10000)
                for (unsigned long long i = start; i <= stop; i++) frames.push_back(i);
        } else {
            frames.push_back(start);
        }
        while (*e == ',') e++;
    }
    return frames;
}

inline bool title_characters() {
    const uint32_t info = ld32(GD(0x101D18EC));
    if (info < 0x10000000u) return false;
    if (ld8(info + 0x11u) != 0x26u) return false;
    const uint32_t mgr = ld32(GD(0x101E6994));
    if (mgr < 0x10000000u) return false;
    const uint32_t actor = ld32(mgr + 0x20u);
    return actor >= 0x10000000u;
}

inline bool picture_bright(const uint8_t* rgba, int width, int height) {
    if (!rgba || width < 32 || height < 32) return false;
    auto column = [&](int x) {
        int lit = 0;
        int samples = 0;
        for (int y = 0; y < height; y += 24) {
            const uint8_t* p = rgba + ((size_t)y * (size_t)width + (size_t)x) * 4;
            if (p[0] + p[1] + p[2] > 48) lit++;
            samples++;
        }
        return samples > 0 && lit * 2 > samples;
    };
    return column(8) && column(width - 9);
}

struct TitleBurst {
    int left = -1;
    int written = 0;
};

inline TitleBurst& title_burst() {
    static TitleBurst burst;
    return burst;
}

inline std::atomic<bool>& title_lit_flag() {
    static std::atomic<bool> lit{false};
    return lit;
}

inline void title_note_lit() { title_lit_flag().store(true, std::memory_order_relaxed); }
inline bool title_is_lit() { return title_lit_flag().load(std::memory_order_relaxed); }

inline void title_arm() {
    TitleBurst& burst = title_burst();
    if (burst.left >= 0) return;
    burst.left = env::DUMP_TITLE.integer_or(0);
    if (burst.left > 0)
        LOG("[gfx] title picture is up: dumping %d frame%s", burst.left, burst.left == 1 ? "" : "s");
}

inline bool title_due(int& index) {
    TitleBurst& burst = title_burst();
    if (burst.left <= 0) return false;
    index = burst.written++;
    burst.left--;
    return true;
}

struct PresentMark {
    bool blend = false;
    float t = 1.f;
};

inline std::deque<PresentMark>& present_marks() {
    static std::deque<PresentMark> q;
    return q;
}

inline std::mutex& present_mark_mu() {
    static std::mutex mu;
    return mu;
}

inline void push_mark(bool blend, float t) {
    std::lock_guard<std::mutex> lock(present_mark_mu());
    present_marks().push_back({blend, t});
}

inline PresentMark take_mark() {
    std::lock_guard<std::mutex> lock(present_mark_mu());
    if (present_marks().empty()) return {};
    PresentMark mark = present_marks().front();
    present_marks().pop_front();
    return mark;
}

inline PresentMark& current_mark() {
    static PresentMark mark;
    return mark;
}

}
