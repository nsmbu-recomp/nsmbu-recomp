#include "hooks/map_menu_settings.h"

#include "core/runtime.h"
#include "guest_addr.h"
#include "overlay/game_rect.h"
#include "overlay/overlay.h"

#include <atomic>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <mutex>
#include <vector>

extern "C" void f_025A4218_orig(Cpu* c);
extern "C" void f_025A42CC_orig(Cpu* c);
extern "C" void f_025A4B98_orig(Cpu* c);

namespace map_menu {

constexpr uint32_t kParts = 0x11E8;
constexpr int kPartCount = 10;
constexpr uint32_t kActive = 0x1A04;
constexpr uint32_t kCursor = 0xEC8;
constexpr uint32_t kCursorIndex = 0x250;
constexpr uint32_t kCursorPrev = 0x254;
constexpr uint32_t kCursorStep = 0x2C;
constexpr uint32_t kCursorFocus = 0x25C;
constexpr uint32_t kDecideAnime = 0x25E;
constexpr uint32_t kDecideTrig = 0x25F;
constexpr uint32_t kTouchByte = 0x266;
constexpr uint32_t kButtonLock = 0x268;
constexpr uint32_t kButtonState = 0x26C;
constexpr uint32_t kCursorList = 0x34;
constexpr uint32_t kPad = 0x164;
constexpr uint32_t kPlus = 1u << 11;
constexpr uint32_t kTouch = 1u << 12;
constexpr uint32_t kDecide = 1u << 13;
constexpr uint32_t kConfirm = kPlus | kTouch | kDecide;
constexpr uint32_t kMenuPads = 0x1214;
constexpr uint32_t kPadStride = 0x194;
constexpr uint32_t kHold = 0x10C;
constexpr int kMenuPadCount = 5;
constexpr uint32_t kUp = 1u << 4;
constexpr uint32_t kDown = 1u << 5;
constexpr uint32_t kLeft = 1u << 6;
constexpr uint32_t kRight = 1u << 7;
constexpr float kGuideY = -348.f;

static std::atomic<uint32_t> g_menu{0};
static std::atomic<bool> g_enabled{false};
static std::atomic<bool> g_open_request{false};
static uint32_t g_settings = 0;
static int g_settings_idx = -1;
static int g_translate = -1;
static uint32_t g_title = 0;
static uint32_t g_left[2] = {};
static int g_left_n = 0;
static int g_title_idx = -1;
static uint32_t g_prev_pad = 0;
static bool g_click = false;
static int g_click_age = 0;
static int g_settle = 0;
static bool g_blank = false;
static bool g_saw_overlay = false;
static bool g_nav = false;
constexpr uint32_t kCursorMove = 0x10216958u;
constexpr uint32_t kMenuIdle = 0x10211B14u;
constexpr uint32_t kMenuAnim = 0x10211B54u;
constexpr uint32_t kMpState = 0x18;
constexpr uint32_t kStateId = 8;
static int g_force_idx = -1;
static uint32_t g_force_btn = 0;
static uint32_t g_show_btn = 0;
static uint32_t g_leave_btn = 0;
static int g_guide_from = -1;
static std::mutex g_hit_mu;
static float g_win_w = 0, g_win_h = 0, g_ox = 0, g_oy = 0, g_ow = 0, g_oh = 0;
static bool g_hit_ok = false;
static float g_nx = -1, g_ny = -1;

bool enabled() { return g_enabled.load(); }

void set_enabled(bool on) { g_enabled.store(on); }

static bool guest_obj(uint32_t p) {
    return p >= 0x01000000u && p < 0x80000000u && (p & 3u) == 0;
}

static int find_xy(uint32_t pane, float x, float y) {
    if (!guest_obj(pane)) return -1;
    for (uint32_t off = 0; off + 8 <= 0x240; off += 4) {
        const float a = (float)ldf32(pane + off);
        const float b = (float)ldf32(pane + off + 4);
        if (std::fabs(a - x) < 0.5f && std::fabs(b - y) < 0.5f) return (int)off;
    }
    return -1;
}

static bool button_has(uint32_t btn, uint32_t word) {
    if (!word) return false;
    for (uint32_t off = 0; off < 0xB0; off += 4)
        if (ld32(btn + off) == word) return true;
    return false;
}

static bool pane_under(uint32_t pane, uint32_t root) {
    for (int i = 0; i < 8 && guest_obj(pane); i++) {
        if (pane == root) return true;
        const uint32_t parent = ld32(pane + 0xC);
        if (parent == pane) return false;
        pane = parent;
    }
    return false;
}

static int title_index(uint32_t cursor, uint32_t pane) {
    if (!guest_obj(pane)) return -1;
    const uint32_t sentinel = cursor + kCursorList;
    uint32_t node = ld32(sentinel);
    for (int i = 0; i < 41; i++) {
        if (node == 0 || node == sentinel) return -1;
        if (!guest_obj(node) || node < 0x20) return -1;
        const uint32_t btn = node - 0x20;
        const uint32_t hit = ld32(btn + 0x3C);
        if (button_has(btn, pane) || pane_under(hit, pane)) return i;
        const uint32_t next = ld32(node);
        if (next == node) return -1;
        node = next;
    }
    return -1;
}

static bool is_left(uint32_t pane) {
    for (int i = 0; i < g_left_n; i++)
        if (g_left[i] == pane) return true;
    return false;
}

static void nudge(uint32_t self) {
    if (!g_enabled.load() || !g_settings) return;
    const uint32_t parts = self + kParts;
    if (g_translate < 0) {
        for (int i = 0; i < kPartCount && g_translate < 0; i++) {
            const uint32_t pane = ld32(parts + (uint32_t)i * 4);
            g_translate = find_xy(pane, kColLeft, kRowTopY);
        }
        if (g_translate >= 0) LOG("[menu] settings column uses pane translate +%X", g_translate);
    }
    if (g_translate < 0) return;
    const uint32_t off = (uint32_t)g_translate;
    uint32_t panes[kPartCount];
    float xs[kPartCount];
    float ys[kPartCount];
    int n = 0;
    for (int i = 0; i < kPartCount; i++) {
        const uint32_t pane = ld32(parts + (uint32_t)i * 4);
        if (!guest_obj(pane)) continue;
        panes[n] = pane;
        xs[n] = (float)ldf32(pane + off);
        ys[n] = (float)ldf32(pane + off + 4);
        n++;
    }
    if (!g_title) {
        for (int i = 0; i < n; i++) {
            if (std::fabs(ys[i] - kRowBotY) > 1.f || std::fabs(xs[i]) > 2000.f) continue;
            if (bottom_column(xs[i]) == kColLeft) {
                if (g_left_n < 2 && !is_left(panes[i])) g_left[g_left_n++] = panes[i];
            } else {
                g_title = panes[i];
            }
        }
    }
    for (int i = 0; i < n; i++) {
        float nx = 0;
        if (is_left(panes[i])) nx = kColLeft;
        else if (panes[i] == g_title) nx = kColMid;
        else continue;
        if (std::fabs(xs[i] - nx) > 0.5f) stf32(panes[i] + off, nx);
    }
    if (guest_obj(g_settings)) {
        const float x = (float)ldf32(g_settings + off);
        const float y = (float)ldf32(g_settings + off + 4);
        if (std::fabs(x - kColRight) > 0.5f) stf32(g_settings + off, kColRight);
        if (std::fabs(y - kRowBotY) > 0.5f) stf32(g_settings + off + 4, kRowBotY);
    }
}

static void forget(uint32_t self) {
    uint32_t cur = self;
    if (g_menu.compare_exchange_strong(cur, 0u)) {
        g_settings = 0;
        g_settings_idx = -1;
        g_title = 0;
        g_left_n = 0;
        g_title_idx = -1;
        g_prev_pad = 0;
        g_force_idx = -1;
        g_force_btn = 0;
        g_show_btn = 0;
        g_leave_btn = 0;
        g_guide_from = -1;
        g_click = false;
        g_click_age = 0;
        g_settle = 0;
        g_blank = false;
        g_saw_overlay = false;
        g_nav = false;
    }
}

static uint32_t button_at(uint32_t cursor, int index) {
    const uint32_t sentinel = cursor + kCursorList;
    uint32_t node = ld32(sentinel);
    for (int i = 0; i < 41; i++) {
        if (node == 0 || node == sentinel || !guest_obj(node) || node < 0x20) return 0;
        if (i == index) return node - 0x20;
        const uint32_t next = ld32(node);
        if (next == node) return 0;
        node = next;
    }
    return 0;
}

static void write_rect(uint32_t btn, float cx, float cy, float hw, float hh) {
    stf32(btn + 0x40, cx - hw);
    stf32(btn + 0x48, cx + hw);
    stf32(btn + 0x44, cy - hh);
    stf32(btn + 0x4C, cy + hh);
    stf32(btn + 0x5C, cx - hw);
    stf32(btn + 0x64, cx + hw);
    stf32(btn + 0x60, cy - hh);
    stf32(btn + 0x68, cy + hh);
}

static void arm_settings(uint32_t self) {
    if (!g_enabled.load() || !g_settings || g_translate < 0) return;
    const uint32_t cursor = self + kCursor;
    g_settings_idx = title_index(cursor, g_settings);
    if (g_settings_idx < 0) return;
    const uint32_t btn = button_at(cursor, g_settings_idx);
    if (!btn) return;
    const uint32_t flags = ld32(btn + 4);
    st32(btn + 4, flags | 8u);
    if (!g_click) st8(btn + 0x84, settings_decide_byte(false));
    const int title_i = title_index(cursor, g_title);
    float cx = kColRight;
    float cy = kRowBotY;
    float hw = kBtnW * 0.5f;
    float hh = kBtnH * 0.5f;
    if (title_i >= 0) {
        const uint32_t title = button_at(cursor, title_i);
        if (title) {
            const float x0 = (float)ldf32(title + 0x40);
            const float x1 = (float)ldf32(title + 0x48);
            const float y0 = (float)ldf32(title + 0x44);
            const float y1 = (float)ldf32(title + 0x4C);
            hw = std::fabs(x1 - x0) * 0.5f;
            hh = std::fabs(y1 - y0) * 0.5f;
            const float title_x = g_title ? (float)ldf32(g_title + (uint32_t)g_translate) : kColMid;
            cx = (x0 + x1) * 0.5f + (kColRight - title_x);
            cy = (y0 + y1) * 0.5f;
        }
    }
    if (!(hw > 1.f)) hw = kBtnW * 0.5f;
    if (!(hh > 1.f)) hh = kBtnH * 0.5f;
    static bool logged = false;
    if (!logged) {
        logged = true;
        LOG("[menu] settings cursor %d at %08X rect %.0f,%.0f %.0fx%.0f", g_settings_idx, cursor, cx, cy, hw * 2.f, hh * 2.f);
    }
}

static bool rect_center(uint32_t btn, float& cx, float& cy) {
    const float x0 = (float)ldf32(btn + 0x40);
    const float x1 = (float)ldf32(btn + 0x48);
    const float y0 = (float)ldf32(btn + 0x44);
    const float y1 = (float)ldf32(btn + 0x4C);
    if (!(std::fabs(x1 - x0) > 1.f) || !(std::fabs(y1 - y0) > 1.f)) return false;
    cx = (x0 + x1) * 0.5f;
    cy = (y0 + y1) * 0.5f;
    return true;
}

static void copy_x(uint32_t dst, uint32_t src) {
    stf32(dst + 0x40, (float)ldf32(src + 0x40));
    stf32(dst + 0x48, (float)ldf32(src + 0x48));
    stf32(dst + 0x5C, (float)ldf32(src + 0x5C));
    stf32(dst + 0x64, (float)ldf32(src + 0x64));
}

void fit_settings_hit(uint32_t mgr, uint32_t point) {
    if (!g_enabled.load() || !g_settings || !guest_obj(mgr)) return;
    const uint32_t sentinel = mgr + 0x10;
    uint32_t node = ld32(sentinel);
    uint32_t settings_btn = 0;
    uint32_t column_btn = 0;
    float column_x = -100000.f;
    for (int i = 0; i < 41; i++) {
        if (node == 0 || node == sentinel || !guest_obj(node) || node < 0x20) break;
        const uint32_t btn = node - 0x20;
        const uint32_t hit = ld32(btn + 0x3C);
        float cx = 0, cy = 0;
        if (button_has(btn, g_settings) || pane_under(hit, g_settings)) settings_btn = btn;
        else if (rect_center(btn, cx, cy) && cy > 20.f && cx > column_x) {
            column_x = cx;
            column_btn = btn;
        }
        const uint32_t next = ld32(node);
        if (next == node) break;
        node = next;
    }
    if (settings_btn && column_btn) copy_x(settings_btn, column_btn);
    (void)point;
}

static int column_index(uint32_t cursor) {
    const uint32_t sentinel = cursor + kCursorList;
    uint32_t node = ld32(sentinel);
    int found = -1;
    float best = -100000.f;
    for (int i = 0; i < 41; i++) {
        if (node == 0 || node == sentinel || !guest_obj(node) || node < 0x20) break;
        const uint32_t btn = node - 0x20;
        float cx = 0, cy = 0;
        if (rect_center(btn, cx, cy) && cy > 20.f && cx > best) {
            best = cx;
            found = i;
        }
        const uint32_t next = ld32(node);
        if (next == node) break;
        node = next;
    }
    return found;
}

static int guide_index(uint32_t cursor, uint32_t& out) {
    const uint32_t sentinel = cursor + kCursorList;
    uint32_t node = ld32(sentinel);
    int found = -1;
    float best = 100000.f;
    for (int i = 0; i < 41; i++) {
        if (node == 0 || node == sentinel || !guest_obj(node) || node < 0x20) break;
        const uint32_t btn = node - 0x20;
        float cx = 0, cy = 0;
        if (rect_center(btn, cx, cy) && cx > 300.f && cy < best) {
            best = cy;
            found = i;
            out = btn;
        }
        const uint32_t next = ld32(node);
        if (next == node) break;
        node = next;
    }
    if (!(best < -150.f)) return -1;
    return found;
}

static void choose(uint32_t cursor, int index) {
    const uint32_t btn = button_at(cursor, index);
    if (!btn) return;
    g_force_idx = index;
    g_force_btn = btn;
}

static uint32_t part_named(uint32_t self, const char* name) {
    const uint32_t parts = self + kParts;
    for (int i = 0; i < kPartCount; i++) {
        const uint32_t pane = ld32(parts + (uint32_t)i * 4);
        if (!guest_obj(pane)) continue;
        if (std::strcmp(reinterpret_cast<const char*>(mem::ptr(pane + 0x80)), name) == 0) return pane;
    }
    return 0;
}

static int middle_index(uint32_t cursor) {
    const uint32_t sentinel = cursor + kCursorList;
    uint32_t node = ld32(sentinel);
    int found = -1;
    float best = 100000.f;
    for (int i = 0; i < 41; i++) {
        if (node == 0 || node == sentinel || !guest_obj(node) || node < 0x20) break;
        const uint32_t btn = node - 0x20;
        float cx = 0, cy = 0;
        if (rect_center(btn, cx, cy) && cy < 0.f && cy > -200.f) {
            const float dist = std::fabs(cx);
            if (dist < best) {
                best = dist;
                found = i;
            }
        }
        const uint32_t next = ld32(node);
        if (next == node) break;
        node = next;
    }
    if (!(best < 80.f)) return -1;
    return found;
}

static void on_pad(uint32_t self) {
    if (!g_enabled.load() || overlay::is_open()) return;
    const uint32_t cursor = self + kCursor;
    const uint32_t pad_at = cursor + kPad;
    const uint32_t pad = settings_dir_pad(ld32(pad_at), ld32(self + kMenuPads));
    if (ld8(cursor + kCursorFocus) == 0 && !settings_move_without_focus(g_nav, g_blank, g_click)) {
        g_prev_pad = pad;
        g_force_idx = -1;
        g_force_btn = 0;
        return;
    }
    const uint32_t trig = pad & ~g_prev_pad;
    g_prev_pad = pad;
    if (g_settings) g_settings_idx = title_index(cursor, g_settings);
    const int idx = (int)ld32(cursor + kCursorIndex);
    int title_i = title_index(cursor, part_named(self, "Pa_TitleBtn_00"));
    if (title_i < 0) title_i = middle_index(cursor);
    const int players_i = title_index(cursor, part_named(self, "Pa_NumBtn_00"));
    const int coins_i = title_index(cursor, part_named(self, "Pa_SCoinBtn_00"));
    const int suspend_i = title_index(cursor, part_named(self, "Pa_SuspendBtn_00"));
    const int save_i = title_index(cursor, part_named(self, "Pa_SaveBtn_00"));
    const int column_i = column_index(cursor);
    uint32_t guide = 0;
    const int guide_i = guide_index(cursor, guide);
    float here_x = 0, here_y = 0;
    const uint32_t here = button_at(cursor, idx);
    const bool bottom_card = here && rect_center(here, here_x, here_y) && here_y < 0.f && here_y > -200.f &&
        idx != g_settings_idx;
    if (trig & kDown && column_i >= 0 && idx == column_i && g_settings_idx >= 0) choose(cursor, g_settings_idx);
    else if (trig & kUp && g_settings_idx >= 0 && idx == g_settings_idx && column_i >= 0) choose(cursor, column_i);
    else if (trig & kDown && players_i >= 0 && idx == players_i && title_i >= 0) choose(cursor, title_i);
    else if (trig & kUp && title_i >= 0 && idx == title_i && players_i >= 0) choose(cursor, players_i);
    else if (trig & kUp && coins_i >= 0 && (idx == suspend_i || idx == save_i)) choose(cursor, coins_i);
    else if (trig & kRight && title_i >= 0 && idx == title_i && g_settings_idx >= 0) choose(cursor, g_settings_idx);
    else if (trig & kLeft && g_settings_idx >= 0 && idx == g_settings_idx && title_i >= 0) choose(cursor, title_i);
    else if (trig & kDown && guide_i >= 0 && (bottom_card || idx == g_settings_idx)) {
        g_guide_from = idx;
        choose(cursor, guide_i);
    } else if (trig & kUp && guide_i >= 0 && idx == guide_i) {
        const int back = g_guide_from >= 0 ? g_guide_from : title_i;
        if (back >= 0) choose(cursor, back);
    }
    if (!g_click && settings_repair(idx, g_settings_idx, (trig & kConfirm) != 0, 0).hold) {
        g_click = true;
        g_click_age = 0;
        const uint32_t btn = button_at(cursor, idx);
        if (btn) st8(btn + 0x84, settings_decide_byte(true));
    }
    if (g_force_idx >= 0) {
        const uint32_t keep = ~(kUp | kDown | kLeft | kRight);
        st32(pad_at, ld32(pad_at) & keep);
        st32(self + kMenuPads, ld32(self + kMenuPads) & keep);
    }
}

static void apply_choice(uint32_t self) {
    if (!g_enabled.load() || g_force_idx < 0 || !g_force_btn) return;
    const uint32_t cursor = self + kCursor;
    const SettingsCursorPair pair = settings_cursor_pair(g_force_idx);
    st32(cursor + kCursorIndex, (uint32_t)pair.current);
    st32(cursor + kCursorPrev, (uint32_t)pair.previous);
    const uint32_t shown = ld32(cursor + settings_yellow_target());
    g_leave_btn = settings_release_previous(guest_obj(shown), shown == g_force_btn) ? shown : 0;
    st32(cursor + settings_yellow_target(), g_force_btn);
    st32(cursor + 0x40, g_force_btn);
    st32(cursor + kCursorStep, 0);
    // Focus 0 returns before the cursor state that places the yellow cursor.
    if (settings_focus_for_move(true)) st8(cursor + kCursorFocus, 1);
    g_show_btn = g_force_btn;
}

static void hide_yellow_cursor(bool hide);

// button+0x1C is the vtable. Offset 0x1C clears the old button. Offset 0x14 selects the new one.
static void call_button(Cpu* c, uint32_t button, uint32_t slot) {
    if (!guest_obj(button)) return;
    const uint32_t vt = ld32(button + 0x1C);
    if (!guest_obj(vt)) return;
    const uint32_t fn = ld32(vt + slot);
    if (!guest_obj(fn)) return;
    guest_call(c, fn, {button});
}

// SelectCursor::set at 0x024C6744. r3 is the instance, r4 is the button, r5 is the cursor type.
// A type of 0x18 returns before the layout moves.
static void place_yellow(Cpu* c, uint32_t self) {
    const uint32_t leave = g_leave_btn;
    const uint32_t show = g_show_btn;
    g_leave_btn = 0;
    g_show_btn = 0;
    if (!guest_obj(show)) return;
    const bool moved = settings_release_previous(guest_obj(leave), leave == show);
    if (moved) call_button(c, leave, 0x1C);
    call_button(c, show, 0x14);
    if (moved) guest_call(c, settings_cursor_sound(), {show});
    c->r[3] = self;
    const uint32_t cursor = self + kCursor;
    st32(cursor + settings_yellow_target(), show);
    st32(cursor + 0x40, show);
    const uint32_t inst = ld32(0x101D17A8u);
    if (!guest_obj(inst)) return;
    uint32_t type = ld32(cursor + 0x258);
    if (type >= 0x18u) type = 0;
    guest_call(c, 0x024C6744u, {inst, show, type, 0});
    c->r[3] = self;
    hide_yellow_cursor(false);
}

static void clear_choice() {
    g_force_idx = -1;
    g_force_btn = 0;
}

static void clear_confirm(uint32_t self) {
    const uint32_t cursor = self + kCursor;
    st32(cursor + kPad, ld32(cursor + kPad) & ~kConfirm);
    for (int i = 0; i < kMenuPadCount; i++) {
        const uint32_t hold = self + kMenuPads + (uint32_t)i * kPadStride + kHold;
        st32(hold, ld32(hold) & ~kConfirm);
    }
}

static void finish_settings_click(Cpu* c, uint32_t self) {
    const uint32_t cursor = self + kCursor;
    const uint32_t state_at = cursor + kButtonState + (uint32_t)g_settings_idx * 4;
    const SettingsRepair repair = settings_repair(g_settings_idx, g_settings_idx, true, ld32(state_at));
    st32(state_at, repair.word);
    st8(cursor + kButtonLock, 0);
    st8(cursor + kDecideTrig, 0);
    st8(cursor + kTouchByte, 0);
    clear_confirm(self);
    guest_call(c, GC(0x029C4AF0u), {cursor + 0x10, GC(kCursorMove)});
    guest_call(c, GC(0x029C4AF0u), {self + 0x10, GC(kMenuIdle)});
    c->r[3] = self;
    st8(cursor + kCursorFocus, 1);
    st8(cursor + kDecideAnime, 1);
    g_click = false;
    g_click_age = 0;
    g_blank = true;
    g_saw_overlay = false;
    g_settle = 4;
    if (settings_click_end(true).open_overlay) g_open_request.store(true);
}

static uint8_t g_cursor_draw = 1;

static void hide_yellow_cursor(bool hide); 

void blank_cursor() {
    if (settings_hide_cursor(g_blank)) hide_yellow_cursor(true);
}

static void hide_yellow_cursor(bool hide) {
    const uint32_t inst = ld32(0x101D17A8u);
    if (!guest_obj(inst)) return;
    const uint32_t at = inst + 0x2C5F4u;
    if (hide) {
        const uint8_t cur = ld8(at);
        if (cur != 0) g_cursor_draw = cur;
        st8(at, 0);
        return;
    }
    st8(at, g_cursor_draw != 0 ? g_cursor_draw : 1);
}

static void restore_settings_menu(Cpu* c, uint32_t self) {
    const uint32_t cursor = self + kCursor;
    if (g_settings_idx >= 0) {
        const uint32_t state_at = cursor + kButtonState + (uint32_t)g_settings_idx * 4;
        st32(state_at, settings_repair(g_settings_idx, g_settings_idx, true, ld32(state_at)).word);
    }
    st8(cursor + kButtonLock, 0);
    st8(cursor + kDecideTrig, 0);
    st8(cursor + kTouchByte, 0);
    clear_confirm(self);
    guest_call(c, GC(0x029C4AF0u), {cursor + 0x10, GC(settings_cursor_state())});
    guest_call(c, GC(0x029C4AF0u), {self + 0x10, GC(settings_return_state())});
    c->r[3] = self;
    st8(cursor + kCursorFocus, 1);
    st8(cursor + kDecideAnime, 1);
    hide_yellow_cursor(false);
    g_blank = false;
    g_saw_overlay = false;
    g_nav = true;
    g_settle = 4;
}

static bool anim_finished(Cpu* c, uint32_t self) {
    const uint32_t mp = ld32(self + kMpState);
    if (!guest_obj(mp) || ld32(mp + kStateId) != kMenuAnim) return false;
    const uint32_t anim = guest_call(c, GC(0x02671AF4u), {self + 0x28, 0});
    c->r[3] = self;
    return guest_obj(anim) && (ld32(anim + 0x2C) & 1u) != 0;
}

static void watch_settings_click(Cpu* c, uint32_t self) {
    const bool open = overlay::is_open();
    const SettingsBlank blank = settings_blank(g_blank, g_saw_overlay, open);
    if (g_blank && open) g_saw_overlay = true;
    if (blank.restore) {
        restore_settings_menu(c, self);
    } else if (blank.hold) {
        st8(self + kCursor + kDecideAnime, 1);
        clear_confirm(self);
        return;
    }
    if (!g_enabled.load() || !g_click || g_settings_idx < 0) return;
    g_click_age++;
    const int idx = (int)ld32(self + kCursor + kCursorIndex);
    if (idx != g_settings_idx) {
        g_click = false;
        g_click_age = 0;
        return;
    }
    const bool done = anim_finished(c, self) || g_click_age >= settings_open_delay(true);
    c->r[3] = self;
    if (!done || !settings_click_end(true).keep_menu) return;
    finish_settings_click(c, self);
}

static void settle_settings(uint32_t self, bool tick) {
    if (!g_enabled.load() || g_settle <= 0) return;
    st8(self + kCursor + kDecideAnime, 1);
    clear_confirm(self);
    if (!tick) return;
    g_settle--;
    // A set gate here blocks Plus and B, so the last settle frame clears it.
    if (g_settle == 0) st8(self + kCursor + kDecideAnime, 0);
}

static void release_menu_gate(uint32_t self) {
    if (!settings_move_without_focus(g_nav, g_blank, g_click)) return;
    st8(self + kCursor + kDecideAnime, 0);
}

static uint16_t ru16(const uint8_t* p) {
    return (uint16_t)(((uint16_t)p[0] << 8) | p[1]);
}

static uint32_t ru32(const uint8_t* p) {
    return ((uint32_t)p[0] << 24) | ((uint32_t)p[1] << 16) | ((uint32_t)p[2] << 8) | p[3];
}

static void wu32(uint8_t* p, uint32_t v) {
    p[0] = (uint8_t)(v >> 24);
    p[1] = (uint8_t)(v >> 16);
    p[2] = (uint8_t)(v >> 8);
    p[3] = (uint8_t)v;
}

static void wf32(uint8_t* p, float v) {
    uint32_t u = 0;
    std::memcpy(&u, &v, 4);
    wu32(p, u);
}

static bool pane_named(const uint8_t* s, uint32_t n, const char* name) {
    const size_t len = std::strlen(name);
    if (n < 0xC + len + 1) return false;
    return std::memcmp(s + 0xC, name, len) == 0 && s[0xC + len] == 0;
}

static void replace_bytes(std::vector<uint8_t>& s, const uint8_t* from, size_t fn, const uint8_t* to, size_t tn) {
    if (fn < tn || s.size() < fn) return;
    for (size_t i = 0; i + fn <= s.size(); i++) {
        if (std::memcmp(s.data() + i, from, fn) != 0) continue;
        std::memcpy(s.data() + i, to, tn);
        std::memset(s.data() + i + tn, 0, fn - tn);
        return;
    }
}

static bool patch_menu_flyt(std::vector<uint8_t>& file) {
    if (file.size() < 0x20 || std::memcmp(file.data(), "FLYT", 4) != 0) return false;
    const uint8_t ja[] = {0x30, 0xBF, 0x30, 0xA4, 0x30, 0xC8, 0x30, 0xEB, 0x30, 0x78, 0x30, 0x82, 0x30, 0x69, 0x30, 0x8B};
    const uint8_t en[] = {0x00, 'S', 0x00, 'e', 0x00, 't', 0x00, 't', 0x00, 'i', 0x00, 'n', 0x00, 'g', 0x00, 's'};
    const char* title_id = "CourseSelectMenu_TitleBtn_00";
    const char* set_id = "CourseSelectMenu_SetBtn_00";
    std::vector<uint8_t> out(file.begin(), file.begin() + 0x14);
    bool inserted = false;
    bool already = false;
    uint32_t off = 0x14;
    while (off + 8 <= file.size()) {
        const uint32_t sz = ru32(file.data() + off + 4);
        if (sz < 8 || off + sz > file.size()) return false;
        std::vector<uint8_t> sec(file.begin() + off, file.begin() + off + sz);
        if (std::memcmp(sec.data(), "grp1", 4) == 0 && sec.size() >= 0x24) {
            char group[25] = {};
            std::memcpy(group, sec.data() + 8, 24);
            if (std::strcmp(group, "G_InOut_00") == 0) {
                const uint16_t kids = ru16(sec.data() + 0x20);
                bool has = false;
                if (0x24u + (uint32_t)kids * 24u <= sec.size()) {
                    for (uint16_t i = 0; i < kids; i++) {
                        if (std::memcmp(sec.data() + 0x24 + (uint32_t)i * 24u, "Pa_SetBtn_00", 12) == 0)
                            has = true;
                    }
                }
                if (!has && 0x24u + (uint32_t)kids * 24u == sec.size()) {
                    sec.resize(sec.size() + 24, 0);
                    std::memcpy(sec.data() + sec.size() - 24, "Pa_SetBtn_00", 12);
                    sec[0x20] = (uint8_t)((kids + 1) >> 8);
                    sec[0x21] = (uint8_t)(kids + 1);
                    wu32(sec.data() + 4, (uint32_t)sec.size());
                }
            }
        }
        if (std::memcmp(sec.data(), "prt1", 4) == 0) {
            if (pane_named(sec.data(), (uint32_t)sec.size(), "Pa_SetBtn_00")) already = true;
            if (pane_named(sec.data(), (uint32_t)sec.size(), "Pa_SuspendBtn_00") ||
                pane_named(sec.data(), (uint32_t)sec.size(), "Pa_SaveBtn_00"))
                wf32(sec.data() + 0x2C, kColLeft);
            if (pane_named(sec.data(), (uint32_t)sec.size(), "Pa_TitleBtn_00")) wf32(sec.data() + 0x2C, kColMid);
            if (pane_named(sec.data(), (uint32_t)sec.size(), "Pa_SetUp_00") ||
                pane_named(sec.data(), (uint32_t)sec.size(), "Pa_SetUp_01"))
                wf32(sec.data() + 0x30, kGuideY);
        }
        out.insert(out.end(), sec.begin(), sec.end());
        if (!inserted && std::memcmp(sec.data(), "prt1", 4) == 0 &&
            pane_named(sec.data(), (uint32_t)sec.size(), "Pa_TitleBtn_00")) {
            std::vector<uint8_t> copy = sec;
            std::memset(copy.data() + 0xC, 0, 24);
            std::memcpy(copy.data() + 0xC, "Pa_SetBtn_00", 12);
            wf32(copy.data() + 0x2C, kColRight);
            wf32(copy.data() + 0x30, kRowBotY);
            replace_bytes(copy, (const uint8_t*)title_id, std::strlen(title_id) + 1, (const uint8_t*)set_id, std::strlen(set_id) + 1);
            replace_bytes(copy, ja, sizeof ja, en, sizeof en);
            if (copy.size() >= 0x60) {
                const uint32_t props = ru32(copy.data() + 0x54);
                for (uint32_t i = 0; i < props && i < 16; i++) {
                    const uint32_t prop = 0x60 + i * 40;
                    if (prop + 40 > copy.size()) break;
                    const uint32_t pane = ru32(copy.data() + prop + 28);
                    if (!pane || pane >= copy.size()) continue;
                    if (!settings_icon_name(copy.data() + pane, (uint32_t)copy.size() - pane)) continue;
                    hide_settings_icon(copy.data() + pane);
                }
            }
            out.insert(out.end(), copy.begin(), copy.end());
            inserted = true;
        }
        off += sz;
    }
    if (already || !inserted || off != file.size()) return false;
    uint32_t count = 0;
    for (uint32_t at = 0x14; at + 8 <= out.size();) {
        const uint32_t sz = ru32(out.data() + at + 4);
        if (sz < 8 || at + sz > out.size()) return false;
        count++;
        at += sz;
    }
    wu32(out.data() + 0xC, (uint32_t)out.size());
    out[0x10] = (uint8_t)(count >> 8);
    out[0x11] = (uint8_t)count;
    file.swap(out);
    return true;
}

static bool patch_menu_flan(std::vector<uint8_t>& file) {
    if (file.size() < 0x20 || std::memcmp(file.data(), "FLAN", 4) != 0) return false;
    const char* title = "Pa_TitleBtn_00";
    const char* set = "Pa_SetBtn_00";
    uint32_t pai = 0;
    uint32_t pai_sz = 0;
    for (uint32_t off = 0x14; off + 8 <= file.size();) {
        const uint32_t sz = ru32(file.data() + off + 4);
        if (sz < 8 || off + sz > file.size()) return false;
        if (std::memcmp(file.data() + off, "pai1", 4) == 0) {
            pai = off;
            pai_sz = sz;
            break;
        }
        off += sz;
    }
    if (!pai || pai_sz < 0x18) return false;
    uint8_t* sec = file.data() + pai;
    const uint16_t entries = ru16(sec + 0x0E);
    const uint32_t table = ru32(sec + 0x10);
    if (entries == 0 || entries > 64 || table + (uint32_t)entries * 4u > pai_sz) return false;
    int title_i = -1;
    for (uint16_t i = 0; i < entries; i++) {
        const uint32_t o = ru32(sec + table + (uint32_t)i * 4u);
        if (o + 28 > pai_sz) return false;
        if (std::memcmp(sec + o, set, 12) == 0 && sec[o + 12] == 0) return false;
        if (std::memcmp(sec + o, title, 15) == 0) title_i = (int)i;
    }
    if (title_i < 0) return false;
    const uint32_t start = ru32(sec + table + (uint32_t)title_i * 4u);
    uint32_t end = pai_sz;
    for (uint16_t i = 0; i < entries; i++) {
        const uint32_t o = ru32(sec + table + (uint32_t)i * 4u);
        if (o > start && o < end) end = o;
    }
    if (end <= start + 28) return false;
    std::vector<uint8_t> chunk(sec + start, sec + end);
    std::memset(chunk.data(), 0, 28);
    std::memcpy(chunk.data(), set, 12);
    const uint32_t table_end = table + (uint32_t)entries * 4u;
    std::vector<uint8_t> neu;
    neu.reserve(file.size() + 4 + chunk.size());
    neu.insert(neu.end(), file.begin(), file.begin() + pai + table_end);
    neu.insert(neu.end(), 4, 0);
    neu.insert(neu.end(), file.begin() + pai + table_end, file.begin() + pai + pai_sz);
    neu.insert(neu.end(), chunk.begin(), chunk.end());
    neu.insert(neu.end(), file.begin() + pai + pai_sz, file.end());
    for (uint16_t i = 0; i < entries; i++) {
        uint8_t* slot = neu.data() + pai + table + (uint32_t)i * 4u;
        wu32(slot, ru32(slot) + 4u);
    }
    wu32(neu.data() + pai + table + (uint32_t)entries * 4u, pai_sz + 4u);
    neu[pai + 0x0E] = (uint8_t)((entries + 1) >> 8);
    neu[pai + 0x0F] = (uint8_t)(entries + 1);
    wu32(neu.data() + pai + 4, pai_sz + 4u + (uint32_t)chunk.size());
    wu32(neu.data() + 0xC, (uint32_t)neu.size());
    file.swap(neu);
    return true;
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
    const uint32_t saved[11] = {c->r[0], c->r[3], c->r[4], c->r[5], c->r[6], c->r[7], c->r[8], c->r[9], c->r[10], c->r[11], c->r[12]};
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
    if (!start_menu_flyt(mem::ptr(flyt), bytes)) return;
    std::vector<uint8_t> file(bytes);
    std::memcpy(file.data(), mem::ptr(flyt), bytes);
    if (!patch_menu_flyt(file)) {
        restore_build_regs(c, saved, resource);
        static bool logged = false;
        if (!logged) {
            logged = true;
            LOG("[menu] settings button could not be inserted into the start menu");
        }
        return;
    }
    const uint32_t neu = guest_call(c, GC(0x029F1594u), {(uint32_t)file.size()});
    if (!guest_obj(neu)) {
        restore_build_regs(c, saved, resource);
        return;
    }
    std::memcpy(mem::ptr(neu), file.data(), file.size());
    if (direct) {
        restore_build_regs(c, saved, neu);
    } else {
        st32(slot, neu);
        restore_build_regs(c, saved, resource);
    }
    LOG("[menu] settings button added to the start menu");
}

static void install_settings_anim(Cpu* c) {
    if (!g_enabled.load()) return;
    const uint32_t pai = c->r[4];
    if (!guest_obj(pai) || ld32(pai) != 0x70616931u) return;
    uint32_t flan = 0;
    for (uint32_t back = 4; back <= 0x80; back += 4) {
        const uint32_t cand = pai - back;
        if (guest_obj(cand) && ld32(cand) == 0x464C414Eu) {
            flan = cand;
            break;
        }
    }
    if (!flan) return;
    const uint32_t bytes = ld32(flan + 0xC);
    if (bytes < 0x40 || bytes > 0x20000 || pai - flan >= bytes) return;
    const uint8_t* src = mem::ptr(flan);
    bool has_title = false;
    bool has_set = false;
    for (uint32_t i = 0; i + 15 < bytes; i++) {
        if (std::memcmp(src + i, "Pa_TitleBtn_00", 15) == 0) has_title = true;
        if (std::memcmp(src + i, "Pa_SetBtn_00", 12) == 0 && src[i + 12] == 0) has_set = true;
    }
    if (!has_title || has_set) return;
    const uint32_t resource = c->r[3];
    const uint32_t saved[11] = {
        c->r[0], resource, c->r[4], c->r[5], c->r[6], c->r[7], c->r[8], c->r[9], c->r[10], c->r[11], c->r[12]};
    std::vector<uint8_t> file(bytes);
    std::memcpy(file.data(), src, bytes);
    if (!patch_menu_flan(file)) return;
    const uint32_t neu = guest_call(c, GC(0x029F1594u), {(uint32_t)file.size()});
    if (!guest_obj(neu)) {
        restore_build_regs(c, saved, resource);
        return;
    }
    std::memcpy(mem::ptr(neu), file.data(), file.size());
    restore_build_regs(c, saved, resource);
    c->r[4] = neu + (pai - flan);
    c->r[6] = ld16(c->r[4] + 0x0E);
    static bool logged = false;
    if (!logged) {
        logged = true;
        LOG("[menu] settings button joined the menu animations");
    }
}

static void bind_settings(Cpu* c, uint32_t self) {
    if (!g_enabled.load() || g_settings) return;
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
    g_settings = guest_call(c, GC(0x0267273Cu), {self + 0x28, safe_ea});
    if (g_settings) LOG("[menu] settings button pane %08X", g_settings);
}

static void copy_name(char* dst, size_t n, const char* src) {
    if (n == 0) return;
    size_t i = 0;
    for (; i + 1 < n && src[i]; i++) dst[i] = src[i];
    dst[i] = 0;
}

static bool guest_label(uint32_t addr, char* dst, size_t n) {
    if (!guest_obj(addr)) return false;
    char buf[48];
    int len = 0;
    bool letter = false;
    for (; len < 40; len++) {
        const uint8_t c = ld8(addr + (uint32_t)len);
        if (c == 0) break;
        if (c < 32 || c > 126) return false;
        if ((c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z')) letter = true;
        buf[len] = (char)c;
    }
    if (len < 3 || !letter) return false;
    for (int i = 0; i < len; i++) {
        const char c = buf[i];
        const bool ok = (c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '_';
        if (!ok) return false;
    }
    buf[len] = 0;
    copy_name(dst, n, buf);
    return true;
}

static void button_name(uint32_t btn, int index, char* dst, size_t n) {
    dst[0] = 0;
    if (index == g_settings_idx && g_settings_idx >= 0) {
        copy_name(dst, n, "Settings");
        return;
    }
    for (uint32_t off = 0; off < 0x80; off += 4) {
        if (guest_label(ld32(btn + off), dst, n)) return;
    }
    char fallback[16];
    std::snprintf(fallback, sizeof fallback, "button %d", index);
    copy_name(dst, n, fallback);
}

void probe(MenuProbe& out) {
    out = {};
    const uint32_t self = g_menu.load();
    if (!guest_obj(self) || ld8(self + kActive) == 0) return;
    const uint32_t cursor = self + kCursor;
    out.open = true;
    out.cursor = (int)ld32(cursor + kCursorIndex);
    for (int i = 0; i < 41 && out.count < 16; i++) {
        const uint32_t btn = button_at(cursor, i);
        if (!btn) break;
        ButtonProbe& b = out.buttons[out.count];
        b.index = i;
        b.selected = i == out.cursor;
        const float x0 = (float)ldf32(btn + 0x40);
        const float y0 = (float)ldf32(btn + 0x44);
        const float x1 = (float)ldf32(btn + 0x48);
        const float y1 = (float)ldf32(btn + 0x4C);
        if (std::fabs(x1 - x0) > 1.f && std::fabs(y1 - y0) > 1.f && std::fabs(x0) < 4000.f && std::fabs(y0) < 4000.f) {
            b.rect = true;
            b.x0 = x0;
            b.y0 = y0;
            b.x1 = x1;
            b.y1 = y1;
        }
        button_name(btn, i, b.name, sizeof b.name);
        out.count++;
    }
}

bool visible() { return false; }

void prepare(float win_w, float win_h, bool overlay_open) {
    std::lock_guard<std::mutex> lock(g_hit_mu);
    g_hit_ok = false;
    g_win_w = win_w;
    g_win_h = win_h;
    if (!g_menu.load() || overlay_open) return;
    if (!overlay::game_content_rect(win_w, win_h, g_ox, g_oy, g_ow, g_oh)) return;
    g_hit_ok = true;
}

static bool over_tile_locked() {
    if (!g_hit_ok) return false;
    float sx = 0, sy = 0;
    if (!window_to_screen(g_nx, g_ny, g_win_w, g_win_h, g_ox, g_oy, g_ow, g_oh, sx, sy)) return false;
    return contains(settings_tile(), sx, sy);
}

bool pointer(float nx, float ny) {
    if (!g_enabled.load()) return false;
    std::lock_guard<std::mutex> lock(g_hit_mu);
    g_nx = nx;
    g_ny = ny;
    return over_tile_locked();
}

bool click() {
    if (!g_enabled.load()) return false;
    std::lock_guard<std::mutex> lock(g_hit_mu);
    if (!over_tile_locked()) return false;
    g_open_request.store(true);
    return true;
}

bool take_open() { return g_open_request.exchange(false); }

void draw() {}

}

extern "C" void hook_025A4218(Cpu* c) {
    const uint32_t self = c->r[3];
    map_menu::forget(self);
    f_025A4218_orig(c);
}

extern "C" void site_026730C4(Cpu* c) {
    map_menu::install_settings_button(c, c->r[26]);
}

extern "C" void site_02AB7040(Cpu* c) {
    map_menu::install_settings_anim(c);
}

extern "C" void site_02682674(Cpu* c) {
    map_menu::fit_settings_hit(c->r[27], c->r[30]);
}

extern "C" void site_025A4C5C(Cpu* c) {
    map_menu::settle_settings(c->r[30], false);
    map_menu::apply_choice(c->r[30]);
    map_menu::clear_choice();
}

extern "C" void hook_025A42CC(Cpu* c) {
    const uint32_t self = c->r[3];
    f_025A42CC_orig(c);
    map_menu::bind_settings(c, self);
    map_menu::nudge(self);
}

extern "C" void hook_025A4B98(Cpu* c) {
    const uint32_t self = c->r[3];
    const bool active = ld8(self + map_menu::kActive) != 0;
    if (active) {
        map_menu::bind_settings(c, self);
        c->r[3] = self;
        map_menu::on_pad(self);
        map_menu::watch_settings_click(c, self);
        map_menu::settle_settings(self, true);
        c->r[3] = self;
    }
    c->r[3] = self;
    f_025A4B98_orig(c);
    if (!active) {
        map_menu::forget(self);
        return;
    }
    map_menu::nudge(self);
    map_menu::arm_settings(self);
    map_menu::g_menu.store(self);
    map_menu::blank_cursor();
    map_menu::place_yellow(c, self);
    map_menu::release_menu_gate(self);
    c->r[3] = self;
}
