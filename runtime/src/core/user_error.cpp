#include "core/user_error.h"

#include "app_title.h"
#include "core/build_info.h"
#include "core/env.h"
#include "core/runtime.h"
#include "guest_addr.h"
#include "platform/host.h"

#include <algorithm>
#include <cctype>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <string>
#include <string_view>
#include <vector>

#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <commctrl.h>
#include <shellapi.h>
#include <windows.h>
#endif
#if defined(NSMBU_SDL_HOST)
#include <SDL3/SDL.h>
#include <SDL3/SDL_messagebox.h>
#include <SDL3/SDL_misc.h>
#endif
#if defined(__APPLE__)
#include <CoreFoundation/CoreFoundation.h>
#include <sys/sysctl.h>
#endif
#if TARGET_OS_IPHONE
// runtime/src/gfx/metal/ios/app_ios.mm
void ios_show_error(const char* title, const char* message);
void ios_open_url(const char* url);
#endif

namespace user_error {
namespace {

namespace fs = std::filesystem;

std::string read_file(const fs::path& p, size_t max_bytes) {
    std::ifstream in(p, std::ios::binary);
    if (!in) return {};
    std::string data((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
    if (data.size() > max_bytes) data = data.substr(data.size() - max_bytes);
    return data;
}

fs::path newest_crash_log(const fs::path& dir) {
    std::error_code ec;
    fs::path best;
    fs::file_time_type best_t{};
    bool have = false;
    for (auto& e : fs::directory_iterator(dir, ec)) {
        if (ec || !e.is_regular_file(ec)) continue;
        const std::string name = e.path().filename().string();
        if (name.rfind("crash-", 0) != 0 || name.size() < 5 || name.compare(name.size() - 4, 4, ".log") != 0) continue;
        const auto t = e.last_write_time(ec);
        if (ec) continue;
        if (!have || t > best_t) {
            best = e.path();
            best_t = t;
            have = true;
        }
    }
    return best;
}

bool hidden() {
    const char* h = env::HIDDEN_WINDOWS();
    return h && *h && strcmp(h, "0") != 0;
}

const char* platform_label() {
#ifdef __ANDROID__
    return "Android";
#elif defined(_WIN32)
    return "Windows";
#elif TARGET_OS_IPHONE
    return "iOS";
#elif defined(__APPLE__)
    return "macOS";
#elif defined(__aarch64__) || defined(__arm64__)
    return "Linux (arm64)";
#else
    return "Linux (x86-64)";
#endif
}

std::string trim_ws(std::string s) {
    while (!s.empty() && std::isspace(static_cast<unsigned char>(s.front()))) s.erase(s.begin());
    while (!s.empty() && std::isspace(static_cast<unsigned char>(s.back()))) s.pop_back();
    return s;
}

std::string cpu_label() {
#if defined(_WIN32)
    wchar_t buf[256];
    DWORD size = sizeof buf;
    if (RegGetValueW(HKEY_LOCAL_MACHINE, L"HARDWARE\\DESCRIPTION\\System\\CentralProcessor\\0",
                     L"ProcessorNameString", RRF_RT_REG_SZ, nullptr, buf, &size) == ERROR_SUCCESS) {
        char out[256];
        const int n = WideCharToMultiByte(CP_UTF8, 0, buf, -1, out, sizeof out, nullptr, nullptr);
        if (n > 0) return trim_ws(out);
    }
#elif defined(__APPLE__)
    char buf[256] = {};
    size_t n = sizeof buf;
    if (sysctlbyname("machdep.cpu.brand_string", buf, &n, nullptr, 0) == 0 && buf[0]) return trim_ws(buf);
    n = sizeof buf;
    if (sysctlbyname("hw.model", buf, &n, nullptr, 0) == 0 && buf[0]) return trim_ws(buf);
#else
    std::ifstream in("/proc/cpuinfo");
    std::string line;
    while (std::getline(in, line)) {
        const char* keys[] = {"model name", "Hardware", "cpu model"};
        for (const char* key : keys) {
            const size_t klen = std::strlen(key);
            if (line.compare(0, klen, key) != 0) continue;
            size_t colon = line.find(':');
            if (colon == std::string::npos) continue;
            std::string v = trim_ws(line.substr(colon + 1));
            if (!v.empty()) return v;
        }
    }
#endif
    return {};
}

std::string ram_label() {
#if defined(NSMBU_SDL_HOST)
    const int mb = SDL_GetSystemRAM();
    if (mb > 0) {
        int gb = (mb + 512) / 1024;
        if (gb < 1) gb = 1;
        return std::to_string(gb) + " GB";
    }
#endif
#if defined(_WIN32)
    MEMORYSTATUSEX st{};
    st.dwLength = sizeof st;
    if (GlobalMemoryStatusEx(&st) && st.ullTotalPhys) {
        const int gb = static_cast<int>((st.ullTotalPhys + (512ull << 20)) / (1024ull << 20));
        return std::to_string(gb < 1 ? 1 : gb) + " GB";
    }
#elif defined(__APPLE__)
    uint64_t mem = 0;
    size_t n = sizeof mem;
    if (sysctlbyname("hw.memsize", &mem, &n, nullptr, 0) == 0 && mem) {
        const int gb = static_cast<int>((mem + (512ull << 20)) / (1024ull << 20));
        return std::to_string(gb < 1 ? 1 : gb) + " GB";
    }
#else
    std::ifstream in("/proc/meminfo");
    std::string line;
    while (std::getline(in, line)) {
        if (line.compare(0, 8, "MemTotal") != 0) continue;
        unsigned long kb = 0;
        if (std::sscanf(line.c_str(), "MemTotal: %lu", &kb) == 1 && kb > 0) {
            const int gb = static_cast<int>((kb + 512 * 1024) / (1024 * 1024));
            return std::to_string(gb < 1 ? 1 : gb) + " GB";
        }
    }
#endif
    return {};
}

std::string gpu_label() {
#if !defined(_WIN32) && !defined(__APPLE__) && !defined(__ANDROID__)
    FILE* f = popen("lspci -nn 2>/dev/null", "r");
    if (!f) return {};
    char buf[512];
    std::string best;
    while (std::fgets(buf, sizeof buf, f)) {
        std::string line = buf;
        if (line.find("VGA compatible controller") == std::string::npos &&
            line.find("3D controller") == std::string::npos &&
            line.find("Display controller") == std::string::npos)
            continue;
        size_t colon = line.find(": ");
        if (colon == std::string::npos) continue;
        std::string name = trim_ws(line.substr(colon + 2));
        while (!name.empty() && (name.back() == '\n' || name.back() == '\r')) name.pop_back();
        if (!name.empty()) {
            best = name;
            break;
        }
    }
    pclose(f);
    return best;
#else
    return {};
#endif
}

std::string hardware_label() {
    const std::string cpu = cpu_label();
    const std::string gpu = gpu_label();
    const std::string ram = ram_label();
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

std::string url_encode(std::string_view s) {
    std::string out;
    out.reserve(s.size() * 3);
    for (unsigned char c : s) {
        if (std::isalnum(c) || c == '-' || c == '_' || c == '.' || c == '~') out.push_back((char)c);
        else if (c == ' ') out += "%20";
        else if (c == '\n') out += "%0A";
        else if (c == '\r') continue;
        else {
            char hex[4];
            snprintf(hex, sizeof hex, "%%%02X", c);
            out += hex;
        }
    }
    return out;
}

enum Choice { kOk = 0, kReport = 1 };

#ifdef NSMBU_SDL_HOST
Choice sdl_ask(const std::string& message) {
    const SDL_MessageBoxButtonData buttons[] = {
        {SDL_MESSAGEBOX_BUTTON_RETURNKEY_DEFAULT, kOk, "OK"},
        {0, kReport, "Report crash..."},
    };
    const SDL_MessageBoxData box{SDL_MESSAGEBOX_ERROR, nullptr, app_title::kName, message.c_str(), 2, buttons, nullptr};
    int choice = kOk;
    if (!SDL_ShowMessageBox(&box, &choice)) return kOk;
    return static_cast<Choice>(choice);
}
#endif

#ifdef _WIN32
Choice win_ask(const std::string& message) {
    const int n = MultiByteToWideChar(CP_UTF8, 0, message.c_str(), -1, nullptr, 0);
    const int tn = MultiByteToWideChar(CP_UTF8, 0, app_title::kName, -1, nullptr, 0);
    if (n <= 0 || tn <= 0) return kOk;
    std::wstring w(static_cast<size_t>(n - 1), L'\0');
    std::wstring title(static_cast<size_t>(tn - 1), L'\0');
    MultiByteToWideChar(CP_UTF8, 0, message.c_str(), -1, w.data(), n);
    MultiByteToWideChar(CP_UTF8, 0, app_title::kName, -1, title.data(), tn);

    TASKDIALOG_BUTTON buttons[] = {
        {kOk, L"OK"},
        {kReport, L"Report crash..."},
    };
    TASKDIALOGCONFIG cfg{};
    cfg.cbSize = sizeof cfg;
    cfg.dwFlags = TDF_ALLOW_DIALOG_CANCELLATION | TDF_SIZE_TO_CONTENT;
    cfg.pszWindowTitle = title.c_str();
    cfg.pszMainIcon = TD_ERROR_ICON;
    cfg.pszMainInstruction = L"Something went wrong";
    cfg.pszContent = w.c_str();
    cfg.cButtons = 2;
    cfg.pButtons = buttons;
    cfg.nDefaultButton = kOk;
    int button = kOk;
    if (FAILED(TaskDialogIndirect(&cfg, &button, nullptr, nullptr))) {
        MessageBoxW(nullptr, w.c_str(), title.c_str(), MB_OK | MB_ICONERROR);
        return kOk;
    }
    return static_cast<Choice>(button);
}
#endif

#if TARGET_OS_IPHONE
Choice apple_ask(const std::string& message) {
    // the iOS port has no upstream issue tracker to report to
    ::ios_show_error(app_title::kName, message.c_str());
    return kOk;
}
#elif defined(__APPLE__) && !defined(NSMBU_SDL_HOST)
Choice apple_ask(const std::string& message) {
    CFStringRef body = CFStringCreateWithCString(kCFAllocatorDefault, message.c_str(), kCFStringEncodingUTF8);
    CFStringRef title = CFStringCreateWithCString(kCFAllocatorDefault, app_title::kName, kCFStringEncodingUTF8);
    CFOptionFlags result = 0;
    CFUserNotificationDisplayAlert(0, kCFUserNotificationStopAlertLevel, nullptr, nullptr, nullptr, title, body,
                                   CFSTR("OK"), CFSTR("Report crash..."), nullptr, &result);
    if (title) CFRelease(title);
    if (body) CFRelease(body);
    if (result == kCFUserNotificationDefaultResponse) return kOk;
    if (result == kCFUserNotificationAlternateResponse) return kReport;
    return kOk;
}
#endif

Choice ask(const std::string& message) {
#ifdef _WIN32
    return win_ask(message);
#elif defined(NSMBU_SDL_HOST)
    return sdl_ask(message);
#elif defined(__APPLE__)
    return apple_ask(message);
#else
    (void)message;
    return kOk;
#endif
}

}

std::string wrap_message(const std::string& text, size_t width) {
    if (width < 24) width = 24;
    std::string out;
    size_t col = 0;
    for (size_t i = 0; i < text.size();) {
        if (text[i] == '\n') {
            out.push_back('\n');
            col = 0;
            i++;
            continue;
        }
        size_t j = i;
        while (j < text.size() && text[j] != '\n' && text[j] != ' ' && text[j] != '/') j++;
        if (j < text.size() && (text[j] == ' ' || text[j] == '/')) j++;
        if (j == i) j = i + 1;
        const size_t word = j - i;
        if (col > 0 && col + word > width) {
            out.push_back('\n');
            col = 0;
        }
        out.append(text, i, word);
        col += word;
        i = j;
    }
    return out;
}

std::string collect_logs_text(size_t max_bytes) {
    host::ensure_log_dir();
    const fs::path dir = host::log_dir();
    std::string out;
    const fs::path crash = newest_crash_log(dir);
    if (!crash.empty()) {
        out += "===== " + crash.filename().string() + " =====\n";
        out += read_file(crash, max_bytes);
        out += "\n\n";
    }
    const fs::path run = dir / "nsmbu.log";
    if (fs::is_regular_file(run)) {
        out += "===== nsmbu.log =====\n";
        out += read_file(run, max_bytes);
        out += "\n";
    }
    if (out.empty()) out = "(no log file yet)\n";
    if (out.size() > max_bytes) out = out.substr(out.size() - max_bytes);
    return out;
}

std::string report_issue_url(const std::string& message, IssueKind kind) {
    const char* template_file =
        kind == IssueKind::InGame ? "1-in-game-bug.yml" : "2-setup-or-other-bug.yml";
    std::string version = build::version();
    const char* commit = build::commit();
    if (commit && *commit && version.find(commit) == std::string::npos) {
        if (!version.empty()) version += " ";
        version += "(";
        version += commit;
        version += ")";
    }
    std::string app_info = message;
    if (app_info.size() > 2000) app_info = app_info.substr(0, 2000) + "\n…\n";

    std::string url = "https://github.com/nsmbu-recomp/nsmbu-recomp/issues/new?template=";
    url += template_file;
    url += "&version=";
    url += url_encode(version);
    url += "&game_version=";
    url += url_encode(g_guest_build_label);
    url += "&platform=";
    url += url_encode(platform_label());
    url += "&hardware=";
    url += url_encode(hardware_label());
    url += "&app_info=";
    url += url_encode(app_info);
    return url;
}

void open_url(const char* url) {
#ifdef _WIN32
    ShellExecuteA(nullptr, "open", url, nullptr, nullptr, SW_SHOWNORMAL);
#elif defined(NSMBU_SDL_HOST)
    if (!SDL_OpenURL(url)) LOG("[user_error] cannot open %s: %s", url, SDL_GetError());
#elif TARGET_OS_IPHONE
    ::ios_open_url(url);
#elif defined(__APPLE__)
    const std::string cmd = std::string("open ") + "'" + url + "'";
    if (system(cmd.c_str()) != 0) LOG("[user_error] cannot open %s", url);
#else
    (void)url;
#endif
}

void open_logs_folder() {
    host::ensure_log_dir();
    const std::string dir = host::log_dir();
#ifdef _WIN32
    ShellExecuteA(nullptr, "explore", dir.c_str(), nullptr, nullptr, SW_SHOWNORMAL);
#elif defined(NSMBU_SDL_HOST)
    std::error_code ec;
    std::string p = fs::absolute(dir, ec).generic_string();
    if (ec) p = dir;
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
    if (!SDL_OpenURL(url.c_str())) LOG("[user_error] cannot open logs folder: %s", SDL_GetError());
#elif TARGET_OS_IPHONE
    (void)dir; // the logs are in the app's Documents folder, visible in Files
#elif defined(__APPLE__)
    const std::string cmd = "open '" + dir + "'";
    if (system(cmd.c_str()) != 0) LOG("[user_error] cannot open logs folder");
#else
    (void)dir;
#endif
}

void show_fatal(const std::string& message, IssueKind kind) {
    if (hidden()) return;
#if TARGET_OS_IPHONE
    const std::string& body = message;  // UIKit wraps to the screen's width
#else
    const std::string body = wrap_message(message);
#endif
    const Choice c = ask(body);
    if (c == kReport) {
        const std::string url = report_issue_url(message, kind);
        open_url(url.c_str());
        open_logs_folder();
    }
}

[[noreturn]] void fail(const std::string& message, IssueKind kind) {
    LOG("FATAL: %s", message.c_str());
    show_fatal(message, kind);
    fflush(stderr);
    std::_Exit(1);
}

}
