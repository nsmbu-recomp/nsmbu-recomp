#include "core/runtime.h"
#include "guest_addr.h"
#include "input/input.h"

#include <cstdio>
#include <cstring>
#include <string>
#include <vector>
#include <sys/stat.h>

extern "C" void f_029D9E30_orig(Cpu* c);
extern "C" void f_029872BC_orig(Cpu* c);
extern "C" void f_029D7648_orig(Cpu* c);
extern "C" void f_029D8C10_orig(Cpu* c);
extern "C" void f_029D8478_orig(Cpu* c);
extern "C" void f_029D8270_orig(Cpu* c);
extern "C" void f_029D8304_orig(Cpu* c);
extern "C" void f_029D82D4_orig(Cpu* c);
extern "C" void f_029D8394_orig(Cpu* c);
extern "C" void f_029D9560_orig(Cpu* c);

static const uint32_t kSaveMgr = GD(0x101E7DA4);
static const uint32_t kSaveSlotSize = 0x204u;
static const uint32_t kMainRecordBase = 0x10u;
static const uint32_t kBackupRecordBase = 0xC28u;
static const uint32_t kSavedataSize = 0xB134u;
static const uint32_t kLevelFlagCount = 123u;
static const uint32_t kLevelFlagOff = 0x6Bu;
static const uint32_t kRecordCompleteOff = 0x1Eu;
static const uint32_t kQuickPastMain = kBackupRecordBase - kMainRecordBase;

static const char* read_error_name(int32_t e) {
    if (e == 0) return "ok";
    if (e == 1) return "device unavailable";
    if (e == -1) return "file unavailable";
    if (e == -2) return "file invalid";
    return "unknown";
}

static const char* write_error_name(int32_t e) {
    if (e == 0) return "ok";
    if (e == -1) return "device unavailable";
    if (e == -2) return "write failed";
    return "unknown";
}

static const char* state_name(uint32_t state) {
    switch (state) {
    case 1: return "load rp_savedata.dat";
    case 2: return "write rp_savedata.dat";
    case 3: return "load challenge replay";
    case 4: return "write challenge replay";
    case 5: return "load rp_personal_savedata.dat";
    case 6: return "write rp_personal_savedata.dat";
    default: return "unknown state";
    }
}

static uint32_t save_mgr() {
    return ld32(kSaveMgr);
}

static uint32_t savedata_buf(uint32_t mgr) {
    return mgr ? ld32(mgr + 0x24u) : 0;
}

static bool in_mem2(uint32_t ea) {
    return ea >= mem::kMem2Start && ea < mem::kMem2End;
}

static void format_exists(char* out, size_t n, uint32_t savedata) {
    if (!in_mem2(savedata) || savedata + kMainRecordBase + 3u * kSaveSlotSize >= mem::kMem2End) {
        snprintf(out, n, "?");
        return;
    }
    char* p = out;
    char* end = out + n;
    for (uint32_t i = 0; i < 3u && p + 8 < end; i++) {
        uint8_t main = ld8(savedata + kMainRecordBase + i * kSaveSlotSize);
        uint8_t backup = ld8(savedata + kBackupRecordBase + i * kSaveSlotSize);
        int wrote = snprintf(p, (size_t)(end - p), "%s%d%s%s", i ? " " : "", i, main ? "+" : "-", backup ? "b" : "");
        if (wrote < 0) break;
        p += wrote;
    }
}

struct RecordView {
    bool ok = false;
    uint8_t exists = 0;
    uint8_t lives = 0;
    uint8_t world = 0;
    uint8_t section = 0;
    uint8_t node = 0;
    int flags = 0;
};

static RecordView view_record(const uint8_t* rec) {
    RecordView v;
    v.ok = true;
    v.exists = rec[0];
    v.lives = rec[0x0C];
    v.world = rec[4];
    v.section = rec[5];
    v.node = rec[6];
    for (uint32_t i = 0; i < kLevelFlagCount; i++)
        if (rec[kLevelFlagOff + i]) v.flags++;
    return v;
}

static RecordView view_guest_record(uint32_t savedata, uint32_t base) {
    RecordView v;
    uint32_t rec = savedata + base;
    if (!in_mem2(savedata) || rec + kSaveSlotSize >= mem::kMem2End) return v;
    uint8_t raw[kSaveSlotSize];
    for (uint32_t i = 0; i < kSaveSlotSize; i++) raw[i] = ld8(rec + i);
    return view_record(raw);
}

static bool read_host_savedata(std::vector<uint8_t>& out) {
    std::string path = config::save_dir + "/common/rp_savedata.dat";
    FILE* f = fopen(path.c_str(), "rb");
    if (!f) return false;
    if (fseek(f, 0, SEEK_END) != 0) {
        fclose(f);
        return false;
    }
    long n = ftell(f);
    if (n < (long)kSavedataSize || fseek(f, 0, SEEK_SET) != 0) {
        fclose(f);
        return false;
    }
    out.resize(kSavedataSize);
    size_t got = fread(out.data(), 1, out.size(), f);
    fclose(f);
    return got == out.size();
}

static void append_record(char* out, size_t n, const char* name, const RecordView& v) {
    size_t len = strlen(out);
    if (len >= n) return;
    if (!v.ok)
        snprintf(out + len, n - len, " %s=?", name);
    else
        snprintf(out + len, n - len, " %s exists=%u lives=%u map=%u.%u.%u flags=%d", name, v.exists, v.lives, v.world,
                 v.section, v.node, v.flags);
}

static void log_slot_records(const char* where, uint8_t file, const RecordView& main_rec, const RecordView& quick) {
    char buf[320];
    snprintf(buf, sizeof buf, "[save] %s file %u", where, (unsigned)file);
    append_record(buf, sizeof buf, "main", main_rec);
    append_record(buf, sizeof buf, "quick", quick);
    LOG("%s", buf);
}

static void log_guest_and_host_records(const char* guest_where) {
    uint32_t buf = savedata_buf(save_mgr());
    uint8_t file = 0xFF;
    if (in_mem2(buf) && buf + 8u < mem::kMem2End) file = ld8(buf + 7);
    if (file < 6u) {
        log_slot_records(guest_where, file, view_guest_record(buf, kMainRecordBase + (uint32_t)file * kSaveSlotSize),
                         view_guest_record(buf, kBackupRecordBase + (uint32_t)file * kSaveSlotSize));
    } else {
        LOG("[save] %s file %u out of range", guest_where, (unsigned)file);
    }
    std::vector<uint8_t> host;
    if (!read_host_savedata(host)) {
        LOG("[save] host rp_savedata.dat unreadable");
        return;
    }
    uint8_t host_file = host[7];
    if (host_file >= 6u) {
        LOG("[save] host file %u out of range", (unsigned)host_file);
        return;
    }
    uint32_t main_off = kMainRecordBase + (uint32_t)host_file * kSaveSlotSize;
    uint32_t quick_off = kBackupRecordBase + (uint32_t)host_file * kSaveSlotSize;
    log_slot_records("host", host_file, view_record(host.data() + main_off), view_record(host.data() + quick_off));
}

static long host_save_size(const char* rel) {
    std::string path = config::save_dir + rel;
    struct stat st;
    if (stat(path.c_str(), &st) != 0) return -1;
    return (long)st.st_size;
}

static void log_savedata_summary(const char* what, bool writing) {
    uint32_t mgr = save_mgr();
    int32_t err = mgr ? (int32_t)ld32(mgr + (writing ? 0x1Cu : 0x18u)) : 0x7fffffff;
    const char* (*err_name)(int32_t) = writing ? write_error_name : read_error_name;
    uint32_t buf = savedata_buf(mgr);
    char exists[48];
    format_exists(exists, sizeof exists, buf);
    uint8_t last = 0xFF;
    uint32_t magic = 0;
    uint8_t ver[3] = {};
    if (in_mem2(buf) && buf + 0x10u < mem::kMem2End) {
        magic = ld32(buf);
        ver[0] = ld8(buf + 4);
        ver[1] = ld8(buf + 5);
        ver[2] = ld8(buf + 6);
        last = ld8(buf + 7);
    }
    LOG("[save] %s: %s (%d), last_file=%u, magic=%08X ver=%u.%u.%u, files %s, dir %s", what, err_name(err), err,
        (unsigned)last, magic, ver[0], ver[1], ver[2], exists, config::save_dir.c_str());
}

extern "C" void hook_029D9E30(Cpu* c) {
    uint32_t state = c->r[5];
    if (state >= 1 && state <= 6) LOG("[save] %s starting", state_name(state));
    f_029D9E30_orig(c);
}

extern "C" void site_029D94C0(Cpu*) {
    log_savedata_summary("load rp_savedata.dat", false);
    LOG("[save] host common/rp_savedata.dat %ld bytes", host_save_size("/common/rp_savedata.dat"));
    log_guest_and_host_records("guest after load");
}

extern "C" void hook_029D9560(Cpu* c) {
    f_029D9560_orig(c);
    log_savedata_summary("write rp_savedata.dat", true);
    LOG("[save] host common/rp_savedata.dat %ld bytes after write", host_save_size("/common/rp_savedata.dat"));
    log_guest_and_host_records("guest after write");
}

extern "C" void site_029D9CB8(Cpu*) {
    uint32_t mgr = save_mgr();
    int32_t err = mgr ? (int32_t)ld32(mgr + 0x18u) : 0x7fffffff;
    LOG("[save] load rp_personal_savedata.dat: %s (%d), host %ld bytes, dir %s", read_error_name(err), err,
        host_save_size("/user/rp_personal_savedata.dat"), config::save_dir.c_str());
}

extern "C" void site_029D9E2C(Cpu*) {
    uint32_t mgr = save_mgr();
    int32_t err = mgr ? (int32_t)ld32(mgr + 0x1Cu) : 0x7fffffff;
    LOG("[save] write rp_personal_savedata.dat: %s (%d), host %ld bytes, dir %s", write_error_name(err), err,
        host_save_size("/user/rp_personal_savedata.dat"), config::save_dir.c_str());
}

extern "C" void hook_029D8270(Cpu* c) {
    LOG("[save] startReadGame requested");
    f_029D8270_orig(c);
}

extern "C" void hook_029D8304(Cpu* c) {
    uint32_t mgr = save_mgr();
    uint32_t buf = savedata_buf(mgr);
    char exists[48];
    format_exists(exists, sizeof exists, buf);
    uint8_t last = in_mem2(buf) && buf + 8u < mem::kMem2End ? ld8(buf + 7) : 0xFF;
    LOG("[save] startWriteGame requested (last_file=%u, files %s)", (unsigned)last, exists);
    f_029D8304_orig(c);
}

extern "C" void hook_029D82D4(Cpu* c) {
    LOG("[save] startReadPersonalSavedata requested");
    f_029D82D4_orig(c);
}

extern "C" void hook_029D8394(Cpu* c) {
    LOG("[save] startWritePersonalSavedata requested");
    f_029D8394_orig(c);
}

extern "C" void hook_029872BC(Cpu* c) {
    uint32_t mgr = save_mgr();
    uint32_t buf = savedata_buf(mgr);
    uint8_t file = 0;
    uint8_t exists = 0;
    if (in_mem2(buf) && buf + kMainRecordBase + 3u * kSaveSlotSize < mem::kMem2End) {
        file = ld8(buf + 7);
        if (file < 6u) exists = ld8(buf + kMainRecordBase + (uint32_t)file * kSaveSlotSize);
    }
    LOG("[save] continue: file %u %s -> %s (controller %s)", (unsigned)file, exists ? "has record" : "empty",
        exists ? "CourseSelect" : "Begin Story Mode", input::pro_controller() ? "Pro" : "GamePad");
    f_029872BC_orig(c);
}

static uint8_t g_main_before[kSaveSlotSize];
static uint8_t g_quick_keep[kSaveSlotSize];
static uint32_t g_main_ea;
static int g_promote;

static bool same_progress(const uint8_t* a, const uint8_t* b) {
    if (memcmp(a + 4, b + 4, 3) != 0) return false;
    if (a[0x0C] != b[0x0C]) return false;
    return memcmp(a + kLevelFlagOff, b + kLevelFlagOff, kLevelFlagCount) == 0;
}

static void copy_record(uint8_t* dst, uint32_t ea) {
    for (uint32_t i = 0; i < kSaveSlotSize; i++) dst[i] = ld8(ea + i);
}

extern "C" void hook_029D8C10(Cpu* c) {
    g_promote = 0;
    uint32_t mgr = c->r[3];
    uint32_t file = c->r[4] & 0xFFu;
    uint32_t savedata = savedata_buf(mgr);
    uint32_t main_off = kMainRecordBase + file * kSaveSlotSize;
    uint32_t quick_off = kBackupRecordBase + file * kSaveSlotSize;
    if (file < 6u && in_mem2(savedata) && savedata + quick_off + kSaveSlotSize < mem::kMem2End && ld8(savedata + quick_off) != 0) {
        copy_record(g_main_before, savedata + main_off);
        copy_record(g_quick_keep, savedata + quick_off);
        g_main_ea = savedata + main_off;
        g_promote = 1;
    }
    f_029D8C10_orig(c);
}

extern "C" void site_029D8C48(Cpu*) {
    if (!g_promote) return;
    g_promote = 0;
    if (!in_mem2(g_main_ea) || g_main_ea + kSaveSlotSize >= mem::kMem2End) return;
    uint8_t now[kSaveSlotSize];
    copy_record(now, g_main_ea);
    if (!same_progress(now, g_main_before) || same_progress(g_quick_keep, g_main_before)) return;
    for (uint32_t i = 0; i < kSaveSlotSize; i++) st8(g_main_ea + i, g_quick_keep[i]);
    LOG("[save] startSaveGame kept quick file node %u lives %u", g_quick_keep[6], g_quick_keep[0x0C]);
}

extern "C" void hook_029D8478(Cpu* c) {
    uint32_t mgr = c->r[3];
    uint32_t file = c->r[4] & 0xFFu;
    uint32_t savedata = savedata_buf(mgr);
    uint32_t main_off = kMainRecordBase + file * kSaveSlotSize;
    uint32_t quick_off = kBackupRecordBase + file * kSaveSlotSize;
    if (file < 6u && in_mem2(savedata) && savedata + quick_off + kSaveSlotSize < mem::kMem2End && ld8(savedata + quick_off) != 0) {
        for (uint32_t i = 0; i < kSaveSlotSize; i++) st8(savedata + main_off + i, ld8(savedata + quick_off + i));
        LOG("[save] open file %u commits quick node %u lives %u", file, ld8(savedata + main_off + 6), ld8(savedata + main_off + 0x0C));
    }
    f_029D8478_orig(c);
}

extern "C" void hook_029D7648(Cpu* c) {
    uint8_t file = (uint8_t)c->r[4];
    uint32_t mgr = c->r[3] ? c->r[3] : save_mgr();
    uint32_t buf = savedata_buf(mgr);
    uint8_t main = 0;
    uint8_t backup = 0;
    if (in_mem2(buf) && file < 6u && buf + kBackupRecordBase + ((uint32_t)file + 1u) * kSaveSlotSize < mem::kMem2End) {
        main = ld8(buf + kMainRecordBase + (uint32_t)file * kSaveSlotSize);
        backup = ld8(buf + kBackupRecordBase + (uint32_t)file * kSaveSlotSize);
    }
    RecordView main_rec = view_guest_record(buf, kMainRecordBase + (uint32_t)file * kSaveSlotSize);
    RecordView quick = view_guest_record(buf, kBackupRecordBase + (uint32_t)file * kSaveSlotSize);
    const char* used = quick.ok && quick.exists ? "quick" : "main";
    LOG("[save] apply file %u uses %s (main %s, quick %s)", (unsigned)file, used, main ? "set" : "empty",
        backup ? "set" : "empty");
    if (file < 6u) log_slot_records("apply", file, main_rec, quick);
    f_029D7648_orig(c);
}

struct FileSelectSig {
    uint8_t init = 0;
    uint8_t used = 0;
    uint8_t exists = 0;
    uint8_t lives = 0;
    uint8_t complete = 0;
    uint8_t world = 0;
    uint8_t section = 0;
    uint8_t node = 0;
    int flags = 0;
};

static FileSelectSig g_file_select_sig[6];
static uint8_t g_file_select_complete[6];
static uint8_t g_file_select_complete_init[6];

static bool file_select_quick_caller(uint32_t lr) {
    return lr == GC(0x02631BD4) || lr == GC(0x0263ADC8) || lr == GC(0x02641A3C);
}

static void log_file_select_slot(uint32_t file, bool use_quick, const RecordView& rec, uint8_t complete) {
    FileSelectSig& prev = g_file_select_sig[file];
    uint8_t used = use_quick ? 1 : 0;
    if (prev.init && prev.used == used && prev.exists == rec.exists && prev.lives == rec.lives &&
        prev.complete == complete && prev.flags == rec.flags && prev.world == rec.world && prev.section == rec.section &&
        prev.node == rec.node)
        return;
    prev.init = 1;
    prev.used = used;
    prev.exists = rec.exists;
    prev.lives = rec.lives;
    prev.complete = complete;
    prev.flags = rec.flags;
    prev.world = rec.world;
    prev.section = rec.section;
    prev.node = rec.node;
    LOG("[save] file-select slot file %u uses %s exists=%u lives=%u complete=%u flags=%d map=%u.%u.%u", file,
        use_quick ? "quick" : "main", (unsigned)rec.exists, (unsigned)rec.lives, (unsigned)complete, rec.flags,
        (unsigned)rec.world, (unsigned)rec.section, (unsigned)rec.node);
}

extern "C" void site_029D71A4(Cpu* c) {
    uint32_t savedata = c->r[12];
    uint32_t file = c->r[4] & 0xFFu;
    uint32_t quick_off = kBackupRecordBase + file * kSaveSlotSize;
    uint32_t main_off = kMainRecordBase + file * kSaveSlotSize;
    RecordView quick = file < 6u ? view_guest_record(savedata, quick_off) : RecordView{};
    bool use_quick = file_select_quick_caller(c->lr) && quick.ok && quick.exists != 0;
    if (use_quick) c->r[12] = savedata + kQuickPastMain;
    if (file >= 6u) return;
    RecordView chosen = use_quick ? quick : view_guest_record(savedata, main_off);
    if (!chosen.ok) return;
    uint8_t complete = ld8(savedata + (use_quick ? quick_off : main_off) + kRecordCompleteOff);
    log_file_select_slot(file, use_quick, chosen, complete);
}

static void note_file_select_complete(uint32_t file, uint8_t byte) {
    if (file >= 6u) return;
    if (g_file_select_complete_init[file] && g_file_select_complete[file] == byte) return;
    g_file_select_complete_init[file] = 1;
    g_file_select_complete[file] = byte;
    LOG("[save] file-select complete file %u byte=%u", file, (unsigned)byte);
}

static void shift_file_select_completion(Cpu* c, int reg, uint32_t file) {
    uint32_t base = c->r[reg];
    uint32_t end = base + kBackupRecordBase + kSaveSlotSize;
    if (!in_mem2(base) || end < base || end >= mem::kMem2End) return;
    if (ld8(base + kBackupRecordBase) == 0) return;
    uint32_t shifted = base + kQuickPastMain;
    c->r[reg] = shifted;
    note_file_select_complete(file & 0xFFu, ld8(shifted + 0x2Eu));
}

extern "C" void site_02631BE4(Cpu* c) {
    shift_file_select_completion(c, 12, c->r[30]);
}

extern "C" void site_0263ADD8(Cpu* c) {
    shift_file_select_completion(c, 5, c->r[27]);
}

extern "C" void site_02641A4C(Cpu* c) {
    shift_file_select_completion(c, 7, c->r[31]);
}
