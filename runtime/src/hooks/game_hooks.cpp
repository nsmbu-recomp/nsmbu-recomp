

#include "input/input.h"
#include "input/motion/motion.h"
#include "core/runtime.h"

extern "C" void site_0261864C(Cpu* c) {
    if (c->r[3] != 0 && input::pro_controller() && motion::drives_gamepad()) c->r[3] = 0;
}

extern "C" void f_0250ECAC_orig(Cpu* c);
extern "C" void hook_0250ECAC(Cpu* c) {
    int16_t angle = 0;
    if (c->r[3] && motion::platform_tilt(angle)) st16(c->r[3] + 0x130, (uint16_t)angle);
    f_0250ECAC_orig(c);
}
