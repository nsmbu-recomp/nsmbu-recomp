
#pragma once
#include <chrono>
#include <cstdint>
#include <cstdio>
#include <string>
#include <vector>

#include "ppc.h"

namespace mem {
constexpr uint32_t kMem2Start = 0x10000000;
constexpr uint32_t kMem2End = 0x50000000;
constexpr uint32_t kRuntimeStart = 0x60000000;
constexpr uint32_t kRuntimeEnd = 0x70000000;
constexpr uint32_t kHostStart = 0x68000000;
constexpr uint32_t kFixedStart = 0x6FFF0000;
constexpr uint32_t kFixedSize = 0x10000;
constexpr uint32_t kFgBucket = 0xE0000000;
constexpr uint32_t kFgBucketSize = 0x02800000;
constexpr uint32_t kMem1 = 0xF4000000;
constexpr uint32_t kMem1Size = 0x02000000;
constexpr uint32_t kHleFuncBase = 0xC2000000;

void init();
void reset_allocators();

uint32_t runtime_alloc(uint32_t size, uint32_t align = 16);
uint32_t host_alloc(uint32_t size, uint32_t align = 16);

enum FixedSlot : uint32_t { kFixInterpEye = 0, kFixInterpMtx, kFixCount };
inline uint32_t fixed_slot(FixedSlot id) { return kFixedStart + 0x100 * id; }
uint32_t runtime_top();
void raise_runtime_top(uint32_t top);
struct AllocRec { uint32_t addr, size; uint64_t tag; };
std::vector<AllocRec> runtime_alloc_log();
inline uint8_t* ptr(uint32_t ea) { return PPC_MEM_BASE + ea; }
inline uint32_t guest(const void* p) { return (uint32_t)((const uint8_t*)p - PPC_MEM_BASE); }
std::string read_cstr(uint32_t ea);
void write_cstr(uint32_t ea, const std::string& s, uint32_t max);
}

struct LoadedModule {
    uint32_t entry;
    uint32_t sda_base;
    uint32_t sda2_base;
    uint32_t stack_size;
    uint32_t data_end;
};
bool load_rpx(const std::string& path, LoadedModule& out);

namespace dispatch {
void init();

uint32_t register_host(PpcFunc fn, const char* name);
void set(uint32_t addr, PpcFunc fn);
PpcFunc lookup(uint32_t addr);
}

uint32_t guest_call(Cpu* c, uint32_t fn, std::initializer_list<uint32_t> args = {});

namespace threads {
void block_begin();
void block_end();
bool ensure_core();
void release_core();
void set_service_core(uint32_t core);
void report_sched();
int draw_lock_waiters();
int64_t draw_lock_wait_ns();
}
struct BlockingScope {
    BlockingScope() { threads::block_begin(); }
    ~BlockingScope() { threads::block_end(); }
    BlockingScope(const BlockingScope&) = delete;
};

namespace threads {
void init(const LoadedModule& m);
void run_main(const LoadedModule& m, int argc, uint32_t argv);
Cpu* current();
uint32_t current_thread();

Cpu* make_service_cpu(const char* name, uint32_t stack_size = 0x10000);
void retire_service_cpu();

void service_begin();
void service_end();

void park_sleep_until(std::chrono::steady_clock::time_point t,
                      bool precise = false, void (*before_resume)() = nullptr);

bool quiesce(int timeout_ms, std::string& busy, int entry_mode = 0, int entry_after_ms = 0);
void thaw();
void wait_prefix_parked(const char* prefix, int timeout_ms);
[[noreturn]] void kill_all_for_reboot();
[[noreturn]] void abort_for_reboot();
void reset_for_reboot();
bool reboot_killing();
}

namespace timebase {
constexpr uint64_t kTicksPerSec = 62156250ull;
uint64_t now();

uint64_t guest_now();
uint64_t to_guest(uint64_t host_ticks);
uint64_t to_host(uint64_t guest_ticks);
void set_guest_now(uint64_t guest_ticks);
}

struct HleReg {
    const char* lib;
    const char* name;
    PpcFunc fn;
    HleReg(const char* l, const char* n, PpcFunc f);
};
PpcFunc hle_find(const char* lib, const char* name);
PpcFunc hle_find_any(const char* name);

#define HLE(lib, name)                                                        \
    extern "C" void imp_##lib##_##name(Cpu* c);                                \
    static HleReg hle_reg_##lib##_##name(#lib, #name, imp_##lib##_##name);   \
    extern "C" void imp_##lib##_##name(Cpu* c)

inline uint32_t arg(Cpu* c, int i) { return c->r[3 + i]; }
inline uint64_t arg64(Cpu* c, int reg) { return ((uint64_t)c->r[reg] << 32) | c->r[reg + 1]; }
inline void ret(Cpu* c, uint32_t v) { c->r[3] = v; }
inline void ret64(Cpu* c, uint64_t v) { c->r[3] = (uint32_t)(v >> 32); c->r[4] = (uint32_t)v; }

extern bool g_trace_hle;
void log_msg(const char* fmt, ...) __attribute__((format(printf, 1, 2)));
[[noreturn]] void fatal(const char* fmt, ...) __attribute__((format(printf, 1, 2)));
#define LOG(...) log_msg(__VA_ARGS__)

void log_set_sink(void (*sink)(const char*, size_t));
void log_ring_write(int fd, void (*out)(int, const char*, size_t));
void log_ring_copy(std::vector<std::string>& out);
#define TRACE(...) do { if (g_trace_hle) log_msg(__VA_ARGS__); } while (0)

namespace config {
extern std::string game_dir;
extern std::string save_dir;
inline constexpr const char kRpxName[] = "red-pro2.rpx";
inline std::string rpx_path() { return game_dir + "/code/" + kRpxName; }
}
