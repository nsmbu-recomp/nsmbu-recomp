#include <cstdio>
#include <cstdlib>

extern "C" void* decomp_missing(const char* name) {
    std::fprintf(stderr, "decomp: missing %s\n", name ? name : "(unknown)");
    return nullptr;
}
