#include "ppc.h"

#include <stdio.h>
#include <stdlib.h>

static int fails;

static void expect_bits(const char* what, double in, uint64_t want) {
    uint64_t got = f64_as_u64(round25(in));
    if (got != want) {
        fprintf(stderr, "%s: round25(%016llx) = %016llx, want %016llx\n",
                what, (unsigned long long)f64_as_u64(in),
                (unsigned long long)got, (unsigned long long)want);
        fails++;
    }
}

int main(void) {
    expect_bits("one stays", u64_as_f64(0x3ff0000000000000ull), 0x3ff0000000000000ull);
    expect_bits("bits below 25 are cleared", u64_as_f64(0x3ff0000000000001ull), 0x3ff0000000000000ull);
    expect_bits("guard bit rounds up", u64_as_f64(0x3ff0000008000000ull), 0x3ff0000010000000ull);
    expect_bits("quiet nan keeps the quiet bit", u64_as_f64(0x7ff8000000000001ull), 0x7ff8000000000000ull);
    expect_bits("signalling payload only in cleared bits becomes infinity",
                u64_as_f64(0x7ff0000000000001ull), 0x7ff0000000000000ull);
    expect_bits("signalling nan with a kept payload stays signalling",
                u64_as_f64(0x7ff0000010000000ull), 0x7ff0000010000000ull);
    if (f64_as_u64(to_single(1.0)) != f64_as_u64(1.0)) {
        fprintf(stderr, "to_single(1.0) changed the value\n");
        fails++;
    }
    if (fails) return EXIT_FAILURE;
    return EXIT_SUCCESS;
}
