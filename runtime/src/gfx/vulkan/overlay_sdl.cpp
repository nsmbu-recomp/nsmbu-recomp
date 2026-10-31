#include "gfx/renderer.h"

#include "core/env.h"
#ifdef NSMBU_SDL_HOST
#include "backend.h"
#include "gfx/aspect.h"
#include "gfx/display_modes.h"
#include "settings.h"
#include "device_choice.h"
#include "input/input.h"
#include "hooks/interp.h"
#include "overlay/hostui.h"
#include "platform/host.h"
#include "platform/perf_hint.h"
#ifdef __ANDROID__
#include "platform/heat_guard.h"
#endif
#include "core/runtime.h"
#include <SDL3/SDL.h>
#include <algorithm>
#include <atomic>
#include <cctype>
#include <cstdio>
#include <cstring>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <map>
#include <mutex>
#include <vector>

namespace hostui {
namespace {
std::mutex g_mu;
std::vector<std::function<void()>> g_posted;
std::map<std::string, std::string> g_values;
bool g_loaded = false;
std::atomic<bool> g_run_in_background{true};
std::atomic<bool> g_app_focused{true};

bool display_usable(SDL_DisplayID id, SDL_Rect& bounds) {
    if (SDL_GetDisplayUsableBounds(id, &bounds)) return true;
    return SDL_GetDisplayBounds(id, &bounds);
}

void clamp_windowed_size(const SDL_Rect& usable, int& wdt, int& hgt) {
    if (wdt > 0 && hgt > 0 && wdt < usable.w - 64 && hgt < usable.h - 64) return;
    wdt = std::min(1920, std::max(1280, usable.w * 2 / 3));
    hgt = std::min(1080, std::max(720, usable.h * 2 / 3));
    if (wdt > usable.w - 64) wdt = std::max(640, usable.w - 64);
    if (hgt > usable.h - 64) hgt = std::max(360, usable.h - 64);
}

void ensure_windowed_frame(SDL_Window* w) {
    SDL_Rect usable{};
    if (!display_usable(SDL_GetDisplayForWindow(w), usable)) return;
    int wdt = 0, hgt = 0;
    if (!SDL_GetWindowSize(w, &wdt, &hgt)) return;
    const int prev_w = wdt, prev_h = hgt;
    clamp_windowed_size(usable, wdt, hgt);
    if (wdt == prev_w && hgt == prev_h) return;
    const int x = usable.x + (usable.w - wdt) / 2;
    const int y = usable.y + (usable.h - hgt) / 2;
    SDL_SetWindowSize(w, wdt, hgt);
    SDL_SetWindowPosition(w, x, y);
    char bw[16], bh[16];
    snprintf(bw, sizeof bw, "%d", wdt);
    snprintf(bh, sizeof bh, "%d", hgt);
    set("windowWidth", bw);
    set("windowHeight", bh);
}

void apply_background_hint(bool on) {
#ifndef __ANDROID__
    SDL_SetHint(SDL_HINT_JOYSTICK_ALLOW_BACKGROUND_EVENTS, on ? "1" : "0");
#else
    (void)on;
#endif
}

std::string path() {
    if (const char* e = env::SETTINGS()) return e;
    if (env::NO_HOST_INPUT()) return {};
    return host::config_dir() + "/settings.ini";
}
void load_locked() {
    if (g_loaded) return;
    g_loaded = true;
    std::string p = path();
    if (p.empty()) return;
    std::ifstream in(p);
    std::string line;
    while (std::getline(in, line)) {
        if (line.empty() || line[0] == '#') continue;
        size_t eq = line.find('=');
        if (eq != std::string::npos) g_values[line.substr(0, eq)] = line.substr(eq + 1);
    }
}
void save_locked() {
    std::string p = path();
    if (p.empty()) return;
    std::error_code ec;
    std::filesystem::create_directories(std::filesystem::path(p).parent_path(), ec);
    std::string tmp = p + ".tmp";
    {
        std::ofstream out(tmp, std::ios::trunc);
        out << "# New Super Mario Bros. U settings (settings overlay, ` or F12)\n";
        for (auto& [k, v] : g_values) out << k << '=' << v << '\n';
        if (!out) return;
    }
    if (!host::replace_file(tmp, p)) LOG("[settings] cannot write %s", p.c_str());
}
bool env_set(std::initializer_list<env::Var> vars) {
    for (env::Var v : vars)
        if ((v)()) return true;
    return false;
}

void save_display_locked() {
    using namespace gfx;
    if (!env_set({env::DRC_MODE})) g_values["drcMode"] = kModeNames[display_saved_mode()];
    if (!env_set({env::DRC_PIP})) {
        char v[16];
        g_values["pipCorner"] = kCornerNames[g_corner];
        snprintf(v, sizeof v, "%g", g_pip_size.load());
        g_values["pipSize"] = v;
        snprintf(v, sizeof v, "%g", g_pip_opacity.load());
        g_values["pipOpacity"] = v;
    }
    save_locked();
}
void save_display() {
    std::lock_guard<std::mutex> lk(g_mu);
    load_locked();
    save_display_locked();
}

void apply_drc_window() {
    SDL_Window* w = gfxvk::R.drc.window;
    if (!w) return;
    const bool want = gfx::drc_window_wanted();
    const bool shown = !(SDL_GetWindowFlags(w) & SDL_WINDOW_HIDDEN);
    if (want && !shown && !env::HIDDEN_WINDOWS()) {
        SDL_ShowWindow(w);
        gfxvk::R.drc.visible = true;
    } else if (!want && shown) {
        if (SDL_GetWindowFlags(w) & SDL_WINDOW_FULLSCREEN) SDL_SetWindowFullscreen(w, false);
        SDL_HideWindow(w);
        gfxvk::R.drc.visible = false;
    }
}
}

void post(std::function<void()> fn) {
    std::lock_guard<std::mutex> lk(g_mu);
    g_posted.push_back(std::move(fn));
}
void run_posted() {
    std::vector<std::function<void()>> fns;
    {
        std::lock_guard<std::mutex> lk(g_mu);
        fns.swap(g_posted);
    }
    for (auto& f : fns) f();
}

bool get(const char* key, std::string& value) {
    std::lock_guard<std::mutex> lk(g_mu);
    load_locked();
    auto it = g_values.find(key);
    if (it == g_values.end()) return false;
    value = it->second;
    return true;
}
void set(const char* key, const std::string& value) {
    std::lock_guard<std::mutex> lk(g_mu);
    load_locked();
    g_values[key] = value;
    save_locked();
}
void reload_settings() {
    std::lock_guard<std::mutex> lk(g_mu);
    g_loaded = false;
    g_values.clear();
    load_locked();
}

float res_scale() { return gfxvk::requested_res_scale(); }
void set_res_scale(float s) { gfxvk::set_res_scale(s); }

void graphics_changed() {
    std::lock_guard<std::mutex> lk(g_mu);
    load_locked();
    auto put = [&](const char* key, std::initializer_list<env::Var> vars, const std::string& v) {
        if (!env_set(vars)) g_values[key] = v;
    };
    char res[16];
    snprintf(res, sizeof res, "%g", gfxvk::requested_res_scale());
    put("resScale", {env::RES_SCALE}, res);
    put("aniso", {env::ANISO}, gfxvk::aniso_enabled() ? "1" : "0");
    put("fxaa", {env::FXAA}, gfxvk::fxaa_enabled() ? "1" : "0");
#ifdef __ANDROID__

    put("fps60", {env::INTERP, env::INTERP_FPS}, interp::mode() == 1 || perf_hint::fps60_chosen() ? "1" : "0");
    put("heatGuard", {env::HEAT_GUARD}, heat_guard::name(heat_guard::to_setting(heat_guard::choice.load())));
#else
    put("fps60", {env::INTERP, env::INTERP_FPS}, std::to_string(interp::mode()));
#endif

    put("fpsUnlocked", {env::INTERP_FPS}, interp::unlocked() ? "1" : "0");
    put("fpsCustom", {env::INTERP_FPS}, interp::custom_on() ? "1" : "0");
    put("interpFps", {env::INTERP_FPS}, std::to_string(interp::custom_on() ? interp::custom_fps() : interp::fps()));
    put("fps60Paced", {env::INTERP_PACED}, interp::paced_interpolation_at(60) ? "1" : "0");
    put("scaleFilter", {env::SCALE_FILTER}, std::to_string(gfxvk::scale_filter()));
    char aspect[32];
    aspect::choice_text(aspect, sizeof aspect);
    put("aspect", {env::ASPECT}, aspect);
    if (!env_set({env::VK_PRESENT_MODE}) && gfxvk::present_mode_user_set()) {
        g_values["vkPresentMode"] = std::to_string(gfxvk::present_mode());
        g_values["vkPresentChosen"] = "1";
    }
    if (!env_set({env::VK_GPU}) && !gfxvk::device_inventory.empty()) {
        const std::string gpu = gfxvk::device_setting();
        g_values["gpu"] = gpu.empty() ? "dedicated" : gpu;
    }
    save_locked();
}

void load_saved_options() {
    std::map<std::string, std::string> v;
    {
        std::lock_guard<std::mutex> lk(g_mu);
        load_locked();
        v = g_values;
    }
    auto saved = [&](const char* key, std::initializer_list<env::Var> vars) { return !env_set(vars) && v.count(key); };
    auto num = [&](const char* key) { return atof(v[key].c_str()); };
    if (saved("resScale", {env::RES_SCALE})) gfxvk::set_res_scale((float)num("resScale"));
    if (saved("aniso", {env::ANISO})) gfxvk::set_aniso(num("aniso") != 0);
    if (saved("fxaa", {env::FXAA})) gfxvk::set_fxaa(num("fxaa") != 0);
    if (saved("fpsCustom", {env::INTERP_FPS}) && num("fpsCustom") != 0 && v.count("interpFps"))
        interp::set_custom_fps((int)num("interpFps"));
    else if (saved("interpFps", {env::INTERP_FPS})) interp::set_fps((int)num("interpFps"));
    if (saved("fps60", {env::INTERP, env::INTERP_FPS})) interp::set_mode((int)num("fps60"));
    if (saved("fps60Paced", {env::INTERP_PACED})) interp::set_paced_interpolation_at(60, num("fps60Paced") != 0);
    if (saved("fpsUnlocked", {env::INTERP_FPS}) && num("fpsUnlocked") != 0) interp::set_unlocked(true);
#ifdef __ANDROID__
    if (saved("heatGuard", {env::HEAT_GUARD})) heat_guard::choice = heat_guard::parse(v["heatGuard"].c_str());
#endif
    if (saved("scaleFilter", {env::SCALE_FILTER})) gfxvk::set_scale_filter((int)num("scaleFilter"));
    if (saved("aspect", {env::ASPECT})) aspect::apply_choice(v["aspect"].c_str());
    if (saved("vkPresentMode", {env::VK_PRESENT_MODE})) {
        const int mode = (int)num("vkPresentMode");
        const bool chosen = v.count("vkPresentChosen") && v["vkPresentChosen"] == "1";
        if (chosen || mode != gfxvk::kPresentFifo) gfxvk::set_present_mode(mode);
    }

    using namespace gfx;
    if (v.count("drcMode")) {
        const int m = find_name(kModeNames, kDrcModeCount, v["drcMode"].c_str(), g_mode);
        if (drc_mode_offered(m)) g_mode = m;
    }
    if (v.count("pipCorner")) g_corner = find_name(kCornerNames, 4, v["pipCorner"].c_str(), g_corner);
    if (v.count("pipSize")) g_pip_size = std::clamp((float)num("pipSize"), 0.1f, 0.5f);
    if (v.count("pipOpacity")) g_pip_opacity = std::clamp((float)num("pipOpacity"), 0.2f, 1.0f);
    display_env_overrides();
    if (!drc_mode_offered(g_mode) && g_mode == kDrcWindow) g_mode = kDrcOff;
    g_shown = !input::pro_controller();
    apply_drc_window();
    LOG("[display] GamePad screen mode: %s%s", kModeNames[g_mode], g_shown ? "" : " (hidden)");
    if (saved("runInBackground", {env::RUN_IN_BACKGROUND}))
        g_run_in_background.store(num("runInBackground") != 0, std::memory_order_relaxed);
    else if (const char* e = env::RUN_IN_BACKGROUND())
        g_run_in_background.store(atoi(e) != 0, std::memory_order_relaxed);
    apply_background_hint(g_run_in_background.load(std::memory_order_relaxed));
#ifndef __ANDROID__

    SDL_Window* tv = gfxvk::R.tv.window;
    if (tv && display_start_fullscreen(v["tvFullScreen"] == "1", SDL_GetWindowFlags(tv) & SDL_WINDOW_HIDDEN))
        set_fullscreen(true);
#endif
}

int scale_filter() { return gfxvk::scale_filter(); }
void set_scale_filter(int f) {
    gfxvk::set_scale_filter(f);
    graphics_changed();
}
bool scale_filter_available() { return gfxvk::graphics_feature_available(gfxvk::GraphicsFeature::ScaleFilter); }

bool fullscreen() {
    return gfxvk::R.tv.window && (SDL_GetWindowFlags(gfxvk::R.tv.window) & SDL_WINDOW_FULLSCREEN) != 0;
}
void set_fullscreen(bool on) {
    if (!gfxvk::R.tv.window) return;
    if (fullscreen() == on) return;
    if (!SDL_SetWindowFullscreen(gfxvk::R.tv.window, on)) {
        LOG("[display] TV window: full screen %s failed: %s", on ? "on" : "off", SDL_GetError());
    } else if (!on) {
        ensure_windowed_frame(gfxvk::R.tv.window);
    }
    tv_fullscreen_changed();
}

bool run_in_background() { return g_run_in_background.load(std::memory_order_relaxed); }
void set_run_in_background(bool on) {
    g_run_in_background.store(on, std::memory_order_relaxed);
    apply_background_hint(on);
    set("runInBackground", on ? "1" : "0");
    LOG("[display] run in background %s", on ? "on" : "off");
}
bool app_focused() { return g_app_focused.load(std::memory_order_relaxed); }
void set_app_focused(bool on) { g_app_focused.store(on, std::memory_order_relaxed); }

bool window_size(int& width, int& height) {
#ifndef __ANDROID__
    std::string sw, sh;
    if (!get("windowWidth", sw) || !get("windowHeight", sh)) return false;
    int w = atoi(sw.c_str()), h = atoi(sh.c_str());
    if (w < 320 || h < 240 || w > 7680 || h > 4320) return false;
    if (SDL_WasInit(SDL_INIT_VIDEO)) {
        SDL_Rect bounds{};
        if (display_usable(SDL_GetPrimaryDisplay(), bounds)) clamp_windowed_size(bounds, w, h);
    }
    width = w;
    height = h;
    return true;
#else
    (void)width;
    (void)height;
    return false;
#endif
}

void window_resized() {
#ifndef __ANDROID__
    SDL_Window* tv = gfxvk::R.tv.window;
    if (!tv || fullscreen() || (SDL_GetWindowFlags(tv) & SDL_WINDOW_HIDDEN)) return;
    int w = 0, h = 0;
    if (!SDL_GetWindowSize(tv, &w, &h) || w < 320 || h < 240) return;
    SDL_Rect bounds{};
    if (display_usable(SDL_GetDisplayForWindow(tv), bounds) &&
        (w >= bounds.w - 64 || h >= bounds.h - 64))
        return;
    char bw[16], bh[16];
    snprintf(bw, sizeof bw, "%d", w);
    snprintf(bh, sizeof bh, "%d", h);
    {
        std::lock_guard<std::mutex> lk(g_mu);
        load_locked();
        if (g_values["windowWidth"] == bw && g_values["windowHeight"] == bh) return;
        g_values["windowWidth"] = bw;
        g_values["windowHeight"] = bh;
        save_locked();
    }
#endif
}

void tv_fullscreen_changed() {
#ifndef __ANDROID__
    if (gfx::display_fullscreen_env() || !gfxvk::R.tv.window) return;
    const std::string on = fullscreen() ? "1" : "0";
    {
        std::lock_guard<std::mutex> lk(g_mu);
        load_locked();
        auto it = g_values.find("tvFullScreen");
        if ((it == g_values.end() ? std::string("0") : it->second) == on) return;
        g_values["tvFullScreen"] = on;
        save_locked();
    }
    LOG("[display] TV window full screen %s: remembered for the next start", on == "1" ? "on" : "off");
#endif
}

int drc_modes() { return gfx::kDrcModeCount; }
bool drc_mode_offered(int m) { return gfx::drc_mode_offered(m); }
int drc_mode() { return gfx::g_mode; }
void set_drc_mode(int m) {
    if (!gfx::drc_mode_offered(m)) return;
    gfx::display_set_mode(m);
    apply_drc_window();
    save_display();
    LOG("[display] GamePad screen mode: %s", gfx::kModeNames[m]);
}
int pip_corner() { return gfx::g_corner; }
void set_pip_corner(int c) {
    gfx::g_corner = std::clamp(c, 0, 3);
    save_display();
}
float pip_size() { return gfx::g_pip_size; }
void set_pip_size(float s) {
    gfx::g_pip_size = std::clamp(s, 0.1f, 0.5f);
    save_display();
}
float pip_opacity() { return gfx::g_pip_opacity; }
void set_pip_opacity(float o) {
    gfx::g_pip_opacity = std::clamp(o, 0.2f, 1.0f);
    save_display();
}
bool drc_available() {
    return gfxvk::R.drc.window != nullptr || gfx::g_mode == gfx::kDrcPip || gfx::g_mode == gfx::kDrcAuto;
}
bool drc_shown() {
    SDL_Window* w = gfxvk::R.drc.window;
    return gfx::drc_screen_shown(w && !(SDL_GetWindowFlags(w) & SDL_WINDOW_HIDDEN));
}

void show_drc(bool on) {
    gfx::display_show_drc(on);
    apply_drc_window();
    LOG("[display] GamePad screen %s (%s)", on ? "shown" : "hidden", gfx::kModeNames[gfx::g_mode]);
}
void toggle_drc() { show_drc(!drc_shown()); }
void drc_window_closed() {
    if (gfx::g_mode == gfx::kDrcWindow) gfx::g_shown = false;
    apply_drc_window();
}
void set_pro_controller(bool on) {

    input::set_pro_controller(on);
    show_drc(!on);
}
const char* name() { return "SDL"; }
void set_clipboard(const std::string& text) { SDL_SetClipboardText(text.c_str()); }
#ifdef __ANDROID__
bool can_open_folder() { return false; }
void open_folder(const std::string&) {}
#else
bool can_open_folder() { return true; }
void open_folder(const std::string& path) {

    std::error_code ec;
    std::string p = std::filesystem::absolute(path, ec).generic_string();
    if (ec) p = path;
    std::string url = "file://";
    if (!p.empty() && p[0] != '/') url += "/";
    for (unsigned char c : p) {
        if (isalnum(c) || strchr("/-_.~:", c)) url += (char)c;
        else {
            char hex[4];
            snprintf(hex, sizeof hex, "%%%02X", c);
            url += hex;
        }
    }
    if (!SDL_OpenURL(url.c_str())) LOG("[overlay] cannot open %s: %s", url.c_str(), SDL_GetError());
}
#endif

}
#endif
