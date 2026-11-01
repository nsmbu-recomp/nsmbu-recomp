

#include <SDL3/SDL.h>
#include <SDL3/SDL_main.h>

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cctype>
#include <cstring>
#include <deque>
#include <map>
#include <memory>
#include <mutex>
#include <string>
#include <vector>

#include <cerrno>
#include <cstdlib>
#include <filesystem>

#ifdef _WIN32
#include <windows.h>

#include "console_setup_win.h"
#else
#include <sys/stat.h>
#include <unistd.h>
#endif
#ifdef __APPLE__
#include <CoreGraphics/CoreGraphics.h>
#include <sys/sysctl.h>
#endif

#include "imgui.h"
#include "imgui_impl_sdl3.h"
#include "imgui_impl_sdlrenderer3.h"

#if defined(__APPLE__)
#define SETUP_IN_TERMINAL "tools/Setup in Terminal.command"
#elif defined(_WIN32)
#define SETUP_IN_TERMINAL "tools\\Setup in a console window.bat"
#else
#define SETUP_IN_TERMINAL "tools/setup-in-terminal.sh"
#endif

struct J {
    enum T { Null, Bool, Num, Str, Arr, Obj } t = Null;
    bool b = false;
    double n = 0;
    std::string s;
    std::vector<J> a;
    std::vector<std::pair<std::string, J>> o;

    J();
    J(const J&);
    J(J&&);
    J& operator=(const J&);
    J& operator=(J&&);
    ~J();

    const J* get(const char* k) const;
    std::string str(const char* k, const std::string& def = "") const {
        const J* v = get(k);
        return v && v->t == Str ? v->s : def;
    }
    bool boolean(const char* k) const {
        const J* v = get(k);
        return v && v->t == Bool && v->b;
    }
    double num(const char* k) const {
        const J* v = get(k);
        return v && v->t == Num ? v->n : 0;
    }
    bool is_null(const char* k) const {
        const J* v = get(k);
        return !v || v->t == Null;
    }
};

J::J() = default;
J::J(const J&) = default;
J::J(J&&) = default;
J& J::operator=(const J&) = default;
J& J::operator=(J&&) = default;
J::~J() = default;

const J* J::get(const char* k) const {
    for (auto& kv : o)
        if (kv.first == k) return &kv.second;
    return nullptr;
}

struct JParser {
    const char* p;
    const char* e;
    bool ok = true;
    void ws() {
        while (p < e && (*p == ' ' || *p == '\t' || *p == '\n' || *p == '\r')) p++;
    }
    static void put_utf8(std::string& out, unsigned cp) {
        if (cp < 0x80) out += (char)cp;
        else if (cp < 0x800) out += (char)(0xC0 | (cp >> 6)), out += (char)(0x80 | (cp & 0x3F));
        else if (cp < 0x10000)
            out += (char)(0xE0 | (cp >> 12)), out += (char)(0x80 | ((cp >> 6) & 0x3F)), out += (char)(0x80 | (cp & 0x3F));
        else
            out += (char)(0xF0 | (cp >> 18)), out += (char)(0x80 | ((cp >> 12) & 0x3F)),
                out += (char)(0x80 | ((cp >> 6) & 0x3F)), out += (char)(0x80 | (cp & 0x3F));
    }
    unsigned hex4() {
        if (e - p < 4) return ok = false, 0;
        unsigned v = 0;
        for (int i = 0; i < 4; i++) {
            char c = *p++;
            v <<= 4;
            if (c >= '0' && c <= '9') v |= c - '0';
            else if (c >= 'a' && c <= 'f') v |= c - 'a' + 10;
            else if (c >= 'A' && c <= 'F') v |= c - 'A' + 10;
            else ok = false;
        }
        return v;
    }
    std::string string() {
        std::string out;
        p++;
        while (p < e && *p != '"') {
            if (*p == '\\' && p + 1 < e) {
                p++;
                char c = *p++;
                switch (c) {
                case 'n': out += '\n'; break;
                case 't': out += '\t'; break;
                case 'r': out += '\r'; break;
                case 'b': out += '\b'; break;
                case 'f': out += '\f'; break;
                case 'u': {
                    unsigned cp = hex4();
                    if (cp >= 0xD800 && cp < 0xDC00 && e - p >= 6 && p[0] == '\\' && p[1] == 'u') {
                        p += 2;
                        unsigned lo = hex4();
                        cp = 0x10000 + ((cp - 0xD800) << 10) + (lo - 0xDC00);
                    }
                    put_utf8(out, cp);
                    break;
                }
                default: out += c;
                }
            } else {
                out += *p++;
            }
        }
        if (p < e) p++;
        else ok = false;
        return out;
    }
    J value() {
        J v;
        ws();
        if (p >= e) return ok = false, v;
        if (*p == '{') {
            v.t = J::Obj;
            p++;
            ws();
            if (p < e && *p == '}') return p++, v;
            while (ok && p < e) {
                ws();
                if (p >= e || *p != '"') return ok = false, v;
                std::string k = string();
                ws();
                if (p >= e || *p != ':') return ok = false, v;
                p++;
                v.o.emplace_back(k, value());
                ws();
                if (p < e && *p == ',') { p++; continue; }
                if (p < e && *p == '}') { p++; break; }
                return ok = false, v;
            }
        } else if (*p == '[') {
            v.t = J::Arr;
            p++;
            ws();
            if (p < e && *p == ']') return p++, v;
            while (ok && p < e) {
                v.a.push_back(value());
                ws();
                if (p < e && *p == ',') { p++; continue; }
                if (p < e && *p == ']') { p++; break; }
                return ok = false, v;
            }
        } else if (*p == '"') {
            v.t = J::Str;
            v.s = string();
        } else if (!strncmp(p, "true", 4)) {
            v.t = J::Bool, v.b = true, p += 4;
        } else if (!strncmp(p, "false", 5)) {
            v.t = J::Bool, p += 5;
        } else if (!strncmp(p, "null", 4)) {
            p += 4;
        } else {
            char* end = nullptr;
            v.t = J::Num;
            v.n = SDL_strtod(p, &end);
            if (end == p) ok = false;
            p = end;
        }
        return v;
    }
};

static bool parse_json(const std::string& s, J& out) {
    JParser jp{s.data(), s.data() + s.size()};
    out = jp.value();
    return jp.ok && out.t == J::Obj;
}

static std::string jstr(const std::string& s) {
    std::string o = "\"";
    for (unsigned char c : s) {
        if (c == '"' || c == '\\') o += '\\', o += (char)c;
        else if (c < 0x20) {
            char b[8];
            snprintf(b, sizeof b, "\\u%04x", c);
            o += b;
        } else o += (char)c;
    }
    return o + "\"";
}

struct Child {
    SDL_Process* proc = nullptr;
    std::string partial;
    std::deque<std::string> lines;
    bool exited = false;
    int exit_code = 0;

    bool start(const std::vector<std::string>& args, const std::string& cwd) {
        std::vector<const char*> argv;
        for (auto& a : args) argv.push_back(a.c_str());
        argv.push_back(nullptr);
        SDL_PropertiesID props = SDL_CreateProperties();
        SDL_SetPointerProperty(props, SDL_PROP_PROCESS_CREATE_ARGS_POINTER, (void*)argv.data());
        SDL_SetNumberProperty(props, SDL_PROP_PROCESS_CREATE_STDIN_NUMBER, SDL_PROCESS_STDIO_APP);
        SDL_SetNumberProperty(props, SDL_PROP_PROCESS_CREATE_STDOUT_NUMBER, SDL_PROCESS_STDIO_APP);
        SDL_SetBooleanProperty(props, SDL_PROP_PROCESS_CREATE_STDERR_TO_STDOUT_BOOLEAN, true);
        SDL_SetStringProperty(props, SDL_PROP_PROCESS_CREATE_WORKING_DIRECTORY_STRING, cwd.c_str());
#ifdef _WIN32
        SDL_SetBooleanProperty(props, SDL_PROP_PROCESS_CREATE_BACKGROUND_BOOLEAN, true);
#endif
        proc = SDL_CreateProcessWithProperties(props);
        SDL_DestroyProperties(props);
        return proc != nullptr;
    }

    void poll() {
        if (!proc) return;
        SDL_IOStream* out = SDL_GetProcessOutput(proc);
        char buf[16384];
        for (;;) {
            size_t n = out ? SDL_ReadIO(out, buf, sizeof buf) : 0;
            if (n == 0) break;
            partial.append(buf, n);
        }
        size_t pos;
        while ((pos = partial.find('\n')) != std::string::npos) {
            std::string l = partial.substr(0, pos);
            if (!l.empty() && l.back() == '\r') l.pop_back();
            lines.push_back(l);
            partial.erase(0, pos + 1);
        }
        if (!exited && SDL_WaitProcess(proc, false, &exit_code)) exited = true;
    }

    bool send(const std::string& json) {
        if (!proc || exited) return false;
        SDL_IOStream* in = SDL_GetProcessInput(proc);
        if (!in) return false;
        std::string l = json + "\n";
        bool ok = SDL_WriteIO(in, l.data(), l.size()) == l.size();
        SDL_FlushIO(in);
        return ok;
    }

    void stop(bool kill) {
        if (!proc) return;
        if (kill && !exited) SDL_KillProcess(proc, true);
        else if (!exited) {
            send("{\"cmd\":\"quit\"}");
            for (int i = 0; i < 50 && !SDL_WaitProcess(proc, false, &exit_code); i++) SDL_Delay(20);
            if (!SDL_WaitProcess(proc, false, &exit_code)) SDL_KillProcess(proc, true);
        }
        SDL_DestroyProcess(proc);
        proc = nullptr;
    }
};

static bool run_quick(const std::vector<std::string>& args) {
    std::vector<const char*> argv;
    for (auto& a : args) argv.push_back(a.c_str());
    argv.push_back(nullptr);
    SDL_PropertiesID props = SDL_CreateProperties();
    SDL_SetPointerProperty(props, SDL_PROP_PROCESS_CREATE_ARGS_POINTER, (void*)argv.data());
    SDL_SetNumberProperty(props, SDL_PROP_PROCESS_CREATE_STDOUT_NUMBER, SDL_PROCESS_STDIO_NULL);
    SDL_SetNumberProperty(props, SDL_PROP_PROCESS_CREATE_STDERR_NUMBER, SDL_PROCESS_STDIO_NULL);
    SDL_Process* p = SDL_CreateProcessWithProperties(props);
    SDL_DestroyProperties(props);
    if (!p) return false;
    int code = -1;
    SDL_WaitProcess(p, true, &code);
    SDL_DestroyProcess(p);
    return code == 0;
}

static std::string g_exe, g_game_dir, g_data_dir;

static bool launch_game() {
    std::string save = g_data_dir + "/nsmbu/save";
#ifdef _WIN32
    auto wide = [](const std::string& u) {
        int n = MultiByteToWideChar(CP_UTF8, 0, u.c_str(), -1, nullptr, 0);
        std::wstring w(n > 0 ? n - 1 : 0, L'\0');
        if (n > 1) MultiByteToWideChar(CP_UTF8, 0, u.c_str(), -1, w.data(), n);
        return w;
    };
    std::wstring cmd = L"\"" + wide(g_exe) + L"\" --game \"" + wide(g_game_dir) + L"\" --save \"" + wide(save) + L"\"";
    STARTUPINFOW si = {sizeof si};
    PROCESS_INFORMATION pi = {};
    std::wstring cwd = wide(g_data_dir);
    if (!CreateProcessW(wide(g_exe).c_str(), cmd.data(), nullptr, nullptr, FALSE, CREATE_NO_WINDOW, nullptr, cwd.c_str(), &si, &pi))
        return false;
    CloseHandle(pi.hThread);
    CloseHandle(pi.hProcess);
    return true;
#else
    if (chdir(g_data_dir.c_str()) != 0) return false;
    const char* argv[] = {g_exe.c_str(), "--game", g_game_dir.c_str(), "--save", save.c_str(), nullptr};
    execv(g_exe.c_str(), (char* const*)argv);
    return false;
#endif
}

static bool shift_held() {
#if defined(__APPLE__)
    return (CGEventSourceFlagsState(kCGEventSourceStateCombinedSessionState) & kCGEventFlagMaskShift) != 0;
#elif defined(_WIN32)
    return (GetAsyncKeyState(VK_SHIFT) & 0x8000) != 0;
#else
    return false;
#endif
}

enum class Screen { Starting, NeedCLT, Menu, Welcome, Source, Keys, Installing, Error, Save, Done, Shaders, Fatal };

static const char* screen_name(Screen s) {
    switch (s) {
    case Screen::Starting: return "starting";
    case Screen::NeedCLT: return "clt";
    case Screen::Menu: return "menu";
    case Screen::Welcome: return "welcome";
    case Screen::Source: return "source";
    case Screen::Keys: return "keys";
    case Screen::Installing: return "installing";
    case Screen::Error: return "error";
    case Screen::Save: return "save";
    case Screen::Done: return "done";
    case Screen::Shaders: return "shaders";
    case Screen::Fatal: return "fatal";
    }
    return "?";
}

struct Step {
    std::string id, title;
    int state = 0;
    double done = 0, total = 0;
    std::string detail;
};

struct App {
    std::string pkg;
    std::vector<std::string> passthru;
    Child child;
    Screen screen = Screen::Starting;
    std::string fatal;
    std::string fatal_hint;
    std::string fatal_folder;
    std::string fatal_log;

    bool hello = false;
    std::string version, data_dir, app_dir, log_path, platform;
    bool installed = false, game_files = false, save_exists = false;
    std::string installed_version;

    int next_id = 1, pending_id = 0;
    std::string pending_cmd;

    std::string source_path, source_kind;
    std::string probe_msg;
    bool probe_ok = false;
    std::string source_title, source_title_id, source_title_id_text, source_build;
    std::string source_update_title, source_update_sha256;
    int source_update_version = 0;
    int source_files = 0;
    std::string disc_key_found, disc_key_file, common_key_found, common_key_file;
    std::string update_dir_found, update_dir;
    bool update_skipped = false;
    int disc_mode = 0, common_mode = 0;
    char disc_paste[128] = {}, paste[128] = {};
    std::string keys_msg;

    std::vector<Step> steps;
    int current = -1;
    std::string install_source;
    std::string error_msg, error_back;
    std::string result_app;

    bool portable = false, legacy = false;
    std::string package, game_dir;
    double free_bytes = 0, source_bytes = 0, toolchain_bytes = 0;
    bool opt_remove_toolchain = false, opt_shortcut = false;
    std::deque<std::pair<std::string, std::string>> queue;
    std::string after;
    bool exec_game = false;

    int save_kind = 0;
    std::string save_path, save_msg;
    bool confirm_replace = false;

    Screen shader_back = Screen::Menu;
    bool shader_running = false, shader_done = false;
    double shader_done_n = 0, shader_total = 0, shader_started = 0;
    std::string shader_phase, shader_detail, shader_error;

    std::vector<std::string> log;
    bool log_scroll = false;
    std::string toast;
    double toast_until = 0;

    std::mutex dlg_mu;
    std::string dlg_target, dlg_result;
    bool dlg_done = false;

    double clt_next_check = 0;
    bool clt_install_started = false;

    bool self_test = false;
    std::string shots_dir;
    J script;
    size_t script_pos = 0;
    int stable_frames = 0, ready_frames = 0;
    bool shot_taken = false;
    double step_started = 0;
    std::string autoclick;
    std::string shot_pending;
    int exit_code = -1;
    std::string last_screen;
};

static App A;

static void addlog(const std::string& s) {
    A.log.push_back(s);
    A.log_scroll = true;
}

static void go(Screen s) {
    A.screen = s;
}

static void start_shaders(Screen back);

static void fail(const std::string& what, const std::string& todo, const std::string& folder = "");

static int request(const std::string& cmd, const std::string& fields) {
    int id = A.next_id++;
    std::string json = "{\"cmd\":" + jstr(cmd) + ",\"id\":" + std::to_string(id) + (fields.empty() ? "" : "," + fields) + "}";
    A.pending_id = id;
    A.pending_cmd = cmd;
    if (!A.child.send(json))
        fail("The setup process is not running any more (request \"" + cmd + "\" could not be sent" +
                 (A.child.exited ? ", it ended with exit code " + std::to_string(A.child.exit_code) : std::string()) + ").",
             "Open NSMBU again; finished steps are kept. If it happens again, click \"Copy log\" and attach the "
             "log to a bug report.");
    return id;
}

static bool busy() { return A.pending_id != 0; }

static ImFont* g_font = nullptr;
static ImVec4 ACCENT(0.11f, 0.47f, 0.76f, 1.0f);
static ImVec4 GOOD(0.16f, 0.60f, 0.30f, 1.0f);
static ImVec4 BAD(0.80f, 0.20f, 0.18f, 1.0f);
static ImVec4 MUTED(0.42f, 0.45f, 0.50f, 1.0f);
static float g_ui_scale = 1.0f;
static bool g_dark = false;
static Uint8 g_clear_r = 248, g_clear_g = 250, g_clear_b = 251;

static void load_fonts(float scale) {
    ImGuiIO& io = ImGui::GetIO();
    const char* candidates[] = {
#if defined(__APPLE__)
        "/System/Library/Fonts/SFNS.ttf", "/System/Library/Fonts/Helvetica.ttc", "/Library/Fonts/Arial.ttf",
#elif defined(_WIN32)
        "C:\\Windows\\Fonts\\segoeui.ttf", "C:\\Windows\\Fonts\\arial.ttf",
#else
        "/usr/share/fonts/truetype/noto/NotoSans-Regular.ttf", "/usr/share/fonts/noto/NotoSans-Regular.ttf",
        "/usr/share/fonts/google-noto/NotoSans-Regular.ttf", "/usr/share/fonts/truetype/dejavu/DejaVuSans.ttf",
        "/usr/share/fonts/TTF/DejaVuSans.ttf", "/usr/share/fonts/dejavu/DejaVuSans.ttf",
        "/usr/share/fonts/truetype/ubuntu/Ubuntu-R.ttf", "/usr/share/fonts/cantarell/Cantarell-Regular.otf",
#endif
    };
    (void)scale;
    for (const char* c : candidates) {
        SDL_PathInfo info;
        if (SDL_GetPathInfo(c, &info) && info.type == SDL_PATHTYPE_FILE) {
            g_font = io.Fonts->AddFontFromFileTTF(c, 17.0f);
            if (g_font) break;
        }
    }
    if (!g_font) {
        ImFontConfig cfg;
        cfg.SizePixels = 17.0f;
        g_font = io.Fonts->AddFontDefaultVector(&cfg);
    }
}

static bool system_dark() { return SDL_GetSystemTheme() == SDL_SYSTEM_THEME_DARK; }

static void apply_style(float scale) {
    g_ui_scale = scale;
    g_dark = system_dark();
    ImGuiStyle& st = ImGui::GetStyle();
    st = ImGuiStyle();
    if (g_dark) ImGui::StyleColorsDark();
    else ImGui::StyleColorsLight();
    st.WindowPadding = ImVec2(28, 22);
    st.FramePadding = ImVec2(12, 7);
    st.ItemSpacing = ImVec2(10, 10);
    st.FrameRounding = 6;
    st.GrabRounding = 6;
    st.ChildRounding = 8;
    st.PopupRounding = 8;
    st.WindowRounding = 0;
    st.FrameBorderSize = 1;
    ImVec4* c = st.Colors;
    if (g_dark) {
        ACCENT = ImVec4(0.35f, 0.68f, 0.98f, 1);
        GOOD = ImVec4(0.35f, 0.78f, 0.48f, 1);
        BAD = ImVec4(0.95f, 0.42f, 0.40f, 1);
        MUTED = ImVec4(0.68f, 0.71f, 0.76f, 1);
        g_clear_r = 30;
        g_clear_g = 33;
        g_clear_b = 38;
        c[ImGuiCol_WindowBg] = ImVec4(0.12f, 0.13f, 0.15f, 1);
        c[ImGuiCol_ChildBg] = ImVec4(0.16f, 0.17f, 0.20f, 1);
        c[ImGuiCol_PopupBg] = ImVec4(0.14f, 0.15f, 0.18f, 1);
        c[ImGuiCol_TitleBg] = c[ImGuiCol_PopupBg];
        c[ImGuiCol_TitleBgActive] = c[ImGuiCol_PopupBg];
        c[ImGuiCol_TitleBgCollapsed] = c[ImGuiCol_PopupBg];
        c[ImGuiCol_ModalWindowDimBg] = ImVec4(0.0f, 0.0f, 0.0f, 0.55f);
        c[ImGuiCol_Border] = ImVec4(0.32f, 0.35f, 0.40f, 1);
        c[ImGuiCol_Text] = ImVec4(0.92f, 0.93f, 0.95f, 1);
        c[ImGuiCol_Button] = ImVec4(0.22f, 0.24f, 0.28f, 1);
        c[ImGuiCol_ButtonHovered] = ImVec4(0.30f, 0.34f, 0.40f, 1);
        c[ImGuiCol_ButtonActive] = ImVec4(0.18f, 0.30f, 0.42f, 1);
        c[ImGuiCol_FrameBg] = ImVec4(0.18f, 0.19f, 0.22f, 1);
        c[ImGuiCol_FrameBgHovered] = ImVec4(0.24f, 0.26f, 0.30f, 1);
        c[ImGuiCol_FrameBgActive] = ImVec4(0.28f, 0.32f, 0.38f, 1);
        c[ImGuiCol_Header] = ImVec4(0.22f, 0.25f, 0.30f, 1);
        c[ImGuiCol_HeaderHovered] = ImVec4(0.28f, 0.32f, 0.38f, 1);
        c[ImGuiCol_HeaderActive] = ImVec4(0.32f, 0.38f, 0.46f, 1);
        c[ImGuiCol_Separator] = ImVec4(0.32f, 0.35f, 0.40f, 1);
        c[ImGuiCol_ScrollbarBg] = ImVec4(0.12f, 0.13f, 0.15f, 1);
        c[ImGuiCol_ScrollbarGrab] = ImVec4(0.35f, 0.38f, 0.42f, 1);
    } else {
        ACCENT = ImVec4(0.11f, 0.47f, 0.76f, 1);
        GOOD = ImVec4(0.16f, 0.60f, 0.30f, 1);
        BAD = ImVec4(0.80f, 0.20f, 0.18f, 1);
        MUTED = ImVec4(0.42f, 0.45f, 0.50f, 1);
        g_clear_r = 248;
        g_clear_g = 250;
        g_clear_b = 251;
        c[ImGuiCol_WindowBg] = ImVec4(0.975f, 0.98f, 0.985f, 1);
        c[ImGuiCol_ChildBg] = ImVec4(1, 1, 1, 1);
        c[ImGuiCol_PopupBg] = ImVec4(1, 1, 1, 1);
        c[ImGuiCol_TitleBg] = c[ImGuiCol_PopupBg];
        c[ImGuiCol_TitleBgActive] = c[ImGuiCol_PopupBg];
        c[ImGuiCol_TitleBgCollapsed] = c[ImGuiCol_PopupBg];
        c[ImGuiCol_ModalWindowDimBg] = ImVec4(0.0f, 0.0f, 0.0f, 0.35f);
        c[ImGuiCol_Border] = ImVec4(0.82f, 0.85f, 0.88f, 1);
        c[ImGuiCol_Text] = ImVec4(0.12f, 0.14f, 0.17f, 1);
        c[ImGuiCol_Button] = ImVec4(0.92f, 0.94f, 0.96f, 1);
        c[ImGuiCol_ButtonHovered] = ImVec4(0.85f, 0.90f, 0.95f, 1);
        c[ImGuiCol_ButtonActive] = ImVec4(0.78f, 0.86f, 0.94f, 1);
        c[ImGuiCol_FrameBg] = ImVec4(1, 1, 1, 1);
        c[ImGuiCol_Header] = ImVec4(0.90f, 0.93f, 0.96f, 1);
        c[ImGuiCol_HeaderHovered] = ImVec4(0.85f, 0.90f, 0.95f, 1);
    }
    c[ImGuiCol_PlotHistogram] = ACCENT;
    c[ImGuiCol_CheckMark] = ACCENT;
    st.ScaleAllSizes(scale);
    st.FontScaleDpi = scale;
}

static void heading(const char* text, float size = 27.0f) {
    ImGui::PushFont(nullptr, size);
    ImGui::TextUnformatted(text);
    ImGui::PopFont();
}

static void muted(const std::string& t) {
    ImGui::PushStyleColor(ImGuiCol_Text, MUTED);
    ImGui::TextWrapped("%s", t.c_str());
    ImGui::PopStyleColor();
}

static void button_caption(const std::string& t) {
    ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(ImGui::GetStyle().ItemSpacing.x, 4.0f));
    muted(t);
    ImGui::PopStyleVar();
    ImGui::Dummy(ImVec2(0, 12.0f));
}

static void colored(const ImVec4& col, const std::string& t) {
    ImGui::PushStyleColor(ImGuiCol_Text, col);
    ImGui::TextWrapped("%s", t.c_str());
    ImGui::PopStyleColor();
}

static void mark(int state) {
    float sz = ImGui::GetFontSize();
    ImVec2 p = ImGui::GetCursorScreenPos();
    ImDrawList* dl = ImGui::GetWindowDrawList();
    ImVec2 c(p.x + sz * 0.5f, p.y + sz * 0.55f);
    float r = sz * 0.45f;
    if (state == 2) {
        dl->AddCircleFilled(c, r, ImGui::GetColorU32(GOOD));
        dl->AddLine(ImVec2(c.x - r * 0.45f, c.y), ImVec2(c.x - r * 0.1f, c.y + r * 0.38f), IM_COL32_WHITE, sz * 0.12f);
        dl->AddLine(ImVec2(c.x - r * 0.1f, c.y + r * 0.38f), ImVec2(c.x + r * 0.5f, c.y - r * 0.35f), IM_COL32_WHITE, sz * 0.12f);
    } else if (state == 3) {
        dl->AddCircleFilled(c, r, ImGui::GetColorU32(BAD));
        float d = r * 0.4f;
        dl->AddLine(ImVec2(c.x - d, c.y - d), ImVec2(c.x + d, c.y + d), IM_COL32_WHITE, sz * 0.12f);
        dl->AddLine(ImVec2(c.x - d, c.y + d), ImVec2(c.x + d, c.y - d), IM_COL32_WHITE, sz * 0.12f);
    } else if (state == 1) {
        float t = (float)ImGui::GetTime() * 6.0f;
        dl->PathArcTo(c, r * 0.85f, t, t + 4.4f, 24);
        dl->PathStroke(ImGui::GetColorU32(ACCENT), sz * 0.14f);
    } else {
        dl->AddCircle(c, r * 0.85f, ImGui::GetColorU32(MUTED), 24, sz * 0.08f);
    }
    ImGui::Dummy(ImVec2(sz * 1.1f, sz));
    ImGui::SameLine();
}

static bool button(const char* label, const ImVec2& size = ImVec2(0, 0), bool primary = false, bool enabled = true) {
    if (!enabled) ImGui::BeginDisabled();
    if (primary) {
        ImGui::PushStyleColor(ImGuiCol_Button, ACCENT);
        ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.15f, 0.53f, 0.83f, 1));
        ImGui::PushStyleColor(ImGuiCol_ButtonActive, ImVec4(0.08f, 0.40f, 0.66f, 1));
        ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(1, 1, 1, 1));
    }
    bool pressed = ImGui::Button(label, size);
    if (primary) ImGui::PopStyleColor(4);
    if (!enabled) ImGui::EndDisabled();
    std::string visible = label;
    if (size_t h = visible.find("##"); h != std::string::npos) visible = visible.substr(0, h);
    if (enabled && !A.autoclick.empty() && A.autoclick == visible) {
        A.autoclick.clear();
        pressed = true;
    }
    return pressed;
}

static bool radio(const char* label, int* v, int value) {
    ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(4, 3));
    bool r = ImGui::RadioButton(label, v, value);
    ImGui::PopStyleVar();
    return r;
}

static bool checkbox(const char* label, bool* v) {
    ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(4, 3));
    bool r = ImGui::Checkbox(label, v);
    ImGui::PopStyleVar();
    return r;
}

static float button_w(const char* label) {
    return ImGui::CalcTextSize(label, nullptr, true).x + ImGui::GetStyle().FramePadding.x * 2 + 24;
}

static float footer_reserve() {
    return ImGui::GetFrameHeight() + 8 + ImGui::GetStyle().WindowPadding.y + ImGui::GetStyle().ItemSpacing.y;
}

static int footer(std::initializer_list<const char*> labels, int primary = -1, int disabled_mask = 0) {
    float h = ImGui::GetFrameHeight() + 8;
    float pad = ImGui::GetStyle().WindowPadding.y;
    float y = ImGui::GetWindowHeight() - pad - h;
    if (ImGui::GetCursorPosY() < y) ImGui::SetCursorPosY(y);
    else ImGui::Dummy(ImVec2(0, ImGui::GetStyle().ItemSpacing.y));
    float total = 0;
    for (const char* l : labels) total += button_w(l) + ImGui::GetStyle().ItemSpacing.x;
    float x = ImGui::GetWindowWidth() - pad - total;
    if (x < pad) x = pad;
    ImGui::SetCursorPosX(x);
    int i = 0, hit = -1;
    for (const char* l : labels) {
        if (i) ImGui::SameLine();
        if (button(l, ImVec2(button_w(l), h), i == primary, !(disabled_mask & (1 << i)))) hit = i;
        i++;
    }
    return hit;
}

static void show_toast(const std::string& t) {
    A.toast = t;
    A.toast_until = ImGui::GetTime() + 2.5;
}

static std::string read_file(const std::string& path) {
    size_t n = 0;
    void* d = path.empty() ? nullptr : SDL_LoadFile(path.c_str(), &n);
    if (!d) return "";
    std::string s((const char*)d, n);
    SDL_free(d);
    return s;
}

static void copy_log() {
    std::string text = read_file(A.log_path);
    text += "\n---- installer window ----\n";
    if (!A.fatal.empty()) text += "Setup could not continue: " + A.fatal + "\n" + A.fatal_hint + "\n";
    for (auto& l : A.log) text += l + "\n";
    SDL_SetClipboardText(text.c_str());
    show_toast("The log is on the clipboard.");
}

static std::string url_encode(const std::string& s) {
    std::string out;
    out.reserve(s.size() * 3);
    for (unsigned char c : s) {
        if (isalnum(c) || c == '-' || c == '_' || c == '.' || c == '~') out.push_back((char)c);
        else if (c == ' ') out += "%20";
        else if (c == '\n') out += "%0A";
        else if (c == '\r') continue;
        else {
            char b[4];
            snprintf(b, sizeof b, "%%%02X", c);
            out += b;
        }
    }
    return out;
}

static std::string trim_ws(std::string s) {
    while (!s.empty() && isspace((unsigned char)s.front())) s.erase(s.begin());
    while (!s.empty() && isspace((unsigned char)s.back())) s.pop_back();
    return s;
}

static std::string setup_platform_label() {
#if defined(_WIN32)
    return "Windows";
#elif defined(__APPLE__)
    return "macOS";
#elif defined(__aarch64__) || defined(__arm64__)
    return "Linux (arm64)";
#else
    return "Linux (x86-64)";
#endif
}

static std::string setup_hardware_label() {
    std::string cpu;
#if defined(_WIN32)
    wchar_t buf[256];
    DWORD size = sizeof buf;
    if (RegGetValueW(HKEY_LOCAL_MACHINE, L"HARDWARE\\DESCRIPTION\\System\\CentralProcessor\\0",
                     L"ProcessorNameString", RRF_RT_REG_SZ, nullptr, buf, &size) == ERROR_SUCCESS) {
        char out[256];
        if (WideCharToMultiByte(CP_UTF8, 0, buf, -1, out, sizeof out, nullptr, nullptr) > 0) cpu = trim_ws(out);
    }
#elif defined(__APPLE__)
    char buf[256] = {};
    size_t n = sizeof buf;
    if (sysctlbyname("machdep.cpu.brand_string", buf, &n, nullptr, 0) == 0 && buf[0]) cpu = trim_ws(buf);
    else {
        n = sizeof buf;
        if (sysctlbyname("hw.model", buf, &n, nullptr, 0) == 0 && buf[0]) cpu = trim_ws(buf);
    }
#else
    if (FILE* f = fopen("/proc/cpuinfo", "r")) {
        char line[512];
        while (fgets(line, sizeof line, f)) {
            const char* keys[] = {"model name", "Hardware", "cpu model"};
            for (const char* key : keys) {
                size_t klen = strlen(key);
                if (strncmp(line, key, klen) != 0) continue;
                char* colon = strchr(line, ':');
                if (!colon) continue;
                cpu = trim_ws(colon + 1);
                while (!cpu.empty() && (cpu.back() == '\n' || cpu.back() == '\r')) cpu.pop_back();
                if (!cpu.empty()) break;
            }
            if (!cpu.empty()) break;
        }
        fclose(f);
    }
#endif
    std::string gpu;
#if !defined(_WIN32) && !defined(__APPLE__)
    if (FILE* f = popen("lspci -nn 2>/dev/null", "r")) {
        char line[512];
        while (fgets(line, sizeof line, f)) {
            if (!strstr(line, "VGA compatible controller") && !strstr(line, "3D controller") &&
                !strstr(line, "Display controller"))
                continue;
            char* colon = strstr(line, ": ");
            if (!colon) continue;
            gpu = trim_ws(colon + 2);
            while (!gpu.empty() && (gpu.back() == '\n' || gpu.back() == '\r')) gpu.pop_back();
            if (!gpu.empty()) break;
        }
        pclose(f);
    }
#endif
    std::string ram;
    int mb = SDL_GetSystemRAM();
    if (mb > 0) {
        int gb = (mb + 512) / 1024;
        if (gb < 1) gb = 1;
        ram = std::to_string(gb) + " GB";
    }
    std::string out;
    if (!cpu.empty()) out = cpu;
    if (!gpu.empty()) {
        if (!out.empty()) out += ", ";
        out += gpu;
    }
    if (!ram.empty()) {
        if (!out.empty()) out += ", ";
        out += ram;
    }
    return out.empty() ? "unknown" : out;
}

static void open_folder(const std::string& path);

static void report_issue() {
    std::string err = !A.error_msg.empty() ? A.error_msg : A.fatal;
    if (!A.fatal_hint.empty()) err += (err.empty() ? "" : "\n\n") + A.fatal_hint;
    if (err.empty()) err = "Setup failed (see logs).";
    if (err.size() > 2000) err = err.substr(0, 2000) + "\n…\n";
    std::string url = "https://github.com/nsmbu-recomp/nsmbu-recomp/issues/new?template=2-setup-or-other-bug.yml";
    url += "&version=" + url_encode(A.version);
    url += "&platform=" + url_encode(setup_platform_label());
    url += "&hardware=" + url_encode(setup_hardware_label());
    url += "&app_info=" + url_encode(err);
    if (!SDL_OpenURL(url.c_str())) show_toast(std::string("Could not open the browser: ") + SDL_GetError());
    std::string folder = A.data_dir;
    if (folder.empty() && !A.log_path.empty()) {
        size_t slash = A.log_path.find_last_of("/\\");
        if (slash != std::string::npos) folder = A.log_path.substr(0, slash);
    }
    if (!folder.empty()) open_folder(folder);
}

static std::string file_url(const std::string& path) {
    std::string u = "file://";
#ifdef _WIN32
    u += "/";
#endif
    for (unsigned char c : path) {
        if (isalnum(c) || strchr("/-_.~:", c)) u += (char)(c == '\\' ? '/' : c);
        else if (c == '\\') u += '/';
        else {
            char b[4];
            snprintf(b, sizeof b, "%%%02X", c);
            u += b;
        }
    }
    return u;
}

static void open_folder(const std::string& path) {
    SDL_OpenURL(file_url(path).c_str());
}

static void SDLCALL dialog_cb(void*, const char* const* files, int) {
    std::lock_guard<std::mutex> lk(A.dlg_mu);
    A.dlg_result = (files && files[0]) ? files[0] : "";
    A.dlg_done = true;
}

static SDL_Window* g_window = nullptr;

static void choose_file(const std::string& target, const SDL_DialogFileFilter* filters, int nf) {
    {
        std::lock_guard<std::mutex> lk(A.dlg_mu);
        A.dlg_target = target;
        A.dlg_done = false;
    }
    SDL_ShowOpenFileDialog(dialog_cb, nullptr, g_window, filters, nf, nullptr, false);
}

static void choose_folder(const std::string& target) {
    {
        std::lock_guard<std::mutex> lk(A.dlg_mu);
        A.dlg_target = target;
        A.dlg_done = false;
    }
    SDL_ShowOpenFolderDialog(dialog_cb, nullptr, g_window, nullptr, false);
}

static void clear_source_identity() {
    A.source_title.clear();
    A.source_title_id.clear();
    A.source_title_id_text.clear();
    A.source_build.clear();
    A.source_update_title.clear();
    A.source_update_sha256.clear();
    A.source_update_version = 0;
    A.source_bytes = 0;
    A.source_files = 0;
}

static void apply_source_identity(const J& ev) {
    if (ev.get("title")) A.source_title = ev.str("title");
    if (ev.get("title_id")) A.source_title_id = ev.str("title_id");
    if (ev.get("title_id_text")) A.source_title_id_text = ev.str("title_id_text");
    if (ev.get("build")) A.source_build = ev.str("build");
    if (ev.get("update_title")) A.source_update_title = ev.str("update_title");
    if (ev.get("update_sha256")) A.source_update_sha256 = ev.str("update_sha256");
    if (ev.get("update_version")) A.source_update_version = (int)ev.num("update_version");
    if (ev.get("bytes")) A.source_bytes = ev.num("bytes");
    if (ev.get("files")) A.source_files = (int)ev.num("files");
}

static void set_source(const std::string& path) {
    A.source_path = path;
    A.probe_ok = false;
    A.probe_msg = "Checking...";
    clear_source_identity();
    A.disc_key_found.clear();
    A.common_key_found.clear();
    A.disc_key_file.clear();
    A.update_dir_found.clear();
    A.update_dir.clear();
    A.update_skipped = false;
    A.keys_msg.clear();
    SDL_memset(A.disc_paste, 0, sizeof A.disc_paste);
    SDL_memset(A.paste, 0, sizeof A.paste);
    request("probe", "\"path\":" + jstr(path));
}

static void keys_changed() {
    if (A.pending_cmd != "check_keys") A.keys_msg.clear();
}

static void set_disc_key_file(const std::string& p) { A.disc_key_file = p, keys_changed(); }
static void set_common_key_file(const std::string& p) { A.common_key_file = p, keys_changed(); }
static void set_update_dir(const std::string& p) {
    A.update_dir = p;
    A.update_skipped = false;
    keys_changed();
}
static void skip_update_dir() {
    A.update_dir.clear();
    A.update_skipped = true;
    keys_changed();
}
static std::string chosen_update_dir() {
    if (A.update_skipped) return {};
    return !A.update_dir.empty() ? A.update_dir : A.update_dir_found;
}
static void set_disc_mode(int m) {
    if (A.disc_mode != m) A.disc_mode = m, keys_changed();
}
static void set_common_mode(int m) {
    if (A.common_mode != m) A.common_mode = m, keys_changed();
}
static void set_save_path(const std::string& p) { A.save_path = p, A.save_msg.clear(); }
static void set_save_kind(int k) {
    if (A.save_kind != k) A.save_kind = k, A.save_path.clear(), A.save_msg.clear();
}

static void apply_dialog(const std::string& target, const std::string& path) {
    if (path.empty()) return;
    if (target == "source") set_source(path);
    else if (target == "disc_key") set_disc_key_file(path);
    else if (target == "common_key") set_common_key_file(path);
    else if (target == "update") set_update_dir(path);
    else if (target == "save") set_save_path(path);
}

static void start_install(const std::string& source) {
    A.install_source = source;
    A.steps.clear();
    A.current = -1;
    std::string f = "\"source\":" + jstr(source);
    if (source != "installed") f += ",\"path\":" + jstr(A.source_path);
    request("install", f);
    SDL_memset(A.disc_paste, 0, sizeof A.disc_paste);
    SDL_memset(A.paste, 0, sizeof A.paste);
    go(Screen::Installing);
}

static void check_keys() {
    std::string f = "\"image\":" + jstr(A.source_path);
    if (A.disc_mode == 1 && A.disc_paste[0]) f += ",\"disc_key_hex\":" + jstr(A.disc_paste);
    else if (A.disc_mode == 0 && !A.disc_key_file.empty()) f += ",\"disc_key_file\":" + jstr(A.disc_key_file);
    if (A.common_mode == 1 && A.paste[0]) f += ",\"common_key_hex\":" + jstr(A.paste);
    else if (A.common_mode == 0 && !A.common_key_file.empty()) f += ",\"common_key_file\":" + jstr(A.common_key_file);
    f += ",\"update_dir\":" + jstr(chosen_update_dir());
    A.keys_msg = chosen_update_dir().empty() ? "Checking the keys..." : "Checking the keys and update...";
    request("check_keys", f);
    f.assign(f.size(), '\0');
}

static int step_index(const std::string& id) {
    for (size_t i = 0; i < A.steps.size(); i++)
        if (A.steps[i].id == id) return (int)i;
    return -1;
}

static void handle_reply(const J& ev) {
    std::string cmd = ev.str("cmd");
    bool ok = ev.boolean("ok");
    if ((int)ev.num("id") == A.pending_id) A.pending_id = 0, A.pending_cmd.clear();
    if (cmd == "probe") {
        A.probe_ok = ok;
        if (!ok) {
            A.probe_msg = ev.str("message");
            return;
        }
        A.source_kind = ev.str("kind");
        A.source_path = ev.str("path", A.source_path);
        A.disc_key_found = ev.str("disc_key");
        A.common_key_found = ev.str("common_key");
        A.update_dir_found = ev.str("update_dir");
        if (A.source_kind == "image") {
            set_disc_mode(A.disc_key_found.empty() && A.disc_key_file.empty() ? 1 : 0);
            set_common_mode(A.common_key_found.empty() && A.common_key_file.empty() ? 1 : 0);
        }
        apply_source_identity(ev);
        if (ev.get("message") && !ev.str("message").empty())
            A.probe_msg = ev.str("message");
        else if (A.source_kind == "folder")
            A.probe_msg = "Extracted game folder: New Super Mario Bros. U (USA)";
        else
            A.probe_msg = "Wii U disc image. Setup extracts it and applies an optional 1.3.0 update folder when you "
                          "provide one.";
    } else if (cmd == "check_keys") {
        if (ok) {
            A.keys_msg.clear();
            apply_source_identity(ev);
            start_install("image");
        } else {
            A.keys_msg = ev.str("message");
        }
    } else if (cmd == "install") {
        if (ok) {
            for (auto& s : A.steps)
                if (s.state != 3) s.state = 2;
            A.result_app = ev.str("app");
            A.save_exists = ev.boolean("save_exists");
            A.toolchain_bytes = ev.num("toolchain_bytes");
            A.game_dir = ev.str("game_dir", A.game_dir);
            g_exe = ev.str("exe"), g_game_dir = A.game_dir, g_data_dir = ev.str("data_dir", A.data_dir);
            A.installed = true;
            addlog("Setup finished.");
            go(A.save_exists ? Screen::Done : Screen::Save);
        } else {
            if (A.current >= 0 && A.current < (int)A.steps.size()) A.steps[A.current].state = 3;
            A.error_msg = ev.str("message");
            A.error_back = A.install_source == "installed" ? "menu" : "source";
            go(Screen::Error);
        }
    } else if (cmd == "remove_toolchain" || cmd == "shortcut") {
        if (!ok) addlog(ev.str("message"));
    } else if (cmd == "shaders") {
        A.shader_running = false;
        if (ev.boolean("cancelled")) {
            show_toast("Recompile stopped. Shaders compiled so far are kept.");
            go(A.shader_back);
            return;
        }
        if (!ok) {
            A.shader_error = ev.str("message");
            return;
        }
        A.shader_done = true;
        A.shader_done_n = ev.num("compiled");
        A.shader_detail = std::to_string((int)A.shader_done_n) + " shaders compiled";
    } else if (cmd == "import_save" || cmd == "import_existing") {
        if (ok) {
            A.save_msg = ev.str("message");
            A.save_exists = true;
            go(Screen::Done);
        } else if (ev.str("problem") == "exists") {
            A.confirm_replace = true;
        } else {
            A.save_msg = ev.str("message");
        }
    }
}

static void handle_event(const J& ev) {
    std::string e = ev.str("event");
    if (e == "hello") {
        A.hello = true;
        if (A.version.empty()) A.version = ev.str("version");
        else if (ev.str("version") != A.version)
            addlog("note: setup.py reports version " + ev.str("version") + ", the package says " + A.version);
        A.data_dir = ev.str("data_dir");
        A.app_dir = ev.str("app_dir");
        A.log_path = ev.str("log");
        A.platform = ev.str("platform");
        A.game_files = ev.boolean("game_files");
        A.save_exists = ev.boolean("save_exists");
        A.portable = ev.boolean("portable");
        A.legacy = ev.boolean("legacy");
        A.package = ev.str("package");
        A.game_dir = ev.str("game_dir");
        A.free_bytes = ev.num("free_bytes");
        const J* inst = ev.get("installed");
        A.installed = inst && inst->t == J::Obj;
        A.installed_version = A.installed ? inst->str("version") : "";
        if (A.installed) {
            A.result_app = inst->str("app");
            g_exe = inst->str("exe"), g_game_dir = inst->str("game_dir"), g_data_dir = A.data_dir;
        }
        go(A.installed ? Screen::Menu : Screen::Welcome);
    } else if (e == "log") {
        addlog(ev.str("text"));
    } else if (e == "plan") {
        A.steps.clear();
        if (const J* st = ev.get("steps"))
            for (auto& s : st->a) {
                Step x;
                x.id = s.str("id");
                x.title = s.str("title");
                A.steps.push_back(x);
            }
    } else if (e == "step") {
        int i = step_index(ev.str("id"));
        for (int k = 0; k < i; k++) A.steps[k].state = 2;
        if (i >= 0) A.steps[i].state = 1, A.current = i;
    } else if (e == "shader") {
        A.shader_done_n = ev.num("done");
        A.shader_total = ev.num("total");
        A.shader_detail = ev.str("detail");
        A.shader_phase = ev.str("phase");
    } else if (e == "progress") {
        if (A.current >= 0) {
            Step& s = A.steps[A.current];
            s.done = ev.num("done");
            s.total = ev.num("total");
            s.detail = ev.str("detail");
        }
    } else if (e == "reply") {
        handle_reply(ev);
    } else if (e == "fatal") {
        fail(ev.str("message"), A.log_path.empty() ? "Fix what the message says and open NSMBU again."
                                                   : "Fix what the message says and open NSMBU again. The setup log is " +
                                                         A.log_path + ".");
    }
}

static std::string format_size(double b) {
    char t[32];
    if (b >= 1e9) snprintf(t, sizeof t, "%.1f GB", b / 1e9);
    else snprintf(t, sizeof t, "%.0f MB", b / 1e6);
    return t;
}

static void run_queue() {
    if (busy()) return;
    if (!A.queue.empty()) {
        auto r = A.queue.front();
        A.queue.pop_front();
        request(r.first, r.second);
        return;
    }
    if (A.after.empty()) return;
    std::string a = A.after;
    A.after.clear();
    if (a == "play" && (!A.portable || g_exe.empty())) {
        request("launch", "");
        A.after = "quit";
    } else if (a == "play") A.exec_game = true, A.exit_code = 0;
    else if (a == "quit") A.exit_code = 0;
}

static void pump_child() {
    run_queue();
    A.child.poll();
    while (!A.child.lines.empty()) {
        std::string l = A.child.lines.front();
        A.child.lines.pop_front();
        J ev;
        if (!l.empty() && l[0] == '{' && parse_json(l, ev)) handle_event(ev);
        else if (!l.empty()) addlog(l);
    }
    if (A.child.exited && A.screen != Screen::Fatal && A.exit_code < 0 && A.child.proc) {
        std::string code = "exit code " + std::to_string(A.child.exit_code);
        if (A.hello)
            fail("The setup process (installer/setup.py) ended unexpectedly (" + code + ").",
                 "Open NSMBU again; finished steps are kept. The last lines it wrote are below" +
                     (A.log_path.empty() ? std::string(".") : ", its full log is " + A.log_path + ".") +
                     " If it happens again, click \"Copy log\" and attach the log to a bug report.");
        else
            fail("The setup process ended before it was ready (" + code + "; its output is below).",
                 "Its output usually says what is missing. You can also run the setup in a terminal: " SETUP_IN_TERMINAL
                 " in the release folder. If that fails too, click \"Copy log\" and attach the log to a bug report.");
    }
}

static void centered_button_row(float w) {
    float x = ImGui::GetCursorPosX() + (ImGui::GetContentRegionAvail().x - w) * 0.5f;
    if (x > ImGui::GetCursorPosX()) ImGui::SetCursorPosX(x);
}

static bool begin_confirm(const char* title) {
    ImVec2 center = ImGui::GetMainViewport()->GetCenter();
    ImGui::SetNextWindowPos(center, ImGuiCond_Appearing, ImVec2(0.5f, 0.5f));
    ImGui::SetNextWindowSizeConstraints(ImVec2(420, 0), ImVec2(FLT_MAX, FLT_MAX));
    ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, ImGui::GetStyle().PopupRounding);
    if (!ImGui::BeginPopupModal(title, nullptr, ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoTitleBar)) {
        ImGui::PopStyleVar();
        return false;
    }
    heading(title, 20);
    ImGui::Spacing();
    return true;
}

static void end_confirm() {
    ImGui::EndPopup();
    ImGui::PopStyleVar();
}

static float space_above_footer() {
    return ImGui::GetContentRegionAvail().y - footer_reserve() - ImGui::GetStyle().ItemSpacing.y * 2;
}

static float log_pane_height(float share = 0.4f) {
    float line = ImGui::GetTextLineHeightWithSpacing();
    float avail = std::max(0.0f, space_above_footer());
    return std::clamp(std::max(line * 10, avail * share), line * 10, std::max(line * 10, avail));
}

static void page_header(const char* title, const std::string& sub = "") {
    heading(title);
    if (!sub.empty()) muted(sub);
    ImGui::Spacing();
}

static void log_pane(float height) {
    ImGui::BeginChild("log", ImVec2(0, height), ImGuiChildFlags_Borders);
    ImGui::PushStyleColor(ImGuiCol_Text, MUTED);
    for (auto& l : A.log) ImGui::TextUnformatted(l.c_str());
    ImGui::PopStyleColor();
    if (A.log_scroll) ImGui::SetScrollHereY(1.0f), A.log_scroll = false;
    ImGui::EndChild();
}

static void screen_starting() {
    page_header("NSMBU");
    ImGui::Spacing();
    mark(1);
    ImGui::TextUnformatted("Preparing the installer...");
    if (!A.log.empty()) log_pane(260);
}

static bool clt_ok() { return run_quick({"/usr/bin/xcode-select", "-p"}) && run_quick({"/usr/bin/xcrun", "--find", "clang"}); }

static void start_child();

static void screen_clt() {
    page_header("Apple's Command Line Tools are needed");
    ImGui::TextWrapped("Setup compiles the game on your Mac with Apple's free Command Line Tools (compiler and "
                       "Python). They are not installed yet.");
    ImGui::Spacing();
    if (!A.clt_install_started) {
        ImGui::TextWrapped("Click the button: Apple's installer opens. Click \"Install\" there and accept the license; it "
                           "takes a few minutes. Setup continues by itself afterwards.");
    } else {
        mark(1);
        ImGui::TextUnformatted("Waiting for the Command Line Tools installation to finish...");
    }
    if (ImGui::GetTime() > A.clt_next_check) {
        A.clt_next_check = ImGui::GetTime() + 3.0;
        if (clt_ok()) start_child();
    }
    int b = footer({"Quit", A.clt_install_started ? "Open Apple's installer again" : "Install Command Line Tools"}, 1);
    if (b == 0) A.exit_code = 0;
    if (b == 1) {
        run_quick({"/usr/bin/xcode-select", "--install"});
        A.clt_install_started = true;
    }
}

static std::string home_folder() { return A.portable && !A.package.empty() ? A.package : A.data_dir; }

static void screen_welcome() {
    page_header("Welcome", "New Super Mario Bros. U, native PC port " + A.version);
    if (A.portable) {
        ImGui::TextWrapped("The first start prepares the game once: it reads your own dump of the game and builds it for "
                           "this computer (about two minutes). Releases never contain game code, so this happens here, "
                           "once. After that, starting NSMBU starts the game directly.");
    } else {
        ImGui::TextWrapped("This installer builds the game on your computer from your own dump of the game. The "
                           "download contains no game files, no game code and no keys.");
    }
    ImGui::Spacing();
    ImGui::TextUnformatted("You need:");
    ImGui::Bullet();
    ImGui::TextWrapped("your NSMBU (USA, Europe, or an NSMBU + NSLU pack): a disc image (.wud/.wux), a Cemu archive "
                       "at that title version (USA 64 or 0, Europe 80 or 0, or combo 0), or an extracted folder (code, content, "
                       "meta);");
    ImGui::Bullet();
    ImGui::TextWrapped("for a disc image: its disc key and the Wii U common key (from your console). The matching "
                       "1.3.0 update is optional (update/ next to the image, or chosen on the next screen). A Cemu "
                       "archive or an extracted folder needs no keys.");
    ImGui::Spacing();
    if (A.portable) {
        ImGui::TextUnformatted("Space, all of it in this folder:");
        ImGui::Bullet();
        ImGui::TextWrapped("an extracted game folder is used where it is: nothing is copied;");
        ImGui::Bullet();
        ImGui::TextWrapped("a disc image or Cemu archive is extracted into the folder: about 1.7 GB;");
        ImGui::Bullet();
        ImGui::TextWrapped("while preparing, about 1 GB more (removed afterwards)%s.",
#if defined(_WIN32) || (!defined(__APPLE__))
                           ", plus the compiler download, which you can remove at the end"
#else
                           ""
#endif
        );
        ImGui::Spacing();
        muted("Everything stays in " + home_folder() + " (free: " + format_size(A.free_bytes) +
              "). Nothing is written to your user folders unless you ask for a shortcut.");
    } else {
        muted("The game will be installed in " + A.data_dir + ". Your saves and settings there are never changed "
              "without asking.");
    }
    int b = footer({"Quit", "Continue"}, 1);
    if (b == 0) A.exit_code = 0;
    if (b == 1) go(Screen::Source);
}

static void screen_menu() {
    bool update = A.installed_version != A.version;
    page_header(A.portable ? "NSMBU" : "NSMBU is installed",
                update ? "Prepared with " + A.installed_version + ". This release: " + A.version + "."
                       : "Version " + A.version + ", in " + home_folder());
    if (update) {
        float full_w = ImGui::GetContentRegionAvail().x;
        float primary_h = ImGui::GetFrameHeight() + 12;
        if (button("Update to this release", ImVec2(full_w, primary_h), true)) start_install("installed");
        button_caption("Rebuilds the game with this release and precompiles shaders. Your game files and saves are kept; no keys needed.");
    }
    if (button("Repair", ImVec2(button_w("Repair"), 0))) start_install("installed");
    button_caption("Rebuilds the game code from the installed game files, then precompiles shaders.");
    const char* recompile = "Recompile shaders";
    if (update) {
        ImGui::BeginDisabled();
        button(recompile, ImVec2(button_w(recompile), 0));
        ImGui::EndDisabled();
        button_caption("Update to this release first, then recompile shaders.");
    } else if (button(recompile, ImVec2(button_w(recompile), 0))) {
        start_shaders(Screen::Menu);
    } else {
        button_caption("Runs the game in the background again to refresh shader caches.");
    }
    if (button("Change the game...", ImVec2(button_w("Change the game..."), 0))) go(Screen::Source);
    if (button("Import saves or settings...", ImVec2(button_w("Import saves or settings..."), 0))) go(Screen::Save);
    if (button("Open the folder", ImVec2(button_w("Open the folder"), 0))) open_folder(home_folder());
    float h = ImGui::GetFrameHeight() + 8;
    float pad = ImGui::GetStyle().WindowPadding.y;
    float pad_x = ImGui::GetStyle().WindowPadding.x;
    float y = ImGui::GetWindowHeight() - pad - h;
    if (ImGui::GetCursorPosY() < y) ImGui::SetCursorPosY(y);
    else ImGui::Dummy(ImVec2(0, ImGui::GetStyle().ItemSpacing.y));
    ImGui::SetCursorPosX(pad_x);
    if (button("Play", ImVec2((button_w("Play") + 56) * 2, h), !update)) A.after = "play";
    float quit_w = button_w("Quit");
    ImGui::SameLine();
    ImGui::SetCursorPosX(ImGui::GetWindowWidth() - pad_x - quit_w);
    if (button("Quit", ImVec2(quit_w, h))) A.exit_code = 0;
}

static void screen_source() {
    page_header("Choose your game",
                "A Wii U disc image (.wux or .wud), optionally with the 1.3.0 update, a Cemu archive (.wua) at USA "
                "title version 64 or 0, Europe 80 or 0, or combo version 0, or an already extracted game folder.");
    static const SDL_DialogFileFilter filters[] = {{"Wii U disc image or Cemu archive (.wux, .wud, .wua)", "wux;wud;wua"},
                                                   {"Wii U disc image (.wux, .wud)", "wux;wud"},
                                                   {"Cemu Wii U archive (.wua)", "wua"},
                                                   {"All files", "*"}};
    float half = (ImGui::GetContentRegionAvail().x - ImGui::GetStyle().ItemSpacing.x) * 0.5f;
    if (button("Choose disc image or archive...", ImVec2(half, 0), A.source_path.empty()))
        choose_file("source", filters, 4);
    ImGui::SameLine();
    if (button("Choose extracted game folder...", ImVec2(half, 0))) choose_folder("source");
    ImGui::Spacing();
    if (!A.source_path.empty()) {
        ImGui::BeginChild("src", ImVec2(0, 0), ImGuiChildFlags_Borders | ImGuiChildFlags_AutoResizeY);
        if (A.pending_cmd == "probe") {
            mark(1);
            ImGui::TextUnformatted("Checking...");
        } else if (!A.probe_ok) {
            mark(3);
            ImGui::TextWrapped("%s", A.probe_msg.c_str());
        } else if (!A.source_title.empty() || A.source_kind == "image" || A.source_kind == "archive" ||
                   A.source_kind == "folder") {
            mark(2);
            const char* kind = A.source_kind == "archive"   ? "Cemu archive"
                               : A.source_kind == "folder" ? "Extracted game folder"
                                                           : "Wii U disc image";
            if (!A.source_title.empty()) ImGui::TextWrapped("%s", A.source_title.c_str());
            else ImGui::TextUnformatted(kind);
            std::string meta;
            if (!A.source_title.empty()) meta = kind;
            if (!A.source_title_id_text.empty())
                meta += (meta.empty() ? "" : " · ") + ("Title " + A.source_title_id_text);
            if (!A.source_build.empty()) meta += (meta.empty() ? "" : " · ") + ("build " + A.source_build);
            if (A.source_files > 0 || A.source_bytes > 0) {
                std::string stats;
                if (A.source_files > 0) stats = std::to_string(A.source_files) + " files";
                if (A.source_bytes > 0) stats += (stats.empty() ? "" : ", ") + format_size(A.source_bytes);
                meta += (meta.empty() ? "" : " · ") + stats;
            }
            if (!meta.empty()) muted(meta);
            if (!A.source_update_title.empty() || !A.source_update_sha256.empty() || A.source_update_version > 0) {
                std::string upd;
                if (!A.source_update_title.empty()) upd = A.source_update_title;
                else if (A.source_update_version > 0)
                    upd = "title version " + std::to_string(A.source_update_version);
                if (!A.source_update_sha256.empty()) {
                    std::string sha = A.source_update_sha256.size() > 16 ? A.source_update_sha256.substr(0, 16) + "…"
                                                                        : A.source_update_sha256;
                    upd += (upd.empty() ? "" : " · ") + ("RPX " + sha);
                }
                muted("Update: " + upd);
            }
            if (A.source_kind == "image")
                muted("Setup extracts it. The 1.3.0 update folder on the next screen is optional (needed for mods).");
            else if (A.source_kind == "archive")
                muted("No keys needed. Game files are extracted from the archive.");
        } else {
            mark(2);
            ImGui::TextWrapped("%s", A.probe_msg.c_str());
        }
        muted(A.source_path);
        ImGui::EndChild();
    }
    int b = footer({"Back", "Next"}, 1, (A.probe_ok && !busy()) ? 0 : 2);
    if (b == 0) go(A.installed ? Screen::Menu : Screen::Welcome);
    if (b == 1) {
        if (A.source_kind == "folder" || A.source_kind == "archive") start_install(A.source_kind);
        else go(Screen::Keys);
    }
}

static std::string base_name(const std::string& p) {
    size_t s = p.find_last_of("/\\");
    return s == std::string::npos ? p : p.substr(s + 1);
}

static void key_panel(const char* id, const char* title, int* mode, char* paste, size_t paste_n, const char* status,
                      int status_mark, const char* choose_id, const char* file_target,
                      const SDL_DialogFileFilter* keyf, int keyf_n, float width) {
    char file_radio[64], paste_radio[64], ctrl_id[64], paste_id[64], choose_btn[64];
    std::snprintf(file_radio, sizeof file_radio, "Use a key file##%s", id);
    std::snprintf(paste_radio, sizeof paste_radio, "Type or paste the key##%s", id);
    std::snprintf(ctrl_id, sizeof ctrl_id, "ctrl##%s", id);
    std::snprintf(paste_id, sizeof paste_id, "##paste_%s", id);
    std::snprintf(choose_btn, sizeof choose_btn, "Choose key file...##%s", choose_id);

    ImGui::BeginChild(id, ImVec2(width, 0), ImGuiChildFlags_Borders | ImGuiChildFlags_AutoResizeY);
    heading(title, 20);
    if (status && status[0]) {
        mark(status_mark);
        ImGui::TextUnformatted(status);
    } else {
        ImGui::Dummy(ImVec2(0, ImGui::GetTextLineHeight()));
    }
    int prev = *mode;
    radio(paste_radio, mode, 1);
    ImGui::SameLine(0, 30);
    radio(file_radio, mode, 0);
    if (*mode != prev) keys_changed();
    float row_h = ImGui::GetFrameHeight();
    ImGui::BeginChild(ctrl_id, ImVec2(-1, row_h), ImGuiChildFlags_None, ImGuiWindowFlags_NoScrollbar);
    if (*mode == 0) {
        if (button(choose_btn, ImVec2(-1, row_h))) choose_file(file_target, keyf, keyf_n);
    } else {
        ImGui::SetNextItemWidth(-1);
        if (ImGui::InputTextWithHint(paste_id, "Type or paste 32 hex digits (hidden)", paste, paste_n,
                                     ImGuiInputTextFlags_Password | ImGuiInputTextFlags_CharsNoBlank))
            keys_changed();
    }
    ImGui::EndChild();
    ImGui::EndChild();
}

static void screen_keys() {
    static const SDL_DialogFileFilter keyf[] = {{"Key files (.key, .bin, .txt)", "key;bin;txt"}, {"All files", "*"}};
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(20, 14));

    ImGui::BeginChild("image", ImVec2(0, 0), ImGuiChildFlags_Borders | ImGuiChildFlags_AutoResizeY);
    heading("Disc image", 20);
    muted(A.source_path);
    if (!A.source_title.empty()) {
        mark(2);
        ImGui::TextWrapped("%s", A.source_title.c_str());
        std::string meta;
        if (!A.source_title_id_text.empty()) meta = "Title " + A.source_title_id_text;
        if (!A.source_build.empty()) meta += (meta.empty() ? "" : " · ") + std::string("build ") + A.source_build;
        if (A.source_files > 0 || A.source_bytes > 0) {
            std::string stats;
            if (A.source_files > 0) stats = std::to_string(A.source_files) + " files";
            if (A.source_bytes > 0) stats += (stats.empty() ? "" : ", ") + format_size(A.source_bytes);
            meta += (meta.empty() ? "" : " · ") + stats;
        }
        if (!meta.empty()) muted(meta);
    } else {
        mark(1);
        ImGui::TextWrapped("Title and size are shown once the disc key and common key decrypt this image.");
    }
    if (!A.source_update_title.empty() || !A.source_update_sha256.empty() || A.source_update_version > 0) {
        std::string line;
        if (!A.source_update_title.empty()) line = A.source_update_title;
        else if (A.source_update_version > 0) line = "title version " + std::to_string(A.source_update_version);
        if (!A.source_update_sha256.empty()) {
            std::string sha = A.source_update_sha256.size() > 16 ? A.source_update_sha256.substr(0, 16) + "…"
                                                                : A.source_update_sha256;
            line += (line.empty() ? "" : " · ") + ("RPX " + sha);
        }
        muted("Update: " + line);
    }
    ImGui::EndChild();
    ImGui::Spacing();

    std::string disc_status, common_status;
    int disc_mark = 0, common_mark = 0;
    if (!A.disc_key_file.empty()) {
        disc_status = "Key file: " + base_name(A.disc_key_file);
        disc_mark = 2;
    } else if (!A.disc_key_found.empty()) {
        disc_status = "Found: " + base_name(A.disc_key_found);
        disc_mark = 2;
    } else {
        disc_status = "Not found next to the image.";
        disc_mark = 3;
    }
    if (!A.common_key_file.empty()) {
        common_status = "Key file: " + base_name(A.common_key_file);
        common_mark = 2;
    } else if (!A.common_key_found.empty()) {
        common_status = "Found: " + base_name(A.common_key_found);
        common_mark = 2;
    }

    float key_half = (ImGui::GetContentRegionAvail().x - ImGui::GetStyle().ItemSpacing.x) * 0.5f;
    key_panel("disc", "Disc key", &A.disc_mode, A.disc_paste, sizeof A.disc_paste, disc_status.c_str(), disc_mark,
              "disc", "disc_key", keyf, 2, key_half);
    ImGui::SameLine();
    key_panel("common", "Wii U common key", &A.common_mode, A.paste, sizeof A.paste, common_status.c_str(),
              common_mark, "common", "common_key", keyf, 2, key_half);
    ImGui::Spacing();

    ImGui::BeginChild("update", ImVec2(0, 0), ImGuiChildFlags_Borders | ImGuiChildFlags_AutoResizeY);
    heading("1.3.0 update (optional)", 20);
    muted("Folder with code, content and meta (USA title version 64, or Europe 80). The USA or Europe base disc "
          "alone also works.");
    std::string upd = chosen_update_dir();
    if (!upd.empty()) {
        mark(2);
        if (!A.update_dir.empty()) ImGui::TextWrapped("Update folder: %s", A.update_dir.c_str());
        else ImGui::TextWrapped("Found next to the image: %s", base_name(A.update_dir_found).c_str());
    } else if (A.update_skipped) {
        muted("Skipped. Install continues with the base disc if its RPX is known.");
    } else {
        muted("Not selected. Put an update/ folder next to the disc image, choose one here, or continue without it.");
    }
    float upd_half = (ImGui::GetContentRegionAvail().x - ImGui::GetStyle().ItemSpacing.x) * 0.5f;
    if (button(upd.empty() ? "Choose update folder...##upd" : "Choose a different update folder...##upd",
               ImVec2(upd.empty() ? -1.0f : upd_half, 0)))
        choose_folder("update");
    if (!upd.empty()) {
        ImGui::SameLine();
        if (button("Don't use an update##updskip", ImVec2(upd_half, 0))) skip_update_dir();
    }
    ImGui::EndChild();
    ImGui::PopStyleVar();

    if (!A.keys_msg.empty()) {
        ImGui::Spacing();
        if (A.pending_cmd == "check_keys") {
            mark(1);
            ImGui::TextUnformatted(A.keys_msg.c_str());
        } else {
            colored(BAD, A.keys_msg);
        }
    }
    bool have_disc = A.disc_mode == 1 ? strlen(A.disc_paste) > 0
                                      : (!A.disc_key_file.empty() || !A.disc_key_found.empty());
    bool have_common = A.common_mode == 1 ? strlen(A.paste) > 0 : (!A.common_key_file.empty() || !A.common_key_found.empty());
    int b = footer({"Back", "Check keys and install"}, 1, (have_disc && have_common && !busy()) ? 0 : 2);
    if (b == 0) go(Screen::Source);
    if (b == 1) check_keys();
}

static double step_weight(const std::string& id) {
    static const std::map<std::string, double> w = {{"keys", 1},      {"archive", 1},   {"folder", 1},
                                                    {"compiler", 3},  {"extract", 15},  {"copy", 8},
                                                    {"translate", 8}, {"compile", 60},  {"app", 4},
                                                    {"shaders", 25}};
    auto it = w.find(id);
    return it == w.end() ? 1 : it->second;
}

static void screen_installing() {
    if (A.portable) page_header("Preparing the game", "One time, about two minutes. You can keep using your computer.");
    else page_header("Installing", "This takes a few minutes. You can keep using your computer.");
    double tw = 0, dw = 0;
    const Step* current = nullptr;
    for (auto& s : A.steps) {
        double w = step_weight(s.id);
        tw += w;
        if (s.state == 2) dw += w;
        else if (s.state == 1) {
            if (s.total > 0) dw += w * std::min(1.0, s.done / s.total);
            if (!current) current = &s;
        }
    }
    heading("Overall", 20);
    float bar_h = ImGui::GetFontSize() + ImGui::GetStyle().FramePadding.y;
    ImGui::ProgressBar(tw > 0 ? (float)(dw / tw) : 0.0f, ImVec2(-1, bar_h));
    if (current) {
        std::string t = current->title;
        if (size_t paren = t.find(" ("); paren != std::string::npos) t = t.substr(0, paren);
        if (!current->detail.empty()) t += " — " + current->detail;
        muted(t);
    }
    ImGui::Spacing();
    float gap = ImGui::GetStyle().ItemSpacing.y;
    float avail = std::max(0.0f, ImGui::GetContentRegionAvail().y - gap);
    float steps_h = avail * (2.0f / 3.0f);
    ImGui::BeginChild("steps", ImVec2(0, steps_h), ImGuiChildFlags_Borders);
    float icon_pad = ImGui::GetFontSize() * 1.1f + ImGui::GetStyle().ItemSpacing.x;
    for (auto& s : A.steps) {
        mark(s.state);
        std::string t = s.title;
        if (size_t paren = t.find(" ("); paren != std::string::npos) t = t.substr(0, paren);
        if (s.state == 0) ImGui::PushStyleColor(ImGuiCol_Text, MUTED);
        ImGui::TextWrapped("%s", t.c_str());
        if (s.state == 0) ImGui::PopStyleColor();
        if (s.state == 1) {
            ImGui::Indent(icon_pad);
            float frac = s.total > 0 ? (float)std::min(1.0, s.done / s.total) : -1.0f * (float)ImGui::GetTime();
            ImGui::ProgressBar(frac, ImVec2(-1, 0), s.total > 0 ? nullptr : "");
            if (!s.detail.empty()) muted(s.detail);
            ImGui::Unindent(icon_pad);
            ImGui::Spacing();
        }
    }
    ImGui::EndChild();
    ImGui::Spacing();
    log_pane(std::max(0.0f, ImGui::GetContentRegionAvail().y));
}

static void screen_error() {
    ImGui::PushStyleColor(ImGuiCol_Text, BAD);
    heading("Setup stopped");
    ImGui::PopStyleColor();
    ImGui::Spacing();

    std::string head = A.error_msg, rest;
    if (size_t nl = head.find('\n'); nl != std::string::npos) rest = head.substr(nl + 1), head = head.substr(0, nl);
    if (!head.empty()) head[0] = (char)toupper((unsigned char)head[0]);
    ImGui::TextWrapped("%s", head.c_str());
    if (!rest.empty()) {
        float h = std::min(ImGui::GetTextLineHeightWithSpacing() * 10, std::max(80.0f, space_above_footer() * 0.45f));
        ImGui::BeginChild("errdetail", ImVec2(0, h), ImGuiChildFlags_Borders);
        ImGui::PushStyleColor(ImGuiCol_Text, MUTED);
        ImGui::TextWrapped("%s", rest.c_str());
        ImGui::PopStyleColor();
        ImGui::EndChild();
    }
    ImGui::Spacing();
    muted("Nothing that was finished is lost. Copy log, then Report issue. The log contains no keys.");
    log_pane(std::max(0.0f, space_above_footer()));
    int b = footer({"Quit", "Copy log", "Report issue...", "Retry"}, 3);
    if (b == 0) A.exit_code = 1;
    if (b == 1) copy_log();
    if (b == 2) report_issue();
    if (b == 3) {
        A.keys_msg.clear();
        if (A.error_back == "menu") go(Screen::Menu);
        else {
            if (!A.source_path.empty()) set_source(A.source_path);
            go(Screen::Source);
        }
    }
}

static void send_import(bool replace) {
    std::string r = replace ? ",\"replace\":true" : "";
    if (A.save_kind == 2) request("import_existing", r.empty() ? "" : r.substr(1));
    else if (A.save_kind == 3) request("import_existing", "\"path\":" + jstr(A.save_path) + r);
    else request("import_save", std::string("\"kind\":\"hd\",\"path\":") + jstr(A.save_path) + r);
}

static void screen_save() {
    page_header("Your save (optional)", "Use a save you already have, or start a new game.");
    if (A.save_exists) colored(MUTED, "A save is already installed. Importing replaces it; the current save is backed up first.");
    int kind = A.save_kind;
    if (kind == 4) kind = 3;
    else if (kind == 2 && !A.legacy) kind = 1;
    radio("Start with a new save", &kind, 0);
    radio("NSMBU save (a folder with cking.sav, from Cemu or a Wii U)", &kind, 1);
    if (A.save_kind == 1) {
        ImGui::Indent();
        if (button("Choose folder...##hd")) choose_folder("save");
        if (!A.save_path.empty()) ImGui::SameLine(), ImGui::TextUnformatted(base_name(A.save_path).c_str());
        ImGui::Unindent();
    }
    if (A.legacy) radio("Copy saves and settings from my earlier installation (copied, not moved)", &kind, 2);
    radio("Copy saves and settings from another NSMBU folder", &kind, 3);
    set_save_kind(kind);
    if (A.save_kind == 3) {
        ImGui::Indent();
        if (button("Choose folder...##other")) choose_folder("save");
        if (!A.save_path.empty()) ImGui::SameLine(), ImGui::TextUnformatted(base_name(A.save_path).c_str());
        muted("The folder of an earlier NSMBU release (it has a data folder).");
        ImGui::Unindent();
    }
    if (!A.save_msg.empty()) colored(BAD, A.save_msg);
    if (A.confirm_replace) {
        ImGui::OpenPopup("Replace save?");
        A.confirm_replace = false;
    }
    if (begin_confirm("Replace save?")) {
        ImGui::TextUnformatted("A save is already installed.");
        ImGui::TextUnformatted("Replace it? The current save is moved to a backup folder first.");
        ImGui::Spacing();
        float gap = ImGui::GetStyle().ItemSpacing.x;
        centered_button_row(160 + gap + 160);
        if (button("Replace", ImVec2(160, 0), true)) {
            send_import(true);
            ImGui::CloseCurrentPopup();
        }
        ImGui::SameLine();
        if (button("Keep my save", ImVec2(160, 0))) ImGui::CloseCurrentPopup();
        end_confirm();
    }
    bool can = A.save_kind == 0 || A.save_kind == 2 || !A.save_path.empty();
    int b = footer({A.save_kind == 0 ? "Continue" : "Import"}, 0, (can && !busy()) ? 0 : 1);
    if (b == 0) {
        if (A.save_kind == 0) go(Screen::Done);
        else send_import(false);
    }
}

static void screen_done() {
    page_header("Ready to play");
    mark(2);
    ImGui::TextWrapped(A.portable ? "The game is prepared." : "New Super Mario Bros. U is installed.");
    ImGui::Spacing();
    if (A.portable) {
        muted("Everything is in " + home_folder() + ".");
        muted("To play, start NSMBU again: it starts the game directly. To repair, update or change the "
#ifdef __linux__
              "game, start it with --setup (or use the shortcut's Setup action)."
#else
              "game, hold Shift while starting it."
#endif
        );
    } else {
#ifdef __APPLE__
        if (!A.result_app.empty()) muted("The game: " + A.result_app + " (also in Launchpad and Spotlight).");
#elif defined(_WIN32)
        muted("The game: Start menu and desktop shortcut \"NSMBU\".");
#else
        muted("The game: your applications menu, or " + A.data_dir + "/play.sh.");
#endif
        muted("Game files and saves: " + A.data_dir);
    }
    if (!A.save_msg.empty()) muted(A.save_msg);
    if (A.portable) {
        ImGui::Spacing();
        if (A.toolchain_bytes > 0) {
            checkbox(("Remove the downloaded compiler (" + format_size(A.toolchain_bytes) + ")").c_str(),
                            &A.opt_remove_toolchain);
            muted("Keep it for repairs. Run setup again to restore it if removed.");
        }
        checkbox(
#if defined(__APPLE__)
            "Add NSMBU to my Applications folder (a link)",
#elif defined(_WIN32)
            "Add NSMBU to the Start menu",
#else
            "Add NSMBU to my applications menu",
#endif
            &A.opt_shortcut);
        muted("Off by default: then nothing is written outside this folder.");
    }
    ImGui::Spacing();
    if (button("Recompile shaders", ImVec2(button_w("Recompile shaders"), 0)) && !busy()) start_shaders(Screen::Done);
    muted("Runs the game in the background again to refresh shader caches.");
    int b = footer({"Quit", "Open folder", "Play"}, 2, busy() || !A.after.empty() ? 7 : 0);
    auto queue_options = [] {
        if (A.portable && A.toolchain_bytes > 0 && A.opt_remove_toolchain) A.queue.push_back({"remove_toolchain", ""});
        if (A.portable && A.opt_shortcut) A.queue.push_back({"shortcut", ""});
        A.toolchain_bytes = 0, A.opt_shortcut = false;
    };
    if (b == 0) queue_options(), A.after = "quit";
    if (b == 1) open_folder(home_folder());
    if (b == 2) queue_options(), A.after = "play";
}

static std::string format_elapsed(double seconds) {
    int t = seconds < 0 ? 0 : (int)seconds;
    int m = t / 60, s = t % 60;
    char b[40];
    if (m >= 60) snprintf(b, sizeof b, "%d h %d min", m / 60, m % 60);
    else if (m) snprintf(b, sizeof b, "%d min %d s", m, s);
    else snprintf(b, sizeof b, "%d s", s);
    return b;
}

static void start_shaders(Screen back) {
    A.shader_back = back;
    A.shader_running = true;
    A.shader_done = false;
    A.shader_done_n = 0;
    A.shader_total = 0;
    A.shader_started = ImGui::GetTime();
    A.shader_phase = "Starting the game";
    A.shader_detail = "Waiting for the game to draw.";
    A.shader_error.clear();
    request("shaders", "");
    go(Screen::Shaders);
}

static void cancel_shaders() {
    if (!A.child.send("{\"cmd\":\"shaders_cancel\",\"id\":0}"))
        A.shader_error = "Could not ask the game to stop.\nClose this window if it stays on this screen.";
}

static void screen_shaders() {
    if (!A.shader_error.empty()) {
        page_header("Could not recompile shaders");
        std::string head = A.shader_error, rest;
        if (size_t nl = head.find('\n'); nl != std::string::npos) rest = head.substr(nl + 1), head = head.substr(0, nl);
        if (!head.empty()) head[0] = (char)toupper((unsigned char)head[0]);
        ImGui::PushStyleColor(ImGuiCol_Text, BAD);
        ImGui::TextWrapped("%s", head.c_str());
        ImGui::PopStyleColor();
        if (!rest.empty()) {
            ImGui::Spacing();
            muted(rest);
        }
        ImGui::Spacing();
        muted("Nothing else in the installation was changed.");
        log_pane(std::max(0.0f, space_above_footer()));
        int b = footer({"Back", "Copy log", "Report issue...", "Retry"}, 3, busy() ? 15 : 0);
        if (b == 0) go(A.shader_back);
        if (b == 1) copy_log();
        if (b == 2) report_issue();
        if (b == 3) start_shaders(A.shader_back);
        return;
    }
    if (A.shader_done && !A.shader_running) {
        page_header("Shaders are ready");
        mark(2);
        ImGui::TextWrapped("%d shaders were compiled.", (int)A.shader_done_n);
        ImGui::Spacing();
        muted("The next time you play, the game spends less time compiling those shaders. "
              "Scenes you have not seen yet can still compile the first time you reach them.");
        float bar_h = ImGui::GetFrameHeight() + 6;
        ImGui::Spacing();
        ImGui::ProgressBar(1.0f, ImVec2(-1, bar_h), "Done");
        int b = footer({"Back", "Play"}, 1);
        if (b == 0) go(A.shader_back);
        if (b == 1) A.after = "play";
        return;
    }
    page_header("Recompiling shaders",
                "The game is running in the background. You can keep using your computer. This can take several minutes.");
    heading(A.shader_phase.empty() ? "Starting the game" : A.shader_phase.c_str());
    float bar_h = ImGui::GetFrameHeight() + 6;
    float frac = A.shader_total > 0 ? (float)std::min(1.0, A.shader_done_n / A.shader_total) : -1.0f * (float)ImGui::GetTime();
    char overlay[64];
    overlay[0] = 0;
    if (A.shader_total > 0) snprintf(overlay, sizeof overlay, "%d / %d", (int)A.shader_done_n, (int)A.shader_total);
    ImGui::ProgressBar(frac, ImVec2(-1, bar_h), A.shader_total > 0 ? overlay : "");
    if (!A.shader_detail.empty()) muted(A.shader_detail);
    muted("Elapsed: " + format_elapsed(ImGui::GetTime() - A.shader_started));
    ImGui::Spacing();
    muted("Stop leaves the shaders already compiled in place. You can play without finishing this.");
    if (footer({"Cancel"}, -1, A.shader_running ? 0 : 1) == 0) ImGui::OpenPopup("Stop recompiling?");
    if (begin_confirm("Stop recompiling?")) {
        ImGui::TextUnformatted("Stop compiling shaders now?");
        muted("Shaders already compiled are kept. You can run this again later.");
        ImGui::Spacing();
        float gap = ImGui::GetStyle().ItemSpacing.x;
        centered_button_row(180 + gap + 180);
        if (button("Stop", ImVec2(180, 0), true)) {
            cancel_shaders();
            ImGui::CloseCurrentPopup();
        }
        ImGui::SameLine();
        if (button("Continue", ImVec2(180, 0))) ImGui::CloseCurrentPopup();
        end_confirm();
    }
}

static void screen_fatal() {
    ImGui::PushStyleColor(ImGuiCol_Text, BAD);
    heading("Setup could not continue");
    ImGui::PopStyleColor();
    ImGui::TextWrapped("%s", A.fatal.c_str());
    if (!A.fatal_hint.empty()) {
        ImGui::Spacing();
        ImGui::TextUnformatted("What to do:");
        ImGui::TextWrapped("%s", A.fatal_hint.c_str());
    }
    if (!A.fatal_log.empty()) {
        ImGui::Spacing();
        muted("This was written to " + A.fatal_log);
    }
    if (!A.log.empty()) log_pane(log_pane_height(0.5f));
#ifdef __APPLE__
    const char* show = "Show in Finder";
#else
    const char* show = "Open the folder";
#endif
    int b = A.fatal_folder.empty() ? footer({"Quit", "Copy log", "Report issue..."})
                                   : footer({"Quit", "Copy log", "Report issue...", show});
    if (b == 0) A.exit_code = 1;
    if (b == 1) copy_log();
    if (b == 2) report_issue();
    if (b == 3) open_folder(A.fatal_folder);
}

struct PackageSearch {
    std::string pkg;
    std::string start;
    std::string checked;
    std::string error;
    int err = 0;
    bool translocated = false;
    bool original_known = true;
};

#ifdef __APPLE__

static bool untranslocate_bundle(const std::string& bundle, std::string& original, bool& known) {
    original = bundle;
    known = true;
    SDL_SharedObject* sec = SDL_LoadObject("/System/Library/Frameworks/Security.framework/Security");
    SDL_SharedObject* cf = SDL_LoadObject("/System/Library/Frameworks/CoreFoundation.framework/CoreFoundation");
    using CreateURL = const void* (*)(const void*, const unsigned char*, long, unsigned char);
    using IsTranslocated = unsigned char (*)(const void*, bool*, void**);
    using Original = const void* (*)(const void*, void**);
    using GetFS = unsigned char (*)(const void*, unsigned char, unsigned char*, long);
    using Release = void (*)(const void*);
    auto create = cf ? (CreateURL)SDL_LoadFunction(cf, "CFURLCreateFromFileSystemRepresentation") : nullptr;
    auto getfs = cf ? (GetFS)SDL_LoadFunction(cf, "CFURLGetFileSystemRepresentation") : nullptr;
    auto release = cf ? (Release)SDL_LoadFunction(cf, "CFRelease") : nullptr;
    auto is_tl = sec ? (IsTranslocated)SDL_LoadFunction(sec, "SecTranslocateIsTranslocatedURL") : nullptr;
    auto orig_fn = sec ? (Original)SDL_LoadFunction(sec, "SecTranslocateCreateOriginalPathForURL") : nullptr;

    bool translocated = bundle.find("/AppTranslocation/") != std::string::npos;
    if (create && getfs && release) {
        const void* url = create(nullptr, (const unsigned char*)bundle.c_str(), (long)bundle.size(), 1);
        if (url) {
            bool t = false;
            if (is_tl && is_tl(url, &t, nullptr)) translocated = t;
            if (translocated) {
                known = false;
                const void* o = orig_fn ? orig_fn(url, nullptr) : nullptr;
                unsigned char buf[4096];
                if (o && getfs(o, 1, buf, sizeof buf)) original = (const char*)buf, known = true;
                if (o) release(o);
            }
            release(url);
        }
    } else if (translocated) {
        known = false;
    }
    if (translocated) SDL_Log("App Translocation: started from %s, original %s", bundle.c_str(),
                              known ? original.c_str() : "unknown");
    return translocated;
}
#endif

static std::string with_trailing_sep(std::string p) {
    if (p.empty()) return p;
    char last = p.back();
    if (last != '/' && last != '\\') p.push_back('/');
    return p;
}

static PackageSearch find_package() {
    PackageSearch r;
    if (const char* e = SDL_getenv("NSMBU_SETUP_PKG")) {
        r.pkg = with_trailing_sep(e);
        r.start = r.pkg;
        r.checked = r.pkg + "installer/setup.py";
        return r;
    }
    std::string base = SDL_GetBasePath() ? SDL_GetBasePath() : "./";
    r.start = base;
#ifdef __APPLE__

    size_t app = base.rfind(".app/Contents/");
    if (app != std::string::npos) {
        std::string bundle = base.substr(0, app + 4), original;
        r.translocated = untranslocate_bundle(bundle, original, r.original_known);
        base = original + base.substr(bundle.size());
        r.start = original.substr(0, original.find_last_of('/') + 1);
    }
#endif
    r.checked = r.start + "installer/setup.py";
    for (int up = 0; up < 5; up++) {
        std::string candidate = base + "installer/setup.py";
        SDL_PathInfo info;
        if (SDL_GetPathInfo(candidate.c_str(), &info)) return r.pkg = base, r;
        if (base == r.start) {
#ifndef _WIN32
            struct stat sb;
            r.err = stat(candidate.c_str(), &sb) == 0 ? 0 : errno;
            r.error = r.err ? strerror(r.err) : "unreadable";
#else
            r.error = SDL_GetError();
#endif
            if (r.err == EPERM || r.err == EACCES) break;
        }

        if (base.size() > 1) base.pop_back();
        size_t s = base.find_last_of("/\\");
        if (s == std::string::npos) break;
        base = base.substr(0, s + 1);
    }
    return r;
}

static void fail_no_package(const PackageSearch& r) {
    std::string folder = r.start;
    if (folder.size() > 1 && (folder.back() == '/' || folder.back() == '\\')) folder.pop_back();
#ifdef __APPLE__
    if (r.translocated && !r.original_known) {
        fail("macOS started NSMBU from a temporary read-only copy (App Translocation, because the downloaded "
             "folder is still marked as quarantined), and did not say where the original is, so the release folder "
             "around the app cannot be found.",
             "In Finder, drag \"NSMBU.app\" out of the unzipped folder (for example onto the Desktop) and back "
             "into the same folder, then open it again: an app moved with Finder is started where it is. Or, in "
             "Terminal: xattr -dr com.apple.quarantine followed by the path of the unzipped folder. Keep the app in "
             "that folder: it needs tools/ and sdk/ next to it.");
        return;
    }
    if (r.err == EPERM || r.err == EACCES) {
        fail("macOS did not let NSMBU read its release folder " + folder + " (checking " + r.checked + ": " +
                 r.error + ").",
             "If you answered \"Don't Allow\" when macOS asked about access to a folder (Downloads, Desktop, "
             "Documents, an external drive), allow NSMBU in System Settings > Privacy & Security > Files and "
             "Folders, or move the whole unzipped folder to another place (for example your home folder or a Games "
             "folder) and open the app from there.",
             folder);
        return;
    }
    fail("NSMBU could not find its release files: " + r.checked + " is missing (" + r.error + ").",
         "Keep \"NSMBU.app\" inside the unzipped release folder, next to tools/ and sdk/. If you "
         "moved only the app (for example into Applications), move it back; to keep the game somewhere else, move "
         "the whole folder.",
         folder);
#else
    fail("NSMBU could not find its release files: " + r.checked + " is missing or unreadable (" + r.error + ").",
         "Keep this program inside the unzipped release folder, next to tools/ and sdk/. To keep the "
         "game somewhere else, move the whole folder; if files are missing, unzip the release again.",
         folder);
#endif
}

static bool pkg_is_portable(const std::string& pkg) {
    SDL_PathInfo info;
    return !pkg.empty() && SDL_GetPathInfo((pkg + "portable.txt").c_str(), &info);
}

static std::string data_dir_of(const std::string& pkg, const std::vector<std::string>& passthru) {
    for (size_t i = 0; i < passthru.size(); i++) {
        const std::string& s = passthru[i];
        if (s == "--data-dir" && i + 1 < passthru.size() && !passthru[i + 1].empty()) return passthru[i + 1];
        if (s.rfind("--data-dir=", 0) == 0 && s.size() > strlen("--data-dir=")) return s.substr(strlen("--data-dir="));
    }
    if (pkg_is_portable(pkg)) return pkg + "data";
#if defined(__APPLE__)
    const char* home = SDL_getenv("HOME");
    return std::string(home ? home : ".") + "/Library/Application Support/nsmbu";
#elif defined(_WIN32)

    const char* root = SDL_getenv("LOCALAPPDATA");
    if (root && *root) return std::string(root) + "\\NSMBU";
    const char* profile = SDL_getenv("USERPROFILE");
    return std::string(profile ? profile : ".") + "\\AppData\\Local\\NSMBU";
#else
    const char* xdg = SDL_getenv("XDG_DATA_HOME");
    if (xdg && *xdg) return std::string(xdg) + "/nsmbu";
    const char* home = SDL_getenv("HOME");
    return std::string(home ? home : ".") + "/.local/share/nsmbu";
#endif
}

static bool check_writable(const std::string& data) {
    std::string probe = data + "/.write-test";
    std::string what;
    if (!SDL_CreateDirectory(data.c_str())) what = "creating the folder " + data;
    else {
        SDL_IOStream* f = SDL_IOFromFile(probe.c_str(), "wb");
        if (!f) what = "creating the file " + probe;
        else {
            bool ok = SDL_WriteIO(f, "ok\n", 3) == 3;
            ok = SDL_CloseIO(f) && ok;
            if (!ok) what = "writing the file " + probe;
            SDL_RemovePath(probe.c_str());
        }
    }
    if (what.empty()) return true;
    std::string err = SDL_GetError();
    const bool portable = pkg_is_portable(A.pkg);
    fail("The folder for the game, saves and settings cannot be written to: " + what + " failed (" + err + ").",
         portable ? "Copy the whole unzipped folder to a place you can write to (for example your home folder or a "
                   "Games folder; not a disk image, a read-only drive or another user's folder) and open NSMBU "
                   "from there."
                  : "Setup keeps the game, saves and settings in " + data + ". Check that you can write there, or "
                   "choose another place (start with --data-dir FOLDER), then open NSMBU again.",
         portable ? A.pkg : data);
    return false;
}

static std::string fatal_log_file() {
    if (!A.pkg.empty()) {
        std::string data = data_dir_of(A.pkg, A.passthru);
        if (SDL_CreateDirectory(data.c_str())) {
            std::string p = data + "/setup-window.log";
            if (SDL_IOStream* f = SDL_IOFromFile(p.c_str(), "ab")) return SDL_CloseIO(f), p;
        }
    }
#if defined(__APPLE__)
    const char* home = SDL_getenv("HOME");
    std::string dir = std::string(home ? home : "/tmp") + "/Library/Logs";
    if (!home || !SDL_CreateDirectory(dir.c_str())) dir = "/tmp";
    return dir + "/NSMBU setup.log";
#elif defined(_WIN32)
    const char* t = SDL_getenv("TEMP");
    return std::string(t ? t : ".") + "\\NSMBU setup.log";
#else
    const char* t = SDL_getenv("TMPDIR");
    return std::string(t && *t ? t : "/tmp") + "/nsmbu-setup-log.log";
#endif
}

static void fail(const std::string& what, const std::string& todo, const std::string& folder) {
    A.fatal = what;
    A.fatal_hint = todo;
    A.fatal_folder = folder;
    go(Screen::Fatal);
    SDL_Log("Setup could not continue: %s", what.c_str());
    SDL_Log("What to do: %s", todo.c_str());
    std::string path = fatal_log_file();
    if (SDL_IOStream* f = SDL_IOFromFile(path.c_str(), "ab")) {
        char when[64] = "";
        SDL_Time t;
        SDL_DateTime dt;
        if (SDL_GetCurrentTime(&t) && SDL_TimeToDateTime(t, &dt, true))
            snprintf(when, sizeof when, "%04d-%02d-%02d %02d:%02d:%02d", dt.year, dt.month, dt.day, dt.hour, dt.minute,
                     dt.second);
        std::string text = std::string("==== ") + when + " NSMBU" + (A.version.empty() ? "" : " " + A.version) +
                           ": setup could not continue\n" +
                           what + "\nWhat to do: " + todo + "\n";
        const char* bp = SDL_GetBasePath();
        text += std::string("Program: ") + (bp ? bp : "?") + "\nRelease folder: " + (A.pkg.empty() ? "(not found)" : A.pkg) + "\n";
        if (!A.log_path.empty()) text += "Setup log: " + A.log_path + "\n";
        for (auto& l : A.log) text += "  " + l + "\n";
        SDL_WriteIO(f, text.data(), text.size());
        SDL_CloseIO(f);
        A.fatal_log = path;
    } else {
        A.fatal_log.clear();
    }
}

static std::string package_version() {
    J m;
    if (!parse_json(read_file(A.pkg + "sdk/manifest.json"), m)) return "";
    return m.str("version");
}

static bool path_is_file(const std::string& path) {
    SDL_PathInfo info;
    return SDL_GetPathInfo(path.c_str(), &info) && info.type == SDL_PATHTYPE_FILE;
}

static void start_child() {
    std::vector<std::string> args;
    SDL_SetEnvironmentVariable(SDL_GetEnvironment(), "PYTHONDONTWRITEBYTECODE", "1", true);
#if defined(__APPLE__)
    std::string release_sh = A.pkg + "tools/Setup in Terminal.command";
    std::string setup_py = A.pkg + "installer/setup.py";
    if (path_is_file(release_sh)) args = {"/bin/bash", release_sh};
    else if (path_is_file(setup_py)) args = {"/usr/bin/python3", setup_py};
    else {
        fail("Setup could not start: " + release_sh + " and " + setup_py + " are missing.",
             "Keep this program inside the release folder (or the source tree) so tools/ is next to it.", A.pkg);
        return;
    }
#elif defined(_WIN32)

    if (!have_bundled_python(A.pkg)) {
        fail("Setup could not start: " + bundled_python(A.pkg) + " is missing.",
             "The release is incomplete: unzip it again (the whole zip, keeping its folders) and start NSMBU.exe "
             "from the unzipped folder.",
             A.pkg);
        return;
    }
    args = {bundled_python(A.pkg), A.pkg + "installer\\setup.py"};
#else
    std::string release_sh = A.pkg + "tools/setup-in-terminal.sh";
    std::string setup_py = A.pkg + "installer/setup.py";
    if (path_is_file(release_sh)) args = {"/bin/sh", release_sh};
    else if (path_is_file(setup_py)) args = {"python3", setup_py};
    else {
        fail("Setup could not start: " + release_sh + " and " + setup_py + " are missing.",
             "Keep this program inside the release folder (or the source tree) so tools/ is next to it.", A.pkg);
        return;
    }
#endif
    args.push_back("--gui-protocol");
    for (auto& a : A.passthru) args.push_back(a);
    go(Screen::Starting);
    if (!A.child.start(args, A.pkg)) {
        std::string err = SDL_GetError();
        std::string cmd;
        for (auto& a : args) cmd += (cmd.empty() ? "" : " ") + a;
        fail("Could not start the setup process (" + cmd + "): " + err + ".",
             "Check that the release folder is complete (unzip it again if files are missing). You can also run the "
             "setup in a terminal: " SETUP_IN_TERMINAL " in the release folder.",
             A.pkg);
    }
}

static void automation_frame() {
    if (A.script.t != J::Arr) return;
    if (A.script_pos >= A.script.a.size()) {
        if (A.shot_pending.empty()) A.exit_code = 0;
        return;
    }
    const J& st = A.script.a[A.script_pos];
    double now = SDL_GetTicks() / 1000.0;
    if (A.step_started == 0) A.step_started = now;
    if (now - A.step_started > 45 * 60) {
        SDL_Log("automation: timeout waiting for screen %s (at %s)", st.str("screen").c_str(), screen_name(A.screen));
        A.exit_code = 2;
        return;
    }
    if (st.str("screen") != screen_name(A.screen)) {
        A.stable_frames = A.ready_frames = 0;
        A.shot_taken = false;
        return;
    }
    if (A.stable_frames == 0 && st.get("set")) {
        const J& s = *st.get("set");
        for (auto& kv : s.o) {
            const std::string& k = kv.first;
            const std::string v = kv.second.t == J::Str ? kv.second.s : "";
            if (k == "source") set_source(v);
            else if (k == "disc_key_file") set_disc_key_file(v);
            else if (k == "common_key_file") set_common_key_file(v);
            else if (k == "disc_mode") set_disc_mode(v == "paste" ? 1 : 0);
            else if (k == "common_mode") set_common_mode(v == "paste" ? 1 : 0);
            else if (k == "save_kind")
                set_save_kind(v == "hd" ? 1 : v == "legacy" ? 2 : v == "other" ? 3 : 0);
            else if (k == "save_path") set_save_path(v);
        }
    }
    A.stable_frames++;
    if (st.boolean("idle") && busy()) return;
    if (!st.str("when_step").empty()) {
        int i = step_index(st.str("when_step"));
        if (i < 0 || A.steps[i].state != 1 || A.steps[i].done <= 0) return;
    }
    if (++A.ready_frames < 20) return;
    if (!st.str("shot").empty() && !A.shot_taken) {
        A.shot_pending = st.str("shot");
        A.shot_taken = true;
        return;
    }
    if (!A.shot_pending.empty()) return;
    A.ready_frames = 0;
    A.shot_taken = false;
    if (!st.str("click").empty()) A.autoclick = st.str("click");
    A.script_pos++;
    A.stable_frames = 0;
    A.step_started = now;
}

static void save_shot(SDL_Renderer* r, const std::string& name) {
    SDL_Surface* s = SDL_RenderReadPixels(r, nullptr);
    if (!s) return;
    std::string path = A.shots_dir + "/" + name;
    if (!SDL_SavePNG(s, path.c_str())) SDL_Log("screenshot %s: %s", path.c_str(), SDL_GetError());
    SDL_DestroySurface(s);
}

static bool game_ready(const std::string& pkg, const std::vector<std::string>& passthru) {
    SDL_PathInfo info;
    std::string data = data_dir_of(pkg, passthru);
    J st, man;
    if (!parse_json(read_file(data + "/install.json"), st) || !parse_json(read_file(pkg + "sdk/manifest.json"), man))
        return false;
    if (st.str("version") != man.str("version") || st.boolean("placeholder_code")) return false;
    if (!st.str("app").empty()) return false;
    auto resolve = [&](std::string p) {
        bool abs = !p.empty() && (p[0] == '/' || p[0] == '\\' || (p.size() > 1 && p[1] == ':'));
        return p.empty() || abs ? p : data + "/" + p;
    };
    std::string exe = resolve(st.str("exe")), game = resolve(st.str("game_dir"));
    if (exe.empty() || game.empty() || !SDL_GetPathInfo(exe.c_str(), &info) ||
        !SDL_GetPathInfo((game + "/code/red-pro2.rpx").c_str(), &info))
        return false;
    g_exe = exe, g_game_dir = game, g_data_dir = data;
    return true;
}

#ifdef __linux__
static bool dir_has_plugin(const std::string& dir) {
    std::error_code ec;
    if (!std::filesystem::is_directory(dir, ec)) return false;
    for (const auto& entry : std::filesystem::directory_iterator(dir, ec))
        if (entry.path().extension() == ".so") return true;
    return false;
}

static void use_user_libdecor_plugin() {
    if (const char* e = std::getenv("LIBDECOR_PLUGIN_DIR"))
        if (*e) return;
    const char* system_dirs[] = {
        "/usr/lib/x86_64-linux-gnu/libdecor/plugins-1",
        "/usr/lib/aarch64-linux-gnu/libdecor/plugins-1",
        "/usr/lib64/libdecor/plugins-1",
        "/usr/lib/libdecor/plugins-1",
    };
    for (const char* dir : system_dirs)
        if (dir_has_plugin(dir)) return;
    const char* home = std::getenv("HOME");
    if (!home || !*home) return;
    std::string user = std::string(home) + "/.local/lib/libdecor/plugins-1";
    if (!dir_has_plugin(user)) return;
    setenv("LIBDECOR_PLUGIN_DIR", user.c_str(), 0);
}
#else
static void use_user_libdecor_plugin() {}
#endif

int main(int argc, char** argv) {
#ifdef _WIN32
    if (argc > 1 && !strcmp(argv[1], "--console-setup")) {
        return console_setup(find_package().pkg, std::vector<std::string>(argv + 2, argv + argc));
    }
#endif
    std::string automate;
    bool want_setup = false;
    for (int i = 1; i < argc; i++) {
        std::string a = argv[i];
        if (a == "--automate" && i + 1 < argc) automate = argv[++i];
        else if (a == "--screenshots" && i + 1 < argc) A.shots_dir = argv[++i];
        else if (a == "--self-test") A.self_test = true;
        else if (a == "--setup") want_setup = true;
        else if (a.rfind("-psn_", 0) == 0) continue;
        else A.passthru.push_back(a);
    }
    if (!automate.empty()) {
        std::string text = read_file(automate);
        JParser jp{text.data(), text.data() + text.size()};
        A.script = jp.value();
        if (!jp.ok || A.script.t != J::Arr) {
            SDL_Log("automation script %s is not a JSON array", automate.c_str());
            return 2;
        }
    }

    if (!want_setup && automate.empty() && !A.self_test && !shift_held()) {
        std::string pkg = find_package().pkg;
        if (!pkg.empty() && game_ready(pkg, A.passthru) && launch_game()) return 0;
    }

    SDL_SetHint(SDL_HINT_APP_NAME, "NSMBU");
    use_user_libdecor_plugin();
    if (!SDL_Init(SDL_INIT_VIDEO)) {
        SDL_Log("SDL_Init: %s", SDL_GetError());
        return 1;
    }
    bool offscreen = SDL_GetCurrentVideoDriver() && !strcmp(SDL_GetCurrentVideoDriver(), "offscreen");
    if (offscreen) SDL_SetHint(SDL_HINT_RENDER_DRIVER, "software");
    float scale = offscreen ? 1.0f : SDL_GetDisplayContentScale(SDL_GetPrimaryDisplay());
    if (scale <= 0) scale = 1.0f;
    {
        SDL_PropertiesID wp = SDL_CreateProperties();
        SDL_SetStringProperty(wp, SDL_PROP_WINDOW_CREATE_TITLE_STRING, "NSMBU");
        SDL_SetNumberProperty(wp, SDL_PROP_WINDOW_CREATE_WIDTH_NUMBER, (int)(960 * scale));
        SDL_SetNumberProperty(wp, SDL_PROP_WINDOW_CREATE_HEIGHT_NUMBER, (int)(640 * scale));
        SDL_SetNumberProperty(wp, SDL_PROP_WINDOW_CREATE_FLAGS_NUMBER,
                              SDL_WINDOW_RESIZABLE | SDL_WINDOW_HIGH_PIXEL_DENSITY | SDL_WINDOW_HIDDEN);

        if (offscreen) SDL_SetBooleanProperty(wp, SDL_PROP_WINDOW_CREATE_EXTERNAL_GRAPHICS_CONTEXT_BOOLEAN, true);
        g_window = SDL_CreateWindowWithProperties(wp);
        SDL_DestroyProperties(wp);
    }
    if (!g_window) {
        SDL_Log("SDL_CreateWindow: %s", SDL_GetError());
        return 1;
    }
    SDL_SetWindowMinimumSize(g_window, (int)(760 * scale), (int)(560 * scale));
    SDL_Renderer* renderer = SDL_CreateRenderer(g_window, offscreen ? "software" : nullptr);
    if (!renderer) {
        SDL_Log("SDL_CreateRenderer: %s", SDL_GetError());
        return 1;
    }
    SDL_SetRenderVSync(renderer, 1);
    SDL_SetWindowPosition(g_window, SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED);
    SDL_ShowWindow(g_window);

    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImGuiIO& io = ImGui::GetIO();
    io.IniFilename = nullptr;
    io.LogFilename = nullptr;
    apply_style(scale);
    load_fonts(scale);
    ImGui_ImplSDL3_InitForSDLRenderer(g_window, renderer);
    ImGui_ImplSDLRenderer3_Init(renderer);

    PackageSearch found = find_package();
    A.pkg = found.pkg;
    if (!A.pkg.empty()) {
        A.version = package_version();
        if (!A.version.empty()) SDL_SetWindowTitle(g_window, ("NSMBU " + A.version).c_str());
        if (found.translocated)
            addlog("macOS started a temporary copy of the app (App Translocation); the release folder is " + A.pkg);
    }
    if (A.pkg.empty()) {
        fail_no_package(found);
    } else if (!check_writable(data_dir_of(A.pkg, A.passthru))) {

    } else {
#ifdef __APPLE__
        if (!clt_ok()) go(Screen::NeedCLT);
        else start_child();
#else
        start_child();
#endif
    }

    double self_test_deadline = SDL_GetTicks() / 1000.0 + 600;
    int self_test_frames = 0;
    bool quit_requested = false;
    while (A.exit_code < 0) {
        SDL_Event ev;
        bool idle = A.script.t != J::Arr && !A.self_test && A.screen != Screen::Installing && A.screen != Screen::Starting &&
                    !busy() && A.screen != Screen::NeedCLT;
        if (idle) SDL_WaitEventTimeout(nullptr, 100);
        while (SDL_PollEvent(&ev)) {
            ImGui_ImplSDL3_ProcessEvent(&ev);
            if (ev.type == SDL_EVENT_QUIT || (ev.type == SDL_EVENT_WINDOW_CLOSE_REQUESTED && ev.window.windowID == SDL_GetWindowID(g_window)))
                quit_requested = true;
            if (ev.type == SDL_EVENT_SYSTEM_THEME_CHANGED && system_dark() != g_dark) apply_style(g_ui_scale);
        }
        pump_child();
        {
            std::string target, result;
            bool done = false;
            {
                std::lock_guard<std::mutex> lk(A.dlg_mu);
                if (A.dlg_done) done = true, target = A.dlg_target, result = A.dlg_result, A.dlg_done = false;
            }
            if (done) apply_dialog(target, result);
        }
        automation_frame();

        ImGui_ImplSDLRenderer3_NewFrame();
        ImGui_ImplSDL3_NewFrame();
        ImGui::NewFrame();
        ImGui::SetNextWindowPos(ImVec2(0, 0));
        ImGui::SetNextWindowSize(io.DisplaySize);
        ImGui::Begin("##main", nullptr,
                     ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoSavedSettings |
                         ImGuiWindowFlags_NoBringToFrontOnFocus);
        switch (A.screen) {
        case Screen::Starting: screen_starting(); break;
        case Screen::NeedCLT: screen_clt(); break;
        case Screen::Menu: screen_menu(); break;
        case Screen::Welcome: screen_welcome(); break;
        case Screen::Source: screen_source(); break;
        case Screen::Keys: screen_keys(); break;
        case Screen::Installing: screen_installing(); break;
        case Screen::Error: screen_error(); break;
        case Screen::Save: screen_save(); break;
        case Screen::Done: screen_done(); break;
        case Screen::Shaders: screen_shaders(); break;
        case Screen::Fatal: screen_fatal(); break;
        }
        if (!A.toast.empty() && ImGui::GetTime() < A.toast_until) {
            ImGui::SetCursorPos(ImVec2(ImGui::GetStyle().WindowPadding.x,
                                       ImGui::GetWindowHeight() - ImGui::GetStyle().WindowPadding.y - ImGui::GetFrameHeight()));
            colored(GOOD, A.toast);
        }
        if (quit_requested) {
            if (A.screen == Screen::Installing) ImGui::OpenPopup("Stop setup?");
            else if (A.screen == Screen::Shaders && A.shader_running) ImGui::OpenPopup("Stop recompiling?");
            else A.exit_code = 0;
            quit_requested = false;
        }
        if (begin_confirm("Stop setup?")) {
            ImGui::TextUnformatted("Setup is still working. Stop it now?");
            muted("You can run Setup again later; finished steps are kept.");
            ImGui::Spacing();
            float gap = ImGui::GetStyle().ItemSpacing.x;
            centered_button_row(180 + gap + 180);
            if (button("Stop setup", ImVec2(180, 0), true)) {
                A.child.stop(true);
                A.exit_code = 1;
            }
            ImGui::SameLine();
            if (button("Continue", ImVec2(180, 0))) ImGui::CloseCurrentPopup();
            end_confirm();
        }
        ImGui::End();
        ImGui::Render();

        SDL_SetRenderScale(renderer, io.DisplayFramebufferScale.x, io.DisplayFramebufferScale.y);
        SDL_SetRenderDrawColor(renderer, g_clear_r, g_clear_g, g_clear_b, 255);
        SDL_RenderClear(renderer);
        ImGui_ImplSDLRenderer3_RenderDrawData(ImGui::GetDrawData(), renderer);
        if (!A.shot_pending.empty()) {
            if (!A.shots_dir.empty()) save_shot(renderer, A.shot_pending);
            A.shot_pending.clear();
        }
        if (A.self_test) {
            if (A.screen == Screen::Welcome || A.screen == Screen::Menu) {
                if (++self_test_frames == 30) {
                    if (!A.shots_dir.empty()) save_shot(renderer, std::string("selftest-") + screen_name(A.screen) + ".png");
                    SDL_Log("self-test: setup.py answered (%s %s), %s screen rendered", A.version.c_str(),
                            A.platform.c_str(), screen_name(A.screen));
                    A.exit_code = 0;
                }
            } else if (A.screen == Screen::Fatal || A.screen == Screen::NeedCLT ||
                       SDL_GetTicks() / 1000.0 > self_test_deadline) {
                SDL_Log("self-test failed at the %s screen: %s", screen_name(A.screen), A.fatal.c_str());
                if (!A.fatal_hint.empty()) SDL_Log("  what to do: %s", A.fatal_hint.c_str());
                for (auto& l : A.log) SDL_Log("  %s", l.c_str());
                A.exit_code = 1;
            }
        }
        SDL_RenderPresent(renderer);
        if (A.last_screen != screen_name(A.screen)) {
            A.last_screen = screen_name(A.screen);
            if (A.script.t == J::Arr || A.self_test) SDL_Log("screen: %s", A.last_screen.c_str());
        }
    }
    SDL_memset(A.disc_paste, 0, sizeof A.disc_paste);
    SDL_memset(A.paste, 0, sizeof A.paste);
    A.child.stop(false);
    ImGui_ImplSDLRenderer3_Shutdown();
    ImGui_ImplSDL3_Shutdown();
    ImGui::DestroyContext();
    SDL_DestroyRenderer(renderer);
    SDL_DestroyWindow(g_window);
    SDL_Quit();
    if (A.exec_game && !launch_game()) {
        SDL_ShowSimpleMessageBox(SDL_MESSAGEBOX_ERROR, "NSMBU", ("Could not start " + g_exe).c_str(), nullptr);
        return 1;
    }
    return A.exit_code;
}
