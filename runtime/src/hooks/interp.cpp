#include "hooks/interp.h"
#include "mods/packages.h"
#include "mods/sdk/nsmbu.h"

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cmath>
#include <cstdlib>
#include <cstring>
#include <string>
#include <unordered_map>
#include <vector>

#include "hooks/interp_joints.h"
#include "hooks/interp_pacing.h"
#include "hooks/interp_picture.h"
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
bool blend_env_locked() {
    const char* e = env::INTERP();
    return e && *e;
}
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
void set_blend_enabled(bool on) {
    if (blend_env_locked()) return;
    if (g_blend.exchange(on, std::memory_order_relaxed) == on) return;
    pose_reset();
    LOG("[interp] in-between frames %s", on ? "on" : "off");
}
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

void clear_model_poses();

void remember_area() {
    const uint32_t area = ld32(GD(0x101D1598));
    if (area == g_view_area) return;
    g_view_n = 0;
    g_cam_cur_valid = false;
    g_cam.valid = false;
    g_view_area = area;
    clear_model_poses();
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

void present_camera(Cpu* c, uint32_t r, const float* live, uint32_t mgr, uint32_t view, uint32_t proj) {
    if (!g_interp_present || !g_cam.valid || !g_cam_cur_valid || cam_cut(g_cam.v, g_cam_cur)) {
        cam_trace("snap", g_cam.v, live, live);
        return;
    }
    const float t = pacing::present_sample_t(!g_hold, pass_t());
    float box[4];
    bool same = true;
    for (int i = 0; i < 4; i++) {
        box[i] = pacing::lerp_f(g_cam.v[i], g_cam_cur[i], t);
        if (std::fabs(box[i] - live[i]) > 0.01f) same = false;
    }
    if (same) {
        cam_trace("same", g_cam.v, g_cam_cur, box);
        return;
    }
    apply_box(c, r, box);
    if (proj) write_view_proj(c, mgr, view, proj);
    else for (int i = 0; i < g_view_n; i++) write_view_proj(c, g_views[i].mgr, g_views[i].view, g_views[i].proj);
    apply_box(c, r, live);
    cam_trace(g_hold ? "hold" : "older", g_cam.v, g_cam_cur, box);
}

void apply_hold_camera(Cpu* c) {
    remember_area();
    if (!course_play_active() || !g_cam_cur_valid) return;
    const uint32_t r = renderer_instance();
    if (!r) return;
    float cur[4];
    if (!read_box(r, cur)) {
        for (int i = 0; i < 4; i++) cur[i] = g_cam_cur[i];
    }
    present_camera(c, r, cur, 0, 0, 0);
}



void pose_reset() { g_pose_reset.store(true, std::memory_order_relaxed); }

void apply_pose_reset() {
    if (!g_pose_reset.exchange(false, std::memory_order_relaxed)) return;
    g_cam = CamBox{};
    g_cam_cur_valid = false;
    g_view_n = 0;
    clear_model_poses();
}

}

namespace interp {
namespace {
void record_model(Cpu* c);
}
}

extern "C" void hook_024F30DC(Cpu* c) {
    const uint32_t model = c->r[3];
    const uint32_t view = c->r[4];
    f_024F30DC_orig(c);
    c->r[3] = model;
    c->r[4] = view;
    interp::record_model(c);
}


namespace interp {
namespace {
constexpr uint32_t kCalcView = 0x02AAAD18;
constexpr uint32_t kUploadShapes = 0x02AA56F4;
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
    std::vector<uint32_t> prev;
    std::vector<uint8_t> prev_stale;
    std::vector<uint32_t> cur;
    std::vector<uint8_t> cur_stale;
};
std::unordered_map<uint32_t, ModelPose> g_models;
bool g_in_model_present = false;

void clear_model_poses() { g_models.clear(); }

void note_view(ModelPose& p, uint32_t view) {
    if (view > 31) return;
    for (uint8_t v : p.views)
        if (v == view) return;
    if (p.views.size() < 4) p.views.push_back(static_cast<uint8_t>(view));
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

void upload_pose(Cpu* c, ModelPose& p) {
    const uint32_t obj = ld32(p.model + 0x40);
    const uint32_t count = p.views.empty() ? 1u : static_cast<uint32_t>(p.views.size());
    for (uint32_t i = 0; i < count; i++) {
        const uint32_t view = p.views.empty() ? 0u : p.views[i];
        guest_call(c, kCalcView, {obj, view});
        guest_call(c, kUploadShapes, {p.model + 0x28, view});
    }
    if (!obj || !wwatch::readable(obj, 0x20)) return;
    const uint32_t block = ld32(obj + 0x18);
    const uint32_t size = ld32(obj + 0x1C);
    if (!block || size == 0 || !wwatch::readable(block, size)) return;
    picture::snapshot(p.submitted, mem::ptr(block), size);
}

void record_model(Cpu* c) {
    if (g_in_model_present || !enabled() || !blend_enabled() || g_hold || !g_logic_pass || !course_play_active()) return;
    const uint32_t model = c->r[3];
    const uint32_t view = c->r[4];
    if (!wwatch::readable(model, 0x48)) return;
    const uint32_t obj = ld32(model + 0x40);
    if (!obj || !wwatch::readable(obj, 0x20)) return;
    const uint32_t res = ld32(obj);
    const uint32_t mtx = ld32(obj + 0x10);
    const uint32_t ubo_size = ld32(obj + 0x1C);
    if (!res || !mtx || !wwatch::readable(res, 0xC)) return;
    const uint32_t n = ld16(res + 8);
    if (n == 0 || n > kMaxBones || ubo_size == 0 || ubo_size > n * 0x40) return;
    if (!wwatch::readable(mtx, n * 0x30)) return;

    const uint32_t* live = reinterpret_cast<const uint32_t*>(mem::ptr(mtx));
    ModelPose& p = g_models[obj];
    if (p.step == g_logic_steps && p.mtx == mtx && p.n == n && p.cur.size() == n * joints::kWords && p.cur_stale.size() == n) {
        p.model = model;
        note_view(p, view);
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
    note_view(p, view);
}

bool present_models(Cpu* c) {
    if (!enabled() || !blend_enabled() || !course_play_active()) return false;
    const float t = pacing::present_sample_t(!g_hold, pass_t());
    std::vector<uint32_t> drop;
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
        if (exact && !p.ubo_blended) continue;
        if (!exact) {
            std::vector<uint32_t> saved(live, live + p.n * joints::kWords);
            std::vector<uint32_t> mid(saved.size());
            const auto blended = joints::blend(p.prev.data(), p.cur.data(),
                                                p.prev_stale.size() == p.n ? p.prev_stale.data() : nullptr, p.n, joints::kCutDist, t,
                                                mid.data(), joints::blend_mtx);
            if (!blended.cut) {
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
        if (p.ubo_blended) {
            upload_pose(c, p);
            p.ubo_blended = false;
        }
    }
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
        if (g_wake_skip || g_skip_map_draw) return;
        const uint32_t self = c->r[3];
        const bool sync = present_models(c);
        c->r[3] = self;
        f_029C4FE4_orig(c);
        if (sync) gx2::sync_submitted();
        return;
    }
    f_029C4FE4_orig(c);
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
    shift_camera(cur);
    remember_view(mgr, view, proj);
    present_camera(c, r, cur, mgr, view, proj);
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
