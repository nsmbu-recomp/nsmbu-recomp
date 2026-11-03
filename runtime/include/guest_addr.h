

#pragma once
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>

typedef struct { uint32_t start, end; } GuestChanged;
typedef struct { uint32_t start; int32_t delta; } GuestStep;

#ifdef __cplusplus
extern "C" {
#endif
extern const GuestStep g_guest_code_steps[];
extern const unsigned g_guest_code_step_count;
extern const GuestStep g_guest_data_steps[];
extern const unsigned g_guest_data_step_count;
extern const uint32_t g_guest_code_lo, g_guest_code_hi, g_guest_data_lo, g_guest_data_hi;
extern const GuestChanged g_guest_changed_code[];
extern const unsigned g_guest_changed_code_count;
extern const char g_guest_build_name[];
extern const char g_guest_build_label[];
extern const char g_guest_build_title_id[];
extern const int g_guest_build_title_version;
#ifdef __cplusplus
}
#endif

static inline uint32_t guest_shift(const GuestStep* steps, unsigned n, uint32_t canon) {
    uint32_t out = canon;
    for (unsigned i = 0; i < n && steps[i].start <= canon; i++) out = canon + (uint32_t)steps[i].delta;
    return out;
}

static inline int guest_in_bounds(uint32_t a, uint32_t lo, uint32_t hi) {
    return (!lo && !hi) || (lo <= a && a < hi);
}
static inline int guest_code_valid(uint32_t a) {
    if (!guest_in_bounds(a, g_guest_code_lo, g_guest_code_hi)) return 0;
    unsigned lo = 0, hi = g_guest_changed_code_count;
    while (lo < hi) {
        unsigned mid = lo + (hi - lo) / 2;
        const GuestChanged* r = &g_guest_changed_code[mid];
        if (a <= r->start) hi = mid;
        else if (a >= r->end) lo = mid + 1;
        else return 0;
    }
    return 1;
}
static inline int guest_data_valid(uint32_t a) {
    return guest_in_bounds(a, g_guest_data_lo, g_guest_data_hi);
}
static inline void guest_bad_address(uint32_t a, const char* kind) {
    fprintf(stderr, "[%s] unmapped canonical %s address %08X\n", g_guest_build_name, kind, a);
    abort();
}

static inline uint32_t guest_code(uint32_t canon) {
    if (!guest_code_valid(canon)) guest_bad_address(canon, "code");
    return guest_shift(g_guest_code_steps, g_guest_code_step_count, canon);
}

static inline uint32_t guest_data(uint32_t canon) {
    if (!guest_data_valid(canon)) guest_bad_address(canon, "data");
    return guest_shift(g_guest_data_steps, g_guest_data_step_count, canon);
}

static inline int guest_code_range_ok(uint32_t canon_lo, uint32_t canon_hi) {
    if (!guest_code_valid(canon_lo) || !guest_code_valid(canon_hi)) return 0;
    return (int32_t)(guest_code(canon_hi) - guest_code(canon_lo)) == (int32_t)(canon_hi - canon_lo);
}

#define GC(a) guest_code((uint32_t)(a))
#define GD(a) guest_data((uint32_t)(a))
