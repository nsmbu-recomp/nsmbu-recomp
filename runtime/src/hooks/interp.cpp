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
#include "core/runtime.h"
#include "save/savestate.h"

#include "core/env.h"
extern "C" {
void f_025F172C_orig(Cpu* c);
void f_0203593C_orig(Cpu* c);
void f_020315CC_orig(Cpu* c);
void f_02032FE8_orig(Cpu* c);
void f_020357CC_orig(Cpu* c);
void f_025EE048_orig(Cpu* c);
void f_02756170_orig(Cpu* c);
void f_02617AF4_orig(Cpu* c);
void f_02614F74_orig(Cpu* c);
void f_0273841C_orig(Cpu* c);
void f_0270870C_orig(Cpu* c);
void f_0255E854_orig(Cpu* c);
void f_02738438_orig(Cpu* c);
void f_0278FEBC_orig(Cpu* c);
void f_027199C0_orig(Cpu* c);
void f_02618940_orig(Cpu* c);
void f_02728A74_orig(Cpu* c);
void f_02039834_orig(Cpu* c);

void f_025D42EC(Cpu* c);
void f_025DE788_orig(Cpu* c);
void f_025DE024_orig(Cpu* c);
void f_025E0EE4_orig(Cpu* c);
void f_025DDCEC_orig(Cpu* c);
void f_025D42C4_orig(Cpu* c);
void f_0200E6EC_orig(Cpu* c);
void f_024FFC40_orig(Cpu* c);
void f_025E1B44_orig(Cpu* c);
void f_02027754_orig(Cpu* c);
void f_0201EBA0_orig(Cpu* c);
void f_025E1988_orig(Cpu* c);
void f_025E19CC_orig(Cpu* c);
void f_025E1A04_orig(Cpu* c);
void f_025E1A40_orig(Cpu* c);
void f_025E1A7C_orig(Cpu* c);
void f_025E1AA4_orig(Cpu* c);
void f_027F55FC_orig(Cpu* c);
void f_027F5018_orig(Cpu* c);

}

namespace interp {

using pacing::valid_fps;
static const int g_env_fps = [] { const char* e = env::INTERP_FPS(); return e ? valid_fps(atoi(e)) : 0; }();
static std::atomic<int> g_fps{g_env_fps ? g_env_fps : 60};
static std::atomic<bool> g_on{[] {
    const char* e = env::INTERP();
    return (e ? atoi(e) != 0 : g_env_fps != 0);
}()};
bool interp_on() { return g_on.load(std::memory_order_relaxed); }

bool enabled() { return interp_on(); }
int fps() { return g_fps.load(std::memory_order_relaxed); }

static const int g_env_hz = env::DISPLAY_HZ.integer_or(-1);
static std::atomic<int> g_display_hz{0};
static std::atomic<bool> g_present_vsync{true};
int display_hz() { return g_env_hz >= 0 ? g_env_hz : g_display_hz.load(std::memory_order_relaxed); }
bool present_vsync() { return g_present_vsync.load(std::memory_order_relaxed); }
int output_fps() { return pacing::cap_fps(fps(), display_hz(), present_vsync()); }
void set_display_hz(int hz) {
    if (hz < 0) hz = 0;
    const int old = g_display_hz.exchange(hz);
    if (old == hz) return;
    if (g_env_hz >= 0) LOG("[interp] display refresh rate %d Hz (NSMBU_DISPLAY_HZ=%d is used)", hz, g_env_hz);
    else LOG("[interp] display refresh rate %d Hz: frame interpolation draws up to %d fps", hz, pacing::cap_fps(240, hz, present_vsync()));
}
void set_present_vsync(bool on) {
    if (g_present_vsync.exchange(on) != on) LOG("[interp] presentation %s the display's vsync", on ? "waits for" : "does not wait for");
}
int in_between() {
    if (!interp_on()) return 0;
    const int out = output_fps();
    if (out >= 240) return 3;
    if (out >= 165) return 2;
    if (out >= 120) return 1;
    return 0;
}
int frames_per_step() { return in_between() + 1; }
static const char* fps_name(int f) { return f >= 240 ? "240 fps" : f >= 165 ? "165 fps" : f >= 120 ? "120 fps" : "60 fps"; }
void set_enabled(bool v) {
    if (v && fps() <= 60) v = false;
    g_on = v;
    LOG("[interp] frame interpolation %s", v ? fps_name(fps()) : "off");
}
void set_fps(int f) {
    f = valid_fps(f);
    if (g_fps.exchange(f) != f && interp_on()) LOG("[interp] frame interpolation at %s", fps_name(f));
}
int mode() { return interp_on() && fps() > 60 ? 1 : 0; }
void set_mode(int m) {
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
    if (mode() != 1) return "60 fps";
    const int f = fps(), out = output_fps();
    if (out >= f) return fps_name(f);
    if (f >= 240 && out >= 165) return "240 fps (165 shown)";
    if (f >= 240 && out >= 120) return "240 fps (120 shown)";
    if (f >= 240) return "240 fps (60 shown)";
    if (f >= 165 && out >= 120) return "165 fps (120 shown)";
    if (f >= 165) return "165 fps (60 shown)";
    return "120 fps (60 shown)";
}

uint32_t effective_swap_interval(uint32_t game) { return game; }
int vsync_rate() { return 1; }

namespace {
constexpr uint32_t kEye = 0xDC, kCenter = 0xE8, kUp = 0xF4, kFovy = 0xD4, kBank = 0x100;

struct CamState {
    float eye[3], center[3], up[3], fovy;
    int16_t bank;
};

CamState read_cam(uint32_t cam) {
    CamState s;
    for (int i = 0; i < 3; i++) {
        s.eye[i] = (float)ldf32(cam + kEye + 4 * i);
        s.center[i] = (float)ldf32(cam + kCenter + 4 * i);
        s.up[i] = (float)ldf32(cam + kUp + 4 * i);
    }
    s.fovy = (float)ldf32(cam + kFovy);
    s.bank = (int16_t)ld16(cam + kBank);
    return s;
}

void write_cam(uint32_t cam, const CamState& s) {
    for (int i = 0; i < 3; i++) {
        stf32(cam + kEye + 4 * i, s.eye[i]);
        stf32(cam + kCenter + 4 * i, s.center[i]);
        stf32(cam + kUp + 4 * i, s.up[i]);
    }
    stf32(cam + kFovy, s.fovy);
    st16(cam + kBank, (uint16_t)s.bank);
}

float dist(const float* a, const float* b) {
    float d = 0;
    for (int i = 0; i < 3; i++) d += (a[i] - b[i]) * (a[i] - b[i]);
    return std::sqrt(d);
}

using pacing::lerp_f;
using pacing::lerp_s16;

float g_last_step = 0;

bool cam_snap(const CamState& a, const CamState& b) {
    static const float kCut = (float)env::INTERP_CUT.real_or(800.0f);
    float step = std::max(dist(a.eye, b.eye), dist(a.center, b.center));
    float prev = g_last_step;
    g_last_step = step;
    return step > kCut || std::fabs(a.fovy - b.fovy) > 20.0f || (step > 60.0f && step > 4.0f * prev + 20.0f);
}

CamState blend(const CamState& a, const CamState& b, float t) {
    CamState m;
    for (int i = 0; i < 3; i++) {
        m.center[i] = lerp_f(a.center[i], b.center[i], t);
        m.up[i] = lerp_f(a.up[i], b.up[i], t);
    }

    float da[3], db[3], la = 0, lb = 0;
    for (int i = 0; i < 3; i++) {
        da[i] = a.eye[i] - a.center[i];
        db[i] = b.eye[i] - b.center[i];
        la += da[i] * da[i];
        lb += db[i] * db[i];
    }
    la = std::sqrt(la);
    lb = std::sqrt(lb);
    if (la > 1e-3f && lb > 1e-3f) {
        float cosang = 0;
        for (int i = 0; i < 3; i++) cosang += (da[i] / la) * (db[i] / lb);
        if (cosang < 0.7071f) return b;

        if (std::fabs(la - lb) > 0.1f * std::max(la, lb)) return b;
        float dir[3], ld = 0;

        float wa, wb;
        pacing::slerp_weights(cosang, t, wa, wb);
        for (int i = 0; i < 3; i++) {
            dir[i] = wa * da[i] / la + wb * db[i] / lb;
            ld += dir[i] * dir[i];
        }
        ld = std::sqrt(ld);
        float len = lerp_f(la, lb, t);
        for (int i = 0; i < 3; i++) m.eye[i] = m.center[i] + (ld > 1e-3f ? dir[i] / ld : db[i] / lb) * len;
    } else {
        for (int i = 0; i < 3; i++) m.eye[i] = lerp_f(a.eye[i], b.eye[i], t);
    }

    float fwd[3], lf = 0, lu = 0, d = 0;
    for (int i = 0; i < 3; i++) { fwd[i] = m.center[i] - m.eye[i]; lf += fwd[i] * fwd[i]; }
    lf = std::sqrt(lf);
    if (lf > 1e-3f) {
        for (int i = 0; i < 3; i++) { fwd[i] /= lf; d += m.up[i] * fwd[i]; }
        for (int i = 0; i < 3; i++) { m.up[i] -= d * fwd[i]; lu += m.up[i] * m.up[i]; }
        lu = std::sqrt(lu);
        if (lu > 0.1f) {
            for (int i = 0; i < 3; i++) m.up[i] /= lu;
        } else {
            for (int i = 0; i < 3; i++) m.up[i] = b.up[i];
        }
    }
    m.fovy = lerp_f(a.fovy, b.fovy, t);
    m.bank = lerp_s16(a.bank, b.bank, t);
    return m;
}

std::atomic<uint64_t> g_executed_steps{0};
uint64_t g_logic_steps = 0;
uint64_t g_passes = 0;
bool g_hold = false;
bool g_cam_blended = false;
bool g_logic_pass = false;
bool g_blend_draw = false;
bool g_hold_next = false;
bool g_hold_frame = false;

int g_step_n = 1;
int g_phase = 0;
uint64_t g_record_passes = 0;

bool g_exact_step = false;

bool blended_hold() { return g_hold && g_phase < g_step_n; }

struct Prev {
    uint32_t cam = 0; CamState s{}; bool valid = false; bool snap = true; uint64_t snap_step = ~0ull;
    CamState drawn{};
    uint64_t drawn_pass = ~0ull;
};
Prev g_prev[4];

Prev* prev_for(uint32_t cam) {
    for (auto& p : g_prev)
        if (p.cam == cam) return &p;
    for (auto& p : g_prev)
        if (!p.valid) { p.cam = cam; return &p; }
    g_prev[0] = Prev{cam};
    return &g_prev[0];
}
}

float pass_t() {
    if (!enabled()) return 1.0f;
    return pacing::pass_fraction(g_hold ? g_phase : 0, g_step_n, g_exact_step);
}

bool record_pass() { return g_hold && g_phase >= g_step_n; }

}

static void cam_trace(uint32_t cam, const char* what) {
    static int left = env::INTERP_CAM_TRACE.integer_or(0);
    if (left <= 0 || !interp::enabled()) return;
    left--;
    interp::CamState s = interp::read_cam(cam);
    LOG("[interp] camera %08X step %llu t %.3f %-7s eye %.3f %.3f %.3f center %.3f %.3f %.3f", cam, (unsigned long long)interp::g_logic_steps,
        interp::pass_t(), what, s.eye[0], s.eye[1], s.eye[2], s.center[0], s.center[1], s.center[2]);
}

extern "C" void hook_024FFC40(Cpu* c) {
    using namespace interp;
    uint32_t cam = c->r[3];
    Prev* p = prev_for(cam);
    if (g_blend_draw) {
        CamState cur = read_cam(cam);
        if (p->valid) {
            if (!g_hold) {
                p->snap = cam_snap(p->s, cur);
                p->snap_step = g_logic_steps;
            }
            if (p->snap_step == g_logic_steps && !p->snap) write_cam(cam, blend(p->s, cur, pass_t()));
        }
        p->drawn = read_cam(cam);
        p->drawn_pass = g_passes;

        g_cam_blended = p->valid;
        cam_trace(cam, "blended");
        f_024FFC40_orig(c);
        g_cam_blended = false;
        write_cam(cam, cur);
        return;
    }
    p->s = read_cam(cam);
    p->valid = true;
    p->drawn = p->s;
    p->drawn_pass = g_passes;
    cam_trace(cam, "exact");
    f_024FFC40_orig(c);
}

namespace interp {
float pass_t();
namespace {
constexpr uint32_t kMdlJoints = 0x2C, kJntMtx = 0x10, kJntNum = 0x2C;
constexpr int kMaxJoints = 1024;

struct ModelPrev {
    uint32_t mtx = 0;
    uint64_t pass = 0;
    std::vector<uint32_t> w;
};
std::unordered_map<uint32_t, ModelPrev> g_models;
struct ModelStats { uint32_t blended = 0, fresh = 0, cut = 0; } g_mstats;

std::mutex g_ubo_mu;
std::unordered_map<uint32_t, uint32_t> g_ubo_done;

float wf(uint32_t w) { return u32_as_f32(__builtin_bswap32(w)); }
uint32_t fw(float f) { return __builtin_bswap32(f32_as_u32(f)); }

void trace_model(const char* what, uint32_t jnt, uint32_t n, const uint32_t* w) {
    static int left = env::INTERP_MODEL_TRACE.integer_or(0);
    static const uint32_t min_joints = env::INTERP_MODEL_TRACE_JOINTS.integer_or(40);
    static uint32_t which = 0;
    if (left <= 0 || n < min_joints || (which && which != jnt)) return;
    which = jnt;
    left--;
    LOG("[interp] model %08X (%u joints, mtx %08X) step %llu t %.3f %-7s root %.2f %.2f %.2f", jnt, n, ld32(jnt + kJntMtx),
        (unsigned long long)g_logic_steps, pass_t(), what, wf(w[3]), wf(w[7]), wf(w[11]));
}

bool to_quat(const float* m, float* s, float* q) {
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

void blend_mtx(const float* a, const float* b, float* out, float t) {
    float sa[3], sb[3], qa[4], qb[4];
    for (int i = 0; i < 3; i++) out[4 * i + 3] = lerp_f(a[4 * i + 3], b[4 * i + 3], t);
    if (!to_quat(a, sa, qa) || !to_quat(b, sb, qb)) {
        for (int i = 0; i < 3; i++)
            for (int j = 0; j < 3; j++) out[4 * i + j] = lerp_f(a[4 * i + j], b[4 * i + j], t);
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
        float s = lerp_f(sa[j], sb[j], t);
        for (int i = 0; i < 3; i++) out[4 * i + j] = r[i][j] * s;
    }
}
}
}

extern "C" void hook_027F55FC(Cpu* c) {
    using namespace interp;
    static const bool off = env::INTERP_MODELS() && !atoi(env::INTERP_MODELS());
    if (!enabled() || off || (!g_hold && g_exact_step)) {
        f_027F55FC_orig(c);
        return;
    }
    const bool record = record_pass();
    uint32_t model = c->r[3];
    uint32_t jnt = ld32(model + kMdlJoints);
    uint32_t mtx = jnt ? ld32(jnt + kJntMtx) : 0;
    uint32_t n = jnt ? ld16(jnt + kJntNum) : 0;
    if (!mtx || n == 0 || n > kMaxJoints) {
        f_027F55FC_orig(c);
        return;
    }
    const uint32_t words = 12 * n;
    const uint32_t* cur = (const uint32_t*)ppc_ptr(mtx);
    if (record) {
        ModelPrev& p = g_models[jnt];
        p.mtx = mtx;
        p.pass = g_record_passes;
        p.w.assign(cur, cur + words);
        trace_model("exact", jnt, n, cur);
        f_027F55FC_orig(c);
        return;
    }

    auto it = g_models.find(jnt);
    if (it == g_models.end() || it->second.mtx != mtx || it->second.w.size() != words ||
        it->second.pass + 1 < g_record_passes) {
        g_mstats.fresh++;
        trace_model("new", jnt, n, cur);
        f_027F55FC_orig(c);
        return;
    }
    const std::vector<uint32_t>& prev = it->second.w;
    if (memcmp(prev.data(), cur, 4 * words) == 0) {
        trace_model("still", jnt, n, cur);
        f_027F55FC_orig(c);
        return;
    }
    static const float kCut = (float)env::INTERP_MODEL_CUT.real_or(400.0f);
    const float t = pass_t();
    std::vector<uint32_t> saved(cur, cur + words);
    std::vector<uint32_t> mid(words);
    for (uint32_t m = 0; m < n; m++) {
        float a[12], b[12], o[12];
        for (int k = 0; k < 12; k++) { a[k] = wf(prev[12 * m + k]); b[k] = wf(saved[12 * m + k]); }
        float dx = a[3] - b[3], dy = a[7] - b[7], dz = a[11] - b[11];
        if (!(dx * dx + dy * dy + dz * dz <= kCut * kCut)) {
            g_mstats.cut++;
            trace_model("cut", jnt, n, cur);
            f_027F55FC_orig(c);
            return;
        }
        blend_mtx(a, b, o, t);
        for (int k = 0; k < 12; k++) mid[12 * m + k] = fw(o[k]);
    }
    g_mstats.blended++;
    trace_model("blended", jnt, n, mid.data());
    {
        std::lock_guard<std::mutex> lk(g_ubo_mu);
        g_ubo_done[model]++;
    }
    memcpy(ppc_ptr(mtx), mid.data(), 4 * words);
    f_027F55FC_orig(c);
    const uint32_t r3 = c->r[3];
    c->r[3] = model;
    f_027F5018_orig(c);
    c->r[3] = r3;
    memcpy(ppc_ptr(mtx), saved.data(), 4 * words);
}

extern "C" void hook_027F5018(Cpu* c) {
    using namespace interp;
    const uint32_t model = c->r[3];
    {
        std::lock_guard<std::mutex> lk(g_ubo_mu);
        if (!enabled()) g_ubo_done.clear();
        auto it = g_ubo_done.find(model);
        if (it != g_ubo_done.end()) {
            if (--it->second == 0) g_ubo_done.erase(it);
            return;
        }
    }
    f_027F5018_orig(c);
}

namespace interp {
void fx_pass_start() {}
void fx_hold_blend(float) {}
void fx_ss_reset() {}

void ss_reset() {
    for (auto& p : g_prev) p = Prev{};
    g_last_step = 0;
    g_models.clear();
    {
        std::lock_guard<std::mutex> lk(g_ubo_mu);
        g_ubo_done.clear();
    }
    g_record_passes += 8;
    g_hold_next = false;
    g_phase = 0;
    g_step_n = 1;
    fx_ss_reset();
}

static const char* const g_env_paced = env::INTERP_PACED();
static std::atomic<bool> g_paced{[] {
#ifdef __ANDROID__
    return !g_env_paced || atoi(g_env_paced) != 0;
#else
    return g_env_paced && atoi(g_env_paced) != 0;
#endif
}()};
static std::atomic<bool> g_paced_hi{!g_env_paced || atoi(g_env_paced) != 0};
static std::atomic<bool>& paced_flag(int f) { return f > 60 ? g_paced_hi : g_paced; }
static bool paced() { return paced_flag(fps()).load(std::memory_order_relaxed); }
bool paced_interpolation() { return paced(); }
bool paced_interpolation_at(int f) { return paced_flag(f).load(std::memory_order_relaxed); }
void set_paced_interpolation_at(int f, bool on) {
    if (paced_flag(f).exchange(on) != on)
        LOG("[interp] paced interpolation at %s %s", f > 60 ? "120/240 fps" : "60 fps", on ? "on (keeps the game's speed)" : "off");
}
void set_paced_interpolation(bool on) { set_paced_interpolation_at(fps(), on); }

static std::atomic<float> g_paced_share{-1};
float paced_drawn_share() { return paced() && interp_on() ? g_paced_share.load(std::memory_order_relaxed) : -1; }

using pace_clock = std::chrono::steady_clock;
static pace_clock::time_point g_last_logic{}, g_last_entry{};

static pace_clock::duration g_slept{}, g_logic_avg = std::chrono::milliseconds(16), g_hold_avg = std::chrono::milliseconds(16);
static pace_clock::time_point g_last_hold{};
static bool g_probe = false;
constexpr auto kProbeAfter = std::chrono::seconds(10);

static pace_clock::duration vsync_tick() {
    return std::chrono::nanoseconds(16'683'333LL * 2 / std::max(2, frames_per_step()));
}

static bool g_pass_was_hold = false;
static bool g_wait_step = false;
static uint64_t g_paced_possible = 0, g_paced_holds = 0, g_paced_steps = 0, g_paced_planned = 0;
constexpr auto kPacedStep = std::chrono::nanoseconds(33'333'333);

constexpr auto kPacedBudget = std::chrono::nanoseconds(kPacedStep + std::chrono::milliseconds(2));

static void paced_pass_start() {
    static bool previousRecord = false;
    if (!paced() || !interp_on()) { g_exact_step = false; previousRecord = false; return; }
    if (!g_hold_next) g_exact_step = !previousRecord;
    previousRecord = g_hold_next && g_phase + 1 >= g_step_n;
    const auto now = pace_clock::now();
    if (g_last_entry != pace_clock::time_point{}) {
        const auto pass = now - g_last_entry - g_slept;
        pace_clock::duration& avg = g_pass_was_hold ? g_hold_avg : g_logic_avg;

        avg = g_pass_was_hold && g_probe ? pass : pace_clock::duration(pacing::update_average(avg.count(), pass.count()));
        if (g_pass_was_hold) g_probe = false;
    }
    g_pass_was_hold = g_hold_next;
    g_last_entry = now;
    g_slept = {};
    if (g_hold_next) return;
    if (g_wait_step && now < g_last_logic + kPacedStep) {
        threads::park_sleep_until(g_last_logic + kPacedStep);
        g_slept = pace_clock::now() - now;
    }
    g_wait_step = false;
    g_last_logic = pace_clock::now();
}

static int g_step_holds = 0;
static int plan_step() {
    int n = std::max(1, in_between());
    if (n > 1 && paced() && interp_on()) {
        n = pacing::plan_in_between(n, kPacedBudget.count(), std::chrono::duration_cast<std::chrono::nanoseconds>(g_logic_avg).count(),
                                    std::chrono::duration_cast<std::chrono::nanoseconds>(g_hold_avg).count());
        g_paced_planned += n;
    }
    if (g_exact_step) n = 1;
    g_step_holds = 0;
    return n;
}

static void paced_step_done(int holds) {
    const int n = std::max(1, in_between());
    g_paced_holds += holds;
    g_paced_possible += n;
    g_paced_steps++;
    static unsigned recentHolds = 0, recentPossible = 0, recentN = 0;
    recentHolds += holds;
    recentPossible += n;
    if (++recentN == 60) {
        g_paced_share.store(recentHolds / float(recentPossible), std::memory_order_relaxed);
        recentHolds = recentPossible = recentN = 0;
    }
    if (g_paced_steps >= 300) {
        if (n > 1)
            LOG("[interp] paced: %.0f%% of in-between frames drawn (in-between pass %.1f ms; %.2f of %d planned per step)",
                100.0 * g_paced_holds / double(g_paced_possible), std::chrono::duration<double, std::milli>(g_hold_avg).count(),
                g_paced_planned / double(g_paced_steps), n);
        else
            LOG("[interp] paced: %.0f%% of in-between frames drawn (in-between pass %.1f ms)", 100.0 * g_paced_holds / double(g_paced_possible),
                std::chrono::duration<double, std::milli>(g_hold_avg).count());
        g_paced_possible = g_paced_holds = g_paced_steps = g_paced_planned = 0;
    }
}

static void after_pass(int phase) {
    const bool pacing = paced() && interp_on();
    if (phase > 0) g_step_holds++;
    if (phase >= g_step_n) {
        g_hold_next = false;
        if (pacing) {
            paced_step_done(g_step_holds);
            if (in_between() > 1) g_wait_step = true;
        }
        return;
    }
    if (!pacing) { g_hold_next = true; g_wait_step = false; return; }
    if (phase > 0) g_last_hold = pace_clock::now();
    const auto now = pace_clock::now();
    const auto elapsed = std::chrono::duration_cast<std::chrono::nanoseconds>(now - g_last_logic).count();

    const bool probe = phase == 0 && now - g_last_hold >= kProbeAfter;
    const auto pass = std::chrono::duration_cast<std::chrono::nanoseconds>(probe ? vsync_tick() : g_hold_avg).count();
    switch (pacing::next_pass(phase, g_step_n, elapsed, pass, kPacedBudget.count())) {
    case pacing::Next::kHold:
        g_hold_next = true;
        g_probe = probe;
        g_last_hold = now;
        break;
    case pacing::Next::kRecord:
        g_hold_next = true;
        g_phase = g_step_n - 1;
        g_last_hold = now;
        break;
    default:
        g_hold_next = false;
        g_wait_step = true;
        paced_step_done(g_step_holds);
        break;
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

extern "C" void hook_0203593C(Cpu* c) {
    using namespace interp;
    fx_pass_start();
    ss::service(c);
    g_passes++;

    static uint64_t passes = 0;
    static const uint64_t at = env::INTERP_AT_STEP.u64_or(0);
    if (at && ++passes == at) set_enabled(true);
    paced_pass_start();
    if (!enabled() || !g_hold_next) {
        g_logic_steps++;
    }
    if (!enabled()) {
        g_hold_next = false;
        g_phase = 0;
        g_step_n = 1;
        {
            PassTimer timer(0);
            f_0203593C_orig(c);
        }
        static unsigned stats_steps = 0;
        if (pass_stats() && ++stats_steps % 300 == 0) LOG("[interp]%s", pass_cpu_report().c_str());
        return;
    }
    static uint64_t frames = 0;
    frames++;
    {
        std::lock_guard<std::mutex> lk(g_ubo_mu);
        g_ubo_done.clear();
    }
    if (g_hold_next) {

        g_phase++;
        g_hold = true;
        g_hold_frame = true;
        PassTimer timer(record_pass() ? 2 : 1);
        if (record_pass()) {
            g_record_passes++;
        } else {
            fx_hold_blend(pass_t());
        }
        f_0203593C_orig(c);
        g_hold_frame = false;
        after_pass(g_phase);
        g_hold = false;
        return;
    }
    g_phase = 0;
    g_step_n = plan_step();
    {
        PassTimer timer(0);
        f_0203593C_orig(c);
    }
    after_pass(0);
    static uint64_t n = 0, t0 = timebase::now(), f0 = 0;
    if (++n % 300 == 0) {
        uint64_t t = timebase::now();
        LOG("[interp] %.1f logic steps/s (%s, %.2f frames per step); models per step: %.1f blended, %.1f new, %.1f cut%s",
            300.0 * timebase::kTicksPerSec / (double)(t - t0), mode_name(), (frames - f0) / 300.0, g_mstats.blended / 300.0,
            g_mstats.fresh / 300.0, g_mstats.cut / 300.0, pass_cpu_report().c_str());
        t0 = t;
        f0 = frames;
        g_mstats.blended = g_mstats.fresh = g_mstats.cut = 0;
        for (auto it = g_models.begin(); it != g_models.end();)
            it = it->second.pass + 2 < g_record_passes ? g_models.erase(it) : std::next(it);
    }
}

extern "C" void hook_025F172C(Cpu* c) {
    using namespace interp;
    if (g_hold_frame) {
        g_blend_draw = blended_hold();
        f_025D42EC(c);
        g_blend_draw = false;
        return;
    }
    g_logic_pass = enabled() && !g_exact_step;
    g_blend_draw = g_logic_pass;
    f_025F172C_orig(c);
    g_logic_pass = false;
    g_blend_draw = false;
}

static Cpu* g_hold_child_cpu = nullptr;
static bool called_from_frame_function() {

    static const uint32_t lo = GC(0x0203593C), hi = GC(0x02035A78);
    uint32_t lr = g_hold_child_cpu ? g_hold_child_cpu->lr : 0;
    return lr > lo && lr < hi;
}
static bool hold_skip_child(int i) {

    static const uint32_t run = (uint32_t)env::HOLD_RUN.ul_or(0x7FCC, 0) | 1;
    return interp::g_hold_frame && !(run & (1u << i)) && called_from_frame_function();
}
extern "C" void hook_02032FE8(Cpu* c) { g_hold_child_cpu = c; if (!hold_skip_child(0)) f_02032FE8_orig(c); }
extern "C" void hook_020357CC(Cpu* c) { g_hold_child_cpu = c; if (!hold_skip_child(1)) f_020357CC_orig(c); }
extern "C" void hook_025EE048(Cpu* c) { g_hold_child_cpu = c; if (!hold_skip_child(2)) f_025EE048_orig(c); }
extern "C" void hook_02756170(Cpu* c) { g_hold_child_cpu = c; if (!hold_skip_child(3)) f_02756170_orig(c); }
extern "C" void hook_02617AF4(Cpu* c) { g_hold_child_cpu = c; if (!hold_skip_child(4)) f_02617AF4_orig(c); }
extern "C" void hook_02614F74(Cpu* c) { g_hold_child_cpu = c; if (!hold_skip_child(5)) f_02614F74_orig(c); }
extern "C" void hook_0273841C(Cpu* c) { g_hold_child_cpu = c; if (!hold_skip_child(6)) f_0273841C_orig(c); }
extern "C" void hook_0270870C(Cpu* c) { g_hold_child_cpu = c; if (!hold_skip_child(7)) f_0270870C_orig(c); }
namespace interp { void light_trace_add(const char*, ...) {} }

namespace {
const uint32_t kSetLightTarget = GD(0x101E8EC8), kSetLightEfTarget = GD(0x101E8ECC), kLightStatusPt = GD(0x101E8CC8);
struct SetLightState {
    uint64_t step = ~0ull;
    uint32_t target, ef_target, status, pos[3];
} g_setlight;
}
extern "C" void hook_0255E854(Cpu* c) {
    g_hold_child_cpu = c;
    if (hold_skip_child(8)) return;
    if (interp::enabled() && called_from_frame_function()) {
        SetLightState& s = g_setlight;
        uint32_t st = ld32(kLightStatusPt);
        if (!interp::g_hold_frame) {
            s.step = interp::g_logic_steps;
            s.target = ld32(kSetLightTarget);
            s.ef_target = ld32(kSetLightEfTarget);
            s.status = st;
            for (int i = 0; i < 3; i++) s.pos[i] = st ? ld32(st + 4 * i) : 0;
        } else if (s.step == interp::g_logic_steps && s.status == st) {
            st32(kSetLightTarget, s.target);
            st32(kSetLightEfTarget, s.ef_target);
            for (int i = 0; i < 3 && st; i++) st32(st + 4 * i, s.pos[i]);
        }
    }

    f_0255E854_orig(c);
    uint32_t st = ld32(GD(0x101E8CC8));
    interp::light_trace_add(" setLight t%.2f r%u p(%.1f,%.1f,%.1f)", ldf32(GD(0x101E8EC8)), st ? ld8(st + 0x18) : 0, st ? ldf32(st) : 0.0,
                            st ? ldf32(st + 4) : 0.0, st ? ldf32(st + 8) : 0.0);
}
extern "C" void hook_02738438(Cpu* c) {
    g_hold_child_cpu = c;
    if (hold_skip_child(9)) return;
    uint32_t o = c->r[3];
    interp::light_trace_add(" hdl[%08X %08X %08X %08X]", ld32(o + 0x10), ld32(o + 0x14), ld32(o + 0x18), ld32(o + 0x1C));
    f_02738438_orig(c);
}
extern "C" void hook_0278FEBC(Cpu* c) { g_hold_child_cpu = c; if (!hold_skip_child(10)) f_0278FEBC_orig(c); }
extern "C" void hook_027199C0(Cpu* c) { g_hold_child_cpu = c; if (!hold_skip_child(11)) f_027199C0_orig(c); }
extern "C" void hook_02618940(Cpu* c) { g_hold_child_cpu = c; if (!hold_skip_child(12)) f_02618940_orig(c); }
extern "C" void hook_02728A74(Cpu* c) { g_hold_child_cpu = c; if (!hold_skip_child(13)) f_02728A74_orig(c); }
extern "C" void hook_02039834(Cpu* c) { g_hold_child_cpu = c; if (!hold_skip_child(14)) f_02039834_orig(c); }

static bool skip(int bit) {
    static const int run = env::INTERP_RUN.integer_or(0);
    return interp::g_hold && !(run & bit);
}
static bool g_in_execute = false;
extern "C" void hook_025DE788(Cpu* c) {
    if (skip(1)) return;
    interp::record_executed_step();
    g_in_execute = true;
    f_025DE788_orig(c);
    if (!interp::g_hold_frame) {
        mods::lua_sdk::set_cpu(c);
        mods::packages::frame(interp::g_logic_steps);
    }
    g_in_execute = false;
}
extern "C" void hook_025DE024(Cpu* c) { if (!skip(2)) f_025DE024_orig(c); }
extern "C" void hook_025E0EE4(Cpu* c) { if (skip(4)) c->r[3] = 1; else f_025E0EE4_orig(c); }
extern "C" void hook_025DDCEC(Cpu* c) { if (skip(8)) c->r[3] = 1; else f_025DDCEC_orig(c); }
extern "C" void hook_025D42C4(Cpu* c) { if (!skip(16)) f_025D42C4_orig(c); }
extern "C" void hook_0200E6EC(Cpu* c) { if (!skip(32)) f_0200E6EC_orig(c); }

extern "C" void f_02715310_orig(Cpu* c);
extern "C" void hook_02715310(Cpu* c) { if (!skip(64)) f_02715310_orig(c); }

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

bool logic_pass() { return g_logic_pass; }

bool blend_draw() { return g_blend_draw; }
bool in_execute() { return g_in_execute; }

uint64_t hold_pass_count() { return g_record_passes; }
uint64_t pass_count() { return g_passes; }
const char* phase_name() { return !enabled() ? "interp off" : g_hold ? "IN-BETWEEN" : g_logic_pass ? "logic" : "other"; }

bool repeat_input() {
    static const bool off = env::INTERP_NO_REPEAT() != nullptr;
    return enabled() && g_hold_next && !off;
}
}

static void se_trace(int fn, Cpu* c) {
    static FILE* f = env::SE_TRACE() ? fopen(env::SE_TRACE(), "w") : nullptr;
    if (!f || interp::g_hold) return;
    fprintf(f, "%llu %d %d %08X %08X\n", (unsigned long long)interp::g_logic_steps, interp::g_hold ? 0 : 1, fn, c->r[4], c->lr);
    fflush(f);
}
static void se_stat(int fn) {
    static const bool on = env::SE_STATS() != nullptr;
    if (!on) return;
    static uint32_t n[7][4];
    int ph = !interp::enabled() ? 0 : interp::g_hold ? 1 : interp::g_logic_pass ? 2 : 3;
    n[fn][ph]++;
    static uint64_t t0 = timebase::now();
    uint64_t t = timebase::now();
    if (t - t0 > 5 * timebase::kTicksPerSec) {
        static const char* fns[7] = {"core", "w1988", "w19CC", "w1A04", "w1A40", "w1A7C", "w1AA4"};
        char buf[700];
        int k = snprintf(buf, sizeof buf, "[se] starts per 5 s [off/in-between/logic/other]:");
        for (int f = 0; f < 7; f++)
            if (n[f][0] + n[f][1] + n[f][2] + n[f][3])
                k += snprintf(buf + k, sizeof buf - k, " %s=%u/%u/%u/%u", fns[f], n[f][0], n[f][1], n[f][2], n[f][3]);
        LOG("%s", buf);
        memset(n, 0, sizeof n);
        t0 = t;
    }
}
extern "C" void hook_0201EBA0(Cpu* c) { se_stat(0); se_trace(0, c); if (interp::g_hold) { c->r[3] = 0; return; } f_0201EBA0_orig(c); }
extern "C" void hook_025E1988(Cpu* c) { se_stat(1); se_trace(1, c); if (interp::g_hold) { c->r[3] = 0; return; } f_025E1988_orig(c); }
extern "C" void hook_025E19CC(Cpu* c) { se_stat(2); se_trace(2, c); if (interp::g_hold) { c->r[3] = 0; return; } f_025E19CC_orig(c); }
extern "C" void hook_025E1A04(Cpu* c) { se_stat(3); se_trace(3, c); if (interp::g_hold) { c->r[3] = 0; return; } f_025E1A04_orig(c); }
extern "C" void hook_025E1A40(Cpu* c) { se_stat(4); se_trace(4, c); if (interp::g_hold) { c->r[3] = 0; return; } f_025E1A40_orig(c); }
extern "C" void hook_025E1A7C(Cpu* c) { se_stat(5); se_trace(5, c); if (interp::g_hold) { c->r[3] = 0; return; } f_025E1A7C_orig(c); }
extern "C" void hook_025E1AA4(Cpu* c) { se_stat(6); se_trace(6, c); if (interp::g_hold) { c->r[3] = 0; return; } f_025E1AA4_orig(c); }

extern "C" void hook_025E1B44(Cpu* c) {
    if (interp::g_cam_blended) return;
    if (!interp::enabled()) {
        f_025E1B44_orig(c);
        return;
    }
    const uint32_t eye = mem::fixed_slot(mem::kFixInterpEye), mtx = mem::fixed_slot(mem::kFixInterpMtx);
    for (int i = 0; i < 3; i++) st32(eye + 4 * i, ld32(c->r[3] + 4 * i));
    for (int i = 0; i < 12; i++) st32(mtx + 4 * i, ld32(c->r[4] + 4 * i));
    c->r[3] = eye;
    c->r[4] = mtx;
    f_025E1B44_orig(c);
}
extern "C" void hook_02027754(Cpu* c) { if (!interp::g_cam_blended) f_02027754_orig(c); }

extern "C" void hook_020315CC(Cpu* c) {
    using namespace interp;
    static uint64_t done_for = ~0ull;
    if (enabled()) {
        if (done_for == g_logic_steps) return;
        done_for = g_logic_steps;
    }
    f_020315CC_orig(c);
}

extern "C" void f_02574144_orig(Cpu* c);
extern "C" void hook_02574144(Cpu* c) {
    using namespace interp;
    if (!enabled()) {
        f_02574144_orig(c);
        return;
    }
    CamState exact[4];
    bool swapped[4] = {};
    for (int i = 0; i < 4; i++) {
        Prev& p = g_prev[i];
        if (!p.cam || p.drawn_pass != g_passes) continue;
        exact[i] = read_cam(p.cam);
        write_cam(p.cam, p.drawn);
        swapped[i] = true;
    }
    f_02574144_orig(c);
    for (int i = 3; i >= 0; i--)
        if (swapped[i]) write_cam(g_prev[i].cam, exact[i]);
}
