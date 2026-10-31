

#include "core/env.h"
#include <TargetConditionals.h>
#if TARGET_OS_IPHONE
#import <UIKit/UIKit.h>
#include "platform/keycodes.h"
#else
#import <AppKit/AppKit.h>
#include <Carbon/Carbon.h>
#endif
#import <GameController/GameController.h>
#import <QuartzCore/QuartzCore.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <mutex>

#include "input/input.h"
#include "input/input_map.h"
#include "gfx/renderer.h"
#include "core/runtime.h"
#include "save/savestate.h"
#include "gfx/screenshot.h"
#include "test/script_frame.h"
#include "../../input/motion/motion.h"
#include "../../overlay/overlay.h"

#include <memory>
#include <string>
#include <vector>

namespace gfx { void request_capture(); bool menu_hotkey(uint16_t keyCode); bool controls_window_is_key(); bool text_input_key(void* event); }
namespace mods { void update_gyro_mouse(); }
#if TARGET_OS_IPHONE
namespace gfx { void ios_touch_values(float* v); void ios_present_alert(UIAlertController* a); }
#endif

namespace input {

static std::mutex g_mu;
static bool g_keys[256];
static PadState g_pads[input::kMaxPlayers];
static bool g_touch;
static float g_tx, g_ty;
static float g_player_values[input::kMaxPlayers][input_map::kPadCount];
static int g_controller_count = 0;
static std::array<std::string, input::kMaxPlayers> g_controller_names;

void set_touch(bool down, float x, float y) {
    std::lock_guard<std::mutex> lk(g_mu);
    g_touch = down;
    g_tx = x;
    g_ty = y;
}

static bool g_script_keys[256];

static PadState keyboard_state(bool use_host) {
    bool keys[256];
    for (int i = 0; i < 256; i++) keys[i] = (use_host && g_keys[i]) || g_script_keys[i];
    return input_map::keyboard_state(input_map::current(), keys);
}

static int read_controllers(float values[input::kMaxPlayers][input_map::kPadCount]) {
    using namespace input_map;
    NSArray<GCController*>* controllers = [GCController controllers];
    const int n = std::min<int>((int)controllers.count, input::kMaxPlayers);
    for (int i = 0; i < n; i++) {
        GCController* c = controllers[i];
        GCExtendedGamepad* g = c.extendedGamepad;
        if (!g) continue;
        float* v = values[i];
        auto put = [&](int p, float x) { if (x > v[p]) v[p] = x; };
        auto b = [&](GCControllerButtonInput* in, int p) {
            if (in) put(p, in.isPressed ? std::max(in.value, 0.51f) : std::min(in.value, 0.5f));
        };
        b(g.buttonA, kPadA); b(g.buttonB, kPadB); b(g.buttonX, kPadX); b(g.buttonY, kPadY);
        b(g.leftShoulder, kPadLB); b(g.rightShoulder, kPadRB);
        b(g.leftTrigger, kPadLT); b(g.rightTrigger, kPadRT);
        b(g.buttonMenu, kPadMenu); b(g.buttonOptions, kPadOptions); b(g.buttonHome, kPadHome);
        b(g.leftThumbstickButton, kPadL3); b(g.rightThumbstickButton, kPadR3);
        b(g.dpad.up, kPadDUp); b(g.dpad.down, kPadDDown); b(g.dpad.left, kPadDLeft); b(g.dpad.right, kPadDRight);
        auto stick = [&](GCControllerDirectionPad* d, int up, int down, int left, int right) {
            float x = d.xAxis.value, y = d.yAxis.value;
            put(right, std::max(x, 0.f)); put(left, std::max(-x, 0.f));
            put(up, std::max(y, 0.f)); put(down, std::max(-y, 0.f));
        };
        stick(g.leftThumbstick, kPadLSUp, kPadLSDown, kPadLSLeft, kPadLSRight);
        stick(g.rightThumbstick, kPadRSUp, kPadRSDown, kPadRSLeft, kPadRSRight);
    }
    return n;
}

void controller_values(float* v) {
    using namespace input_map;
    float values[input::kMaxPlayers][kPadCount] = {};
    const int n = read_controllers(values);
    for (int i = 0; i < kPadCount; i++) v[i] = 0;
    for (int p = 0; p < n; p++)
        for (int i = 0; i < kPadCount; i++) v[i] = std::max(v[i], values[p][i]);
}

void controller_values(int player, float* v) {
    std::lock_guard<std::mutex> lk(g_mu);
    player = std::clamp(player, 0, input::kMaxPlayers - 1);
    std::copy(g_player_values[player], g_player_values[player] + input_map::kPadCount, v);
}

static float g_host_values[input_map::kPadCount];
void host_controller_values(float* v) {
    std::lock_guard<std::mutex> lk(g_mu);
    std::copy(g_host_values, g_host_values + input_map::kPadCount, v);
}

void host_controller_values(int player, float* v) { controller_values(player, v); }

bool controller_connected(int player) {
    std::lock_guard<std::mutex> lk(g_mu);
    return player >= 0 && player < g_controller_count;
}

std::string controller_name(int player) {
    std::lock_guard<std::mutex> lk(g_mu);
    if (player < 0 || player >= input::kMaxPlayers) return {};
    return g_controller_names[player];
}

void held_keys(bool* keys) {
    std::lock_guard<std::mutex> lk(g_mu);
    for (int i = 0; i < 256; i++) keys[i] = g_keys[i] || g_script_keys[i];
}

void release_keys() {
    std::lock_guard<std::mutex> lk(g_mu);
    memset(g_keys, 0, sizeof(g_keys));
}

bool has_rumble() { return false; }
void stop_rumble_now() {}

#if !TARGET_OS_IPHONE
static bool modifier_down(uint16_t code, NSEventModifierFlags f) {
    switch (code) {
    case kVK_Shift: return f & 0x02;
    case kVK_RightShift: return f & 0x04;
    case kVK_Control: return f & 0x01;
    case kVK_RightControl: return f & 0x2000;
    case kVK_Option: return f & 0x20;
    case kVK_RightOption: return f & 0x40;
    case kVK_CapsLock: return f & NSEventModifierFlagCapsLock;
    default: return false;
    }
}

static const NSTimeInterval kTestKeyTimestamp = 4242.0;
static void start_test_keys() {
    const char* e = env::TEST_POST_KEYS();
    if (!e || !*e) return;
    struct Press { uint64_t frame; uint16_t code; NSEventModifierFlags flags; NSString* chars; uint64_t hold; };
    auto presses = std::make_shared<std::vector<Press>>();
    for (const char* p = e; *p;) {
        char* end;
        uint64_t f = strtoull(p, &end, 10);
        if (*end != ':') break;
        p = end + 1;
        size_t len = strcspn(p, ",");
        std::string k(p, len);
        p += len + (p[len] == ',');
        Press pr{f, 0, 0, @"", 0};
        if (k.rfind("Text=", 0) == 0) {
            NSString* t = [NSString stringWithUTF8String:k.c_str() + 5];
            for (NSUInteger i = 0; i < t.length; i++)
                presses->push_back({f + i * 2, kVK_ANSI_A, 0, [t substringWithRange:NSMakeRange(i, 1)], 0});
            continue;
        }
        if (size_t sl = k.find('/'); sl != std::string::npos) {
            pr.hold = strtoull(k.c_str() + sl + 1, nullptr, 10);
            k.resize(sl);
        }
        if (k.rfind("Cmd+", 0) == 0) { pr.flags = NSEventModifierFlagCommand; k = k.substr(4); }
        if (k == "F1") { pr.code = kVK_F1; pr.chars = [NSString stringWithFormat:@"%C", (unichar)NSF1FunctionKey]; pr.flags |= NSEventModifierFlagFunction; }
        else if (k == "Comma") { pr.code = kVK_ANSI_Comma; pr.chars = @","; }
        else if (k == "Escape") { pr.code = kVK_Escape; pr.chars = @"\x1b"; }
        else if (int c = input_map::key_from_id(k); c != input_map::kNoKey) pr.code = (uint16_t)c;
        else { LOG("[input] NSMBU_TEST_POST_KEYS: unknown key %s", k.c_str()); continue; }
        presses->push_back(pr);
    }

    for (size_t i = 0, n = presses->size(); i < n; i++)
        if ((*presses)[i].hold) {
            Press up = (*presses)[i];
            up.frame += up.hold;
            up.hold = ~0ull;
            presses->push_back(up);
        }
    std::stable_sort(presses->begin(), presses->end(), [](const Press& a, const Press& b) { return a.frame < b.frame; });
    [NSTimer scheduledTimerWithTimeInterval:1.0 / 120 repeats:YES block:^(NSTimer* t) {
        uint64_t frame = render::frame_count();
        while (!presses->empty() && frame >= presses->front().frame) {
            Press pr = presses->front();
            presses->erase(presses->begin());
            LOG("[input] test key %s%u %s at frame %llu", pr.flags & NSEventModifierFlagCommand ? "Cmd+" : "", pr.code,
                pr.hold == ~0ull ? "up" : pr.hold ? "down" : "press", (unsigned long long)frame);
            std::vector<NSEventType> types = pr.hold == ~0ull ? std::vector<NSEventType>{NSEventTypeKeyUp}
                                             : pr.hold         ? std::vector<NSEventType>{NSEventTypeKeyDown}
                                                               : std::vector<NSEventType>{NSEventTypeKeyDown, NSEventTypeKeyUp};
            for (NSEventType type : types) {
                NSEvent* ev = [NSEvent keyEventWithType:type location:NSZeroPoint modifierFlags:pr.flags timestamp:kTestKeyTimestamp
                                           windowNumber:0 context:nil characters:pr.chars charactersIgnoringModifiers:pr.chars
                                              isARepeat:NO keyCode:pr.code];
                [NSApp postEvent:ev atStart:NO];
            }
        }
        if (presses->empty()) [t invalidate];
    }];
}
#endif  // !TARGET_OS_IPHONE

static void update_motion_sensors() API_AVAILABLE(macos(11.0)) {
    const bool want = motion::wants_controller_sensors() && !env::NO_HOST_INPUT();
    static NSMutableSet* active = [NSMutableSet set];
    int n = 0;
    NSArray<GCController*>* connected = [GCController controllers];

    for (GCController* c in [active allObjects])
        if (![connected containsObject:c]) {
            [active removeObject:c];
            c.motion.valueChangedHandler = nil;
            motion::controller_gone((uint64_t)(uintptr_t)(__bridge void*)c);
        }
    for (GCController* c in connected) {
        GCMotion* m = c.motion;
        if (!m || !m.hasRotationRate) continue;
        n++;
        const bool on = [active containsObject:c];

        if (want && on && m.sensorsRequireManualActivation && !m.sensorsActive) {
            LOG("[gyro] %s: its motion sensors were off, switching them on again", c.vendorName.UTF8String ?: "controller");
            m.sensorsActive = YES;
        }
        if (want == on) continue;
        if (want) {
            [active addObject:c];
            if (m.sensorsRequireManualActivation) m.sensorsActive = YES;
            const uint64_t id = (uint64_t)(uintptr_t)(__bridge void*)c;
            m.valueChangedHandler = ^(GCMotion* mm) {
                if (overlay::blocks_input()) return;
                GCRotationRate r = mm.rotationRate;
                GCAcceleration a = mm.hasGravityAndUserAcceleration
                                       ? GCAcceleration{mm.gravity.x + mm.userAcceleration.x, mm.gravity.y + mm.userAcceleration.y,
                                                        mm.gravity.z + mm.userAcceleration.z}
                                       : mm.acceleration;
                const float g = 9.80665f;
                float gyro[3] = {(float)r.x, (float)r.z, (float)-r.y};
                float acc[3] = {(float)-a.x * g, (float)-a.z * g, (float)a.y * g};
                static const bool log = env::GYRO_LOG() != nullptr;
                static double last_log = 0;
                if (log && CACurrentMediaTime() - last_log > 0.5) {
                    last_log = CACurrentMediaTime();
                    LOG("[gyro] GameController raw: rate %.3f %.3f %.3f rad/s, acc %.3f %.3f %.3f g", r.x, r.y, r.z, a.x, a.y, a.z);
                }
                motion::controller_sample(id, 0, gyro, acc);
            };
        } else {
            [active removeObject:c];
            m.valueChangedHandler = nil;
            if (m.sensorsRequireManualActivation) m.sensorsActive = NO;
            motion::controller_gone((uint64_t)(uintptr_t)(__bridge void*)c);
        }
    }
    motion::set_gyro_controllers(n);
}

// controllers (and on iOS the on-screen controls) into the players' pads, 240 times a second
static void poll_controllers() {
    float values[input::kMaxPlayers][input_map::kPadCount] = {};
    const int n = read_controllers(values);
#if TARGET_OS_IPHONE
    gfx::ios_touch_values(values[0]);  // on-screen controls (ios/touch_controls.mm) belong to player 1
#endif
    PadState states[input::kMaxPlayers];
    for (int i = 0; i < input::kMaxPlayers; i++)
        states[i] = input_map::controller_state(input_map::player(i), values[i]);
    float v[input_map::kPadCount] = {};
    for (int i = 0; i < input_map::kPadCount; i++)
        for (int p = 0; p < n; p++) v[i] = std::max(v[i], values[p][i]);
    {
        std::lock_guard<std::mutex> lk(g_mu);
        g_controller_count = n;
        for (int i = 0; i < input::kMaxPlayers; i++) {
            std::copy(values[i], values[i] + input_map::kPadCount, g_player_values[i]);
            g_pads[i] = states[i];
            if (i < n && i < (int)[[GCController controllers] count]) {
                GCController* c = [GCController controllers][i];
                const char* name = c.vendorName.UTF8String ?: c.productCategory.UTF8String;
                g_controller_names[i] = name ? name : "";
            } else {
                g_controller_names[i].clear();
            }
        }
        std::copy(v, v + input_map::kPadCount, g_host_values);
    }

    static int tick = 0;
    if (@available(macOS 11.0, *)) if (tick++ % 60 == 0) update_motion_sensors();
    if (!overlay::blocks_input() && !env::NO_HOST_INPUT()) {
        bool keys[256];
        {
            std::lock_guard<std::mutex> lk(g_mu);
            std::copy(g_keys, g_keys + 256, keys);
        }
        motion::poll_recalibrate(v, keys);
        screenshot::poll_controller(v);
    }
    mods::update_gyro_mouse();
}

#if TARGET_OS_IPHONE
// GCKeyboard reports HID usages; the key map, the overlay and the hotkeys use the Mac's virtual key codes
static int hid_to_vk(GCKeyCode c) {
    static const uint8_t letters[26] = {kVK_ANSI_A, kVK_ANSI_B, kVK_ANSI_C, kVK_ANSI_D, kVK_ANSI_E, kVK_ANSI_F, kVK_ANSI_G,
                                        kVK_ANSI_H, kVK_ANSI_I, kVK_ANSI_J, kVK_ANSI_K, kVK_ANSI_L, kVK_ANSI_M, kVK_ANSI_N,
                                        kVK_ANSI_O, kVK_ANSI_P, kVK_ANSI_Q, kVK_ANSI_R, kVK_ANSI_S, kVK_ANSI_T, kVK_ANSI_U,
                                        kVK_ANSI_V, kVK_ANSI_W, kVK_ANSI_X, kVK_ANSI_Y, kVK_ANSI_Z};
    static const uint8_t digits[10] = {kVK_ANSI_1, kVK_ANSI_2, kVK_ANSI_3, kVK_ANSI_4, kVK_ANSI_5,
                                       kVK_ANSI_6, kVK_ANSI_7, kVK_ANSI_8, kVK_ANSI_9, kVK_ANSI_0};
    static const uint8_t fkeys[12] = {kVK_F1, kVK_F2, kVK_F3, kVK_F4, kVK_F5, kVK_F6, kVK_F7, kVK_F8, kVK_F9, kVK_F10, kVK_F11, kVK_F12};
    const long u = (long)c;
    if (u >= 0x04 && u <= 0x1D) return letters[u - 0x04];
    if (u >= 0x1E && u <= 0x27) return digits[u - 0x1E];
    if (u >= 0x3A && u <= 0x45) return fkeys[u - 0x3A];
    switch (u) {
    case 0x28: return kVK_Return;
    case 0x29: return kVK_Escape;
    case 0x2A: return kVK_Delete;
    case 0x2B: return kVK_Tab;
    case 0x2C: return kVK_Space;
    case 0x2D: return kVK_ANSI_Minus;
    case 0x2E: return kVK_ANSI_Equal;
    case 0x2F: return kVK_ANSI_LeftBracket;
    case 0x30: return kVK_ANSI_RightBracket;
    case 0x31: return kVK_ANSI_Backslash;
    case 0x33: return kVK_ANSI_Semicolon;
    case 0x34: return kVK_ANSI_Quote;
    case 0x35: return kVK_ANSI_Grave;
    case 0x36: return kVK_ANSI_Comma;
    case 0x37: return kVK_ANSI_Period;
    case 0x38: return kVK_ANSI_Slash;
    case 0x39: return kVK_CapsLock;
    case 0x4A: return kVK_Home;
    case 0x4B: return kVK_PageUp;
    case 0x4C: return kVK_ForwardDelete;
    case 0x4D: return kVK_End;
    case 0x4E: return kVK_PageDown;
    case 0x4F: return kVK_RightArrow;
    case 0x50: return kVK_LeftArrow;
    case 0x51: return kVK_DownArrow;
    case 0x52: return kVK_UpArrow;
    case 0x58: return kVK_ANSI_KeypadEnter;
    case 0xE0: return kVK_Control;
    case 0xE1: return kVK_Shift;
    case 0xE2: return kVK_Option;
    case 0xE3: return kVK_Command;
    case 0xE4: return kVK_RightControl;
    case 0xE5: return kVK_RightShift;
    case 0xE6: return kVK_RightOption;
    case 0xE7: return kVK_RightCommand;
    default: return -1;
    }
}

static void attach_keyboard(GCKeyboard* kb) {
    kb.keyboardInput.keyChangedHandler = ^(GCKeyboardInput* in, GCControllerButtonInput*, GCKeyCode keyCode, BOOL pressed) {
        const int code = hid_to_vk(keyCode);
        if (code < 0 || env::NO_HOST_INPUT()) return;
        auto held = [in](GCKeyCode k) { return [in buttonForKeyCode:k].isPressed; };
        const bool shift = held(GCKeyCodeLeftShift) || held(GCKeyCodeRightShift);
        const bool ctrl = held(GCKeyCodeLeftControl) || held(GCKeyCodeRightControl);
        const bool alt = held(GCKeyCodeLeftAlt) || held(GCKeyCodeRightAlt);
        const bool cmd = held(GCKeyCodeLeftGUI) || held(GCKeyCodeRightGUI);
        const int m = (shift ? overlay::kShift : 0) | (ctrl ? overlay::kCtrl : 0) | (alt ? overlay::kAlt : 0) | (cmd ? overlay::kSuper : 0);
        if (overlay::key(code, pressed, false, m)) return;
        if (pressed && screenshot::key_down((uint16_t)code)) return;
        if (cmd) return;
        std::lock_guard<std::mutex> lk(g_mu);
        if (pressed && gfx::menu_hotkey((uint16_t)code)) return;
        g_keys[code] = pressed;
    };
}

void init() {
    input_map::load_startup();
    for (NSNotificationName n in @[ GCKeyboardDidConnectNotification ])
        [[NSNotificationCenter defaultCenter] addObserverForName:n object:nil queue:nil usingBlock:^(NSNotification* note) {
            attach_keyboard((GCKeyboard*)note.object);
        }];
    if (GCKeyboard.coalescedKeyboard) attach_keyboard(GCKeyboard.coalescedKeyboard);
    [[NSNotificationCenter defaultCenter] addObserverForName:UIApplicationWillResignActiveNotification
                                                      object:nil queue:nil usingBlock:^(NSNotification*) {
        std::lock_guard<std::mutex> lk(g_mu);
        memset(g_keys, 0, sizeof(g_keys));
    }];
    [NSTimer scheduledTimerWithTimeInterval:1.0 / 240 repeats:YES block:^(NSTimer*) { poll_controllers(); }];
}
#else
void init() {
    input_map::load_startup();
    start_test_keys();
    NSEventMask mask = NSEventMaskKeyDown | NSEventMaskKeyUp | NSEventMaskFlagsChanged;
    [NSEvent addLocalMonitorForEventsMatchingMask:mask handler:^NSEvent*(NSEvent* e) {
        if (e.modifierFlags & NSEventModifierFlagCommand) return e;
        if (NSApp.keyWindow.sheetParent || NSApp.modalWindow) return e;
        if (gfx::controls_window_is_key()) return e;
        uint16_t code = e.keyCode & 0xFF;
        const bool posted = e.timestamp == kTestKeyTimestamp;
        if (env::NO_HOST_INPUT() && !posted) return e;
        {

            NSEventModifierFlags f = e.modifierFlags;
            int m = (f & NSEventModifierFlagShift ? overlay::kShift : 0) | (f & NSEventModifierFlagControl ? overlay::kCtrl : 0) |
                    (f & NSEventModifierFlagOption ? overlay::kAlt : 0) | (f & NSEventModifierFlagCommand ? overlay::kSuper : 0);
            bool down = e.type == NSEventTypeKeyDown || (e.type == NSEventTypeFlagsChanged && modifier_down(code, f));
            bool repeat = e.type != NSEventTypeFlagsChanged && e.isARepeat;
            if (gfx::text_input_key((__bridge void*)e)) return nil;
            if (overlay::key(code, down, repeat, m)) return nil;

            if (e.type == NSEventTypeKeyDown && !e.isARepeat && screenshot::key_down(code)) return nil;
            if (posted) return nil;
        }
        std::lock_guard<std::mutex> lk(g_mu);
        if (e.type == NSEventTypeKeyDown && !e.isARepeat && gfx::menu_hotkey(code)) return nil;

        if (e.type == NSEventTypeKeyDown) g_keys[code] = g_keys[code] || !e.isARepeat;
        else if (e.type == NSEventTypeKeyUp) g_keys[code] = false;
        else g_keys[code] = modifier_down(code, e.modifierFlags);
        return nil;
    }];

    [[NSNotificationCenter defaultCenter] addObserverForName:NSApplicationDidResignActiveNotification
                                                      object:nil queue:nil usingBlock:^(NSNotification*) {
        std::lock_guard<std::mutex> lk(g_mu);
        memset(g_keys, 0, sizeof(g_keys));
    }];
    [GCController startWirelessControllerDiscoveryWithCompletionHandler:nil];

    [NSTimer scheduledTimerWithTimeInterval:1.0 / 240 repeats:YES block:^(NSTimer*) { poll_controllers(); }];
}
#endif

struct Press { uint64_t from, to; uint32_t bits; };
static std::vector<Press> scripted() {
    std::vector<Press> v;
    if (const char* e = env::PRESS()) {
        unsigned long long a, b; unsigned bits; int n;
        while (sscanf(e, "%llu-%llu:%x%n", &a, &b, &bits, &n) == 3) {
            v.push_back({a, b, bits});
            e += n;
            if (*e != ',') break;
            e++;
        }
    }
    return v;
}

struct KeyPress { uint64_t from, to; std::vector<int> codes; };
static std::vector<KeyPress> scripted_keys() {
    std::vector<KeyPress> v;
    if (const char* e = env::KEYS()) {
        unsigned long long a, b; int n;
        while (sscanf(e, "%llu-%llu:%n", &a, &b, &n) == 2) {
            e += n;
            KeyPress kp{a, b, {}};
            for (;;) {
                size_t len = strcspn(e, "+,");
                int code = input_map::key_from_id(std::string(e, len));
                if (code == input_map::kNoKey) LOG("[input] NSMBU_KEYS: unknown key %.*s", (int)len, e);
                else kp.codes.push_back(code);
                e += len;
                if (*e != '+') break;
                e++;
            }
            v.push_back(kp);
            if (*e != ',') break;
            e++;
        }
    }
    return v;
}

struct Stick { uint64_t from, to; float x, y; };
static std::vector<Stick> scripted_stick(env::Var var = env::STICK) {
    std::vector<Stick> v;
    if (const char* e = (var)()) {
        unsigned long long a, b; float x, y; int n;
        while (sscanf(e, "%llu-%llu:%f:%f%n", &a, &b, &x, &y, &n) == 4) {
            v.push_back({a, b, x, y});
            e += n;
            if (*e != ',') break;
            e++;
        }
    }
    return v;
}

namespace {
struct TimedStick { double from, to; float x, y; };
struct TimedPress { double from, to; uint32_t bits; };
struct Scenario {
    uint64_t origin = 0;
    std::vector<TimedStick> sticks, rsticks;
    std::vector<TimedPress> presses;
    int mode = -1;
    double mode_at = 0, end = 0;
    Scenario() {
        if (const char* e = env::TEST_ORIGIN()) origin = strtoull(e, nullptr, 10);
        auto timed_sticks = [](const char* e, std::vector<TimedStick>& out) {
            double a, b; float x, y; int n;
            while (e && sscanf(e, "%lf-%lf:%f:%f%n", &a, &b, &x, &y, &n) == 4) {
                out.push_back({a, b, x, y});
                e += n;
                if (*e != ',') break;
                e++;
            }
        };
        timed_sticks(env::TEST_STICK(), sticks);
        timed_sticks(env::TEST_RSTICK(), rsticks);
        if (const char* e = env::TEST_PRESS()) {
            double a, b; unsigned bits; int n;
            while (sscanf(e, "%lf-%lf:%x%n", &a, &b, &bits, &n) == 3) {
                presses.push_back({a, b, bits});
                e += n;
                if (*e != ',') break;
                e++;
            }
        }
        if (const char* e = env::TEST_MODE()) sscanf(e, "%d@%lf", &mode, &mode_at);
        if (const char* e = env::TEST_END()) end = atof(e);
    }
};
}
}
namespace interp { void set_mode(int m); uint64_t logic_steps(); }
namespace input {

static bool g_test_touch = false;
static float g_test_tx = 0, g_test_ty = 0;
static void apply_scenario(PadState& s) {
    static Scenario sc;

    if (render::frame_count() < sc.origin) return;

    static const uint64_t s0 = [] {
        LOG("[test] origin at guest time %.4f s, logic step %llu", (double)timebase::now() / timebase::kTicksPerSec,
            (unsigned long long)interp::logic_steps());
        return interp::logic_steps();
    }();

    double t = (double)(interp::logic_steps() - s0) / 30.0;
    static std::atomic<bool> mode_set{false}, ended{false};
    static std::atomic<int> dbg{0};
    if (env::TEST_DEBUG() && dbg++ % 30 == 0) LOG("[test] t=%.3f frame %llu", t, (unsigned long long)render::frame_count());
    if (sc.mode >= 0 && t >= sc.mode_at && !mode_set.exchange(true)) {
        LOG("[test] t=%.3f s: 60 fps mode %d", t, sc.mode);
        interp::set_mode(sc.mode);
    }

    static std::vector<std::pair<double, int>> modes = [] {
        std::vector<std::pair<double, int>> v;
        for (const char* e = env::TEST_MODES(); e && *e;) {
            int m; double at; int n;
            if (sscanf(e, "%d@%lf%n", &m, &at, &n) != 2) break;
            v.push_back({at, m});
            e += n;
            if (*e != ',') break;
            e++;
        }
        return v;
    }();
    for (auto& [at, m] : modes)
        if (m >= 0 && t >= at) {
            LOG("[test] t=%.3f s: 60 fps mode %d", t, m);
            interp::set_mode(m);
            m = -1;
        }
    if (sc.end > 0 && t >= sc.end && !ended.exchange(true)) {
        LOG("[test] t=%.3f s: end", t);
        if (FILE* f = fopen("test_done", "w")) fclose(f);
    }
    for (auto& p : sc.sticks)
        if (t >= p.from && t < p.to) { s.lx = p.x; s.ly = p.y; }
    for (auto& p : sc.rsticks)
        if (t >= p.from && t < p.to) { s.rx = p.x; s.ry = p.y; }
    for (auto& p : sc.presses)
        if (t >= p.from && t < p.to) s.buttons |= p.bits;
    static std::vector<TimedStick> touches = [] {
        std::vector<TimedStick> v;
        const char* e = env::TEST_TOUCH();
        double a, b; float x, y; int n;
        while (e && sscanf(e, "%lf-%lf:%f:%f%n", &a, &b, &x, &y, &n) == 4) {
            v.push_back({a, b, x, y});
            e += n;
            if (*e != ',') break;
            e++;
        }
        return v;
    }();
    g_test_touch = false;
    for (auto& p : touches)
        if (t >= p.from && t < p.to) g_test_touch = true, g_test_tx = p.x, g_test_ty = p.y;
}

static std::atomic<bool> g_pro{env::PRO_CONTROLLER() != nullptr};
bool pro_controller() { return g_pro.load(std::memory_order_relaxed); }
void set_pro_controller(bool on) { g_pro = on; LOG("[input] keyboard/controller act as %s", on ? "Pro Controller" : "GamePad"); }

PadState read(int player) {
    player = std::clamp(player, 0, input::kMaxPlayers - 1);
    static const std::vector<Press> script = scripted();
    static const std::vector<Stick> sticks = scripted_stick();
    static const std::vector<KeyPress> keys = scripted_keys();
    static const std::vector<Stick> rsticks = scripted_stick(env::RSTICK);
    std::lock_guard<std::mutex> lk(g_mu);

    static const bool no_host = env::NO_HOST_INPUT() != nullptr;
    PadState s = g_pads[player];
    if (no_host) s = PadState{};
    if (player == 0) {
        const uint64_t sf = script_frame::frame(render::frame_count());
        if (!keys.empty()) {
            memset(g_script_keys, 0, sizeof g_script_keys);
            for (auto& p : keys)
                if (sf >= p.from && sf <= p.to)
                    for (int c : p.codes) g_script_keys[c] = true;
        }
        PadState k = keyboard_state(!no_host);
        for (auto& p : script)
            if (sf >= p.from && sf <= p.to) s.buttons |= p.bits;
        for (auto& p : sticks)
            if (sf >= p.from && sf <= p.to) { s.lx = p.x; s.ly = p.y; }
        for (auto& p : rsticks)
            if (sf >= p.from && sf <= p.to) { s.rx = p.x; s.ry = p.y; }
        apply_scenario(s);
        s.buttons |= k.buttons;
        if (k.lx || k.ly) { s.lx = k.lx; s.ly = k.ly; }
        if (k.rx || k.ry) { s.rx = k.rx; s.ry = k.ry; }
        s.touch = g_touch;
        s.tx = g_tx;
        s.ty = g_ty;
        if (g_test_touch) s.touch = true, s.tx = g_test_tx, s.ty = g_test_ty;

        static const bool log_buttons = env::LOG_BUTTONS() != nullptr;
        static uint32_t last_buttons = 0;
        if (log_buttons && s.buttons != last_buttons) {
            LOG("[input] frame %llu buttons %04X", (unsigned long long)render::frame_count(), s.buttons);
            last_buttons = s.buttons;
        }
    }
    if (overlay::blocks_input()) s = PadState{};
    return s;
}

PadState read() { return read(0); }

#if TARGET_OS_IPHONE
void prompt_text(const std::u16string& initial, int max_len,
                 std::function<void(bool ok, std::u16string text)> done) {
    NSString* init = [NSString stringWithCharacters:(const unichar*)initial.data() length:initial.size()];
    dispatch_async(dispatch_get_main_queue(), ^{
        {
            std::lock_guard<std::mutex> lk(g_mu);
            memset(g_keys, 0, sizeof(g_keys));
        }
        UIAlertController* alert = [UIAlertController alertControllerWithTitle:@"Enter text"
                                                                       message:[NSString stringWithFormat:@"Up to %d characters.", max_len]
                                                                preferredStyle:UIAlertControllerStyleAlert];
        [alert addTextFieldWithConfigurationHandler:^(UITextField* f) { f.text = init; }];
        auto finish = [done, max_len, alert](bool ok) {
            NSString* t = alert.textFields.firstObject.text ?: @"";
            std::u16string out(t.length, u'\0');
            [t getCharacters:(unichar*)out.data() range:NSMakeRange(0, t.length)];
            if ((int)out.size() > max_len) out.resize(max_len);
            done(ok, out);
        };
        [alert addAction:[UIAlertAction actionWithTitle:@"OK" style:UIAlertActionStyleDefault handler:^(UIAlertAction*) { finish(true); }]];
        [alert addAction:[UIAlertAction actionWithTitle:@"Cancel" style:UIAlertActionStyleCancel handler:^(UIAlertAction*) { finish(false); }]];
        gfx::ios_present_alert(alert);
    });
}
#else
void prompt_text(const std::u16string& initial, int max_len,
                 std::function<void(bool ok, std::u16string text)> done) {
    NSString* init = [NSString stringWithCharacters:(const unichar*)initial.data() length:initial.size()];
    dispatch_async(dispatch_get_main_queue(), ^{
        {
            std::lock_guard<std::mutex> lk(g_mu);
            memset(g_keys, 0, sizeof(g_keys));
        }
        NSAlert* alert = [[NSAlert alloc] init];
        alert.messageText = @"Enter text";
        alert.informativeText = [NSString stringWithFormat:@"Up to %d characters.", max_len];
        [alert addButtonWithTitle:@"OK"];
        [alert addButtonWithTitle:@"Cancel"];
        NSTextField* field = [[NSTextField alloc] initWithFrame:NSMakeRect(0, 0, 260, 24)];
        field.stringValue = init;
        alert.accessoryView = field;
        alert.window.initialFirstResponder = field;
        auto finish = [done, max_len, field](NSModalResponse r) {
            NSString* t = field.stringValue;
            std::u16string out(t.length, u'\0');
            [t getCharacters:(unichar*)out.data() range:NSMakeRange(0, t.length)];
            if ((int)out.size() > max_len) out.resize(max_len);
            done(r == NSAlertFirstButtonReturn, out);
        };
        NSWindow* parent = NSApp.mainWindow ?: NSApp.windows.firstObject;
        if (parent) [alert beginSheetModalForWindow:parent completionHandler:^(NSModalResponse r) { finish(r); }];
        else finish([alert runModal]);
    });
}
#endif

}
