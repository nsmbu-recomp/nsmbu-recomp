

#include "core/env.h"
#import <Cocoa/Cocoa.h>
#include <Carbon/Carbon.h>
#include "input/input.h"
#include "platform/host.h"
#include <sys/stat.h>
#include <ctime>

#include "hooks/interp.h"
#include "save/savestate.h"
#include "gfx/screenshot.h"
#include "input/input_map.h"
#include "../../overlay/hostui.h"
#include "../../overlay/overlay.h"
#include "crash/crashrec.h"
#include "gfx/aspect.h"
#include "gfx/renderer.h"
#include "gfx/res_presets.h"
#ifdef NSMBU_HAS_VULKAN
#include "gfx/vulkan/settings.h"
#endif
#include "core/runtime.h"
#include "core/build_info.h"
#include "core/window_title.h"

namespace gfx {
bool drc_window_available();
bool drc_window_shown();
void show_drc_window(bool on);
void install_display_menu(NSMenu* bar);
void set_host_setting(const char* key, const std::string& value);
}

static float g_res_shown = 0;
static float current_res_scale() { return g_res_shown ? g_res_shown : render::res_scale(); }
static void set_res(float f) { g_res_shown = f; render::set_res_scale(f); }
static void cycle_res() { set_res(gfx::next_res_preset(current_res_scale())); }

namespace gx2 { bool uncapped(); }
#include "../../mods/mods.h"
#include "../../overlay/overlay.h"
namespace ax { void start_sound_trace(const char* path, double seconds); }
namespace gfx { bool menu_hotkey(uint16_t code); NSMenuItem* controls_menu_item();  void install_overlay_input(); }

static NSWindow* g_tv;

static const bool g_prefs = env::NO_HOST_INPUT() == nullptr;
static bool g_prefs_loaded = false;
static bool env_set(std::initializer_list<env::Var> vars) {
    for (env::Var v : vars)
        if ((v)()) return true;
    return false;
}

static NSString* portable_prefs_path() {
    return host::portable() ? @((host::portable_user_dir() + "/graphics.plist").c_str()) : nil;
}
static NSMutableDictionary* g_portable_prefs;
static id pref(NSString* key) {
    if (NSString* p = portable_prefs_path()) {
        if (!g_portable_prefs)
            g_portable_prefs = [[NSDictionary dictionaryWithContentsOfFile:p] mutableCopy] ?: [NSMutableDictionary new];
        return g_portable_prefs[key];
    }
    return [NSUserDefaults.standardUserDefaults objectForKey:key];
}
static void set_pref(NSString* key, id value) {
    if (portable_prefs_path()) {
        pref(key);
        g_portable_prefs[key] = value;
    } else {
        [NSUserDefaults.standardUserDefaults setObject:value forKey:key];
    }
}
static void load_prefs() {
    g_prefs_loaded = true;
    if (!g_prefs) return;
    auto saved = [&](NSString* key, std::initializer_list<env::Var> vars) { return !env_set(vars) && pref(key) != nil; };
    if (saved(@"resScale", {env::RES_SCALE})) set_res([pref(@"resScale") floatValue]);
    if (saved(@"aniso", {env::ANISO})) render::set_aniso([pref(@"aniso") boolValue]);
    if (saved(@"fxaa", {env::FXAA})) render::set_fxaa([pref(@"fxaa") boolValue]);

    if (saved(@"interpFps", {env::INTERP_FPS})) interp::set_fps([pref(@"interpFps") intValue]);
    if (saved(@"fps60", {env::INTERP, env::INTERP_FPS})) interp::set_mode([pref(@"fps60") intValue]);
    if (saved(@"fps60Paced", {env::INTERP_PACED})) interp::set_paced_interpolation_at(60, [pref(@"fps60Paced") boolValue]);
    if (saved(@"fpsUnlocked", {env::INTERP_FPS}) && [pref(@"fpsUnlocked") boolValue]) interp::set_unlocked(true);
    if (!interp::blend_env_locked()) {
        if (id blend = pref(@"frameInterpolation")) interp::set_blend_enabled([blend intValue] != 0);
    }
    if (saved(@"fpsHighPaced", {env::INTERP_PACED})) interp::set_paced_interpolation_at(120, [pref(@"fpsHighPaced") boolValue]);
    if (saved(@"aspect", {env::ASPECT})) aspect::apply_choice([pref(@"aspect") UTF8String]);
#ifdef NSMBU_HAS_VULKAN
    if (saved(@"vkPresentMode", {env::VK_PRESENT_MODE})) gfxvk::set_present_mode([pref(@"vkPresentMode") intValue]);
#endif
}
static void save_prefs() {
    if (!g_prefs || !g_prefs_loaded) return;
    if (!env_set({env::RES_SCALE})) set_pref(@"resScale", @(current_res_scale()));
    if (!env_set({env::ANISO})) set_pref(@"aniso", @(render::aniso()));
    if (!env_set({env::FXAA})) set_pref(@"fxaa", @(render::fxaa()));
    if (!env_set({env::INTERP, env::INTERP_FPS})) set_pref(@"fps60", @(interp::mode()));
    if (!env_set({env::INTERP_FPS})) {
        set_pref(@"interpFps", @(interp::fps()));
        set_pref(@"fpsUnlocked", @(interp::unlocked()));
    }
    if (!env_set({env::ASPECT})) {
        char aspect[32];
        aspect::choice_text(aspect, sizeof aspect);
        set_pref(@"aspect", @(aspect));
    }
    if (!env_set({env::INTERP_PACED})) {
        set_pref(@"fps60Paced", @(interp::paced_interpolation_at(60)));
        set_pref(@"fpsHighPaced", @(interp::paced_interpolation_at(120)));
    }
    if (!interp::blend_env_locked()) set_pref(@"frameInterpolation", @(interp::blend_enabled()));
#ifdef NSMBU_HAS_VULKAN
    if (!env_set({env::VK_PRESENT_MODE})) set_pref(@"vkPresentMode", @(gfxvk::present_mode()));
#endif
    if (NSString* p = portable_prefs_path()) [g_portable_prefs writeToFile:p atomically:YES];
}

static void update_title() {
    save_prefs();
    char base[window_title::kCap];
    window_title::format_base(base, sizeof base);
    NSString* baseTitle = @(base);
    NSString* t = baseTitle;
    if (build::dev() && gx2::uncapped())
        t = [t stringByAppendingString:@" - UNCAPPED (debug)"];
    std::string msg = ss::last_message();
    if (!msg.empty()) t = [NSString stringWithFormat:@"%@ - %s", baseTitle, msg.c_str()];
    if (env::LOG_TITLE() && ![t isEqualToString:g_tv.title]) {
        static NSString* last;
        if (![t isEqualToString:last]) LOG("[display] title: %s", t.UTF8String);
        last = t;
    }
    [g_tv setTitle:t];
}

static void choose_renderer(render::Api a) {
    if (a == render::preferred()) return;
    render::set_preferred(a);
    update_title();
    if (a == render::active()) return;
    if (env::NO_HOST_INPUT()) return;
    NSAlert* alert = [NSAlert new];
    alert.messageText = [NSString stringWithFormat:@"The game will use %s after a restart.", render::api_name(a)];
    alert.informativeText = @"Restart now? Progress since your last save (in-game save or save state) is lost.";
    [alert addButtonWithTitle:@"Restart Now"];
    [alert addButtonWithTitle:@"Later"];
    [alert beginSheetModalForWindow:g_tv completionHandler:^(NSModalResponse r) {
        if (r != NSAlertFirstButtonReturn) return;
        if (!render::restart()) {
            NSAlert* e = [NSAlert new];
            e.messageText = @"The game could not restart itself.";
            e.informativeText = @"Quit and start it again to switch the renderer.";
            [e beginSheetModalForWindow:g_tv completionHandler:nil];
        }
    }];
}

@interface NsmbuStateMenu : NSObject <NSMenuDelegate>
@end
@implementation NsmbuStateMenu
- (void)save:(NSMenuItem*)item { ss::request_save((int)item.tag); }
- (void)load:(NSMenuItem*)item { ss::request_load((int)item.tag); }
- (void)toggleCrashRecovery:(NSMenuItem*)item { crashrec::set_enabled(!crashrec::enabled()); }
- (void)toggleFullStates:(NSMenuItem*)item { ss::set_full_states(!ss::full_states()); }
- (void)loadAuto:(NSMenuItem*)item { crashrec::request_load((int)item.tag); }
- (void)menuNeedsUpdate:(NSMenu*)m {
    [m removeAllItems];
    ss::SlotInfo info[ss::kSlots + 1];
    for (int i = 1; i <= ss::kSlots; i++) info[i] = ss::slot_info(i);
    auto label = [&](int i) -> NSString* {
        const ss::SlotInfo& s = info[i];
        if (!s.used) return @"empty";
        NSString* d = [NSString stringWithFormat:@"%s%s%s%s", s.when.c_str(), s.area.empty() ? "" : " · ", s.area.c_str(),
                                                    s.portable ? "" : " · full"];
        if (!s.controller.empty()) d = [d stringByAppendingFormat:@" · %s", s.controller.c_str()];
        return s.compatible ? d : [d stringByAppendingString:@" (incompatible)"];
    };
    for (int i = 1; i <= ss::kSlots; i++) {
        NSMenuItem* it = [m addItemWithTitle:[NSString stringWithFormat:@"Save to slot %d  (%@)", i, label(i)] action:@selector(save:) keyEquivalent:@""];
        it.target = self;
        it.tag = i;
        it.toolTip = [NSString stringWithFormat:@"Shortcut in game: Shift+F%d", i];
    }
    [m addItem:[NSMenuItem separatorItem]];
    for (int i = 1; i <= ss::kSlots; i++) {
        NSMenuItem* it = [m addItemWithTitle:[NSString stringWithFormat:@"Load slot %d  (%@)", i, label(i)] action:@selector(load:) keyEquivalent:@""];
        it.target = self;
        it.tag = i;
        it.enabled = info[i].used && info[i].compatible;
        it.toolTip = [NSString stringWithFormat:@"Shortcut in game: F%d", i];
    }

    [m addItem:[NSMenuItem separatorItem]];
    NSMenuItem* fs = [m addItemWithTitle:@"Full Save States (large, contain game data, don't share)" action:@selector(toggleFullStates:)
                           keyEquivalent:@""];
    fs.target = self;
    fs.state = ss::full_states() ? NSControlStateValueOn : NSControlStateValueOff;
    fs.enabled = !ss::full_states_forced();
    fs.toolTip = @"For debugging: Save makes a snapshot of the whole running game (about 300 MB). Off: Save makes a small "
                 @"portable state (progress and position, no game data) that can be attached to bug reports.";

    [m addItem:[NSMenuItem separatorItem]];
    NSMenuItem* cr = [m addItemWithTitle:[NSString stringWithFormat:@"Crash Recovery (automatic state every %d min)",
                                                                    (crashrec::interval_seconds() + 30) / 60]
                                  action:@selector(toggleCrashRecovery:) keyEquivalent:@""];
    cr.target = self;
    cr.state = crashrec::enabled() ? NSControlStateValueOn : NSControlStateValueOff;
    cr.toolTip = @"Saves the game into automatic states in the background and records the controller input since the "
                 @"latest one. After a crash, the crash log in nsmbu/logs/ says how to load it and replay the input. "
                 @"Saving freezes the game for a moment.";
    for (int i = 1; i <= crashrec::kAutoSlots; i++) {
        crashrec::AutoInfo a = crashrec::auto_info(i);
        NSString* d = a.used ? [NSString stringWithFormat:@"%s%s%s", a.when.c_str(), a.area.empty() ? "" : " · ", a.area.c_str()] : @"empty";
        NSMenuItem* it = [m addItemWithTitle:[NSString stringWithFormat:@"Load automatic state %d  (%@)", i, d] action:@selector(loadAuto:) keyEquivalent:@""];
        it.target = self;
        it.tag = i;
        it.enabled = a.used;
    }
}
@end
static NsmbuStateMenu* g_state_menu;

@interface NsmbuGraphicsMenu : NSObject <NSMenuItemValidation>
@end

@implementation NsmbuGraphicsMenu
- (void)toggleAniso:(NSMenuItem*)item { render::set_aniso(!render::aniso()); update_title(); }
- (void)capture:(NSMenuItem*)item { render::request_capture(); }
- (void)screenshot:(NSMenuItem*)item { screenshot::request(); }
- (void)openScreenshots:(NSMenuItem*)item { hostui::open_folder(screenshot::dir()); }
- (void)toggleScreenshotGamePad:(NSMenuItem*)item { screenshot::set_gamepad_too(!screenshot::gamepad_too()); }
- (void)openSettings:(NSMenuItem*)item { overlay::set_open(!overlay::is_open()); }
- (void)setRenderer:(NSMenuItem*)item { choose_renderer((render::Api)item.tag); }
- (void)recordSound:(NSMenuItem*)item { gfx::menu_hotkey(kVK_ANSI_9); }
- (void)setController:(NSMenuItem*)item {
    input::set_pro_controller(item.tag == 1);
    gfx::show_drc_window(item.tag == 0);
    gfx::set_host_setting("proController", item.tag == 1 ? "1" : "0");
}
- (void)setFaceLayout:(NSMenuItem*)item {

    input_map::Mapping m = input_map::current();
    input_map::apply_face_layout(m, item.tag == 1 ? input_map::FaceLayout::kLabels : input_map::FaceLayout::kPosition);
    input_map::set_current(m);
}
- (void)toggleInterpFps:(NSMenuItem*)item { interp::toggle_fps((int)item.tag); update_title(); }
- (void)toggleDrcWindow:(NSMenuItem*)item { gfx::show_drc_window(!gfx::drc_window_shown()); }
- (void)toggleFxaa:(NSMenuItem*)item { render::set_fxaa(!render::fxaa()); update_title(); }
- (void)setRes:(NSMenuItem*)item { set_res(gfx::kResPresets[item.tag].scale); update_title(); }
- (BOOL)validateMenuItem:(NSMenuItem*)item {
    if (item.action == @selector(setRes:))
        item.state = fabsf(gfx::kResPresets[item.tag].scale - current_res_scale()) < 0.01f ? NSControlStateValueOn : NSControlStateValueOff;
    if (item.action == @selector(setRenderer:)) {
        render::Api a = (render::Api)item.tag;
        item.state = a == render::preferred() ? NSControlStateValueOn : NSControlStateValueOff;
        NSString* name = a == render::Api::Vulkan ? @"    Vulkan (MoltenVK)" : @"    Metal";
        item.title = a == render::active() ? [name stringByAppendingString:@"  \u2014 in use"] : name;
        if (!render::compiled(a)) return NO;
        return YES;
    }
    if (item.action == @selector(setController:))
        item.state = (item.tag == 1) == input::pro_controller() ? NSControlStateValueOn : NSControlStateValueOff;
    if (item.action == @selector(setFaceLayout:)) {
        input_map::FaceLayout fl = input_map::face_layout(input_map::current());
        bool on = fl == (item.tag == 1 ? input_map::FaceLayout::kLabels : input_map::FaceLayout::kPosition);
        item.state = on ? NSControlStateValueOn : NSControlStateValueOff;
    }
    if (item.action == @selector(toggleInterpFps:))
        item.state = interp::mode() == 1 && interp::fps() == item.tag ? NSControlStateValueOn : NSControlStateValueOff;
    if (item.action == @selector(toggleDrcWindow:)) {
        item.state = gfx::drc_window_shown() ? NSControlStateValueOn : NSControlStateValueOff;
        return gfx::drc_window_available();
    }
    if (item.action == @selector(toggleFxaa:)) {
        item.state = render::fxaa() ? NSControlStateValueOn : NSControlStateValueOff;
        return render::feature_available(render::kFeatureFXAA);
    }
    if (item.action == @selector(toggleAniso:)) {
        item.state = render::aniso() ? NSControlStateValueOn : NSControlStateValueOff;
        return render::feature_available(render::kFeatureAniso);
    }
    if (item.action == @selector(capture:)) return render::feature_available(render::kFeatureCapture);
    if (item.action == @selector(screenshot:)) {

        int k = input_map::current().keys[input_map::kScreenshot][0];
        if (k == input_map::kNoKey) k = input_map::current().keys[input_map::kScreenshot][1];
        item.title = k == input_map::kNoKey ? @"Take Screenshot"
                                            : [NSString stringWithFormat:@"Take Screenshot (%s)", input_map::key_label(k).c_str()];
    }
    if (item.action == @selector(toggleScreenshotGamePad:))
        item.state = screenshot::gamepad_too() ? NSControlStateValueOn : NSControlStateValueOff;
    return YES;
}
@end

static NsmbuGraphicsMenu* g_target;

static NSMenuItem* add(NSMenu* m, NSString* title, SEL action, NSString* key, NSInteger tag = 0) {
    NSMenuItem* it = [m addItemWithTitle:title action:action keyEquivalent:@""];
    it.target = g_target;
    it.tag = tag;

    if (key.length) it.toolTip = [NSString stringWithFormat:@"Shortcut in game: %@", key];
    return it;
}

namespace gfx {

void install_menu(NSWindow* tv) {
    g_tv = tv;
    g_target = [NsmbuGraphicsMenu new];
    NSMenu* bar = [NSMenu new];

    NSMenuItem* appItem = [bar addItemWithTitle:@"" action:nil keyEquivalent:@""];
    NSMenu* app = [NSMenu new];

    NSMenuItem* settings = [app addItemWithTitle:@"Settings\u2026" action:@selector(openSettings:) keyEquivalent:@","];
    settings.target = g_target;
    settings.toolTip = @"In-game settings overlay over the picture (` or F12, or \u2318, in the game window)";
    [app addItem:[NSMenuItem separatorItem]];
    [app addItemWithTitle:@"Quit New Super Mario Bros. U" action:@selector(terminate:) keyEquivalent:@"q"];
    appItem.submenu = app;

    NSMenuItem* gfxItem = [bar addItemWithTitle:@"Graphics" action:nil keyEquivalent:@""];
    NSMenu* g = [[NSMenu alloc] initWithTitle:@"Graphics"];
    if (render::can_choose()) {

        [g addItemWithTitle:@"Renderer (takes effect after a restart)" action:nil keyEquivalent:@""].enabled = NO;
        NSString* why = render::fallback_reason().empty() ? @"" : [NSString stringWithFormat:@"\nThis start: %s", render::fallback_reason().c_str()];
        add(g, @"    Metal", @selector(setRenderer:), @"", (NSInteger)render::Api::Metal).toolTip =
            @"Apple's Metal: the original renderer, all features";
        add(g, @"    Vulkan (MoltenVK)", @selector(setRenderer:), @"", (NSInteger)render::Api::Vulkan).toolTip =
            [@"Vulkan through MoltenVK (needs: brew install vulkan-loader molten-vk glslang). Falls back to Metal if it cannot start." stringByAppendingString:why];
        [g addItem:[NSMenuItem separatorItem]];
    }
    [g addItemWithTitle:@"Internal resolution (R cycles)" action:nil keyEquivalent:@""].enabled = NO;
    for (size_t i = 0; i < gfx::res_preset_count(); i++) {
        NSString* title = [NSString stringWithFormat:@"    %s", gfx::kResPresets[i].label];
        NSMenuItem* item = add(g, title, @selector(setRes:), @"R", (NSInteger)i);
        if (gfx::kResPresets[i].scale == 1.0f)
            item.toolTip = @"The Wii U picture. Shortcut in game: R";
    }
    [g addItem:[NSMenuItem separatorItem]];
    add(g, @"16x anisotropic filtering (N)", @selector(toggleAniso:), @"N");
    add(g, @"Edge smoothing, FXAA (8)", @selector(toggleFxaa:), @"8");
    [g addItemWithTitle:@"Frame rate (experimental)" action:nil keyEquivalent:@""].enabled = NO;
    add(g, @"60 fps: frame interpolation (6)", @selector(toggleInterpFps:), @"6", 60);
    add(g, @"120 fps: frame interpolation", @selector(toggleInterpFps:), @"", 120);
    add(g, @"240 fps: frame interpolation", @selector(toggleInterpFps:), @"", 240);
    [g addItem:[NSMenuItem separatorItem]];
    add(g, @"Take Screenshot (F10)", @selector(screenshot:), @"").toolTip =
        @"Saves the TV picture as a PNG (internal resolution, without the settings overlay) in the screenshots folder. "
        @"The key can be changed in Input > Controls (Screenshot).";
    add(g, @"    Also Save the GamePad Screen", @selector(toggleScreenshotGamePad:), @"").toolTip =
        @"A second file, ..._GamePad.png, while the GamePad picture is shown (its window or the overlay)";
    add(g, @"    Open Screenshots Folder", @selector(openScreenshots:), @"");
    add(g, @"Capture frame for debugging (P)", @selector(capture:), @"P").toolTip =
        render::active() == render::Api::Vulkan ? @"Shortcut in game: P. Vulkan: the TV, GamePad and window pictures (the draw log is Metal only)"
                                                 : @"Shortcut in game: P";
    add(g, @"Record 3 s of sound activity (9)", @selector(recordSound:), @"9");
    gfxItem.submenu = g;

    NSMenuItem* inItem = [bar addItemWithTitle:@"Input" action:nil keyEquivalent:@""];
    NSMenu* in = [[NSMenu alloc] initWithTitle:@"Input"];
    [in addItemWithTitle:@"Keyboard and controllers act as" action:nil keyEquivalent:@""].enabled = NO;
    add(in, @"    Wii U GamePad", @selector(setController:), @"", 0);
    add(in, @"    Wii U Pro Controller", @selector(setController:), @"", 1);
    [in addItemWithTitle:@"Face buttons drive the Wii U's A/B/X/Y" action:nil keyEquivalent:@""].enabled = NO;
    add(in, @"    By position (Nintendo)", @selector(setFaceLayout:), @"", 0).toolTip =
        @"The bottom face button is B and the right one is A (Nintendo layout)";
    add(in, @"    By label (Xbox)", @selector(setFaceLayout:), @"", 1).toolTip =
        @"The button named A is A: on an Xbox pad A accepts/acts and B goes back (issue #78). "
        @"Rewrites the A/B/X/Y controller bindings only";
    [in addItem:[NSMenuItem separatorItem]];
    add(in, @"Show GamePad screen (\u2318G)", @selector(toggleDrcWindow:), @"");
    [in addItem:gfx::controls_menu_item()];
    inItem.submenu = in;
    install_display_menu(bar);

    mods::mouse_init((__bridge void*)tv);

    NSMenuItem* ssItem = [bar addItemWithTitle:@"Save States" action:nil keyEquivalent:@""];
    NSMenu* sm = [[NSMenu alloc] initWithTitle:@"Save States"];
    sm.autoenablesItems = NO;
    g_state_menu = [NsmbuStateMenu new];
    sm.delegate = g_state_menu;
    ssItem.submenu = sm;

    NSMenuItem* winItem = [bar addItemWithTitle:@"Window" action:nil keyEquivalent:@""];
    NSMenu* wm = [[NSMenu alloc] initWithTitle:@"Window"];
    [wm addItemWithTitle:@"Minimize" action:@selector(performMiniaturize:) keyEquivalent:@"m"];
    [wm addItemWithTitle:@"Zoom" action:@selector(performZoom:) keyEquivalent:@""];
    [wm addItem:[NSMenuItem separatorItem]];
    [wm addItemWithTitle:@"Close Window" action:@selector(performClose:) keyEquivalent:@"w"];
    winItem.submenu = wm;
    NSApp.windowsMenu = wm;

    NSApp.mainMenu = bar;
    install_overlay_input();
    update_title();

    dispatch_async(dispatch_get_main_queue(), ^{
        load_prefs();
        update_title();
    });

    [NSTimer scheduledTimerWithTimeInterval:0.5 repeats:YES block:^(NSTimer*) {
        update_title();
    }];
}

float menu_res_scale() { return current_res_scale(); }
void menu_set_res_scale(float f) { set_res(f); }
void menu_options_changed() { update_title(); }

bool menu_hotkey(uint16_t code) {
    switch (code) {
    case kVK_ANSI_N: if (render::feature_available(render::kFeatureAniso)) render::set_aniso(!render::aniso()); break;
    case kVK_ANSI_6: interp::toggle_fps(60); break;
    case kVK_ANSI_T: interp::toggle_fps(120); break;
    case kVK_ANSI_8: if (render::feature_available(render::kFeatureFXAA)) render::set_fxaa(!render::fxaa()); break;
    case kVK_ANSI_R: cycle_res(); break;
    case kVK_ANSI_P: render::request_capture(); return true;
    case kVK_F1: case kVK_F2: case kVK_F3: case kVK_F4: case kVK_F5: {
        int slot = code == kVK_F1 ? 1 : code == kVK_F2 ? 2 : code == kVK_F3 ? 3 : code == kVK_F4 ? 4 : 5;
        if ([NSEvent modifierFlags] & NSEventModifierFlagShift) ss::request_save(slot);
        else ss::request_load(slot);
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
    dispatch_async(dispatch_get_main_queue(), ^{ update_title(); });
    return true;
}

}
