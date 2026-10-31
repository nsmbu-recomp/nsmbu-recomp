

#include "save/savestate.h"
#include "guest_addr.h"
#include "save/full_state_header.h"
#include "input/input.h"

#include "core/env.h"
#ifdef __APPLE__
#include <compression.h>
#include <mach-o/ldsyms.h>
#include <mach-o/loader.h>
#else
#include <lz4.h>
#endif
#include "platform/host.h"
#include "gfx/renderer.h"
#include <sys/stat.h>
#ifndef _WIN32
#include <unistd.h>
#endif

#include <algorithm>
#include <atomic>
#include <cstdarg>
#include <cstdlib>
#include <ctime>
#include <map>
#include <memory>
#include <mutex>
#include <filesystem>
#include <thread>
#include <unordered_map>

#include "core/runtime.h"
#include "core/build_info.h"
#include "crash/crashrec.h"
#include "save/portable_state.h"
#include "mods/sdk/nsmbu.h"
#include "save/state_memory.h"
#include "overlay/quit_prompt.h"
#include "input/rumble.h"

bool threads_ss_save(ss::Writer& w, std::string& why);
bool threads_ss_present(ss::Reader r, std::string& why);
bool threads_ss_check(ss::Reader r, std::string& why);
void threads_ss_load(ss::Reader& r);
void threads_ss_targets(ss::Reader r);
void mem_ss_save(ss::Writer& w);
bool mem_ss_check(ss::Reader r, std::string& why);
void mem_ss_load(ss::Reader& r);
void fs_ss_save(ss::Writer& w);
void fs_ss_load(ss::Reader& r);
void ax_ss_save(ss::Writer& w);
bool ax_ss_check(ss::Reader r, std::string& why);
void ax_ss_load(ss::Reader& r);
void gx2_ss_drain();
void gx2_ss_save(ss::Writer& w);
bool gx2_ss_check(ss::Reader r, std::string& why);
void gx2_ss_load(ss::Reader& r);
namespace interp { void ss_reset(); }
namespace aspect { void ss_reset(); }
namespace dispatch { std::vector<std::pair<uint32_t, std::string>> host_functions(); }

namespace interp { uint64_t logic_steps(); }
extern "C" bool nsmbu_apply_map_pose(int32_t world, int32_t section, int32_t node, int32_t course);
namespace ss {
namespace {

constexpr char kMagic[8] = {'N', 'S', 'M', 'B', 'U', 'S', 'A', 'V'};
#ifdef __APPLE__
constexpr uint32_t kVersion = 1;
#else
constexpr uint32_t kVersion = 2;
#endif
constexpr uint32_t kChunk = 0x10000;
constexpr uint32_t kBlock = 8 << 20;

using Header = FullStateHeader;

enum : uint32_t {
    kSecThreads = 'THRD',
    kSecHeaps = 'HEAP',
    kSecFiles = 'FILE',
    kSecAudio = 'AX  ',
    kSecGx2 = 'GX2 ',
    kSecAllocs = 'RALC',
    kSecDispatch = 'DSPT',
    kSecMemory = 'MEM ',
};

using Region=MemoryRegion;

std::vector<Region> regions() {
    uint32_t top = (mem::runtime_top() + kChunk - 1) & ~(kChunk - 1);
    return {{mem::kMem2Start, mem::kMem2End - mem::kMem2Start},
            {mem::kRuntimeStart, top - mem::kRuntimeStart},
            {mem::kFixedStart, mem::kFixedSize},
            {mem::kFgBucket, mem::kFgBucketSize},
            {mem::kMem1, mem::kMem1Size}};
}

struct Snapshot {
    Header h{};
    std::vector<uint8_t> payload;
    std::map<uint32_t, std::pair<size_t, size_t>> sections;
    std::unordered_map<uint32_t, const uint8_t*> chunks;
    std::vector<Region> regs;
    int slot = 0;

    Reader section(uint32_t tag) const {
        auto it = sections.find(tag);
        if (it == sections.end()) return Reader(nullptr, 0);
        return Reader(payload.data() + it->second.first, it->second.second);
    }
    bool parse() {
        Reader r(payload.data(), payload.size());
        while (r.ok && !r.at_end()) {
            uint32_t tag = r.u32();
            uint64_t n = r.u64();
            size_t off = r.p - payload.data();
            if (!r.bytes(nullptr, n)) return false;
            sections[tag] = {off, (size_t)n};
        }
        if (!r.ok || !sections.count(kSecMemory)) return false;
        Reader m=section(kSecMemory);
        return read_regions(m,regs,chunks);
    }
};

std::mutex g_mu;
std::mutex g_io;
std::string g_message;
std::chrono::steady_clock::time_point g_message_time;
std::atomic<int> g_save_req{0};
int g_done_slot = 0;
std::function<void(bool, const std::string&)> g_done;
std::shared_ptr<Snapshot> g_load_ready;
std::atomic<bool> g_loading{false};
int g_attempts = 0;
const Snapshot* g_check = nullptr;

void message(const char* fmt, ...) __attribute__((format(printf, 1, 2)));
void message(const char* fmt, ...) {
    char buf[512];
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(buf, sizeof buf, fmt, ap);
    va_end(ap);
    LOG("[savestate] %s", buf);
    std::lock_guard<std::mutex> lk(g_mu);
    g_message = buf;
    g_message_time = std::chrono::steady_clock::now();
}

std::function<void(bool, const std::string&)> take_done(int slot) {
    std::lock_guard<std::mutex> lk(g_mu);
    if (g_done_slot != slot) return {};
    g_done_slot = 0;
    return std::move(g_done);
}
void report_done(int slot, bool ok, const std::string& why) {
    if (auto d = take_done(slot)) d(ok, why);
}

std::string state_dir() {
    static const std::string dir = [] {
        std::string d;
        if (const char* e = env::STATE_DIR()) d = e;
        else {
#if defined(__APPLE__) && !TARGET_OS_IPHONE
            d=std::string(getenv("HOME")?getenv("HOME"):".")+"/Library/Application Support/nsmbu/states";
#else
            d=host::config_dir()+"/states";
#endif
        }
        std::error_code ec; std::filesystem::create_directories(d,ec);
        return d;
    }();
    return dir;
}
bool is_auto(int slot) { return slot > crashrec::kAutoBase && slot <= crashrec::kAutoBase + crashrec::kAutoSlots; }
std::string slot_label(int slot) {
    return is_auto(slot) ? "Automatic state " + std::to_string(slot - crashrec::kAutoBase) : "Slot " + std::to_string(slot);
}
std::string slot_path(int slot, const char* ext = "bin") {
    if (is_auto(slot)) return state_dir() + "/auto/auto" + std::to_string(slot - crashrec::kAutoBase) + "." + ext;
    return state_dir() + "/slot" + std::to_string(slot) + "." + ext;
}

void build_uuid(uint8_t out[16]) {
    static uint8_t id[16]{};
    static bool ready = false;
    if (!ready) {
        ready = true;
#ifdef __APPLE__
        const auto* h = (const mach_header_64*)&_mh_execute_header;
        const uint8_t* p = (const uint8_t*)(h + 1);
        for (uint32_t i = 0; i < h->ncmds; i++) {
            const auto* lc = (const load_command*)p;
            if (lc->cmd == LC_UUID) {
                memcpy(id, ((const uuid_command*)lc)->uuid, 16);
                break;
            }
            p += lc->cmdsize;
        }
#else
        uint64_t first = 0xcbf29ce484222325ull, second = 0x84222325cbf29ce4ull;
        FILE* f = nullptr;
#if !defined(_WIN32)
        f = fopen("/proc/self/exe", "rb");
#endif
        if (!f) f = fopen(host::executable_path().c_str(), "rb");
        if (!f) {
            LOG("[savestate] cannot identify executable; state compatibility is unavailable");
        } else {
            unsigned char buf[65536];
            size_t n;
            while ((n = fread(buf, 1, sizeof buf, f)))
                for (size_t i = 0; i < n; i++) {
                    first = (first ^ buf[i]) * 0x100000001b3ull;
                    second = (second + buf[i]) * 0x100000001b3ull;
                }
            fclose(f);
            memcpy(id, &first, 8);
            memcpy(id + 8, &second, 8);
        }
#endif
    }
    memcpy(out, id, 16);
}

uint64_t game_id() {
    static const uint64_t id = [] {
        uint64_t h = 0xcbf29ce484222325ull;
        FILE* f = fopen(config::rpx_path().c_str(), "rb");
        if (!f) return h;
        std::vector<uint8_t> buf(1 << 20);
        size_t n;
        while ((n = fread(buf.data(), 1, buf.size(), f)) > 0)
            for (size_t i = 0; i < n; i++) h = (h ^ buf[i]) * 0x100000001b3ull;
        fclose(f);
        return h;
    }();
    return id;
}

const uint32_t kStageName = GD(0x1046F0B0) + 0x5134;
std::string stage_name() {
    if (const char* e = env::STATE_STAGE_ADDR()) {
        uint32_t a = (uint32_t)strtoul(e, nullptr, 16);
        return std::string((const char*)mem::ptr(a), strnlen((const char*)mem::ptr(a), 8));
    }
    const char* p = (const char*)mem::ptr(kStageName);
    size_t n = strnlen(p, 8);
    for (size_t i = 0; i < n; i++)
        if (p[i] < 0x20 || p[i] > 0x7E) return "";
    return std::string(p, n);
}

void capture_memory(Writer& w) {
    capture_regions(w,regions(),[](uint32_t a){return mem::ptr(a);},
                    [](const uint8_t* p,size_t n){return host::memory_touched(const_cast<uint8_t*>(p),n);});
}

void restore_memory(const Snapshot& s) {
    restore_regions(s.regs,s.chunks,[](uint32_t a){return mem::ptr(a);},
                    [](const uint8_t* p,size_t n){return host::memory_touched(const_cast<uint8_t*>(p),n);});
}

bool write_slot(int slot, const Header& h0, const std::vector<uint8_t>& payload) {
    Header h = h0;
    size_t nblocks = (payload.size() + kBlock - 1) / kBlock;
    h.blocks = (uint32_t)nblocks;
    h.raw_size = payload.size();
    std::vector<std::vector<uint8_t>> out(nblocks);
    std::vector<std::thread> pool;
    std::atomic<size_t> next{0};
    unsigned nt = std::max(1u, std::min(8u, std::thread::hardware_concurrency()));
    for (unsigned t = 0; t < nt; t++)
        pool.emplace_back([&] {
#ifdef __APPLE__
            std::vector<uint8_t> scratch(compression_encode_scratch_buffer_size(COMPRESSION_LZ4));
#endif
            for (size_t i; (i = next++) < nblocks;) {
                size_t raw = std::min<size_t>(kBlock, payload.size() - i * kBlock);
                std::vector<uint8_t>& o = out[i];
                o.resize(8 + raw + raw / 8 + 1024);
#ifdef __APPLE__
                size_t n = compression_encode_buffer(o.data() + 8, o.size() - 8, payload.data() + i * kBlock, raw, scratch.data(), COMPRESSION_LZ4);
#else
                size_t n = (size_t)LZ4_compress_default((const char*)payload.data()+i*kBlock,(char*)o.data()+8,(int)raw,(int)o.size()-8);
#endif
                uint32_t hdr[2] = {(uint32_t)raw, (uint32_t)n};
                if (!n || n >= raw) {
                    memcpy(o.data() + 8, payload.data() + i * kBlock, raw);
                    hdr[1] = 0;
                    n = raw;
                }
                memcpy(o.data(), hdr, 8);
                o.resize(8 + n);
            }
        });
    for (auto& t : pool) t.join();
    std::string tmp = slot_path(slot, "tmp");
    FILE* f = fopen(tmp.c_str(), "wb");
    if (!f) return false;
    bool ok = fwrite(&h, sizeof h, 1, f) == 1;
    for (auto& o : out) ok = ok && fwrite(o.data(), 1, o.size(), f) == o.size();
    ok = fclose(f) == 0 && ok;
    if (ok) ok = host::replace_file(tmp,slot_path(slot));
    else remove(tmp.c_str());
    return ok;
}

bool read_header(FILE* f, Header& h, std::string& why) {
    if (!read_full_state_header(f, h, why)) return false;
    if (h.version != kVersion) { why = "saved by another version"; return false; }
    if (h.cpu_size != sizeof(Cpu)) { why = "saved by an incompatible build"; return false; }
    if (h.game_id != game_id()) { why = "saved with a different game executable"; return false; }
    return true;
}

std::shared_ptr<Snapshot> read_slot(int slot, std::string& why) {
    FILE* f = fopen(slot_path(slot).c_str(), "rb");
    if (!f) { why = "empty"; return nullptr; }
    auto s = std::make_shared<Snapshot>();
    s->slot = slot;
    if (!read_header(f, s->h, why)) { fclose(f); return nullptr; }
    std::vector<std::vector<uint8_t>> blocks(s->h.blocks);
    std::vector<uint32_t> raws(s->h.blocks), comps(s->h.blocks);
    bool ok = true;
    for (uint32_t i = 0; i < s->h.blocks && ok; i++) {
        uint32_t hdr[2];
        ok = fread(hdr, 8, 1, f) == 1 && hdr[0] <= kBlock;
        if (!ok) break;
        raws[i] = hdr[0];
        comps[i] = hdr[1];
        blocks[i].resize(hdr[1] ? hdr[1] : hdr[0]);
        ok = fread(blocks[i].data(), 1, blocks[i].size(), f) == blocks[i].size();
    }
    fclose(f);
    if (!ok) { why = "file is truncated"; return nullptr; }
    s->payload.resize(s->h.raw_size);
    std::atomic<size_t> next{0};
    std::atomic<bool> bad{false};
    std::vector<std::thread> pool;
    unsigned nt = std::max(1u, std::min(8u, std::thread::hardware_concurrency()));
    for (unsigned t = 0; t < nt; t++)
        pool.emplace_back([&] {
#ifdef __APPLE__
            std::vector<uint8_t> scratch(compression_decode_scratch_buffer_size(COMPRESSION_LZ4));
#endif
            for (size_t i; (i = next++) < blocks.size();) {
                uint8_t* dst = s->payload.data() + i * (size_t)kBlock;
                if ((size_t)i * kBlock + raws[i] > s->payload.size()) { bad = true; continue; }
                if (!comps[i]) { memcpy(dst, blocks[i].data(), raws[i]); continue; }
#ifdef __APPLE__
                size_t n = compression_decode_buffer(dst, raws[i], blocks[i].data(), comps[i], scratch.data(), COMPRESSION_LZ4);
#else
                int decoded = LZ4_decompress_safe((const char*)blocks[i].data(),(char*)dst,(int)comps[i],(int)raws[i]);
                size_t n = decoded<0?0:(size_t)decoded;
#endif
                if (n != raws[i]) bad = true;
            }
        });
    for (auto& t : pool) t.join();
    if (bad || !s->parse()) { why = "file is corrupt"; return nullptr; }
    return s;
}

void put_section(Writer& w, uint32_t tag, const Writer& sec) {
    w.u32(tag);
    w.u64(sec.b.size());
    w.bytes(sec.b.data(), sec.b.size());
}

void save_allocs(Writer& w) {
    auto log = mem::runtime_alloc_log();
    w.u32(mem::runtime_top());
    w.u32((uint32_t)log.size());
    for (auto& a : log) w.pod(a);
}

bool check_allocs(Reader r, std::string& why) {
    uint32_t top = r.u32();
    (void)top;
    uint32_t n = r.u32();
    auto cur = mem::runtime_alloc_log();
    for (uint32_t i = 0; i < n && r.ok; i++) {
        mem::AllocRec a = r.pod<mem::AllocRec>();
        if (i >= cur.size()) break;
        const mem::AllocRec& b = cur[i];

        if (a.tag != b.tag && i < 4) LOG("[savestate] runtime object #%u allocated by different code (rebuilt?)", i);
        if (a.addr != b.addr || a.size != b.size) {
            char buf[160];
            snprintf(buf, sizeof buf, "runtime objects are laid out differently (#%u: %08X+%X vs %08X+%X)", i, a.addr, a.size, b.addr,
                     b.size);
            why = buf;
            return false;
        }
    }
    return r.ok;
}

void save_dispatch(Writer& w) {
    auto v = dispatch::host_functions();
    w.u32((uint32_t)v.size());
    for (auto& [a, n] : v) {
        w.u32(a);
        w.str(n);
    }
}

bool check_dispatch(Reader r, std::string& why) {
    std::unordered_map<uint32_t, std::string> cur;
    for (auto& [a, n] : dispatch::host_functions()) cur[a] = n;
    uint32_t n = r.u32();
    for (uint32_t i = 0; i < n && r.ok; i++) {
        uint32_t a = r.u32();
        std::string name = r.str();
        auto it = cur.find(a);
        if (it == cur.end() || it->second != name) {
            why = "host function " + name + " is not registered at the same address";
            return false;
        }
    }
    return r.ok;
}

bool do_save(int slot) {
    std::string busy, why;
    auto t0 = std::chrono::steady_clock::now();
    if (!threads::quiesce(250, busy, 1, 30)) {
        threads::thaw();
        if (++g_attempts < 30) return false;
        if (!is_auto(slot)) message("Slot %d: not saved (game busy:%s)", slot, busy.c_str());
        report_done(slot, false, "game busy:" + busy);
        return true;
    }
    gx2_ss_drain();
    Writer threads_w;
    if (!threads_ss_save(threads_w, why)) {
        threads::thaw();
        if (++g_attempts < 30) return false;
        if (!is_auto(slot)) message("Slot %d: not saved (%s)", slot, why.c_str());
        report_done(slot, false, why);
        return true;
    }
    auto payload = std::make_shared<Writer>();
    payload->b.reserve(512u << 20);
    put_section(*payload, kSecThreads, threads_w);
    Writer w;
    mem_ss_save(w);
    put_section(*payload, kSecHeaps, w);
    w = Writer();
    fs_ss_save(w);
    put_section(*payload, kSecFiles, w);
    w = Writer();
    ax_ss_save(w);
    put_section(*payload, kSecAudio, w);
    w = Writer();
    gx2_ss_save(w);
    put_section(*payload, kSecGx2, w);
    w = Writer();
    save_allocs(w);
    put_section(*payload, kSecAllocs, w);
    w = Writer();
    save_dispatch(w);
    put_section(*payload, kSecDispatch, w);

    payload->u32(kSecMemory);
    size_t len_at = payload->b.size();
    payload->u64(0);
    capture_memory(*payload);
    uint64_t mlen = payload->b.size() - len_at - 8;
    memcpy(&payload->b[len_at], &mlen, 8);
    Header h{};
    memcpy(h.magic, kMagic, 8);
    h.version = kVersion;
    h.header_size = sizeof(Header);
    h.created = (uint64_t)time(nullptr);
    std::string stage = stage_name();
    snprintf(h.area, sizeof h.area, "%s", stage.c_str());
    build_uuid(h.build);
    h.game_id = game_id();
    h.cpu_size = sizeof(Cpu);
    h.controller = input::pro_controller() ? 2 : 1;
    threads::thaw();
    double ms = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - t0).count();
    LOG("[savestate] slot %d: captured %.1f MB in %.1f ms (stage %s)", slot, payload->b.size() / 1048576.0, ms, stage.c_str());
    if (!is_auto(slot)) render::request_tv_dump(slot_path(slot, "png"), 0);
    else crashrec::on_auto_saved(slot - crashrec::kAutoBase);

    std::thread([slot, h, payload, stage, done = take_done(slot)] {
        auto t1 = std::chrono::steady_clock::now();
        bool ok;
        {
            std::lock_guard<std::mutex> io(g_io);
            ok = write_slot(slot, h, payload->b);
        }
        double ms = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - t1).count();
        struct stat st{};
        stat(slot_path(slot).c_str(), &st);
        LOG("[savestate] slot %d: %s (%.1f MB on disk, %.0f ms)", slot, ok ? "written" : "WRITE FAILED", st.st_size / 1048576.0, ms);
        if (done) done(ok, ok ? "" : "write failed");
        if (is_auto(slot)) return;
        std::lock_guard<std::mutex> lk(g_mu);
        g_message = ok ? "Saved to slot " + std::to_string(slot) + (stage.empty() ? "" : " (" + stage + ")")
                       : "Slot " + std::to_string(slot) + ": write failed";
        g_message_time = std::chrono::steady_clock::now();
    }).detach();
    return true;
}

bool do_load(const std::shared_ptr<Snapshot>& s) {
    std::string busy, why;
    auto t0 = std::chrono::steady_clock::now();
    static const int wait_for_threads = [] {
        const char* timed = env::STATE_LOAD_AT();
        return timed && *timed ? 2400 : 30;
    }();
    if (!threads_ss_present(s->section(kSecThreads), why)) {
        if (++g_attempts < wait_for_threads) {
            if (g_attempts == 1) LOG("[savestate] slot %d: waiting for the game threads (%s)", s->slot, why.c_str());
            return false;
        }
        message("%s: cannot load here (%s)", slot_label(s->slot).c_str(), why.c_str());
        return true;
    }
    threads_ss_targets(s->section(kSecThreads));
    if (!threads::quiesce(250, busy, 2, 0)) {
        threads::thaw();
        if (++g_attempts < 30) return false;
        message("%s: not loaded (game busy:%s)", slot_label(s->slot).c_str(), busy.c_str());
        return true;
    }
    g_check = s.get();
    bool ok = check_allocs(s->section(kSecAllocs), why) && check_dispatch(s->section(kSecDispatch), why) &&
              mem_ss_check(s->section(kSecHeaps), why) && ax_ss_check(s->section(kSecAudio), why);
    bool layout_ok = ok;
    ok = ok && threads_ss_check(s->section(kSecThreads), why);
    g_check = nullptr;
    if (!ok) {
        threads::thaw();
        if (layout_ok && ++g_attempts < wait_for_threads) {
            if (g_attempts == 1) LOG("[savestate] slot %d: waiting for the game threads (%s)", s->slot, why.c_str());
            return false;
        }
        message("%s: cannot load here (%s)", slot_label(s->slot).c_str(), why.c_str());
        return true;
    }
    gx2_ss_drain();
    g_check = s.get();
    ok = gx2_ss_check(s->section(kSecGx2), why);
    g_check = nullptr;
    if (!ok) {
        threads::thaw();
        message("%s: cannot load here (%s)", slot_label(s->slot).c_str(), why.c_str());
        return true;
    }
    restore_memory(*s);
    if (s->h.controller) input::set_pro_controller(restored_pro_controller(s->h.controller, input::pro_controller()));
    Reader r = s->section(kSecAllocs);
    mem::raise_runtime_top(r.u32());
    r = s->section(kSecHeaps);
    mem_ss_load(r);
    r = s->section(kSecFiles);
    fs_ss_load(r);
    r = s->section(kSecAudio);
    ax_ss_load(r);
    r = s->section(kSecGx2);
    gx2_ss_load(r);
    interp::ss_reset();
    aspect::ss_reset();
    rumble::reset();
    r = s->section(kSecThreads);
    threads_ss_load(r);
    threads::thaw();
    double ms = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - t0).count();
    std::string area = s->h.area[0] ? " (" + std::string(s->h.area) + ")" : "";
    message("Loaded %s%s", is_auto(s->slot) ? slot_label(s->slot).c_str() : ("slot " + std::to_string(s->slot)).c_str(), area.c_str());
    LOG("[savestate] slot %d: restored in %.1f ms", s->slot, ms);
    return true;
}

const uint32_t kCourseTask = GD(0x101D15F0);
const uint32_t kCourseSelect = GD(0x101CC96C);
const uint32_t kPlayerMgr = GD(0x101E6994);
const uint32_t kCourseInfo = GD(0x101D18EC);
const uint32_t kMainGame = GD(0x101E7D7C);
const uint32_t kSaveMgr = GD(0x101E7DA4);
const uint32_t kScene = GD(0x101E7B28);
const uint32_t kFaderBusy = GC(0x02983B28);
const uint32_t kCreatorTitle = GC(0x0202B51C);
const uint32_t kCreatorFileSelect = GC(0x0298AD3C);
const uint32_t kCreatorSaveSelect = GC(0x024C11F4);
const uint32_t kCreatorMenu = GC(0x0202B61C);
constexpr uint32_t kPlayerActor = 0x20;
constexpr uint32_t kPosX = 0x6C, kPosY = 0x70, kPosZ = 0x74;
constexpr uint32_t kSaveDataOff = 0x24;
constexpr uint32_t kSceneCreator = 0x58;
constexpr uint32_t kSceneNext = 0x5C;
constexpr uint32_t kSceneFader = 0x68;

bool in_mem2(uint32_t a) { return a >= mem::kMem2Start && a < mem::kMem2End; }

struct CallScope {
    Cpu* c;
    Cpu saved;
    explicit CallScope(Cpu* cpu) : c(cpu), saved(*cpu) {}
    ~CallScope() {
        uint32_t core = c->core;
        void* th = c->thread;
        *c = saved;
        c->core = core;
        c->thread = th;
    }
};

enum class Where { None, Course, Map };
struct Live {
    Where where = Where::None;
    uint32_t actor = 0;
    int world = 0, course = 0, section = 0, node = 0;
};

bool fader_idle(Cpu* c, std::string& why) {
    uint32_t scene = ld32(kScene);
    if (!in_mem2(scene)) { why = "the scene is not ready"; return false; }
    if (ld32(scene + kSceneNext) != 0) { why = "a scene transition is in progress"; return false; }
    uint32_t fader = ld32(scene + kSceneFader);
    if (!in_mem2(fader)) { why = "a scene transition is in progress"; return false; }
    CallScope scope(c);
    if (guest_call(c, kFaderBusy, {fader}) != 0) { why = "a scene transition is in progress"; return false; }
    return true;
}

bool describe_place(std::string& why, bool& retry, Live& live) {
    retry = false;
    live = Live{};
    uint32_t course_task = ld32(kCourseTask);
    uint32_t map = ld32(kCourseSelect);
    if (course_task != 0) {
        uint32_t mgr = ld32(kPlayerMgr);
        uint32_t actor = mgr ? ld32(mgr + kPlayerActor) : 0;
        if (!in_mem2(actor)) {
            why = "the player is not in the course";
            retry = true;
            return false;
        }
        uint32_t info = ld32(kCourseInfo);
        if (!in_mem2(info)) {
            why = "the course is not ready";
            retry = true;
            return false;
        }
        int world = ld8(info + 0x10);
        int course = ld8(info + 0x11);
        if (world > 8 || course > 255) {
            why = "the course is not ready";
            retry = true;
            return false;
        }
        live.where = Where::Course;
        live.actor = actor;
        live.world = world;
        live.course = course;
        return true;
    }
    if (map != 0) {
        uint32_t game = ld32(kMainGame);
        if (!in_mem2(game)) {
            why = "the map place is not ready";
            retry = true;
            return false;
        }
        int world = (int32_t)ld32(game + 0x19C);
        int section = (int32_t)ld32(game + 0x1A0);
        int node = (int32_t)ld32(game + 0x1A4);
        int course = (int32_t)ld32(game + 0x1A8);
        if (world < 0 || world > 8 || section < 0 || section > 1 || node < 0 || node > 255 || course < -1 || course > 255) {
            why = "the map place is not ready";
            retry = true;
            return false;
        }
        live.where = Where::Map;
        live.world = world;
        live.course = course;
        live.section = section;
        live.node = node;
        return true;
    }
    uint32_t scene = ld32(kScene);
    uint32_t creator = in_mem2(scene) ? ld32(scene + kSceneCreator) : 0;
    if (creator == kCreatorTitle) {
        why = "the title screen is showing";
        retry = true;
        return false;
    }
    if (creator == kCreatorFileSelect || creator == kCreatorSaveSelect) {
        why = "file select is showing";
        retry = true;
        return false;
    }
    why = "no game in progress (start or continue a save file first)";
    retry = true;
    return false;
}

bool player_has_control(Cpu* c, std::string& why) {
    if (!fader_idle(c, why)) return false;
    uint32_t scene = ld32(kScene);
    if (in_mem2(scene) && ld32(scene + kSceneCreator) == kCreatorMenu) { why = "a game menu is open"; return false; }
    return true;
}

bool load_waits(const std::string& why) {
    return why.find("leaving course") != std::string::npos || why.find("busy") != std::string::npos || why == "no guest cpu" ||
           why == "the scene is not ready" || why == "a scene transition is in progress" || why == "a game menu is open" ||
           why == "the map place is not ready";
}

void ensure_cpu(Cpu* c) {
    if (mods::lua_sdk::cpu() != c) mods::lua_sdk::set_cpu(c);
}

bool copy_savedata(std::vector<uint8_t>& out, std::string& why) {
    uint32_t mgr = ld32(kSaveMgr);
    if (!in_mem2(mgr)) { why = "save data is not loaded"; return false; }
    uint32_t buf = ld32(mgr + kSaveDataOff);
    if (!in_mem2(buf) || (uint64_t)buf + pstate::kNsmbuSaveDataSize > mem::kMem2End) {
        why = "save data is not loaded";
        return false;
    }
    out.resize(pstate::kNsmbuSaveDataSize);
    memcpy(out.data(), mem::ptr(buf), out.size());
    if (out[7] > 2) { why = "save file is not selected"; return false; }
    return true;
}

bool restore_savedata(const pstate::State& s, std::string& why) {
    if (s.savedata.size() != pstate::kNsmbuSaveDataSize) { why = "save data has the wrong size"; return false; }
    if ((int)s.savedata[7] != s.file_slot) { why = "save file does not match this state"; return false; }
    uint32_t mgr = ld32(kSaveMgr);
    if (!in_mem2(mgr)) { why = "save data is not loaded"; return false; }
    uint32_t buf = ld32(mgr + kSaveDataOff);
    if (!in_mem2(buf) || (uint64_t)buf + s.savedata.size() > mem::kMem2End) {
        why = "save data is not loaded";
        return false;
    }
    memcpy(mem::ptr(buf), s.savedata.data(), s.savedata.size());
    return true;
}

std::string meta_value(const char* key) {
    static const std::string meta = [] {
        std::string s;
        if (FILE* f = fopen((config::game_dir + "/meta/meta.xml").c_str(), "rb")) {
            char buf[4096];
            size_t n;
            while ((n = fread(buf, 1, sizeof buf, f)) > 0 && s.size() < (1u << 20)) s.append(buf, n);
            fclose(f);
        }
        return s;
    }();
    std::string open = std::string("<") + key;
    size_t p = meta.find(open);
    if (p == std::string::npos) return "";
    p = meta.find('>', p);
    size_t e = meta.find('<', p);
    if (p == std::string::npos || e == std::string::npos) return "";
    return meta.substr(p + 1, e - p - 1);
}
std::string title_id() { std::string t = meta_value("title_id"); return t.empty() ? "0005000010101d00" : t; }

std::string hex64(uint64_t v) { char b[20]; snprintf(b, sizeof b, "%016llx", (unsigned long long)v); return b; }

std::string now_text() {
    time_t t = time(nullptr);
    struct tm tmv;
#ifdef _WIN32
    localtime_s(&tmv, &t);
#else
    localtime_r(&t, &tmv);
#endif
    char buf[32];
    strftime(buf, sizeof buf, "%Y-%m-%d %H:%M:%S", &tmv);
    return buf;
}

struct Arrival {
    std::string place;
    float pos[3]{};
    int world = 0, course = 0, section = 0, node = 0;
    uint64_t frame = 0;
} g_arrival;

bool capture_portable(Cpu* c, pstate::State& s, std::string& why) {
    (void)c;
    bool retry = false;
    Live live;
    if (!describe_place(why, retry, live)) return false;
    (void)retry;
    if (!copy_savedata(s.savedata, why)) return false;
    s.file_slot = s.savedata[7];
    s.place = live.where == Where::Course ? "course" : "map";
    s.world = live.world;
    s.course = live.course;
    s.section = live.section;
    s.node = live.node;
    if (live.where == Where::Course) {
        s.pos[0] = (float)ldf32(live.actor + kPosX);
        s.pos[1] = (float)ldf32(live.actor + kPosY);
        s.pos[2] = (float)ldf32(live.actor + kPosZ);
    }
    char label[16];
    if (s.place == "course") snprintf(label, sizeof label, "%d-%d", s.world + 1, s.course + 1);
    else snprintf(label, sizeof label, "W%d n%d", s.world + 1, s.node);
    s.stage = label;
    s.title_id = title_id();
    s.title_version = (uint32_t)strtoul(meta_value("title_version").c_str(), nullptr, 10);
    s.game_hash = hex64(game_id());
    s.runtime = std::string(build::version()) + " (" + build::commit() + ")";
    s.created = now_text();
    s.controller = input::pro_controller() ? 2 : 1;
    s.player_name.clear();
    return true;
}

void arm_arrival(const pstate::State& s) {
    g_arrival = Arrival{};
    g_arrival.place = s.place;
    g_arrival.world = s.world;
    g_arrival.course = s.course;
    g_arrival.section = s.section;
    g_arrival.node = s.node;
    g_arrival.pos[0] = s.pos[0];
    g_arrival.pos[1] = s.pos[1];
    g_arrival.pos[2] = s.pos[2];
    g_arrival.frame = render::frame_count();
}

bool apply_portable(Cpu* c, const pstate::State& s, std::string& why, bool& retry) {
    retry = false;
    if (s.place != "course" && s.place != "map") { why = "this state has no course or map place"; return false; }
    if (s.place == "course" && mods::lua_sdk::story_leave_pending()) {
        if (!restore_savedata(s, why)) return false;
        ensure_cpu(c);
        std::string err;
        if (!mods::lua_sdk::load_story_course(s.world, s.course, err)) {
            why = err;
            retry = load_waits(err);
            return false;
        }
        if (s.controller) input::set_pro_controller(restored_pro_controller(s.controller, input::pro_controller()));
        arm_arrival(s);
        return true;
    }
    Live live;
    if (!describe_place(why, retry, live)) return false;
    if (!player_has_control(c, why)) { retry = true; return false; }
    if (!restore_savedata(s, why)) return false;
    ensure_cpu(c);
    if (s.controller) input::set_pro_controller(restored_pro_controller(s.controller, input::pro_controller()));
    if (s.place == "map") {
        if (!nsmbu_apply_map_pose(s.world, s.section, s.node, s.course)) {
            why = "the map place is not ready";
            retry = true;
            return false;
        }
        std::string err;
        if (!mods::lua_sdk::open_course_select(err)) {
            why = err;
            retry = load_waits(err);
            return false;
        }
    } else {
        std::string err;
        if (!mods::lua_sdk::load_story_course(s.world, s.course, err)) {
            why = err;
            retry = load_waits(err);
            return false;
        }
    }
    arm_arrival(s);
    return true;
}

std::string portable_label(int slot) { return slot > 0 ? "Slot " + std::to_string(slot) : std::string("Portable state"); }

bool read_text(const std::string& path, std::string& out, std::string& why) {
    FILE* f = fopen(path.c_str(), "rb");
    if (!f) { why = "empty"; return false; }
    out.clear();
    char buf[4096];
    size_t n;
    while ((n = fread(buf, 1, sizeof buf, f)) > 0) {
        out.append(buf, n);
        if (out.size() > pstate::kMaxFileSize) { fclose(f); why = "file is too large to be a portable state"; return false; }
    }
    fclose(f);
    return true;
}

bool read_portable(const std::string& path, pstate::State& s, std::string& why) {
    std::string text;
    if (!read_text(path, text, why)) return false;
    if (!pstate::read(text, s, why)) return false;
    if (s.title_id != title_id()) { why = "made with another game (title " + s.title_id + ")"; return false; }
    return true;
}

bool write_portable(const std::string& path, const pstate::State& s, std::string& why) {
    std::string text = pstate::write(s, why);
    if (text.empty()) return false;
    std::string tmp = path + ".tmp";
    FILE* f = fopen(tmp.c_str(), "wb");
    if (!f) { why = "cannot write " + tmp; return false; }
    bool ok = fwrite(text.data(), 1, text.size(), f) == text.size();
    ok = fclose(f) == 0 && ok;
    if (ok) ok = host::replace_file(tmp, path);
    else remove(tmp.c_str());
    if (!ok) why = "write failed";
    return ok;
}

struct PendingPortable { std::shared_ptr<pstate::State> s; int slot = 0; std::string path; };
std::atomic<int> g_psave_req{0};
PendingPortable g_pload;
bool g_pload_waiting = false;
std::string g_last_portable;


void do_portable_save(Cpu* c, int slot) {
    pstate::State s;
    std::string why;
    bool retry = false;
    Live live;
    bool ready = describe_place(why, retry, live) && player_has_control(c, why) && capture_portable(c, s, why);
    (void)retry;
    if (!ready) {
        message("Slot %d: not saved (%s)", slot, why.c_str());
        report_done(slot, false, why);
        return;
    }
    std::string path = slot_path(slot, pstate::kExtension);
    if (!write_portable(path, s, why)) {
        message("Slot %d: not saved (%s)", slot, why.c_str());
        report_done(slot, false, why);
        return;
    }
    struct stat st{};
    stat(path.c_str(), &st);
    if (s.place == "course")
        LOG("[savestate] slot %d: portable state written (%lld bytes; %s at %.1f %.1f %.1f, save file %d)", slot, (long long)st.st_size,
            s.stage.c_str(), s.pos[0], s.pos[1], s.pos[2], s.file_slot + 1);
    else
        LOG("[savestate] slot %d: portable state written (%lld bytes; map world %d section %d node %d, save file %d)", slot,
            (long long)st.st_size, s.world + 1, s.section, s.node, s.file_slot + 1);
    struct stat full{};
    std::string older;
    if (stat(slot_path(slot).c_str(), &full) == 0) {
        char b[96];
        snprintf(b, sizeof b, "; the slot also keeps an older full state (%.0f MB)", full.st_size / 1048576.0);
        older = b;
        LOG("[savestate] slot %d also holds an older full state (%s, %.0f MB); it is kept", slot, slot_path(slot).c_str(),
            full.st_size / 1048576.0);
    }
    render::request_tv_dump(slot_path(slot, "png"), 0);
    {
        std::lock_guard<std::mutex> lk(g_mu);
        g_last_portable = path;
    }
    message("Saved to slot %d (%s)%s", slot, s.stage.c_str(), older.c_str());
    report_done(slot, true, "");
}

void service_portable_load(Cpu* c) {
    PendingPortable p;
    {
        std::lock_guard<std::mutex> lk(g_mu);
        p = g_pload;
    }
    if (!p.s) return;
    std::string why;
    bool retry = false;
    if (!apply_portable(c, *p.s, why, retry)) {
        if (retry) {
            if (!g_pload_waiting) message("%s: waiting to load (%s)", portable_label(p.slot).c_str(), why.c_str());
            g_pload_waiting = true;
            return;
        }
        message("%s: cannot load (%s)", portable_label(p.slot).c_str(), why.c_str());
    } else {
        const char* where = p.s->place == "map" ? "map" : "course";
        LOG("[savestate] %s: portable state applied (%s %s, save file %d)", portable_label(p.slot).c_str(), where, p.s->stage.c_str(),
            p.s->file_slot + 1);
        message("Loaded %s (%s)", p.slot ? ("slot " + std::to_string(p.slot)).c_str() : "portable state", p.s->stage.c_str());
        std::lock_guard<std::mutex> lk(g_mu);
        g_last_portable = p.path;
    }
    g_pload_waiting = false;
    std::lock_guard<std::mutex> lk(g_mu);
    g_pload = PendingPortable{};
}

void watch_arrival(Cpu* c) {
    if (g_arrival.place.empty()) return;
    uint64_t now = render::frame_count();
    if (now <= g_arrival.frame) return;
    if (now - g_arrival.frame > 30 * 60) {
        LOG("[savestate] portable load: did not arrive (%s)", g_arrival.place.c_str());
        g_arrival = Arrival{};
        return;
    }
    if (g_arrival.place == "course") {
        uint32_t task = ld32(kCourseTask);
        uint32_t mgr = ld32(kPlayerMgr);
        uint32_t actor = mgr ? ld32(mgr + kPlayerActor) : 0;
        if (task == 0 || !in_mem2(actor)) return;
        stf32(actor + kPosX, g_arrival.pos[0]);
        stf32(actor + kPosY, g_arrival.pos[1]);
        stf32(actor + kPosZ, g_arrival.pos[2]);
        LOG("[savestate] portable load: course %d-%d at %.1f %.1f %.1f", g_arrival.world + 1, g_arrival.course + 1, g_arrival.pos[0],
            g_arrival.pos[1], g_arrival.pos[2]);
        g_arrival = Arrival{};
        return;
    }
    std::string why;
    if (!fader_idle(c, why) || ld32(kCourseSelect) == 0) return;
    if (!nsmbu_apply_map_pose(g_arrival.world, g_arrival.section, g_arrival.node, g_arrival.course)) return;
    LOG("[savestate] portable load: map world %d section %d node %d", g_arrival.world + 1, g_arrival.section, g_arrival.node);
    g_arrival = Arrival{};
}

int g_full_setting = -1;
std::string full_cfg_path() { return state_dir() + "/full_save_states.cfg"; }
int full_env() {
    static const int v = [] {
        if (const char* e = env::FULL_SAVE_STATES()) return atoi(e) != 0 ? 1 : 0;
        for (env::Var t : {env::STATE_SAVE_AT, env::STATE_LOAD_AT, env::TEST_SAVE, env::TEST_LOAD})
            if ((t)()) return 1;
        return -1;
    }();
    return v;
}

bool newer_is_portable(int slot) {
    struct stat a{}, b{};
    bool full = stat(slot_path(slot).c_str(), &a) == 0, port = stat(slot_path(slot, pstate::kExtension).c_str(), &b) == 0;
    if (full && port) {
#ifdef __APPLE__
        return b.st_mtimespec.tv_sec > a.st_mtimespec.tv_sec ||
               (b.st_mtimespec.tv_sec == a.st_mtimespec.tv_sec && b.st_mtimespec.tv_nsec >= a.st_mtimespec.tv_nsec);
#else
        return b.st_mtime >= a.st_mtime;
#endif
    }
    return port;
}

struct Timed { uint64_t frame; int slot; };
std::vector<Timed> parse_timed(env::Var var) {
    std::vector<Timed> v;
    const char* e = (var)();
    unsigned long long f;
    int s, n;
    while (e && sscanf(e, "%llu:%d%n", &f, &s, &n) == 2) {
        v.push_back({f, s});
        e += n;
        if (*e != ',') break;
        e++;
    }
    return v;
}

}

uint32_t snap_ld32(uint32_t ea) {
    if (!g_check) return ld32(ea);
    uint32_t base = ea & ~(kChunk - 1);
    auto it = g_check->chunks.find(base);
    if (it == g_check->chunks.end()) {
        for (auto& g : g_check->regs)
            if (ea - g.base < g.size) return 0;
        return ld32(ea);
    }
    uint32_t v;
    memcpy(&v, it->second + (ea - base), 4);
    return __builtin_bswap32(v);
}

namespace {
SlotInfo full_slot_info(int slot) {
    SlotInfo info;
    info.path = slot_path(slot);
    FILE* f = fopen(info.path.c_str(), "rb");
    if (!f) return info;
    Header h{};
    std::string why;
    info.used = true;
    info.compatible = read_header(f, h, why);
    fclose(f);
    if (info.compatible) info.controller = controller_label(h.controller);
    if (info.compatible || memcmp(h.magic, kMagic, 8) == 0) {
        time_t t = (time_t)h.created;
        struct tm tmv;
#ifdef _WIN32
        localtime_s(&tmv,&t);
#else
        localtime_r(&t, &tmv);
#endif
        char buf[64];
        strftime(buf, sizeof buf, "%b %d %H:%M:%S", &tmv);
        info.when = buf;
        h.area[sizeof h.area - 1] = 0;
        if (h.area[0]) info.area = std::string(h.area);
    }
    return info;
}

SlotInfo portable_slot_info(int slot) {
    SlotInfo info;
    info.portable = true;
    info.path = slot_path(slot, pstate::kExtension);
    std::string text, why;
    if (!read_text(info.path, text, why)) {
        info.used = why != "empty";
        info.compatible = false;
        return info;
    }
    info.used = true;
    pstate::State s;
    info.compatible = pstate::read(text, s, why) && s.title_id == title_id();
    if (info.compatible) {

        static const char* mon[] = {"Jan", "Feb", "Mar", "Apr", "May", "Jun", "Jul", "Aug", "Sep", "Oct", "Nov", "Dec"};
        int y, mo, d;
        char rest[16];
        if (sscanf(s.created.c_str(), "%d-%d-%d %15s", &y, &mo, &d, rest) == 4 && mo >= 1 && mo <= 12) {
            char buf[48];
            snprintf(buf, sizeof buf, "%s %02d %s", mon[mo - 1], d, rest);
            info.when = buf;
        } else info.when = s.created;
        info.area = s.stage;
        info.controller = controller_label(s.controller);
    }
    return info;
}

}

SlotInfo slot_info(int slot) {
    if (is_auto(slot)) return full_slot_info(slot);
    SlotInfo info = newer_is_portable(slot) ? portable_slot_info(slot) : full_slot_info(slot);

    struct stat st{};
    if (stat(slot_path(slot, info.portable ? "bin" : pstate::kExtension).c_str(), &st) == 0) {
        info.older_other = true;
        info.older_bytes = (uint64_t)st.st_size;
    }
    return info;
}

void request_save_full(int slot) {
    if ((slot < 1 || slot > kSlots) && !is_auto(slot)) return;
    g_save_req = slot;
}

void request_save_portable(int slot) {
    if (slot < 1 || slot > kSlots) return;
    g_psave_req = slot;
}

void request_save(int slot, std::function<void(bool ok, const std::string& why)> done) {
    if ((slot < 1 || slot > kSlots) && !is_auto(slot)) {
        if (done) done(false, "no such slot");
        return;
    }
    {
        std::lock_guard<std::mutex> lk(g_mu);
        g_done_slot = slot;
        g_done = std::move(done);
    }
    if (is_auto(slot) || full_states()) g_save_req = slot;
    else g_psave_req = slot;
}

void request_save(int slot) { request_save(slot, nullptr); }

bool in_gameplay() { return quitprompt::gameplay_stage(stage_name()); }

void request_load_portable_file(const std::string& path) {
    auto s = std::make_shared<pstate::State>();
    std::string why;
    if (!read_portable(path, *s, why)) {
        message("Portable state: %s", why.c_str());
        return;
    }
    if (s->runtime != std::string(build::version()) + " (" + build::commit() + ")")
        LOG("[savestate] portable state from %s (this build: %s %s)", s->runtime.c_str(), build::version(), build::commit());
    int slot = 0;
    for (int i = 1; i <= kSlots; i++)
        if (path == slot_path(i, pstate::kExtension)) slot = i;
    std::lock_guard<std::mutex> lk(g_mu);
    g_pload = PendingPortable{s, slot, path};
}

void request_load(int slot) {
    if ((slot < 1 || slot > kSlots) && !is_auto(slot)) return;
    if (!is_auto(slot) && newer_is_portable(slot)) {
        request_load_portable_file(slot_path(slot, pstate::kExtension));
        return;
    }
    if (g_loading.exchange(true)) return;
    std::thread([slot] {
        auto t0 = std::chrono::steady_clock::now();
        std::string why;
        std::shared_ptr<Snapshot> s;
        {
            std::lock_guard<std::mutex> io(g_io);
            s = read_slot(slot, why);
        }
        if (!s) {
            message("%s: %s", slot_label(slot).c_str(), why.c_str());
            g_loading = false;
            return;
        }
        LOG("[savestate] slot %d: read and decompressed %.1f MB in %.0f ms", slot, s->payload.size() / 1048576.0,
            std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - t0).count());
        std::lock_guard<std::mutex> lk(g_mu);
        g_load_ready = s;
    }).detach();
}

bool full_states_forced() { return full_env() >= 0; }

bool full_states() {
    if (full_env() >= 0) return full_env() == 1;
    std::lock_guard<std::mutex> lk(g_mu);
    if (g_full_setting < 0) {
        g_full_setting = 0;
        if (FILE* f = fopen(full_cfg_path().c_str(), "r")) {
            int v = 0;
            if (fscanf(f, "%d", &v) == 1) g_full_setting = v ? 1 : 0;
            fclose(f);
        }
    }
    return g_full_setting == 1;
}

void set_full_states(bool on) {
    {
        std::lock_guard<std::mutex> lk(g_mu);
        g_full_setting = on ? 1 : 0;
    }
    if (FILE* f = fopen(full_cfg_path().c_str(), "w")) {
        fprintf(f, "%d\n", on ? 1 : 0);
        fclose(f);
    }
    LOG("[savestate] full save states %s", on ? "on" : "off");
}

std::string bug_report_text() {
    std::string pstate_path;
    {
        std::lock_guard<std::mutex> lk(g_mu);
        pstate_path = g_last_portable;
    }
    if (pstate_path.empty()) {
        std::filesystem::file_time_type best{};
        for (int i = 1; i <= kSlots; i++) {
            std::error_code ec;
            std::string p = slot_path(i, pstate::kExtension);
            auto t = std::filesystem::last_write_time(p, ec);
            if (!ec && (pstate_path.empty() || t > best)) { best = t; pstate_path = p; }
        }
    }
    std::string save = config::save_dir + "/user/rp_personal_savedata.dat";
    std::string common = config::save_dir + "/common/rp_savedata.dat";
    auto absolute = [](const std::string& p) {
        std::error_code ec;
        auto a = std::filesystem::absolute(p, ec);
        return ec ? p : a.lexically_normal().string();
    };
    if (!pstate_path.empty()) pstate_path = absolute(pstate_path);
    save = absolute(save);
    common = absolute(common);
    std::string out;
    if (!pstate_path.empty()) out += "Portable save state (attach): " + pstate_path + "\n";
    else out += "Portable save state: none yet (Save a state first: it is small and contains no game data)\n";
    out += "Save files (attach): " + save + " and " + common + "\n";
    out += "Never attach full save states (.bin): they contain game data.\n";
    return out;
}

void notice(const std::string& text) { message("%s", text.c_str()); }

std::string last_message() {
    std::lock_guard<std::mutex> lk(g_mu);
    if (g_message.empty() || std::chrono::steady_clock::now() - g_message_time > std::chrono::seconds(g_message.size() > 60 ? 7 : 4)) return "";
    return g_message;
}

std::atomic<uint64_t> g_last_load_frame{0}, g_last_load_step{0};
std::atomic<uint32_t> g_last_load_counter{0};
uint32_t last_load_counter() { return g_last_load_counter.load(); }
uint64_t last_load_frame() { return g_last_load_frame.load(); }
uint64_t last_load_step() { return g_last_load_step.load(); }

std::string states_dir() { return state_dir(); }

void service(Cpu* c) {
    crashrec::service();

    static const std::vector<Timed> save_at = parse_timed(env::STATE_SAVE_AT), load_at = parse_timed(env::STATE_LOAD_AT),
                                    psave_at = parse_timed(env::PORTABLE_SAVE_AT), pload_at = parse_timed(env::PORTABLE_LOAD_AT);
    if (!save_at.empty() || !load_at.empty() || !psave_at.empty() || !pload_at.empty()) {
        static uint64_t last = 0;
        uint64_t f = render::frame_count();
        for (auto& t : save_at)
            if (t.frame > last && t.frame <= f) request_save_full(t.slot);
        for (auto& t : load_at)
            if (t.frame > last && t.frame <= f) request_load(t.slot);
        for (auto& t : psave_at)
            if (t.frame > last && t.frame <= f) request_save_portable(t.slot);
        for (auto& t : pload_at)
            if (t.frame > last && t.frame <= f) request_load_portable_file(slot_path(t.slot, pstate::kExtension));
        last = f;
    }
    static bool env_load = [] {
        if (const char* e = env::PORTABLE_LOAD()) request_load_portable_file(e);
        return true;
    }();
    (void)env_load;
    if (int slot = g_psave_req.exchange(0)) do_portable_save(c, slot);
    service_portable_load(c);
    watch_arrival(c);

    static const int dump = env::STATE_DUMP.integer_or(0);
    if (int slot = g_save_req.load()) {
        if (do_save(slot)) {
            g_save_req = 0;
            g_attempts = 0;
            for (int k = 1; k <= dump; k++)
                render::request_tv_dump("state_save" + std::to_string(slot) + "_" + std::to_string(k) + ".png", k);
        }
        return;
    }
    std::shared_ptr<Snapshot> s;
    {
        std::lock_guard<std::mutex> lk(g_mu);
        s = g_load_ready;
    }
    if (s && do_load(s)) {
        g_last_load_frame = render::frame_count();
        g_last_load_step = interp::logic_steps();
        g_last_load_counter = ld32(GD(0x101FF560));
        {
            std::lock_guard<std::mutex> lk(g_mu);
            g_load_ready.reset();
        }
        g_loading = false;
        g_attempts = 0;
        for (int k = 1; k <= dump; k++)
            render::request_tv_dump("state_load" + std::to_string(s->slot) + "_" + std::to_string(k) + ".png", k);
    }
}

}
