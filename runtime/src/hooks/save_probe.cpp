#include "core/runtime.h"
#include "guest_addr.h"
#include "hooks/interp.h"

#include <cstdio>
#include <cstring>

extern "C" void f_029D8C10_orig(Cpu* c);
extern "C" void f_029D8478_orig(Cpu* c);
extern "C" void f_029D8304_orig(Cpu* c);

static const uint32_t kSaveMgr = GD(0x101E7DA4);
static const uint32_t kSaveSlotSize = 0x204u;
static const uint32_t kMainRecordBase = 0x10u;
static const uint32_t kBackupRecordBase = 0xC28u;
static const uint32_t kLevelFlagCount = 123u;
static const uint32_t kLevelFlagOff = 0x6Bu;
static const uint32_t kRecordCompleteOff = 0x1Eu;
static const uint32_t kQuickPastMain = kBackupRecordBase - kMainRecordBase;

static uint32_t save_mgr() {
    return ld32(kSaveMgr);
}

static uint32_t savedata_buf(uint32_t mgr) {
    return mgr ? ld32(mgr + 0x24u) : 0;
}

static bool in_mem2(uint32_t ea) {
    return ea >= mem::kMem2Start && ea < mem::kMem2End;
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

static int g_fill_quick;
static uint32_t g_fill_file;

extern "C" void hook_029D8304(Cpu* c) {
    uint32_t mgr = save_mgr();
    uint32_t buf = savedata_buf(mgr);
    if (g_fill_quick) {
        g_fill_quick = 0;
        uint32_t file = g_fill_file;
        uint32_t main_off = kMainRecordBase + file * kSaveSlotSize;
        uint32_t quick_off = kBackupRecordBase + file * kSaveSlotSize;
        if (file < 6u && in_mem2(buf) && buf + quick_off + kSaveSlotSize < mem::kMem2End) {
            for (uint32_t i = 0; i < kSaveSlotSize; i++) st8(buf + quick_off + i, ld8(buf + main_off + i));
        }
    }
    f_029D8304_orig(c);
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
    // Map Save snapshots the story record and then clears the quick record.
    // Keep that snapshot in the quick slot so the file select can resume it.
    if (!interp::course_play_active() && ld32(GD(0x101CC96Cu))) {
        uint32_t savedata = savedata_buf(save_mgr());
        uint8_t file = in_mem2(savedata) && savedata + 8u < mem::kMem2End ? ld8(savedata + 7) : 0xFF;
        if (file < 6u) {
            g_fill_quick = 1;
            g_fill_file = file;
        }
    }
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
