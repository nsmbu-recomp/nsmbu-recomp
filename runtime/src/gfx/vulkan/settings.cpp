#include "settings.h"
#include "gfx/res_presets.h"
#include "core/runtime.h"
#include <atomic>
#include <cstdlib>
#include <cstring>
#include "hooks/interp.h"
#include "hooks/interp_pacing.h"
#include "gx2/gx2.h"
#include "core/env.h"
namespace gfxvk {
namespace {
int normalize(int v) { return (v % 3 + 3) % 3; }
struct Settings {
    std::atomic<bool> aniso{env::ANISO.enabled()};
    std::atomic<bool> fxaa{env::FXAA.enabled()};
    std::atomic<int> filter{[] { const char* e = env::SCALE_FILTER(); return e && !std::strcmp(e,"sharp") ? 1 : e && !std::strcmp(e,"integer") ? 2 : 0; }()};
    std::atomic<bool> available[static_cast<int>(GraphicsFeature::Count)]{};
};
Settings& settings() { static Settings s; return s; }
int env_present_mode() {
    const char* e = env::VK_PRESENT_MODE();
    if (!e || !*e) return -1;
    return !std::strcmp(e, "mailbox") ? kPresentMailbox : !std::strcmp(e, "immediate") ? kPresentImmediate : kPresentFifo;
}
std::atomic<int> g_present{env_present_mode()};
std::atomic<bool> g_present_user{env_present_mode() >= 0};
std::atomic<unsigned> g_offered{1u << kPresentFifo};
int automatic_present_mode() {
    const unsigned offered = g_offered.load(std::memory_order_relaxed);
    if (offered & (1u << kPresentMailbox)) return kPresentMailbox;
    if (offered & (1u << kPresentImmediate)) return kPresentImmediate;
    return kPresentFifo;
}
}
int present_mode() {
    const int m = g_present.load(std::memory_order_relaxed);
    return m >= 0 ? m : automatic_present_mode();
}
void set_present_mode(int m) {
    if (m < 0 || m >= kPresentModes) m = kPresentFifo;
    g_present_user.store(true, std::memory_order_relaxed);
    if (g_present.exchange(m) != m) LOG("[vulkan] present mode %s requested", present_mode_name(m));
}
bool present_mode_user_set() { return g_present_user.load(std::memory_order_relaxed); }
bool present_mode_from_env() { return env_present_mode() >= 0; }
bool present_mode_offered(int m) { return m >= 0 && m < kPresentModes && (g_offered.load() >> m & 1); }
void set_present_modes_offered(unsigned mask) { g_offered = mask | 1u << kPresentFifo; }
int effective_present_mode() {
    if (!gx2::uncapped() && !interp::unlocked()) return present_mode();
    return present_mode_offered(kPresentImmediate) ? kPresentImmediate : present_mode_offered(kPresentMailbox) ? kPresentMailbox : kPresentFifo;
}
const char* present_mode_name(int m) { return m == kPresentMailbox ? "mailbox" : m == kPresentImmediate ? "immediate" : "fifo"; }
bool aniso_enabled() { return settings().aniso.load(std::memory_order_relaxed); }
void set_aniso(bool v) { settings().aniso.store(v,std::memory_order_relaxed); LOG("[gfx] anisotropy %s",v?"on":"off"); }
bool fxaa_enabled() { return settings().fxaa.load(std::memory_order_relaxed); }
void set_fxaa(bool v) { settings().fxaa.store(v,std::memory_order_relaxed); LOG("[gfx] FXAA %s",v?"on":"off"); }
int scale_filter() { return settings().filter.load(std::memory_order_relaxed); }
void set_scale_filter(int v) { settings().filter.store(normalize(v),std::memory_order_relaxed); }
bool graphics_feature_available(GraphicsFeature f) { int i=static_cast<int>(f); return i>=0 && i<static_cast<int>(GraphicsFeature::Count) && settings().available[i].load(std::memory_order_relaxed); }
void set_graphics_feature_available(GraphicsFeature f,bool v) { int i=static_cast<int>(f); if(i>=0 && i<static_cast<int>(GraphicsFeature::Count)) settings().available[i].store(v,std::memory_order_relaxed); }
bool graphics_hotkey(char k,bool activate) {
    GraphicsFeature f;
    switch(k) {
    case 'R': if(activate) set_res_scale(gfx::next_res_preset(requested_res_scale())); return true;
    case '6': if(activate) { interp::set_fast_forward(false); interp::set_fps(60); interp::set_mode(0); } return true;
    case 'T': if(activate) { interp::set_fast_forward(false); interp::toggle_fps(120); } return true;
    case '7': if(activate) {
        interp::set_fast_forward(false);
        int n = 0;
        const int* order = interp::pacing::fps_ladder(n);
        const int cur = interp::mode() == 1 ? interp::fps() : 60;
        int next = 120;
        for (int i = 0; i < n; i++) if (order[i] == cur) next = order[(i + 1) % n];
        if (next == 60) interp::set_mode(0);
        else { interp::set_fps(next); interp::set_mode(1); }
        return true;
    } return true;
    case '9': if(activate) {
        if (interp::fast_forward()) { interp::set_fast_forward(false); return true; }
        static int last_rate = 165;
        if (interp::mode() == 1) {
            if (interp::fps() != 60) last_rate = interp::fps();
            interp::set_mode(0);
        } else {
            interp::set_fps(last_rate);
            interp::set_mode(1);
        }
        return true;
    } return true;
    case 'N': f=GraphicsFeature::Anisotropy; break;
    case '8': f=GraphicsFeature::FXAA; break;
    default: return false;
    }
    if(activate && graphics_feature_available(f)) switch(k) {
    case 'N': set_aniso(!aniso_enabled()); break;
    case '8': set_fxaa(!fxaa_enabled()); break;
    }
    return true;
}
}
