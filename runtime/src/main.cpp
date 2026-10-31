#include "core/env.h"
#ifdef __APPLE__
#include <TargetConditionals.h>
#endif
#ifndef _WIN32
#include <dlfcn.h>
#include <fcntl.h>
#include <signal.h>
#include <sys/stat.h>
#include <unistd.h>
#else
#include <fcntl.h>
#include <io.h>
#include <stdlib.h>
#include <sys/stat.h>
#endif
#include <cstdio>
#include <cstring>
#include <ctime>
#include <filesystem>
#include "guest_addr.h"
#include "platform/host.h"
#ifdef _WIN32
#include <timeapi.h>
#endif

#include <cstring>
#include <string>
#include <thread>

#include "app_title.h"
#include "gfx/renderer.h"
#include "mods/cemu_pack.h"
#include "mods/content.h"
#include "gx2/gx2.h"
#include "recomp_table.h"
#include "crash/report_header.h"
#include "crash/crash_addr.h"
#include "core/build_info.h"
#include "core/game_folder.h"
#include "core/user_error.h"
#include "crash/crashrec.h"
#include "crash/crash_context.h"
#include "agent/agent_ctl.h"
#include "input/input.h"
#include "mods/mods.h"
#include "mods/packages.h"
#include "core/runtime.h"
#ifdef __ANDROID__
#include <SDL3/SDL.h>
#include <SDL3/SDL_main.h>
namespace interp { void set_mode(int); }
#endif

#ifdef NSMBU_HAS_VULKAN
namespace gfxvk { int renderer_smoke_test(); }
#endif
#ifdef NSMBU_HAS_METAL
int gfx_headstart_warm();
#endif

void mem_setup_heaps(uint32_t data_end);
void trace_dump(FILE* f, unsigned last);
void mem_init_data_imports(uint32_t alloc_slot, uint32_t alloc_ex_slot, uint32_t free_slot);

static bool crash_cli_no_dialog() {
#ifndef NDEBUG
    return env::CLI.enabled();
#else
    return false;
#endif
}

static int crash_append(char* buf, int n, int cap, const char* fmt, uint32_t v) {
    if (n < 0 || n >= cap) return cap - 1;
    int wrote = snprintf(buf + n, (size_t)(cap - n), fmt, v);
    if (wrote < 0 || n + wrote >= cap) return cap - 1;
    return n + wrote;
}

static void crash_guest_cpu(int fd, void (*out)(int, const char*, size_t)) {
    Cpu* c = threads::current();
    if (!c) return;
    char buf[320];
    int n = snprintf(buf, sizeof buf, "  guest lr=%08X", c->lr);
    if (uint32_t fn = recomp_fn_containing(c->lr)) n = crash_append(buf, n, (int)sizeof buf, " fn=%08X", fn);
    n = crash_append(buf, n, (int)sizeof buf, " ctr=%08X", c->ctr);
    if (uint32_t fn = recomp_fn_containing(c->ctr)) n = crash_append(buf, n, (int)sizeof buf, " fn=%08X", fn);
    n = crash_append(buf, n, (int)sizeof buf, " cr=%08X\n", ppc_mfcr(c));
    out(fd, buf, (size_t)n);
    for (int i = 0; i < 32; i += 8) {
        n = snprintf(buf, sizeof buf, "  r%-2d %08X %08X %08X %08X %08X %08X %08X %08X\n", i, c->r[i], c->r[i + 1],
                     c->r[i + 2], c->r[i + 3], c->r[i + 4], c->r[i + 5], c->r[i + 6], c->r[i + 7]);
        if (n < 0) n = (int)sizeof buf - 1;
        if (n > (int)sizeof buf) n = (int)sizeof buf;
        out(fd, buf, (size_t)n);
    }
    out(fd, "  guest call chain:", 19);
    uint32_t sp = c->r[1];
    for (int i = 0; i < 24 && sp >= 0x10000000u && sp < 0xF0000000u; i++) {
        uint32_t prev = ld32(sp);
        if (!prev || prev <= sp || prev - sp > 0x100000u) break;
        n = snprintf(buf, sizeof buf, " %08X", ld32(prev + 4));
        if (n > 0) out(fd, buf, (size_t)n);
        sp = prev;
    }
    out(fd, "\n", 1);
}

#ifndef _WIN32

static int g_crash_fd = -1;
static void crash_raw(int fd, const char* s, size_t n) {
    if (write(2, s, n) < 0) {}
    if (fd >= 0 && write(fd, s, n) < 0) {}
}
static void crash_log_raw(int fd, const char* s, size_t n) {
    if (fd >= 0 && write(fd, s, n) < 0) {}
}
static char g_crash_log_dir[512];
static void init_crash_log_dir() {
    host::ensure_log_dir();
    std::snprintf(g_crash_log_dir, sizeof g_crash_log_dir, "%s", host::log_dir().c_str());
}
static void crash_out(int fd, const char* s, size_t n) { crash_context::redact(fd, {s,n}, crash_raw); }
static void crash_log_only(int fd, const char* s, size_t n) { crash_context::redact(fd, {s,n}, crash_log_raw); }
static void crash_handler(int sig, siginfo_t* si, void* uctx) {
    uintptr_t a = (uintptr_t)si->si_addr;
    uintptr_t base = (uintptr_t)PPC_MEM_BASE;
    char path[640];
    {
        if (g_crash_log_dir[0]) mkdir(g_crash_log_dir, 0755);
        time_t t = time(nullptr);
        struct tm tmv;
        localtime_r(&t, &tmv);
        char name[64];
        strftime(name, sizeof name, "crash-%Y%m%d-%H%M%S.log", &tmv);
        std::snprintf(path, sizeof path, "%s/%s", g_crash_log_dir[0] ? g_crash_log_dir : "logs", name);
        g_crash_fd = open(path, O_WRONLY | O_CREAT | O_TRUNC, 0644);
    }
    const int fd = g_crash_fd;
    char buf[256];
    int n;
    if (a >= base && a < base + 0x100000000ull)
        n = snprintf(buf, sizeof buf, "\nCRASH: signal %d at guest address %08X\n", sig, (unsigned)(a - base));
    else
        n = snprintf(buf, sizeof buf, "\nCRASH: signal %d at host address %p\n", sig, si->si_addr);
    crash_out(fd, buf, n);

    char where[384], mpath[512] = "", line[1024];
    if (uintptr_t pc = crash_addr::context_pc(uctx)) {
        crash_addr::describe(where, sizeof where, pc, mpath, sizeof mpath);
        n = crash_addr::fit(snprintf(line, sizeof line, "  host pc %p%s\n", (void*)pc, where), sizeof line);
        crash_out(fd, line, n);
        if (mpath[0]) {
            n = crash_addr::fit(snprintf(line, sizeof line, "  module: %s\n", mpath), sizeof line);
            crash_out(fd, line, n);
        }
    }

    if (!(a >= base && a < base + 0x100000000ull) && crash_addr::describe(where, sizeof where, a)) {
        n = crash_addr::fit(snprintf(line, sizeof line, "  fault address %p%s\n", si->si_addr, where), sizeof line);
        crash_out(fd, line, n);
    }
    crash_guest_cpu(fd, crash_out);
    crash_addr::host_backtrace(fd, crash_out, uctx);
    crash_context::note(fd, crash_out);
    crashrec::crash_note(fd, crash_out);
    if (fd >= 0) {
        crash_log_only(fd, "\n--- last log lines ---\n", 24);
        log_ring_write(fd, crash_log_only);
        close(fd);
        n = snprintf(buf, sizeof buf, "[crash] wrote %s\n", path);
        crash_out(-1, buf, n);
    }
    if (g_ppc_trace) {
        FILE* f = fopen("trace_dump.txt", "w");
        if (f) { trace_dump(f, 3000); fclose(f); if (write(2, "[trace] wrote trace_dump.txt\n", 29) < 0) {} }
    }
    input::stop_rumble_now();
    if (!crash_cli_no_dialog()) {
#ifndef __ANDROID__
        if (fork() == 0) {
            std::string msg = "The game crashed.\n\nA crash log was written to:\n";
            msg += path;
            msg += "\n\nReport crash opens GitHub and the logs folder; describe what happened yourself.";
            user_error::show_fatal(msg, user_error::IssueKind::InGame);
            _exit(0);
        }
#endif
    }
    _exit(128 + sig);
}

static void install_crash_handler() {
    init_crash_log_dir();
    static char altstack[1 << 16];
    stack_t ss{};
    ss.ss_sp = altstack;
    ss.ss_size = sizeof altstack;
    sigaltstack(&ss, nullptr);
    struct sigaction sa{};
    sa.sa_sigaction = crash_handler;
    sa.sa_flags = SA_SIGINFO | SA_ONSTACK;
    sigaction(SIGSEGV, &sa, nullptr);
    sigaction(SIGBUS, &sa, nullptr);
    sigaction(SIGILL, &sa, nullptr);
    sigaction(SIGFPE, &sa, nullptr);
    crash_addr::prime();
}

#else
static void win_crash_raw(int fd, const char* s, size_t n) {
    fwrite(s, 1, n, stderr);
    if (fd >= 0) _write(fd, s, (unsigned)n);
}
static void win_crash_log_raw(int fd, const char* s, size_t n) { if (fd >= 0) _write(fd, s, (unsigned)n); }
static void win_crash_out(int fd, const char* s, size_t n) { crash_context::redact(fd, {s,n}, win_crash_raw); }
static void win_crash_log_only(int fd, const char* s, size_t n) { crash_context::redact(fd, {s,n}, win_crash_log_raw); }
static char g_crash_log_dir[512];
static void init_crash_log_dir() {
    host::ensure_log_dir();
    std::snprintf(g_crash_log_dir, sizeof g_crash_log_dir, "%s", host::log_dir().c_str());
}
static LONG WINAPI crash_handler(EXCEPTION_POINTERS* ex) {
    auto code=ex->ExceptionRecord->ExceptionCode;
    if (g_crash_log_dir[0]) {
        std::error_code ec;
        std::filesystem::create_directories(g_crash_log_dir, ec);
    }
    char path[640]; time_t t=time(nullptr); struct tm tmv; localtime_s(&tmv,&t);
    char name[64];
    strftime(name,sizeof name,"crash-%Y%m%d-%H%M%S.log",&tmv);
    std::snprintf(path,sizeof path,"%s/%s", g_crash_log_dir[0] ? g_crash_log_dir : "logs", name);
    int fd=_open(path,_O_WRONLY|_O_CREAT|_O_TRUNC|_O_BINARY,_S_IREAD|_S_IWRITE);
    int n;

    char where[384], mpath[512]="", line[1024];
    using crash_addr::fit;
    crash_addr::describe(where,sizeof where,(uintptr_t)ex->ExceptionRecord->ExceptionAddress,mpath,sizeof mpath);
    n=fit(snprintf(line,sizeof line,"CRASH: Windows exception %08lX at %p%s\n",code,ex->ExceptionRecord->ExceptionAddress,where),sizeof line); win_crash_out(fd,line,n);
    if(mpath[0]){n=fit(snprintf(line,sizeof line,"  module: %s\n",mpath),sizeof line); win_crash_out(fd,line,n);}

    const EXCEPTION_RECORD* er=ex->ExceptionRecord;
    if((code==EXCEPTION_ACCESS_VIOLATION||code==EXCEPTION_IN_PAGE_ERROR)&&er->NumberParameters>=2){
        const ULONG_PTR kind=er->ExceptionInformation[0], target=er->ExceptionInformation[1];
        const char* what=kind==0?"read":kind==1?"write":kind==8?"execute (DEP)":"access";
        const uintptr_t gbase=(uintptr_t)PPC_MEM_BASE;
        n=fit(snprintf(line,sizeof line,"  %s: %s of address %p",code==EXCEPTION_ACCESS_VIOLATION?"access violation":"in-page error",
                       what,(void*)target),sizeof line);
        if(target>=gbase&&target<gbase+0x100000000ull) n+=fit(snprintf(line+n,sizeof line-n," (guest address %08X)",(unsigned)(target-gbase)),sizeof line-n);
        else if(crash_addr::describe(where,sizeof where,target)) n+=fit(snprintf(line+n,sizeof line-n,"%s",where),sizeof line-n);
        if(n>(int)sizeof line-2) n=(int)sizeof line-2;
        line[n++]='\n'; win_crash_out(fd,line,n);
    }
    crash_guest_cpu(fd, win_crash_out);
    crash_addr::host_backtrace(fd,win_crash_out,ex->ContextRecord);
    crash_context::note(fd,win_crash_out);
    crashrec::crash_note(fd,win_crash_out);
    if(fd>=0){win_crash_log_only(fd,"\n--- last log lines ---\n",24); log_ring_write(fd,win_crash_log_only); _close(fd); fprintf(stderr,"[crash] wrote %s\n",path);}
    if(g_ppc_trace) { FILE* f=fopen("trace_dump.txt","w"); if(f){trace_dump(f,3000);fclose(f);} }
    input::stop_rumble_now();
    if (crash_cli_no_dialog()) {
        fflush(stderr);
        _exit(1);
    }
    {
        std::string msg = "The game crashed.\n\nA crash log was written to:\n";
        msg += path;
        msg += "\n\nReport crash opens GitHub and the logs folder; describe what happened yourself.";
        user_error::show_fatal(msg, user_error::IssueKind::InGame);
    }
    return EXCEPTION_EXECUTE_HANDLER;
}
static void install_crash_handler() {
    init_crash_log_dir();
    SetUnhandledExceptionFilter(crash_handler);
    crash_addr::prime();
}
#endif
static void init_data_imports() {
    uint32_t alloc = 0, alloc_ex = 0, free_ = 0;
    for (unsigned i = 0; i < g_recomp_import_count; i++) {
        const RecompImport& im = g_recomp_imports[i];
        if (im.is_func) continue;
        std::string n = im.name;
        if (n == "MEMAllocFromDefaultHeap") alloc = im.addr;
        else if (n == "MEMAllocFromDefaultHeapEx") alloc_ex = im.addr;
        else if (n == "MEMFreeToDefaultHeap") free_ = im.addr;
        else if (n == "__gh_FOPEN_MAX") st32(im.addr, 20);
        else if (n == "environ") st32(im.addr, im.addr + 0x10);
    }
    mem_init_data_imports(alloc, alloc_ex, free_);
}

static void apply_portable_mode() {
    if (!host::portable()) return;
    const std::string u = host::portable_user_dir();
    std::error_code ec;
    std::filesystem::create_directories(u, ec);
    auto set = [](const env::Var& k, const std::string& v) {
#ifdef _WIN32
        if (getenv(k.name())) return;
        _putenv_s(k.name(), v.c_str());
#else
        setenv(k.name(), v.c_str(), 0);
#endif
    };
    set(env::STATE_DIR, u + "/states");
#ifdef __APPLE__
    set(env::DISPLAY_SETTINGS, u + "/display.plist");
    set(env::SHADER_CACHE, u + "/shaders.bin");
#endif
}

static void default_vulkan_cpu_paths() {
    for (const env::Var& var : env::vulkan_cpu_paths) {
        if (var()) continue;
#ifdef _WIN32
        _putenv_s(var.name(), "1");
#else
        setenv(var.name(), "1", 0);
#endif
    }
}

static int g_log_fd = -1;
static size_t g_log_bytes = 0;
static constexpr size_t kLogFileMax = 64u << 20;
static void log_file_raw(int fd, const char* s, size_t n) {
#ifdef _WIN32
    if (fd >= 0) _write(fd, s, (unsigned)n);
#else
    if (fd >= 0 && write(fd, s, n) < 0) {}
#endif
}
static void log_file_line(int fd, const char* s, size_t n) {
    crash_context::redact(fd, {s, n}, log_file_raw);
    if (n == 0 || s[n - 1] != '\n') log_file_raw(fd, "\n", 1);
}
static void log_file_sink(const char* s, size_t n) {
    if (g_log_fd < 0 || g_log_bytes > kLogFileMax) return;
    log_file_line(g_log_fd, s, n);
    g_log_bytes += n + 1;
    if (g_log_bytes > kLogFileMax) {
        static const char note[] = "[log] the log file reached 64 MiB; later lines go to the console only\n";
        log_file_raw(g_log_fd, note, sizeof note - 1);
    }
}
static void start_log_file() {
    const char* e = env::LOG_FILE();
    if (e && !strcmp(e, "0")) return;
    host::ensure_log_dir();
    const std::string dir = host::log_dir();
    std::string path = e && *e ? e : dir + "/nsmbu.log";
    std::error_code ec;
    if (!(e && *e)) {
        std::filesystem::remove(dir + "/nsmbu-previous.log", ec);
        std::filesystem::rename(path, dir + "/nsmbu-previous.log", ec);
    }
#ifdef _WIN32
    g_log_fd = _open(path.c_str(), _O_WRONLY | _O_CREAT | _O_TRUNC | _O_BINARY, _S_IREAD | _S_IWRITE);
#else
    g_log_fd = open(path.c_str(), O_WRONLY | O_CREAT | O_TRUNC | O_CLOEXEC, 0644);
#endif
    if (g_log_fd < 0) { LOG("[log] cannot write %s", path.c_str()); return; }
    log_ring_write(g_log_fd, log_file_line);
    log_set_sink(log_file_sink);
    LOG("[log] writing %s", path.c_str());
}

static void print_help(const char* argv0) {
    const char* name = argv0 && *argv0 ? argv0 : "nsmbu";
    if (const char* slash = strrchr(name, '/')) name = slash + 1;
#ifdef _WIN32
    if (const char* slash = strrchr(name, '\\')) name = slash + 1;
#endif
    printf("Usage: %s [options]\n"
           "\n"
           "Options:\n"
           "  --game DIR            Game dump directory\n"
           "  --save DIR            Save data directory\n"
           "  --trace               Log HLE calls\n"
           "  --renderer API        metal or vulkan (also --renderer=API)\n"
           "  --open W-C            Boot into a course pack with the native intro skip (e.g. 1-1)\n"
#ifdef NSMBU_HAS_METAL
           "  --warm-shaders        Fill the Metal shader cache and exit\n"
#endif
#ifdef NSMBU_HAS_VULKAN
           "  --renderer-smoke      Run the Vulkan smoke test and exit\n"
#endif
           "  -h, --help            Show this help\n",
           name);
}

static bool valid_open_pack(const char* s) {
    const char* dash = strchr(s, '-');
    if (!dash || dash == s || dash[-1] < '1' || dash[-1] > '9' || dash != s + 1) return false;
    const char* p = dash + 1;
    if (*p == '0' || !*p) return false;
    long course = 0;
    for (; *p; ++p) {
        if (*p < '0' || *p > '9') return false;
        course = course * 10 + (*p - '0');
        if (course > 256) return false;
    }
    return course >= 1;
}

#if defined(__ANDROID__) || TARGET_OS_IPHONE
// env.txt in the working folder: NAME=value lines for the NSMBU_* settings a phone has no shell for
static void apply_env_txt() {
    if (FILE* f = fopen("env.txt", "r")) {
        char line[512];
        while (fgets(line, sizeof line, f)) {
            line[strcspn(line, "\r\n")] = 0;
            char* eq = strchr(line, '=');
            if (line[0] == '#' || !eq || eq == line) continue;
            *eq = 0;
            setenv(line, eq + 1, 1);
            LOG("[boot] env.txt: %s=%s", line, eq + 1);
        }
        fclose(f);
    }
}
#endif

int main(int argc, char** argv) {
    if (const char* warm = env::SHADER_WARM())
        if (*warm && std::strcmp(warm, "0")) std::setvbuf(stderr, nullptr, _IONBF, 0);
    apply_portable_mode();
    default_vulkan_cpu_paths();
#ifdef _WIN32

    timeBeginPeriod(1);
#endif
#ifdef __ANDROID__

    if (const char* dir = SDL_GetAndroidExternalStoragePath()) {
        if (chdir(dir) != 0) fprintf(stderr, "cannot enter %s\n", dir);
        setenv("XDG_CONFIG_HOME", (std::string(dir) + "/config").c_str(), 1);

        setenv(env::VK_DRAW_BATCH.name(), "2048", 0);

        if (!env::INTERP()) interp::set_mode(1);

        apply_env_txt();
    }
#endif
#if TARGET_OS_IPHONE
    // the app's Documents folder, which the Files app and Finder show: game/, nsmbu/save/, env.txt, logs
    if (const char* home = getenv("HOME")) {
        const std::string docs = std::string(home) + "/Documents";
        if (chdir(docs.c_str()) != 0) fprintf(stderr, "cannot enter %s\n", docs.c_str());
        apply_env_txt();
    }
#endif
    bool warm_shaders = false;
#ifdef NSMBU_HAS_VULKAN
    bool renderer_smoke = false;
#endif
    for (int i = 1; i < argc; i++) {
        if (!strcmp(argv[i], "--help") || !strcmp(argv[i], "-h")) {
            print_help(argv[0]);
            return 0;
        }
        else if (!strcmp(argv[i], "--game") && i + 1 < argc) config::game_dir = argv[++i];
        else if (!strcmp(argv[i], "--save") && i + 1 < argc) config::save_dir = argv[++i];
        else if (!strcmp(argv[i], "--trace")) g_trace_hle = true;
        else if (!strcmp(argv[i], "--warm-shaders")) warm_shaders = true;
        else if (!strcmp(argv[i], "--open") && i + 1 < argc) {
            const char* pack = argv[++i];
            if (!valid_open_pack(pack)) {
                fprintf(stderr, "--open expects a course pack like 1-1\n");
                print_help(argv[0]);
                return 1;
            }
            mods::packages::set_open_pack(pack);
        }
        else if (!strncmp(argv[i], "--open=", 7)) {
            const char* pack = argv[i] + 7;
            if (!valid_open_pack(pack)) {
                fprintf(stderr, "--open expects a course pack like 1-1\n");
                print_help(argv[0]);
                return 1;
            }
            mods::packages::set_open_pack(pack);
        }
#ifdef NSMBU_HAS_VULKAN
        else if (!strcmp(argv[i], "--renderer-smoke")) renderer_smoke = true;
#endif
    }
    crash_context::initialize();
    install_crash_handler();
    start_log_file();

    LOG("[boot] %s %s (%s), %s", app_title::kName, build::version(), build::commit(), reporthdr::os_description().c_str());
    LOG("[boot] game version %s (%s, title %s, title version %d%s)", g_guest_build_label, g_guest_build_name,
        g_guest_build_title_id, g_guest_build_title_version, g_guest_build_mods ? "" : ", mods unsupported");

    if (env::TEST_HOST_CRASH()) {
        LOG("[boot] NSMBU_TEST_HOST_CRASH: crashing on purpose in the C library");
        size_t (*volatile len)(const char*) = strlen;
#ifndef _WIN32

        if (void* f = dlsym(RTLD_DEFAULT, "strlen")) len = (size_t (*)(const char*))f;
#endif
        LOG("%zu", len((const char*)(uintptr_t)16));
    }

    render::choose(argc, argv);
#ifdef NSMBU_HAS_VULKAN
    if(renderer_smoke) {

        int result = 1;
        host::with_autorelease_pool([&] {
            render::g_backend = &render::vulkan_backend();
            try {
                render::g_backend->init();
            } catch (const std::exception& e) {
                fprintf(stderr, "[renderer smoke] FAIL: Vulkan could not start: %s\n", e.what());
                return;
            }
            result = gfxvk::renderer_smoke_test();
        });
        return result;
    }
#endif
    mods::cemu::set_vulkan(render::requested()==render::Api::Vulkan);
    mods::content::set_game_root(config::game_dir);
    mods::packages::initialize();
#if TARGET_OS_IPHONE
    if (const std::string why = game_folder::unusable_reason(config::game_dir); !why.empty())
        game_folder::fail(why + "\n\nCopy the extracted game (the folder with code, content and meta) into this app's "
                                "Documents as a folder named game: in the Files app under On My iPhone/iPad > NSMBU, or "
                                "with Finder (your device > Files > NSMBU). Then open the app again.");
#else
    if (const std::string why = game_folder::unusable_reason(config::game_dir); !why.empty()) game_folder::fail(why);
#endif
    mem::init();

    LoadedModule m{};
    std::string rpx = config::rpx_path();
    if (!load_rpx(rpx, m)) fatal("cannot load %s", rpx.c_str());
    if (m.entry != g_recomp_entry_point) fatal("%s does not match the recompiled code", rpx.c_str());
    LOG("[boot] loaded %s (%s / %s, title %s version %d): entry %08X sda %08X sda2 %08X data end %08X",
        rpx.c_str(), g_guest_build_label, g_guest_build_name, g_guest_build_title_id, g_guest_build_title_version,
        m.entry, m.sda_base, m.sda2_base, m.data_end);
    {
        std::string common = config::save_dir + "/common/rp_savedata.dat";
        std::string personal = config::save_dir + "/user/rp_personal_savedata.dat";
        struct stat st{};
        if (stat(common.c_str(), &st) == 0)
            LOG("[boot] save dir %s (rp_savedata.dat %lld bytes)", config::save_dir.c_str(), (long long)st.st_size);
        else
            LOG("[boot] save dir %s (no rp_savedata.dat yet)", config::save_dir.c_str());
        if (stat(personal.c_str(), &st) == 0)
            LOG("[boot] rp_personal_savedata.dat %lld bytes", (long long)st.st_size);
    }

    dispatch::init();
    init_data_imports();
    mem_setup_heaps(m.data_end);
    threads::init(m);

    uint32_t argv_arr = mem::runtime_alloc(16);
    uint32_t arg0 = mem::runtime_alloc(16);
    mem::write_cstr(arg0, config::kRpxName, 16);
    st32(argv_arr, arg0);

    render::init();
    agent_ctl::init();
    mods::cemu::set_vulkan(render::active()==render::Api::Vulkan);
    if (warm_shaders) {

#ifdef NSMBU_HAS_METAL
        if (render::active() == render::Api::Metal) return gfx_headstart_warm();
#endif
        fprintf(stderr, "--warm-shaders fills the Metal shader cache; the %s renderer compiles shaders on first use.\n",
                render::api_name(render::active()));
        return 1;
    }
    std::thread([m, argv_arr] {
        threads::run_main(m, 1, argv_arr);
        LOG("[boot] game main thread returned");
        std::exit(0);
    }).detach();
    render::run_main_loop();
    return 0;
}
