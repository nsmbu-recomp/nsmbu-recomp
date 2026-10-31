#include "core/runtime.h"
#include "hooks/interp.h"
#include "hooks/mii_draw.h"
#include "mods/packages.h"
#include "mods/sdk/nsmbu.h"
#include "guest_addr.h"
#include "input/input.h"

#include <cstdio>

extern "C" void f_02994164_orig(Cpu* c);
extern "C" void nsmbu_note_scene_src(uint32_t src, uint32_t factory);
extern "C" void f_0226FBC4_orig(Cpu* c);
extern "C" void f_024BD86C_orig(Cpu* c);
extern "C" void f_0252F900_orig(Cpu* c);
extern "C" void f_0252D9C0_orig(Cpu* c);
extern "C" void f_025CF020_orig(Cpu* c);
extern "C" void f_024EFE58_orig(Cpu* c);
extern "C" void f_024F009C_orig(Cpu* c);

static const uint32_t kAreaTask = GD(0x101D1598);
static const uint32_t kMiiCentral = GD(0x101D2CC4);
static const uint32_t kCourseInfo = GD(0x101D18EC);
static const uint32_t kCourseSelectMgr = GD(0x101CBE14);
static const uint32_t kFieldGame = GD(0x101D1604);
static const uint32_t kMainGame = GD(0x101E7D7C);
static const uint32_t kSaveMgr = GD(0x101E7DA4);
static const uint32_t kSaveSlotSize = 0x204u;
static const uint32_t kPathNodeSize = 0x68;

static bool course_pair_ok(uint8_t world, uint8_t course) {
    return world != 0xFFu && world <= 8u && course != 0xFFu && course <= 40u;
}

static bool read_course_info(uint8_t* world_out, uint8_t* course_out) {
    uint32_t info = ld32(kCourseInfo);
    if (info == 0) return false;
    uint8_t world = ld8(info + 0x10);
    uint8_t course = ld8(info + 0x11);
    uint8_t next_world = ld8(info + 0x1C);
    uint8_t next_course = ld8(info + 0x1D);
    if (course_pair_ok(next_world, next_course) && (next_world != world || next_course != course)) {
        world = next_world;
        course = next_course;
    }
    if (world == 0xFFu || course == 0xFFu || course > 40u) {
        const uint8_t now_world = ld8(info + 0x00);
        const uint8_t now_course = ld8(info + 0x01);
        if ((world == 0xFFu || world > 8u) && now_world != 0xFFu) world = now_world;
        if ((course == 0xFFu || course > 40u) && now_course != 0xFFu) course = now_course;
    }
    if (world_out) *world_out = world;
    if (course_out) *course_out = course;
    return true;
}

static bool course_banner_state(uint8_t g[3], uint8_t* world_out, uint8_t* course_out) {
    uint8_t world = 0;
    uint8_t course = 0;
    if (!read_course_info(&world, &course)) return false;
    int display = (int)course + 1;
    if (display < 1 || display > 999) return false;
    char buf[4];
    int n = snprintf(buf, sizeof(buf), "%d", display);
    if (n < 1 || n > 3) return false;
    g[0] = g[1] = g[2] = 0xFF;
    for (int i = 0; i < n; i++) {
        int d = buf[i] - '0';
        g[i] = (d >= 1 && d <= 5) ? (uint8_t)(d - 1) : 0xFF;
    }
    if (world_out) *world_out = world;
    if (course_out) *course_out = course;
    return true;
}

static bool in_mem2(uint32_t ea) {
    return ea >= mem::kMem2Start && ea < mem::kMem2End;
}

static bool checkpoint_missing() {
    uint32_t field_game = ld32(kFieldGame);
    if (field_game == 0) return true;
    return (int32_t)ld32(field_game + 0x10) < 0;
}

static uint32_t cs_get_path(uint32_t mgr, uint32_t world, uint32_t section) {
    uint32_t idx = (world << 1) + section;
    if (idx >= 0x12u) return 0;
    return ld32(mgr + 0x60u + idx * 4u);
}

static uint32_t cs_get_node(uint32_t path, uint32_t node_idx) {
    if (path == 0 || node_idx >= 0x100u) return 0;
    return path + 0x10u + node_idx * kPathNodeSize;
}

static bool node_name_matches(uint32_t node, const char* want) {
    if (!in_mem2(node)) return false;
    uint32_t p = ld32(node);
    if (!in_mem2(p) || ld8(p) < 0x20 || ld8(p) >= 0x7F) return false;
    return mem::read_cstr(p) == want;
}

static bool find_course_map_node(uint8_t world, uint8_t course, uint32_t* section_out,
                                 uint32_t* node_idx_out, uint32_t* cp_index_out) {
    uint32_t mgr = ld32(kCourseSelectMgr);
    if (mgr == 0) return false;
    char want[8];
    snprintf(want, sizeof(want), "W%u%02u", (unsigned)world + 1u, (unsigned)course + 1u);
    for (uint32_t section = 0; section < 2u; section++) {
        uint32_t path = cs_get_path(mgr, world, section);
        if (path == 0 || !in_mem2(path)) continue;
        for (uint32_t node_idx = 0; node_idx < 0x100u; node_idx++) {
            uint32_t node = cs_get_node(path, node_idx);
            if (!in_mem2(node) || node + kPathNodeSize > mem::kMem2End) break;
            if (!node_name_matches(node, want)) continue;
            if (section_out) *section_out = section;
            if (node_idx_out) *node_idx_out = node_idx;
            if (cp_index_out) *cp_index_out = node_idx;
            return true;
        }
    }
    return false;
}

static void write_checkpoint(uint32_t data, int32_t index, uint8_t world, uint8_t course,
                             uint8_t file, const uint8_t banner[3]) {
    st32(data + 0x00u, (uint32_t)index);
    st8(data + 0x04u, world);
    st8(data + 0x05u, course);
    st8(data + 0x06u, file);
    st8(data + 0x07u, 0);
    st32(data + 0x08u, 0);
    st8(data + 0x0Cu, 0xFF);
    st8(data + 0x0Du, 0xFF);
    st8(data + 0x0Eu, 0xFF);
    st8(data + 0x0Fu, 0xFF);
    const uint32_t slot = (uint32_t)index < 2u ? (uint32_t)index : 0u;
    st8(data + 0x0Eu + slot, 0);
    for (int i = 0; i < 3; i++) st8(data + 0x10u + (uint32_t)i, banner[i]);
}

static void seed_player_map_fields(uint32_t player, uint32_t node, uint8_t world, uint32_t section,
                                   uint32_t course_slot, bool pack_create_pos) {
    if (player == 0 || !in_mem2(player)) return;
    st32(player + 0xF0u, world);
    st32(player + 0xF4u, section);
    st32(player + 0xF8u, 0);
    st32(player + 0xFCu, course_slot);
    if (node == 0 || !in_mem2(node) || node + 0x4Cu >= mem::kMem2End) return;
    const double x = ldf32(node + 0x44u);
    const double y = ldf32(node + 0x48u);
    const double z = ldf32(node + 0x4Cu);
    if (player + 0x58u < mem::kMem2End) {
        stf32(player + 0x50u, x);
        stf32(player + 0x54u, y);
        stf32(player + 0x58u, z);
    }
    if (pack_create_pos && player + 0x118u < mem::kMem2End) {
        stf32(player + 0x110u, x);
        stf32(player + 0x114u, y);
        stf32(player + 0x118u, z);
    }
}

static bool g_left_course = false;

static void seed_map_return_pose(uint32_t player, bool pack_create_pos) {
    if (!g_left_course) return;
    uint8_t world = 0;
    uint8_t course = 0;
    if (!read_course_info(&world, &course)) return;
    if (!course_pair_ok(world, course)) return;
    uint32_t section = 0;
    uint32_t node_idx = 0;
    if (!find_course_map_node(world, course, &section, &node_idx, nullptr)) return;
    uint32_t course_slot = 0;
    uint32_t game = ld32(kMainGame);
    if (game == 0) return;
    st32(game + 0x19Cu, world);
    st32(game + 0x1A0u, section);
    st32(game + 0x1A4u, node_idx);
    st32(game + 0x1A8u, course);
    st32(game + 0x2Cu, ld32(game + 0x2Cu) | 8u);
    if (checkpoint_missing()) {
        uint8_t banner[3];
        uint8_t bw = 0;
        uint8_t bc = 0;
        if (!course_banner_state(banner, &bw, &bc)) {
            banner[0] = banner[1] = banner[2] = 0xFF;
        }
        uint32_t field_game = ld32(kFieldGame);
        if (field_game != 0) {
            uint32_t info = ld32(kCourseInfo);
            uint8_t file = info != 0 ? ld8(info + 0x12) : 0;
            write_checkpoint(field_game + 0x10, (int32_t)node_idx, world, course, file, banner);
        }
    }
    uint32_t mgr = ld32(kCourseSelectMgr);
    uint32_t path = mgr != 0 ? cs_get_path(mgr, world, section) : 0;
    uint32_t node = cs_get_node(path, node_idx);
    uint32_t save_mgr = ld32(kSaveMgr);
    uint32_t savedata = save_mgr != 0 ? ld32(save_mgr + 0x24u) : 0;
    if (in_mem2(savedata) && savedata + 0x10u + 3u * kSaveSlotSize < mem::kMem2End) {
        uint8_t file = ld8(savedata + 0x07u);
        if (file >= 3u) file = 0;
        uint32_t slot = savedata + 0x10u + (uint32_t)file * kSaveSlotSize;
        st8(slot + 0x04u, (uint8_t)(world + 1u));
        st8(slot + 0x05u, (uint8_t)section);
        st8(slot + 0x06u, (uint8_t)node_idx);
        st8(slot + 0x43u, (uint8_t)node_idx);
    }
    if (node != 0 && in_mem2(node)) {
        int32_t slot = (int32_t)ld32(node + 0x58u);
        if (slot >= 0) course_slot = (uint32_t)slot;
    }
    seed_player_map_fields(player, node, world, section, course_slot, pack_create_pos);
}

extern "C" bool nsmbu_apply_map_pose(int32_t world, int32_t section, int32_t node, int32_t course) {
    if (world < 0 || world > 8 || section < 0 || section > 1 || node < 0 || node > 255) return false;
    uint32_t game = ld32(kMainGame);
    if (game == 0 || !in_mem2(game)) return false;
    st32(game + 0x19Cu, (uint32_t)world);
    st32(game + 0x1A0u, (uint32_t)section);
    st32(game + 0x1A4u, (uint32_t)node);
    st32(game + 0x1A8u, (uint32_t)course);
    st32(game + 0x2Cu, ld32(game + 0x2Cu) | 8u);
    uint8_t banner[3] = {0xFF, 0xFF, 0xFF};
    if (course >= 0 && course <= 255) {
        int display = (int)course + 1;
        char buf[4];
        int n = snprintf(buf, sizeof buf, "%d", display);
        if (n >= 1 && n <= 3) {
            for (int i = 0; i < n; i++) {
                int d = buf[i] - '0';
                banner[i] = (d >= 1 && d <= 5) ? (uint8_t)(d - 1) : 0xFF;
            }
        }
    }
    uint32_t field_game = ld32(kFieldGame);
    if (field_game != 0 && in_mem2(field_game)) {
        uint32_t info = ld32(kCourseInfo);
        uint8_t file = info != 0 ? ld8(info + 0x12) : 0;
        uint8_t course_b = course < 0 ? 0 : (uint8_t)course;
        write_checkpoint(field_game + 0x10, node, (uint8_t)world, course_b, file, banner);
    }
    uint32_t save_mgr = ld32(kSaveMgr);
    uint32_t savedata = save_mgr != 0 ? ld32(save_mgr + 0x24u) : 0;
    if (in_mem2(savedata) && savedata + 0x10u + 3u * kSaveSlotSize < mem::kMem2End) {
        uint8_t file = ld8(savedata + 0x07u);
        if (file < 3u) {
            uint32_t slot = savedata + 0x10u + (uint32_t)file * kSaveSlotSize;
            st8(slot + 0x04u, (uint8_t)(world + 1));
            st8(slot + 0x05u, (uint8_t)section);
            st8(slot + 0x06u, (uint8_t)node);
            st8(slot + 0x43u, (uint8_t)node);
        }
    }
    return true;
}

extern "C" void nsmbu_banner_entry_arm(void) {}

extern "C" void nsmbu_fill_course_banner_digits(void) {}

extern "C" void hook_02994164(Cpu* c) {
    uint32_t factory = ld32(c->r[5] + 4);
    if (factory == GC(0x0202B4C8)) g_left_course = false;
    if (factory == GC(0x0298AD3C))
        LOG("[scene] FileSelect (controller %s)", input::pro_controller() ? "Pro" : "GamePad");
    else if (factory == GC(0x0202B570))
        LOG("[scene] empty save -> Begin Story Mode (controller %s)", input::pro_controller() ? "Pro" : "GamePad");
    else if (factory == GC(0x0202B86C))
        LOG("[scene] CourseSelect from save (controller %s)", input::pro_controller() ? "Pro" : "GamePad");
    nsmbu_note_scene_src(c->r[4], factory);
    f_02994164_orig(c);
    mods::packages::scene_change(factory);
}

extern "C" void hook_029CBBB4(Cpu*) {}

extern "C" void hook_024BD86C(Cpu* c) {
    if (mods::lua_sdk::story_leave_pending()) return;
    interp::begin_course_setup();
    f_024BD86C_orig(c);
    interp::end_course_setup();
}

extern "C" void hook_025CF020(Cpu* c) {
    if (ld32(kAreaTask) == 0) {
        c->r[3] = 0;
        return;
    }
    f_025CF020_orig(c);
}

extern "C" void hook_0252F900(Cpu* c) {
    if (ld32(kAreaTask) == 0) return;
    f_0252F900_orig(c);
}

extern "C" void hook_0252D9C0(Cpu* c) {
    if (ld32(kAreaTask) == 0) return;
    f_0252D9C0_orig(c);
}

static bool mii_model_drawable() {
    uint32_t central = ld32(kMiiCentral);
    if (!central) return false;
    return mii_model_draw_ready(central, ld32(central + kMiiShaderProgram));
}

extern "C" void hook_024EFE58(Cpu* c) {
    if (!mii_model_drawable()) return;
    f_024EFE58_orig(c);
}

extern "C" void hook_024F009C(Cpu* c) {
    if (!mii_model_drawable()) return;
    f_024F009C_orig(c);
}

extern "C" void nsmbu_mark_course_left(void) {
    g_left_course = true;
}

extern "C" void site_024B8324(Cpu* c) {
    nsmbu_mark_course_left();
    seed_map_return_pose(0, false);
}

extern "C" void hook_0226FBC4(Cpu* c) {
    uint32_t player = c->r[3];
    seed_map_return_pose(player, true);
    f_0226FBC4_orig(c);
    seed_map_return_pose(player, false);
}

extern "C" void site_0226FD14(Cpu* c) {
    seed_map_return_pose(c->r[30], true);
}

extern "C" void site_0226FECC(Cpu* c) {
    seed_map_return_pose(c->r[30], true);
}

extern "C" void site_02270310(Cpu* c) {
    seed_map_return_pose(c->r[30], true);
}

extern "C" void site_022703A4(Cpu* c) {
    seed_map_return_pose(c->r[30], true);
}

static void retarget_focus_node(Cpu* c, int copied_reg) {
    if (!g_left_course) return;
    seed_map_return_pose(0, false);
    uint8_t world = 0;
    uint8_t course = 0;
    if (!read_course_info(&world, &course) || !course_pair_ok(world, course)) return;
    uint32_t section = 0;
    uint32_t node_idx = 0;
    if (!find_course_map_node(world, course, &section, &node_idx, nullptr)) return;
    uint32_t mgr = ld32(kCourseSelectMgr);
    uint32_t path = mgr != 0 ? cs_get_path(mgr, world, section) : 0;
    uint32_t node = cs_get_node(path, node_idx);
    if (node == 0 || !in_mem2(node)) return;
    int32_t title_course = (int32_t)ld32(node + 0x50u);
    if (title_course < 0) return;
    c->r[3] = node;
    if (copied_reg >= 0) c->r[copied_reg] = (uint32_t)title_course;
}

extern "C" void site_0227CCF4(Cpu* c) {
    retarget_focus_node(c, 8);
}

extern "C" void site_0227E2A8(Cpu* c) {
    retarget_focus_node(c, 11);
}
