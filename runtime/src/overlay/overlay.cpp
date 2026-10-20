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
#include "hostui.h"
#include "controls_view.h"
#include "text_entry.h"
#include "gfx/aspect.h"
#include "crash/crashrec.h"
#include "text/game_languages.h"
#include "../gfx/renderer.h"
#ifdef __ANDROID__
#include "../gfx/vulkan/android_driver.h"
#endif
#ifdef NSMBU_HAS_VULKAN
#include "../gfx/vulkan/settings.h"
namespace gfxvk { bool buffer_cache_enabled(); }
#endif
#include "input/input.h"
#include "input/input_map.h"
#include "../mods/mods.h"
#include "../mods/sdk/nsmbu.h"
#include "../mods/packages.h"
#include "../input/motion/motion.h"
#include "../platform/keycodes.h"
#include "audio/audio_out.h"
#include "input/rumble.h"
#include "hooks/interp.h"
#include "core/runtime.h"
#include "save/savestate.h"
#include "gfx/screenshot.h"
#include "gfx/render_prof.h"
#include "core/build_info.h"
#include "crash/report_header.h"

namespace gx2 { uint64_t flips_presented(); bool uncapped(); void set_uncapped(bool on); }

namespace overlay {
namespace {

using clock = std::chrono::steady_clock;
double now_s() { return std::chrono::duration<double>(clock::now().time_since_epoch()).count(); }

std::atomic<bool> g_open{false};
std::atomic<bool> g_perf{false};
std::atomic<bool> g_perf_detail{false};
PerfAverage g_average;
std::atomic<float> g_density{1.0f};
std::atomic<bool> g_wait_release{false};
std::atomic<double> g_last_frame{0};
const bool g_no_host = env::NO_HOST_INPUT() != nullptr;

const bool g_no_host_keys = g_no_host && !env::TEST_POST_KEYS();
bool g_pad_b_used = false;

struct Event {
    enum Kind { Key, MousePos, MouseButton, Wheel, Focus } kind;
    int code = 0;
    bool down = false;
    float x = 0, y = 0;
    int mods = 0;
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

enum Tab { kSaves, kGraphics, kDisplay, kAudio, kMods, kControls, kAbout, kDebug, kTabs };
const char* const kTabNames[kTabs] = {"Saves", "Graphics", "Display", "Audio", "Mods", "Controls", "Language / About", "debug"};
const char* const kTabIds[kTabs] = {"saves", "graphics", "display", "audio", "mods", "controls", "about", "debug"};

struct Ui {
    bool init = false;
    int tab = kSaves, select_tab = -1;
    double last_time = 0;

    float values[input_map::kPadCount] = {};
    float prev[input_map::kPadCount] = {};
    double options_since = -1;
    bool options_latched = false;

    int cap_action = -1, cap_col = 0;
    bool cap_pad_released = false;
    double cap_started = 0;
    std::string cap_note;

    ss::SlotInfo slots[ss::kSlots + 1];
    crashrec::AutoInfo autos[crashrec::kAutoSlots + 1];
    double slots_time = -1;

    double fps_t0 = 0, fps = 0;
    uint64_t fps_n0 = 0;
    float frame_ms[120] = {};
    int frame_i = 0;
    double last_present = 0;

    int language = -1;
    int language_at_start = -1;
    bool linearized = false;
    bool just_opened = false;
    bool list_view = false;
    int hover_action = -1;
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

    c[ImGuiCol_WindowBg] = ImVec4(0.03f, 0.07f, 0.12f, 0.975f);
    c[ImGuiCol_ChildBg] = ImVec4(0.06f, 0.13f, 0.20f, 0.35f);
    c[ImGuiCol_PopupBg] = ImVec4(0.05f, 0.11f, 0.18f, 0.98f);
    c[ImGuiCol_Border] = ImVec4(0.30f, 0.62f, 0.70f, 0.45f);
    c[ImGuiCol_TitleBg] = ImVec4(0.05f, 0.20f, 0.30f, 1.0f);
    c[ImGuiCol_TitleBgActive] = ImVec4(0.06f, 0.27f, 0.38f, 1.0f);
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
    static const char* const candidates[] = {
#if defined(__APPLE__)
        "/System/Library/Fonts/Supplemental/Arial Unicode.ttf",
        "/System/Library/Fonts/Supplemental/Arial.ttf",
#elif defined(_WIN32)
        "C:/Windows/Fonts/segoeui.ttf",
        "C:/Windows/Fonts/arial.ttf",
#else
        "/usr/share/fonts/truetype/dejavu/DejaVuSans.ttf",
        "/usr/share/fonts/TTF/DejaVuSans.ttf",
        "/usr/share/fonts/dejavu-sans-fonts/DejaVuSans.ttf",
        "/usr/share/fonts/truetype/noto/NotoSans-Regular.ttf",
        "/usr/share/fonts/noto/NotoSans-Regular.ttf",
#endif
    };
    for (const char* path : candidates) {
        if (FILE* f = fopen(path, "rb")) {
            fclose(f);
            if (io.Fonts->AddFontFromFileTTF(path, 17.0f)) {
                LOG("[overlay] font %s", path);
                return;
            }
        }
    }
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

    if (controller_pressed(kPadHome)) set_open(!is_open());
    if (controller_down(kPadOptions)) {
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

void feed_gamepad(ImGuiIO& io, bool enabled) {
    using namespace input_map;
    auto key = [&](ImGuiKey k, int p) { io.AddKeyAnalogEvent(k, enabled && U.values[p] > 0.5f, enabled ? U.values[p] : 0.0f); };
    key(ImGuiKey_GamepadFaceDown, kPadA);
    key(ImGuiKey_GamepadFaceRight, kPadB);
    key(ImGuiKey_GamepadFaceLeft, kPadX);
    key(ImGuiKey_GamepadFaceUp, kPadY);
    key(ImGuiKey_GamepadDpadUp, kPadDUp);
    key(ImGuiKey_GamepadDpadDown, kPadDDown);
    key(ImGuiKey_GamepadDpadLeft, kPadDLeft);
    key(ImGuiKey_GamepadDpadRight, kPadDRight);
    key(ImGuiKey_GamepadLStickUp, kPadLSUp);
    key(ImGuiKey_GamepadLStickDown, kPadLSDown);
    key(ImGuiKey_GamepadLStickLeft, kPadLSLeft);
    key(ImGuiKey_GamepadLStickRight, kPadLSRight);
    key(ImGuiKey_GamepadRStickUp, kPadRSUp);
    key(ImGuiKey_GamepadRStickDown, kPadRSDown);
    key(ImGuiKey_GamepadL2, kPadLT);
    key(ImGuiKey_GamepadR2, kPadRT);
    key(ImGuiKey_GamepadStart, kPadMenu);
}

void help(const char* text) {
    if (ImGui::IsItemHovered(ImGuiHoveredFlags_DelayShort | ImGuiHoveredFlags_ForTooltip)) ImGui::SetTooltip("%s", text);
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
}

void tab_saves() {
    refresh_slots(false);
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
                    if (ImGui::IsItemHovered())
                        ImGui::SetTooltip("Kept on disk, not loaded: the newer portable state is. It contains game data: "
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
    help("This game writes a full save state. "
         "The small portable state is unused. "
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
            note("The game uses %s after a restart. Progress since your last save is lost.", render::api_name(render::preferred()));
            if (ImGui::Button("Restart now")) hostui::post([] { render::restart(); });
        }
    }
    heading("Frame rate");
    int m = interp::mode(), f = interp::fps();
    for (int r : {60, 120, 165, 240}) {
        if (r != 60) ImGui::SameLine();
        char label[16];
        if (r != 60) ImGui::BeginDisabled();
        snprintf(label, sizeof label, "%d fps", r);
        const bool on = r == 60 ? m != 1 : m == 1 && f == r;
        if (radio(label, on)) post_changed([r] {
            if (r == 60) interp::set_mode(0);
            else { interp::set_fps(r); interp::set_mode(1); }
        });
        if (r != 60) ImGui::EndDisabled();
    }
    help("60 fps is the game rate.\n"
         "120, 165 and 240 fps are the higher rates. The game logic stays at 60 steps a second.\n"
         "The display shows at most its refresh rate.");

    if (const int hz = interp::display_hz(); hz > 0) {
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
    if (m == 1) {
        bool paced;
        if (check("Keep game speed (recommended)", interp::paced_interpolation(), &paced, !env::INTERP_PACED()))
            post_changed([paced] { interp::set_paced_interpolation(paced); });
        help("When the computer cannot draw all frames, skip in-between frames instead of slowing the\n"
             "whole game down. The performance overlay shows how many are drawn.\n"
             "Off: every logic step waits for all of its frames, so if the frame target is not reached\n"
             "(e.g. 120/165/240 fps at a high internal resolution) the whole game runs in slow motion.\n"
             "Saved for 120, 165 and 240 fps.");
        if (!interp::paced_interpolation())
            note("Off: if this computer cannot reach %d fps, the whole game slows down (the performance\n"
                 "overlay then shows fewer than 30 logic steps/s). Turn it on to keep the game's speed.", interp::fps());
    }

    bool unc;
    if (check("Uncapped (debug: the game runs too fast)", gx2::uncapped(), &unc)) hostui::post([unc] { gx2::set_uncapped(unc); });
    help("Debug only, to see how many frames a second this computer can draw: no frame limit and no\n"
         "vsync. The game counts frames, so it runs faster than normal. The frame rate is in the window\n"
         "title and the performance overlay. Not saved. (With a higher frame rate and Keep game speed\n"
         "on, the game logic still keeps 60 steps a second.)");

    heading("Internal resolution");
    static const float scales[] = {1.0f, 1.5f, 2.0f, 3.0f};
    static const char* const names[] = {"1x  (1280x720)", "1.5x  (1920x1080)", "2x  (2560x1440)", "3x  (3840x2160)"};
    float cur = hostui::res_scale();
    for (int i = 0; i < 4; i++) {
        if (i) ImGui::SameLine();
        float s = scales[i];
        if (radio(names[i], std::fabs(cur - s) < 0.01f)) post_changed([s] { hostui::set_res_scale(s); });
    }

    heading("Aspect ratio");
    int am = aspect::mode();
    for (int i = aspect::kOriginal; i <= aspect::k32x9; i++) {
        if (i) ImGui::SameLine();
        if (radio(aspect::mode_name(i), am == i)) post_changed([i] { aspect::set_mode(i); });
    }

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
    heading("Overlay");
    if (check("Performance overlay (FPS, frame time)", perf_shown(), &v)) set_perf_shown(v);
    if (check("Performance overlay detail (lag breakdown)", perf_detail_shown(), &v, perf_shown())) set_perf_detail_shown(v);
    if (!perf_shown()) help("Needs the performance overlay on");
    if (ImGui::Button("Reset performance averages")) g_average.reset();

    static double copiedAt = -10;
    if (ImGui::Button("Copy performance report")) {
        std::string report = rprof::latest_report();

        reporthdr::Info h;
        h.version = build::version();
        h.commit = build::commit();
        h.os = reporthdr::os_description();
        h.gpu = render::device();
        h.renderer = render::vulkan() ? "Vulkan" : "Metal";
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
        report = reporthdr::format(h) +
                 (report.empty() ? std::string("No report yet: play for a few seconds, then copy again.\n") : report);
        hostui::post([report] { hostui::set_clipboard(report); });
        copiedAt = ImGui::GetTime();
    }
    if (ImGui::GetTime() - copiedAt < 2.0) {
        ImGui::SameLine();
        ImGui::TextDisabled("Copied");
    }
    help("Where the renderer spends its time over the last few seconds, as text for a bug report");
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
        for (int i = 0; i < hostui::drc_modes() && i < 5; i++) {
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
                if (!option.description.empty() && ImGui::IsItemHovered()) ImGui::SetTooltip("%s", option.description.c_str());
                ImGui::PopID();
            }
            if(mod.restart_required) note("Changes apply on the next game start. Active files stay loaded until exit.");
            ImGui::TreePop();
        }
        ImGui::PopID();
    }
}

void tab_mods() {
    heading("Mod manager");
    if (!mods::packages::supported()) {
        note("%s", mods::packages::unsupported_reason().c_str());
        return;
    }
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
    U.cap_action = a;
    U.cap_col = col;
    U.cap_pad_released = false;
    U.cap_started = now_s();
    U.cap_note.clear();
    g_capture_key = -2;
    g_capturing_keys = col != kColPad;
}

void clear_binding(int a, int col) {
    input_map::Mapping m = input_map::current();
    if (col == kColPad) m.pad[a] = input_map::kPadNone;
    else m.keys[a][col] = input_map::kNoKey;
    input_map::set_current(m);
}

void apply_capture() {
    if (U.cap_action < 0) return;
    using namespace input_map;
    input_map::Mapping m = input_map::current();
    const int a = U.cap_action, colm = U.cap_col;
    int code = g_capture_key.exchange(-2);
    bool done = false;
    if (code == -1) done = true;
    else if (code == -3) {
        if (colm == kColPad) m.pad[a] = kPadNone;
        else m.keys[a][colm == kColAny ? 0 : colm] = kNoKey;
        input_map::set_current(m);
        done = true;
    } else if (code >= 0 && colm != kColPad) {
        if (const char* r = input_map::reserved_key(code)) U.cap_note = std::string("That key is used for ") + r;
        else {
            m.keys[a][colm == kColAny ? 0 : colm] = code;
            input_map::set_current(m);
            done = true;
        }
    }
    if (!done) {

        int pressed = -1;
        bool any = false;
        for (int p = 1; p < kPadCount; p++) {
            any |= U.values[p] > 0.3f;
            if (U.values[p] > 0.6f && pressed < 0) pressed = p;
        }
        if (!U.cap_pad_released) U.cap_pad_released = !any;
        else if (pressed > 0) {
            if (colm == kColPad || colm == kColAny) {
                m.pad[a] = pressed;
                input_map::set_current(m);
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

void controls_list(input_map::Mapping& m, float h) {
    if (ImGui::BeginTable("map", 4, ImGuiTableFlags_RowBg | ImGuiTableFlags_ScrollY | ImGuiTableFlags_BordersInnerH | ImGuiTableFlags_SizingStretchSame,
                          ImVec2(0, h))) {
        ImGui::TableSetupScrollFreeze(0, 1);
        ImGui::TableSetupColumn("Wii U input", ImGuiTableColumnFlags_WidthStretch, 1.6f);
        ImGui::TableSetupColumn("Key");
        ImGui::TableSetupColumn("Key 2");
        ImGui::TableSetupColumn("Controller");
        ImGui::TableHeadersRow();
        for (int a = 0; a < input_map::kActionCount; a++) {
            ImGui::PushID(a);
            ImGui::TableNextRow();
            ImGui::TableNextColumn();
            ImGui::AlignTextToFramePadding();
            const bool conflict = input_map::has_conflict(m, a);
            if (conflict) ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(1.0f, 0.62f, 0.20f, 1.0f));
            ImGui::TextUnformatted(input_map::action_label(a));
            if (conflict) ImGui::PopStyleColor();
            for (int col = 0; col < 3; col++) {
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

void save_gyro(const motion::Settings& g) {
    hostui::post([g] {
        motion::set_settings(g);
        for (const char* k : motion::kKeys) hostui::set(k, motion::value_of(g, k));
    });
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

void gyro_window(bool& open) {
    const char* title = "Gyro aiming##gyro";
    if (open) { ImGui::OpenPopup(title); open = false; }
    ImGui::SetNextWindowPos(ImGui::GetMainViewport()->GetCenter(), ImGuiCond_Appearing, ImVec2(0.5f, 0.5f));
    if (!ImGui::BeginPopupModal(title, nullptr, ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoSavedSettings)) return;
    motion::Settings g = motion::settings(), before = g;
    bool v;
    ImGui::PushTextWrapPos(ImGui::GetFontSize() * 34);
    note("Moving the GamePad looks around in first person. "
         "This works with the GamePad and the Pro Controller choice alike. The game's own "
         "Options > Gyro switch still decides whether it uses the motion, and it ignores the motion while the right "
         "stick is pushed.");
    for (int i = 0; i < motion::kSourceCount; i++)
        if (radio(motion::source_label(i), g.source == i)) g.source = i;
    if (motion::env_override()) note("NSMBU_GYRO=%s overrides the saved source.", env::GYRO());
    if (g.source == motion::kOff && motion::gyro_controllers() > 0) note("A controller with a gyro is connected: choose Controller gyro to use it.");
    if (g.source == motion::kController || g.source == motion::kCemuhook) {
        ImGui::TextUnformatted("Turn left/right by");
        for (int a = 0; a < motion::kAxisModeCount; a++) {
            ImGui::SameLine();
            if (radio(motion::axis_label(a), g.tuning.axis == a)) g.tuning.axis = a;
        }
        help("Player space: turning the controller left or right about the real vertical, however you hold it "
             "(recommended). Yaw: turning it about its own vertical axis (as if it lay flat). Roll: tilting it to "
             "the side like a steering wheel. Tilting its top up or down always looks up or down.");
    }
    const float lo = motion::Tuning::kMinSensitivity, hi = motion::Tuning::kMaxSensitivity;
    ImGui::SetNextItemWidth(220);
    ImGui::SliderFloat("Sensitivity left/right", &g.tuning.sensitivity_x, lo, hi, "%.2fx", ImGuiSliderFlags_Logarithmic);
    ImGui::SameLine(0, 16);
    if (check("Invert##x", g.tuning.invert_x, &v)) g.tuning.invert_x = v;
    ImGui::SetNextItemWidth(220);
    ImGui::SliderFloat("Sensitivity up/down", &g.tuning.sensitivity_y, lo, hi, "%.2fx", ImGuiSliderFlags_Logarithmic);
    ImGui::SameLine(0, 16);
    if (check("Invert##y", g.tuning.invert_y, &v)) g.tuning.invert_y = v;
    if (ImGui::Button("Default sensitivity"))
        g.tuning.sensitivity_x = g.tuning.sensitivity_y = motion::Tuning::kDefaultSensitivity;
    help("1.0 turns the view as far as moving a real Wii U GamePad by the same angle would (about twice the "
         "controller's turn); the default 0.5 lets the view follow the controller about one to one.");
    if (g.source == motion::kMouse) {
        ImGui::SetNextItemWidth(220);
        ImGui::SliderFloat("Mouse: degrees per point", &g.mouse_degrees, 0.01f, 1.0f, "%.3f", ImGuiSliderFlags_Logarithmic);
        help("How far one point of mouse movement turns the GamePad (before the sensitivity above). With Steam "
             "Input's gyro to mouse, tune this and Steam's own sensitivity together.");
        note("While the game aims, the pointer is captured and the mouse turns the GamePad.");
    }
    if (g.source == motion::kCemuhook) {
        static char host[256] = "";
        static int port = 0;
        static bool init = false;
        if (!init || ImGui::IsWindowAppearing()) { snprintf(host, sizeof host, "%s", g.dsu_host.c_str()); port = g.dsu_port; init = true; }
        ImGui::SetNextItemWidth(220);
        if (ImGui::InputText("Server", host, sizeof host, ImGuiInputTextFlags_EnterReturnsTrue) || ImGui::IsItemDeactivatedAfterEdit())
            g.dsu_host = host[0] ? host : "127.0.0.1";
        ImGui::SetNextItemWidth(120);
        if (ImGui::InputInt("Port", &port, 0, 0, ImGuiInputTextFlags_EnterReturnsTrue) || ImGui::IsItemDeactivatedAfterEdit())
            g.dsu_port = std::clamp(port, 1, 65535);
        ImGui::SetNextItemWidth(120);
        int slot = g.dsu_slot + 1;
        if (ImGui::SliderInt("Controller slot", &slot, 1, 4)) g.dsu_slot = slot - 1;
        note("A Cemuhook (DSU) server: DS4Windows, BetterJoy, SteamDeckGyroDSU or a phone app; default 127.0.0.1, port 26760.");
    }

    const char* pad_name = g.recenter_pad > 0 ? input_map::pad_label(g.recenter_pad) : "None";
    ImGui::SetNextItemWidth(220);
    if (ImGui::BeginCombo("Recalibrate: controller", pad_name)) {
        for (int p = 0; p < input_map::kPadCount; p++)
            if (ImGui::Selectable(p ? input_map::pad_label(p) : "None", g.recenter_pad == p)) g.recenter_pad = p;
        ImGui::EndCombo();
    }
    std::string key_name = g.recenter_key >= 0 ? input_map::key_label(g.recenter_key) : "None";
    ImGui::SetNextItemWidth(220);
    if (ImGui::BeginCombo("Recalibrate: key", key_name.c_str())) {
        if (ImGui::Selectable("None", g.recenter_key < 0)) g.recenter_key = -1;
        for (int k = 0; k < 256; k++) {
            std::string id = input_map::key_id(k);
            if (id.rfind("Key", 0) == 0) continue;
            if (ImGui::Selectable(input_map::key_label(k).c_str(), g.recenter_key == k)) g.recenter_key = k;
        }
        ImGui::EndCombo();
    }
    help("If the view drifts while the controller rests, recalibrate and put the controller down for a second: "
         "the gyro's offset is learnt anew. The button or key also reaches the game if the controls use it; pick "
         "a free one.");
    if (ImGui::Button("Recalibrate now")) motion::recalibrate();
    ImGui::SameLine();
    ImGui::TextUnformatted(motion::status().c_str());
    ImGui::PopTextWrapPos();
    if (!(g == before)) save_gyro(g);
    if (ImGui::Button("Close") || ImGui::IsKeyPressed(ImGuiKey_GamepadFaceRight, false)) ImGui::CloseCurrentPopup();
    ImGui::EndPopup();
}

void tab_controls() {
    bool v;
    input_map::Mapping m = input_map::current();

    const bool pro = input::pro_controller();
    ImGui::AlignTextToFramePadding();
    ImGui::TextUnformatted("Keyboard and controllers act as");
    ImGui::SameLine();

    if (radio("Wii U GamePad", !pro))
        hostui::post([] { hostui::set_pro_controller(false); hostui::set("proController", "0"); });
    ImGui::SameLine();
    if (radio("Wii U Pro Controller", pro))
        hostui::post([] { hostui::set_pro_controller(true); hostui::set("proController", "1"); });
    ImGui::SameLine(ImGui::GetContentRegionAvail().x + ImGui::GetCursorPosX() - ImGui::CalcTextSize("List view").x -
                    ImGui::GetFrameHeight() - ImGui::GetStyle().ItemInnerSpacing.x);
    ImGui::Checkbox("List view", &U.list_view);
    help("A plain table of all inputs instead of the controller drawing");

    const input_map::FaceLayout fl = input_map::face_layout(m);
    ImGui::AlignTextToFramePadding();
    ImGui::TextUnformatted("Face buttons");
    ImGui::SameLine();
    if (radio(input_map::face_layout_label(input_map::FaceLayout::kPosition), fl == input_map::FaceLayout::kPosition)) {
        input_map::apply_face_layout(m, input_map::FaceLayout::kPosition);
        input_map::set_current(m);
    }
    ImGui::SameLine();
    if (radio(input_map::face_layout_label(input_map::FaceLayout::kLabels), fl == input_map::FaceLayout::kLabels)) {
        input_map::apply_face_layout(m, input_map::FaceLayout::kLabels);
        input_map::set_current(m);
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
        status = std::string(input_map::action_label(U.hover_action)) + "  -  " + summary(m, U.hover_action) +
                 ".  A / Enter / click: rebind; X / Delete / right-click: clear.";
    } else if (int n = input_map::conflict_count(m)) {
        sc = ImVec4(1.0f, 0.62f, 0.20f, 1.0f);
        status = std::to_string(n) + " input(s) are bound to more than one action (orange).";
    } else {
        status = "Select a button on the controller or a chip to rebind it. Changes are saved to controls.json.";
    }
    const float bottom = ImGui::GetFrameHeightWithSpacing() * 2 + 4;
    const ImVec2 avail = ImGui::GetContentRegionAvail();
    const float h = std::max(160.0f, avail.y - bottom);
    U.hover_action = -1;
    if (U.list_view) {
        controls_list(m, h);
    } else {
        ControlsView cv;
        cv.m = m;
        cv.pro = pro;
        cv.cap_action = U.cap_action;
        cv.cap_col = U.cap_col;
        static float live[input_map::kPadCount];
        std::copy(std::begin(U.values), std::end(U.values), live);
        test_pad(live);
        cv.pad = live;
        cv.t = now_s();

        static bool keys[256];
        for (int i = 0; i < 256; i++) keys[i] = g_held_keys[i].load(std::memory_order_relaxed);
        cv.keys = keys;
        for (int a = 0; a < input_map::kActionCount; a++) {
            float x = 0;
            for (int key : m.keys[a])
                if (key != input_map::kNoKey && keys[key]) x = 1;
            if (int p = m.pad[a]; p != input_map::kPadNone) x = std::max(x, live[p]);
            cv.act[a] = x;
        }
        input_map::Mapping mm = m;
        mm.invert_camera_y = false;
        input::PadState ks = input_map::keyboard_state(mm, keys), cs = input_map::controller_state(mm, live);
        const bool kl = ks.lx || ks.ly, kr = ks.rx || ks.ry;
        cv.stick[0][0] = kl ? ks.lx : cs.lx;
        cv.stick[0][1] = kl ? ks.ly : cs.ly;
        cv.stick[1][0] = kr ? ks.rx : cs.rx;
        cv.stick[1][1] = kr ? ks.ry : cs.ry;
        draw_controls(cv, avail.x, h);
        U.hover_action = cv.hover_action;
        if (cv.start_action >= 0) start_capture(cv.start_action, cv.start_col);
        if (cv.clear_action >= 0) clear_binding(cv.clear_action, cv.clear_col);
    }
    ImGui::PushStyleColor(ImGuiCol_Text, sc);
    ImGui::TextUnformatted(status.c_str());
    ImGui::PopStyleColor();
    float dz = m.deadzone * 100.0f;
    ImGui::SetNextItemWidth(200);
    if (ImGui::SliderFloat("Stick dead zone", &dz, 0.0f, 90.0f, "%.0f%%")) {
        m.deadzone = std::clamp(dz / 100.0f, 0.0f, 0.9f);
        input_map::set_current(m);
    }
    ImGui::SameLine(0, 24);
    if (check("Invert camera up/down", m.invert_camera_y, &v)) {
        m.invert_camera_y = v;
        input_map::set_current(m);
    }
    ImGui::SameLine(0, 24);

    if (check("Rumble", rumble::enabled(), &v, input::has_rumble())) {
        rumble::set_enabled(v);
        hostui::post([v] { hostui::set("rumble", v ? "1" : "0"); });
    }
    help(input::has_rumble() ? "Controller vibration when the game asks for it. Off keeps the motors still."
                             : "Controller vibration: this host does not drive controller motors yet.");
    ImGui::SameLine(0, 24);
    static bool gyro_open = false;
    {
        const motion::Settings g = motion::settings();
        std::string label = std::string("Gyro: ") + (g.source == motion::kOff ? "off" : motion::source_label(g.source)) + "...";
        if (ImGui::Button(label.c_str())) gyro_open = true;
        help("Aim in first person by moving a controller with a gyro, a Cemuhook (DSU) source or the mouse (Steam Input)");
    }
    gyro_window(gyro_open);
    ImGui::SameLine(0, 24);
    if (ImGui::Button("Reset to defaults")) input_map::set_current(input_map::Mapping::defaults());
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

void tab_debug() {
}

void tab_about() {
    const char* env = env::LANGUAGE();
    const bool env_set = env && *env;
    char* end = nullptr;
    const long env_value = env_set ? strtol(env, &end, 10) : 1;
    const int env_language = env_set && !*end && env_value >= 0 && env_value < game_lang::kLanguages ? (int)env_value : 1;
    if (U.language < 0) {
        U.language = saved_language();
        const game_lang::Start now = game_lang::current();
        U.language_at_start = now.language;
        if (U.language_at_start < 0) {
            const game_lang::Start s = game_lang::choose(env_set ? env_language : U.language);
            U.language_at_start = s.language;
        }
    }
    heading("Console language (applies on the next start)");
    note("The game picks its text language from the console language; the game shows only the languages its disc carries.");
    ImGui::BeginDisabled(env_set);
    for (int i : {1, 2, 5, 3, 4, 8, 9, 10, 7, 0, 6, 11}) {
        if (i != 1 && i != 3 && i != 9 && i != 0) ImGui::SameLine();
        if (radio(game_lang::name(i), U.language == i)) choose_language(i);
    }
    ImGui::EndDisabled();
    if (env_set) {
        note("NSMBU_LANGUAGE=%s is set for this start and takes precedence.", env);
    } else if (U.language != U.language_at_start) {
        warn("%s is saved. Restart the game to apply it: the game reads the console language only when it starts.",
             game_lang::name(U.language));
    }
    heading("About");
    ImGui::Text("%s - native port (%s host, %s renderer)", app_title::kName, hostui::name(),
                render::api_name(render::active()));
#ifdef __APPLE__
    note("Settings overlay: ` or F12, Cmd+, or Settings... in the app menu, or hold Select / "
         "press Home on a controller. Esc, ` / F12 or B closes. "
#else
    note("Settings overlay: ` or F12, or hold Select / press Home on a controller. Esc, ` / F12 or B closes. "
#endif
         "L / R switch tabs on a controller. The game keeps running and sees no input while this menu is open.");
    note("When the game asks for text (your name), a text window appears over the game: type on the keyboard "
         "(Enter = OK, Esc = Cancel), click the on-screen keys, or use a controller: D-pad / left stick choose a key, "
         "A types it, B deletes, X adds a space, Y is Shift, L / R switch between letters, accents and symbols, "
         "Start confirms.");
    note("Built with Dear ImGui %s (MIT License, Omar Cornut and contributors).", IMGUI_VERSION);
}

static void lag_row(const char* name, double ms, double frameMs, const ImVec4& color) {
    if (ms < 0.1) return;
    ImGui::PushStyleColor(ImGuiCol_PlotHistogram, color);
    ImGui::ProgressBar((float)(frameMs > 0 ? std::min(ms / frameMs, 1.0) : 0), ImVec2(120, 13), "");
    ImGui::PopStyleColor();
    ImGui::SameLine();
    ImGui::Text("%.1f ms %s", ms, name);
}

void perf_window(bool menu_open) {
    const double t = now_s();
    if (U.fps_t0 == 0) { U.fps_t0 = t; U.fps_n0 = gx2::flips_presented(); }
    if (t - U.fps_t0 >= 0.5) {
        uint64_t n = gx2::flips_presented();
        U.fps = (double)(n - U.fps_n0) / (t - U.fps_t0);
        U.fps_t0 = t;
        U.fps_n0 = n;
    }
    float worst = 0, sum = 0;
    for (float f : U.frame_ms) { worst = std::max(worst, f); sum += f; }
    ImGui::SetNextWindowPos(ImVec2(10, 10), ImGuiCond_Always);
    ImGui::SetNextWindowBgAlpha(0.55f);
    ImGuiWindowFlags fl = ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoSavedSettings |
                          ImGuiWindowFlags_NoFocusOnAppearing | ImGuiWindowFlags_NoNav | ImGuiWindowFlags_NoInputs;
    if (ImGui::Begin("##perf", nullptr, fl)) {
        ImGui::PushStyleColor(ImGuiCol_TextDisabled, ImVec4(1.0f, 1.0f, 1.0f, 1.0f));
        ImGui::Text("%.0f fps   %.1f ms (worst %.1f)", U.fps, sum / 120.0f, worst);
        {

            static double t0 = 0; static uint64_t s0 = 0; static double rate = 30;
            if (t0 == 0 || t - t0 < 0) { t0 = t; s0 = interp::logic_steps(); }
            else if (t - t0 >= 2.0) { rate = (double)(interp::logic_steps() - s0) / (t - t0); t0 = t; s0 = interp::logic_steps(); }
            if (interp::mode() == 1 && !interp::paced_interpolation() && rate > 1 && rate < 26)
                ImGui::TextColored(ImVec4(1.0f, 0.75f, 0.3f, 1.0f), "Slow motion: %.0f of 30 logic steps/s. Turn on Keep game speed", rate);
        }
#ifdef __ANDROID__
        static AndroidTelemetry telemetry;
        static double next_read = 0;
        if (t >= next_read) { telemetry.read(); next_read = t + 2; }
        if (telemetry.busy >= 0) ImGui::Text("GPU busy %.0f%%", telemetry.busy);
        for (const auto& [name, value] : telemetry.temperatures)
            ImGui::Text("%s %.1f C", name.c_str(), value);
#endif
        ImGui::PlotLines("##ft", U.frame_ms, 120, U.frame_i, nullptr, 0.0f, 50.0f, ImVec2(220, 36));
        ImGui::TextDisabled("%s  %gx  %s", render::api_name(render::active()), hostui::res_scale(), interp::mode_name());
        if (float share = interp::paced_drawn_share(); share >= 0)
            ImGui::TextDisabled("in-between frames drawn: %.0f%%", share * 100.0f);
        rprof::Breakdown b;
        if (perf_detail_shown() && rprof::latest_breakdown(b)) {
            ImGui::SeparatorText("Lag breakdown");
            const ImVec4 red(0.9f, 0.35f, 0.35f, 1.0f), orange(0.95f, 0.65f, 0.2f, 1.0f), yellow(0.95f, 0.9f, 0.35f, 1.0f),
                         blue(0.45f, 0.65f, 1.0f, 1.0f), grey(0.55f, 0.55f, 0.6f, 1.0f);
            struct Row {
                const char* name;
                double ms;
                ImVec4 color;
            };
            Row rows[] = {
                {"GPU", b.gpuWaitMs, red},
                {"renderer CPU", b.renderCpuMs, orange},
                {"vsync present", b.acquireMs + b.presentMs, blue},
                {"game waits for renderer", b.gameSyncMs, yellow},
                {"write-back read", b.wbReadMs, yellow},
                {"renderer idle (waiting for the game)", b.renderIdleMs, grey},
            };
            std::sort(std::begin(rows), std::end(rows), [](const Row& a, const Row& r) { return a.ms > r.ms; });
            for (const Row& r : rows) lag_row(r.name, r.ms, b.ms, r.color);
            const Row& top = rows[0];
            if (top.ms > b.ms * 0.5)
                ImGui::TextColored(top.color, "main cause: %s", top.name);
            ImGui::TextDisabled("renders %.2f ops ms/frame, %.1f MiB uploads/frame, %.0f logic steps/s",
                                b.renderCpuMs, b.uploadMiB, b.logicRate);
        }
        ImGui::PopStyleColor();
    }
    ImGui::End();
    (void)menu_open;
}

void settings_window() {
    ImGuiIO& io = ImGui::GetIO();
    const ImVec2 ds = io.DisplaySize;
    const ImVec2 size(std::min(860.0f, ds.x * 0.94f), std::min(620.0f, ds.y * 0.92f));
    ImGui::SetNextWindowPos(ImVec2(ds.x * 0.5f, ds.y * 0.5f), ImGuiCond_Always, ImVec2(0.5f, 0.5f));
    ImGui::SetNextWindowSize(size, ImGuiCond_Always);
    ImGuiWindowFlags fl = ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoCollapse |
                          ImGuiWindowFlags_NoSavedSettings;
    bool open = true;
    if (U.just_opened) {
        ImGui::SetNextWindowFocus();
        U.just_opened = false;
    }
    if (ImGui::Begin(app_title::kSettings, &open, fl)) {

        if (U.cap_action < 0 && !ImGui::IsPopupOpen(nullptr, ImGuiPopupFlags_AnyPopupId)) {
            if (controller_pressed(input_map::kPadLB)) U.select_tab = (U.tab + kTabs - 1) % kTabs;
            if (controller_pressed(input_map::kPadRB)) U.select_tab = (U.tab + 1) % kTabs;
        }
        if (ImGui::BeginTabBar("tabs", ImGuiTabBarFlags_FittingPolicyShrink)) {
            for (int i = 0; i < kTabs; i++) {
                ImGuiTabItemFlags f = U.select_tab == i ? ImGuiTabItemFlags_SetSelected : 0;
                if (ImGui::BeginTabItem(kTabNames[i], nullptr, f)) {
                    if (U.tab != i) U.cap_action = -1, g_capturing_keys = false, U.cap_note.clear();
                    U.tab = i;
                    ImGui::BeginChild("page", ImVec2(0, 0), ImGuiChildFlags_NavFlattened);
                    switch (i) {
                    case kSaves: tab_saves(); break;
                    case kGraphics: tab_graphics(); break;
                    case kDisplay: tab_display(); break;
                    case kAudio: tab_audio(); break;
                    case kMods: tab_mods(); break;
                    case kControls: tab_controls(); break;
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
    }
    ImGui::End();

    if (!open) set_open(false);
    if (U.cap_action < 0 && controller_pressed(input_map::kPadB) && !g_pad_b_used && !ImGui::IsPopupOpen(nullptr, ImGuiPopupFlags_AnyPopupId))
        set_open(false);
    g_pad_b_used = false;
}

}

bool is_open() { return g_open.load(std::memory_order_relaxed); }
void set_open(bool open) {
    if (g_open.exchange(open) == open) return;
    for (auto& k : g_held_keys) k = false;
    input::release_keys();
    if (!open) g_wait_release = true;
    if (open) {
        mods::mouse_release();
        U.just_opened = true;
        U.slots_time = -1;
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
bool perf_detail_shown() { return g_perf_detail.load(std::memory_order_relaxed); }
void set_perf_detail_shown(bool on) {
    g_perf_detail = on;
    hostui::post([on] { hostui::set("perfDetail", on ? "1" : "0"); });
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
bool mouse_move(float nx, float ny) {
    if (!captures() || g_no_host) return false;
    push({Event::MousePos, 0, false, nx, ny, 0});
    return true;
}
bool mouse_button(int button, bool down) {
    if (!captures() || g_no_host) return false;
    push({Event::MouseButton, button, down, 0, 0, 0});
    return true;
}
bool mouse_wheel(float dx, float dy) {
    if (!captures() || g_no_host) return false;
    push({Event::Wheel, 0, false, dx, dy, 0});
    return true;
}
void set_density(float d) { g_density = d > 0.5f ? d : 1.0f; }

ImDrawData* frame(float pw, float ph, void (*renderer_init)()) {
    static TestSwitch test = parse_test();
    static bool prefs_read = false;
    if (!prefs_read) {
        prefs_read = true;
        std::string v;
        if (hostui::get("perfOverlay", v)) g_perf = v == "1";
        if (hostui::get("perfDetail", v)) g_perf_detail = v == "1";

        if (!env::PRO_CONTROLLER() && hostui::get("proController", v))
            hostui::post([pro = v == "1"] { hostui::set_pro_controller(pro); });

        if (!rumble::env_override() && hostui::get("rumble", v)) rumble::set_enabled(v != "0");

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
    if (U.last_present > 0) {
        U.frame_ms[U.frame_i] = (float)((t - U.last_present) * 1000.0);
        U.frame_i = (U.frame_i + 1) % 120;
    }
    U.last_present = t;
    g_last_frame = t;
    g_average.sample(t, gx2::flips_presented(), interp::logic_steps(), int(render::active()),
                     interp::mode(), interp::fps(), hostui::res_scale());
    read_controller();

    const bool open = is_open(), perf = perf_shown(), text = !open && text_entry::active();

    const std::string toast = open ? std::string() : ss::last_message();
    U.linearized = false;
    if (!open && !perf && !text && toast.empty()) {
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

    const float scale = std::max({1.0f, g_density.load(), ph / 820.0f});
    io.DisplaySize = ImVec2(pw / scale, ph / scale);
    io.DisplayFramebufferScale = ImVec2(scale, scale);
    io.DeltaTime = U.last_time > 0 ? (float)std::clamp(t - U.last_time, 1e-4, 0.25) : 1.0f / 60.0f;
    U.last_time = t;
    std::vector<Event> events;
    {
        std::lock_guard<std::mutex> lk(g_mu);
        events.swap(g_events);
    }
    for (const Event& e : events) {
        switch (e.kind) {
        case Event::Key:
            io.AddKeyEvent(ImGuiMod_Shift, e.mods & kShift);
            io.AddKeyEvent(ImGuiMod_Ctrl, e.mods & kCtrl);
            io.AddKeyEvent(ImGuiMod_Alt, e.mods & kAlt);
            io.AddKeyEvent(ImGuiMod_Super, e.mods & kSuper);
            if (ImGuiKey k = imgui_key(e.code); k != ImGuiKey_None) io.AddKeyEvent(k, e.down);
            break;
        case Event::MousePos: io.AddMousePosEvent(e.x * io.DisplaySize.x, e.y * io.DisplaySize.y); break;
        case Event::MouseButton: io.AddMouseButtonEvent(e.code, e.down); break;
        case Event::Wheel: io.AddMouseWheelEvent(e.x, e.y); break;
        default: break;
        }
    }
    apply_capture();
    feed_gamepad(io, open && U.cap_action < 0);
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
