#include "hooks/pause_menu_settings.h"

#include "core/runtime.h"
#include "core/write_watch.h"
#include "guest_addr.h"
#include "overlay/overlay.h"

#include <atomic>
#include <cstring>
#include <vector>

extern "C" void f_025F3834_orig(Cpu* c);

namespace pause_menu {

constexpr uint32_t kIndex = 0x10;
constexpr uint32_t kCursorIndex = 0x4C;
constexpr uint32_t kButtons = 0x0C;
constexpr uint32_t kMgr = 0x14;
constexpr uint32_t kPad = 0x50;
constexpr uint32_t kDraw = 0x8;
constexpr uint32_t kActive = 0x250;
constexpr uint32_t kShown = 0xA90;
constexpr uint32_t kPause = 0x101D830Cu;
constexpr uint32_t kHideCursor = 0x024C67B4u;
constexpr uint32_t kCursorInst = 0x101D17A8u;
constexpr uint32_t kCursorDraw = 0x2C5F4u;
constexpr uint32_t kMin = 0x204;
constexpr uint32_t kMax = 0x208;
constexpr uint32_t kName = 0x54;
constexpr uint32_t kState = 0x220;
constexpr uint32_t kChangeState = 0x029C4AF0u;
constexpr uint32_t kPlaceCursor = 0x025BE1F0u;
constexpr uint32_t kSelectButton = 0x02682990u;
constexpr uint32_t kMenuState = 0x10216DB4u;
constexpr uint32_t kMode = 0x1C;
constexpr uint32_t kPressByte = 0x84;
constexpr uint32_t kPressAnim = 0x30;
constexpr uint32_t kDecideAnim = 0x6C;
constexpr uint32_t kAnimFrame = 0xC;
constexpr uint32_t kAnimEnable = 0x10;
constexpr uint32_t kAnimVt = 0x14;
constexpr uint32_t kAnimSpeed = 0x24;
constexpr uint32_t kAnimFlags = 0x2C;
constexpr uint32_t kAnimUpdate = 0x1C;
constexpr uint32_t kShowAnim = 0x02676C24u;
constexpr uint32_t kBindAnim = 0x02AB62ECu;
constexpr uint32_t kPressMute = 0x98;
constexpr uint32_t kButtonVt = 0x1C;
constexpr uint32_t kPressSlot = 0x24;
constexpr uint32_t kLayoutAnim = 0x02671680u;
constexpr uint32_t kAnimPlay = 0x02672A4Cu;
constexpr uint32_t kInAnim = 0x10216E14u;
constexpr uint32_t kGetAnim = 0x02671AF4u;
constexpr uint32_t kMgrStep = 0x8;
constexpr uint32_t kMgrButton = 0x18;
constexpr uint32_t kWindowMgr = 0x101D15E8u;
constexpr uint32_t kLayoutObj = 0x10;
constexpr uint32_t kRootPane = 0x0C;
constexpr uint32_t kPaneFlags = 0x44;
constexpr uint32_t kPaneVisible = 1u;
constexpr uint32_t kControllerPane0 = 0x25C;
constexpr uint32_t kControllerPane1 = 0x260;

static std::atomic<bool> g_enabled{kStartsEnabled};
static std::atomic<bool> g_open_request{false};
static bool g_blank = false;
static bool g_armed = false;
static bool g_opened = false;
static bool g_saw_overlay = false;
static int g_settle = 0;
static int g_index = 2;
static bool g_pressed = false;
static bool g_replay_ready = true;
static bool g_left = false;
static bool g_held = false;
static bool g_restore_panes = false;
struct SavedPane {
    uint32_t pane;
    uint8_t flags;
};
static SavedPane g_saved_panes[160];
static int g_saved_count = 0;
static uint32_t g_menu_root = 0;
static bool g_page_closed = false;
static bool g_block_back = false;
static bool g_restore_menu = false;
static bool g_cursor_placed = false;
static bool g_handed = false;
static bool g_in_started = false;
static int g_held_frames = 0;
static int g_mode_seen = 0;
static bool g_quit_dim = false;
static uint32_t g_quit_flags = 0;
struct QuitAnim {
    uint32_t anim;
    float frame;
    float speed;
    uint8_t enable;
    uint32_t flags;
};
static QuitAnim g_quit_anims[8];
static int g_quit_anim_n = 0;
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

// hide(2) clears the pause slot. The draw byte at +0x2C5F4 is a separate gate.
static void hide_pause_cursor(Cpu* c) {
    hide_cursor(true);
    const uint32_t inst = ld32(kCursorInst);
    if (!guest_obj(inst)) return;
    guest_call(c, GC(kHideCursor), {inst, 2});
}

static void undo_back_out(Cpu* c, uint32_t self) {
    st32(self + kMode, 0);
    guest_call(c, GC(kChangeState), {self + kState, kMenuState});
    st8(self + kDraw, g_draw ? g_draw : 1);
    st8(self + kActive, g_active ? g_active : 1);
    st8(self + kShown, g_shown ? g_shown : 1);
    hide_pause_cursor(c);
    g_cursor_placed = false;
    g_held_frames = 0;
    g_handed = false;
    g_in_started = false;
    g_restore_menu = true;
    g_left = false;
}

static void play_menu_in(Cpu* c, uint32_t self) {
    const uint32_t layout = ld32(self);
    if (!guest_obj(layout)) return;
    // r4 = 0 plays the anim named In. That is the fade used when the pause menu opens.
    // animPlay applies the first frame before the draw, so the menu does not pop in.
    guest_call(c, GC(kLayoutAnim), {layout, 0, kInAnim, 0});
    guest_call(c, GC(kAnimPlay), {layout, 0xE, 0});
}

static void set_pane_visible(uint32_t pane, bool visible);
static uint32_t layout_root(uint32_t self);

// Out ends with the button list shrunk away. Frame 0 of that anim is the list.
// Hold that frame after the overlay closes so the controller page does not stay up.
// The open menu enter does this after it stores +0x10.
// 0x025BE1F0 aims the cursor at that row. 0x02682990 selects the button.
static void place_menu_cursor(Cpu* c, uint32_t self) {
    st32(self + kIndex, (uint32_t)g_index);
    guest_call(c, GC(kPlaceCursor), {self});
    const uint32_t arr = ld32(self + kButtons);
    if (!guest_obj(arr)) return;
    const uint32_t button = ld32(arr + (uint32_t)g_index * 4u);
    if (!guest_obj(button)) return;
    guest_call(c, GC(kSelectButton), {self + kMgr, button});
}

static void remember_menu(uint32_t self) {
    if (g_held) return;
    const uint8_t draw = ld8(self + kDraw);
    const uint8_t active = ld8(self + kActive);
    const uint8_t shown = ld8(self + kShown);
    if (draw != 0) g_draw = draw;
    if (active != 0) g_active = active;
    if (shown != 0) g_shown = shown;
    g_held = true;
}

static void set_pane_visible(uint32_t pane, bool visible) {
    if (!guest_obj(pane)) return;
    const uint8_t flags = ld8(pane + kPaneFlags);
    st8(pane + kPaneFlags, visible ? (uint8_t)(flags | kPaneVisible) : (uint8_t)(flags & ~kPaneVisible));
}

static uint32_t layout_root(uint32_t self) {
    const uint32_t layout = ld32(self);
    if (!guest_obj(layout)) return 0;
    const uint32_t obj = ld32(layout + kLayoutObj);
    if (!guest_obj(obj)) return 0;
    return ld32(obj + kRootPane);
}

static void remember_pane(uint32_t pane) {
    if (!guest_obj(pane) || !wwatch::readable(pane + kPaneFlags, 1)) return;
    for (int i = 0; i < g_saved_count; i++)
        if (g_saved_panes[i].pane == pane) return;
    if (g_saved_count >= (int)(sizeof g_saved_panes / sizeof g_saved_panes[0])) return;
    g_saved_panes[g_saved_count].pane = pane;
    g_saved_panes[g_saved_count].flags = ld8(pane + kPaneFlags);
    g_saved_count++;
}

static void save_pause_tree(uint32_t root) {
    if (g_restore_panes || g_blank) return;
    g_saved_count = 0;
    remember_pane(root);
    uint32_t stack[48];
    int n = 0;
    if (wwatch::readable(root + 0x14, 4)) stack[n++] = ld32(root + 0x14);
    for (int step = 0; step < 160 && n > 0; step++) {
        const uint32_t pane = stack[--n];
        if (!guest_obj(pane) || pane == root + 0x14) continue;
        remember_pane(pane);
        if (wwatch::readable(pane + 0x14, 4)) {
            const uint32_t child = ld32(pane + 0x14);
            if (guest_obj(child) && child != pane + 0x14 && n < 48) stack[n++] = child;
        }
        if (!wwatch::readable(pane, 4)) continue;
        const uint32_t next = ld32(pane);
        if (guest_obj(next) && next != pane && n < 48) stack[n++] = next;
    }
}

static void restore_pause_tree() {
    if (g_saved_count == 0) {
        static int warned = 0;
        if (warned++ == 0) LOG("[pause] no saved pause items");
        return;
    }
    for (int i = 0; i < g_saved_count; i++) {
        const uint32_t pane = g_saved_panes[i].pane;
        if (!guest_obj(pane) || !wwatch::readable(pane + kPaneFlags, 1)) continue;
        st8(pane + kPaneFlags, g_saved_panes[i].flags);
    }
}

static bool pane_name_is(uint32_t pane, const char* prefix) {
    if (!wwatch::readable(pane + 0x80, 16)) return false;
    const char* name = (const char*)mem::ptr(pane + 0x80);
    return std::strncmp(name, prefix, std::strlen(prefix)) == 0;
}

// Buttons sit under the pause root. The draw root is that pane, not the pointer from getRootPane.
static bool pause_layout_root(uint32_t root) {
    static uint32_t found[8] = {};
    static int found_n = 0;
    static uint32_t miss[12] = {};
    static int misses = 0;
    for (int i = 0; i < found_n; i++)
        if (found[i] == root) return true;
    for (int i = 0; i < misses; i++)
        if (miss[i] == root) return false;
    bool hit = false;
    uint32_t stack[48];
    int n = 0;
    if (wwatch::readable(root + 0x14, 4)) stack[n++] = ld32(root + 0x14);
    const uint32_t sentinel_base = root;
    for (int step = 0; step < 80 && n > 0 && !hit; step++) {
        const uint32_t pane = stack[--n];
        if (!guest_obj(pane) || pane == sentinel_base + 0x14) continue;
        if (pane_name_is(pane, "Pa_") || pane_name_is(pane, "N_Btn") || pane_name_is(pane, "T_Btn")) {
            char name[25] = {};
            std::memcpy(name, mem::ptr(pane + 0x80), 24);
            LOG("[pause] pause layout %08X via %.24s", root, name);
            hit = true;
            break;
        }
        if (wwatch::readable(pane + 0x14, 4)) {
            const uint32_t child = ld32(pane + 0x14);
            const uint32_t sentinel = pane + 0x14;
            if (guest_obj(child) && child != sentinel && n < 48) stack[n++] = child;
        }
        if (!wwatch::readable(pane, 4)) continue;
        const uint32_t next = ld32(pane);
        if (guest_obj(next) && next != pane && n < 48) stack[n++] = next;
    }
    if (hit) {
        if (found_n < 8) found[found_n++] = root;
    } else if (misses < 12) {
        miss[misses++] = root;
        LOG("[pause] layout %08X has no pause button", root);
    }
    return hit;
}

bool skip_pane(uint32_t pane) {
    if (!guest_obj(pane)) return false;
    uint32_t top = pane;
    for (int i = 0; i < 24 && guest_obj(top); i++) {
        if (!wwatch::readable(top + kRootPane, 4)) break;
        const uint32_t parent = ld32(top + kRootPane);
        if (!guest_obj(parent)) break;
        top = parent;
    }
    if (!pause_layout_root(top)) return false;
    if (g_menu_root == 0) g_menu_root = top;
    const bool menu_root = top == g_menu_root;
    // The button list is on screen until the overlay hides this layout.
    // Save that list once per draw of the root. Closing the overlay puts those flags back.
    if (menu_root && pane == top && !g_blank && !g_restore_panes) save_pause_tree(top);
    if (g_restore_panes && menu_root && pane == top) restore_pause_tree();
    const bool hidden = g_armed && g_blank;
    return pause_settings_skips_pause_root(menu_root, g_page_closed, hidden);
}

// 0x10216D94 sets the visible bit on these two panes and sets +0x8 back to 1.
// That is the controller page. The draw byte does not cover it.
static void hide_controller_panes(uint32_t self) {
    if (pause_settings_controller_pane_visible(false, true)) return;
    set_pane_visible(ld32(self + kControllerPane0), false);
    set_pane_visible(ld32(self + kControllerPane1), false);
}

static void hide_for_overlay(Cpu* c, uint32_t self, bool playing_out, bool controller_page) {
    const bool open = overlay::is_open();
    if (g_armed) hide_controller_panes(self);
    // Closing the overlay wins over a controller-page state that is still current.
    // In runs first, then the pause items draw again. The controller panes stay hidden.
    if (g_blank && !open && pause_menu_fades_in(true, false) && !playing_out) {
        if (!g_in_started && !pause_settings_pins_out(g_handed)) {
            play_menu_in(c, self);
            g_in_started = true;
        }
        restore_pause_tree();
        g_restore_panes = true;
        hide_controller_panes(self);
        set_pane_visible(layout_root(self), true);
        st8(self + kDraw, g_draw ? g_draw : 1);
        st8(self + kActive, g_active ? g_active : 1);
        st8(self + kShown, g_shown ? g_shown : 1);
        g_blank = false;
        g_held = false;
        return;
    }
    if (!pause_settings_hides_menu_items(open, controller_page, playing_out)) return;
    if (!g_held) remember_menu(self);
    g_blank = true;
    // The Out state's exit clears the draw byte. Writing it during Out pops the shrink.
    st8(self + kDraw, 0);
    st8(self + kActive, 0);
    st8(self + kShown, 0);
    st32(self + kPad, pause_settings_idle_pad(ld32(self + kPad)));
    set_pane_visible(layout_root(self), false);
    hide_cursor(true);
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

static void close_controller_window() {
    const uint32_t mgr = ld32(kWindowMgr);
    if (!guest_obj(mgr)) return;
    const uint32_t type = ld32(mgr + 0x1AA0);
    const uint8_t request = ld8(mgr + 0x1AB8);
    const uint8_t visible = ld8(mgr + 0x1AA9);
    const uint8_t shown = ld8(mgr + 0x1AB9);
    if (!pause_settings_closes_window(type, request | shown, visible)) return;
    st32(mgr + 0x1AA0, 0);
    st8(mgr + 0x1AA8, 0);
    st8(mgr + 0x1AA9, 0);
    st8(mgr + 0x1AB8, 0);
    st8(mgr + 0x1AB9, 0);
    st8(mgr + 0x1ABD, 0);
    st8(mgr + 0x22B4, 0);
}

static uint16_t ld16(uint32_t ea) {
    return (uint16_t)(((uint16_t)ld8(ea) << 8) | ld8(ea + 1));
}

// The manager calls the press virtual when button+0x84 is 0.
// That call is inside the step, before this hook. Keep the byte set so only the hook plays it.
static uint32_t find_named_button(uint32_t self, const char* name) {
    const uint32_t arr = ld32(self + kButtons);
    if (!guest_obj(arr)) return 0;
    const int mn = (int)ld32(self + kMin);
    const int mx = (int)ld32(self + kMax);
    const int begin = mn < mx ? mn : 0;
    const int end = mx > begin ? mx : begin + 4;
    for (int i = begin; i <= end && i < 16; i++) {
        const uint32_t button = ld32(arr + (uint32_t)i * 4u);
        if (button_name_is(button, name)) return button;
    }
    return 0;
}

static uint32_t find_settings_button(uint32_t self) {
    return find_named_button(self, "Pa_PlayStyle_00");
}

// The open-menu enter enables every row and rebinds the button anims.
// Quit at the start of 1-1 is the row that enter turns bright. Put that row back.
static void snap_quit(uint32_t self) {
    g_quit_anim_n = 0;
    g_quit_dim = false;
    const uint32_t button = find_named_button(self, "Pa_Exit_00");
    if (!guest_obj(button)) return;
    g_quit_flags = ld32(button + 4);
    g_quit_dim = (g_quit_flags & 0x10u) == 0;
    if (!g_quit_dim) return;
    static const uint32_t offs[] = {0x30, 0x34, 0x38, 0x6C, 0x70, 0x74, 0x78};
    for (uint32_t off : offs) {
        const uint32_t anim = ld32(button + off);
        if (!guest_obj(anim) || g_quit_anim_n >= 8) continue;
        QuitAnim& snap = g_quit_anims[g_quit_anim_n++];
        snap.anim = anim;
        snap.frame = (float)ldf32(anim + kAnimFrame);
        snap.speed = (float)ldf32(anim + kAnimSpeed);
        snap.enable = ld8(anim + kAnimEnable);
        snap.flags = ld32(anim + kAnimFlags);
    }
}

static void restore_quit(Cpu* c, uint32_t self) {
    if (!g_quit_dim) return;
    const uint32_t button = find_named_button(self, "Pa_Exit_00");
    if (!guest_obj(button)) return;
    st32(button + 4, g_quit_flags);
    for (int i = 0; i < g_quit_anim_n; i++) {
        const QuitAnim& snap = g_quit_anims[i];
        if (!guest_obj(snap.anim)) continue;
        stf32(snap.anim + kAnimFrame, snap.frame);
        stf32(snap.anim + kAnimSpeed, snap.speed);
        st8(snap.anim + kAnimEnable, snap.enable);
        st32(snap.anim + kAnimFlags, snap.flags);
        const uint32_t vt = ld32(snap.anim + kAnimVt);
        if (!guest_obj(vt)) continue;
        const uint32_t step = ld32(vt + kAnimUpdate);
        if (guest_obj(step)) guest_call(c, step, {snap.anim});
    }
}

static void block_game_press(uint32_t self) {
    const uint32_t button = find_settings_button(self);
    if (!guest_obj(button)) return;
    st8(button + kPressByte, 1);
    const uint32_t mgr = self + kMgr;
    st32(mgr + 0x28, ld32(mgr + 0x28) & ~4u);
}

// The manager calls the press virtual only when button+0x84 is 0.
// Leave it clear once the menu is back so the next A can start Out.
static void release_settings_press(uint32_t self) {
    if (!pause_settings_releases_press(true)) return;
    const uint32_t button = find_settings_button(self);
    if (!guest_obj(button)) return;
    st8(button + kPressByte, 0);
    const uint32_t anim = ld32(button + kPressAnim);
    if (guest_obj(anim)) stf32(anim + kAnimFrame, 0.f);
}

// The press virtual leaves the decide anim at its last frame with speed -1.
// A later call does not rewind it, so put that anim back and step one frame before the draw.
static void restart_decide(Cpu* c, uint32_t button) {
    const uint32_t decide = ld32(button + kDecideAnim);
    if (!guest_obj(decide)) return;
    guest_call(c, GC(kShowAnim), {button, decide});
    guest_call(c, GC(kBindAnim), {button, decide});
    const uint32_t res = ld32(decide + 8);
    if (guest_obj(res)) {
        const uint16_t count = ld16(res + 8);
        if (count != 0) stf32(decide + kAnimFrame, (float)count);
    }
    stf32(decide + kAnimSpeed, -1.0);
    st8(decide + kAnimEnable, 1);
    st32(decide + kAnimFlags, ld32(decide + kAnimFlags) & ~7u);
    const uint32_t vt = ld32(decide + kAnimVt);
    if (!guest_obj(vt)) return;
    const uint32_t step = ld32(vt + kAnimUpdate);
    if (guest_obj(step)) guest_call(c, step, {decide});
}

static void play_press(Cpu* c, uint32_t self) {
    const uint32_t arr = ld32(self + kButtons);
    if (!guest_obj(arr) || g_index < 0) return;
    const uint32_t button = ld32(arr + (uint32_t)g_index * 4u);
    const PausePress press = pause_press(button, guest_obj(button));
    if (!press.play) return;
    const uint32_t anim = ld32(button + kPressAnim);
    if (guest_obj(anim)) {
        const float frame = pause_replay_frame(pause_press_replays_anim(false), (float)ldf32(anim + kAnimFrame));
        stf32(anim + kAnimFrame, frame);
        stf32(anim + kAnimSpeed, 0);
        st8(anim + kAnimEnable, 1);
    }
    // Frame 0 makes this one call start the decide anim and play the click.
    st8(button + kPressMute, 0);
    const uint32_t vt = ld32(button + kButtonVt);
    if (guest_obj(vt)) {
        const uint32_t fn = ld32(vt + kPressSlot);
        if (guest_obj(fn)) guest_call(c, fn, {button});
    }
    const uint32_t decide = ld32(button + kDecideAnim);
    float speed = 0.f;
    if (guest_obj(decide)) speed = (float)ldf32(decide + kAnimSpeed);
    if (!(speed < -0.5f)) {
        restart_decide(c, button);
        if (guest_obj(decide)) speed = (float)ldf32(decide + kAnimSpeed);
    }
    st8(button + kPressByte, 1);
    LOG("[pause] Settings press speed %.2f", speed);
}

static bool anim_out_finished(Cpu* c, uint32_t self) {
    const uint32_t layout = ld32(self);
    if (!guest_obj(layout)) return false;
    const uint32_t anim = guest_call(c, GC(0x02671AF4u), {layout, 0});
    if (!guest_obj(anim)) return false;
    return (ld32(anim + kAnimFlags) & 1u) != 0;
}

static uint32_t pause_state_id(Cpu* c, uint32_t self) {
    const uint32_t pending = ld32(self + kState + 4);
    const uint32_t obj = ld32(self + kState + 8);
    const uint32_t id = current_state(c, self);
    const uint32_t inside = guest_obj(obj) ? ld32(obj + 8) : 0;
    if (pause_out_state(id) || pause_controller_state(id)) return id;
    if (pause_out_state(pending) || pause_controller_state(pending)) return pending;
    if (pause_out_state(inside) || pause_controller_state(inside)) return inside;
    if (pause_out_state(obj) || pause_controller_state(obj)) return obj;
    return id;
}

static void follow_settings_fade(Cpu* c, uint32_t self) {
    const bool confirm = settings_confirm_pad(ld32(self + kPad));
    const bool again = g_saw_overlay && pause_press_again(g_pressed, confirm, g_replay_ready, overlay::is_open());
    if (again) {
        g_pressed = false;
        g_opened = false;
        g_saw_overlay = false;
        g_settle = 0;
        g_left = false;
    }
    g_replay_ready = !confirm;
    st32(self + kPad, pause_settings_idle_pad(ld32(self + kPad)));
    const uint32_t id = pause_state_id(c, self);
    const bool window = pause_controller_state(id);
    const bool out = pause_out_state(id);
    if (out) remember_menu(self);
    const bool finished = window || (out && anim_out_finished(c, self));
    const PauseFade fade = pause_settings_fade(id, finished, overlay::is_open(), g_opened, g_pressed);
    if (fade.play_press) {
        g_pressed = true;
        play_press(c, self);
    }
    if (fade.block_press) block_game_press(self);
    if (g_pressed || fade.leave) close_controller_window();
    // 3 at +0x1C would enter the close state again once this menu is current.
    if (out || window || g_left) st32(self + kMode, 0);
    if (fade.leave && !g_left) {
        g_left = true;
        st32(self + kMode, 0);
        guest_call(c, GC(kChangeState), {self + kState, kMenuState});
        // The open-menu enter places the cursor on Continue while Out is still scaled.
        // Putting it on Settings here makes the corners huge. Place after the list is back.
        // That enter also enables Quit. Put the dim row back before the draw.
        st32(self + kIndex, (uint32_t)g_index);
        restore_quit(c, self);
        hide_cursor(true);
        g_cursor_placed = false;
        g_held_frames = 0;
        close_controller_window();
    }
    if (fade.open_overlay && settings_click_end(true).open_overlay) {
        g_opened = true;
        g_open_request.store(true);
        LOG("[pause] Settings opens the overlay");
    }
}

static void disarm();

static void release_leave() {
    g_page_closed = false;
    g_restore_menu = false;
    g_cursor_placed = false;
    g_handed = false;
    g_in_started = false;
    g_held_frames = 0;
    g_block_back = false;
    if (g_armed) disarm();
}

static void disarm() {
    g_armed = false;
    g_opened = false;
    g_saw_overlay = false;
    g_settle = 0;
    g_pressed = false;
    g_replay_ready = true;
    g_left = false;
    g_held = false;
    g_restore_panes = false;
    g_saved_count = 0;
    const uint32_t self = ld32(kPause);
    if (guest_obj(self)) release_settings_press(self);
}

// 0x025FD500 has already stepped the course pause object before this runs.
// A Settings confirm is in the Out state. Leave it until that fade would open the controller window.
static void intercept_course_pause(Cpu* c) {
    if (!g_enabled.load()) return;
    const uint32_t self = ld32(kPause);
    if (!guest_obj(self)) return;
    const bool open = overlay::is_open();
    const bool back_held = (ld32(self + kPad) & (1u << 14)) != 0;
    // blocks_input stays set until Back is released, including the frame the overlay closes.
    g_block_back = pause_settings_blocks_back(open || overlay::blocks_input(), back_held, g_block_back);
    // The step already entered the Back fade. Put the button list back while that B is still held.
    if (pause_settings_cancels_back_out(g_block_back, current_state(c, self))) undo_back_out(c, self);
    // The controller layout stays skipped after the overlay closes, until this pause menu goes away.
    // Closing the overlay writes the draw bytes to 0. That is not the pause menu leaving.
    if (!g_blank && !open && !pause_menu_shown(ld8(self + kActive), ld8(self + kShown))) {
        if (g_page_closed || g_armed) hide_cursor(true);
        g_page_closed = false;
        g_menu_root = 0;
        g_restore_menu = false;
        g_cursor_placed = false;
        g_handed = false;
        g_in_started = false;
        g_held_frames = 0;
        g_block_back = false;
        g_quit_dim = false;
        g_quit_anim_n = 0;
        if (g_armed) disarm();
    }
    if (g_armed && g_saw_overlay && !open && g_settle == 0)
        g_settle = pause_settings_settle(true, false);
    if (open) g_saw_overlay = true;
    bool playing_out = false;
    bool controller_page = false;
    if (g_armed) {
        const uint32_t id = pause_state_id(c, self);
        playing_out = pause_out_state(id) && !anim_out_finished(c, self);
        controller_page = pause_controller_state(id);
    }
    hide_for_overlay(c, self, playing_out, controller_page);
    // The step can already be in Out before this hook arms. Do not rewind that shrink.
    // Pending and saved ids stay on Out after the fade has returned.
    // Only the current state may drop the pin. A one-frame drop lets the shrink run away.
    const uint32_t now = current_state(c, self);
    const bool out_now = pause_out_state(now);
    // Continue, Quit, and Back after the overlay must run their own leave.
    // Treating that Out as Settings hides the menu and leaves the course paused.
    if (pause_settings_drops_hold(g_page_closed, settings_row(self), now)) {
        release_leave();
        return;
    }
    if (out_now) {
        g_restore_menu = false;
        g_cursor_placed = false;
        g_handed = false;
        g_in_started = false;
        g_held_frames = 0;
        hide_cursor(true);
    } else if (!g_handed && g_page_closed && !open && !g_opened) g_restore_menu = true;
    else if (!g_handed && g_saw_overlay && !open) g_restore_menu = true;
    // Out scales the cursor with the button. Hide it until the button list is full size again.
    if (g_page_closed && !g_cursor_placed) hide_cursor(true);
    if (pause_settings_holds_menu(g_restore_menu, out_now) && !open) {
        if (!g_in_started && !pause_settings_pins_out(g_handed)) {
            play_menu_in(c, self);
            g_in_started = true;
            g_held_frames = 0;
        }
        restore_quit(c, self);
        st8(self + kDraw, g_draw ? g_draw : 1);
        st8(self + kActive, g_active ? g_active : 1);
        st8(self + kShown, g_shown ? g_shown : 1);
        // 0x025BE214 copies +0x4C over the row. That word stays 0, so the next step
        // aims the cursor at Continue and the corners grow off the button.
        if (pause_settings_locks_row(g_cursor_placed)) {
            st32(self + kIndex, (uint32_t)g_index);
            st32(self + kCursorIndex, (uint32_t)g_index);
        }
        // A sets this word to 2. The next step turns 2 into 3 and enters Out.
        // Leave a fresh 2 alone. A 2 that never becomes 3 would block the pad.
        const uint32_t mode = ld32(self + kMode);
        if (mode == 2u) g_mode_seen++;
        else g_mode_seen = 0;
        if (pause_settings_clears_mode(mode, g_mode_seen)) {
            st32(self + kMode, 0);
            g_mode_seen = 0;
        }
        if (ld32(self + kMode) == 0u) release_settings_press(self);
        g_held_frames++;
        if (!g_cursor_placed && pause_settings_hands_back(anim_out_finished(c, self), g_held_frames)) {
            place_menu_cursor(c, self);
            st32(self + kCursorIndex, (uint32_t)g_index);
            hide_cursor(false);
            g_cursor_placed = true;
            g_handed = true;
            g_restore_menu = false;
        }
    } else if (!pause_settings_holds_menu(g_restore_menu, out_now)) {
        g_held_frames = 0;
        g_mode_seen = 0;
    }
    if (g_armed) {
        follow_settings_fade(c, self);
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
    const uint32_t id = pause_state_id(c, self);
    if (!pause_settings_arms(id, ld32(self + kMode), settings_row(self))) return;
    g_index = (int)ld32(self + kIndex);
    if (!g_quit_dim) snap_quit(self);
    g_shown = ld8(self + kShown);
    if (g_shown == 0) g_shown = 1;
    g_armed = true;
    g_page_closed = true;
    g_cursor_placed = false;
    g_handed = false;
    g_in_started = false;
    hide_cursor(true);
    const uint32_t button = find_settings_button(self);
    const uint8_t byte = guest_obj(button) ? ld8(button + kPressByte) : 0xff;
    float frame = -1.f;
    if (guest_obj(button)) {
        const uint32_t anim = ld32(button + kPressAnim);
        if (guest_obj(anim)) frame = (float)ldf32(anim + kAnimFrame);
    }
    LOG("[pause] Settings arm byte %u frame %.2f step %u mode %u", byte, frame, ld32(self + kMgr + kMgrStep),
        ld32(self + kMode));
    follow_settings_fade(c, self);
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

void strip_other_back(uint32_t other) {
    if (!pause_settings_strips_other_back(g_block_back) || !guest_obj(other)) return;
    st32(other + kPad, ld32(other + kPad) & ~(1u << 14));
}

// The other pause step runs after intercept and moves this cursor.
// Put it back on Settings after that step, once the button list is full size.
void finish_pause_frame(Cpu* c, uint32_t other) {
    if (guest_obj(other) && pause_settings_cancels_back_out(g_block_back, current_state(c, other)))
        undo_back_out(c, other);
    if (!g_enabled.load() || !g_page_closed) return;
    if (guest_obj(other) && pause_settings_drops_hold(true, settings_row(other), current_state(c, other))) {
        release_leave();
        return;
    }
    const uint32_t self = ld32(kPause);
    if (!guest_obj(self)) return;
    const bool open = overlay::is_open();
    const bool out_now = pause_out_state(current_state(c, self));
    const bool other_out = guest_obj(other) && pause_out_state(current_state(c, other));
    if (g_handed) return;
    const bool hold = pause_settings_holds_menu(g_restore_menu, out_now || other_out) && !open && !g_block_back;
    if (!hold || pause_settings_locks_row(g_cursor_placed)) {
        hide_pause_cursor(c);
        return;
    }
    hide_cursor(false);
}

}

extern "C" void hook_025F3834(Cpu* c) {
    const uint32_t self = c->r[3];
    pause_menu::intercept_course_pause(c);
    // This object's step is about to run. Back here starts Out and leaves the yellow cursor up.
    pause_menu::strip_other_back(self);
    c->r[3] = self;
    f_025F3834_orig(c);
    c->r[3] = self;
    pause_menu::finish_pause_frame(c, self);
}

// The layout site is shared with the world-map button. This file owns the pause half.
// map_menu_settings.cpp defines site_026730C4 and calls this.
extern "C" void pause_menu_site_026730C4(Cpu* c, uint32_t layout) {
    pause_menu::install_settings_button(c, layout);
}
