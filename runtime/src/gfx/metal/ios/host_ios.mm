// iOS host hooks: what overlay_appkit.mm, menu.mm, text_input_appkit.mm and mods/mouse.mm provide on the
// Mac. The settings overlay (overlay/overlay.cpp) is the only settings UI on iOS.
#include "core/env.h"
#import <GameController/GameController.h>
#import <UIKit/UIKit.h>

#include <atomic>
#include <cstdlib>
#include <ctime>

#include "core/runtime.h"
#include "gfx/aspect.h"
#include "gfx/display_modes.h"
#include "gfx/renderer.h"
#include "hooks/interp.h"
#include "input/input.h"
#include "platform/host.h"
#include "platform/keycodes.h"
#include "save/savestate.h"
#include "../../../overlay/hostui.h"
#include "../../../overlay/overlay.h"

namespace ax { void start_sound_trace(const char* path, double seconds); }

namespace gfx {
bool host_setting(const char* key, std::string& value);
void set_host_setting(const char* key, const std::string& value);
int display_filter();
void display_set_filter(int f);
int display_drc_mode();
void set_drc_mode(int m);
void display_set_pip(int corner, float size, float opacity);
bool tv_fullscreen();
void display_set_tv_fullscreen(bool on);
bool drc_window_available();
bool drc_window_shown();
void show_drc_window(bool on);
void ios_present_alert(UIAlertController* a);
}

// ---------------------------------------------------------------- graphics preferences (menu.mm on the Mac)
namespace {
const float kResScales[] = {1.0f, 1.5f, 2.0f, 3.0f};
float g_res_shown = 0;
bool g_prefs_loaded = false;
float current_res_scale() { return g_res_shown ? g_res_shown : render::res_scale(); }
void set_res(float f) { g_res_shown = f; render::set_res_scale(f); }
void cycle_res() {
    float cur = current_res_scale();
    size_t n = sizeof kResScales / sizeof *kResScales, i = 0;
    while (i < n && kResScales[i] <= cur + 0.01f) i++;
    set_res(kResScales[i % n]);
}

bool env_set(std::initializer_list<env::Var> vars) {
    for (env::Var v : vars)
        if ((v)()) return true;
    return false;
}
id pref(NSString* key) { return [NSUserDefaults.standardUserDefaults objectForKey:key]; }
void set_pref(NSString* key, id value) { [NSUserDefaults.standardUserDefaults setObject:value forKey:key]; }

void load_prefs() {
    g_prefs_loaded = true;
    if (env::NO_HOST_INPUT()) return;
    auto saved = [&](NSString* key, std::initializer_list<env::Var> vars) { return !env_set(vars) && pref(key) != nil; };
    if (saved(@"resScale", {env::RES_SCALE})) set_res([pref(@"resScale") floatValue]);
    if (saved(@"aniso", {env::ANISO})) render::set_aniso([pref(@"aniso") boolValue]);
    if (saved(@"fxaa", {env::FXAA})) render::set_fxaa([pref(@"fxaa") boolValue]);
    if (saved(@"interpFps", {env::INTERP_FPS})) interp::set_fps([pref(@"interpFps") intValue]);
    if (saved(@"fps60", {env::INTERP, env::INTERP_FPS})) interp::set_mode([pref(@"fps60") intValue]);
    if (saved(@"fps60Paced", {env::INTERP_PACED})) interp::set_paced_interpolation_at(60, [pref(@"fps60Paced") boolValue]);
    if (saved(@"fpsHighPaced", {env::INTERP_PACED})) interp::set_paced_interpolation_at(120, [pref(@"fpsHighPaced") boolValue]);
    if (saved(@"aspect", {env::ASPECT})) aspect::apply_choice([pref(@"aspect") UTF8String]);
}
void save_prefs() {
    if (env::NO_HOST_INPUT() || !g_prefs_loaded) return;
    if (!env_set({env::RES_SCALE})) set_pref(@"resScale", @(current_res_scale()));
    if (!env_set({env::ANISO})) set_pref(@"aniso", @(render::aniso()));
    if (!env_set({env::FXAA})) set_pref(@"fxaa", @(render::fxaa()));
    if (!env_set({env::INTERP, env::INTERP_FPS})) set_pref(@"fps60", @(interp::mode()));
    if (!env_set({env::INTERP_FPS})) set_pref(@"interpFps", @(interp::fps()));
    if (!env_set({env::ASPECT})) {
        char aspect[32];
        aspect::choice_text(aspect, sizeof aspect);
        set_pref(@"aspect", @(aspect));
    }
    if (!env_set({env::INTERP_PACED})) {
        set_pref(@"fps60Paced", @(interp::paced_interpolation_at(60)));
        set_pref(@"fpsHighPaced", @(interp::paced_interpolation_at(120)));
    }
}
}  // namespace

namespace gfx {

// the scene connected (app_ios.mm): saved graphics settings apply from here on
void ios_load_prefs() { load_prefs(); }

float menu_res_scale() { return current_res_scale(); }
void menu_set_res_scale(float f) { set_res(f); }
void menu_options_changed() { save_prefs(); }

// hardware keyboard shortcuts (input.mm), as the Mac's Graphics and Save States menus have them
bool menu_hotkey(uint16_t code) {
    switch (code) {
    case kVK_ANSI_N: if (render::feature_available(render::kFeatureAniso)) render::set_aniso(!render::aniso()); break;
    case kVK_ANSI_6: interp::toggle_fps(60); break;
    case kVK_ANSI_8: if (render::feature_available(render::kFeatureFXAA)) render::set_fxaa(!render::fxaa()); break;
    case kVK_ANSI_R: cycle_res(); break;
    case kVK_F1: case kVK_F2: case kVK_F3: case kVK_F4: case kVK_F5: {
        int slot = code == kVK_F1 ? 1 : code == kVK_F2 ? 2 : code == kVK_F3 ? 3 : code == kVK_F4 ? 4 : 5;
        GCKeyboardInput* kb = GCKeyboard.coalescedKeyboard.keyboardInput;
        if ([kb buttonForKeyCode:GCKeyCodeLeftShift].isPressed || [kb buttonForKeyCode:GCKeyCodeRightShift].isPressed)
            ss::request_save(slot);
        else
            ss::request_load(slot);
        return true;
    }
    case kVK_ANSI_9: {
        char name[64];
        time_t t = time(nullptr);
        host::ensure_log_dir();
        strftime(name, sizeof name, "%Y%m%d-%H%M%S-sound.log", localtime(&t));
        const std::string path = host::log_dir() + "/" + name;
        ax::start_sound_trace(path.c_str(), 3.0);
        return true;
    }
    default: return false;
    }
    save_prefs();
    return true;
}

bool controls_window_is_key() { return false; }
// the game's text entry (overlay/text_entry.h) takes characters from the hardware keyboard through the overlay
bool text_input_key(void*) { return false; }

}  // namespace gfx

// ---------------------------------------------------------------- settings overlay host (overlay_appkit.mm on the Mac)
namespace hostui {
namespace {
std::atomic<bool> g_app_focused{true};
}

void post(std::function<void()> fn) {
    dispatch_async(dispatch_get_main_queue(), ^{ fn(); });
}
bool get(const char* key, std::string& value) { return gfx::host_setting(key, value); }
void set(const char* key, const std::string& value) { gfx::set_host_setting(key, value); }
void reload_settings() {}
float res_scale() { return gfx::menu_res_scale(); }
void set_res_scale(float s) { gfx::menu_set_res_scale(s); }
void graphics_changed() { gfx::menu_options_changed(); }
int scale_filter() { return gfx::display_filter(); }
void set_scale_filter(int f) { gfx::display_set_filter(f); }
bool scale_filter_available() { return true; }
bool fullscreen() { return true; }
void set_fullscreen(bool) {}
// iOS suspends apps in the background, and the GPU may not be used there: the game always pauses
bool run_in_background() { return false; }
void set_run_in_background(bool) {}
bool app_focused() { return g_app_focused.load(std::memory_order_relaxed); }
void set_app_focused(bool on) { g_app_focused.store(on, std::memory_order_relaxed); }
int drc_modes() { return gfx::kDrcModeCount; }
bool drc_mode_offered(int m) { return gfx::drc_mode_offered(m); }
int drc_mode() { return gfx::display_drc_mode(); }
void set_drc_mode(int m) { gfx::set_drc_mode(m); }
int pip_corner() { return gfx::g_corner; }
void set_pip_corner(int c) { gfx::display_set_pip(c, gfx::g_pip_size, gfx::g_pip_opacity); }
float pip_size() { return gfx::g_pip_size; }
void set_pip_size(float s) { gfx::display_set_pip(gfx::g_corner, s, gfx::g_pip_opacity); }
float pip_opacity() { return gfx::g_pip_opacity; }
void set_pip_opacity(float o) { gfx::display_set_pip(gfx::g_corner, gfx::g_pip_size, o); }
bool drc_available() { return gfx::drc_window_available(); }
bool drc_shown() { return gfx::drc_window_shown(); }
void show_drc(bool on) { gfx::show_drc_window(on); }
void set_pro_controller(bool on) {
    input::set_pro_controller(on);
    gfx::show_drc_window(!on);
}
const char* name() { return "UIKit"; }
void set_clipboard(const std::string& text) { UIPasteboard.generalPasteboard.string = @(text.c_str()); }
// the folders are under the app's Documents, which the Files app shows
bool can_open_folder() { return false; }
void open_folder(const std::string&) {}
void run_posted() {}
void load_saved_options() {}
void toggle_drc() { show_drc(!drc_shown()); }
void drc_window_closed() {}
void tv_fullscreen_changed() {}
bool window_size(int&, int&) { return false; }
void window_resized() {}

}  // namespace hostui

// ---------------------------------------------------------------- mouse camera (mods/mouse.mm on the Mac): no mouse
namespace mods {
bool mouse_captured() { return false; }
void update_gyro_mouse() {}
void mouse_release() {}
void mouse_init(void*) {}
}
