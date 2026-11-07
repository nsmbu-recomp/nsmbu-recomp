

#include "input/rumble.h"

#include <cassert>
#include <cmath>
#include <cstdarg>
#include <cstdio>
#include <cstdlib>
#include <thread>

#include "core/env.h"
void log_msg(const char* fmt, ...) {
    va_list ap;
    va_start(ap, fmt);
    vfprintf(stderr, fmt, ap);
    va_end(ap);
    fputc('\n', stderr);
}

using rumble::Motor;
constexpr uint64_t kFrame = 1000000 / 60;
static bool near(float a, float b) { return std::fabs(a - b) < 1e-4f; }

int main() {
    const uint64_t t0 = 1000000;
    {

        Motor m;
        assert(m.level(t0, kFrame) == 0 && !m.active(t0));
    }
    {

        Motor m;
        const uint8_t on[2] = {0xFF, 0xFF};
        m.gamepad_pattern(on, 16, t0);
        assert(near(m.level(t0, kFrame), 1) && m.active(t0));
        assert(near(m.level(t0 + 10 * Motor::kBitUs, kFrame), 1));
        assert(m.level(t0 + 16 * Motor::kBitUs, kFrame) == 0 && !m.active(t0 + 16 * Motor::kBitUs));
        assert(m.level(t0 + 60 * 1000000ull, kFrame) == 0);
    }
    {

        Motor m;
        const uint8_t bits[16] = {0xF0, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF};
        m.gamepad_pattern(bits, 4, t0);
        assert(m.level(t0, kFrame) == 0 && m.level(t0 + 2 * Motor::kBitUs, kFrame) == 0);
        assert(!m.active(t0 + 4 * Motor::kBitUs));

        const uint8_t first[1] = {0x01};
        m.gamepad_pattern(first, 8, t0);
        assert(m.level(t0, Motor::kBitUs) == 1 && m.level(t0 + Motor::kBitUs, Motor::kBitUs) == 0);

        const uint8_t half[2] = {0x55, 0x55};
        m.gamepad_pattern(half, 16, t0);
        assert(near(m.level(t0, 2 * Motor::kBitUs), 0.5f));

        const uint8_t all[32] = {0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF,
                                 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF};
        m.gamepad_pattern(all, 255, t0);
        assert(m.active(t0 + 119 * Motor::kBitUs) && !m.active(t0 + 120 * Motor::kBitUs));
    }
    {

        Motor m;
        const uint8_t on[15] = {0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF};
        m.gamepad_pattern(on, 120, t0);
        m.gamepad_stop();
        assert(m.level(t0 + kFrame, kFrame) == 0 && !m.active(t0 + kFrame));
        m.gamepad_pattern(on, 120, t0);
        m.gamepad_pattern(on, 0, t0 + kFrame);
        assert(m.level(t0 + kFrame, kFrame) == 0);
        m.gamepad_pattern(on, 120, t0);
        m.gamepad_pattern(nullptr, 120, t0 + kFrame);
        assert(m.level(t0 + kFrame, kFrame) == 0);
        const uint8_t off[15] = {};
        m.gamepad_pattern(on, 120, t0);
        m.gamepad_pattern(off, 120, t0 + kFrame);
        assert(m.level(t0 + 2 * kFrame, kFrame) == 0);

        m.gamepad_pattern(on, 120, t0);
        m.pro_motor(true, t0);
        m.reset();
        assert(m.level(t0, kFrame) == 0 && !m.active(t0));
    }
    {

        Motor m;
        m.pro_motor(true, t0);
        for (uint64_t t = t0; t < t0 + 3000000; t += 33333) {
            m.pro_motor(true, t);
            assert(near(m.level(t, kFrame), 1));
        }
        m.pro_motor(false, t0 + 3000000);
        assert(m.level(t0 + 3000000, kFrame) == 0);

        m.pro_motor(true, t0);
        assert(near(m.level(t0 + Motor::kProStaleUs - 1, kFrame), 1));
        assert(m.level(t0 + Motor::kProStaleUs, kFrame) == 0 && !m.active(t0 + Motor::kProStaleUs));

        const uint8_t on[1] = {0xFF};
        m.gamepad_pattern(on, 8, t0 + Motor::kProStaleUs);
        assert(near(m.level(t0 + Motor::kProStaleUs, kFrame), 1));
    }
    {

        assert(rumble::enabled() == !(env::RUMBLE() && !atoi(env::RUMBLE())));
        rumble::set_enabled(true);
        const uint8_t on[15] = {0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF};
        rumble::gamepad_pattern(0, on, 120);
        assert(rumble::host_level(kFrame) > 0);
        rumble::set_enabled(false);
        assert(rumble::host_level(kFrame) == 0);
        rumble::set_enabled(true);
        assert(rumble::host_level(kFrame) > 0);
        rumble::gamepad_stop(1);
        assert(rumble::host_level(kFrame) > 0);
        rumble::gamepad_stop(0);
        assert(rumble::host_level(kFrame) == 0);
        rumble::pro_motor(0, true);
        assert(rumble::host_level(kFrame) > 0);
        rumble::reset();
        assert(rumble::host_level(kFrame) == 0);

        rumble::gamepad_pattern(0, on, 4);
        std::this_thread::sleep_for(std::chrono::milliseconds(60));
        assert(rumble::host_level(kFrame) == 0);
    }
    printf("rumble: all tests passed\n");
    return 0;
}
