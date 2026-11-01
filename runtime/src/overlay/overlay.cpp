#include <mutex>

#include "app_title.h"
#include "overlay.h"
#include "perf_average.h"
#include "core/env.h"
#ifdef __ANDROID__
#include "android_telemetry.h"
#endif

#include <algorithm>
#include <cstdarg>
#include <functional>
#include <atomic>
#include <chrono>
#include <cfloat>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <mutex>
#include <string>
#include <vector>

#include "imgui.h"
#ifdef __ANDROID__
#include "imgui_internal.h"
#endif
#include "hostui.h"
#include "controls_view.h"
#include "flow_layout.h"
#include "controller_shortcuts.h"
#include "text_entry.h"
#include "gfx/aspect.h"
#include "gfx/display_modes.h"
#include "gfx/res_presets.h"
#include "crash/crashrec.h"
#include "text/game_languages.h"
#include "../gfx/renderer.h"
#ifdef __ANDROID__
#include "../gfx/vulkan/android_driver.h"
#include <SDL3/SDL_system.h>
#include <jni.h>
#include "../platform/heat_guard.h"
#include "../platform/phone_rumble.h"
#include "../platform/touch_buttons.h"
#endif
#ifdef NSMBU_HAS_VULKAN
#include "../gfx/vulkan/settings.h"
#include "../gfx/vulkan/device_choice.h"
namespace gfxvk { bool buffer_cache_enabled(); }
#endif
#include "input/input.h"
#include "input/input_map.h"
#include "controller_shortcuts.h"
#include "gamepad_nav.h"
#include "mobile_ui.h"
#include "../mods/mods.h"
#include "../mods/sdk/nsmbu.h"
#include "../mods/lua_gui.h"
#include "../mods/packages.h"
#include "../input/motion/motion.h"
#include "../platform/keycodes.h"
#include "audio/audio_out.h"
#include "input/rumble.h"
#include "hooks/interp.h"
#include "hooks/interp_pacing.h"
#include "hooks/collision_wireframe.h"
#include "hooks/course_hot_reload.h"
#include "core/runtime.h"
#include "save/savestate.h"
#include "save/save_preview.h"
#include "gfx/screenshot.h"
#include "gfx/render_prof.h"
#include "perf_metrics.h"
#include "core/build_info.h"
#include "crash/report_header.h"

namespace gx2 { uint64_t flips_presented(); bool uncapped(); void set_uncapped(bool on); }

namespace overlay {
namespace {

using clock = std::chrono::steady_clock;
double now_s() { return std::chrono::duration<double>(clock::now().time_since_epoch()).count(); }

std::atomic<bool> g_open{false};
std::atomic<bool> g_perf{false};
std::atomic<bool> g_perf_timing{false};
std::atomic<bool> g_perf_presentation{false};
std::atomic<bool> g_perf_detail{false};
PerfAverage g_average;
std::atomic<float> g_density{1.0f};
std::atomic<bool> g_wait_release{false};
std::atomic<double> g_last_frame{0};
const bool g_no_host = env::NO_HOST_INPUT() != nullptr;

const bool g_no_host_keys = g_no_host && !env::TEST_POST_KEYS();
bool g_pad_b_used = false;

struct Event {
    enum Kind { Key, MousePos, MouseButton, Wheel, Text, Focus } kind;
    int code = 0;
    bool down = false;
    float x = 0, y = 0;
    int mods = 0;
    bool touch = false;
    std::string text;
};
std::mutex g_mu;
std::vector<Event> g_events;
std::atomic<int> g_capture_key{-2};
std::atomic<bool> g_capturing_keys{false};

std::atomic<bool> g_held_keys[256];

void push(const Event& e) {
    std::lock_guard<std::mutex> lk(g_mu);
    if (g_events.size() < 4096) g_events.push_back(e);
}

ImGuiKey imgui_key(int code) {
    switch (code) {
    case kVK_ANSI_A: return ImGuiKey_A; case kVK_ANSI_B: return ImGuiKey_B; case kVK_ANSI_C: return ImGuiKey_C;
    case kVK_ANSI_D: return ImGuiKey_D; case kVK_ANSI_E: return ImGuiKey_E; case kVK_ANSI_F: return ImGuiKey_F;
    case kVK_ANSI_G: return ImGuiKey_G; case kVK_ANSI_H: return ImGuiKey_H; case kVK_ANSI_I: return ImGuiKey_I;
    case kVK_ANSI_J: return ImGuiKey_J; case kVK_ANSI_K: return ImGuiKey_K; case kVK_ANSI_L: return ImGuiKey_L;
    case kVK_ANSI_M: return ImGuiKey_M; case kVK_ANSI_N: return ImGuiKey_N; case kVK_ANSI_O: return ImGuiKey_O;
    case kVK_ANSI_P: return ImGuiKey_P; case kVK_ANSI_Q: return ImGuiKey_Q; case kVK_ANSI_R: return ImGuiKey_R;
    case kVK_ANSI_S: return ImGuiKey_S; case kVK_ANSI_T: return ImGuiKey_T; case kVK_ANSI_U: return ImGuiKey_U;
    case kVK_ANSI_V: return ImGuiKey_V; case kVK_ANSI_W: return ImGuiKey_W; case kVK_ANSI_X: return ImGuiKey_X;
    case kVK_ANSI_Y: return ImGuiKey_Y; case kVK_ANSI_Z: return ImGuiKey_Z;
    case kVK_ANSI_0: return ImGuiKey_0; case kVK_ANSI_1: return ImGuiKey_1; case kVK_ANSI_2: return ImGuiKey_2;
    case kVK_ANSI_3: return ImGuiKey_3; case kVK_ANSI_4: return ImGuiKey_4; case kVK_ANSI_5: return ImGuiKey_5;
    case kVK_ANSI_6: return ImGuiKey_6; case kVK_ANSI_7: return ImGuiKey_7; case kVK_ANSI_8: return ImGuiKey_8;
    case kVK_ANSI_9: return ImGuiKey_9;
    case kVK_Return: case kVK_ANSI_KeypadEnter: return ImGuiKey_Enter;
    case kVK_Tab: return ImGuiKey_Tab;
    case kVK_Space: return ImGuiKey_Space;
    case kVK_Delete: return ImGuiKey_Backspace;
    case kVK_ForwardDelete: return ImGuiKey_Delete;
    case kVK_Escape: return ImGuiKey_Escape;
    case kVK_LeftArrow: return ImGuiKey_LeftArrow; case kVK_RightArrow: return ImGuiKey_RightArrow;
    case kVK_UpArrow: return ImGuiKey_UpArrow; case kVK_DownArrow: return ImGuiKey_DownArrow;
    case kVK_Home: return ImGuiKey_Home; case kVK_End: return ImGuiKey_End;
    case kVK_PageUp: return ImGuiKey_PageUp; case kVK_PageDown: return ImGuiKey_PageDown;
    case kVK_Shift: return ImGuiKey_LeftShift; case kVK_RightShift: return ImGuiKey_RightShift;
    case kVK_Control: return ImGuiKey_LeftCtrl; case kVK_RightControl: return ImGuiKey_RightCtrl;
    case kVK_Option: return ImGuiKey_LeftAlt; case kVK_RightOption: return ImGuiKey_RightAlt;
    case kVK_Command: return ImGuiKey_LeftSuper; case kVK_RightCommand: return ImGuiKey_RightSuper;
    default: return ImGuiKey_None;
    }
}

enum Tab { kControls, kGraphics, kDisplay, kAudio, kSaves, kMods, kAbout, kDebug,
#ifdef __ANDROID__
           kAndroid,
#endif
           kTabs };
#ifdef __ANDROID__
const char* const kTabNames[kTabs] = {"Controls", "Graphics", "Display", "Audio", "Saves", "Mods", "Language", "Debug", "Android"};
const char* const kTabIds[kTabs] = {"controls", "graphics", "display", "audio", "saves", "mods", "about", "debug", "android"};
#else
const char* const kTabNames[kTabs] = {"Controls", "Graphics", "Display", "Audio", "Saves", "Mods", "Language", "Debug"};
const char* const kTabIds[kTabs] = {"controls", "graphics", "display", "audio", "saves", "mods", "about", "debug"};
#endif

struct Ui {
    bool init = false;
    int tab = kControls, select_tab = -1;
    double last_time = 0;

    float values[input_map::kPadCount] = {};
    float prev[input_map::kPadCount] = {};
    float nav_values[input_map::kPadCount] = {};
    float nav_prev[input_map::kPadCount] = {};
    GamepadNavigation gamepad_navigation;
    ImVec2 mouse_position = {};
    double options_since = -1;
    bool options_latched = false;

    int cap_action = -1, cap_col = 0, cap_player = 0;
    bool cap_pad_released = false;
    double cap_started = 0;
    std::string cap_note;

    ss::SlotInfo slots[ss::kSlots + 1];
    crashrec::AutoInfo autos[crashrec::kAutoSlots + 1];
    save_preview::Info game_save;
    double slots_time = -1;

    double fps_t0 = 0, fps = 0;
    uint64_t fps_n0 = 0;
    float frame_ms[FrameHistoryAverages::kFrames] = {};
    uint32_t frame_logic[FrameHistoryAverages::kFrames] = {};
    int frame_i = 0;
    double last_present = 0;
    double sample_restore = 0;
    bool sample_undo = false;

    int language = -1;
    int language_at_start = -1;
    bool restart_confirm = false;
    std::string restart_why;
    bool linearized = false;
    bool just_opened = false;
    bool list_view = false;
    int hover_action = -1;
    int control_player = 0;
    float options_height = 0;
};
Ui U;

bool controller_down(int p) { return U.values[p] > 0.5f; }
bool controller_pressed(int p) { return U.values[p] > 0.5f && U.prev[p] <= 0.5f; }

void post_changed(std::function<void()> fn) {
    hostui::post([fn] {
        fn();
        hostui::graphics_changed();
    });
}

void setup_style() {
    ImGuiStyle& s = ImGui::GetStyle();
    ImGui::StyleColorsDark(&s);
    s.WindowRounding = 10;
    s.ChildRounding = 6;
    s.FrameRounding = 5;
    s.PopupRounding = 6;
    s.GrabRounding = 5;
    s.TabRounding = 6;
    s.WindowPadding = ImVec2(16, 14);
    s.FramePadding = ImVec2(9, 5);
    s.ItemSpacing = ImVec2(10, 7);
    s.WindowBorderSize = 1;
    s.FrameBorderSize = 1;
    s.WindowTitleAlign = ImVec2(0.5f, 0.5f);
    ImVec4* c = s.Colors;

    c[ImGuiCol_WindowBg] = ImVec4(0.03f, 0.07f, 0.12f, 1.0f);
    c[ImGuiCol_ChildBg] = ImVec4(0.06f, 0.13f, 0.20f, 0.35f);
    c[ImGuiCol_PopupBg] = ImVec4(0.05f, 0.11f, 0.18f, 0.98f);
    c[ImGuiCol_Border] = ImVec4(0.30f, 0.62f, 0.70f, 0.45f);
    c[ImGuiCol_TitleBg] = c[ImGuiCol_WindowBg];
    c[ImGuiCol_TitleBgActive] = c[ImGuiCol_WindowBg];
    c[ImGuiCol_TitleBgCollapsed] = c[ImGuiCol_WindowBg];
    c[ImGuiCol_FrameBg] = ImVec4(0.10f, 0.22f, 0.32f, 0.85f);
    c[ImGuiCol_FrameBgHovered] = ImVec4(0.14f, 0.34f, 0.46f, 0.90f);
    c[ImGuiCol_FrameBgActive] = ImVec4(0.17f, 0.42f, 0.54f, 1.0f);
    c[ImGuiCol_Button] = ImVec4(0.12f, 0.36f, 0.48f, 0.90f);
    c[ImGuiCol_ButtonHovered] = ImVec4(0.18f, 0.50f, 0.62f, 1.0f);
    c[ImGuiCol_ButtonActive] = ImVec4(0.22f, 0.60f, 0.70f, 1.0f);
    c[ImGuiCol_Header] = ImVec4(0.12f, 0.36f, 0.48f, 0.70f);
    c[ImGuiCol_HeaderHovered] = ImVec4(0.18f, 0.50f, 0.62f, 0.85f);
    c[ImGuiCol_HeaderActive] = ImVec4(0.22f, 0.60f, 0.70f, 1.0f);
    c[ImGuiCol_Tab] = ImVec4(0.08f, 0.24f, 0.34f, 0.90f);
    c[ImGuiCol_TabHovered] = ImVec4(0.20f, 0.52f, 0.64f, 1.0f);
    c[ImGuiCol_TabSelected] = ImVec4(0.16f, 0.44f, 0.56f, 1.0f);
    c[ImGuiCol_CheckMark] = ImVec4(0.55f, 0.95f, 0.85f, 1.0f);
    c[ImGuiCol_SliderGrab] = ImVec4(0.40f, 0.80f, 0.80f, 1.0f);
    c[ImGuiCol_SliderGrabActive] = ImVec4(0.55f, 0.95f, 0.90f, 1.0f);
    c[ImGuiCol_Separator] = ImVec4(0.30f, 0.62f, 0.70f, 0.35f);
    c[ImGuiCol_NavCursor] = ImVec4(1.0f, 0.85f, 0.35f, 1.0f);
    c[ImGuiCol_TableHeaderBg] = ImVec4(0.08f, 0.24f, 0.34f, 1.0f);
    c[ImGuiCol_TableRowBgAlt] = ImVec4(1, 1, 1, 0.03f);
}

void setup_fonts() {
    ImGuiIO& io = ImGui::GetIO();
    static const ImWchar glyphs[] = {
        0x0020, 0x00FF,
        0x2010, 0x2027,
        0x2190, 0x2193,
        0x2212, 0x2212,
        0x2302, 0x2302,
        0x2326, 0x2326,
        0x232B, 0x232B,
        0,
    };
    static const char* const candidates[] = {
#if defined(__APPLE__)
        "/System/Library/Fonts/Supplemental/Arial Unicode.ttf",
        "/System/Library/Fonts/Supplemental/Arial.ttf",
#elif defined(_WIN32)
        "C:/Windows/Fonts/segoeui.ttf",
        "C:/Windows/Fonts/arial.ttf",
#elif defined(__ANDROID__)
        "/system/fonts/RobotoStatic-Regular.ttf",
        "/system/fonts/Roboto-Regular.ttf",
        "/system/fonts/DroidSans.ttf",
#else
        "/usr/share/fonts/truetype/dejavu/DejaVuSans.ttf",
        "/usr/share/fonts/TTF/DejaVuSans.ttf",
        "/usr/share/fonts/dejavu-sans-fonts/DejaVuSans.ttf",
        "/usr/share/fonts/truetype/noto/NotoSans-Regular.ttf",
        "/usr/share/fonts/noto/NotoSans-Regular.ttf",
#endif
    };
    ImFont* font = nullptr;
    for (const char* path : candidates) {
        if (FILE* f = fopen(path, "rb")) {
            fclose(f);
            font = io.Fonts->AddFontFromFileTTF(path, 17.0f, nullptr, glyphs);
            if (font) {
                LOG("[overlay] font %s", path);
                break;
            }
        }
    }
#ifdef __ANDROID__
    if (font) {
        const char* symbols = "/system/fonts/NotoSansSymbols-Regular-Subsetted.ttf";
        if (FILE* f = fopen(symbols, "rb")) {
            fclose(f);
            ImFontConfig merge;
            merge.MergeMode = true;
            if (io.Fonts->AddFontFromFileTTF(symbols, 17.0f, &merge, glyphs))
                LOG("[overlay] symbol font %s", symbols);
        }
    }
#endif
    if (font) return;
    ImFontConfig cfg;
    cfg.SizePixels = 17.0f;
    io.Fonts->AddFontDefaultVector(&cfg);
}

void init_context() {
    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImGuiIO& io = ImGui::GetIO();
    io.IniFilename = nullptr;
    io.LogFilename = nullptr;
    io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard | ImGuiConfigFlags_NavEnableGamepad;
    io.BackendFlags |= ImGuiBackendFlags_HasGamepad;
    io.BackendPlatformName = "nsmbu";
    io.ConfigNavCaptureKeyboard = true;
    setup_style();
    setup_fonts();
    ImGui::GetStyle().FontSizeBase = 17.0f;
    U.init = true;
}

struct TestSwitch {
    bool open = false, perf = false;
    int tab = -1;
    uint64_t at = 1;
    bool done = false;
};
TestSwitch parse_test() {
    TestSwitch t;
    const char* e = env::TEST_OVERLAY();
    if (!e || !*e) { t.done = true; return t; }
    std::string s = e;
    if (size_t at = s.find('@'); at != std::string::npos) {
        t.at = strtoull(s.c_str() + at + 1, nullptr, 10);
        s.resize(at);
    }
    std::string what = s, tab;
    if (size_t c = s.find(':'); c != std::string::npos) { what = s.substr(0, c); tab = s.substr(c + 1); }
    t.open = what == "open";
    t.perf = what == "perf";
    for (int i = 0; i < kTabs; i++)
        if (tab == kTabIds[i]) t.tab = i;
    return t;
}

void test_pad(float* v) {
    struct Hold { uint64_t from, to; int pad; float value; };
    static const std::vector<Hold> holds = [] {
        std::vector<Hold> out;
        const char* e = env::TEST_PAD();
        while (e && *e) {
            unsigned long long a, b;
            int n;
            if (sscanf(e, "%llu-%llu:%n", &a, &b, &n) != 2) break;
            e += n;
            for (;;) {
                size_t len = strcspn(e, "+,");
                std::string item(e, len), id = item;
                float value = 1;
                if (size_t c = item.find(':'); c != std::string::npos) { id = item.substr(0, c); value = (float)atof(item.c_str() + c + 1); }
                int p = input_map::pad_from_id(id);
                if (p > 0) out.push_back({a, b, p, value});
                else LOG("[overlay] NSMBU_TEST_PAD: unknown controller input %s", id.c_str());
                e += len;
                if (*e != '+') break;
                e++;
            }
            if (*e != ',') break;
            e++;
        }
        return out;
    }();
    const uint64_t f = render::frame_count();
    for (const Hold& h : holds)
        if (f >= h.from && f <= h.to) v[h.pad] = std::max(v[h.pad], h.value);
}

void read_controller() {
    std::copy(std::begin(U.values), std::end(U.values), std::begin(U.prev));
    if (g_no_host) {
        std::fill(std::begin(U.values), std::end(U.values), 0.0f);
        if (g_wait_release.load()) g_wait_release = false;
        return;
    }
    input::host_controller_values(U.values);
    input_map::deadzone_values(input_map::current(), U.values);
    using namespace input_map;
    const double t = now_s();

    if (U.cap_action < 0 &&
        overlay_shortcuts::settings_controller_press(controller_pressed(kPadHome), controller_pressed(kPadR3)))
        set_open(!is_open());

    const bool menu = controller_down(kPadMenu);
    const bool options = controller_down(kPadOptions);
    const bool prev_menu = U.prev[kPadMenu] > 0.5f;
    const bool prev_options = U.prev[kPadOptions] > 0.5f;
    const bool pad_combo = overlay_shortcuts::combo_edge(menu, options, prev_menu, prev_options);

    bool keys[256];
    input::held_keys(keys);
    const bool kb_combo = overlay_shortcuts::keyboard_start_select(input_map::current(), keys);
    static bool prev_kb_combo = false;
    const bool kb_edge = kb_combo && !prev_kb_combo;
    prev_kb_combo = kb_combo;

    if (!g_capturing_keys.load() && (pad_combo || kb_edge)) set_open(!is_open());

    if (menu && options) {
        U.options_since = -1;
        U.options_latched = true;
    } else if (controller_down(kPadOptions)) {
        if (!U.options_latched) {
            if (is_open()) {
                set_open(false);
                U.options_latched = true;
            } else {
                if (U.options_since < 0) U.options_since = t;
                if (t - U.options_since >= 0.5) {
                    set_open(true);
                    U.options_latched = true;
                }
            }
        }
    } else {
        U.options_since = -1;
        U.options_latched = false;
    }
    if (g_wait_release.load()) {
        bool any = false;
        for (int p = 1; p < kPadCount; p++) any |= U.values[p] > 0.3f;
        if (!any) g_wait_release = false;
    }
}

void feed_gamepad(ImGuiIO& io, bool enabled, bool mouse_used) {
    const int face[4] = {input_map::face_input(input_map::kA), input_map::face_input(input_map::kB),
                         input_map::face_input(input_map::kX), input_map::face_input(input_map::kY)};
    std::copy(std::begin(U.prev), std::end(U.prev), std::begin(U.nav_prev));
    std::copy(std::begin(U.values), std::end(U.values), std::begin(U.nav_values));
    U.gamepad_navigation.feed(io, U.nav_values, U.nav_prev, enabled, mouse_used, face);
}

void wrapped_tooltip(const char* text) {
    ImGui::BeginTooltip();
    ImGui::PushTextWrapPos(ImGui::GetFontSize() * 30.0f);
    ImGui::TextUnformatted(text);
    ImGui::PopTextWrapPos();
    ImGui::EndTooltip();
}
void help(const char* text) {
    if (ImGui::IsItemHovered(ImGuiHoveredFlags_DelayShort | ImGuiHoveredFlags_ForTooltip)) wrapped_tooltip(text);
}
void heading(const char* text) {
    ImGui::Spacing();
    ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.55f, 0.95f, 0.85f, 1.0f));
    ImGui::SeparatorText(text);
    ImGui::PopStyleColor();
}
void note(const char* fmt, ...) __attribute__((format(printf, 1, 2)));
void note(const char* fmt, ...) {
    va_list ap;
    va_start(ap, fmt);
    ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.70f, 0.78f, 0.84f, 1.0f));
    ImGui::TextWrappedV(fmt, ap);
    ImGui::PopStyleColor();
    va_end(ap);
}
void warn(const char* fmt, ...) __attribute__((format(printf, 1, 2)));
void warn(const char* fmt, ...) {
    va_list ap;
    va_start(ap, fmt);
    ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(1.0f, 0.80f, 0.35f, 1.0f));
    ImGui::TextWrappedV(fmt, ap);
    ImGui::PopStyleColor();
    va_end(ap);
}

bool radio(const char* label, bool active, bool enabled = true) {
    ImGui::BeginDisabled(!enabled);
    bool clicked = ImGui::RadioButton(label, active) && !active;
    ImGui::EndDisabled();
    return clicked;
}
bool check(const char* label, bool value, bool* out, bool enabled = true) {
    ImGui::BeginDisabled(!enabled);
    bool v = value;
    bool changed = ImGui::Checkbox(label, &v);
    ImGui::EndDisabled();
    *out = v;
    return changed;
}

void refresh_slots(bool force) {
    double t = now_s();
    if (!force && U.slots_time >= 0 && t - U.slots_time < 0.5) return;
    U.slots_time = t;
    for (int i = 1; i <= ss::kSlots; i++) U.slots[i] = ss::slot_info(i);
    for (int i = 1; i <= crashrec::kAutoSlots; i++) U.autos[i] = crashrec::auto_info(i);
    U.game_save = save_preview::inspect_dir(config::save_dir);
}

void tab_saves() {
    refresh_slots(false);
    note("%s", save_preview::summary(U.game_save).c_str());
    if (hostui::can_open_folder() && !config::save_dir.empty()) {
        if (ImGui::Button("Open saves folder")) {
            std::string d = config::save_dir;
            hostui::post([d] { hostui::open_folder(d); });
        }
        help("Your game save files (rp_personal_savedata.dat and rp_savedata.dat), not save states.");
        ImGui::SameLine();
        ImGui::AlignTextToFramePadding();
        ImGui::TextDisabled("%s", config::save_dir.c_str());
    } else if (!config::save_dir.empty()) {
        note("Saves folder: %s", config::save_dir.c_str());
    }
    std::string msg = ss::last_message();
    if (!msg.empty()) note("%s", msg.c_str());
    heading("Save states");
    const bool full = ss::full_states();
    if (full)
        note("Full save states: the whole running game (large, contain game data: never share them). Shift+F1..F5 save in "
             "game, F1..F5 load (` or F12 opens this menu).");
    else
        note("A save state keeps your progress and where the player stands (a few KB, no game data). Loading enters that place "
             "with that progress; enemies and cutscenes start fresh. Shift+F1..F5 save in game, F1..F5 load (` or F12 opens this menu).");
    if (ImGui::BeginTable("slots", 3, ImGuiTableFlags_RowBg | ImGuiTableFlags_BordersInnerH | ImGuiTableFlags_SizingStretchProp)) {
        ImGui::TableSetupColumn("Slot", ImGuiTableColumnFlags_WidthFixed);
        ImGui::TableSetupColumn("Saved");
        ImGui::TableSetupColumn("", ImGuiTableColumnFlags_WidthFixed);
        for (int i = 1; i <= ss::kSlots; i++) {
            const ss::SlotInfo& s = U.slots[i];
            ImGui::PushID(i);
            ImGui::TableNextRow();
            ImGui::TableNextColumn();
            ImGui::AlignTextToFramePadding();
            ImGui::Text("Slot %d", i);
            ImGui::TableNextColumn();
            if (!s.used) ImGui::TextDisabled("empty");
            else {
                std::string d = s.when + (s.area.empty() ? "" : "  -  " + s.area);
                if (!s.controller.empty()) d += "  -  " + s.controller;
                if (!s.portable) d += "  (full)";
                if (!s.compatible) d += "  (incompatible)";
                ImGui::TextUnformatted(d.c_str());
                if (s.older_other && s.portable) {

                    ImGui::TextDisabled("also holds an older full state (slot%d.bin, %.0f MB)", i, s.older_bytes / 1048576.0);
                    if (ImGui::IsItemHovered(ImGuiHoveredFlags_DelayShort | ImGuiHoveredFlags_ForTooltip))
                        wrapped_tooltip("Kept on disk, not loaded: the newer portable state is. It contains game data: "
                                        "don't share it. Delete it in the states folder if you no longer need it.");
                } else if (s.older_other) {
                    ImGui::TextDisabled("also holds an older portable state (slot%d.nsstate)", i);
                }
            }
            ImGui::TableNextColumn();
            if (ImGui::Button("Save")) {
                ss::request_save(i);
                U.slots_time = -1;
            }
            ImGui::SameLine();
            ImGui::BeginDisabled(!s.used || !s.compatible);
            if (ImGui::Button("Load")) {
                ss::request_load(i);
                set_open(false);
            }
            ImGui::EndDisabled();
            ImGui::PopID();
        }
        ImGui::EndTable();
    }

    static double copiedAt = -10;
    if (ImGui::Button("Copy save for bug report")) {
        std::string text = ss::bug_report_text();
        hostui::post([text] { hostui::set_clipboard(text); });
        copiedAt = ImGui::GetTime();
    }
    if (ImGui::GetTime() - copiedAt < 2.0) {
        ImGui::SameLine();
        ImGui::TextDisabled("Copied");
    }
    help("Copies the paths of your newest save state and of your save files (rp_personal_savedata.dat and rp_savedata.dat): "
         "attach them to the bug report. Save a state at the place of the problem first.");
    note("States folder: %s", ss::states_dir().c_str());
    bool fs;
    if (check("Full save states (large, contain game data, don't share) - for debugging", full, &fs, !ss::full_states_forced()))
        ss::set_full_states(fs);
    help(ss::full_states_forced() ? "Set by NSMBU_FULL_SAVE_STATES or a test variable for this start."
                                  : "Saves the whole running game instead (about 300 MB per slot), exactly as it is. "
                                    "These files contain game code and data: never attach them to a bug report.");
    heading("Screenshots");
    {

        const input_map::Mapping m = input_map::current();
        std::string keys;
        for (int k : m.keys[input_map::kScreenshot])
            if (k != input_map::kNoKey) keys += (keys.empty() ? "" : " or ") + input_map::key_label(k);
        if (m.pad[input_map::kScreenshot] != input_map::kPadNone)
            keys += (keys.empty() ? "controller " : " or controller ") + std::string(input_map::pad_short_label(m.pad[input_map::kScreenshot]));
        if (keys.empty()) note("Screenshot is not bound: set a key in the Controls tab (Screenshot).");
        else note("%s in game saves the TV picture as a PNG at the internal resolution, without this menu (change the key in Controls).", keys.c_str());
    }
    bool gp;
    if (check("Also save the GamePad screen (while it is shown)", screenshot::gamepad_too(), &gp))
        hostui::post([gp] { screenshot::set_gamepad_too(gp); });
    help("A second file, ..._GamePad.png, while the GamePad picture is on screen (its window or the overlay in the TV picture).");
    if (hostui::can_open_folder()) {
        if (ImGui::Button("Open screenshots folder")) {
            std::string d = screenshot::dir();
            hostui::post([d] { hostui::open_folder(d); });
        }
        ImGui::SameLine();
    }
    ImGui::AlignTextToFramePadding();
    ImGui::TextDisabled("%s", screenshot::dir().c_str());
    heading("Crash Recovery");
    bool on;
    if (check("Crash Recovery (automatic state every few minutes)", crashrec::enabled(), &on)) crashrec::set_enabled(on);
    help("Saves the game into automatic states in the background and records the controller input since the latest "
         "one. After a crash, the crash log says how to load it. Saving freezes the game for a moment.");
    for (int i = 1; i <= crashrec::kAutoSlots; i++) {
        const crashrec::AutoInfo& a = U.autos[i];
        ImGui::PushID(100 + i);
        ImGui::BeginDisabled(!a.used);
        if (ImGui::Button("Load")) {
            crashrec::request_load(i);
            set_open(false);
        }
        ImGui::EndDisabled();
        ImGui::SameLine();
        ImGui::AlignTextToFramePadding();
        if (a.used) ImGui::Text("Automatic state %d:  %s%s%s", i, a.when.c_str(), a.area.empty() ? "" : "  -  ", a.area.c_str());
        else ImGui::TextDisabled("Automatic state %d:  empty", i);
        ImGui::PopID();
    }
}

std::string performance_device() {
    std::string device = render::device();
#ifdef __APPLE__
    if (!render::vulkan()) device += ", driver: " + reporthdr::os_description();
#endif
#ifdef __ANDROID__
    const auto driver = gfxvk::drivers::active_name();
    if (!driver.empty()) device += " (" + driver + ")";
#endif
    return device.empty() ? "GPU/driver: n/a" : device;
}

std::string performance_report() {
    float avg_ms = 0, worst = 0, avg_fps = 0;
    double avg_logic = 0;
    FrameHistoryAverages::compute(U.frame_ms, U.frame_logic, avg_ms, worst, avg_fps, avg_logic);
    char frame[180];
    snprintf(frame, sizeof frame, "FPS %.1f (average %.1f), frame %.2f ms (worst %.2f), average logic steps/s %.1f\n",
             U.fps, avg_fps, avg_ms, worst, avg_logic);
    std::string report = std::string(frame) + perf::summary();
#ifdef __ANDROID__
    static AndroidTelemetry telemetry;
    telemetry.read();
    if (telemetry.busy >= 0) {
        char busy[64];
        snprintf(busy, sizeof busy, "GPU busy %.0f%%\n", telemetry.busy);
        report += busy;
    }
    for (const auto& [name, value] : telemetry.temperatures) {
        char line[96];
        snprintf(line, sizeof line, "%s %.1f C\n", name.c_str(), value);
        report += line;
    }
#endif
    report += "CPU time is thread running time per output frame. GPU submission intervals may include stalls; not GPU load.\n";
    const std::string detailed = rprof::latest_report();
    report += detailed.empty() ? "No detailed report yet: play for a few seconds, then copy again.\n" : detailed;
    reporthdr::Info h;
    h.version = build::version();
    h.commit = build::commit();
    h.os = reporthdr::os_description();
    h.gpu = performance_device();
    h.renderer = render::api_name(render::active());
    h.host = hostui::name();
    h.fps = interp::mode_name();
    h.scale = hostui::res_scale();
#ifdef NSMBU_HAS_VULKAN
    if (render::vulkan()) {
        h.bufferCache = gfxvk::buffer_cache_enabled();
        h.overrides = reporthdr::vulkan_overrides([](const env::Var& n) -> const char* { return n(); });
    }
#endif
    if (const int g = motion::settings().source; g != motion::kOff) h.gyro = motion::source_id(g);
    report = reporthdr::format(h) + report;
    return report;
}

void tab_graphics() {
#ifdef __ANDROID__
    heading("GPU driver (Snapdragon / Adreno)");
    note("Active: %s", gfxvk::drivers::active_name().c_str());
    auto driver_action = [](auto fn) { hostui::post([fn] { try { fn(); } catch (const std::exception& e) { LOG("[vulkan driver] %s", e.what()); } }); };
    if (ImGui::Button("Install driver ZIP...")) driver_action([] { gfxvk::drivers::request_install(); });
    auto selected = gfxvk::drivers::selection();
    if (radio("System driver", selected.empty())) driver_action([] { gfxvk::drivers::select(""); });
    for (const auto& driver : gfxvk::drivers::installed()) {
        ImGui::PushID(driver.id.c_str());
        auto id = driver.id;
        if (radio((driver.name + " " + driver.version).c_str(), selected == id))
            driver_action([id] { gfxvk::drivers::select(id); });
        ImGui::SameLine();
        if (ImGui::Button("Remove")) driver_action([id] { gfxvk::drivers::remove(id); });
        ImGui::PopID();
    }
    note("%s", gfxvk::drivers::message().c_str());
    help("Driver changes take effect on restart. An unfinished first 120-frame probe selects the system driver on the next start.");
#endif
    if (render::can_choose()) {
        heading("Renderer (takes effect after a restart)");
        for (render::Api a : {render::Api::Metal, render::Api::Vulkan}) {
            std::string label = a == render::Api::Vulkan ? "Vulkan (MoltenVK)" : "Metal";
            if (a == render::active()) label += "  - in use";
            if (radio(label.c_str(), render::preferred() == a, render::compiled(a)))
                post_changed([a] { render::set_preferred(a); });
            ImGui::SameLine();
        }
        ImGui::NewLine();
        if (!render::fallback_reason().empty()) note("This start: %s", render::fallback_reason().c_str());
        if (render::restart_pending()) {
            note("The game uses %s after a restart. This relaunches the app. Progress since your last save is lost.",
                 render::api_name(render::preferred()));
            if (ImGui::Button("Restart now")) hostui::post([] { render::restart(); });
        }
    }
    heading("Frame rate (experimental)");
    int m = interp::mode(), f = interp::fps();
    const bool ff = gx2::uncapped();
    const bool custom = interp::custom_on() && !ff;
    static int custom_value = -1;
    static char custom_buf[8] = "";
    static bool was_custom = false;
    if (custom_value < 0) custom_value = interp::custom_fps();
    if (custom && !was_custom) custom_value = interp::custom_fps();
    was_custom = custom;
    int n_rates = 0;
    const int* rates = interp::pacing::fps_ladder(n_rates);
    const bool free = interp::unlocked() && !ff;
    ImGui::BeginDisabled(ff);
    if (radio("Unlocked", free)) post_changed([] {
        interp::set_fast_forward(false);
        interp::set_unlocked(true);
    });
    for (int i = 0; i < n_rates; i++) {
        const int r = rates[i];
        ImGui::SameLine();
        char label[16];
        snprintf(label, sizeof label, "%d fps", r);
        const bool on = !custom && !free && (r == 60 ? m != 1 : m == 1 && f == r);
        if (radio(label, on)) post_changed([r] {
            interp::set_unlocked(false);
            interp::set_fast_forward(false);
            if (r == 60) { interp::set_fps(60); interp::set_mode(0); }
            else { interp::set_fps(r); interp::set_mode(1); }
        });
    }
    ImGui::EndDisabled();
    ImGui::BeginDisabled(ff);
    if (radio("Custom", custom && !free)) {
        interp::set_unlocked(false);
        interp::set_fast_forward(false);
        interp::set_custom_fps(custom_value);
        post_changed([] {});
    }
    ImGui::SameLine();
    ImGui::SetNextItemWidth(ImGui::CalcTextSize("9999").x + ImGui::GetStyle().FramePadding.x * 2.0f + 10.0f);
    ImGui::InputText("##fps-custom", custom_buf, sizeof custom_buf, ImGuiInputTextFlags_CharsDecimal);
    if (ImGui::IsItemActivated()) {
        interp::set_unlocked(false);
        interp::set_fast_forward(false);
        interp::set_custom_fps(custom_value);
    }
    if (ImGui::IsItemDeactivatedAfterEdit()) {
        int v = atoi(custom_buf);
        v = interp::pacing::clamp_custom_fps(v);
        custom_value = v;
        snprintf(custom_buf, sizeof custom_buf, "%d", v);
        interp::set_unlocked(false);
        interp::set_fast_forward(false);
        interp::set_custom_fps(v);
        post_changed([] {});
    }
    if (!ImGui::IsItemActive()) {
        if (custom) custom_value = interp::custom_fps();
        snprintf(custom_buf, sizeof custom_buf, "%d", custom_value);
    }
    ImGui::EndDisabled();
    bool ff_ui = ff;
    if (check("Fast forward (debug)", ff_ui, &ff_ui)) {
        if (ff_ui) post_changed([] { interp::set_fast_forward(true); });
        else post_changed([] { interp::set_fast_forward(false); });
    }
    help("30 fps draws half as often: two logic steps per picture, so the game keeps its speed.\n"
         "60 fps is the game rate. 120, 144, 165 and 240 are present caps: more draws between\n"
         "fixed 60 Hz logic steps, blended from wall-clock time left in the open step.\n"
         "Custom is any rate from 60 to 1000. Unlocked draws pictures as fast as the machine can while the game stays at 60 logic steps per second.\n"
         "The fps number is that render rate, even when the display cannot show every picture. 30 fps is only the preset. Fast forward (debug) runs the game as fast as this\n"
         "computer can: each picture is one game step, with no frame limit. Hotkeys: 6 = 60 fps,\n"
         "7 = cycle caps, 9 = toggle 60 fps and the last non-60 cap.");

    if (!ff && !interp::blend_enabled() && (interp::unlocked() || interp::fps() > 60))
        note("NSMBU_INTERP=0 is set, so pictures stay at 60 fps.");
    else if (ff)
        note("Fast forward runs the game as fast as this computer can.");
    else if (free)
        note("Unlocked draws as many pictures as this device can. The fps number is that render rate. The display may show fewer.");
    else if (const int hz = interp::display_hz(); hz > 0) {
        const int out = interp::output_fps();
        if (m == 1 && out < f)
            note("Your display: %d Hz. %d fps needs a %d Hz display; %d fps are drawn (the display cannot show more).", hz, f, f, out);
        else if (m == 1 && f > hz + 2)
            note("Your display: %d Hz. Frames beyond %d a second are drawn but not shown (presentation without vsync).", hz, hz);
        else
            note("Your display: %d Hz", hz);
    } else if (m == 1 && f > 60) {
        note("Display refresh rate unknown: %d fps are drawn; frames beyond the display's rate are not shown.", f);
    }
#ifdef __ANDROID__
    {
        const int hg = heat_guard::choice.load();
        const bool editable = !heat_guard::env_override();
        ImGui::TextUnformatted("Heat guard");
        static const char* const hgNames[] = {"Normal", "Relaxed", "Off"};
        for (int i = heat_guard::kNormal; i <= heat_guard::kOff; i++) {
            ImGui::SameLine();
            char label[24];
            snprintf(label, sizeof label, "%s##heatguard", hgNames[i]);
            if (radio(label, hg == i, editable)) post_changed([i] { heat_guard::choice = i; });
        }
        help(editable ? "When the device gets hot, the in-between frames pause and the game keeps 60 fps until it\n"
                        "cools down. Relaxed and Off keep the higher rate longer, but the device may slow down or get\n"
                        "warm. Off never pauses the in-between frames for heat."
                      : "Set by NSMBU_HEAT_GUARD for this start.");
    }
#endif
    heading("Internal resolution");
    float cur = hostui::res_scale();
    for (size_t i = 0; i < gfx::res_preset_count(); i++) {
        if (i) ImGui::SameLine();
        const float s = gfx::kResPresets[i].scale;
        if (radio(gfx::kResPresets[i].label, std::fabs(cur - s) < 0.01f)) post_changed([s] { hostui::set_res_scale(s); });
    }
    note("720p is the Wii U picture. Lower options draw fewer pixels and are scaled up to the window.");

    heading("Effects");
    bool v;
    if (check("16x anisotropic filtering", render::aniso(), &v, render::feature_available(render::kFeatureAniso)))
        post_changed([v] { render::set_aniso(v); });
    ImGui::SameLine(0, 30);
    if (check("Edge smoothing (FXAA)", render::fxaa(), &v, render::feature_available(render::kFeatureFXAA)))
        post_changed([v] { render::set_fxaa(v); });
    heading("Presentation (Vulkan)");
#ifdef NSMBU_HAS_VULKAN
    {
        const bool vk = render::vulkan(), env = gfxvk::present_mode_from_env();
        static const char* const names[] = {"Vsync (smooth)", "Low latency", "Off (may tear)"};
        static const char* const tips[] = {"FIFO: every frame waits for the display's refresh; no tearing",
                                           "MAILBOX: the newest finished frame is shown at the next refresh; no tearing, less delay",
                                           "IMMEDIATE: frames are shown at once; lowest delay, may tear"};

        const int in_use = gfxvk::present_mode_offered(gfxvk::present_mode()) ? gfxvk::present_mode() : gfxvk::kPresentFifo;
        for (int i = 0; i < gfxvk::kPresentModes; i++) {
            if (i) ImGui::SameLine();
            const bool offered = gfxvk::present_mode_offered(i);
            if (radio(names[i], in_use == i, vk && !env && offered)) post_changed([i] { gfxvk::set_present_mode(i); });
            if (vk && !offered) help("not offered by this driver");
            else help(tips[i]);
        }
        if (!vk) note("Used by the Vulkan renderer only (Metal always presents with vsync).");
        else if (env) note("NSMBU_VK_PRESENT_MODE=%s is set for this start and takes precedence.", env::VK_PRESENT_MODE());
        else {
            std::string missing;
            for (int i = 1; i < gfxvk::kPresentModes; i++)
                if (!gfxvk::present_mode_offered(i)) missing += std::string(missing.empty() ? "" : ", ") + names[i];
            if (!missing.empty()) note("Not offered by this driver: %s.", missing.c_str());
        }
    }
#else
    ImGui::BeginDisabled();
    radio("Vsync (smooth)", true);
    ImGui::EndDisabled();
    note("This build has no Vulkan renderer.");
#endif
#ifdef NSMBU_HAS_VULKAN
    if (render::vulkan() && !gfxvk::device_inventory.empty()) {
        heading("Graphics card (next start)");
        const bool env = env::VK_GPU() != nullptr;
        const std::string saved = gfxvk::device_setting();
        const bool dedicated = saved.empty() || saved == "dedicated" || saved == "discrete";
        bool haveDiscrete = false, haveIntegrated = false;
        for (const auto& gpu : gfxvk::device_inventory) {
            if (!gpu.usable()) continue;
            if (gpu.kind == gfxvk::kGpuDiscrete) haveDiscrete = true;
            if (gpu.kind == gfxvk::kGpuIntegrated) haveIntegrated = true;
        }
        auto choose = [](const std::string& value) {
            gfxvk::set_device_setting(value);
            hostui::set("gpu", value.empty() ? "dedicated" : value);
        };
        if (radio("Prefer dedicated", dedicated, !env)) post_changed([choose] { choose(""); });
        ImGui::SameLine();
        if (radio("Prefer integrated", saved == "integrated", !env)) post_changed([choose] { choose("integrated"); });
        for (size_t i = 0; i < gfxvk::device_inventory.size(); i++) {
            const auto& gpu = gfxvk::device_inventory[i];
            ImGui::PushID(int(i));
            const std::string label = gpu.name + " (" + gfxvk::gpu_kind_name(gpu.kind) + ")";
            if (radio(label.c_str(), saved == gpu.identity, !env && gpu.usable())) {
                const std::string identity = gpu.identity;
                post_changed([choose, identity] { choose(identity); });
            }
            if (!gpu.usable()) note("%s", gpu.unavailable.c_str());
            ImGui::PopID();
        }
        note("This start uses %s. A different choice applies the next time the game starts.",
             gfxvk::active_gpu_name.c_str());
        if (env) note("NSMBU_VK_GPU=%s is set for this start and takes precedence.", env::VK_GPU());
#ifndef _WIN32
        else if (gfxvk::gpu_restart_pending(saved, gfxvk::active_gpu_identity, gfxvk::active_gpu_kind, haveDiscrete, haveIntegrated)) {
            note("Progress since your last save is lost if you restart now.");
            if (ImGui::Button("Restart now##gpu")) hostui::post([] { render::restart(); });
        }
#endif
        if (!gfxvk::gpu_fallback.empty()) note("%s", gfxvk::gpu_fallback.c_str());
        help("Prefer dedicated uses a separate graphics card when one can draw the game windows.\n"
             "Prefer integrated keeps the GPU built into the processor.\n"
             "A listed card is used on the next start when it is still available.");
    }
#endif
}

void tab_audio() {
    heading("Sound");
    bool v;
    const bool mute_locked = audio::mute_env_override();
    if (check("Mute", audio::muted(), &v, !mute_locked)) {
        audio::set_muted(v);
        hostui::post([v] { hostui::set("audioMute", v ? "1" : "0"); });
    }
    if (mute_locked) note("NSMBU_NO_AUDIO is set for this start and takes precedence.");
    help("Off plays the game through the host speakers. On keeps the mix running with no sound.");

    const bool vol_locked = audio::volume_env_override();
    float vol = audio::volume() * 100.0f;
    ImGui::BeginDisabled(vol_locked);
    ImGui::SetNextItemWidth(260);
    if (ImGui::SliderFloat("Volume", &vol, 0.0f, 100.0f, "%.0f%%", ImGuiSliderFlags_AlwaysClamp)) {
        const float n = vol / 100.0f;
        audio::set_volume(n);
        hostui::post([n] {
            char buf[32];
            snprintf(buf, sizeof buf, "%g", n);
            hostui::set("audioVolume", buf);
        });
    }
    ImGui::SameLine();
    if (ImGui::Button("100%##volume")) {
        audio::set_volume(1.0f);
        hostui::post([] { hostui::set("audioVolume", "1"); });
    }
    ImGui::EndDisabled();
    if (vol_locked) note("NSMBU_AUDIO_VOLUME=%s is set for this start and takes precedence.", env::AUDIO_VOLUME());
    help("Host output gain. 100% is full level. Applies at once.");

    heading("Output");
    const bool out_locked = audio::output_env_override();
    const audio::OutputSelect::Source cur = audio::output_source();
    static const audio::OutputSelect::Source sources[] = {
        audio::OutputSelect::kAuto, audio::OutputSelect::kTv, audio::OutputSelect::kGamePad};
    for (int i = 0; i < 3; i++) {
        if (i) ImGui::SameLine();
        const auto s = sources[i];
        if (radio(audio::output_source_label(s), cur == s, !out_locked)) {
            audio::set_output_source(s);
            hostui::post([s] { hostui::set("audioOutput", audio::output_source_id(s)); });
        }
    }
    if (out_locked) note("NSMBU_AUDIO_OUTPUT=%s is set for this start and takes precedence.", env::AUDIO_OUTPUT());
    help("Auto follows the game's Off-TV Play faders (TV, then TV + GamePad when the GamePad is audible).\n"
         "TV and GamePad force one mix to the host speakers.");
}

void tab_display() {
    bool v;
    heading("Window");
    if (check("Full screen", hostui::fullscreen(), &v)) hostui::post([v] { hostui::set_fullscreen(v); });
#ifndef __ANDROID__
    help(!strcmp(hostui::name(), "AppKit") ? "The TV window (Cmd+F); remembered for the next start"
                                           : "The TV window (F11 or Alt+Enter); remembered for the next start");
#endif
    if (check("Run in background", hostui::run_in_background(), &v))
        hostui::post([v] { hostui::set_run_in_background(v); });
    help("Keeps the game at full speed when another window is in front.\n"
         "Turn this off to pause the game when the window loses focus.");
    heading("Aspect ratio (experimental)");
    int am = aspect::mode();
    for (int i = aspect::kOriginal; i <= aspect::k32x9; i++) {
        if (i) ImGui::SameLine();
        if (radio(aspect::mode_name(i), am == i)) post_changed([i] { aspect::set_mode(i); });
    }
    heading("Picture scaling");
    static const char* const f[] = {"Smooth", "Sharp", "Integer scale (pixel exact)"};
    const bool fok = hostui::scale_filter_available();
    for (int i = 0; i < 3; i++) {
        if (i) ImGui::SameLine();
        if (radio(f[i], hostui::scale_filter() == i, fok)) hostui::post([i] { hostui::set_scale_filter(i); });
    }
    heading("GamePad screen");

    const int mode = hostui::drc_mode();
    if (hostui::drc_modes() >= 4) {
        static const char* const modes[] = {"Separate window", "Picture-in-picture", "Automatic picture-in-picture", "Off",
                                            "GamePad only"};
        bool first = true;
        for (int i : gfx::kDrcUiOrder) {
            if (i >= hostui::drc_modes()) continue;
            if (!hostui::drc_mode_offered(i)) continue;
            if (!first) ImGui::SameLine();
            first = false;
            if (radio(modes[i], mode == i)) hostui::post([i] { hostui::set_drc_mode(i); });
        }
        if (mode == 2) note("The overlay appears for a few seconds when the GamePad picture changes a lot (a menu opens).");
        if (mode == 4) note("Only the GamePad picture. In the game, Minus switches to Off-TV Play (the game on the GamePad).");
    }
    if (check("Show GamePad screen", hostui::drc_shown(), &v,
              hostui::drc_available() && !(hostui::drc_modes() >= 4 && (mode == 3 || mode == 4))))
        hostui::post([v] { hostui::show_drc(v); });
    if (hostui::drc_modes() >= 4 && (mode == 1 || mode == 2)) {

        static const char* const corners[] = {"Top left", "Top right", "Bottom left", "Bottom right"};
        ImGui::AlignTextToFramePadding();
        ImGui::TextUnformatted("Picture-in-picture corner:");
        for (int i = 0; i < 4; i++) {
            ImGui::SameLine();
            if (radio(corners[i], hostui::pip_corner() == i)) hostui::post([i] { hostui::set_pip_corner(i); });
        }
        ImGui::AlignTextToFramePadding();
        ImGui::TextUnformatted("Size (of the TV picture's width):");
        for (int s : {20, 25, 33, 40}) {
            ImGui::SameLine();
            char l[16];
            snprintf(l, sizeof l, "%d%%##size", s);
            if (radio(l, std::fabs(hostui::pip_size() * 100 - s) < 0.5f)) hostui::post([s] { hostui::set_pip_size(s / 100.0f); });
        }
        ImGui::AlignTextToFramePadding();
        ImGui::TextUnformatted("Opacity:");
        for (int o : {100, 85, 70, 50}) {
            ImGui::SameLine();
            char l[16];
            snprintf(l, sizeof l, "%d%%##opacity", o);
            if (radio(l, std::fabs(hostui::pip_opacity() * 100 - o) < 0.5f)) hostui::post([o] { hostui::set_pip_opacity(o / 100.0f); });
        }
        note("A click (or touch) on the GamePad picture touches the GamePad screen.");
    }
}

void ask_restart(const char* why);
void restart_button(const char* why);
void restart_confirm();

void package_controls() {
    using namespace mods::packages;
    static std::string error;

    static const char* test_enable = g_no_host ? env::TEST_MOD_ENABLE() : nullptr;
    {
        static double last_scan = -1;
        double now = ImGui::GetTime();
        if (last_scan < 0 || now - last_scan >= 1.0) {
            last_scan = now;
            std::string ignore;
            refresh(ignore);
        }
    }
    heading("Installed packages");
    note("Put each mod folder in the mods folder. Then enable it here.");
    if (!error.empty()) ImGui::TextWrapped("%s", error.c_str());
    auto installed = list();
    if (installed.empty()) note("No packages in the mods folder yet.");
    for (const auto& mod : installed) {
        ImGui::PushID(mod.id.c_str());
        bool on = mod.enabled;
        bool toggled = ImGui::Checkbox("##package_enabled", &on);
        if (test_enable && mod.id == test_enable) { toggled = on = true; test_enable = nullptr; }
        if (toggled) enable(mod.id, on, error);
        ImGui::SameLine();
        if (installed.size() == 1) ImGui::SetNextItemOpen(true, ImGuiCond_Once);
        bool expanded = ImGui::TreeNode("details", "%s · %s", mod.name.c_str(), mod.version.c_str());
        if (expanded) {
            note("%s · %s",
                 mod.kind == "cemu" ? "Cemu graphics / shader pack"
                     : mod.kind == "lua" ? "Lua script (can change guest memory)"
                                        : "Model / texture / UI replacement",
                 mod.pending_restart ? "Restart required" : mod.active ? "Active" : mod.enabled ? "Waiting for game update" : "Disabled");
            if (!mod.author.empty()) note("By %s", mod.author.c_str());
            ImGui::TextWrapped("%s", mod.description.c_str());
            if (!mod.reason.empty()) ImGui::TextWrapped("%s", mod.reason.c_str());
            if (!mod.status.empty()) ImGui::TextWrapped("%s", mod.status.c_str());
            for (const auto& dependency : mod.dependencies) note("Requires %s", dependency.c_str());
            for (const auto& conflict : mod.conflicts) note("Conflicts with %s", conflict.c_str());
            for (const auto& option : mod.options) {
                ImGui::PushID(option.id.c_str());
                if (option.type == "bool") {
                    bool value = option.value.boolean;
                    if (ImGui::Checkbox(option.name.c_str(), &value)) configure(mod.id, option.id, value, error);
                } else if (option.type == "number") {
                    double step = option.step > 0 ? option.step : 1;
                    double span = (option.maximum - option.minimum) / step;
                    if (std::isfinite(span) && span >= 0 && span <= 100000) {
                        int count = (int)std::floor(span + 1e-8);
                        int index = (int)std::lround((option.value.number - option.minimum) / step);
                        if (index < 0) index = 0;
                        if (index > count) index = count;
                        char preview[64];
                        snprintf(preview, sizeof preview, "%g", option.minimum + (double)index * step);
                        if (ImGui::SliderInt(option.name.c_str(), &index, 0, count, preview, ImGuiSliderFlags_NoInput)) {
                            double snapped = option.minimum + (double)index * step;
                            if (snapped > option.maximum) snapped = option.maximum;
                            configure(mod.id, option.id, snapped, error);
                        }
                    } else {
                        double value = option.value.number;
                        if (ImGui::SliderScalar(option.name.c_str(), ImGuiDataType_Double, &value,
                                                &option.minimum, &option.maximum, "%g")) {
                            double snapped = option.minimum + std::round((value - option.minimum) / step) * step;
                            if (snapped < option.minimum) snapped = option.minimum;
                            if (snapped > option.maximum) snapped = option.maximum;
                            configure(mod.id, option.id, snapped, error);
                        }
                    }
                } else if (option.type == "radio") {
                    ImGui::TextUnformatted(option.name.c_str());
                    for (size_t i = 0; i < option.choices.size(); ++i) {
                        ImGui::PushID(option.choices[i].c_str());
                        const char* label = i < option.labels.size() ? option.labels[i].c_str() : option.choices[i].c_str();
                        if (radio(label, option.choices[i] == option.value.text))
                            configure(mod.id, option.id, option.choices[i], error);
                        ImGui::PopID();
                        if (i + 1 < option.choices.size()) ImGui::SameLine();
                    }
                } else if (option.type == "enum") {
                    if (ImGui::BeginCombo(option.name.c_str(), option.value.text.c_str())) {
                        for (const auto& choice : option.choices)
                            if (ImGui::Selectable(choice.c_str(), choice == option.value.text))
                                configure(mod.id, option.id, choice, error);
                        ImGui::EndCombo();
                    }
                } else {
                    char value[1025]; snprintf(value, sizeof value, "%s", option.value.text.c_str());
                    if (ImGui::InputText(option.name.c_str(), value, sizeof value, ImGuiInputTextFlags_EnterReturnsTrue))
                        configure(mod.id, option.id, std::string(value), error);
                }
                if (!option.description.empty() && ImGui::IsItemHovered(ImGuiHoveredFlags_DelayShort | ImGuiHoveredFlags_ForTooltip))
                    wrapped_tooltip(option.description.c_str());
                ImGui::PopID();
            }
            if (mod.restart_required)
                note("Content and graphics packs apply after a restart. Active files stay loaded until then.");
            ImGui::TreePop();
        }
        ImGui::PopID();
    }
    bool any_restart = false;
    for (const auto& mod : installed)
        if (mod.pending_restart) any_restart = true;
    if (any_restart) {
        warn("A content or graphics pack change needs a restart.");
        restart_button("Apply mod changes.");
    }
}

void tab_mods() {
    heading("Mod manager");
    auto path = mods::packages::directory();
    if (hostui::can_open_folder() && !path.empty()) {
        if (ImGui::Button("Open mods folder")) {
            hostui::post([path] { hostui::open_folder(path); });
        }
    } else if (!path.empty()) {
        note("Mods folder: %s", path.c_str());
    }
    note("Installed packages start off. Enable a package to load it.");
    package_controls();
}

void start_capture(int a, int col) {
    if (U.control_player > 0 && col != kColPad) col = kColPad;
    U.cap_action = a;
    U.cap_col = col;
    U.cap_player = U.control_player;
    U.cap_pad_released = false;
    U.cap_started = now_s();
    U.cap_note.clear();
    g_capture_key = -2;
    g_capturing_keys = col != kColPad;
}

void clear_binding(int a, int col) {
    input_map::Mapping m = input_map::player(U.control_player);
    if (col == kColPad) m.pad[a] = input_map::kPadNone;
    else m.keys[a][col] = input_map::kNoKey;
    input_map::set_player(U.control_player, m);
}

void apply_capture() {
    if (U.cap_action < 0) return;
    using namespace input_map;
    input_map::Mapping m = input_map::player(U.cap_player);
    const int a = U.cap_action, colm = U.cap_col;
    int code = g_capture_key.exchange(-2);
    bool done = false;
    if (code == -1) done = true;
    else if (code == -3) {
        if (colm == kColPad) m.pad[a] = kPadNone;
        else m.keys[a][colm == kColAny ? 0 : colm] = kNoKey;
        input_map::set_player(U.cap_player, m);
        done = true;
    } else if (code >= 0 && colm != kColPad) {
        if (const char* r = input_map::reserved_key(code)) U.cap_note = std::string("That key is used for ") + r;
        else {
            m.keys[a][colm == kColAny ? 0 : colm] = code;
            input_map::set_player(U.cap_player, m);
            done = true;
        }
    }
    if (!done) {

        float values[kPadCount] = {};
        input::controller_values(U.cap_player, values);
        int pressed = -1;
        bool any = false;
        for (int p = 1; p < kPadCount; p++) {
            any |= values[p] > 0.3f;
            if (values[p] > 0.6f && pressed < 0) pressed = p;
        }
        if (!U.cap_pad_released) U.cap_pad_released = !any;
        else if (pressed > 0) {
            if (colm == kColPad || colm == kColAny) {
                m.pad[a] = pressed;
                input_map::set_player(U.cap_player, m);
            }
            done = true;
        }
    }
    if (!done && now_s() - U.cap_started > 8) done = true;
    if (done) {
        U.cap_action = -1;
        g_capturing_keys = false;
        g_wait_release = true;
    }
}

std::string summary(const input_map::Mapping& m, int a) {
    std::string out;
    for (int k : m.keys[a])
        if (k != input_map::kNoKey) out += (out.empty() ? "" : "  -  ") + input_map::key_label(k);
    if (m.pad[a] != input_map::kPadNone) out += (out.empty() ? "controller " : "  -  controller ") + std::string(input_map::pad_label(m.pad[a]));
    return out.empty() ? "not bound" : out;
}

std::string summary(const input_map::Mapping& m, int a, bool keys) {
    if (keys) return summary(m, a);
    if (m.pad[a] == input_map::kPadNone) return "not bound";
    return std::string("controller ") + input_map::pad_label(m.pad[a]);
}

void controls_list(input_map::Mapping& m, float h, bool keys) {
    const bool scroll = h > 1.0f;
    ImGuiTableFlags flags = ImGuiTableFlags_RowBg | ImGuiTableFlags_BordersInnerH | ImGuiTableFlags_SizingStretchSame;
    if (scroll) flags |= ImGuiTableFlags_ScrollY;
    if (ImGui::BeginTable("map", keys ? 4 : 2, flags, scroll ? ImVec2(0, h) : ImVec2(0, 0))) {
        if (scroll) ImGui::TableSetupScrollFreeze(0, 1);
        ImGui::TableSetupColumn("Wii U input", ImGuiTableColumnFlags_WidthStretch, 1.6f);
        if (keys) {
            ImGui::TableSetupColumn("Key");
            ImGui::TableSetupColumn("Key 2");
        }
        ImGui::TableSetupColumn("Controller");
        ImGui::TableHeadersRow();
        for (int a = 0; a < input_map::kActionCount; a++) {
            ImGui::PushID(a);
            ImGui::TableNextRow();
            ImGui::TableNextColumn();
            ImGui::AlignTextToFramePadding();
            const bool conflict = keys ? input_map::has_conflict(m, a)
                                       : m.pad[a] != input_map::kPadNone && !input_map::pad_users(m, m.pad[a], a).empty();
            if (conflict) ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(1.0f, 0.62f, 0.20f, 1.0f));
            ImGui::TextUnformatted(input_map::action_label(a));
            if (conflict) ImGui::PopStyleColor();
            for (int col = keys ? 0 : kColPad; col < 3; col++) {
                ImGui::TableNextColumn();
                std::string label;
                if (U.cap_action == a && (U.cap_col == col || (U.cap_col == kColAny && col != kColKey1))) label = "...";
                else if (col < 2) label = m.keys[a][col] == input_map::kNoKey ? "-" : input_map::key_label(m.keys[a][col]);
                else label = m.pad[a] == input_map::kPadNone ? "-" : input_map::pad_label(m.pad[a]);
                label += "##c" + std::to_string(col);
                if (ImGui::Button(label.c_str(), ImVec2(-FLT_MIN, 0)) && U.cap_action < 0) start_capture(a, col);
                if (ImGui::IsItemClicked(ImGuiMouseButton_Right) ||
                    (ImGui::IsItemFocused() && U.cap_action < 0 &&
                     (ImGui::IsKeyPressed(ImGuiKey_Delete, false) || ImGui::IsKeyPressed(ImGuiKey_GamepadFaceLeft, false))))
                    clear_binding(a, col);
            }
            ImGui::PopID();
        }
        ImGui::EndTable();
    }
}

void load_gyro() {
    motion::Settings g;
    std::string v;
    bool saved = false, axis = false;
    for (const char* k : motion::kKeys)
        if (hostui::get(k, v)) {
            motion::from_kv(g, k, v);
            saved = true;
            axis |= !strcmp(k, "gyro.axis");
        }
    if (saved && !axis) motion::upgrade_from_first_release(g);
    motion::set_settings(g);
}

void tab_controls() {
    bool v;
    const bool pro = input::pro_controller();
    const bool sync = input_map::sync_players();
    const int p = sync ? 0 : std::clamp(U.control_player, 0, input_map::kPlayers - 1);
    input_map::Mapping m = input_map::player(p);

    if (check("Same controls for every player", sync, &v)) {
        input_map::set_sync_players(v, p);
        if (v) U.control_player = 0;
    }
    help("On: binding a controller button changes it for every player, so all controllers keep the same layout. "
         "Off: bind buttons for one player at a time. The keyboard always drives Player 1. "
         "Each extra controller joins as the next player.");
#ifdef __ANDROID__
    flow::next(flow::text("Player") + 8.0f, 24);
#else
    ImGui::SameLine(0, 24);
#endif
    ImGui::AlignTextToFramePadding();
    ImGui::TextUnformatted("Player");
    for (int i = 0; i < input_map::kPlayers; i++) {
#ifdef __ANDROID__
        flow::next(flow::check(std::to_string(i + 1).c_str()));
#else
        ImGui::SameLine();
#endif
        if (radio(std::to_string(i + 1).c_str(), p == i, !sync)) U.control_player = i;
        if (i == 0) help("Player 1 uses the keyboard and the first controller.");
    }
    if (sync) {
        std::string list;
        for (int i = 0; i < input_map::kPlayers; i++) {
            if (!input::controller_connected(i)) continue;
            if (!list.empty()) list += ", ";
            list += "Player " + std::to_string(i + 1) + ": " + input::controller_name(i);
        }
        if (list.empty()) note("No controllers connected. Player 1 uses the keyboard.");
        else note("Connected: %s", list.c_str());
    } else {
        const std::string name = input::controller_name(p);
        if (p == 0) {
            if (pro)
                note("Player 1: Wii U Pro Controller%s", name.empty() ? " (keyboard only)" : (" (" + name + " and the keyboard)").c_str());
            else
                note("Player 1: Wii U GamePad%s", name.empty() ? " (keyboard only)" : (" (" + name + " and the keyboard)").c_str());
        } else if (input::controller_connected(p)) {
            note("Player %d: %s", p + 1, name.c_str());
        } else {
            note("Player %d: no controller connected. Each extra controller joins as the next player.", p + 1);
        }
    }

    ImGui::AlignTextToFramePadding();
    ImGui::TextUnformatted("Keyboard and controllers act as");
#ifdef __ANDROID__
    flow::next(flow::check("Wii U GamePad"));
#else
    ImGui::SameLine();
#endif

    if (radio("Wii U GamePad", !pro))
        hostui::post([] { hostui::set_pro_controller(false); hostui::set("proController", "0"); });
#ifdef __ANDROID__
    flow::next(flow::check("Wii U Pro Controller"));
#else
    ImGui::SameLine();
#endif
    if (radio("Wii U Pro Controller", pro))
        hostui::post([] { hostui::set_pro_controller(true); hostui::set("proController", "1"); });
#ifdef __ANDROID__
    flow::next(flow::check("List view"));
#else
    ImGui::SameLine(ImGui::GetContentRegionAvail().x + ImGui::GetCursorPosX() - ImGui::CalcTextSize("List view").x -
                    ImGui::GetFrameHeight() - ImGui::GetStyle().ItemInnerSpacing.x);
#endif
    ImGui::Checkbox("List view", &U.list_view);
    help("A plain table of all inputs instead of the controller drawing");

    const input_map::FaceLayout fl = input_map::face_layout(m);
    ImGui::AlignTextToFramePadding();
    ImGui::TextUnformatted("Face buttons");
#ifdef __ANDROID__
    flow::next(flow::check(input_map::face_layout_label(input_map::FaceLayout::kPosition)));
#else
    ImGui::SameLine();
#endif
    if (radio(input_map::face_layout_label(input_map::FaceLayout::kPosition), fl == input_map::FaceLayout::kPosition)) {
        input_map::apply_face_layout(m, input_map::FaceLayout::kPosition);
        input_map::set_player(p, m);
    }
#ifdef __ANDROID__
    flow::next(flow::check(input_map::face_layout_label(input_map::FaceLayout::kLabels)));
#else
    ImGui::SameLine();
#endif
    if (radio(input_map::face_layout_label(input_map::FaceLayout::kLabels), fl == input_map::FaceLayout::kLabels)) {
        input_map::apply_face_layout(m, input_map::FaceLayout::kLabels);
        input_map::set_player(p, m);
    }
    help("How the controller's face buttons drive the Wii U's A/B/X/Y. By position: the bottom "
         "button is B (Nintendo layout). By label: the button named A is A — on an Xbox pad that "
         "makes A accept/act and B go back (issue #78). Only these four bindings are rewritten; "
         "keyboard keys and the other inputs stay as they are.");
    if (fl == input_map::FaceLayout::kCustom) {
        ImGui::SameLine();
        ImGui::TextDisabled("(custom)");
    }

    std::string status;
    ImVec4 sc(0.70f, 0.78f, 0.84f, 1.0f);
    if (U.cap_action >= 0) {
        sc = ImVec4(1.0f, 0.85f, 0.35f, 1.0f);
        status = std::string(input_map::action_label(U.cap_action)) + ": " +
                 (U.cap_col == kColPad   ? "press a controller button or stick direction (Esc cancels)"
                  : U.cap_col == kColAny ? "press a key or a controller button (Esc cancels, Backspace clears)"
                                         : "press a key (Esc cancels, Backspace clears)");
    } else if (!U.cap_note.empty()) {
        status = U.cap_note;
    } else if (U.hover_action >= 0) {
        status = std::string(input_map::action_label(U.hover_action)) + "  -  " + summary(m, U.hover_action, p == 0) +
                 ".  A / Enter / click: rebind; X / Delete / right-click: clear.";
    } else {
        int conflicts = 0;
        if (p == 0) {
            conflicts = input_map::conflict_count(m);
        } else {
            for (int q = 1; q < input_map::kPadCount; q++)
                if (input_map::pad_users(m, q).size() > 1) conflicts++;
        }
        if (conflicts) {
            sc = ImVec4(1.0f, 0.62f, 0.20f, 1.0f);
            status = std::to_string(conflicts) + " input(s) are bound to more than one action (orange).";
        } else {
            status = "Select a button on the controller or a chip to rebind it. Changes are saved to controls.json.";
        }
    }
    const float bottom = ImGui::GetTextLineHeightWithSpacing() + std::max(U.options_height, ImGui::GetFrameHeightWithSpacing() * 2) + 4;
    const ImVec2 avail = ImGui::GetContentRegionAvail();
#ifdef __ANDROID__
    const float h = 0.0f;
    float diagram_h = std::max(420.0f, avail.x * 0.42f);
    if (!U.list_view) {
        const input_map::FaceLayout face = input_map::face_layout(m);
        for (int i = 0; i < 20 && !controls_diagram_fits(pro, face, avail.x, diagram_h); i++) diagram_h += 80.0f;
    }
#else
    const float room = std::max(0.0f, avail.y - bottom);
    const float h = std::max(room, 1.0f);
    const float diagram_h = h;
#endif
    U.hover_action = -1;
    if (U.list_view) {
        controls_list(m, h, p == 0);
    } else {
        ControlsView cv;
        cv.m = m;
        cv.pro = pro;
        cv.cap_action = U.cap_action;
        cv.cap_col = U.cap_col;
        cv.keys_enabled = p == 0;
        static float live[input_map::kPadCount];
        input::controller_values(p, live);
        test_pad(live);
        cv.pad = live;
        cv.t = now_s();

        static bool keys[256];
        for (int i = 0; i < 256; i++) keys[i] = p == 0 && g_held_keys[i].load(std::memory_order_relaxed);
        cv.keys = p == 0 ? keys : nullptr;
        for (int a = 0; a < input_map::kActionCount; a++) {
            float x = 0;
            if (p == 0)
                for (int key : m.keys[a])
                    if (key != input_map::kNoKey && keys[key]) x = 1;
            if (int q = m.pad[a]; q != input_map::kPadNone) x = std::max(x, live[q]);
            cv.act[a] = x;
        }
        input_map::Mapping mm = m;
        mm.invert_camera_y = false;
        input::PadState ks = input_map::keyboard_state(mm, keys), cs = input_map::controller_state(mm, live);
        const bool kl = p == 0 && (ks.lx || ks.ly), kr = p == 0 && (ks.rx || ks.ry);
        cv.stick[0][0] = kl ? ks.lx : cs.lx;
        cv.stick[0][1] = kl ? ks.ly : cs.ly;
        cv.stick[1][0] = kr ? ks.rx : cs.rx;
        cv.stick[1][1] = kr ? ks.ry : cs.ry;
        draw_controls(cv, avail.x, diagram_h);
        U.hover_action = cv.hover_action;
        if (cv.start_action >= 0) start_capture(cv.start_action, cv.start_col);
        if (cv.clear_action >= 0) clear_binding(cv.clear_action, cv.clear_col);
    }
    ImGui::PushStyleColor(ImGuiCol_Text, sc);
    ImGui::TextUnformatted(status.c_str());
    ImGui::PopStyleColor();
    const float options_top = ImGui::GetCursorPosY();
    float dz = m.deadzone * 100.0f;
#ifdef __ANDROID__
    const float gap = 24.0f;
    const float slider_least = ImGui::GetFontSize() * 6.0f;
    ImGui::SetNextItemWidth(flow::item_width(200.0f, "Stick dead zone", slider_least));
#else
    ImGui::SetNextItemWidth(200);
#endif
    if (ImGui::SliderFloat("Stick dead zone", &dz, 0.0f, 90.0f, "%.0f%%")) {
        m.deadzone = std::clamp(dz / 100.0f, 0.0f, 0.9f);
        input_map::set_player(p, m);
    }
#ifdef __ANDROID__
    flow::next(flow::check("Invert camera up/down"), gap);
#else
    ImGui::SameLine(0, 24);
#endif
    if (check("Invert camera up/down", m.invert_camera_y, &v)) {
        m.invert_camera_y = v;
        input_map::set_player(p, m);
    }
#ifdef __ANDROID__
    flow::next(flow::check("Rumble"), gap);
#else
    ImGui::SameLine(0, 24);
#endif

    if (check("Rumble", rumble::enabled(), &v, input::has_rumble())) {
        rumble::set_enabled(v);
        hostui::post([v] { hostui::set("rumble", v ? "1" : "0"); });
    }
    help(input::has_rumble() ? "Controller vibration when the game asks for it. Off keeps the motors still."
                             : "Controller vibration: this host does not drive controller motors yet.");
#ifdef __ANDROID__
    flow::next(flow::button("Reset to defaults"), gap);
#else
    ImGui::SameLine(0, 24);
#endif
    if (ImGui::Button("Reset to defaults")) input_map::set_player(p, input_map::Mapping::defaults());
#ifdef __ANDROID__
    heading("Touch controls");
    if (check("On-screen controls", touch_buttons::controls.load(), &v))
        hostui::post([v] {
            touch_buttons::controls = v;
            hostui::set("touchControls", v ? "1" : "0");
        });
    help("The buttons and sticks drawn over the game. The small button under the view button also shows or\n"
         "hides them; hold it to move and resize them.");
    ImGui::AlignTextToFramePadding();
    ImGui::TextUnformatted("Touch buttons");
    static const char* const touchNames[] = {"Automatic", "Always show", "Hide"};
    for (int i = touch_buttons::Automatic; i <= touch_buttons::Hide; i++) {
        if (i > touch_buttons::Automatic) flow::next(flow::check(touchNames[i]));
        char label[32];
        snprintf(label, sizeof label, "%s##touchbuttons", touchNames[i]);
        if (radio(label, touch_buttons::mode.load() == i))
            hostui::post([i] {
                touch_buttons::mode = i;
                hostui::set("touchButtons", std::to_string(i));
            });
    }
    help("Automatic hides the touch buttons while a controller is connected; a touch in the top left\n"
         "corner brings them back for a few seconds.");
    ImGui::AlignTextToFramePadding();
    ImGui::TextUnformatted("Phone vibration");
    static const char* const phoneNames[] = {"Off", "Touch controls", "No controller motor"};
    const bool phoneEditable = !phone_rumble::env_override();
    for (int i = phone_rumble::kOff; i <= phone_rumble::kFallback; i++) {
        if (i > phone_rumble::kOff) flow::next(flow::check(phoneNames[i]));
        char label[40];
        snprintf(label, sizeof label, "%s##phonerumble", phoneNames[i]);
        if (radio(label, phone_rumble::choice.load() == i, phoneEditable))
            hostui::post([i] {
                phone_rumble::choice = static_cast<phone_rumble::Mode>(i);
                hostui::set("phoneRumble", std::to_string(i));
            });
    }
    help(phoneEditable ? "Touch controls: the phone vibrates while you play with the on-screen controls.\n"
                         "No controller motor: also while the connected controller has no motor."
                       : "Set by NSMBU_PHONE_RUMBLE for this start.");
#endif
    U.options_height = ImGui::GetCursorPosY() - options_top;
}

int saved_language() {
    std::string v;
    if (hostui::get("language", v) && !v.empty()) {
        int l = atoi(v.c_str());
        if (l >= 0 && l < game_lang::kLanguages) return l;
    }
    return 1;
}

void choose_language(int language) {
    U.language = language;
    hostui::post([language] { hostui::set("language", std::to_string(language)); });
}

void ask_restart(const char* why) {
    U.restart_why = why && *why ? why : "settings";
    U.restart_confirm = true;
}

void restart_confirm() {
    if (!U.restart_confirm) return;
    ImGui::OpenPopup("Restart the game?###restart");
    const ImVec2 center = ImGui::GetMainViewport()->GetCenter();
    ImGui::SetNextWindowPos(center, ImGuiCond_Always, ImVec2(0.5f, 0.5f));
    if (!ImGui::BeginPopupModal("Restart the game?###restart", nullptr, ImGuiWindowFlags_AlwaysAutoResize)) return;
    ImGui::PushTextWrapPos(ImGui::GetFontSize() * 28.0f);
    ImGui::TextWrapped("%s This relaunches the app. Progress since your last save is lost.", U.restart_why.c_str());
    ImGui::PopTextWrapPos();
    if (ImGui::Button("Restart now", ImVec2(140, 0))) {
        U.restart_confirm = false;
        ImGui::CloseCurrentPopup();
        hostui::post([] { render::restart(); });
    }
    ImGui::SameLine();
    if (ImGui::Button("Cancel", ImVec2(140, 0))) {
        U.restart_confirm = false;
        ImGui::CloseCurrentPopup();
    }
    ImGui::EndPopup();
}

void restart_button(const char* why) {
    if (ImGui::Button("Restart now")) ask_restart(why);
}

void tab_debug() {
    heading("Overlay");
    bool v;
    heading("Performance overlay");
    if (check("Output and logic", perf_shown(), &v)) set_perf_shown(v);
    if (check("Frame graph and CPU/GPU", perf_timing_shown(), &v)) set_perf_timing_shown(v);
    if (check("Window and presentation", perf_presentation_shown(), &v)) set_perf_presentation_shown(v);
    if (check("Per-frame waits", perf_detail_shown(), &v)) set_perf_detail_shown(v);
    help("Turn on any combination. Sections do not depend on each other.");
    if (ImGui::Button("Reset performance averages")) {
        g_average.reset();
        perf::reset();
        std::memset(U.frame_ms, 0, sizeof U.frame_ms);
        std::memset(U.frame_logic, 0, sizeof U.frame_logic);
        U.frame_i = 0;
    }

    static double copiedAt = -10;
    if (ImGui::Button("Copy performance report")) {
        std::string report = performance_report();
        hostui::post([report] { hostui::set_clipboard(report); });
        copiedAt = ImGui::GetTime();
    }
    if (ImGui::GetTime() - copiedAt < 2.0) {
        ImGui::SameLine();
        ImGui::TextDisabled("Copied");
    }
    help("Where the renderer spends its time over the last few seconds, as text for a bug report");

    heading("Course debug");
    const bool env_locked = env::COLLISION_VIEWER.present();
    if (check("Collision Viewer", collision_wireframe::enabled(), &v, !env_locked)) {
        collision_wireframe::set_enabled(v);
        hostui::post([v] { hostui::set("collisionViewer", v ? "1" : "0"); });
    }
    if (env_locked) help("NSMBU_COLLISION_VIEWER is set for this start and takes precedence.");
    else help("Draw solid tiles in blue and actor hitboxes in red over the course.");

    heading("Course hot reload");
    const bool reload_locked = course_hot_reload::env_locked();
    if (check("Reload the active course when its .szs changes", course_hot_reload::enabled(), &v, !reload_locked)) {
        course_hot_reload::set_enabled(v);
        hostui::post([v] { hostui::set("hotReloadCourses", v ? "1" : "0"); });
    }
    if (reload_locked) help("NSMBU_HOT_RELOAD_COURSES is set for this start and takes precedence.");
    else help("Leaves to the title screen and re-enters the course when the active pack changes on disk.");
    const course_hot_reload::Info reload = course_hot_reload::info();
    if (reload.pack.empty()) {
        note("No course is active.");
    } else {
        note("Pack: %s", reload.pack.c_str());
        note("File: %s", reload.path.c_str());
        note("Status: %s", reload.status.c_str());
        if (ImGui::Button("Reload now")) course_hot_reload::request_reload();
        help("Rebuilds the active course from disk and restarts it at the course entrance.");
    }
}

#ifdef __ANDROID__
static void open_installer() {
    auto* env = static_cast<JNIEnv*>(SDL_GetAndroidJNIEnv());
    if (!env) return;
    jobject activity = static_cast<jobject>(SDL_GetAndroidActivity());
    if (!activity) return;
    if (jclass cls = env->GetObjectClass(activity)) {
        if (jmethodID method = env->GetStaticMethodID(cls, "openInstaller", "()V"))
            env->CallStaticVoidMethod(cls, method);
        env->DeleteLocalRef(cls);
    }
    if (env->ExceptionCheck()) env->ExceptionClear();
    env->DeleteLocalRef(activity);
}

void tab_android() {
    heading("Installer");
    if (ImGui::Button("Quit to the installer")) open_installer();
    help("Leaves the game and opens the installer.");
    note("The app icon skips the installer once a game is ready.");
    note("A new version of the app opens the installer. The next open of that version starts the game.");
    help("Rebuild the game there runs setup again. Long-press the app icon and choose Game setup to open it yourself.");
}
#endif

void tab_about() {
    const char* env = env::LANGUAGE();
    const bool env_set = env && *env;
    char* end = nullptr;
    const long env_value = env_set ? strtol(env, &end, 10) : 1;
    const int env_language = env_set && !*end && env_value >= 0 && env_value < game_lang::kLanguages ? (int)env_value : 1;
    if (U.language < 0) {
        U.language = game_lang::usable(saved_language());
        const game_lang::Start now = game_lang::current();
        U.language_at_start = now.language;
        if (U.language_at_start < 0) {
            const game_lang::Start s = game_lang::choose(env_set ? env_language : U.language);
            U.language_at_start = s.language;
        }
    }
    heading("Console language (applies after a restart)");
    note("The game picks its text language from the console language. Only languages on this disc are listed.");
    ImGui::BeginDisabled(env_set);
    int col = 0;
    for (int i : {1, 2, 5, 3, 4, 8, 9, 10, 7, 0, 6, 11}) {
        if (!game_lang::carried(i)) continue;
        if (col % 3 != 0) ImGui::SameLine();
        if (radio(game_lang::name(i), U.language == i)) choose_language(i);
        col++;
    }
    ImGui::EndDisabled();
    if (env_set) {
        note("NSMBU_LANGUAGE=%s is set for this start and takes precedence.", env);
    } else if (U.language != U.language_at_start) {
        warn("%s is saved. Restart to apply it: the game reads the console language only when it starts.",
             game_lang::name(U.language));
        restart_button("Apply the console language.");
    }
}

static const ImVec4 kPerfOutLabel(1.0f, 1.0f, 1.0f, 1.0f);
static const ImVec4 kPerfLogicLabel(0.45f, 0.85f, 0.95f, 1.0f);
static const ImVec4 kPerfGameCpuLabel(0.45f, 0.65f, 1.0f, 1.0f);
static const ImVec4 kPerfRenderCpuLabel(0.55f, 0.75f, 1.0f, 1.0f);
static const ImVec4 kPerfGpuLabel(0.45f, 0.95f, 0.55f, 1.0f);
static const ImVec4 kPerfPresLabel(0.55f, 0.75f, 0.95f, 1.0f);
static const ImVec4 kPerfWaitLabel(0.95f, 0.85f, 0.35f, 1.0f);
static const ImVec4 kPerfValue(1.0f, 1.0f, 1.0f, 1.0f);

static constexpr float kPerfLabelW = 122.0f;
static constexpr float kPerfValueW = 188.0f;
static constexpr ImGuiTableFlags kPerfTableFlags =
    ImGuiTableFlags_SizingFixedFit | ImGuiTableFlags_NoBordersInBody | ImGuiTableFlags_NoPadOuterX;

static void perf_label_value(const char* label, const ImVec4& labelColor, const char* value,
                             const ImVec4& valueColor = kPerfValue) {
    ImGui::PushStyleVar(ImGuiStyleVar_CellPadding, ImVec2(8.0f, 1.0f));
    ImGui::PushID(label);
    if (ImGui::BeginTable("pv", 2, kPerfTableFlags, ImVec2(0, 0))) {
        ImGui::TableSetupColumn("l", ImGuiTableColumnFlags_WidthFixed, kPerfLabelW);
        ImGui::TableSetupColumn("v", ImGuiTableColumnFlags_WidthFixed, kPerfValueW);
        ImGui::TableNextRow();
        ImGui::TableSetColumnIndex(0);
        ImGui::TextColored(labelColor, "%s", label);
        ImGui::TableSetColumnIndex(1);
        ImGui::TextColored(valueColor, "%s", value);
        ImGui::EndTable();
    }
    ImGui::PopID();
    ImGui::PopStyleVar();
}

static void perf_output_row(double fps, float frameMs, float worstMs, double avgFps, bool showAvg) {
    ImGui::PushStyleVar(ImGuiStyleVar_CellPadding, ImVec2(8.0f, 1.0f));
    if (ImGui::BeginTable("##perf_out", 3, kPerfTableFlags)) {
        ImGui::TableSetupColumn("l", ImGuiTableColumnFlags_WidthFixed, kPerfLabelW);
        ImGui::TableSetupColumn("a", ImGuiTableColumnFlags_WidthFixed, 80.0f);
        ImGui::TableSetupColumn("b", ImGuiTableColumnFlags_WidthFixed, 88.0f);
        ImGui::TableNextRow();
        ImGui::TableSetColumnIndex(0);
        ImGui::TextColored(kPerfOutLabel, "Frame rate");
        ImGui::TableSetColumnIndex(1);
        ImGui::TextColored(kPerfValue, "%.0f FPS", fps);
        ImGui::TableSetColumnIndex(2);
        if (showAvg) ImGui::TextColored(kPerfValue, "avg %.0f", avgFps);
        ImGui::TableNextRow();
        ImGui::TableSetColumnIndex(0);
        ImGui::TextColored(kPerfOutLabel, "Frame time");
        ImGui::TableSetColumnIndex(1);
        ImGui::TextColored(kPerfValue, "%.1f ms", frameMs);
        ImGui::TableSetColumnIndex(2);
        ImGui::TextColored(kPerfValue, "max %.1f", worstMs);
        ImGui::EndTable();
    }
    ImGui::PopStyleVar();
}

static void perf_logic_row(double rate) {
    ImGui::PushStyleVar(ImGuiStyleVar_CellPadding, ImVec2(8.0f, 1.0f));
    if (ImGui::BeginTable("##perf_logic", 3, kPerfTableFlags)) {
        ImGui::TableSetupColumn("l", ImGuiTableColumnFlags_WidthFixed, kPerfLabelW);
        ImGui::TableSetupColumn("rate", ImGuiTableColumnFlags_WidthFixed, 62.0f);
        ImGui::TableSetupColumn("unit", ImGuiTableColumnFlags_WidthFixed, 56.0f);
        ImGui::TableNextRow();
        ImGui::TableSetColumnIndex(0);
        ImGui::TextColored(kPerfLogicLabel, "Logic");
        ImGui::TableSetColumnIndex(1);
        ImGui::TextColored(kPerfValue, "%.1f/60", rate);
        ImGui::TableSetColumnIndex(2);
        ImGui::TextColored(kPerfValue, "steps/s");
        ImGui::EndTable();
    }
    ImGui::PopStyleVar();
}

static void perf_metric_row(const char* label, const ImVec4& labelColor, const perf::Metric& m) {
    char buf[64];
    if (!m.count) snprintf(buf, sizeof buf, "n/a");
    else snprintf(buf, sizeof buf, "%.2f ms  avg %.2f", m.current, m.average());
    perf_label_value(label, labelColor, buf);
}

static const char* tv_picture_label() {
    const float cur = hostui::res_scale();
    for (size_t i = 0; i < gfx::res_preset_count(); i++)
        if (std::fabs(cur - gfx::kResPresets[i].scale) < 0.01f) return gfx::kResPresets[i].label;
    static char buf[32];
    snprintf(buf, sizeof buf, "%.2gx", cur);
    return buf;
}

static const char* presentation_mode_label() {
#ifdef NSMBU_HAS_VULKAN
    if (render::vulkan()) {
        static const char* const names[] = {"Vsync", "Low latency", "Immediate"};
        const int m = gfxvk::present_mode();
        if (m >= 0 && m < gfxvk::kPresentModes) return names[m];
    }
#endif
    return "Vsync (Metal)";
}

static void perf_presentation_footer() {
    ImGuiIO& io = ImGui::GetIO();
    int ww = 0, wh = 0;
    if (!hostui::window_size(ww, wh)) {
        ww = (int)io.DisplaySize.x;
        wh = (int)io.DisplaySize.y;
    }
    char buf[96];
    snprintf(buf, sizeof buf, "%d x %d", ww, wh);
    perf_label_value("Window", kPerfPresLabel, buf);
    perf_label_value("TV picture", kPerfPresLabel, tv_picture_label());
    perf_label_value("Frame pacing", kPerfPresLabel, interp::mode_name());
    perf_label_value("Presentation", kPerfPresLabel, presentation_mode_label());
    perf_label_value("Renderer", kPerfPresLabel, render::api_name(render::active()));
}

static ImU32 perf_graph_color(float ms) {
    if (ms <= 12.0f) return IM_COL32(90, 230, 140, 255);
    if (ms <= 16.7f) return IM_COL32(255, 225, 90, 255);
    return IM_COL32(255, 110, 95, 255);
}

static ImU32 perf_graph_fill(float ms) {
    if (ms <= 12.0f) return IM_COL32(70, 200, 120, 28);
    if (ms <= 16.7f) return IM_COL32(255, 225, 90, 28);
    return IM_COL32(255, 110, 95, 28);
}

static void perf_frame_graph() {
    const float w = ImGui::GetContentRegionAvail().x;
    const float h = 22.0f;
    const ImVec2 p0 = ImGui::GetCursorScreenPos();
    ImGui::Dummy(ImVec2(w, h));

    constexpr int kCount = FrameHistoryAverages::kFrames;
    constexpr float kMin = 0.0f;
    constexpr float kMax = 50.0f;
    const float invScale = 1.0f / (kMax - kMin);
    const int resW = std::max(1, std::min((int)w, kCount) - 1);
    const int itemCount = kCount - 1;
    const float tStep = 1.0f / (float)resW;
    const float baseline = p0.y + h;

    auto sample = [&](int idx) -> float { return U.frame_ms[(idx + U.frame_i) % kCount]; };

    float v0 = sample(0);
    float t0 = 0.0f;
    auto yAt = [&](float v) { return p0.y + h * (1.0f - std::clamp((v - kMin) * invScale, 0.0f, 1.0f)); };

    ImDrawList* dl = ImGui::GetWindowDrawList();
    ImVec2 pos0(p0.x, yAt(v0));

    for (int n = 0; n < resW; n++) {
        const float t1 = t0 + tStep;
        const int v1Idx = (int)(t0 * itemCount + 0.5f);
        const float v1 = sample(v1Idx + 1);
        const ImVec2 pos1(p0.x + t1 * w, yAt(v1));
        const float segMs = 0.5f * (v0 + v1);
        dl->AddQuadFilled(
            ImVec2(pos0.x, baseline), pos0, pos1, ImVec2(pos1.x, baseline), perf_graph_fill(segMs));
        dl->AddLine(pos0, pos1, perf_graph_color(segMs), 1.5f);
        t0 = t1;
        pos0 = pos1;
        v0 = v1;
    }
}

void perf_window(bool menu_open) {
    const double t = now_s();
    static perf::Snapshot snap;
    static double display_logic = 60;
    static double logic_t0 = 0;
    static uint64_t logic_s0 = 0;
    static double logic_rate = 60;
    static int slowWindows = 0;
    static bool slow = false;
    if (logic_t0 == 0 || t - logic_t0 < 0) {
        logic_t0 = t;
        logic_s0 = interp::logic_steps();
    } else if (t - logic_t0 >= 2.0) {
        logic_rate = (double)(interp::logic_steps() - logic_s0) / (t - logic_t0);
        logic_t0 = t;
        logic_s0 = interp::logic_steps();
        slowWindows = logic_rate > 1 && logic_rate < 58.0 ? slowWindows + 1 : 0;
        slow = slow ? logic_rate > 1 && logic_rate < 59.0 : slowWindows >= 2;
    }
    if (U.fps_t0 == 0) { U.fps_t0 = t; U.fps_n0 = render::host_presents(); }
    if (t - U.fps_t0 >= 0.5) {
        uint64_t n = render::host_presents();
        U.fps = (double)(n - U.fps_n0) / (t - U.fps_t0);
        U.fps_t0 = t;
        U.fps_n0 = n;
        snap = perf::snapshot();
        float sample_ms = 0, sample_worst = 0, sample_fps = 0;
        double sample_logic = 0;
        FrameHistoryAverages::compute(U.frame_ms, U.frame_logic, sample_ms, sample_worst, sample_fps, sample_logic);
        if (g_average.started && g_average.logic > 0)
            display_logic = g_average.logic;
        else if (sample_logic > 0)
            display_logic = sample_logic;
    }
    float avg_ms = 0, worst = 0, avg_fps = 0;
    double logic_hist = 0;
    FrameHistoryAverages::compute(U.frame_ms, U.frame_logic, avg_ms, worst, avg_fps, logic_hist);
    (void)logic_hist;

    ImGui::SetNextWindowPos(ImVec2(10, 10), ImGuiCond_Always);
    ImGui::SetNextWindowBgAlpha(0.80f);
    ImGui::SetNextWindowSizeConstraints(ImVec2(330, 0), ImVec2(520, 10000));
    ImGuiWindowFlags fl = ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoSavedSettings |
                          ImGuiWindowFlags_NoFocusOnAppearing | ImGuiWindowFlags_NoNav | ImGuiWindowFlags_NoInputs;
    if (ImGui::Begin("##perf", nullptr, fl)) {
        if (perf_shown()) {
            const double avg_out = g_average.started && g_average.fps > 0 ? g_average.fps : avg_fps;
            perf_output_row(U.fps, avg_ms, worst, avg_out, avg_out > 0);
        }

        if (perf_timing_shown()) {
            if (perf_shown()) ImGui::Spacing();
            if (slow && interp::mode() == 1 && !interp::paced_interpolation())
                ImGui::TextColored(ImVec4(1.0f, 0.75f, 0.3f, 1.0f), "Slow motion: %.1f of 60 logic steps/s. Turn on Keep game speed",
                                    logic_rate);

            perf_frame_graph();
            if (perf_shown()) perf_logic_row(display_logic);

            perf_metric_row("Game CPU", kPerfGameCpuLabel, snap.game);
            perf_metric_row("Render CPU", kPerfRenderCpuLabel, snap.render);
            perf_metric_row("GPU interval", kPerfGpuLabel, snap.gpu);

#ifdef __ANDROID__
            static AndroidTelemetry telemetry;
            static double next_read = 0;
            if (t >= next_read) { telemetry.read(); next_read = t + 2; }
            if (telemetry.busy >= 0) {
                char load[32];
                snprintf(load, sizeof load, "%.0f%%", telemetry.busy);
                perf_label_value("GPU load", kPerfGpuLabel, load);
            }
            for (const auto& [name, value] : telemetry.temperatures) {
                char line[64];
                snprintf(line, sizeof line, "%.1f C", value);
                perf_label_value(name.c_str(), kPerfGpuLabel, line);
            }
#endif
        }

        if (perf_shown() && !perf_timing_shown()) perf_logic_row(display_logic);

        if (perf_presentation_shown()) {
            if (perf_shown() || perf_timing_shown()) ImGui::Spacing();
            perf_presentation_footer();
        }

        rprof::Breakdown b;
        if (perf_detail_shown() && rprof::latest_breakdown(b)) {
            if (perf_shown() || perf_timing_shown() || perf_presentation_shown())
                ImGui::Spacing();
            ImGui::SeparatorText("Per-frame waits");
            struct Row {
                const char* name;
                double ms;
            };
            Row rows[] = {
                {"GPU queue wait", b.gpuWaitMs},
                {"Renderer CPU", b.renderCpuMs},
                {"Present wait", b.acquireMs + b.presentMs},
                {"Game sync", b.gameSyncMs},
                {"Write-back read", b.wbReadMs},
                {"Renderer idle", b.renderIdleMs},
            };
            std::sort(std::begin(rows), std::end(rows), [](const Row& a, const Row& r) { return a.ms > r.ms; });
            int shown = 0;
            for (const Row& r : rows) {
                if (r.ms < 0.1 || shown >= 5) continue;
                char val[32];
                snprintf(val, sizeof val, "%.2f ms", r.ms);
                perf_label_value(r.name, kPerfWaitLabel, val);
                ++shown;
            }
            char up[48];
            snprintf(up, sizeof up, "%.1f MiB/frame", b.uploadMiB);
            perf_label_value("Texture uploads", kPerfWaitLabel, up);
        }
    }
    ImGui::End();
    (void)menu_open;
}

static bool settings_close_button(bool& open, float size) {
    const float pad = ImGui::GetStyle().WindowPadding.x;
    const ImVec2 win = ImGui::GetWindowPos();
    const float lift = ImGui::GetStyle().FramePadding.y * 0.35f;
    const ImVec2 at(win.x + ImGui::GetWindowWidth() - size - pad, ImGui::GetCursorScreenPos().y - lift);
    ImGui::PushClipRect(win, ImVec2(win.x + ImGui::GetWindowWidth(), win.y + ImGui::GetWindowHeight()), false);
    ImGui::SetCursorScreenPos(at);
    ImGui::PushItemFlag(ImGuiItemFlags_NoNav, true);
    const bool pressed = ImGui::InvisibleButton("##settings-close", ImVec2(size, size));
    ImGui::PopItemFlag();
    const bool hot = ImGui::IsItemHovered() || ImGui::IsItemActive();
    ImDrawList* dl = ImGui::GetWindowDrawList();
    if (hot)
        dl->AddRectFilled(at, ImVec2(at.x + size, at.y + size),
                          ImGui::GetColorU32(ImGui::IsItemActive() ? ImGuiCol_ButtonActive : ImGuiCol_ButtonHovered));
    const ImVec2 c(at.x + size * 0.5f, at.y + size * 0.5f);
    const float e = size * 0.22f;
    const float th = std::max(2.0f, size * 0.08f);
    const ImU32 col = ImGui::GetColorU32(ImGuiCol_Text);
    dl->AddLine(ImVec2(c.x - e, c.y - e), ImVec2(c.x + e, c.y + e), col, th);
    dl->AddLine(ImVec2(c.x + e, c.y - e), ImVec2(c.x - e, c.y + e), col, th);
    ImGui::PopClipRect();
    if (pressed) open = false;
    return pressed;
}

#ifdef __ANDROID__
void touch_drag_scroll() {
    ImGuiIO& io = ImGui::GetIO();
    ImGuiWindow* window = ImGui::GetCurrentWindow();
    if (!ImGui::IsMouseDown(ImGuiMouseButton_Left)) return;
    if (window->ScrollMax.y <= 0.0f) return;
    if (!window->InnerRect.Contains(io.MousePos)) return;
    if (window->ScrollbarY && io.MousePos.x >= window->InnerRect.Max.x - window->ScrollbarSizes.x) return;
    if (!ImGui::IsMouseDragging(ImGuiMouseButton_Left, 6.0f)) return;
    ImGui::ClearActiveID();
    const float y = std::clamp(window->Scroll.y - io.MouseDelta.y, 0.0f, window->ScrollMax.y);
    window->Scroll.y = y;
    window->ScrollTarget.y = y;
    window->ScrollTargetCenterRatio.y = 0.0f;
    window->ScrollTargetEdgeSnapDist.y = 0.0f;
}

void settings_tab_button(int i) {
    if (i) flow::next(flow::button(kTabNames[i]));
    const bool on = U.tab == i;
    const ImVec4 face = ImGui::GetStyleColorVec4(on ? ImGuiCol_TabSelected : ImGuiCol_Button);
    ImGui::PushItemFlag(ImGuiItemFlags_NoNav, true);
    ImGui::PushStyleColor(ImGuiCol_Button, face);
    ImGui::PushStyleColor(ImGuiCol_ButtonHovered, face);
    ImGui::PushStyleColor(ImGuiCol_ButtonActive, face);
    const bool pressed = ImGui::Button(kTabNames[i]);
    const bool tapped = ImGui::IsItemHovered() && ImGui::IsMouseClicked(ImGuiMouseButton_Left);
    ImGui::PopStyleColor(3);
    ImGui::PopItemFlag();
    if ((pressed || tapped) && U.tab != i) {
        U.cap_action = -1;
        g_capturing_keys = false;
        U.cap_note.clear();
        U.tab = i;
    }
}
#endif

void settings_window() {
    ImGuiIO& io = ImGui::GetIO();
    const ImVec2 ds = io.DisplaySize;
#ifdef __ANDROID__
    const ImVec2 size(ds.x * 0.98f, ds.y * 0.97f);
#else
    const ImVec2 size(std::min(1040.0f, ds.x * 0.96f), std::min(620.0f, ds.y * 0.92f));
#endif
    ImGui::SetNextWindowPos(ImVec2(ds.x * 0.5f, ds.y * 0.5f), ImGuiCond_Always, ImVec2(0.5f, 0.5f));
    ImGui::SetNextWindowSize(size, ImGuiCond_Always);
    ImGuiWindowFlags fl = ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoCollapse |
                          ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoTitleBar;
    bool open = true;
    if (U.just_opened) {
        ImGui::SetNextWindowFocus();
        U.just_opened = false;
    }
    ImGui::PushStyleVar(ImGuiStyleVar_FrameBorderSize, 0.0f);
    const bool begun = ImGui::Begin(app_title::kSettings, &open, fl);
    ImGui::PopStyleVar();
    if (begun) {
        const float pad = ImGui::GetStyle().WindowPadding.x;
        const float close_sz = ImGui::GetFrameHeight();
        const float top = ImGui::GetCursorPosY();
        settings_close_button(open, close_sz);
        ImGui::SetCursorPos(ImVec2(pad, top));
        ImGui::PushItemWidth(ImGui::GetWindowWidth() - close_sz - pad * 3.0f);
        if (U.cap_action < 0 && !ImGui::IsPopupOpen(nullptr, ImGuiPopupFlags_AnyPopupId)) {
            if (controller_pressed(input_map::kPadLB)) U.select_tab = (U.tab + kTabs - 1) % kTabs;
            if (controller_pressed(input_map::kPadRB)) U.select_tab = (U.tab + 1) % kTabs;
        }
#ifdef __ANDROID__
        if (U.select_tab >= 0 && U.select_tab < kTabs && U.select_tab != U.tab) {
            U.cap_action = -1;
            g_capturing_keys = false;
            U.cap_note.clear();
            U.tab = U.select_tab;
        }
        U.select_tab = -1;
        for (int i = 0; i < kTabs; i++) settings_tab_button(i);
        ImGui::BeginChild("page", ImVec2(0, 0), ImGuiChildFlags_NavFlattened, ImGuiWindowFlags_AlwaysVerticalScrollbar);
        switch (U.tab) {
        case kControls: tab_controls(); break;
        case kGraphics: tab_graphics(); break;
        case kDisplay: tab_display(); break;
        case kAudio: tab_audio(); break;
        case kSaves: tab_saves(); break;
        case kMods: tab_mods(); break;
        case kAbout: tab_about(); break;
        case kDebug: tab_debug(); break;
        case kAndroid: tab_android(); break;
        }
        touch_drag_scroll();
        ImGui::EndChild();
#else
        if (ImGui::BeginTabBar("tabs", ImGuiTabBarFlags_FittingPolicyShrink)) {
            for (int i = 0; i < kTabs; i++) {
                ImGuiTabItemFlags f = U.select_tab == i ? ImGuiTabItemFlags_SetSelected : 0;
                if (ImGui::BeginTabItem(kTabNames[i], nullptr, f)) {
                    if (U.tab != i) U.cap_action = -1, g_capturing_keys = false, U.cap_note.clear();
                    U.tab = i;
                    ImGui::BeginChild("page", ImVec2(0, 0), ImGuiChildFlags_NavFlattened, ImGuiWindowFlags_NoScrollbar);
                    switch (i) {
                    case kControls: tab_controls(); break;
                    case kGraphics: tab_graphics(); break;
                    case kDisplay: tab_display(); break;
                    case kAudio: tab_audio(); break;
                    case kSaves: tab_saves(); break;
                    case kMods: tab_mods(); break;
                    case kAbout: tab_about(); break;
                    case kDebug: tab_debug(); break;
                    }
                    ImGui::EndChild();
                    ImGui::EndTabItem();
                }
            }
            U.select_tab = -1;
            ImGui::EndTabBar();
        }
#endif
        ImGui::PopItemWidth();
        restart_confirm();
    }
    ImGui::End();

    if (!open) set_open(false);
    if (U.cap_action < 0 && controller_pressed(input_map::face_input(input_map::kB)) && !g_pad_b_used &&
        !ImGui::IsPopupOpen(nullptr, ImGuiPopupFlags_AnyPopupId))
        set_open(false);
    g_pad_b_used = false;
}

}

bool is_open() { return g_open.load(std::memory_order_relaxed); }
bool wants_text_input() {
    if (!is_open() || !ImGui::GetCurrentContext()) return false;
    return ImGui::GetIO().WantTextInput;
}
void set_open(bool open) {
    if (g_open.exchange(open) == open) return;
    for (auto& k : g_held_keys) k = false;
    input::release_keys();
    if (!open) g_wait_release = true;
    if (open) {
        mods::mouse_release();
        U.just_opened = true;
        U.slots_time = -1;
        U.gamepad_navigation.reset();
    }
    LOG("[overlay] %s", open ? "opened" : "closed");
}
bool blocks_input() { return captures() || g_wait_release.load(std::memory_order_relaxed); }
bool captures() { return is_open() || text_entry::active(); }
bool alive() { return now_s() - g_last_frame.load(std::memory_order_relaxed) < 1.0; }
bool perf_shown() { return g_perf.load(std::memory_order_relaxed); }
void set_perf_shown(bool on) {
    g_perf = on;
    hostui::post([on] { hostui::set("perfOverlay", on ? "1" : "0"); });
}
bool perf_timing_shown() { return g_perf_timing.load(std::memory_order_relaxed); }
void set_perf_timing_shown(bool on) {
    g_perf_timing = on;
    hostui::post([on] {
        hostui::set("perfTiming", on ? "1" : "0");
        hostui::set("perfExtended", on ? "1" : "0");
    });
}
bool perf_presentation_shown() { return g_perf_presentation.load(std::memory_order_relaxed); }
void set_perf_presentation_shown(bool on) {
    g_perf_presentation = on;
    hostui::post([on] { hostui::set("perfPresentation", on ? "1" : "0"); });
}
bool perf_detail_shown() { return g_perf_detail.load(std::memory_order_relaxed); }
void set_perf_detail_shown(bool on) {
    g_perf_detail = on;
    hostui::post([on] { hostui::set("perfDetail", on ? "1" : "0"); });
}
bool perf_overlay_active() {
    return perf_shown() || perf_timing_shown() || perf_presentation_shown() || perf_detail_shown();
}

bool key(int code, bool down, bool repeat, int mods) {
    if (g_no_host_keys) return false;
#ifdef __APPLE__

    if (code == kVK_ANSI_Comma && (mods & kSuper) && !g_capturing_keys.load()) {
        if (down && !repeat) set_open(!is_open());
        return true;
    }
#endif

    if ((code == kVK_ANSI_Grave || code == kVK_F12) && !(mods & (kShift | kCtrl | kAlt | kSuper))) {
        if (down && !repeat && !g_capturing_keys.load()) set_open(!is_open());
        if (!g_capturing_keys.load()) return true;
    }
    if (!is_open()) {

        if (!text_entry::active()) return false;
        text_entry::key(code, down, repeat);
        return true;
    }
    if (code >= 0 && code < 256 && !repeat) g_held_keys[code] = down;
    if (g_capturing_keys.load()) {
        if (down && !repeat) {
            if (code == kVK_Escape) g_capture_key = -1;
            else if (code == kVK_Delete || code == kVK_ForwardDelete) g_capture_key = -3;
            else g_capture_key = code;
        }
        return true;
    }
    if (code == kVK_Escape && down && !repeat) {
        set_open(false);
        return true;
    }
    push({Event::Key, code, down, 0, 0, mods});
    return true;
}
bool mouse_move(float nx, float ny, bool touch) {
    if (!captures() || g_no_host) return false;
    push({Event::MousePos, 0, false, nx, ny, 0, touch});
    return true;
}
bool mouse_button(int button, bool down, bool touch) {
    if (!captures() || g_no_host) return false;
    push({Event::MouseButton, button, down, 0, 0, 0, touch});
    return true;
}
bool mouse_wheel(float dx, float dy) {
    if (!captures() || g_no_host) return false;
    push({Event::Wheel, 0, false, dx, dy, 0});
    return true;
}
bool text(const char* utf8) {
    if (!utf8 || !*utf8 || !is_open() || g_no_host) return false;
    Event e;
    e.kind = Event::Text;
    e.text = utf8;
    push(e);
    return true;
}
void set_density(float d) { g_density = d > 0.5f ? d : 1.0f; }

void undo_frame_sample() {
    if (U.sample_undo) {
        U.frame_i = (U.frame_i + FrameHistoryAverages::kFrames - 1) % FrameHistoryAverages::kFrames;
        U.frame_ms[U.frame_i] = 0;
        U.frame_logic[U.frame_i] = 0;
    }
    U.last_present = U.sample_restore;
    U.sample_undo = false;
}

ImDrawData* frame(float pw, float ph, void (*renderer_init)()) {
    static TestSwitch test = parse_test();
    static bool prefs_read = false;
    if (!prefs_read) {
        prefs_read = true;
        std::string v;
        if (hostui::get("perfOverlay", v)) g_perf = v == "1";
        std::string timing;
        if (hostui::get("perfTiming", timing))
            g_perf_timing = timing == "1";
        else if (hostui::get("perfExtended", timing)) {
            g_perf_timing = timing == "1";
            if (timing == "1") g_perf_presentation = true;
        }
        if (hostui::get("perfPresentation", v))
            g_perf_presentation = v == "1";
        if (hostui::get("perfDetail", v)) g_perf_detail = v == "1";
        if (!env::COLLISION_VIEWER.present() && hostui::get("collisionViewer", v))
            collision_wireframe::set_enabled(v == "1");
        if (!env::HOT_RELOAD_COURSES.present() && hostui::get("hotReloadCourses", v))
            course_hot_reload::set_enabled(v == "1");

        if (!env::PRO_CONTROLLER() && hostui::get("proController", v))
            hostui::post([pro = v == "1"] { hostui::set_pro_controller(pro); });

        if (!rumble::env_override() && hostui::get("rumble", v)) rumble::set_enabled(v != "0");
#ifdef __ANDROID__
        if (hostui::get("touchButtons", v) && !v.empty()) touch_buttons::mode = std::clamp(atoi(v.c_str()), 0, 2);
        if (hostui::get("touchControls", v)) touch_buttons::controls = v != "0";
        if (!phone_rumble::env_override() && hostui::get("phoneRumble", v)) phone_rumble::choice = phone_rumble::parse(v.c_str());
#endif

        if (!audio::mute_env_override() && hostui::get("audioMute", v)) audio::set_muted(v == "1");
        if (!audio::volume_env_override() && hostui::get("audioVolume", v) && !v.empty())
            audio::set_volume((float)atof(v.c_str()));
        if (!audio::output_env_override() && hostui::get("audioOutput", v) && !v.empty())
            audio::set_output_source(audio::OutputSelect::parse(v.c_str()));

        hostui::post([] { load_gyro(); });
    }
    if (!test.done && render::frame_count() + 1 >= test.at) {
        test.done = true;
        if (test.tab >= 0) U.select_tab = U.tab = test.tab;
        if (test.open) set_open(true);
        if (test.perf) g_perf = true;
        LOG("[overlay] test switch: %s", env::TEST_OVERLAY());
    }
    {
        static bool test_load_done = false;
        const char* ls = env::TEST_LOAD_STORY();
        if (!test_load_done && ls && *ls) {
            int w = 0;
            int c = 0;
            uint64_t at = 1;
            std::string spec = ls;
            if (size_t atp = spec.find('@'); atp != std::string::npos) {
                at = strtoull(spec.c_str() + atp + 1, nullptr, 10);
                spec.resize(atp);
            }
            if (sscanf(spec.c_str(), "%d,%d", &w, &c) == 2 && render::frame_count() + 1 >= at) {
                test_load_done = true;
                mods::lua_sdk::request_load_story(w, c);
                LOG("[overlay] test load story %d-%d", w + 1, c + 1);
            }
        }
    }
#ifdef NSMBU_HAS_VULKAN

    static const char* tpm = env::TEST_PRESENT_MODE();
    if (tpm) {
        const char* at = strchr(tpm, '@');
        if (render::frame_count() + 1 >= (at ? strtoull(at + 1, nullptr, 10) : 1)) {
            std::string m(tpm, at ? at - tpm : strlen(tpm));
            int mode = m == "mailbox" ? gfxvk::kPresentMailbox : m == "immediate" ? gfxvk::kPresentImmediate : gfxvk::kPresentFifo;
            LOG("[overlay] test: presentation %s", m.c_str());
            post_changed([mode] { gfxvk::set_present_mode(mode); });
            tpm = nullptr;
        }
    }
#endif
    const double t = now_s();
    static uint64_t prev_logic_steps = 0;
    const uint64_t logic_steps = interp::logic_steps();
    if (U.last_present > 0) {
        U.frame_ms[U.frame_i] = (float)((t - U.last_present) * 1000.0);
        const uint64_t prev = prev_logic_steps;
        U.frame_logic[U.frame_i] = prev ? (uint32_t)(logic_steps - prev) : 0;
        U.frame_i = (U.frame_i + 1) % FrameHistoryAverages::kFrames;
        U.sample_undo = true;
    } else {
        U.sample_undo = false;
    }
    U.sample_restore = U.last_present;
    U.last_present = t;
    prev_logic_steps = logic_steps;
    g_last_frame = t;
    g_average.sample(t, gx2::flips_presented(), logic_steps, int(render::active()),
                     interp::mode(), interp::fps(), hostui::res_scale());
    read_controller();

    const bool open = is_open(), perf = perf_overlay_active(), text = !open && text_entry::active();
    const bool coll = collision_wireframe::enabled();
    const bool gui = mods::lua_gui::active();
    perf::set_demand(perf, open && U.tab == kGraphics);

    const std::string toast = open ? std::string() : ss::last_message();
    U.linearized = false;
    if (!open && !perf && !text && toast.empty() && !coll && !gui) {
        if (U.init) {
            std::lock_guard<std::mutex> lk(g_mu);
            g_events.clear();
        }
        return nullptr;
    }
    if (pw < 16 || ph < 16) return nullptr;
    if (!U.init) {
        init_context();
        if (renderer_init) renderer_init();
    }
    ImGuiIO& io = ImGui::GetIO();

#ifdef __ANDROID__
    const mobile_ui::Layout touch_ui =
        mobile_ui::android_settings(g_density.load() * 160.0f, g_density.load(), float(pw), float(ph));
    mobile_ui::apply_touch_style(ImGui::GetStyle(), touch_ui);
    const float scale = touch_ui.scale;
#else
    const float scale = mobile_ui::desktop_scale(g_density.load(), float(ph));
#endif
    io.DisplaySize = ImVec2(pw / scale, ph / scale);
    io.DisplayFramebufferScale = ImVec2(scale, scale);
    io.DeltaTime = U.last_time > 0 ? (float)std::clamp(t - U.last_time, 1e-4, 0.25) : 1.0f / 60.0f;
    U.last_time = t;
    std::vector<Event> events;
    {
        std::lock_guard<std::mutex> lk(g_mu);
        events.swap(g_events);
    }
    bool mouse_used = false;
    for (const Event& e : events) {
        switch (e.kind) {
        case Event::Key:
            io.AddKeyEvent(ImGuiMod_Shift, e.mods & kShift);
            io.AddKeyEvent(ImGuiMod_Ctrl, e.mods & kCtrl);
            io.AddKeyEvent(ImGuiMod_Alt, e.mods & kAlt);
            io.AddKeyEvent(ImGuiMod_Super, e.mods & kSuper);
            if (ImGuiKey k = imgui_key(e.code); k != ImGuiKey_None) io.AddKeyEvent(k, e.down);
            break;
        case Event::MousePos:
            io.AddMouseSourceEvent(e.touch ? ImGuiMouseSource_TouchScreen : ImGuiMouseSource_Mouse);
            mouse_used |= e.x != U.mouse_position.x || e.y != U.mouse_position.y;
            U.mouse_position = ImVec2(e.x, e.y);
            io.AddMousePosEvent(e.x * io.DisplaySize.x, e.y * io.DisplaySize.y);
            break;
        case Event::MouseButton:
            io.AddMouseSourceEvent(e.touch ? ImGuiMouseSource_TouchScreen : ImGuiMouseSource_Mouse);
            mouse_used = true;
            io.AddMouseButtonEvent(e.code, e.down);
            break;
        case Event::Wheel:
            mouse_used |= e.x != 0 || e.y != 0;
            io.AddMouseWheelEvent(e.x, e.y);
            break;
        case Event::Text: io.AddInputCharactersUTF8(e.text.c_str()); break;
        default: break;
        }
    }
    apply_capture();
    feed_gamepad(io, open && U.cap_action < 0, mouse_used);
    ImGui::NewFrame();
    if (open) settings_window();
    if (text) {
        float pad[input_map::kPadCount];
        std::copy(std::begin(U.values), std::end(U.values), pad);
        if (g_no_host) test_pad(pad);
        if (text_entry::draw(pad)) {

            for (auto& k : g_held_keys) k = false;
            g_wait_release = true;
        }
    }
    if (perf) perf_window(open);
    if (coll || gui) {
        ImGui::GetBackgroundDrawList()->AddRectFilled(ImVec2(0, 0), ImVec2(1, 1), IM_COL32(0, 0, 0, 1));
        if (coll) collision_wireframe::draw(ImGui::GetForegroundDrawList(), io.DisplaySize.x, io.DisplaySize.y);
        if (gui) mods::lua_gui::draw(ImGui::GetForegroundDrawList(), io.DisplaySize.x, io.DisplaySize.y);
    }
    if (!toast.empty()) {
        ImGui::SetNextWindowPos(ImVec2(io.DisplaySize.x * 0.5f, io.DisplaySize.y - 24), ImGuiCond_Always, ImVec2(0.5f, 1.0f));
        ImGui::SetNextWindowBgAlpha(0.7f);
        ImGui::SetNextWindowSizeConstraints(ImVec2(0, 0), ImVec2(io.DisplaySize.x * 0.8f, FLT_MAX));
        const ImGuiWindowFlags fl = ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoSavedSettings |
                                    ImGuiWindowFlags_NoFocusOnAppearing | ImGuiWindowFlags_NoNav | ImGuiWindowFlags_NoInputs;
        if (ImGui::Begin("##savestate_notice", nullptr, fl)) {
            ImGui::PushTextWrapPos(io.DisplaySize.x * 0.75f);
            ImGui::TextUnformatted(toast.c_str());
            ImGui::PopTextWrapPos();
        }
        ImGui::End();
    }
    ImGui::Render();
    return ImGui::GetDrawData();
}

void linearize_colors(ImDrawData* d) {
    if (!d || U.linearized) return;
    U.linearized = true;
    static uint8_t table[256];
    static bool made = false;
    if (!made) {
        for (int i = 0; i < 256; i++) {
            float c = i / 255.0f;
            c = c <= 0.04045f ? c / 12.92f : powf((c + 0.055f) / 1.055f, 2.4f);
            table[i] = (uint8_t)std::lround(c * 255.0f);
        }
        made = true;
    }
    for (ImDrawList* l : d->CmdLists)
        for (ImDrawVert& v : l->VtxBuffer) {
            uint32_t c = v.col;
            v.col = (c & 0xFF000000u) | (uint32_t)table[(c >> 16) & 0xFF] << 16 | (uint32_t)table[(c >> 8) & 0xFF] << 8 |
                    table[c & 0xFF];
        }
}

}
