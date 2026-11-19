#include "crash/crash_context.h"
#include "core/env.h"
#include "crash/crash_redact.h"
#include "gfx/renderer.h"
#include "input/input.h"
#include "hooks/interp.h"
#include "mods/packages.h"
#include <atomic>
#include <cstdlib>
#include <sstream>
#include <string>
#ifdef __APPLE__
#include <crt_externs.h>
#elif !defined(_WIN32)
extern char** environ;
#endif

namespace crash_context {
namespace {
constexpr size_t capacity = 32768;
static_assert(std::atomic<char>::is_always_lock_free);
static_assert(std::atomic<unsigned>::is_always_lock_free);
std::atomic<char> snapshot[capacity];
std::atomic<unsigned> revision{0}, length{0};
Redactor redactor;
void publish(const std::string& text) {
    revision.fetch_add(1, std::memory_order_seq_cst);
    auto size = text.size() < capacity - 1 ? text.size() : capacity - 1;
    for (size_t i = 0; i < size; ++i) snapshot[i].store(text[i], std::memory_order_seq_cst);
    length.store(unsigned(size), std::memory_order_seq_cst);
    revision.fetch_add(1, std::memory_order_seq_cst);
}
void environment(std::ostringstream& out) { out << env::assignments(); }
}
void initialize() {
    const char* home = getenv("HOME");
    if (!home) home = getenv("USERPROFILE");
    const char* user = getenv("USER");
    if (!user) user = getenv("USERNAME");
    redactor.initialize(home, user);
    std::ostringstream out; out << "\n--- crash context (startup) ---\n"; environment(out);
    publish(out.str());
}
void refresh() {
    std::ostringstream out;
    out << "\n--- crash context (latest game-thread snapshot) ---\n";
    out << "renderer=" << render::api_name(render::active()) << "\nresolution=" << (render::g_backend ? render::res_scale() : 1.f)
        << "\nframe_mode=" << interp::mode_name() << "\nframe_target=" << interp::fps()
        << "\ncontroller=" << (input::pro_controller() ? "Pro" : "GamePad") << '\n';
    for (const auto& package : mods::packages::list()) if (package.enabled || package.active)
        out << "package=" << package.id << " version=" << package.version << " enabled=" << package.enabled << " active=" << package.active << '\n';
    environment(out); publish(out.str());
}
void redact(int fd, std::string_view text, Output out) { redactor.write(fd, text, out); }
void note(int fd, Output out) {

    char text[capacity];
    auto before = revision.load(std::memory_order_seq_cst);
    if (before & 1) { out(fd, "\ncrash context: update interrupted\n", 34); return; }
    auto size = length.load(std::memory_order_seq_cst);
    for (unsigned i = 0; i < size; ++i) text[i] = snapshot[i].load(std::memory_order_seq_cst);
    if (before == revision.load(std::memory_order_seq_cst)) out(fd, text, size);
    else out(fd, "\ncrash context: update interrupted\n", 34);
}
}
