#include "hooks/pause_menu_settings.h"

#include "core/runtime.h"
#include "guest_addr.h"
#include "overlay/overlay.h"

#include <atomic>
#include <cstring>
#include <vector>

extern "C" void f_025F3834_orig(Cpu* c);

namespace pause_menu {

constexpr uint32_t kLayout = 0;
constexpr uint32_t kArray = 0x0C;
constexpr uint32_t kIndex = 0x10;
constexpr uint32_t kMgr = 0x14;
constexpr uint32_t kPad = 0x50;
constexpr uint32_t kDraw = 0x8;
constexpr uint32_t kActive = 0x250;
constexpr uint32_t kMin = 0x204;
constexpr uint32_t kMax = 0x208;
constexpr uint32_t kCount = 0x1AA4;
constexpr uint32_t kName = 0x54;
constexpr uint32_t kFlag = 4;
constexpr uint32_t kSkip = 0x10;
constexpr uint32_t kDecide = 1u << 13;
constexpr int kMaxButtons = 48;

static std::atomic<bool> g_enabled{kStartsEnabled};
static std::atomic<bool> g_open_request{false};
static int g_settings_i = -1;
static int g_story_max = -1;
static uint32_t g_menu = 0;
static bool g_click = false;
static bool g_blank = false;
static uint8_t g_draw = 1;
static uint8_t g_active = 1;
static uint32_t g_saved[kMaxButtons] = {};
static uint8_t g_saved_on[kMaxButtons] = {};

bool enabled() { return g_enabled.load(); }

void set_enabled(bool on) { g_enabled.store(on); }

bool take_open() {
    bool expected = true;
    return g_open_request.compare_exchange_strong(expected, false);
}

static bool guest_obj(uint32_t p) {
    return p >= 0x01000000u && p < 0x80000000u && (p & 3u) == 0;
}

static bool guest_name(uint32_t addr, const char* name) {
    if (!guest_obj(addr)) return false;
    const size_t n = std::strlen(name);
    const uint8_t* p = mem::ptr(addr);
    if (std::memcmp(p, name, n) != 0 || p[n] != 0) return false;
    return true;
}

static bool button_named(uint32_t btn, const char* name) {
    if (!guest_obj(btn)) return false;
    if (guest_name(ld32(btn + kName), name)) return true;
    for (uint32_t off = 0; off < 0x80; off += 4) {
        if (guest_name(ld32(btn + off), name)) return true;
    }
    return false;
}

static uint32_t button_at(uint32_t self, int index) {
    if (index < 0 || index >= kMaxButtons) return 0;
    const uint32_t arr = ld32(self + kArray);
    if (!guest_obj(arr)) return 0;
    return ld32(arr + (uint32_t)index * 4u);
}

static void forget(uint32_t self) {
    if (g_menu != self) return;
    g_menu = 0;
    g_settings_i = -1;
    g_story_max = -1;
    g_click = false;
    g_blank = false;
    std::memset(g_saved_on, 0, sizeof g_saved_on);
}

static void bind_settings(Cpu* c, uint32_t self) {
    if (!g_enabled.load() || g_settings_i >= 0 || g_menu == self) return;
    const uint32_t layout = ld32(self + kLayout);
    const uint32_t arr = ld32(self + kArray);
    const int count = (int)ld32(self + kCount);
    if (!guest_obj(layout) || !guest_obj(arr) || count < 3 || count >= kMaxButtons - 1) return;
    for (int i = 0; i < count; i++) {
        if (!button_named(button_at(self, i), "Pa_SetBtn_00")) continue;
        const uint32_t btn = button_at(self, i);
        st32(btn + kFlag, ld32(btn + kFlag) | kSkip);
        g_settings_i = i;
        g_menu = self;
        return;
    }
    static uint32_t name_ea = 0;
    static uint32_t safe_ea = 0;
    if (!name_ea) {
        name_ea = guest_call(c, GC(0x029F1594u), {16});
        safe_ea = guest_call(c, GC(0x029F1594u), {8});
        if (!guest_obj(name_ea) || !guest_obj(safe_ea)) return;
        std::memcpy(mem::ptr(name_ea), "Pa_SetBtn_00", 13);
        st32(safe_ea, name_ea);
        st32(safe_ea + 4, GD(0x100CED94u));
    }
    const uint32_t btn = guest_call(c, GC(0x026829C0u), {self + kMgr, safe_ea});
    if (!guest_obj(btn)) return;
    const uint32_t neu = guest_call(c, GC(0x029F1594u), {(uint32_t)(count + 1) * 4u});
    if (!guest_obj(neu)) return;
    std::memcpy(mem::ptr(neu), mem::ptr(arr), (size_t)count * 4u);
    st32(neu + (uint32_t)count * 4u, btn);
    st32(self + kArray, neu);
    // Bit 4 of the button word is set on a button the cursor may land on.
    st32(btn + kFlag, ld32(btn + kFlag) | kSkip);
    g_settings_i = count;
    g_menu = self;
    LOG("[pause] settings button is cursor %d", g_settings_i);
}

static void mark_skipped(uint32_t self) {
    if (g_story_max < 0 || g_settings_i < 0) return;
    for (int i = g_story_max + 1; i < g_settings_i; i++) {
        const uint32_t btn = button_at(self, i);
        if (!guest_obj(btn) || g_saved_on[i]) continue;
        const uint32_t flag = ld32(btn + kFlag);
        g_saved[i] = flag;
        g_saved_on[i] = 1;
        st32(btn + kFlag, flag & ~kSkip);
    }
}

static void restore_skipped(uint32_t self) {
    for (int i = 0; i < kMaxButtons; i++) {
        if (!g_saved_on[i]) continue;
        const uint32_t btn = button_at(self, i);
        if (guest_obj(btn)) st32(btn + kFlag, g_saved[i]);
        g_saved_on[i] = 0;
    }
}

static void hide_for_overlay(uint32_t self) {
    const bool open = overlay::is_open();
    if (open && !g_blank) {
        g_draw = ld8(self + kDraw);
        g_active = ld8(self + kActive);
        g_blank = true;
    }
    if (g_blank && open) {
        st8(self + kDraw, 0);
        st8(self + kActive, 0);
        st32(self + kPad, ld32(self + kPad) & ~kDecide);
        return;
    }
    if (g_blank && !open) {
        st8(self + kDraw, g_draw ? g_draw : 1);
        st8(self + kActive, g_active ? g_active : 1);
        g_blank = false;
        g_click = false;
    }
}

static void before(Cpu* c, uint32_t self) {
    if (!g_enabled.load()) return;
    if (!guest_obj(self)) return;
    if (g_menu != 0 && g_menu != self) forget(g_menu);
    bind_settings(c, self);
    if (g_settings_i < 0 || g_menu != self) return;
    hide_for_overlay(self);
    if (g_blank) return;
    const int mn = (int)ld32(self + kMin);
    const int mx = (int)ld32(self + kMax);
    // Create stores 0 and 2 for Continue, Quit, and Change Controller (0x025FCD34).
    if (button_named(button_at(self, mx), "Pa_PlayStyle_00") || (mn == 0 && mx == 2)) g_story_max = mx;
    const bool story = g_story_max >= 0 && mn == 0 && (mx == g_story_max || mx == g_settings_i);
    if (story) {
        st32(self + kMax, (uint32_t)g_settings_i);
        mark_skipped(self);
    }
    const int idx = (int)ld32(self + kIndex);
    const uint32_t pad = ld32(self + kPad);
    if (idx == g_settings_i && (pad & kDecide)) {
        st32(self + kPad, pad & ~kDecide);
        if (!g_click && settings_click_end(true).keep_menu) {
            g_click = true;
            guest_call(c, GC(0x026829A4u), {self + kMgr});
            if (settings_click_end(true).open_overlay) g_open_request.store(true);
        }
    }
}

static void after(uint32_t self) {
    if (!g_enabled.load() || g_menu != self) return;
    restore_skipped(self);
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
    g_settings_i = -1;
    g_story_max = -1;
    g_menu = 0;
    LOG("[pause] settings button added to the pause menu");
}

}

extern "C" void hook_025F3834(Cpu* c) {
    const uint32_t self = c->r[3];
    pause_menu::before(c, self);
    c->r[3] = self;
    f_025F3834_orig(c);
    pause_menu::after(self);
}

// The layout site is shared with the world-map button. This file owns the pause half.
// map_menu_settings.cpp defines site_026730C4 and calls this.
extern "C" void pause_menu_site_026730C4(Cpu* c, uint32_t layout) {
    pause_menu::install_settings_button(c, layout);
}
