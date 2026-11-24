

#include "crash/crash_addr.h"

#include <cstdio>
#include <cstring>

#ifdef _WIN32
#include <windows.h>
#else
#include <dlfcn.h>
#include <execinfo.h>
#include <signal.h>
#include <sys/ucontext.h>
#endif

namespace crash_addr {

#ifdef _WIN32
#define CRASH_ADDR_FMT "%016llX"
#else
#define CRASH_ADDR_FMT "0x%llx"
#endif

static const char* base_name(const char* p) {
    const char* b = p;
    for (const char* s = p; *s; s++)
        if (*s == '/' || *s == '\\') b = s + 1;
    return b;
}

int describe(char* buf, size_t cap, uintptr_t addr, char* path, size_t path_cap) {
    if (!cap) return 0;
    buf[0] = 0;
    if (!addr) return 0;
#ifdef _WIN32
    HMODULE m = nullptr;
    if (!GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
                            (LPCWSTR)addr, &m) || !m)
        return 0;
    wchar_t wide[1024];
    DWORD wn = GetModuleFileNameW(m, wide, 1024);
    char full[1024];
    int fn = wn ? WideCharToMultiByte(CP_UTF8, 0, wide, (int)wn, full, (int)sizeof full - 1, nullptr, nullptr) : 0;
    full[fn > 0 ? fn : 0] = 0;
    if (!full[0]) strcpy(full, "?");
    const uintptr_t base = (uintptr_t)m;
    int n = fit(snprintf(buf, cap, " in %s+0x%llX (base " CRASH_ADDR_FMT ")", base_name(full),
                           (unsigned long long)(addr - base), (unsigned long long)base), cap);
#else
    Dl_info di{};
    if (!dladdr((const void*)addr, &di) || !di.dli_fname) return 0;
    const char* full = di.dli_fname;
    const uintptr_t base = (uintptr_t)di.dli_fbase;
    int n = fit(snprintf(buf, cap, " in %s+0x%llx (base " CRASH_ADDR_FMT ")", base_name(full),
                           (unsigned long long)(addr - base), (unsigned long long)base), cap);

    if (di.dli_sname && di.dli_saddr && addr >= (uintptr_t)di.dli_saddr)
        n += fit(snprintf(buf + n, cap - n, " [%s+0x%llx]", di.dli_sname,
                            (unsigned long long)(addr - (uintptr_t)di.dli_saddr)), cap - n);
#endif
    if (path && path_cap) snprintf(path, path_cap, "%s", full);
    return n;
}

static void frame_line(int fd, Out out, int i, uintptr_t pc) {
    char buf[512];
    int n = fit(snprintf(buf, sizeof buf, "  #%-2d " CRASH_ADDR_FMT, i, (unsigned long long)pc), sizeof buf);
    n += describe(buf + n, sizeof buf - n, pc);
    if ((size_t)n < sizeof buf - 1) buf[n++] = '\n';
    out(fd, buf, n);
}

#ifdef _WIN32
void host_backtrace(int fd, Out out, const void* context) {
    out(fd, "  host backtrace:\n", 18);
#if defined(_M_X64) || defined(__x86_64__) || defined(_M_ARM64) || defined(__aarch64__)
#if defined(_M_ARM64) || defined(__aarch64__)
#define CRASH_PC Pc
#define CRASH_SP Sp
#else
#define CRASH_PC Rip
#define CRASH_SP Rsp
#endif
    if (context) {
        CONTEXT c = *(const CONTEXT*)context;
        const NT_TIB* tib = (const NT_TIB*)NtCurrentTeb();
        const DWORD64 lo = (DWORD64)tib->StackLimit, hi = (DWORD64)tib->StackBase;
        for (int i = 0; i < 48 && c.CRASH_PC; i++) {
            frame_line(fd, out, i, (uintptr_t)c.CRASH_PC);
            const DWORD64 sp = c.CRASH_SP;
            DWORD64 image = 0;
            PRUNTIME_FUNCTION f = RtlLookupFunctionEntry(c.CRASH_PC, &image, nullptr);
            if (f) {
                PVOID handler_data = nullptr;
                DWORD64 frame = 0;
                RtlVirtualUnwind(UNW_FLAG_NHANDLER, image, c.CRASH_PC, f, &c, &handler_data, &frame, nullptr);
            } else {
#if defined(_M_ARM64) || defined(__aarch64__)
                if (i > 0 || c.Lr == c.Pc) break;
                c.Pc = c.Lr;
#else
                if (c.Rsp < lo || c.Rsp + 8 > hi) break;
                c.Rip = *(const DWORD64*)c.Rsp;
                c.Rsp += 8;
#endif
            }
            if (c.CRASH_SP < lo || c.CRASH_SP > hi || c.CRASH_SP < sp) break;
        }
        return;
    }
#undef CRASH_PC
#undef CRASH_SP
#endif

    void* frames[48];
    USHORT nf = RtlCaptureStackBackTrace(0, 48, frames, nullptr);
    for (USHORT i = 0; i < nf; i++) frame_line(fd, out, i, (uintptr_t)frames[i]);
}

void prime() {}
#else
void host_backtrace(int fd, Out out, const void*) {
    out(fd, "  host backtrace:\n", 18);
    void* frames[64];
    int nf = backtrace(frames, 64);
    for (int i = 0; i < nf; i++) frame_line(fd, out, i, (uintptr_t)frames[i]);
}

uintptr_t context_pc(const void* ucontext) {
    if (!ucontext) return 0;
    const ucontext_t* uc = (const ucontext_t*)ucontext;
#if defined(__APPLE__) && (defined(__aarch64__) || defined(__arm64__))
    return (uintptr_t)uc->uc_mcontext->__ss.__pc;
#elif defined(__APPLE__) && defined(__x86_64__)
    return (uintptr_t)uc->uc_mcontext->__ss.__rip;
#elif defined(__linux__) && defined(__x86_64__)
    return (uintptr_t)uc->uc_mcontext.gregs[REG_RIP];
#elif defined(__linux__) && defined(__aarch64__)
    return (uintptr_t)uc->uc_mcontext.pc;
#elif defined(__linux__) && defined(__arm__)
    return (uintptr_t)uc->uc_mcontext.arm_pc;
#elif defined(__linux__) && defined(__i386__)
    return (uintptr_t)uc->uc_mcontext.gregs[REG_EIP];
#else
    (void)uc;
    return 0;
#endif
}

void prime() {
    void* frames[4];
    backtrace(frames, 4);
    Dl_info di;
    dladdr((const void*)&prime, &di);
}
#endif

}
