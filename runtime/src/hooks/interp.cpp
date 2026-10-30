#include "hooks/interp.h"
#include "mods/packages.h"
#include "mods/sdk/nsmbu.h"

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cmath>
#include <cstdlib>
#include <cstring>
#include <mutex>
#include <string>
#include <unordered_map>
#include <vector>

#include "hooks/interp_pacing.h"
#include "guest_addr.h"
#include "gfx/render_prof.h"
#include "perf_metrics.h"
#include "core/runtime.h"
#include "core/write_watch.h"
#include "agent/agent_ctl.h"
#include "save/savestate.h"
#include "test/status_trace.h"

#include "core/env.h"
extern "C" {
void f_029C52CC_orig(Cpu* c);
void f_029C4FE4_orig(Cpu* c);
void f_024FBF6C_orig(Cpu* c);
void f_024F30DC_orig(Cpu* c);
void f_029C5104(Cpu* c);
void f_024FA76C(Cpu* c);
void f_02A05A9C(Cpu* c);
void f_02AA56EC(Cpu* c);
void f_02AA56F4(Cpu* c);
void imp_gx2_GX2DrawDone(Cpu* c);
}

namespace gx2 { bool uncapped(); void set_uncapped(bool on); int gpu_frames_queued(); }

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
void set_unlocked(bool on) {
    if (g_unlocked.exchange(on, std::memory_order_relaxed) == on) return;
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
    if (g_custom_on.load(std::memory_order_relaxed)) {
        const int hz = display_hz();
        if (!present_vsync() || hz <= 0) return f;
        return std::min(f, std::max(1, hz));
    }
    return pacing::cap_fps(f, display_hz(), present_vsync());
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
    g_on = v;
    LOG("[interp] frame interpolation %s", v ? fps_name(fps()) : "off");
}
void set_fps(int f) {
    g_unlocked.store(false, std::memory_order_relaxed);
    g_custom_on.store(false, std::memory_order_relaxed);
    f = valid_fps(f);
    if (g_fps.exchange(f) != f && interp_on()) LOG("[interp] frame interpolation at %s", fps_name(f));
}
void set_custom_fps(int f) {
    g_unlocked.store(false, std::memory_order_relaxed);
    f = pacing::clamp_custom_fps(f);
    g_custom_fps.store(f, std::memory_order_relaxed);
    g_custom_on.store(true, std::memory_order_relaxed);
    if (f == 60) {
        g_on = false;
        LOG("[interp] frame interpolation off");
        return;
    }
    set_enabled(true);
}
int mode() { return unlocked() || (interp_on() && fps() != 60) ? 1 : 0; }
void set_mode(int m) {
    if (m == 0) g_unlocked.store(false, std::memory_order_relaxed);
    g_on = false;
    if (m == 1) set_enabled(true);
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

bool g_exact_step = false;
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

void remember_area() {
    const uint32_t area = ld32(GD(0x101D1598));
    if (area == g_view_area) return;
    g_view_n = 0;
    g_cam_cur_valid = false;
    g_cam.valid = false;
    g_view_area = area;
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

bool record_pass() { return false; }

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

void apply_hold_camera(Cpu* c) {
    remember_area();
    if (!course_play_active() || !g_cam_cur_valid) return;
    const uint32_t r = renderer_instance();
    if (!r) return;
    float cur[4], box[4];
    for (int i = 0; i < 4; i++) cur[i] = g_cam_cur[i];
    const bool record = record_pass();
    const bool blend = !record && g_cam.valid && !g_exact_step && g_cam.step == g_logic_steps && !cam_cut(g_cam.v, cur);
    if (record) {
        g_cam.valid = true;
        g_cam.step = g_logic_steps;
        for (int i = 0; i < 4; i++) g_cam.v[i] = cur[i];
    }
    if (blend) {
        const float t = pass_t();
        for (int i = 0; i < 4; i++) box[i] = pacing::lerp_f(g_cam.v[i], cur[i], t);
    } else {
        for (int i = 0; i < 4; i++) box[i] = cur[i];
    }
    apply_box(c, r, box);
    for (int i = 0; i < g_view_n; i++) write_view_proj(c, g_views[i].mgr, g_views[i].view, g_views[i].proj);
    apply_box(c, r, cur);
    cam_trace(record ? "record" : blend ? "hold" : "snap", g_cam.v, cur, box);
}

constexpr uint32_t kMaxModelJoints = 256;
constexpr uint32_t kSklWorldArr = 0x10;
struct ModelPose {
    uint32_t mdl = 0;
    uint32_t mtx = 0;
    uint32_t joints = 0;
    uint64_t pass = ~0ull;
    uint64_t sync_pass = 0;
    uint64_t subst_pass = ~0ull;
    std::vector<uint32_t> pose;
    std::vector<uint32_t> next;
    std::vector<uint32_t> exact;
    std::vector<uint32_t> mid;
};
std::unordered_map<uint32_t, ModelPose> g_models;
struct ModelStats { uint32_t blended = 0, fresh = 0, cut = 0, still = 0; } g_mstats;

void trace_model(const char* what, uint32_t mdl, uint32_t skel, uint32_t count, uint64_t gen, const uint32_t* w) {
    static int left = env::INTERP_MODEL_TRACE.integer_or(0);
    if (left <= 0 || !enabled()) return;
    left--;
    LOG("[interp] model mdl %08X skel %08X joints %u gen %llu step %llu hold %d phase %d/%d t %.3f %-7s w3 %08X w7 %08X w11 %08X",
        mdl, skel, count, (unsigned long long)gen, (unsigned long long)g_logic_steps, g_hold, g_phase, g_step_n, pass_t(), what,
        w ? w[3] : 0, w ? w[7] : 0, w ? w[11] : 0);
}

inline uint32_t ld32g(uint32_t ea) { return ea >= 0x02000000u && ea < 0x70000000u && wwatch::readable(ea, 4) ? ld32(ea) : 0; }
inline uint16_t ld16g(uint32_t ea) { return ea >= 0x02000000u && ea < 0x70000000u && wwatch::readable(ea, 2) ? ld16(ea) : 0; }

bool model_skeleton(uint32_t mdl, uint32_t& skel, uint32_t& count, uint32_t& arr) {
    uint32_t s = ld32g(mdl + 0x40);
    if (s < 0x02000000u || s >= 0x70000000u) return false;
    uint32_t a = ld32g(s + kSklWorldArr);
    if (a < 0x02000000u || a >= 0x70000000u) return false;
    uint32_t res = ld32g(s);
    if (res < 0x02000000u || res >= 0x70000000u) return false;
    count = ld16g(res + 8);
    if (count == 0 || count > kMaxModelJoints) return false;
    if (!wwatch::readable(a, 12 * 4 * count)) return false;
    skel = s;
    arr = a;
    return true;
}

uint32_t fw(float f) { return f32_as_u32(f); }

bool model_to_quat(const float* m, float* s, float* q) {
    float r[3][3];
    for (int j = 0; j < 3; j++) {
        s[j] = std::sqrt(m[j] * m[j] + m[4 + j] * m[4 + j] + m[8 + j] * m[8 + j]);
        if (s[j] < 1e-6f) return false;
        for (int i = 0; i < 3; i++) r[i][j] = m[4 * i + j] / s[j];
    }
    for (int a = 0; a < 3; a++)
        for (int b = a + 1; b < 3; b++)
            if (std::fabs(r[0][a] * r[0][b] + r[1][a] * r[1][b] + r[2][a] * r[2][b]) > 0.02f) return false;
    float det = r[0][0] * (r[1][1] * r[2][2] - r[1][2] * r[2][1]) - r[0][1] * (r[1][0] * r[2][2] - r[1][2] * r[2][0]) +
                r[0][2] * (r[1][0] * r[2][1] - r[1][1] * r[2][0]);
    if (det < 0.9f) return false;
    float tr = r[0][0] + r[1][1] + r[2][2];
    if (tr > 0) {
        float k = 0.5f / std::sqrt(tr + 1.0f);
        q[0] = 0.25f / k; q[1] = (r[2][1] - r[1][2]) * k; q[2] = (r[0][2] - r[2][0]) * k; q[3] = (r[1][0] - r[0][1]) * k;
    } else if (r[0][0] > r[1][1] && r[0][0] > r[2][2]) {
        float k = 2.0f * std::sqrt(1.0f + r[0][0] - r[1][1] - r[2][2]);
        q[0] = (r[2][1] - r[1][2]) / k; q[1] = 0.25f * k; q[2] = (r[0][1] + r[1][0]) / k; q[3] = (r[0][2] + r[2][0]) / k;
    } else if (r[1][1] > r[2][2]) {
        float k = 2.0f * std::sqrt(1.0f + r[1][1] - r[0][0] - r[2][2]);
        q[0] = (r[0][2] - r[2][0]) / k; q[1] = (r[0][1] + r[1][0]) / k; q[2] = 0.25f * k; q[3] = (r[1][2] + r[2][1]) / k;
    } else {
        float k = 2.0f * std::sqrt(1.0f + r[2][2] - r[0][0] - r[1][1]);
        q[0] = (r[1][0] - r[0][1]) / k; q[1] = (r[0][2] + r[2][0]) / k; q[2] = (r[1][2] + r[2][1]) / k; q[3] = 0.25f * k;
    }
    return true;
}

void model_blend_mtx(const float* a, const float* b, float* out, float t) {
    float sa[3], sb[3], qa[4], qb[4];
    for (int i = 0; i < 3; i++) out[4 * i + 3] = pacing::lerp_f(a[4 * i + 3], b[4 * i + 3], t);
    if (!model_to_quat(a, sa, qa) || !model_to_quat(b, sb, qb)) {
        for (int i = 0; i < 3; i++)
            for (int j = 0; j < 3; j++) out[4 * i + j] = pacing::lerp_f(a[4 * i + j], b[4 * i + j], t);
        return;
    }
    float d = qa[0] * qb[0] + qa[1] * qb[1] + qa[2] * qb[2] + qa[3] * qb[3];
    float sg = d < 0 ? -1.0f : 1.0f, q[4], n = 0;
    float wa, wb;
    pacing::slerp_weights(std::fabs(d), t, wa, wb);
    wb *= sg;
    for (int k = 0; k < 4; k++) { q[k] = wa * qa[k] + wb * qb[k]; n += q[k] * q[k]; }
    n = 1.0f / std::sqrt(n);
    float w = q[0] * n, x = q[1] * n, y = q[2] * n, z = q[3] * n;
    const float r[3][3] = {{1 - 2 * (y * y + z * z), 2 * (x * y - w * z), 2 * (x * z + w * y)},
                           {2 * (x * y + w * z), 1 - 2 * (x * x + z * z), 2 * (y * z - w * x)},
                           {2 * (x * z - w * y), 2 * (y * z + w * x), 1 - 2 * (x * x + y * y)}};
    for (int j = 0; j < 3; j++) {
        float s = pacing::lerp_f(sa[j], sb[j], t);
        for (int i = 0; i < 3; i++) out[4 * i + j] = r[i][j] * s;
    }
}

bool model_pose_sane(const uint32_t* w, uint32_t count) {
    for (uint32_t m = 0; m < count; m++) {
        const uint32_t* a = w + 12 * m;
        for (int k = 0; k < 12; k++) {
            float v = u32_as_f32(a[k]);
            if (!std::isfinite(v) || std::fabs(v) > 10000.0f) return false;
        }
        for (int i = 0; i < 3; i++) {
            float n = std::sqrt(u32_as_f32(a[i]) * u32_as_f32(a[i]) + u32_as_f32(a[4 + i]) * u32_as_f32(a[4 + i]) +
                                u32_as_f32(a[8 + i]) * u32_as_f32(a[8 + i]));
            if (n < 0.05f || n > 100.0f) return false;
        }
    }
    return true;
}

inline void model_read(uint32_t arr, uint32_t words, std::vector<uint32_t>& out) {
    out.resize(words);
    const uint32_t* src = (const uint32_t*)mem::ptr(arr);
    for (uint32_t i = 0; i < words; i++) out[i] = __builtin_bswap32(src[i]);
}

inline void model_write(uint32_t arr, const std::vector<uint32_t>& in) {
    uint32_t* dst = (uint32_t*)mem::ptr(arr);
    for (size_t i = 0; i < in.size(); i++) dst[i] = __builtin_bswap32(in[i]);
}

void model_upload(Cpu* c, uint32_t mdl) {
    const uint32_t save3 = c->r[3];
    const uint32_t save4 = c->r[4];
    c->r[3] = mdl + 0x28;
    c->r[4] = 0;
    f_02AA56EC(c);
    c->r[3] = mdl + 0x28;
    c->r[4] = 0;
    f_02AA56F4(c);
    c->r[3] = save3;
    c->r[4] = save4;
}

void model_stats_report() {
    static uint64_t n = 0;
    if (++n % 300 != 0 || !(g_mstats.blended + g_mstats.fresh + g_mstats.cut + g_mstats.still)) return;
    LOG("[interp] models blended %u fresh %u cut %u still %u", g_mstats.blended, g_mstats.fresh, g_mstats.cut, g_mstats.still);
    g_mstats = ModelStats{};
}

bool model_blend_pose(ModelPose& p, uint32_t count, uint32_t words, const std::vector<uint32_t>& to, std::vector<uint32_t>& mid) {
    if (p.pose.size() != words || to.size() != words || memcmp(p.pose.data(), to.data(), 4u * words) == 0) return false;
    const float kCut = (float)env::INTERP_MODEL_CUT.real_or(400.0f);
    float dx = u32_as_f32(to[3]) - u32_as_f32(p.pose[3]);
    float dy = u32_as_f32(to[7]) - u32_as_f32(p.pose[7]);
    float dz = u32_as_f32(to[11]) - u32_as_f32(p.pose[11]);
    if (dx * dx + dy * dy + dz * dz > kCut * kCut) return false;
    const float t = pass_t();
    mid.resize(words);
    for (uint32_t m = 0; m < count; m++) {
        float a[12], b[12], o[12];
        for (int k = 0; k < 12; k++) {
            a[k] = u32_as_f32(p.pose[12 * m + k]);
            b[k] = u32_as_f32(to[12 * m + k]);
        }
        model_blend_mtx(a, b, o, t);
        for (int k = 0; k < 12; k++) mid[12 * m + k] = fw(o[k]);
    }
    return true;
}

void model_commit_step(ModelPose& p, uint32_t mdl, uint32_t mtx, uint32_t count, uint32_t words,
                       const std::vector<uint32_t>& cur) {
    p.mdl = mdl;
    p.mtx = mtx;
    p.joints = count;
    if (p.next.empty()) {
        p.pose = cur;
        p.next = cur;
    } else if (p.sync_pass != g_record_passes) {
        p.pose = p.next;
        p.next = cur;
        p.sync_pass = g_record_passes;
    }
    p.pass = g_record_passes > 0 ? g_record_passes - 1 : 0;
    p.subst_pass = ~0ull;
    p.exact.clear();
    p.mid.clear();
}

void model_calc_gpu(Cpu* c) {
    const uint32_t mdl = c->r[3];
    uint32_t skel, count, arr;
    if (!model_skeleton(mdl, skel, count, arr)) {
        f_024F30DC_orig(c);
        return;
    }
    const uint32_t words = 12 * count;
    auto it = g_models.find(skel);
    if (it != g_models.end() && it->second.subst_pass == g_passes && it->second.mtx == arr &&
        it->second.mid.size() == words) {
        model_write(arr, it->second.mid);
        if (!ld8(mdl + 0x28 + 0x0D)) model_upload(c, mdl);
        f_024F30DC_orig(c);
        return;
    }
    std::vector<uint32_t> cur;
    model_read(arr, words, cur);
    if (!model_pose_sane(cur.data(), count)) {
        g_mstats.fresh++;
        trace_model("fresh", mdl, skel, count, ~0ull, cur.data());
        f_024F30DC_orig(c);
        return;
    }
    ModelPose& p = g_models[skel];
    if (p.mtx != arr || p.pose.size() != words) {
        p.pose.clear();
        p.next.clear();
        p.sync_pass = 0;
    }
    model_commit_step(p, mdl, arr, count, words, cur);
    if (memcmp(p.pose.data(), p.next.data(), 4u * words) == 0) {
        g_mstats.still++;
        trace_model("still", mdl, skel, count, p.pass, p.next.data());
        f_024F30DC_orig(c);
        return;
    }
    std::vector<uint32_t> mid;
    if (!model_blend_pose(p, count, words, p.next, mid)) {
        g_mstats.cut++;
        trace_model("cut", mdl, skel, count, p.pass, p.next.data());
        f_024F30DC_orig(c);
        return;
    }
    p.exact = p.next;
    p.mid = mid;
    p.subst_pass = g_passes;
    model_write(arr, mid);
    if (!ld8(mdl + 0x28 + 0x0D)) model_upload(c, mdl);
    f_024F30DC_orig(c);
    g_mstats.blended++;
    trace_model("blended", mdl, skel, count, p.pass, mid.data());
}

bool model_subst_pending() {
    for (const auto& kv : g_models) {
        const ModelPose& p = kv.second;
        if (p.subst_pass != ~0ull && !p.exact.empty() && p.mtx) return true;
    }
    return false;
}

void model_restore_draw(Cpu* c) {
    for (auto& kv : g_models) {
        ModelPose& p = kv.second;
        if (p.subst_pass == ~0ull || p.exact.empty() || !p.mtx) continue;
        model_write(p.mtx, p.exact);
        if (p.mdl && !ld8(p.mdl + 0x28 + 0x0D)) model_upload(c, p.mdl);
        p.subst_pass = ~0ull;
        p.exact.clear();
        p.mid.clear();
    }
}

void model_clear() {
    g_models.clear();
    g_mstats = ModelStats{};
}

}

static const bool g_models_on = [] {
    const char* e = env::INTERP_MODELS();
    return !e || atoi(e) != 0;
}();

static void model_record_after_logic(Cpu* c) {
    using namespace interp;
    const uint32_t mdl = c->r[3];
    uint32_t skel, count, arr;
    if (!model_skeleton(mdl, skel, count, arr)) return;
    const uint32_t words = 12 * count;
    std::vector<uint32_t> cur;
    model_read(arr, words, cur);
    if (!model_pose_sane(cur.data(), count)) return;
    ModelPose& p = g_models[skel];
    if (p.mtx != arr || p.pose.size() != words) {
        p.pose.clear();
        p.next.clear();
        p.sync_pass = 0;
    }
    model_commit_step(p, mdl, arr, count, words, cur);
    trace_model("record", mdl, skel, count, p.pass, p.next.data());
}

extern "C" void hook_024F30DC(Cpu* c) {
    using namespace interp;
    if (g_models_on && enabled() && course_play_active()) {
        if (g_hold && !g_exact_step)
            model_calc_gpu(c);
        else {
            f_024F30DC_orig(c);
            if (!g_hold) model_record_after_logic(c);
        }
    } else
        f_024F30DC_orig(c);
}


namespace interp {
void reset_pass_clock();
void finish_wake();
void ss_reset() {
    g_record_passes += 8;
    g_hold_next = false;
    g_phase = 0;
    g_step_n = 1;
    g_frame_n = 1;
    g_cam = CamBox{};
    g_view_n = 0;
    g_cam_cur_valid = false;
    g_view_area = 0;
    model_clear();
    g_blend_t = 1.f;
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
static pacing::WakePlan g_wake_plan;

static std::atomic<int> g_course_setup{0};
void begin_course_setup() { g_course_setup.fetch_add(1, std::memory_order_relaxed); }
void end_course_setup() { g_course_setup.fetch_sub(1, std::memory_order_relaxed); }
bool course_setup_active() { return g_course_setup.load(std::memory_order_relaxed) > 0; }
bool holds_shed() {
    if (unlocked()) return false;
    return g_shed.shed && interp_on() && fps() > 60 && !fast_forward();
}
bool refresh_boost() {
    if (fast_forward()) return false;
    if (unlocked()) return true;
    return mode() == 1 && fps() > 60;
}
int refresh_request_hz(int max_hz) {
    if (max_hz <= 60 || fast_forward()) return 0;
    if (unlocked()) return max_hz;
    if (mode() == 1 && fps() > 60) return std::min(fps(), max_hz);
    return 0;
}
void prepare_wake() {
    if (g_wake_ready) return;
    const bool shed = holds_shed();
    g_frame_n = shed ? 1 : frames_per_step();
    g_step_n = std::max(0, g_frame_n - 1);
    g_granule.store(unlocked() ? 1 : pacing::present_granule(shed ? 60 : output_fps()), std::memory_order_relaxed);
    const auto now = pace_clock::now();
    int64_t dt = 0;
    if (g_wake_valid) dt = std::chrono::duration_cast<std::chrono::nanoseconds>(now - g_last_wake).count();
    g_last_wake = now;
    g_wake_valid = true;
    const bool interpolate = !gx2::uncapped() && !shed && (unlocked() || (interp_on() && output_fps() != 60));
    const int queued = gx2::gpu_frames_queued();
    pacing::WakePlan plan = pacing::advance_accumulator(g_acc_ns, dt, interpolate, pacing::logic_step_budget(queued));
    if (interpolate && plan.logic_steps == 0 && g_logic_steps == 0) {
        plan.logic_steps = 1;
        plan.t = 1.f;
        plan.acc_ns = 0;
    }
    g_acc_ns = plan.acc_ns;
    g_blend_t = interpolate ? plan.t : 1.f;
    g_exact_step = !interpolate || plan.logic_steps > 0;
    g_wake_plan = plan;
    g_wake_hold = interpolate && plan.logic_steps == 0;
    g_hold = g_wake_hold;
    g_wake_skip = pacing::skip_extra_course_draw(g_wake_hold, course_play_active(), queued);
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
    const bool apply = !unlocked() && interp_on() && fps() > 60 && !fast_forward();
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
            f_029C52CC_orig(c);
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
        c->r[3] = self;
        PassTimer timer(0);
        f_029C52CC_orig(c);
    }
    note_present_share(false);
    finish_logic_pass();
    finish_wake();
    status_trace::on_logic_step(g_logic_steps);
    model_stats_report();
    static uint64_t n = 0, t0 = timebase::now(), f0 = 0;
    if (++n % 300 == 0) {
        uint64_t t = timebase::now();
        LOG("[interp] %.1f logic steps/s (%s, %.2f presents per logic)%s", 300.0 * timebase::kTicksPerSec / (double)(t - t0), mode_name(),
            (g_passes - f0) / 300.0, pass_cpu_report().c_str());
        t0 = t;
        f0 = g_passes;
    }
}

extern "C" void hook_029C4FE4(Cpu* c) {
    using namespace interp;
    begin_pass_clock();
    if (enabled()) {
        prepare_wake();
        if (g_wake_skip || (g_hold && !course_play_active())) return;
    }
    f_029C4FE4_orig(c);
    if (!g_models_on || !enabled() || !course_play_active() || g_exact_step || !model_subst_pending()) return;
    imp_gx2_GX2DrawDone(c);
    model_restore_draw(c);
}

extern "C" void hook_024FBF6C(Cpu* c) {
    using namespace interp;
    remember_area();
    const uint32_t mgr = c->r[3], view = c->r[4], proj = c->r[6];
    f_024FBF6C_orig(c);
    if (!enabled() || !course_play_active()) return;
    const uint32_t r = renderer_instance();
    if (!r || proj != r + kProjection) return;
    float cur[4];
    if (!read_box(r, cur)) return;
    for (int i = 0; i < 4; i++) g_cam_cur[i] = cur[i];
    g_cam_cur_valid = true;
    remember_view(mgr, view, proj);
    const bool blend = g_cam.valid && !g_exact_step && g_cam.step == g_logic_steps && !cam_cut(g_cam.v, cur);
    if (!blend) {
        g_cam.valid = true;
        g_cam.step = g_logic_steps;
        for (int i = 0; i < 4; i++) g_cam.v[i] = cur[i];
        cam_trace("exact", cur, cur, cur);
        return;
    }
    float mid[4];
    const float t = pass_t();
    for (int i = 0; i < 4; i++) mid[i] = pacing::lerp_f(g_cam.v[i], cur[i], t);
    apply_box(c, r, mid);
    write_view_proj(c, mgr, view, proj);
    apply_box(c, r, cur);
    cam_trace("blend", g_cam.v, cur, mid);
}

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
