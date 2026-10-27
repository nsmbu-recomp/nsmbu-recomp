#include "gfx/aspect.h"
#include "gfx/aspect_view.h"
#include "gfx/bg_scroll_wide.h"
#include "ppc.h"

extern "C" void f_029A6170_orig(Cpu* c);

namespace {

constexpr uint32_t kMgrMin = 0x10000000u;
constexpr uint32_t kWidth = 0x68u;
constexpr uint32_t kLeft = 0x70u;
constexpr uint32_t kCenterX = 0x74u;
constexpr uint32_t kRight = 0x78u;
constexpr uint32_t kBgLeft = 0x88u;
constexpr uint32_t kBgRight = 0x90u;
constexpr uint32_t kCourseInfo = 0x101D18ECu;
constexpr uint32_t kCourseKind = 0x11u;
constexpr uint32_t kTitleCourse = 0x26u;
constexpr float kKxEpsilon = 1.0001f;

bool title_course() {
    uint32_t info = ld32(kCourseInfo);
    if (info < kMgrMin)
        return false;
    return ld8(info + kCourseKind) == kTitleCourse;
}

}

extern "C" void hook_029A6170(Cpu* c) {
    uint32_t mgr = c->r[3];
    f_029A6170_orig(c);
    if (mgr < kMgrMin)
        return;
    if (aspect::mode() == aspect::kOriginal)
        return;
    float aspect = aspect::game();
    float kx, ky;
    aspect::view::factors(aspect, kx, ky);
    if (kx <= kKxEpsilon)
        return;
    float center = (float)ldf32(mgr + kCenterX);
    if (!title_course()) {
        float left = (float)ldf32(mgr + kLeft);
        float right = (float)ldf32(mgr + kRight);
        aspect::scroll::Edges scalar = aspect::scroll::widen(left, right, center, kx);
        stf32(mgr + kLeft, scalar.left);
        stf32(mgr + kRight, scalar.right);
        stf32(mgr + kWidth, scalar.width);
    }
    float bg_left = (float)ldf32(mgr + kBgLeft);
    float bg_right = (float)ldf32(mgr + kBgRight);
    aspect::scroll::Edges bg = aspect::scroll::widen(bg_left, bg_right, center, kx);
    stf32(mgr + kBgLeft, bg.left);
    stf32(mgr + kBgRight, bg.right);
}
