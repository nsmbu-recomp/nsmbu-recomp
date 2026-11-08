#include "hooks/pause_menu_settings.h"

#include "core/runtime.h"
#include "guest_addr.h"
#include "overlay/overlay.h"

#include <atomic>
#include <cstring>
#include <vector>

extern "C" void f_025F3834_orig(Cpu* c);

namespace pause_menu {

constexpr uint32_t kIndex = 0x10;
constexpr uint32_t kButtons = 0x0C;
constexpr uint32_t kMgr = 0x14;
constexpr uint32_t kPad = 0x50;
constexpr uint32_t kDraw = 0x8;
constexpr uint32_t kActive = 0x250;
constexpr uint32_t kShown = 0xA90;
constexpr uint32_t kPause = 0x101D830Cu;
constexpr uint32_t kCursorInst = 0x101D17A8u;
constexpr uint32_t kCursorDraw = 0x2C5F4u;
constexpr uint32_t kMin = 0x204;
constexpr uint32_t kMax = 0x208;
constexpr uint32_t kName = 0x54;
constexpr uint32_t kState = 0x220;
constexpr uint32_t kChangeState = 0x029C4AF0u;
constexpr uint32_t kMenuState = 0x10216DB4u;
constexpr uint32_t kMode = 0x1C;
constexpr uint32_t kMenuAnim = 0x10216E24u;
constexpr uint32_t kPlayAnim = 0x02671680u;
constexpr uint32_t kWindowMgr = 0x101D15E8u;

static std::atomic<bool> g_enabled{kStartsEnabled};
static std::atomic<bool> g_open_request{false};
static bool g_blank = false;
static bool g_armed = false;
static bool g_opened = false;
static bool g_saw_overlay = false;
static int g_age = 0;
static int g_settle = 0;
static int g_index = 2;
static bool g_anim = false;
static uint8_t g_draw = 1;
static uint8_t g_active = 1;
static uint8_t g_shown = 1;
static uint8_t g_cursor = 1;

bool enabled() { return g_enabled.load(); }

void set_enabled(bool on) { g_enabled.store(on); }

bool take_open() {
    bool expected = true;
    return g_open_request.compare_exchange_strong(expected, false);
}

static bool guest_obj(uint32_t p) {
    return p >= 0x01000000u && p < 0x80000000u && (p & 3u) == 0;
}

static void hide_cursor(bool hide) {
    const uint32_t inst = ld32(kCursorInst);
    if (!guest_obj(inst)) return;
    if (hide) {
        const uint8_t cur = ld8(inst + kCursorDraw);
        if (cur != 0) g_cursor = cur;
        st8(inst + kCursorDraw, 0);
        return;
    }
    st8(inst + kCursorDraw, g_cursor != 0 ? g_cursor : 1);
}

static void hide_for_overlay(uint32_t self) {
    const bool open = overlay::is_open();
    if (open && !g_blank) {
        g_draw = ld8(self + kDraw);
        g_active = ld8(self + kActive);
        if (!g_armed) g_shown = ld8(self + kShown);
        g_blank = true;
    }
    if (g_blank && open) {
        st8(self + kDraw, 0);
        st8(self + kActive, 0);
        st8(self + kShown, 0);
        st32(self + kPad, pause_settings_idle_pad(ld32(self + kPad)));
        hide_cursor(true);
        return;
    }
    if (g_blank && !open) {
        st8(self + kDraw, g_draw ? g_draw : 1);
        st8(self + kActive, g_active ? g_active : 1);
        st8(self + kShown, g_shown ? g_shown : 1);
        hide_cursor(false);
        g_blank = false;
    }
}

static bool button_name_is(uint32_t button, const char* name) {
    if (!guest_obj(button)) return false;
    const uint32_t p = ld32(button + kName);
    if (!guest_obj(p)) return false;
    const size_t n = std::strlen(name);
    return std::memcmp(mem::ptr(p), name, n + 1) == 0;
}

static bool settings_row(uint32_t self) {
    const int mn = (int)ld32(self + kMin);
    const int mx = (int)ld32(self + kMax);
    const int idx = (int)ld32(self + kIndex);
    if (story_settings_row(mn, mx, idx)) return true;
    const uint32_t arr = ld32(self + kButtons);
    if (!guest_obj(arr) || idx < 0 || idx > 40) return false;
    return button_name_is(ld32(arr + (uint32_t)idx * 4), "Pa_PlayStyle_00");
}

static uint32_t current_state(Cpu* c, uint32_t self) {
    const uint32_t st = ld32(self + kState + 8);
    if (!guest_obj(st)) return 0;
    const uint32_t vt = ld32(st);
    if (!guest_obj(vt)) return 0;
    const uint32_t fn = ld32(vt + 0x14);
    if (!guest_obj(fn)) return 0;
    return guest_call(c, fn, {st});
}

static bool controller_state(uint32_t id) {
    return pause_controller_state(id);
}

static bool in_controller_window(Cpu* c, uint32_t self) {
    if (controller_state(ld32(self + kState + 4)) || controller_state(ld32(self + kState + 8))) return true;
    return controller_state(current_state(c, self));
}

static void close_controller_window() {
    const uint32_t mgr = ld32(kWindowMgr);
    if (!guest_obj(mgr) || ld32(mgr + 0x1AA0) != 5) return;
    st32(mgr + 0x1AA0, 0);
    st8(mgr + 0x1AB8, 0);
    st8(mgr + 0x1ABD, 0);
    st8(mgr + 0x22B4, 0);
}

static void show_button_panel(Cpu* c, uint32_t self) {
    const uint32_t layout = ld32(self);
    if (!guest_obj(layout)) return;
    guest_call(c, GC(kPlayAnim), {layout, 2u, kMenuAnim, 0u});
}

static void leave_controller_window(Cpu* c, uint32_t self) {
    st32(self + kPad, pause_settings_idle_pad(ld32(self + kPad)));
    // 3 at +0x1C is the decided button. The next step would enter the controller window.
    st32(self + kMode, 0);
    st8(self + 0xA91, 0);
    st8(self + 0xA93, 0);
    st8(self + 0x251, 0);
    const uint32_t cur = ld32(self + kState + 8);
    const uint32_t next = ld32(self + kState + 4);
    const uint32_t id = current_state(c, self);
    const uint32_t inside = guest_obj(cur) ? ld32(cur + 8) : 0;
    const bool controller = controller_state(cur) || controller_state(next) || controller_state(id) ||
                            controller_state(inside);
    if (!controller) return;
    guest_call(c, GC(kChangeState), {self + kState, kMenuState});
    st32(self + kIndex, (uint32_t)g_index);
    st32(self + kMode, 0);
    guest_call(c, GC(0x025BE1F0u), {self});
    if (!g_anim) {
        g_anim = true;
        show_button_panel(c, self);
    }
    close_controller_window();
}

static void disarm() {
    g_armed = false;
    g_opened = false;
    g_saw_overlay = false;
    g_age = 0;
    g_settle = 0;
    g_anim = false;
}

// 0x025FD500 has already stepped the course pause object before this runs.
// That step can enter the controller window. Keep the pause menu there instead.
static void intercept_course_pause(Cpu* c) {
    if (!g_enabled.load()) return;
    const uint32_t self = ld32(kPause);
    if (!guest_obj(self)) return;
    const bool open = overlay::is_open();
    if (g_armed && g_saw_overlay && !open && g_settle == 0)
        g_settle = pause_settings_settle(true, false);
    if (open) g_saw_overlay = true;
    hide_for_overlay(self);
    if (g_armed) {
        leave_controller_window(c, self);
        if (!g_opened && !open && pause_settings_ready(g_age) && settings_click_end(true).open_overlay) {
            g_opened = true;
            g_open_request.store(true);
            LOG("[pause] Settings opens the overlay");
        } else if (!g_opened && !open) {
            g_age++;
        }
        if (g_saw_overlay && !open) {
            if (g_settle > 0) g_settle--;
            if (g_settle == 0) disarm();
        }
        return;
    }
    if (g_blank) return;
    if (!pause_menu_shown(ld8(self + kActive), ld8(self + kShown))) return;
    if (!settings_row(self)) return;
    const uint32_t pad = ld32(self + kPad);
    const bool back = (pad & (1u << 14)) != 0 && !settings_confirm_pad(pad);
    if (back || !settings_click_end(true).keep_menu) return;
    if (!in_controller_window(c, self) && !settings_confirm_pad(pad)) return;
    g_index = (int)ld32(self + kIndex);
    g_shown = ld8(self + kShown);
    if (g_shown == 0) g_shown = 1;
    g_armed = true;
    g_age = 0;
    leave_controller_window(c, self);
}

static uint32_t flyt_slot(uint32_t base, int bytes) {
    for (int off = 0; off + 4 <= bytes; off += 4) {
        const uint32_t p = ld32(base + (uint32_t)off);
        if (!guest_obj(p)) continue;
        if (ld32(p) == 0x464C5954u) return base + (uint32_t)off;
        if (!guest_obj(p + 0x20)) continue;
        for (uint32_t inner = 0; inner < 0x20; inner += 4) {
            const uint32_t q = ld32(p + inner);
            if (guest_obj(q) && ld32(q) == 0x464C5954u) return p + inner;
        }
    }
    return 0;
}

static void restore_build_regs(Cpu* c, const uint32_t* saved, uint32_t resource) {
    c->r[0] = saved[0];
    c->r[3] = resource;
    for (int i = 4; i <= 12; i++) c->r[i] = saved[i - 2];
}

static void install_settings_button(Cpu* c, uint32_t layout) {
    if (!g_enabled.load()) return;
    const uint32_t saved[11] = {
        c->r[0], c->r[3], c->r[4], c->r[5], c->r[6], c->r[7], c->r[8], c->r[9], c->r[10], c->r[11], c->r[12]};
    const uint32_t resource = c->r[3];
    uint32_t slot = 0;
    if (guest_obj(resource) && ld32(resource) == 0x464C5954u) slot = 0;
    else if (guest_obj(layout)) {
        const uint32_t acc = ld32(layout + 0xE88);
        if (guest_obj(acc)) {
            slot = flyt_slot(acc, 0x40);
            const uint32_t sentinel = acc + 8;
            uint32_t node = ld32(sentinel);
            for (int i = 0; i < 80 && !slot; i++) {
                if (!guest_obj(node) || node == sentinel) break;
                slot = flyt_slot(node, 0x80);
                const uint32_t next = ld32(node);
                if (next == node) break;
                node = next;
            }
        }
    }
    const bool direct = guest_obj(resource) && ld32(resource) == 0x464C5954u;
    const uint32_t flyt = direct ? resource : (slot ? ld32(slot) : 0);
    if (!guest_obj(flyt) || ld32(flyt) != 0x464C5954u) return;
    const uint32_t bytes = ld32(flyt + 0xC);
    if (bytes < 0x20 || bytes > 0x100000) return;
    if (!pause_menu_flyt(mem::ptr(flyt), bytes)) return;
    std::vector<uint8_t> file(bytes);
    std::memcpy(file.data(), mem::ptr(flyt), bytes);
    if (!patch_pause_flyt(file)) {
        restore_build_regs(c, saved, resource);
        return;
    }
    const uint32_t neu = guest_call(c, GC(0x029F1594u), {(uint32_t)file.size()});
    if (!guest_obj(neu)) {
        restore_build_regs(c, saved, resource);
        return;
    }
    std::memcpy(mem::ptr(neu), file.data(), file.size());
    if (direct) restore_build_regs(c, saved, neu);
    else {
        st32(slot, neu);
        restore_build_regs(c, saved, resource);
    }
    LOG("[pause] Change Controller is the Settings button");
}

}

extern "C" void hook_025F3834(Cpu* c) {
    const uint32_t self = c->r[3];
    pause_menu::intercept_course_pause(c);
    c->r[3] = self;
    f_025F3834_orig(c);
    c->r[3] = self;
}

// The layout site is shared with the world-map button. This file owns the pause half.
// map_menu_settings.cpp defines site_026730C4 and calls this.
extern "C" void pause_menu_site_026730C4(Cpu* c, uint32_t layout) {
    pause_menu::install_settings_button(c, layout);
}
