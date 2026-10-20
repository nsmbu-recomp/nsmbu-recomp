#include "core/runtime.h"

void log_msg(const char*, ...) {}

extern "C" __attribute__((weak)) void nsmbu_fill_course_banner_digits(void) {}

__attribute__((weak)) uint32_t guest_call(Cpu* c, uint32_t fn,
                                          std::initializer_list<uint32_t> args) {
    (void)c;
    (void)fn;
    (void)args;
    return 0;
}

namespace mem {
__attribute__((weak)) uint32_t host_alloc(uint32_t size, uint32_t align) {
    (void)size;
    (void)align;
    return 0;
}
}

namespace input {
__attribute__((weak)) bool pro_controller() { return false; }
}
