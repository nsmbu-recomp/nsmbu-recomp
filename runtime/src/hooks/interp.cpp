#include "hooks/interp.h"
#include "mods/packages.h"
#include "mods/sdk/nsmbu.h"

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <unordered_map>
#include <vector>

#include "hooks/interp_joints.h"
#include "hooks/interp_pacing.h"
#include "hooks/interp_picture.h"
#include "guest_addr.h"
#include "gfx/display_modes.h"
#include "gfx/render_prof.h"
#include "perf_metrics.h"
#include "core/runtime.h"
#include "core/write_watch.h"
#include "save/savestate.h"
#include "test/status_trace.h"

#include "core/env.h"
extern "C" {
void f_029C52CC_orig(Cpu* c);
void f_029C4FE4_orig(Cpu* c);
void f_029FD94C_orig(Cpu* c);
void f_024FBF6C_orig(Cpu* c);
void f_024F30DC_orig(Cpu* c);
void f_029C5104(Cpu* c);
void f_0268B70C_orig(Cpu* c);
void f_024FA76C(Cpu* c);
void f_02A05A9C(Cpu* c);
void nsmbu_align_map_camera(void);
}

namespace gx2 { bool uncapped(); void set_uncapped(bool on); int gpu_frames_queued(); void sync_submitted(); }

namespace interp {

using pacing::valid_fps;
static bool listed_fps(int f) {
    int n = 0;
    const int* rates = pacing::fps_ladder(n);
    for (int i = 0; i < n; i++) if (rates[i] == f) return true;
    return false;
}
static int read_env_fps() {
    const char* e = env::INTERP_FPS();
    if (!e || !*e) return 0;
    int f = atoi(e);
    if (f < 1) return 0;
    if (f > pacing::kCustomFpsMax) f = pacing::kCustomFpsMax;
    if (!listed_fps(f)) f = pacing::clamp_custom_fps(f);
    return f;
}
static const int g_env_fps = read_env_fps();
static const bool g_env_custom = g_env_fps && !listed_fps(g_env_fps);
static std::atomic<bool> g_custom_on{g_env_custom};
static std::atomic<int> g_custom_fps{g_env_custom ? g_env_fps : 90};
static std::atomic<int> g_fps{!g_env_fps || g_env_custom ? 60 : g_env_fps};
static std::atomic<bool> g_unlocked{false};
static std::atomic<bool> g_on{[] {
    const char* e = env::INTERP();
    return (e ? atoi(e) != 0 : g_env_fps != 0);
}()};
static std::atomic<bool> g_blend{[] {
    const char* e = env::INTERP();
    if (e && *e) return atoi(e) != 0;
    return true;
}()};
bool blend_enabled() { return g_blend.load(std::memory_order_relaxed); }
bool interp_on() { return g_on.load(std::memory_order_relaxed); }

static std::atomic<int> g_granule{4};

bool enabled() { return unlocked() || interp_on(); }
int fps() {
    if (g_custom_on.load(std::memory_order_relaxed)) return g_custom_fps.load(std::memory_order_relaxed);
    return g_fps.load(std::memory_order_relaxed);
}
bool custom_on() { return g_custom_on.load(std::memory_order_relaxed); }
int custom_fps() { return g_custom_fps.load(std::memory_order_relaxed); }
bool fast_forward() { return gx2::uncapped(); }
void set_fast_forward(bool on) {
    if (on) g_unlocked.store(false, std::memory_order_relaxed);
    gx2::set_uncapped(on);
}
void pose_reset();
void apply_pose_reset();
void set_unlocked(bool on) {
    if (g_unlocked.exchange(on, std::memory_order_relaxed) == on) return;
    pose_reset();
    if (on) {
        gx2::set_uncapped(false);
        g_custom_on.store(false, std::memory_order_relaxed);
        g_on.store(true, std::memory_order_relaxed);
    } else if (fps() == 60) {
        g_on.store(false, std::memory_order_relaxed);
    }
    LOG("[interp] unlocked fps %s", on ? "on: pictures are drawn as fast as they can, and the game stays at 60 logic steps per second" : "off");
}
bool unlocked() { return g_unlocked.load(std::memory_order_relaxed); }

static const int g_env_hz = env::DISPLAY_HZ.integer_or(-1);
static std::atomic<int> g_display_hz{0};
static std::atomic<bool> g_present_vsync{true};
int display_hz() { return g_env_hz >= 0 ? g_env_hz : g_display_hz.load(std::memory_order_relaxed); }
bool present_vsync() { return g_present_vsync.load(std::memory_order_relaxed); }
int output_fps() {
    const int f = std::max(1, fps());
    int out = f;
    if (g_custom_on.load(std::memory_order_relaxed)) {
        const int hz = display_hz();
        if (present_vsync() && hz > 0) out = std::min(f, std::max(1, hz));
    } else {
        out = pacing::cap_fps(f, display_hz(), present_vsync());
    }
    return pacing::pace_fps(out, blend_enabled(), fast_forward());
}
void set_display_hz(int hz) {
    if (hz < 0) hz = 0;
    if (!hz) return;
    const int old = g_display_hz.exchange(hz);
    if (old == hz) return;
    if (g_env_hz >= 0) LOG("[interp] display refresh rate %d Hz (NSMBU_DISPLAY_HZ=%d is used)", hz, g_env_hz);
    else LOG("[interp] display refresh rate %d Hz: frame interpolation draws up to %d fps", hz, pacing::cap_fps(240, hz, present_vsync()));
}
void set_present_vsync(bool on) {
    if (g_present_vsync.exchange(on) != on) LOG("[interp] presentation %s the display's vsync", on ? "waits for" : "does not wait for");
}
static const char* fps_name(int f) {
    static char buf[32];
    if (g_custom_on.load(std::memory_order_relaxed)) {
        snprintf(buf, sizeof buf, "%d fps", f);
        return buf;
    }
    switch (f) {
    case 30: return "30 fps";
    case 120: return "120 fps";
    case 144: return "144 fps";
    case 165: return "165 fps";
    case 240: return "240 fps";
    default: return "60 fps";
    }
}
void set_enabled(bool v) {
    if (v && fps() == 60 && !unlocked()) v = false;
    if (g_on.exchange(v) == v) return;
    pose_reset();
    LOG("[interp] frame interpolation %s", v ? fps_name(fps()) : "off");
}
void set_fps(int f) {
    g_unlocked.store(false, std::memory_order_relaxed);
    g_custom_on.store(false, std::memory_order_relaxed);
    f = valid_fps(f);
    if (g_fps.exchange(f) == f) return;
    pose_reset();
    if (interp_on()) LOG("[interp] frame interpolation at %s", fps_name(f));
}
void set_custom_fps(int f) {
    g_unlocked.store(false, std::memory_order_relaxed);
    f = pacing::clamp_custom_fps(f);
    const int old = g_custom_fps.exchange(f);
    const bool was = g_custom_on.exchange(true);
    if (f == 60) {
        if (g_on.exchange(false)) pose_reset();
        LOG("[interp] frame interpolation off");
        return;
    }
    if (old != f || !was) pose_reset();
    set_enabled(true);
}
int mode() { return unlocked() || (interp_on() && fps() != 60) ? 1 : 0; }
void set_mode(int m) {
    if (m == 0) g_unlocked.store(false, std::memory_order_relaxed);
    if (m != 1) {
        if (g_on.exchange(false)) pose_reset();
        return;
    }
    set_enabled(true);
}

void toggle_fps(int f) {
    if (mode() == 1 && fps() == valid_fps(f)) {
        set_mode(0);
        return;
    }
    set_fps(f);
    set_mode(1);
}
const char* mode_name() {
    static char buf[48];
    if (fast_forward()) return "fast forward";
    if (unlocked()) return "unlocked";
    if (mode() != 1 && !custom_on()) return "60 fps";
    const int f = fps(), out = output_fps();
    if (out >= f) return fps_name(f);
    snprintf(buf, sizeof buf, "%d fps (%d shown)", f, out);
    return buf;
}

uint32_t effective_swap_interval(uint32_t game) { return game; }
int vsync_granule() { return g_granule.load(std::memory_order_relaxed); }

namespace {
std::atomic<uint64_t> g_executed_steps{0};
uint64_t g_logic_steps = 0;
uint64_t g_passes = 0;
bool g_hold = false;
bool g_logic_pass = false;
bool g_hold_next = false;
bool g_pass_valid = false;

int g_step_n = 1;
int g_frame_n = 1;
int g_phase = 0;
uint64_t g_record_passes = 0;
uint64_t g_present_i = 0;
float g_blend_t = 1.f;

bool g_interp_present = false;
std::atomic<bool> g_pose_reset{false};
int64_t g_acc_ns = 0;
bool g_wake_valid = false;
std::chrono::steady_clock::time_point g_last_wake{};

constexpr uint32_t kRendererBox = 0x10;
constexpr uint32_t kProjection = 0x20;
constexpr uint32_t kViewCount = 0x4C4;
constexpr uint32_t kViewInfo = 0x4C8;
constexpr uint32_t kViewStride = 0x80;
constexpr uint32_t kViewProj = 0x34;
constexpr uint32_t kBgScreen = 0x88;
constexpr uint32_t kModelEnv = 0x4A8;
constexpr uint32_t kEnvObjSet = 0x294;
const uint32_t kSetUniformData = GC(0x024EE8A0);

struct CamBox {
    float v[4] = {0, 0, 0, 0};
    uint64_t step = ~0ull;
    bool valid = false;
};

struct ViewSlot {
    uint32_t mgr;
    uint32_t view;
    uint32_t proj;
};

CamBox g_cam;
float g_cam_cur[4] = {0, 0, 0, 0};
bool g_cam_cur_valid = false;
bool g_publish_tiles = false;
bool g_box_open = false;
bool g_sampling = false;
float g_live_box[4] = {0, 0, 0, 0};
uint32_t g_capture_proj = 0;
uint32_t g_tile_proj[256];
bool g_tile_proj_neg[256];
bool g_tile_neg_next = false;
int g_tile_proj_n = 0;
ViewSlot g_views[8];
int g_view_n = 0;
uint32_t g_view_area = 0;

uint32_t renderer_instance() { return ld32(GD(0x101D2D88)); }

bool read_box(uint32_t r, float* b) {
    if (!r) return false;
    for (int i = 0; i < 4; i++) b[i] = (float)ldf32(r + kRendererBox + 4 * i);
    return true;
}

void write_box(uint32_t r, const float* b) {
    for (int i = 0; i < 4; i++) stf32(r + kRendererBox + 4 * i, b[i]);
}

bool cam_cut(const float* a, const float* b) {
    static const float kCut = (float)env::INTERP_CUT.real_or(800.0f);
    float d = 0;
    for (int i = 0; i < 4; i++) d = std::max(d, std::fabs(b[i] - a[i]));
    return d > kCut;
}

void apply_box(Cpu* c, uint32_t r, const float* b) {
    write_box(r, b);
    c->r[3] = r;
    f_024FA76C(c);
}

uint32_t view_info(uint32_t mgr, uint32_t view) {
    if (mgr < 0x02000000u || mgr > 0x70000000u - (kViewInfo + 4u)) return 0;
    const uint32_t count = ld32(mgr + kViewCount);
    const uint32_t base = ld32(mgr + kViewInfo);
    if (base < 0x02000000u || base >= 0x70000000u || view >= count) return 0;
    const uint32_t vi = base + view * kViewStride;
    if (vi < base || vi >= 0x70000000u || !wwatch::readable(vi, kViewStride)) return 0;
    return vi;
}

void write_view_proj(Cpu* c, uint32_t mgr, uint32_t view, uint32_t proj) {
    const uint32_t vi = view_info(mgr, view);
    if (!vi) return;
    c->r[3] = proj;
    f_02A05A9C(c);
    const uint32_t m = c->r[3];
    if (!m) return;
    for (int i = 0; i < 16; i++) st32(vi + kViewProj + 4 * i, ld32(m + 4 * i));
}

void refresh_model_env(Cpu* c, uint32_t mgr, uint32_t view) {
    const uint32_t vi = view_info(mgr, view);
    if (!vi) return;
    guest_call(c, kSetUniformData, {mgr + kModelEnv, view, vi + 4, vi + kViewProj, ld32(vi + 0x74), mgr + kEnvObjSet, ld32(vi + 0x7C)});
}

void remember_view(uint32_t mgr, uint32_t view, uint32_t proj) {
    for (int i = 0; i < g_view_n; i++)
        if (g_views[i].mgr == mgr && g_views[i].view == view) {
            g_views[i].proj = proj;
            return;
        }
    if (g_view_n < 8) {
        g_views[g_view_n].mgr = mgr;
        g_views[g_view_n].view = view;
        g_views[g_view_n].proj = proj;
        g_view_n++;
    }
}

void clear_model_poses();

constexpr float kGrassDebugWaveScale = 1.f;
constexpr float kGrassDebugLiftY = 0.f;

struct GrassSpan {
    uint32_t wave_addr = 0;
    int wave_n = 0;
    std::vector<float> wave_prev;
    std::vector<float> wave_cur;
    uint32_t pos_addr = 0;
    int pos_n = 0;
    std::vector<float> pos_prev;
    std::vector<float> pos_cur;
    uint32_t off_addr = 0;
    int off_n = 0;
    std::vector<float> off_prev;
    std::vector<float> off_cur;
    uint32_t sway_addr = 0;
    int sway_n = 0;
    std::vector<float> sway_prev;
    std::vector<float> sway_cur;
};

constexpr uint32_t kGrassBase = 0x3F90;
constexpr uint32_t kGrassDeco[] = {0, 0xC78, 0x18F0, 0x2568};
constexpr int kGrassDecoN = 4;
constexpr int kGrassMax = 0x12C;
constexpr int kGrassWaveStride = 2;
constexpr int kGrassPosStride = 3;
constexpr uint32_t kDecoSwayOff = 0x570;

GrassSpan g_grass[kGrassDecoN];
bool g_grass_logged = false;

constexpr uint32_t kGrassVertBytes = 4u * 0x88u;

struct GrassBake {
    uint32_t obj = 0;
    uint32_t base = 0;
    uint32_t pos = 0;
    uint32_t begin = 0;
    uint32_t count = 0;
    float cam = 0.f;
    bool cam_ok = false;
    std::vector<float> xs;
};

GrassBake g_bake;

void clear_grass_wave() {
    for (GrassSpan& s : g_grass) s = {};
    g_bake = {};
}

static void read_float_ring(uint32_t addr, int n, int stride, uint32_t& slot_addr, int& slot_n,
                          std::vector<float>& prev, std::vector<float>& cur) {
    if (n <= 0 || n > kGrassMax || !addr || !wwatch::readable(addr, (uint32_t)n * stride * 4)) {
        slot_addr = 0;
        slot_n = 0;
        prev.clear();
        cur.clear();
        return;
    }
    const int floats = n * stride;
    const bool same = slot_addr == addr && slot_n == n && (int)cur.size() == floats;
    if (!same) {
        slot_addr = addr;
        slot_n = n;
        prev.assign((size_t)floats, 0.f);
        cur.resize((size_t)floats);
        for (int i = 0; i < floats; i++) prev[(size_t)i] = cur[(size_t)i] = (float)ldf32(addr + 4u * i);
        return;
    }
    prev.swap(cur);
    for (int i = 0; i < floats; i++) cur[(size_t)i] = (float)ldf32(addr + 4u * i);
}

static void read_grass_deco(uint32_t deco, GrassSpan& s) {
    s = {};
    if (!wwatch::readable(deco + 0xBC, 1)) return;
    if (wwatch::readable(deco + 0x50, 8)) {
        const int n = (int)ld32(deco + 0x50);
        const uint32_t addr = ld32(deco + 0x54);
        read_float_ring(addr, n, kGrassWaveStride, s.wave_addr, s.wave_n, s.wave_prev, s.wave_cur);
    }
    if (wwatch::readable(deco + 0x40, 8)) {
        const int n = (int)ld32(deco + 0x40);
        const uint32_t addr = ld32(deco + 0x44);
        read_float_ring(addr, n, kGrassPosStride, s.pos_addr, s.pos_n, s.pos_prev, s.pos_cur);
    }
    if (wwatch::readable(deco + 0x48, 8)) {
        const int n = (int)ld32(deco + 0x48);
        const uint32_t addr = ld32(deco + 0x4C);
        read_float_ring(addr, n, kGrassPosStride, s.off_addr, s.off_n, s.off_prev, s.off_cur);
    }
    if (wwatch::readable(deco + kDecoSwayOff, (uint32_t)kGrassMax * 4))
        read_float_ring(deco + kDecoSwayOff, kGrassMax, 1, s.sway_addr, s.sway_n, s.sway_prev, s.sway_cur);
}

void capture_grass_wave() {
    const uint32_t bg = ld32(GD(0x101DAB94));
    if (!bg || !wwatch::readable(bg + kGrassBase + 0x40, 8)) {
        clear_grass_wave();
        return;
    }
    const uint32_t root = bg + kGrassBase;
    const bool fourth = wwatch::readable(root + 0x3E4B, 1) && ld8(root + 0x3E4B) != 0;
    int decos = 0;
    int positions = 0;
    for (int i = 0; i < kGrassDecoN; i++) {
        if (i == 3 && !fourth) {
            g_grass[i] = {};
            continue;
        }
        read_grass_deco(root + kGrassDeco[i], g_grass[i]);
        if (!g_grass[i].pos_n && !g_grass[i].wave_n) {
            g_grass[i] = {};
            continue;
        }
        decos++;
        positions += g_grass[i].pos_n;
    }
    if (!g_grass_logged && decos > 0) {
        g_grass_logged = true;
        LOG("[interp] grass deco blend on (%d layers, %d positions)", decos, positions);
    }
}

enum class GrassWriteKind { Wave, Vec3, Sway };

static void write_float_ring(uint32_t addr, int n, int stride, const std::vector<float>& prev, const std::vector<float>& cur,
                             float t, bool restore, GrassWriteKind kind) {
    if (!addr || n <= 0) return;
    const int floats = n * stride;
    if ((int)cur.size() != floats || (int)prev.size() != floats) return;
    if (!wwatch::readable(addr, (uint32_t)floats * 4)) return;
    const bool debug = !restore && (kGrassDebugWaveScale != 1.f || kGrassDebugLiftY != 0.f);
    for (int i = 0; i < floats; i++) {
        float v = restore ? cur[(size_t)i] : pacing::lerp_f(prev[(size_t)i], cur[(size_t)i], t);
        if (debug) {
            if (kind == GrassWriteKind::Wave)
                v *= kGrassDebugWaveScale;
            else if (kind == GrassWriteKind::Vec3 && i % stride == 1)
                v += kGrassDebugLiftY;
        }
        stf32(addr + 4u * i, v);
    }
}

static void write_grass_span(const GrassSpan& s, float t, bool restore) {
    write_float_ring(s.wave_addr, s.wave_n, kGrassWaveStride, s.wave_prev, s.wave_cur, t, restore, GrassWriteKind::Wave);
    write_float_ring(s.pos_addr, s.pos_n, kGrassPosStride, s.pos_prev, s.pos_cur, t, restore, GrassWriteKind::Vec3);
    write_float_ring(s.off_addr, s.off_n, kGrassPosStride, s.off_prev, s.off_cur, t, restore, GrassWriteKind::Vec3);
    const int sway_n = s.sway_n > 0 && s.pos_n > 0 ? std::min(s.sway_n, s.pos_n) : s.sway_n;
    write_float_ring(s.sway_addr, sway_n, 1, s.sway_prev, s.sway_cur, t, restore, GrassWriteKind::Sway);
}

void blend_grass_wave(float t) {
    for (const GrassSpan& s : g_grass) write_grass_span(s, t, false);
}

void restore_grass_wave() {
    for (const GrassSpan& s : g_grass) write_grass_span(s, 1.f, true);
}

static bool grass_ring_same(const std::vector<float>& prev, const std::vector<float>& cur) {
    return !prev.empty() && prev.size() == cur.size() &&
           std::memcmp(prev.data(), cur.data(), prev.size() * sizeof(float)) == 0;
}

static bool grass_span_same(const GrassSpan& s) {
    if (s.wave_n && !grass_ring_same(s.wave_prev, s.wave_cur)) return false;
    if (s.pos_n && !grass_ring_same(s.pos_prev, s.pos_cur)) return false;
    if (s.off_n && !grass_ring_same(s.off_prev, s.off_cur)) return false;
    if (s.sway_n && !grass_ring_same(s.sway_prev, s.sway_cur)) return false;
    return true;
}

static bool grass_want_hold_patch(float t) {
    if (t >= 1.f) return false;
    for (const GrassSpan& s : g_grass) {
        if (s.wave_n || s.pos_n) return !grass_span_same(s);
    }
    return false;
}

static void grass_shift_x(float dx, bool undo) {
    if (dx == 0.f) return;
    const float step = undo ? -dx : dx;
    for (const GrassSpan& s : g_grass) {
        if (s.pos_addr && s.pos_n > 0 && wwatch::readable(s.pos_addr, (uint32_t)s.pos_n * kGrassPosStride * 4)) {
            for (int i = 0; i < s.pos_n; i++) {
                const uint32_t a = s.pos_addr + 4u * (uint32_t)(i * kGrassPosStride);
                stf32(a, (float)ldf32(a) + step);
            }
        }
        if (s.off_addr && s.off_n > 0 && wwatch::readable(s.off_addr, (uint32_t)s.off_n * kGrassPosStride * 4)) {
            for (int i = 0; i < s.off_n; i++) {
                const uint32_t a = s.off_addr + 4u * (uint32_t)(i * kGrassPosStride);
                stf32(a, (float)ldf32(a) + step);
            }
        }
    }
}

static float grass_cam_dx() {
    if (!g_box_open) return 0.f;
    const uint32_t r = renderer_instance();
    if (!r) return 0.f;
    float open[4];
    if (!read_box(r, open)) return 0.f;
    return open[0] - g_live_box[0];
}

static uint32_t grass_draw_obj() {
    const uint32_t slot = GD(0x101D2DB0);
    if (!wwatch::readable(slot, 4)) return 0;
    const uint32_t parent = ld32(slot);
    if (!parent || !wwatch::readable(parent + 0x24, 4)) return 0;
    const uint32_t obj = ld32(parent + 0x24);
    if (!obj || !wwatch::readable(obj + 0x3B4, 8)) return 0;
    return obj;
}

static void grass_collect_xs(std::vector<float>& xs) {
    xs.clear();
    for (const GrassSpan& s : g_grass) {
        if (!s.pos_addr || s.pos_n <= 0) continue;
        if (!wwatch::readable(s.pos_addr, (uint32_t)s.pos_n * kGrassPosStride * 4)) continue;
        for (int i = 0; i < s.pos_n; i++) {
            float x = (float)ldf32(s.pos_addr + 4u * (uint32_t)(i * kGrassPosStride));
            if (s.off_addr && i < s.off_n && wwatch::readable(s.off_addr + 4u * (uint32_t)(i * kGrassPosStride), 4))
                x += (float)ldf32(s.off_addr + 4u * (uint32_t)(i * kGrassPosStride));
            xs.push_back(x);
        }
    }
    std::sort(xs.begin(), xs.end());
    xs.erase(std::unique(xs.begin(), xs.end()), xs.end());
}

static bool near_grass_x(float v, const std::vector<float>& xs) {
    if (xs.empty() || v < 50.f) return false;
    const auto it = std::lower_bound(xs.begin(), xs.end(), v);
    if (it != xs.end() && std::fabs(*it - v) <= 48.f) return true;
    if (it != xs.begin() && std::fabs(*std::prev(it) - v) <= 48.f) return true;
    return false;
}

static void shift_baked_words(uint32_t addr, uint32_t bytes, float dx, const std::vector<float>& xs) {
    if (!addr || bytes < 4 || !wwatch::readable(addr, bytes)) return;
    for (uint32_t o = 0; o + 4 <= bytes; o += 4) {
        const float v = (float)ldf32(addr + o);
        if (!near_grass_x(v, xs)) continue;
        stf32(addr + o, v + dx);
    }
}

static void remember_grass_bake(uint32_t n0) {
    if (g_hold) return;
    const uint32_t obj = grass_draw_obj();
    static bool saw = false;
    if (!saw) {
        saw = true;
        LOG("[interp] grass bake obj=%08x n0=%u", obj, n0);
    }
    if (!obj || !wwatch::readable(obj + 0x394, 0x24)) return;
    const uint32_t n1 = ld32(obj + 0x398);
    const uint32_t base_raw = ld32(obj + 0x3B4);
    const uint32_t span = ld32(obj + 0x394);
    const uint32_t slot = ld32(obj + 0x3A8);
    const uint32_t pos = ld32(obj + 0x30);
    static bool fields = false;
    if (!fields) {
        fields = true;
        LOG("[interp] grass bake fields n0=%u n1=%u span=%u slot=%u base=%08x pos=%08x", n0, n1, span, slot, base_raw, pos);
    }
    if (!base_raw || n1 < n0) return;
    const uint64_t off = (uint64_t)span * slot * 0x88u;
    if (off > 0x10000000u) return;
    const uint32_t base = base_raw + (uint32_t)off;
    static uint64_t step = ~0ull;
    if (g_bake.obj != obj || g_bake.base != base || step != g_logic_steps) {
        g_bake = {};
        g_bake.obj = obj;
        g_bake.base = base;
        g_bake.pos = pos;
        g_bake.begin = n0;
        step = g_logic_steps;
    }
    if (n0 < g_bake.begin) g_bake.begin = n0;
    g_bake.count = n1 - g_bake.begin;
    if (g_bake.count > 8192) g_bake.count = 0;
    const uint32_t r = renderer_instance();
    float box[4];
    if (r && read_box(r, box)) {
        g_bake.cam = box[0];
        g_bake.cam_ok = true;
    }
    grass_collect_xs(g_bake.xs);
    static bool logged = false;
    if (!logged) {
        logged = true;
        const uint32_t a = base + g_bake.begin * kGrassVertBytes;
        const bool have = g_bake.count > 0 && wwatch::readable(a, 12);
        LOG("[interp] grass bake count=%u n0=%u n1=%u x=%.1f y=%.1f z=%.1f cam=%.1f", g_bake.count, n0, n1,
            have ? (double)ldf32(a) : 0.0, have ? (double)ldf32(a + 4) : 0.0, have ? (double)ldf32(a + 8) : 0.0, g_bake.cam);
    }
}

static float baked_grass_shift() {
    if (!g_bake.cam_ok || g_bake.count == 0 || g_bake.xs.empty()) return 0.f;
    const uint32_t r = renderer_instance();
    float box[4];
    if (!r || !read_box(r, box)) return 0.f;
    return g_bake.cam - box[0];
}

static void apply_baked_grass_shift(float dx) {
    if (dx == 0.f || g_bake.count == 0) return;
    const uint32_t bytes = g_bake.count * kGrassVertBytes;
    const uint32_t addr = g_bake.base + g_bake.begin * kGrassVertBytes;
    shift_baked_words(addr, bytes, dx, g_bake.xs);
    if (g_bake.pos && wwatch::readable(g_bake.pos + g_bake.begin * 12, g_bake.count * 12))
        shift_baked_words(g_bake.pos + g_bake.begin * 12, g_bake.count * 12, dx, g_bake.xs);
}

static float grass_screen_px(float x, const float* box) {
    const float w = box[2] - box[0];
    if (std::fabs(w) < 0.01f) return 0.f;
    return (x - box[0]) / w * 1280.f;
}

static float grass_player_x() {
    const uint32_t mgr = ld32(GD(0x101E6994));
    if (!mgr || !wwatch::readable(mgr + 0x20, 4)) return 0.f;
    const uint32_t actor = ld32(mgr + 0x20);
    if (!actor || !wwatch::readable(actor + 0x6C, 4)) return 0.f;
    return (float)ldf32(actor + 0x6C);
}

static void trace_grass_screen() {
    static int left = -1;
    static bool have_origin = false;
    static float origin_x = 0.f;
    static int lock_l = -1;
    static int lock_i = -1;
    static float prev_scr = 0.f;
    static float prev_fix = 0.f;
    static float prev_ply = 0.f;
    static float prev_cam = 0.f;
    static bool prev_ok = false;
    if (left < 0) left = env::INTERP_GRASS_TRACE.integer_or(0);
    if (left <= 0) return;
    const uint32_t r = renderer_instance();
    float now[4];
    if (!r || !read_box(r, now)) return;
    if (!have_origin) {
        origin_x = now[0];
        have_origin = true;
        return;
    }
    if (std::fabs(now[0] - origin_x) < 4.f) return;
    const float t = pacing::present_sample_t(!g_hold, pass_t());
    float lerp_box[4];
    if (g_cam.valid && g_cam_cur_valid)
        for (int i = 0; i < 4; i++) lerp_box[i] = pacing::lerp_f(g_cam.v[i], g_cam_cur[i], t);
    else
        for (int i = 0; i < 4; i++) lerp_box[i] = now[i];
    const float d_live = std::fabs(now[0] - g_live_box[0]);
    const float d_lerp = std::fabs(now[0] - lerp_box[0]);
    const char* match = d_lerp < 0.05f ? "lerp" : d_live < 0.05f ? "live" : "other";
    const bool patch = g_hold && grass_want_hold_patch(t);
    const float dx = patch ? grass_cam_dx() : 0.f;

    auto on_screen = [&](int layer, int index, float& x, float& off, float& y) -> bool {
        const GrassSpan& s = g_grass[layer];
        if (!s.pos_addr || index < 0 || index >= s.pos_n) return false;
        if (!wwatch::readable(s.pos_addr + 4u * (uint32_t)(index * kGrassPosStride), 12)) return false;
        x = (float)ldf32(s.pos_addr + 4u * (uint32_t)(index * kGrassPosStride));
        y = (float)ldf32(s.pos_addr + 4u * (uint32_t)(index * kGrassPosStride + 1));
        off = 0.f;
        if (s.off_addr && index < s.off_n && wwatch::readable(s.off_addr + 4u * (uint32_t)(index * kGrassPosStride), 4))
            off = (float)ldf32(s.off_addr + 4u * (uint32_t)(index * kGrassPosStride));
        const float scr = grass_screen_px(x + off, now);
        return scr >= -80.f && scr <= 1360.f;
    };

    int layer = lock_l;
    int index = lock_i;
    float x = 0.f, off = 0.f, y = 0.f;
    if (layer < 0 || !on_screen(layer, index, x, off, y)) {
        float best = 1.0e9f;
        layer = -1;
        index = -1;
        for (int L = 0; L < kGrassDecoN; L++) {
            const GrassSpan& s = g_grass[L];
            for (int i = 0; i < s.pos_n; i++) {
                float px, po, py;
                if (!on_screen(L, i, px, po, py)) continue;
                const float d = std::fabs(grass_screen_px(px + po, now) - 640.f);
                if (d < best) {
                    best = d;
                    layer = L;
                    index = i;
                    x = px;
                    off = po;
                    y = py;
                }
            }
        }
        lock_l = layer;
        lock_i = index;
        prev_ok = false;
    }
    if (layer < 0) {
        for (int L = 0; L < kGrassDecoN && layer < 0; L++) {
            if (g_grass[L].pos_n <= 0 || !g_grass[L].pos_addr) continue;
            layer = L;
            index = 0;
            on_screen(L, 0, x, off, y);
        }
        lock_l = layer;
        lock_i = index;
        prev_ok = false;
    }
    if (layer < 0) return;
    const GrassSpan& s = g_grass[layer];
    float wave0 = 0.f, wave1 = 0.f, sway = 0.f;
    if (s.wave_addr && index < s.wave_n && wwatch::readable(s.wave_addr + 8u * (uint32_t)index, 8)) {
        wave0 = (float)ldf32(s.wave_addr + 8u * (uint32_t)index);
        wave1 = (float)ldf32(s.wave_addr + 8u * (uint32_t)index + 4);
    }
    if (s.sway_addr && index < s.sway_n && wwatch::readable(s.sway_addr + 4u * (uint32_t)index, 4))
        sway = (float)ldf32(s.sway_addr + 4u * (uint32_t)index);
    float fix_x = x + off;
    if (patch && (int)s.pos_prev.size() == s.pos_n * kGrassPosStride && index < s.pos_n) {
        fix_x = pacing::lerp_f(s.pos_prev[(size_t)(index * kGrassPosStride)], s.pos_cur[(size_t)(index * kGrassPosStride)], t);
        if ((int)s.off_prev.size() == s.off_n * kGrassPosStride && index < s.off_n)
            fix_x += pacing::lerp_f(s.off_prev[(size_t)(index * kGrassPosStride)], s.off_cur[(size_t)(index * kGrassPosStride)], t);
        else
            fix_x += off;
        fix_x += dx;
    }
    const float scr = grass_screen_px(x + off, now);
    const float fix = grass_screen_px(fix_x, now);
    const float ply = grass_screen_px(grass_player_x(), now);
    const float cam_px = grass_screen_px(0.f, now);
    float slip = 0.f, fslip = 0.f, pslip = 0.f;
    if (prev_ok) {
        const float dcam = cam_px - prev_cam;
        slip = (scr - prev_scr) - dcam;
        fslip = (fix - prev_fix) - dcam;
        pslip = (ply - prev_ply) - dcam;
    }
    prev_scr = scr;
    prev_fix = fix;
    prev_ply = ply;
    prev_cam = cam_px;
    prev_ok = true;
    const uint32_t bg = ld32(GD(0x101DAB94));
    const float bg14 = bg && wwatch::readable(bg + 0x14, 4) ? (float)ldf32(bg + 0x14) : 0.f;
    const float bg18 = bg && wwatch::readable(bg + 0x18, 4) ? (float)ldf32(bg + 0x18) : 0.f;
    const unsigned kind = bg && wwatch::readable(bg + kGrassBase + kGrassDeco[layer] + 0xBC, 1)
                              ? ld8(bg + kGrassBase + kGrassDeco[layer] + 0xBC)
                              : 0;
    left--;
    LOG("[interp] grass px hold=%d t=%.2f step=%llu cam=%.1f live=%.1f lerp=%.1f match=%s patch=%d dx=%.2f L%d i=%d k=%u pos=%.1f off=%.1f y=%.1f wave=%.3f/%.3f sway=%.3f scr=%.1f slip=%.2f fix=%.1f fslip=%.2f ply=%.1f pslip=%.2f bg=%.1f/%.1f",
        g_hold ? 1 : 0, t, (unsigned long long)g_logic_steps, now[0], g_live_box[0], lerp_box[0], match, patch ? 1 : 0, dx, layer, index,
        kind, x, off, y, wave0, wave1, sway, scr, slip, fix, fslip, ply, pslip, bg14, bg18);
}

void remember_area() {
    const uint32_t area = ld32(GD(0x101D1598));
    if (area == g_view_area) return;
    g_view_n = 0;
    g_cam_cur_valid = false;
    g_cam.valid = false;
    g_tile_proj_n = 0;
    g_view_area = area;
    clear_model_poses();
    clear_grass_wave();
}

}

int in_between() {
    if (!interp_on()) return 0;
    return std::max(0, output_fps() / 60 - 1);
}
int frames_per_step() {
    if (!interp_on()) return 1;
    return std::max(1, output_fps() / 60);
}

float pass_t() {
    if (!enabled()) return 1.0f;
    return g_blend_t;
}

void cam_trace(const char* what, const float* prev, const float* cur, const float* drawn) {
    static int left = env::INTERP_CAM_TRACE.integer_or(0);
    if (left <= 0 || !enabled()) return;
    left--;
    const uint32_t bg = ld32(GD(0x101E7C54));
    LOG("[interp] cam %-6s step %llu hold %d phase %d/%d prev %.1f %.1f %.1f %.1f cur %.1f %.1f %.1f %.1f drawn %.1f %.1f %.1f %.1f bg %.1f %.1f %.1f %.1f",
        what, (unsigned long long)g_logic_steps, g_hold, g_phase, g_step_n, prev[0], prev[1], prev[2], prev[3], cur[0], cur[1], cur[2],
        cur[3], drawn[0], drawn[1], drawn[2], drawn[3], bg ? (double)ldf32(bg + kBgScreen) : 0.0,
        bg ? (double)ldf32(bg + kBgScreen + 4) : 0.0, bg ? (double)ldf32(bg + kBgScreen + 8) : 0.0,
        bg ? (double)ldf32(bg + kBgScreen + 12) : 0.0);
}

void shift_camera(const float* cur) {
    apply_pose_reset();
    const bool cut = g_cam_cur_valid && cam_cut(g_cam_cur, cur);
    switch (pacing::camera_shift(g_cam.valid, g_cam_cur_valid, g_cam.step, g_logic_steps, cut)) {
    case pacing::CameraShift::Same:
        for (int i = 0; i < 4; i++) g_cam_cur[i] = cur[i];
        g_cam_cur_valid = true;
        return;
    case pacing::CameraShift::Adjacent:
        for (int i = 0; i < 4; i++) g_cam.v[i] = g_cam_cur[i];
        break;
    case pacing::CameraShift::Snap:
        for (int i = 0; i < 4; i++) g_cam.v[i] = cur[i];
        break;
    }
    for (int i = 0; i < 4; i++) g_cam_cur[i] = cur[i];
    g_cam_cur_valid = true;
    g_cam.valid = true;
    g_cam.step = g_logic_steps;
}

void publish_tile_matrix(Cpu* c, uint32_t r) {
    if (!g_publish_tiles || !r || !g_tile_proj_n) return;
    const uint32_t r3 = c->r[3];
    c->r[3] = r + kProjection;
    f_02A05A9C(c);
    const uint32_t m = c->r[3];
    c->r[3] = r3;
    if (!m || !wwatch::readable(m, 64)) return;
    for (int i = 0; i < g_tile_proj_n; i++) {
        const uint32_t a = g_tile_proj[i];
        if (!wwatch::readable(a, 64)) continue;
        for (int k = 0; k < 16; k++) {
            uint32_t w = ld32(m + 4 * k);
            if (k == 5 && g_tile_proj_neg[i]) w ^= 0x80000000u;
            std::memcpy(mem::ptr(a + 4 * k), &w, 4);
        }
    }
    static bool logged = false;
    if (!logged) {
        logged = true;
        LOG("[interp] course tile camera slots %d", g_tile_proj_n);
    }
}

void present_camera(Cpu* c, uint32_t r, const float* live, uint32_t mgr, uint32_t view, uint32_t proj, bool restore) {
    if (g_sampling) return;
    g_sampling = true;
    if (!g_interp_present || !g_cam.valid || !g_cam_cur_valid || cam_cut(g_cam.v, g_cam_cur)) {
        publish_tile_matrix(c, r);
        g_box_open = false;
        g_sampling = false;
        cam_trace("snap", g_cam.v, live, live);
        return;
    }
    const float t = pacing::present_sample_t(!g_hold, pass_t());
    float box[4];
    for (int i = 0; i < 4; i++) box[i] = pacing::lerp_f(g_cam.v[i], g_cam_cur[i], t);
    apply_box(c, r, box);
    publish_tile_matrix(c, r);
    if (proj) {
        write_view_proj(c, mgr, view, proj);
        refresh_model_env(c, mgr, view);
    } else {
        for (int i = 0; i < g_view_n; i++) {
            write_view_proj(c, g_views[i].mgr, g_views[i].view, g_views[i].proj);
            refresh_model_env(c, g_views[i].mgr, g_views[i].view);
        }
    }
    if (restore) {
        apply_box(c, r, live);
        g_box_open = false;
    } else {
        g_box_open = true;
    }
    g_sampling = false;
    cam_trace(g_hold ? "hold" : "older", g_cam.v, g_cam_cur, box);
}

void apply_hold_camera(Cpu* c, bool publish_tiles = false, bool restore = true) {
    remember_area();
    if (!course_play_active() || !g_cam_cur_valid) return;
    const uint32_t r = renderer_instance();
    if (!r) return;
    float cur[4];
    if (!read_box(r, cur)) {
        for (int i = 0; i < 4; i++) cur[i] = g_cam_cur[i];
    }
    if (g_box_open) {
        for (int i = 0; i < 4; i++) cur[i] = g_live_box[i];
    } else {
        for (int i = 0; i < 4; i++) g_live_box[i] = cur[i];
    }
    shift_camera(cur);
    g_publish_tiles = publish_tiles;
    present_camera(c, r, cur, 0, 0, 0, restore);
    g_publish_tiles = false;
}

void finish_open_camera(Cpu* c) {
    const uint32_t r = renderer_instance();
    if (r) {
        g_publish_tiles = true;
        publish_tile_matrix(c, r);
        g_publish_tiles = false;
        if (g_box_open) apply_box(c, r, g_live_box);
    }
    g_box_open = false;
}

void pose_reset() { g_pose_reset.store(true, std::memory_order_relaxed); }

void apply_pose_reset() {
    if (!g_pose_reset.exchange(false, std::memory_order_relaxed)) return;
    g_cam = CamBox{};
    g_cam_cur_valid = false;
    g_tile_proj_n = 0;
    g_view_n = 0;
    g_box_open = false;
    g_sampling = false;
    clear_model_poses();
    clear_grass_wave();
}

}

namespace interp {
bool same_proj_matrix(uint32_t values) {
    if (!g_capture_proj || !values) return false;
    const uint32_t m = g_capture_proj + 0x44;
    if (!wwatch::readable(values, 64) || !wwatch::readable(m, 64)) return false;
    static const int kZero[] = {1, 2, 4, 6, 8, 9, 12, 13, 14};
    for (int i : kZero)
        if (ld32(values + 4 * i) != 0) return false;
    if (ld32(values + 60) != 0x3f800000u) return false;
    static const int kSame[] = {0, 3, 7, 10, 11};
    for (int i : kSame)
        if (ld32(values + 4 * i) != ld32(m + 4 * i)) return false;
    const uint32_t ys = ld32(values + 20);
    const uint32_t yl = ld32(m + 20);
    if (ys != yl && ys != (yl ^ 0x80000000u)) return false;
    g_tile_neg_next = ys != yl;
    return true;
}
void keep_tile_proj(uint32_t addr) {
    if (!addr) return;
    for (int i = 0; i < g_tile_proj_n; i++)
        if (g_tile_proj[i] == addr) return;
    if (g_tile_proj_n < 256) {
        g_tile_proj_neg[g_tile_proj_n] = g_tile_neg_next;
        g_tile_proj[g_tile_proj_n++] = addr;
    }
}
namespace {
void record_model(Cpu* c);
void reblend_held(Cpu* c);
}
}

extern "C" void hook_024F30DC(Cpu* c) {
    const uint32_t model = c->r[3];
    const uint32_t view = c->r[4];
    const uint32_t view_mtx = c->r[5];
    f_024F30DC_orig(c);
    c->r[3] = model;
    c->r[4] = view;
    c->r[5] = view_mtx;
    interp::record_model(c);
    interp::reblend_held(c);
}


namespace interp {
namespace {
const uint32_t kCalcView = GC(0x02AAAD18);
const uint32_t kUploadShapes = GC(0x02AA56F4);
constexpr uint32_t kMaxBones = 256;
constexpr uint32_t kMaxShapes = 512;

struct ModelPose {
    uint32_t model = 0;
    uint32_t mtx = 0;
    uint32_t n = 0;
    uint64_t step = 0;
    bool ubo_blended = false;
    picture::Submitted submitted;
    std::vector<uint8_t> views;
    std::vector<uint32_t> view_mtx;
    std::vector<uint32_t> prev;
    std::vector<uint8_t> prev_stale;
    std::vector<uint32_t> cur;
    std::vector<uint8_t> cur_stale;
};
std::unordered_map<uint32_t, ModelPose> g_models;
bool g_in_model_present = false;

void clear_model_poses() { g_models.clear(); }

void note_view(ModelPose& p, uint32_t view, uint32_t mtx) {
    if (view > 31) return;
    for (size_t i = 0; i < p.views.size(); i++)
        if (p.views[i] == view) {
            if (mtx) p.view_mtx[i] = mtx;
            return;
        }
    if (p.views.size() >= 4) return;
    p.views.push_back(static_cast<uint8_t>(view));
    p.view_mtx.push_back(mtx);
}

bool bone_hidden(uint32_t model, uint32_t bone, uint32_t n, const std::vector<uint8_t>& has_shape) {
    if (bone >= n || !has_shape[bone]) return false;
    const uint32_t bits = ld32(model + 0x2C);
    const uint32_t words = (n + 31) / 32;
    if (!bits || !wwatch::readable(bits, words * 4)) return false;
    const uint32_t word = ld32(bits + (bone >> 5) * 4);
    return ((word >> (bone & 31)) & 1u) == 0;
}

void shape_bones(uint32_t model, uint32_t n, std::vector<uint8_t>& has_shape) {
    const uint32_t shapes = ld32(model + 0x44);
    const uint32_t shape_n = ld16(model + 0x3C);
    if (!shapes || shape_n == 0 || shape_n > kMaxShapes) return;
    if (!wwatch::readable(shapes, shape_n * 0x28)) return;
    for (uint32_t i = 0; i < shape_n; i++) {
        const uint32_t shape = ld32(shapes + i * 0x28);
        if (!shape || !wwatch::readable(shape, 0x12)) continue;
        const uint32_t bone = ld16(shape + 0x10);
        if (bone < n) has_shape[bone] = 1;
    }
}

struct PoseTrace {
    bool on = false;
    float travel = -1.f;
    float elem_delta = -1.f;
    uint32_t bones = 0;
    uint32_t bone = 0;
    uint32_t elem = 0;
    float prev[3] = {};
    float cur[3] = {};
    float drawn[3] = {};
    float e_prev = 0.f;
    float e_cur = 0.f;
    float e_drawn = 0.f;
};

void bone_xyz(const uint32_t* m, float* o) {
    o[0] = joints::word_f(m[3]);
    o[1] = joints::word_f(m[7]);
    o[2] = joints::word_f(m[11]);
}

void consider_pose(PoseTrace& tr, const ModelPose& p, const uint32_t* drawn) {
    if (env::INTERP_MODEL_TRACE.integer_or(0) <= 0 || p.n == 0) return;
    float best_elem = -1.f;
    float best_travel = 0.f;
    uint32_t bone = 0;
    uint32_t elem = 0;
    float pv[3] = {}, cu[3] = {}, dr[3] = {};
    float ep = 0.f, ec = 0.f, ed = 0.f;
    for (uint32_t j = 0; j < p.n; j++) {
        float a[3], b[3], d[3];
        bone_xyz(p.prev.data() + joints::kWords * j, a);
        bone_xyz(p.cur.data() + joints::kWords * j, b);
        bone_xyz(drawn + joints::kWords * j, d);
        const float dx = b[0] - a[0], dy = b[1] - a[1], dz = b[2] - a[2];
        const float dist = dx * dx + dy * dy + dz * dz;
        const uint32_t* prev_w = p.prev.data() + joints::kWords * j;
        const uint32_t* cur_w = p.cur.data() + joints::kWords * j;
        const uint32_t* drawn_w = drawn + joints::kWords * j;
        for (uint32_t k = 0; k < joints::kWords; k++) {
            const float pa = joints::word_f(prev_w[k]);
            const float pb = joints::word_f(cur_w[k]);
            const float delta = std::fabs(pb - pa);
            if (delta <= best_elem) continue;
            best_elem = delta;
            best_travel = dist;
            bone = j;
            elem = k;
            ep = pa;
            ec = pb;
            ed = joints::word_f(drawn_w[k]);
            for (int axis = 0; axis < 3; axis++) {
                pv[axis] = a[axis];
                cu[axis] = b[axis];
                dr[axis] = d[axis];
            }
        }
    }
    if (best_elem <= tr.elem_delta) return;
    tr.on = true;
    tr.elem_delta = best_elem;
    tr.travel = best_travel;
    tr.bones = p.n;
    tr.bone = bone;
    tr.elem = elem;
    tr.e_prev = ep;
    tr.e_cur = ec;
    tr.e_drawn = ed;
    for (int k = 0; k < 3; k++) {
        tr.prev[k] = pv[k];
        tr.cur[k] = cu[k];
        tr.drawn[k] = dr[k];
    }
}

void log_pose(const PoseTrace& tr, float t) {
    static int left = env::INTERP_MODEL_TRACE.integer_or(0);
    if (!tr.on || left <= 0 || tr.elem_delta < 0.02f) return;
    left--;
    float camP[4] = {}, camC[4] = {}, camD[4] = {};
    if (g_cam.valid && g_cam_cur_valid) {
        for (int i = 0; i < 4; i++) {
            camP[i] = g_cam.v[i];
            camC[i] = g_cam_cur[i];
            camD[i] = pacing::lerp_f(camP[i], camC[i], t);
        }
    }
    LOG("[interp] pose hold=%d t=%.3f step=%llu bones=%u bone=%u elem=%u ePrev=%.4f eCur=%.4f eDrawn=%.4f travel=%.3f prev=%.3f,%.3f,%.3f cur=%.3f,%.3f,%.3f drawn=%.3f,%.3f,%.3f camPrev=%.2f,%.2f,%.2f,%.2f camCur=%.2f,%.2f,%.2f,%.2f camDrawn=%.2f,%.2f,%.2f,%.2f",
        g_hold ? 1 : 0, t, (unsigned long long)g_logic_steps, tr.bones, tr.bone, tr.elem, tr.e_prev, tr.e_cur, tr.e_drawn,
        std::sqrt(std::max(0.f, tr.travel)), tr.prev[0], tr.prev[1], tr.prev[2], tr.cur[0], tr.cur[1], tr.cur[2], tr.drawn[0], tr.drawn[1],
        tr.drawn[2], camP[0], camP[1], camP[2], camP[3], camC[0], camC[1], camC[2], camC[3], camD[0], camD[1], camD[2], camD[3]);
}

void upload_pose(Cpu* c, ModelPose& p) {
    const uint32_t obj = ld32(p.model + 0x40);
    const uint32_t count = p.views.empty() ? 1u : static_cast<uint32_t>(p.views.size());
    for (uint32_t i = 0; i < count; i++) {
        const uint32_t view = p.views.empty() ? 0u : p.views[i];
        guest_call(c, kCalcView, {obj, view});
        guest_call(c, kUploadShapes, {p.model + 0x28, view});
        const uint32_t view_mtx = i < p.view_mtx.size() ? p.view_mtx[i] : 0;
        if (view_mtx && wwatch::readable(view_mtx, 0x30) && wwatch::readable(p.model + 0x35, 1) && mem::ptr(p.model + 0x35)[0]) {
            c->r[3] = p.model;
            c->r[4] = view;
            c->r[5] = view_mtx;
            f_024F30DC_orig(c);
            static bool logged = false;
            if (!logged) {
                logged = true;
                LOG("[interp] view-dependent pose on");
            }
        }
    }
    if (!obj || !wwatch::readable(obj, 0x20)) return;
    const uint32_t block = ld32(obj + 0x18);
    const uint32_t size = ld32(obj + 0x1C);
    if (!block || size == 0 || !wwatch::readable(block, size)) return;
    picture::snapshot(p.submitted, mem::ptr(block), size);
}


uint32_t player_model(int player, uint32_t anim_off);
bool player_head(uint32_t model);

void record_model(Cpu* c) {
    if (g_in_model_present || !enabled() || !blend_enabled() || g_hold || !g_logic_pass || !course_play_active()) return;
    const uint32_t model = c->r[3];
    const uint32_t view = c->r[4];
    const uint32_t view_mtx = c->r[5];
    if (!wwatch::readable(model, 0x48)) return;
    const uint32_t obj = ld32(model + 0x40);
    if (!obj || !wwatch::readable(obj, 0x20)) return;
    const uint32_t res = ld32(obj);
    const uint32_t mtx = ld32(obj + 0x10);
    const uint32_t ubo_size = ld32(obj + 0x1C);
    if (!res || !mtx || !wwatch::readable(res, 0xC)) return;
    const uint32_t n = ld16(res + 8);
    if (n == 0 || n > kMaxBones || ubo_size > 0x40000) return;
    if (ubo_size == 0 && !player_head(model)) return;
    if (!wwatch::readable(mtx, n * 0x30)) return;

    const uint32_t* live = reinterpret_cast<const uint32_t*>(mem::ptr(mtx));
    ModelPose& p = g_models[obj];
    if (p.step == g_logic_steps && p.mtx == mtx && p.n == n && p.cur.size() == n * joints::kWords && p.cur_stale.size() == n) {
        p.model = model;
        note_view(p, view, view_mtx);
        const uint32_t* before = p.prev.size() == n * joints::kWords ? p.prev.data() : nullptr;
        std::vector<uint8_t> has_shape(n, 0);
        shape_bones(model, n, has_shape);
        for (uint32_t j = 0; j < n; j++) {
            const uint32_t* prior = before ? before + joints::kWords * j : nullptr;
            p.cur_stale[j] = joints::stale(bone_hidden(model, j, n, has_shape), live + joints::kWords * j, prior) ? 1 : 0;
        }
        std::memcpy(p.cur.data(), live, n * 0x30);
        return;
    }
    const bool adjacent = p.mtx == mtx && p.n == n && p.step + 1 == g_logic_steps && p.cur.size() == n * joints::kWords;
    std::vector<uint8_t> has_shape(n, 0);
    shape_bones(model, n, has_shape);
    std::vector<uint8_t> stale(n, 0);
    for (uint32_t j = 0; j < n; j++) {
        const uint32_t* before = adjacent ? p.cur.data() + joints::kWords * j : nullptr;
        stale[j] = joints::stale(bone_hidden(model, j, n, has_shape), live + joints::kWords * j, before) ? 1 : 0;
    }
    if (adjacent) {
        p.prev = std::move(p.cur);
        p.prev_stale = std::move(p.cur_stale);
    } else {
        p.prev.clear();
        p.prev_stale.clear();
    }
    p.cur.assign(live, live + n * joints::kWords);
    p.cur_stale = std::move(stale);
    p.model = model;
    p.mtx = mtx;
    p.n = n;
    p.step = g_logic_steps;
    p.ubo_blended = false;
    p.views.clear();
    p.view_mtx.clear();
    note_view(p, view, view_mtx);
}

void reblend_held(Cpu* c) {
    if (!g_hold || g_in_model_present || !blend_enabled() || !course_play_active()) return;
    const uint32_t model = c->r[3];
    if (!wwatch::readable(model, 0x48)) return;
    const uint32_t obj = ld32(model + 0x40);
    const auto it = g_models.find(obj);
    if (it == g_models.end()) return;
    ModelPose& p = it->second;
    if (p.model != model || p.step != g_logic_steps || p.n == 0) return;
    if (p.prev.size() != p.n * joints::kWords || p.cur.size() != p.n * joints::kWords) return;
    if (!wwatch::readable(p.mtx, p.n * 0x30)) return;
    const float t = pacing::present_sample_t(false, pass_t());
    if (t >= 1.f || std::memcmp(p.prev.data(), p.cur.data(), p.n * 0x30) == 0) return;
    const uint32_t* live = reinterpret_cast<const uint32_t*>(mem::ptr(p.mtx));
    std::vector<uint32_t> saved(live, live + p.n * joints::kWords);
    std::vector<uint32_t> mid(saved.size());
    const auto blended = joints::blend(p.prev.data(), p.cur.data(),
                                        p.prev_stale.size() == p.n ? p.prev_stale.data() : nullptr, p.n, joints::kCutDist, t,
                                        mid.data(), joints::blend_mtx);
    if (blended.cut) return;
    g_in_model_present = true;
    std::memcpy(mem::ptr(p.mtx), mid.data(), p.n * 0x30);
    upload_pose(c, p);
    std::memcpy(mem::ptr(p.mtx), saved.data(), p.n * 0x30);
    p.ubo_blended = true;
    g_in_model_present = false;
}

uint32_t player_model(int player, uint32_t anim_off) {
    if (player < 0 || player > 3) return 0;
    const uint32_t mgr = ld32(GD(0x101E6994));
    const uint32_t slot = 0x20u + (uint32_t)player * 4u;
    if (!mgr || !wwatch::readable(mgr + slot, 4)) return 0;
    const uint32_t actor = ld32(mgr + slot);
    if (!actor || !wwatch::readable(actor + 0x275C, 4)) return 0;
    const uint32_t pm = ld32(actor + 0x275C);
    if (!pm || !wwatch::readable(pm + anim_off, 4)) return 0;
    const uint32_t anim = ld32(pm + anim_off);
    if (!anim || !wwatch::readable(anim, 4)) return 0;
    return ld32(anim);
}

bool player_head(uint32_t model) {
    if (!model) return false;
    for (int player = 0; player < 4; player++)
        if (model == player_model(player, 0x11Cu)) return true;
    return false;
}

void log_player_part(const char* tag, uint32_t model) {
    static int left = 8;
    if (env::INTERP_MODEL_TRACE.integer_or(0) <= 0 || left <= 0 || !model) return;
    const ModelPose* found = nullptr;
    for (const auto& it : g_models)
        if (it.second.model == model) found = &it.second;
    if (!found || found->n == 0 || found->prev.size() != found->n * joints::kWords) {
        if (found) return;
        static int miss = 2;
        if (miss <= 0) return;
        miss--;
        const uint32_t obj = wwatch::readable(model + 0x40, 4) ? ld32(model + 0x40) : 0;
        const uint32_t res = obj && wwatch::readable(obj, 4) ? ld32(obj) : 0;
        const uint32_t n = res && wwatch::readable(res + 8, 2) ? ld16(res + 8) : 0;
        const uint32_t ubo = obj && wwatch::readable(obj + 0x1C, 4) ? ld32(obj + 0x1C) : 0;
        LOG("[interp] player %s model=%08x recorded=0 n=%u ubo=%u", tag, model, n, ubo);
        return;
    }
    float best = 0.f;
    uint32_t bone = 0;
    float px = 0, cx = 0, dx = 0;
    for (uint32_t j = 0; j < found->n; j++) {
        const uint32_t* a = found->prev.data() + joints::kWords * j;
        const uint32_t* b = found->cur.data() + joints::kWords * j;
        const float ddx = joints::word_f(b[3]) - joints::word_f(a[3]);
        const float ddy = joints::word_f(b[7]) - joints::word_f(a[7]);
        const float ddz = joints::word_f(b[11]) - joints::word_f(a[11]);
        const float d = ddx * ddx + ddy * ddy + ddz * ddz;
        if (d > best) {
            best = d;
            bone = j;
            px = joints::word_f(a[3]);
            cx = joints::word_f(b[3]);
        }
    }
    if (best < 0.01f) return;
    const uint32_t obj = ld32(model + 0x40);
    const uint32_t block = obj ? ld32(obj + 0x18) : 0;
    const uint32_t ubo = obj ? ld32(obj + 0x1C) : 0;
    const uint32_t at = bone * 12 + 3;
    if (block && ubo / 4 > at && wwatch::readable(block, (at + 1) * 4))
        std::memcpy(&dx, mem::ptr(block) + at * 4, 4);
    uint32_t shp = 0;
    uint32_t ent = 0;
    if (ubo == 0 && wwatch::readable(model + 0x44, 4)) {
        const uint32_t arr = ld32(model + 0x44);
        const uint32_t nshp = wwatch::readable(model + 0x3C, 2) ? ld16(model + 0x3C) : 0;
        if (arr && nshp && wwatch::readable(arr, 0x10)) {
            const uint32_t shape = ld32(arr);
            shp = shape && wwatch::readable(shape + 0x16, 1) ? mem::ptr(shape + 0x16)[0] : 0;
            ent = mem::ptr(arr + 9)[0];
            const uint32_t hdr = ld32(arr + 0x0C);
            if (hdr && wwatch::readable(hdr, 8) && ent == 0 && shp == 0) {
                const uint32_t base = ld32(hdr);
                const uint32_t stride = ld32(hdr + 4);
                const uint32_t buf = base + stride * (found->views.empty() ? 0u : found->views[0]);
                if (base && wwatch::readable(buf, 16)) std::memcpy(&dx, mem::ptr(buf) + 12, 4);
            }
        }
    }
    const float span = cx - px;
    const float frac = std::fabs(span) > 0.05f ? (dx - px) / span : -1.f;
    left--;
    LOG("[interp] player %s n=%u bone=%u ubo=%u shp=%u ent=%u frac=%.2f prev=%.2f cur=%.2f drawn=%.2f",
        tag, found->n, bone, ubo, shp, ent, frac, px, cx, dx);
}

bool present_models(Cpu* c) {
    if (!enabled() || !blend_enabled() || !course_play_active()) return false;
    const float t = pacing::present_sample_t(!g_hold, pass_t());
    std::vector<uint32_t> drop;
    PoseTrace trace;
    g_in_model_present = true;
    for (auto& it : g_models) {
        ModelPose& p = it.second;
        if (p.step != g_logic_steps || p.n == 0 || p.prev.size() != p.n * joints::kWords || p.cur.size() != p.n * joints::kWords) {
            if (p.step != g_logic_steps) drop.push_back(it.first);
            continue;
        }
        if (!wwatch::readable(p.model, 0x48) || ld32(p.model + 0x40) != it.first || !wwatch::readable(p.mtx, p.n * 0x30)) {
            drop.push_back(it.first);
            continue;
        }
        const uint32_t* live = reinterpret_cast<const uint32_t*>(mem::ptr(p.mtx));
        const bool same = std::memcmp(p.prev.data(), p.cur.data(), p.n * 0x30) == 0;
        const bool exact = t >= 1.f || same;
        if (exact && !p.ubo_blended) {
            consider_pose(trace, p, p.cur.data());
            continue;
        }
        if (!exact) {
            std::vector<uint32_t> saved(live, live + p.n * joints::kWords);
            std::vector<uint32_t> mid(saved.size());
            const auto blended = joints::blend(p.prev.data(), p.cur.data(),
                                                p.prev_stale.size() == p.n ? p.prev_stale.data() : nullptr, p.n, joints::kCutDist, t,
                                                mid.data(), joints::blend_mtx);
            if (!blended.cut) {
                consider_pose(trace, p, mid.data());
                std::memcpy(mem::ptr(p.mtx), mid.data(), p.n * 0x30);
                upload_pose(c, p);
                std::memcpy(mem::ptr(p.mtx), saved.data(), p.n * 0x30);
                p.ubo_blended = true;
                static bool logged = false;
                if (!logged) {
                    logged = true;
                    LOG("[interp] course model pose blend on (%u bones)", p.n);
                }
                continue;
            }
        }
        consider_pose(trace, p, p.cur.data());
        if (p.ubo_blended) {
            upload_pose(c, p);
            p.ubo_blended = false;
        }
    }
    log_pose(trace, t);
    if (g_hold)
        for (int player = 0; player < 4; player++)
            log_player_part("head", player_model(player, 0x11Cu));
    g_in_model_present = false;
    for (uint32_t key : drop) g_models.erase(key);
    return true;
}
}

void reset_pass_clock();
void finish_wake();
void ss_reset() {
    clear_model_poses();
    g_record_passes += 8;
    g_hold_next = false;
    g_phase = 0;
    g_step_n = 1;
    g_frame_n = 1;
    g_cam = CamBox{};
    g_view_n = 0;
    g_cam_cur_valid = false;
    g_tile_proj_n = 0;
    g_box_open = false;
    g_sampling = false;
    g_capture_proj = 0;
    g_view_area = 0;
    g_blend_t = 1.f;
    g_interp_present = false;
    g_logic_steps = 0;
    g_executed_steps.store(0, std::memory_order_relaxed);
    g_present_i = 0;
    g_acc_ns = 0;
    g_wake_valid = false;
    reset_pass_clock();
    finish_wake();
}

static const char* const g_env_paced = env::INTERP_PACED();
static std::atomic<bool> g_paced{[] {
#ifdef __ANDROID__
    return !g_env_paced || atoi(g_env_paced) != 0;
#else
    return g_env_paced && atoi(g_env_paced) != 0;
#endif
}()};
static bool paced() {
    const int f = fps();
    if (f != 60) return true;
    return g_paced.load(std::memory_order_relaxed);
}
bool paced_interpolation() { return paced(); }
bool paced_interpolation_at(int f) {
    if (f != 60) return true;
    return g_paced.load(std::memory_order_relaxed);
}
void set_paced_interpolation_at(int f, bool on) {
    if (f > 60) return;
    if (g_paced.exchange(on) != on)
        LOG("[interp] paced interpolation at 60 fps %s", on ? "on (keeps the game's speed)" : "off");
}
void set_paced_interpolation(bool on) { set_paced_interpolation_at(fps(), on); }

static std::atomic<float> g_paced_share{-1};
float paced_drawn_share() { return paced() && interp_on() ? g_paced_share.load(std::memory_order_relaxed) : -1; }

using pace_clock = std::chrono::steady_clock;
static uint64_t g_paced_holds = 0, g_paced_steps = 0, g_paced_presents = 0;
static pacing::HoldShed g_shed;
static pace_clock::time_point g_pass_begin{};
static bool g_pass_begin_valid = false;
static bool g_wake_ready = false;
static bool g_wake_hold = false;
static bool g_wake_skip = false;
static bool g_skip_map_draw = false;
static pacing::WakePlan g_wake_plan;

static std::atomic<int> g_course_setup{0};
void begin_course_setup() { g_course_setup.fetch_add(1, std::memory_order_relaxed); }
void end_course_setup() { g_course_setup.fetch_sub(1, std::memory_order_relaxed); }
bool course_setup_active() { return g_course_setup.load(std::memory_order_relaxed) > 0; }
bool holds_shed() {
    if (unlocked() || !blend_enabled()) return false;
    return g_shed.shed && interp_on() && fps() > 60 && !fast_forward();
}
bool refresh_boost() {
    if (fast_forward() || !blend_enabled()) return false;
    if (unlocked()) return true;
    return mode() == 1 && fps() > 60;
}
int refresh_request_hz(int max_hz) {
    if (!blend_enabled() || max_hz <= 60 || fast_forward()) return 0;
    if (unlocked()) return max_hz;
    if (mode() == 1 && fps() > 60) return std::min(fps(), max_hz);
    return 0;
}
void prepare_wake() {
    apply_pose_reset();
    if (g_wake_ready) return;
    const bool previous_hold = g_wake_hold;
    const bool shed = holds_shed();
    g_frame_n = shed ? 1 : frames_per_step();
    g_step_n = std::max(0, g_frame_n - 1);
    const bool blend = blend_enabled();
    const int paced = output_fps();
    g_granule.store(unlocked() && blend ? 1 : pacing::present_granule(shed ? 60 : paced), std::memory_order_relaxed);
    const auto now = pace_clock::now();
    int64_t dt = 0;
    if (g_wake_valid) dt = std::chrono::duration_cast<std::chrono::nanoseconds>(now - g_last_wake).count();
    g_last_wake = now;
    g_wake_valid = true;
    const bool interpolate = pacing::wants_holds(gx2::uncapped(), shed, blend, unlocked(), interp_on(), paced);
    const int queued = gx2::gpu_frames_queued();
    pacing::WakePlan plan = pacing::advance_accumulator(g_acc_ns, dt, interpolate, pacing::logic_step_budget(queued));
    if (interpolate && plan.logic_steps == 0 && g_logic_steps == 0) {
        plan.logic_steps = 1;
        plan.t = 1.f;
        plan.acc_ns = 0;
    }
    g_acc_ns = plan.acc_ns;
    g_wake_plan = plan;
    g_wake_hold = interpolate && plan.logic_steps == 0;
    g_interp_present = interpolate;
    g_blend_t = interpolate ? plan.t : 1.f;
    if (g_wake_hold && output_fps() == 120)
        g_blend_t = 0.5f;
    g_hold = g_wake_hold;
    g_wake_skip = pacing::skip_extra_course_draw(g_wake_hold, course_play_active(), queued);
    g_skip_map_draw = pacing::skip_settled_map_draw(previous_hold, interpolate, course_play_active());
    g_wake_ready = true;
}
void finish_wake() { g_wake_ready = false; }
void reset_pass_clock() { g_pass_begin_valid = false; }
void begin_pass_clock() {
    g_pass_begin = pace_clock::now();
    g_pass_begin_valid = true;
}
void discard_pass_clock() { g_pass_begin_valid = false; }
void finish_logic_pass() {
    int64_t work = 0;
    const bool have = g_pass_begin_valid;
    if (have) work = std::chrono::duration_cast<std::chrono::nanoseconds>(pace_clock::now() - g_pass_begin).count();
    g_pass_begin_valid = false;
    const bool apply = blend_enabled() && !unlocked() && interp_on() && fps() > 60 && !fast_forward();
    const int64_t slot = apply ? pacing::shed_slot_ns(fps()) : 0;
    if (!have && slot > 0) return;
    const pacing::ShedUpdate upd = pacing::note_logic_work(g_shed, work, slot);
    if (!upd.changed) return;
    LOG("[interp] in-between frames %s: logic pass %.1f ms", upd.shed ? "paused" : "resumed", work / 1e6);
}

static void note_present_share(bool hold) {
    g_paced_presents++;
    if (hold) g_paced_holds++;
    else g_paced_steps++;
    static unsigned recentHolds = 0, recentPresents = 0, recentN = 0;
    recentHolds += hold ? 1 : 0;
    recentPresents++;
    if (++recentN == 60) {
        g_paced_share.store(recentHolds / float(std::max(1u, recentPresents)), std::memory_order_relaxed);
        recentHolds = recentPresents = recentN = 0;
    }
    if (g_paced_presents >= 300) {
        LOG("[interp] present cap %s: %.0f%% holds (%.2f presents per logic)", mode_name(),
            100.0 * g_paced_holds / double(std::max<uint64_t>(1, g_paced_presents)),
            g_paced_presents / double(std::max<uint64_t>(1, g_paced_steps)));
        g_paced_holds = g_paced_steps = g_paced_presents = 0;
    }
}
}

namespace interp {
namespace {
struct PassCpu {
    double ms[3] = {0, 0, 0};
    uint32_t n[3] = {0, 0, 0};
};
PassCpu g_pass_cpu;
bool pass_stats() {
    static const bool on = env::INTERP_PASS_STATS() != nullptr;
    return on;
}
double thread_cpu_ms() { return rprof::thread_cpu_ns() / 1e6; }

struct PassTimer {
    int kind;
    double t0;
    explicit PassTimer(int k) : kind(k), t0(pass_stats() ? thread_cpu_ms() : 0) {}
    ~PassTimer() {
        if (!pass_stats()) return;
        g_pass_cpu.ms[kind] += thread_cpu_ms() - t0;
        g_pass_cpu.n[kind]++;
    }
};
std::string pass_cpu_report() {
    if (!pass_stats()) return {};
    char b[160];
    auto avg = [](int k) { return g_pass_cpu.n[k] ? g_pass_cpu.ms[k] / g_pass_cpu.n[k] : 0.0; };
    snprintf(b, sizeof b, "; main thread CPU per pass: logic %.2f ms, blended hold %.2f ms (%u), record %.2f ms", avg(0), avg(1),
             g_pass_cpu.n[1], avg(2));
    g_pass_cpu = PassCpu{};
    return b;
}
}
}

extern "C" void hook_029C52CC(Cpu* c) {
    using namespace interp;
    perf::game_frame();
    if (!enabled()) {
        static const uint64_t at = env::INTERP_AT_STEP.u64_or(0);
        static uint64_t passes = 0;
        if (at && ++passes == at) set_enabled(true);
        if (!enabled()) {
            g_hold_next = false;
            g_hold = false;
            g_logic_pass = false;
            g_phase = 0;
            g_step_n = 1;
            g_frame_n = 1;
            g_granule.store(4, std::memory_order_relaxed);
            g_pass_valid = false;
            g_acc_ns = 0;
            g_wake_valid = false;
            g_blend_t = 1.f;
            g_interp_present = false;
            g_wake_hold = false;
            g_skip_map_draw = false;
            f_029C52CC_orig(c);
            nsmbu_align_map_camera();
            finish_logic_pass();
            finish_wake();
            status_trace::on_logic_step(logic_steps());
            return;
        }
    }
    prepare_wake();
    const uint32_t self = c->r[3];
    g_pass_valid = true;
    g_passes++;
    g_present_i++;

    if (g_wake_hold) {
        g_phase = std::min(g_phase + 1, std::max(1, g_step_n));
        g_hold = true;
        g_logic_pass = false;
        g_hold_next = true;
        note_present_share(true);
        if (!g_wake_skip) {
            apply_hold_camera(c);
            c->r[3] = self;
            PassTimer timer(1);
            f_029C5104(c);
            c->r[3] = self;
        }
        discard_pass_clock();
        finish_wake();
        return;
    }

    g_hold = false;
    g_logic_pass = true;
    g_hold_next = false;
    g_phase = 0;
    const int steps = std::max(1, g_wake_plan.logic_steps);
    for (int i = 0; i < steps; i++) {
        g_logic_steps++;
        g_record_passes++;
        g_tile_proj_n = 0;
        c->r[3] = self;
        PassTimer timer(0);
        f_029C52CC_orig(c);
    }
    nsmbu_align_map_camera();
    if (course_play_active()) {
        const uint32_t r3 = c->r[3], r4 = c->r[4], r5 = c->r[5];
        for (int player = 0; player < 4; player++) {
            const uint32_t head = player_model(player, 0x11Cu);
            if (!head) continue;
            c->r[3] = head;
            c->r[4] = 0;
            c->r[5] = 0;
            record_model(c);
        }
        c->r[3] = r3;
        c->r[4] = r4;
        c->r[5] = r5;
    }
    if (course_play_active() && blend_enabled() && output_fps() > 60)
        capture_grass_wave();
    else
        clear_grass_wave();
    note_present_share(false);
    finish_logic_pass();
    finish_wake();
    status_trace::on_logic_step(g_logic_steps);
    static uint64_t n = 0, t0 = timebase::now(), f0 = 0;
    if (++n % 300 == 0) {
        uint64_t t = timebase::now();
        LOG("[interp] %.1f logic steps/s (%s, %.2f presents per logic)%s", 300.0 * timebase::kTicksPerSec / (double)(t - t0), mode_name(),
            (g_passes - f0) / 300.0, pass_cpu_report().c_str());
        t0 = t;
        f0 = g_passes;
    }
}

extern "C" void hook_029FD94C(Cpu* c) {
    const int mode = gfx::g_mode.load(std::memory_order_relaxed);
    bool use = false;
    if (mode == gfx::kDrcGamePad || mode == gfx::kDrcAuto)
        use = true;
    else if (mode == gfx::kDrcWindow)
        use = gfx::g_has_drc_window.load(std::memory_order_relaxed) &&
              gfx::g_shown.load(std::memory_order_relaxed);
    else if (mode == gfx::kDrcPip)
        use = gfx::pip_shown_now();
    if (!use)
        return;
    f_029FD94C_orig(c);
}

extern "C" void hook_029C4FE4(Cpu* c) {
    using namespace interp;
    begin_pass_clock();
    if (enabled()) {
        prepare_wake();
        if (g_wake_skip || g_skip_map_draw) return;
        const uint32_t self = c->r[3];
        const bool course = course_play_active() && blend_enabled();
        if (course) apply_hold_camera(c, false, false);
        c->r[3] = self;
        const float grass_dx = course && g_hold ? baked_grass_shift() : 0.f;
        if (grass_dx != 0.f) apply_baked_grass_shift(grass_dx);
        const bool drew = present_models(c);
        c->r[3] = self;
        f_029C4FE4_orig(c);
        c->r[3] = self;
        if (course) finish_open_camera(c);
        c->r[3] = self;
        if (drew || grass_dx != 0.f) gx2::sync_submitted();
        if (grass_dx != 0.f) apply_baked_grass_shift(-grass_dx);
        return;
    }
    f_029C4FE4_orig(c);
}

extern "C" void hook_0268B70C(Cpu* c) {
    using namespace interp;
    const bool course = enabled() && course_play_active() && blend_enabled() && output_fps() > 60;
    if (course) trace_grass_screen();
    if (!course || !g_hold) {
        const uint32_t obj = grass_draw_obj();
        const uint32_t n0 = obj && wwatch::readable(obj + 0x398, 4) ? ld32(obj + 0x398) : 0;
        f_0268B70C_orig(c);
        if (course) remember_grass_bake(n0);
        return;
    }
    const uint32_t self = c->r[3];
    const float t = pacing::present_sample_t(false, pass_t());
    const bool patch = grass_want_hold_patch(t);
    if (patch) blend_grass_wave(t);
    const float dx = patch ? grass_cam_dx() : 0.f;
    if (dx != 0.f) grass_shift_x(dx, false);
    f_0268B70C_orig(c);
    c->r[3] = self;
    if (dx != 0.f) grass_shift_x(dx, true);
    if (patch) {
        gx2::sync_submitted();
        restore_grass_wave();
    }
    c->r[3] = self;
}

extern "C" void hook_024FBF6C(Cpu* c) {
    using namespace interp;
    remember_area();
    const uint32_t mgr = c->r[3], view = c->r[4], proj = c->r[6];
    f_024FBF6C_orig(c);
    if (!enabled() || !course_play_active()) return;
    const uint32_t r = renderer_instance();
    if (!r || proj != r + kProjection) return;
    remember_view(mgr, view, proj);
    if (g_sampling || g_box_open) return;
    float cur[4];
    if (!read_box(r, cur)) return;
    shift_camera(cur);
    present_camera(c, r, cur, mgr, view, proj, true);
}

static void course_bg_draw(Cpu* c, uint32_t info, void (*orig)(Cpu*)) {
    using namespace interp;
    uint32_t proj = 0;
    if (info && wwatch::readable(info + 0x18, 4)) {
        proj = ld32(info + 0x18);
        if (!proj || !wwatch::readable(proj + 0x44, 64)) proj = 0;
    }
    const uint32_t prev = g_capture_proj;
    if (proj) g_capture_proj = proj;
    orig(c);
    g_capture_proj = prev;
}

#define COURSE_BG(addr, reg) \
    extern "C" void f_##addr##_orig(Cpu* c); \
    extern "C" void hook_##addr(Cpu* c) { course_bg_draw(c, c->reg, f_##addr##_orig); }

COURSE_BG(024B47A8, r[6])
COURSE_BG(024B47C8, r[6])
COURSE_BG(026904D4, r[4])
COURSE_BG(026905C0, r[4])
COURSE_BG(02690680, r[4])
COURSE_BG(0269083C, r[4])
COURSE_BG(02691690, r[4])
COURSE_BG(0269179C, r[4])
COURSE_BG(026917E4, r[4])
COURSE_BG(02691868, r[4])
COURSE_BG(0269196C, r[4])
COURSE_BG(02691970, r[4])
COURSE_BG(026919A4, r[4])
COURSE_BG(02691A8C, r[4])
COURSE_BG(02691B74, r[4])
COURSE_BG(02691DF0, r[4])
COURSE_BG(02691EAC, r[4])
COURSE_BG(0269206C, r[4])
COURSE_BG(026920B0, r[4])
COURSE_BG(02692194, r[4])
COURSE_BG(0269222C, r[4])
COURSE_BG(02692748, r[4])
COURSE_BG(0269282C, r[4])
COURSE_BG(026928C4, r[4])
COURSE_BG(0269325C, r[5])

#undef COURSE_BG

namespace gx2 { uint64_t swap_count(); }

namespace interp {

void trace_read(const char* who) {

    static int n = env::TRACE_INPUT_PHASE.integer_or(0);
    if (n > 0 && enabled()) {
        n--;
        LOG("[interp] %s read: logic_pass=%d hold=%d hold_next=%d", who, g_logic_pass, g_hold, g_hold_next);
    }
}
bool hold_pass() { return g_hold; }

static uint64_t guest_logic_ticks() { return ld32(GD(0x101FF558)); }

uint64_t logic_steps() { return g_logic_steps ? g_logic_steps : ::gx2::swap_count(); }
void record_executed_step() { g_executed_steps.fetch_add(1, std::memory_order_relaxed); }
uint64_t executed_steps() {
    const uint64_t hooked = g_executed_steps.load(std::memory_order_relaxed);
    return hooked ? hooked : guest_logic_ticks();
}

bool course_play_active() {
    return ld32(GD(0x101D1598)) != 0;
}

bool input_enabled() {
    const uint32_t mgr = ld32(GD(0x101D2DF8));
    if (!mgr) return false;
    return (ld8(mgr + 0x56Cu) & 0x80u) == 0 && ld8(mgr + 0x56Du) == 0;
}

bool player_witness(float& x, float& y, float& z) {
    PlayerSnapshot snap;
    if (!player_snapshot(snap, 0)) return false;
    x = snap.x;
    y = snap.y;
    z = snap.z;
    return true;
}

bool player_snapshot(PlayerSnapshot& out, int player) {
    if (player < 0 || player > 3) return false;
    const uint32_t field = ld32(GD(0x101D1604));
    if (!field) return false;
    const uint32_t mgr = ld32(GD(0x101E6994));
    if (!mgr) return false;
    const uint32_t slot = field + 0x24u + (uint32_t)player * 0x48u;
    const uint32_t actor = ld32(mgr + 0x20u + (uint32_t)player * 4u);
    if (!actor) return false;
    out.player = player;
    out.lives = (int32_t)ld32(slot + 0x4u);
    out.coins = (int32_t)ld32(slot + 0x34u);
    out.course_coins = (int32_t)ld32(field + 0x144u);
    int32_t mode = (int32_t)ld32(slot + 0x8u);
    const int32_t actor_mode = (int32_t)ld32(actor + 0x500u);
    if (actor_mode >= 0 && actor_mode <= 8) mode = actor_mode;
    out.mode = mode;
    out.x = (float)ldf32(actor + 0x6Cu);
    out.y = (float)ldf32(actor + 0x70u);
    out.z = (float)ldf32(actor + 0x74u);
    return true;
}

bool set_player_mode(int player, int mode) {
    if (player < 0 || player > 3 || mode < 0 || mode > 8) return false;
    const uint32_t field = ld32(GD(0x101D1604));
    if (!field) return false;
    const uint32_t mgr = ld32(GD(0x101E6994));
    if (!mgr) return false;
    const uint32_t slot = field + 0x24u + (uint32_t)player * 0x48u;
    const uint32_t actor = ld32(mgr + 0x20u + (uint32_t)player * 4u);
    if (!actor) return false;
    st32(slot + 0x8u, (uint32_t)mode);
    st32(actor + 0x500u, (uint32_t)mode);
    return true;
}

bool logic_pass() { return g_logic_pass; }

uint64_t hold_pass_count() { return g_record_passes; }
uint64_t pass_count() { return g_passes; }
const char* phase_name() { return !enabled() ? "interp off" : g_hold ? "IN-BETWEEN" : g_logic_pass ? "logic" : "other"; }

bool repeat_input() {
    static const bool off = env::INTERP_NO_REPEAT() != nullptr;
    return enabled() && g_hold_next && !off;
}
}
