#include "core/runtime.h"
#include "input/input.h"

namespace render {
__attribute__((weak)) bool restart() { return false; }
}

void log_msg(const char*, ...) {}

extern "C" __attribute__((weak)) void nsmbu_fill_course_banner_digits(void) {}
extern "C" __attribute__((weak)) void nsmbu_banner_entry_arm(void) {}
extern "C" __attribute__((weak)) void nsmbu_mark_course_left(void) {}
extern "C" __attribute__((weak)) void nsmbu_directed_open(uint8_t world, uint8_t course) {
    (void)world;
    (void)course;
}
extern "C" __attribute__((weak)) void nsmbu_map_pose_tick(void) {}

__attribute__((weak)) uint32_t guest_call(Cpu* c, uint32_t fn,
                                          std::initializer_list<uint32_t> args) {
    (void)c;
    (void)fn;
    (void)args;
    return 0;
}

namespace dispatch {
__attribute__((weak)) PpcFunc lookup(uint32_t addr) {
    (void)addr;
    return nullptr;
}
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
__attribute__((weak)) PadState read() { return PadState{}; }
}

