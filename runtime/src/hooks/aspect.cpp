

#include "gfx/aspect.h"
#include "gfx/aspect_panes.h"
#include "gfx/aspect_view.h"
#include "mods/cemu_pack.h"

#include <algorithm>
#include <atomic>
#include <cmath>
#include <cstdlib>
#include <cstring>
#include <mutex>
#include <unordered_map>
#include <vector>

#include "gx2/gx2.h"
#include "gx2/gx2_cmd.h"
#include "core/runtime.h"

#include "core/env.h"
namespace aspect {
namespace {
constexpr float kBase = 16.0f / 9.0f;

int parse_env_mode(float& custom) { return mode_from_text(env::ASPECT(), custom); }
float g_custom = kBase;
std::atomic<int> g_mode{parse_env_mode(g_custom)};
std::atomic<float> g_window{kBase};

std::atomic<float> g_game{kBase};
std::atomic<float> g_draw{kBase};
std::mutex g_fovy_mu;
std::unordered_map<uint32_t, float> g_fovy;

float clamp_aspect(float a) { return std::fmin(std::fmax(a, 1.0f), 4.0f); }

void factors(float a, float& kx, float& ky) {
    kx = a >= kBase ? a / kBase : 1.0f;
    ky = a >= kBase ? 1.0f : kBase / a;
}

bool same(float a, float b) { return std::fabs(a - b) <= 1e-4f * std::fmax(1.0f, std::fabs(a)); }

float g_stored[4] = {};
bool ours(float v) {
    for (float x : g_stored)
        if (x && same(v, x)) return true;
    return false;
}
void remember(float v) {
    if (same(v, kBase) || ours(v)) return;
    memmove(g_stored + 1, g_stored, sizeof g_stored - sizeof *g_stored);
    g_stored[0] = v;
}
bool standard_aspect(float v) { return same(v, kBase) || same(v, g_game) || ours(v); }
float vert_plus_fovy(float fovyDeg, float ky) {
    if (ky == 1.0f) return fovyDeg;
    float t = std::tan(fovyDeg * 0.5f * (float)M_PI / 180.0f) * ky;
    return 2.0f * std::atan(t) * 180.0f / (float)M_PI;
}

bool original() { return g_game == kBase; }
void adjust_projection_args(Cpu* c, int fovyReg, int aspectReg) {
    if (original() && !ours((float)c->f[aspectReg].ps0)) return;
    if (!standard_aspect((float)c->f[aspectReg].ps0)) return;
    remember(g_game);
    float kx, ky;
    factors(g_game, kx, ky);
    c->f[aspectReg].ps0 = c->f[aspectReg].ps1 = g_game;
    c->f[fovyReg].ps0 = c->f[fovyReg].ps1 = vert_plus_fovy((float)c->f[fovyReg].ps0, ky);
}
}

const char* mode_name(int m) {
    static const char* n[kModes] = {"16:9 (original)", "Match window", "16:10", "21:9", "32:9", "custom"};
    return m >= 0 && m < kModes ? n[m] : "?";
}
float mode_value(int m) {
    switch (m) {
    case kOriginal: return kBase;
    case k16x10: return 16.0f / 10.0f;
    case k21x9: return 64.0f / 27.0f;
    case k32x9: return 32.0f / 9.0f;
    case kCustom: return g_custom;
    default: return 0;
    }
}
int mode() { return g_mode.load(std::memory_order_relaxed); }
void set_mode(int m) {
    if (m < 0 || m >= kModes) return;
    g_mode = m;
    LOG("[aspect] %s", mode_name(m));
}
void apply_choice(const char* text) {
    float custom = g_custom;
    int m = mode_from_text(text, custom);
    if (m == kCustom) g_custom = custom;
    set_mode(m);
}
void choice_text(char* out, size_t n) { text_from_mode(mode(), g_custom, out, n); }
void set_window_aspect(float a) {
    if (!(a > 0.1f && a < 20.0f)) return;

    float q = std::round(a * 200.0f) / 200.0f;
    if (std::fabs(q - kBase) < 0.006f) q = kBase;
    g_window.store(q, std::memory_order_relaxed);
}
float requested() {
    if(float pack=mods::cemu::aspect_ratio())return pack;
    int m = mode();
    return clamp_aspect(m == kWindow ? g_window.load(std::memory_order_relaxed) : mode_value(m));
}
float game() { return g_game; }
static uint64_t g_swaps = 0, g_changed_swap = 0;
uint64_t game_frame() { return g_swaps; }

float on_swap() {

    static uint64_t swaps = 0;
    static std::vector<std::pair<uint64_t, float>> at = [] {
        std::vector<std::pair<uint64_t, float>> v;
        if (const char* e = env::ASPECT_AT())
            for (char* p = (char*)e; *p;) {
                uint64_t f = strtoull(p, &p, 10);
                if (*p++ != ':') break;
                v.push_back({f, (float)strtod(p, &p)});
                while (*p == ',') p++;
            }
        return v;
    }();
    swaps++;
    g_swaps = swaps;
    for (auto& [f, v] : at)
        if (f == swaps) {
            if (v == 0) set_mode(kWindow);
            else if (same(v, kBase)) set_mode(kOriginal);
            else { g_custom = v; set_mode(kCustom); }
        }
    float a = requested();
    bool changed = !same(a, g_game);

    float render = g_game.load();
    g_draw.store(render, std::memory_order_relaxed);
    g_game = a;
    if (changed) {
        LOG("[aspect] game aspect %.4f from swap %llu", a, (unsigned long long)swaps);
        g_changed_swap = swaps;
    }
    return render;
}

}

using namespace aspect;

static void store_aspect(Cpu* c) {
    if (original() || !standard_aspect((float)c->f[0].ps0)) return;
    remember(game());
    c->f[0].ps0 = c->f[0].ps1 = game();
}
extern "C" void site_024FFA98(Cpu* c) { store_aspect(c); }
extern "C" void site_025020E0(Cpu* c) { store_aspect(c); }

extern "C" void site_024FFD60(Cpu* c) { adjust_projection_args(c, 1, 2); }

extern "C" void site_024F811C(Cpu* c) { adjust_projection_args(c, 1, 2); }
extern "C" void site_024F8168(Cpu* c) { adjust_projection_args(c, 1, 2); }

extern "C" void site_025ADB38(Cpu* c) { adjust_projection_args(c, 1, 2); }

extern "C" {
void f_02874038_orig(Cpu* c);
void f_02ABC054_orig(Cpu* c);
void f_02ABCAE8_orig(Cpu* c);
}
namespace aspect {
namespace {
constexpr uint32_t kPaneFlags = 0x44, kPaneMtxDirty = 0x10, kPaneGlobal = 0x48;
constexpr uint32_t kPaneParent = 0x0C, kPaneChildren = 0x14, kPaneTrans = 0x1C, kPaneScale = 0x34, kPaneSize = 0x3C, kPaneName = 0x80;

std::mutex g_root_mu;
std::vector<std::pair<uint32_t, bool>> g_root_tv;
bool root_on_tv(uint32_t root) {
    std::lock_guard<std::mutex> lk(g_root_mu);
    for (auto& [r, tv] : g_root_tv)
        if (r == root) return tv;
    return true;
}
void set_root_tv(uint32_t root, bool tv) {
    std::lock_guard<std::mutex> lk(g_root_mu);
    for (auto& e : g_root_tv)
        if (e.first == root) { e.second = tv; return; }
    if (g_root_tv.size() > 256) g_root_tv.erase(g_root_tv.begin());
    g_root_tv.push_back({root, tv});
}

std::vector<std::pair<uint32_t, bool>> g_root_calc;
bool anchor_changed(uint32_t root, bool anchor) {
    std::lock_guard<std::mutex> lk(g_root_mu);
    for (auto& e : g_root_calc)
        if (e.first == root) {
            bool ch = e.second != anchor;
            e.second = anchor;
            return ch;
        }
    if (g_root_calc.size() > 256) g_root_calc.erase(g_root_calc.begin());
    g_root_calc.push_back({root, anchor});
    return true;
}

std::unordered_map<uint32_t, std::pair<float, float>> g_offsets;
void set_offset(uint32_t pane, float dx, float dy) {
    std::lock_guard<std::mutex> lk(g_root_mu);
    if (dx == 0.0f && dy == 0.0f) g_offsets.erase(pane);
    else g_offsets[pane] = {dx, dy};
}
std::pair<float, float> offset_of(uint32_t pane) {
    std::lock_guard<std::mutex> lk(g_root_mu);
    auto it = g_offsets.find(pane);
    return it == g_offsets.end() ? std::pair<float, float>{0, 0} : it->second;
}

bool name_is(uint32_t pane, const char* n) { return !strncmp((const char*)mem::ptr(pane + kPaneName), n, 24); }

bool is_hud_edge_pane(uint32_t pane) {
    const char* n = (const char*)mem::ptr(pane + kPaneName);
    return panes::edge_anchor(std::string_view(n, strnlen(n, 24)));
}

struct DrawShift { uint32_t anchor; float dx, dy; };
thread_local std::vector<DrawShift> t_draw_shift;

bool under_pane(uint32_t pane, uint32_t ancestor) {
    for (uint32_t p = pane; p; p = ld32(p + kPaneParent))
        if (p == ancestor) return true;
    return false;
}

bool has_world_anchor(uint32_t pane, int depth = 0) {
    if (depth > 32) return false;
    const char* name = (const char*)mem::ptr(pane + kPaneName);
    if (panes::projected_root_name(std::string_view(name, strnlen(name, 24)))) return true;
    uint32_t sentinel = pane + kPaneChildren;
    for (uint32_t child = ld32(sentinel); child != sentinel; child = ld32(child))
        if (has_world_anchor(child, depth + 1)) return true;
    return false;
}

thread_local uint32_t t_root = 0;
thread_local bool t_anchor = false;

constexpr uint32_t kTextDrawSelf = 0x02ABE3B8u;
struct ScaledText { uint32_t pane; uint32_t info; float proj[16]; };
std::vector<ScaledText> t_scaled_text;

bool is_text_pane(uint32_t pane) {
    uint32_t vt = ld32(pane + 8);
    if (vt < 0x10000000u) return false;
    return ld32(vt + 0x94) == kTextDrawSelf;
}
void push_text_scale(uint32_t pane, uint32_t info) {
    float kx, ky;
    factors(g_game, kx, ky);
    if ((kx == 1.0f && ky == 1.0f) || !info) return;
    ScaledText s{};
    s.pane = pane;
    s.info = info;
    for (int i = 0; i < 16; i++) s.proj[i] = (float)ldf32(info + uint32_t(i) * 4);
    float scaled[16];
    for (int i = 0; i < 16; i++) scaled[i] = s.proj[i];
    view::scale_guest_ortho(scaled, 16, 1280.0f, 720.0f, kx, ky);
    for (int i = 0; i < 16; i++) stf32(info + uint32_t(i) * 4, scaled[i]);
    t_scaled_text.push_back(s);
}
void pop_text_scale(uint32_t pane) {
    if (t_scaled_text.empty() || t_scaled_text.back().pane != pane) return;
    ScaledText s = t_scaled_text.back();
    t_scaled_text.pop_back();
    for (int i = 0; i < 16; i++) stf32(s.info + uint32_t(i) * 4, s.proj[i]);
}
}

void scale_edge_text_draw(uint32_t pane, uint32_t info, bool edge) {
    if (edge && is_text_pane(pane)) push_text_scale(pane, info);
}
void restore_edge_text_draw(uint32_t pane) { pop_text_scale(pane); }
}

extern "C" void hook_02ABC054(Cpu* c) {
    using namespace aspect;
    uint32_t pane = c->r[3];
    uint32_t parent = ld32(pane + kPaneParent);
    float kx, ky;
    factors(g_game, kx, ky);
    if (!parent) {
        uint32_t saveRoot = t_root;
        bool saveAnchor = t_anchor;
        t_root = pane;
        t_anchor = (kx != 1.0f || ky != 1.0f) && root_on_tv(pane);

        float tx = (float)ldf32(pane + kPaneTrans), ty = (float)ldf32(pane + kPaneTrans + 4);
        bool placed = t_anchor && (tx != 0.0f || ty != 0.0f) && has_world_anchor(pane);

        static const bool log_roots = env::ASPECT_LOG() != nullptr;
        if (log_roots) {
            static std::mutex mu;
            static std::unordered_map<std::string, int> seen;
            std::string nm((const char*)mem::ptr(pane + kPaneName), strnlen((const char*)mem::ptr(pane + kPaneName), 24));
            std::lock_guard<std::mutex> lk(mu);
            if (seen[nm]++ == 0) LOG("[aspect] root '%s' anchor=%d at %.1f, %.1f", nm.c_str(), t_anchor ? 1 : 0, tx, ty);
        }

        if (anchor_changed(pane, t_anchor) || (g_changed_swap && g_swaps - g_changed_swap < 4)) c->r[5] = 1;
        if (placed) { stf32(pane + kPaneTrans, tx * kx); stf32(pane + kPaneTrans + 4, ty * ky); }
        set_offset(pane, placed ? tx * (kx - 1.0f) : 0.0f, placed ? ty * (ky - 1.0f) : 0.0f);
        f_02ABC054_orig(c);
        if (placed) { stf32(pane + kPaneTrans, tx); stf32(pane + kPaneTrans + 4, ty); }
        t_root = saveRoot;
        t_anchor = saveAnchor;
        return;
    }
    if (!t_anchor) {
        if (parent == t_root) set_offset(pane, 0, 0);
        f_02ABC054_orig(c);
        return;
    }
    float tx = (float)ldf32(pane + kPaneTrans), ty = (float)ldf32(pane + kPaneTrans + 4);
    float sx = (float)ldf32(pane + kPaneScale), sy = (float)ldf32(pane + kPaneScale + 4);
    float nx = tx, ny = ty, nsx = sx, nsy = sy;
    panes::Role role = panes::Role::Content;
    if (is_hud_edge_pane(pane)) role = panes::Role::Hud;
    else if (parent == t_root && (name_is(pane, "L_EnemyHP_00") || name_is(pane, "L_CommandA_00"))) role = panes::Role::Projected;
    auto transformed = panes::transform(role, tx, ty, sx, sy, kx, ky);
    nx = transformed.x; ny = transformed.y;

    uint32_t sentinel = pane + kPaneChildren;
    if (ld32(sentinel) == sentinel && std::fabs(tx) < 8.0f && std::fabs(ty) < 8.0f) {
        float w = (float)ldf32(pane + kPaneSize) * std::fabs(sx), h = (float)ldf32(pane + kPaneSize + 4) * std::fabs(sy);
        if (panes::fill(std::string_view((const char*)mem::ptr(pane+kPaneName), strnlen((const char*)mem::ptr(pane+kPaneName),24)), true, tx, ty, w, h)) {
            auto stretched = panes::transform(panes::Role::Fill, tx, ty, sx, sy, kx, ky);
            nsx = stretched.sx; nsy = stretched.sy;
            static const bool log_st = env::ASPECT_LOG() != nullptr;
            if (log_st) {
                static std::mutex mu;
                static std::unordered_map<std::string, int> seen;
                std::string key = std::string((const char*)mem::ptr(t_root + kPaneName), strnlen((const char*)mem::ptr(t_root + kPaneName), 24)) +
                                  "/" + std::string((const char*)mem::ptr(pane + kPaneName), strnlen((const char*)mem::ptr(pane + kPaneName), 24));
                std::lock_guard<std::mutex> lk(mu);
                if (seen[key]++ == 0) LOG("[aspect] stretched '%s' (vtable %08X) %.0fx%.0f", key.c_str(), ld32(pane), w, h);
            }
        }
    }
    bool moved = nx != tx || ny != ty, scaled = nsx != sx || nsy != sy;
    static const bool log_moves = env::ASPECT_LOG() != nullptr;
    if (log_moves && parent == t_root) {
        static std::mutex mu;
        static std::unordered_map<std::string, int> seen;
        auto nm = [](uint32_t p) { return std::string((const char*)mem::ptr(p + kPaneName), strnlen((const char*)mem::ptr(p + kPaneName), 24)); };
        std::string key = nm(t_root) + "/" + nm(pane);
        std::lock_guard<std::mutex> lk(mu);
        if (seen[key]++ == 0) LOG("[aspect] child '%s' role=%d %.1f,%.1f -> %.1f,%.1f", key.c_str(), int(role), tx, ty, nx, ny);
    }
    if (parent == t_root || role == panes::Role::Hud) set_offset(pane, nx - tx, ny - ty);
    if (moved || scaled) st8(pane + kPaneFlags, ld8(pane + kPaneFlags) | kPaneMtxDirty);
    if (moved) { stf32(pane + kPaneTrans, nx); stf32(pane + kPaneTrans + 4, ny); }
    if (scaled) { stf32(pane + kPaneScale, nsx); stf32(pane + kPaneScale + 4, nsy); }
    f_02ABC054_orig(c);
    if (moved) { stf32(pane + kPaneTrans, tx); stf32(pane + kPaneTrans + 4, ty); }
    if (scaled) { stf32(pane + kPaneScale, sx); stf32(pane + kPaneScale + 4, sy); }
}

extern "C" void hook_02ABCAE8(Cpu* c) {
    using namespace aspect;
    uint32_t pane = c->r[3];
    static const uint64_t trace_frame = env::ASPECT_TRACE_FRAME.u64_or(0);
    if (trace_frame && game_frame() == trace_frame) {
        auto trace = [](auto&& self, uint32_t p, int depth) -> void {
            if (depth > 32) return;
            LOG("[pane] %08X parent=%08X name=%.24s flags=%02X base=%02X t=%.3f,%.3f size=%.3f,%.3f global=%.3f,%.3f scale=%.3f,%.3f", p, ld32(p + kPaneParent), (const char*)mem::ptr(p+kPaneName), ld8(p+kPaneFlags), ld8(p+0x47), (float)ldf32(p+kPaneTrans), (float)ldf32(p+kPaneTrans+4), (float)ldf32(p+kPaneSize), (float)ldf32(p+kPaneSize+4), (float)ldf32(p+kPaneGlobal+12), (float)ldf32(p+kPaneGlobal+28), (float)ldf32(p+kPaneGlobal), (float)ldf32(p+kPaneGlobal+20));
            uint32_t sentinel=p+kPaneChildren;
            for(uint32_t ch=ld32(sentinel);ch!=sentinel;ch=ld32(ch)) self(self,ch,depth+1);
        };
        if (!ld32(pane+kPaneParent)) trace(trace,pane,0);
    }
    if (original()) { f_02ABCAE8_orig(c); return; }
    uint32_t parent = ld32(pane + kPaneParent);
    if (!parent) {
        gx2::emit(gx2::OP_LAYOUT_ROOT, {pane});
    } else {

        uint32_t child = pane, root = parent;
        for (int i = 0; i < 16 && ld32(root + kPaneParent); i++) { child = root; root = ld32(root + kPaneParent); }
        bool world = !ld32(root + kPaneParent) &&
            (ldf32(root + kPaneTrans) != 0 || ldf32(root + kPaneTrans + 4) != 0) && has_world_anchor(root);
        if (!ld32(root + kPaneParent) && root_on_tv(root) &&
            (world || is_hud_edge_pane(child) || name_is(child, "L_EnemyHP_00") || name_is(child, "L_CommandA_00"))) {
            auto a = offset_of(child), b = offset_of(root);
            float gx = (float)ldf32(pane + kPaneGlobal + 12) - a.first - b.first;
            float gy = (float)ldf32(pane + kPaneGlobal + 28) - a.second - b.second;
            if (panes::parked_hud(gx, gy)) return;
        }
    }
    float dx = 0, dy = 0;
    bool pushed = false;
    if (parent && is_hud_edge_pane(pane) && gx2::wide_color_target()) {
        float kx, ky;
        factors(g_game, kx, ky);
        float tx = (float)ldf32(pane + kPaneTrans), ty = (float)ldf32(pane + kPaneTrans + 4);
        auto placed = panes::transform(panes::Role::Hud, tx, ty, 1, 1, kx, ky);
        float want_x = placed.x - tx, want_y = placed.y - ty;
        float baked_x = (float)ldf32(pane + kPaneGlobal + 12) - tx;
        float baked_y = (float)ldf32(pane + kPaneGlobal + 28) - ty;
        if ((want_x != 0.0f || want_y != 0.0f) &&
            (std::fabs(baked_x - want_x) > 2.0f || std::fabs(baked_y - want_y) > 2.0f)) {
            t_draw_shift.push_back({pane, want_x, want_y});
            pushed = true;
            dx = want_x;
            dy = want_y;
        }
    } else if (parent && !t_draw_shift.empty() && under_pane(pane, t_draw_shift.back().anchor)) {
        dx = t_draw_shift.back().dx;
        dy = t_draw_shift.back().dy;
    }
    float old_x = 0, old_y = 0;
    if (dx != 0.0f || dy != 0.0f) {
        old_x = (float)ldf32(pane + kPaneGlobal + 12);
        old_y = (float)ldf32(pane + kPaneGlobal + 28);
        stf32(pane + kPaneGlobal + 12, old_x + dx);
        stf32(pane + kPaneGlobal + 28, old_y + dy);
    }
    f_02ABCAE8_orig(c);
    if (dx != 0.0f || dy != 0.0f) {
        stf32(pane + kPaneGlobal + 12, old_x);
        stf32(pane + kPaneGlobal + 28, old_y);
    }
    if (pushed) t_draw_shift.pop_back();
}

static thread_local int t_tag = 0;
static void with_tv_projection(Cpu* c, void (*fn)(Cpu*)) {
    if (aspect::original()) { fn(c); return; }
    t_tag++;
    fn(c);
    t_tag--;
}
namespace aspect {
bool tagged_projection() { return t_tag > 0; }
void layout_root_target(uint32_t root, bool tv) { set_root_tv(root, tv); }
}

extern "C" void hook_02874038(Cpu* c) { with_tv_projection(c, f_02874038_orig); }

extern "C" void f_028F8250_orig(Cpu* c);
extern "C" void hook_028F8250(Cpu* c) { with_tv_projection(c, f_028F8250_orig); }

static void adjust_perspective(Cpu* c) {
    float fovy = (float)c->f[3].ps0, aspect_arg = (float)c->f[4].ps0;
    auto p = aspect::view::expand_perspective(fovy, aspect_arg, aspect::g_draw.load(std::memory_order_relaxed));
    c->f[3].ps0 = c->f[3].ps1 = p.fovy;
    c->f[4].ps0 = c->f[4].ps1 = p.aspect;
}

extern "C" void f_02A06818_orig(Cpu* c);
extern "C" void hook_02A06818(Cpu* c) {
    auto o = aspect::view::expand_ortho((float)c->f[1].ps0, (float)c->f[2].ps0, (float)c->f[3].ps0, (float)c->f[4].ps0,
                                         aspect::g_draw.load(std::memory_order_relaxed));
    c->f[1].ps0 = c->f[1].ps1 = o.top;
    c->f[2].ps0 = c->f[2].ps1 = o.bottom;
    c->f[3].ps0 = c->f[3].ps1 = o.left;
    c->f[4].ps0 = c->f[4].ps1 = o.right;
    f_02A06818_orig(c);
}

extern "C" void f_02A061AC_orig(Cpu* c);
extern "C" void hook_02A061AC(Cpu* c) {
    float fovy = (float)c->f[3].ps0, aspect_arg = (float)c->f[4].ps0;
    adjust_perspective(c);
    f_02A061AC_orig(c);
    if (aspect::view::near_base(aspect_arg) && c->r[3] >= 0x10000000u) {
        std::lock_guard<std::mutex> lk(aspect::g_fovy_mu);
        aspect::g_fovy[c->r[3]] = fovy;
    }
}

extern "C" void f_02A062EC_orig(Cpu* c);
extern "C" void hook_02A062EC(Cpu* c) {
    adjust_perspective(c);
    f_02A062EC_orig(c);
}

extern "C" void f_022A7760_orig(Cpu* c);
extern "C" void hook_022A7760(Cpu* c) {
    uint32_t self = c->r[3];
    if (self >= 0x10000000u) {
        uint32_t proj = self + 168;
        float stored = (float)ldf32(proj + 172), fovy = (float)ldf32(proj + 156), base = fovy;
        if (aspect::view::near_base(stored)) {
            std::lock_guard<std::mutex> lk(aspect::g_fovy_mu);
            aspect::g_fovy[proj] = fovy;
        } else {
            std::lock_guard<std::mutex> lk(aspect::g_fovy_mu);
            auto it = aspect::g_fovy.find(proj);
            if (it != aspect::g_fovy.end()) base = it->second;
            else base = 0;
        }
        if (base > 0.0f) {
            auto p = aspect::view::expand_perspective(base, aspect::view::kBase, aspect::g_draw.load(std::memory_order_relaxed));
            if (std::fabs(p.fovy - fovy) > 1e-6f || std::fabs(p.aspect - stored) > 1e-5f) {
                Cpu g = *c;
                g.r[3] = proj;
                g.f[1].ps0 = g.f[1].ps1 = ldf32(proj + 148);
                g.f[2].ps0 = g.f[2].ps1 = ldf32(proj + 152);
                g.f[3].ps0 = g.f[3].ps1 = p.fovy;
                g.f[4].ps0 = g.f[4].ps1 = p.aspect;
                f_02A062EC_orig(&g);
            }
        }
    }
    f_022A7760_orig(c);
}

void aspect::ss_reset() {
    std::lock_guard<std::mutex> lk(aspect::g_root_mu);
    aspect::g_root_calc.clear();
    std::lock_guard<std::mutex> fk(aspect::g_fovy_mu);
    aspect::g_fovy.clear();
}

extern "C" void site_02ABCB34(Cpu* c) {
    using namespace aspect;
    if (original()) return;
    uint32_t pane = c->r[3];
    bool content = true;
    bool edge = false;
    const char* name = (const char*)mem::ptr(pane + kPaneName);
    uint32_t sentinel = pane + kPaneChildren;
    if (panes::fill(std::string_view(name, strnlen(name, 24)), ld32(sentinel) == sentinel,
                    (float)ldf32(pane + kPaneTrans), (float)ldf32(pane + kPaneTrans + 4),
                    (float)ldf32(pane + kPaneSize) * std::fabs((float)ldf32(pane + kPaneScale)),
                    (float)ldf32(pane + kPaneSize + 4) * std::fabs((float)ldf32(pane + kPaneScale + 4)))) content = false;
    for (uint32_t p = pane; p; p = ld32(p + kPaneParent)) {
        uint32_t parent = ld32(p + kPaneParent);
        if (is_hud_edge_pane(p)) { content = false; edge = true; }
        if (parent && !ld32(parent + kPaneParent) &&
            (name_is(p, "L_EnemyHP_00") || name_is(p, "L_CommandA_00"))) content = false;
        if (!parent && (ldf32(p + kPaneTrans) != 0 || ldf32(p + kPaneTrans + 4) != 0) &&
            has_world_anchor(p)) content = false;
    }
    gx2::emit(gx2::OP_LAYOUT_CONTENT, {content ? 1u : 0u, edge ? 1u : 0u});
    aspect::scale_edge_text_draw(pane, c->r[4], edge);
}
extern "C" void site_02ABCB38(Cpu* c) {
    if (aspect::original()) return;
    aspect::restore_edge_text_draw(c->r[30]);
    gx2::emit(gx2::OP_LAYOUT_CONTENT, {0, 0});
}
namespace aspect {
thread_local bool g_content_clip = false;
void set_content_clip(bool clip) { g_content_clip = clip; }
bool content_clip() { return g_content_clip; }
thread_local bool g_edge_hud = false;
void set_edge_hud(bool edge) { g_edge_hud = edge; }
bool edge_hud() { return g_edge_hud; }
thread_local bool g_cover_draw = false;
void set_cover_draw(bool cover) { g_cover_draw = cover; }
bool cover_draw() { return g_cover_draw; }
}
