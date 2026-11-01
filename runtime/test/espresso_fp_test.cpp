#include <cstdint>
#include <cstdio>
#include <cstring>

extern "C" {
double ppc_fres(double);
double ppc_frsqrte(double);
double ppc_fres_ref(double);
double ppc_frsqrte_ref(double);
}

static double from_bits(uint64_t bits) {
    double d;
    memcpy(&d, &bits, 8);
    return d;
}

static uint64_t to_bits(double d) {
    uint64_t u;
    memcpy(&u, &d, 8);
    return u;
}

static int check(const char* name, uint64_t in_bits, double (*fn)(double), double (*fn_ref)(double)) {
    double in = from_bits(in_bits);
    uint64_t got = to_bits(fn(in));
    uint64_t ref = to_bits(fn_ref(in));
    if (got != ref) {
        fprintf(stderr, "%s in=%016llx got=%016llx ref=%016llx\n",
                name, (unsigned long long)in_bits, (unsigned long long)got, (unsigned long long)ref);
        return 1;
    }
    return 0;
}

static int check_both(uint64_t in_bits) {
    int err = 0;
    err += check("ppc_fres", in_bits, ppc_fres, ppc_fres_ref);
    err += check("ppc_frsqrte", in_bits, ppc_frsqrte, ppc_frsqrte_ref);
    return err;
}

int main() {
    int err = 0;
    const uint64_t specials[] = {
        0x0000000000000000ull,
        0x8000000000000000ull,
        0x7ff0000000000000ull,
        0xfff0000000000000ull,
        0x7ff8000000000000ull,
        0x7ff0123456789abcull,
    };
    for (uint64_t bits : specials) {
        err += check_both(bits);
    }
    err += check_both(0x3ff0000000000000ull);
    err += check_both(0xbff0000000000000ull);
    err += check_both(0x4000000000000000ull);
    err += check_both(0x3fe0000000000000ull);
    err += check_both(0x8000000000000001ull);
    err += check_both(0x0000000000000001ull);
    err += check_both(0xc0934a3b00000000ull);
    const unsigned exps[] = {0, 1, 1021, 1022, 1023, 2046, 0x7ff};
    const uint64_t mantissas[] = {0, 1, 0x123456789abull, 0x000fffffffffffffull};
    for (unsigned e : exps) {
        for (uint64_t m : mantissas) {
            err += check_both(((uint64_t)e << 52) | (m & 0x000fffffffffffffull));
            err += check_both(0x8000000000000000ull | ((uint64_t)e << 52) | (m & 0x000fffffffffffffull));
        }
    }
    for (unsigned idx = 0; idx < 32; ++idx) {
        const unsigned fres_steps[] = {0, 1, 512, 0x3ff};
        for (unsigned step : fres_steps) {
            uint64_t bits = ((uint64_t)1023 << 52) | ((uint64_t)idx << 47) | ((uint64_t)step << 37);
            err += check("ppc_fres", bits, ppc_fres, ppc_fres_ref);
        }
        const unsigned frsqrte_steps[] = {0, 1, 1024, 0x7ff};
        for (unsigned step : frsqrte_steps) {
            uint64_t bits = ((uint64_t)1023 << 52) | ((uint64_t)idx << 48) | ((uint64_t)step << 37);
            err += check("ppc_frsqrte", bits, ppc_frsqrte, ppc_frsqrte_ref);
        }
    }
    uint64_t lcg = 0x123456789abcdef0ull;
    for (int i = 0; i < 10000; ++i) {
        lcg = lcg * 6364136223846793005ull + 1442695040888963407ull;
        err += check_both(lcg);
    }
    return err != 0;
}
