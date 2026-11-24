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
static const uint32_t kCourseSelectMap = GD(0x101CBD34);
static const uint32_t kFieldGame = GD(0x101D1604);
static const uint32_t kMainGame = GD(0x101E7D7C);
static const uint32_t kSaveMgr = GD(0x101E7DA4);
static const uint32_t kAglRenderer = GD(0x101E8A2C);
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
static bool g_directed_pose = false;
static bool g_have_pack = false;
static uint8_t g_directed_world = 0;
static uint8_t g_directed_course = 0;
static bool g_banner_entry_armed = false;

static bool map_pose_override_active() {
    return g_left_course || g_directed_pose;
}

static bool banner_digits_for_course(uint8_t course, uint8_t banner[3]) {
    int display = (int)course + 1;
    if (display < 1 || display > 999) return false;
    char buf[4];
    int n = snprintf(buf, sizeof(buf), "%d", display);
    if (n < 1 || n > 3) return false;
    banner[0] = banner[1] = banner[2] = 0xFF;
    for (int i = 0; i < n; i++) {
        int d = buf[i] - '0';
        banner[i] = (d >= 1 && d <= 5) ? (uint8_t)(d - 1) : 0xFF;
    }
    return true;
}

static void write_field_banner_digits(uint8_t course) {
    uint8_t banner[3];
    if (!banner_digits_for_course(course, banner)) return;
    uint32_t field_game = ld32(kFieldGame);
    if (field_game == 0 || !in_mem2(field_game)) return;
    const uint32_t base = field_game + 0x10u + 0x13Du;
    if (base + 3u > mem::kMem2End) return;
    for (int i = 0; i < 3; i++) st8(base + (uint32_t)i, banner[i]);
}

static bool override_course(uint8_t* world, uint8_t* course) {
    if (g_have_pack && (g_directed_pose || g_left_course)) {
        *world = g_directed_world;
        *course = g_directed_course;
        return course_pair_ok(*world, *course);
    }
    return read_course_info(world, course);
}

static bool opened_node(uint32_t* node_out) {
    uint8_t world = 0;
    uint8_t course = 0;
    if (!override_course(&world, &course)) return false;
    uint32_t section = 0;
    uint32_t node_idx = 0;
    if (!find_course_map_node(world, course, &section, &node_idx, nullptr)) return false;
    uint32_t mgr = ld32(kCourseSelectMgr);
    uint32_t path = mgr != 0 ? cs_get_path(mgr, world, section) : 0;
    uint32_t node = cs_get_node(path, node_idx);
    if (node == 0 || !in_mem2(node) || node + 0x60u >= mem::kMem2End) return false;
    if (node_out) *node_out = node;
    return true;
}

static int camera_rail_index(uint32_t world, uint32_t section, const char* want) {
    uint32_t mgr = ld32(kCourseSelectMgr);
    if (!in_mem2(mgr)) return -1;
    uint32_t idx = (world << 1) + section;
    if (idx >= 0x12u) return -1;
    uint32_t path = ld32(mgr + 0x18u + idx * 4u);
    if (!in_mem2(path)) return -1;
    uint32_t count = ld32(path + 0x1Cu);
    if (count > 64u) count = 64u;
    for (uint32_t i = 0; i < count; i++) {
        uint32_t entry = path + 0x28u + i * 0x448u;
        if (!in_mem2(entry) || entry + 0xC0u >= mem::kMem2End) break;
        auto name_eq = [](uint32_t ea, const char* s) {
            int len = 0;
            for (; s[len] != 0; len++) {
                if (!in_mem2(ea + (uint32_t)len) || ld8(ea + (uint32_t)len) != (uint8_t)s[len]) return false;
            }
            return in_mem2(ea + (uint32_t)len) && ld8(ea + (uint32_t)len) == 0;
        };
        uint32_t text = ld32(entry + 0xB4u);
        if (in_mem2(text) && name_eq(text, want)) return (int)i;
        if (name_eq(entry + 0xB4u, want)) return (int)i;
    }
    return -1;
}

static bool find_named_map_node(const char* want, uint32_t* node_out) {
    uint32_t mgr = ld32(kCourseSelectMgr);
    if (mgr == 0 || !in_mem2(mgr)) return false;
    for (uint32_t slot = 0; slot < 0x12u; slot++) {
        uint32_t path = ld32(mgr + 0x60u + slot * 4u);
        if (!in_mem2(path)) continue;
        for (uint32_t node_idx = 0; node_idx < 0x100u; node_idx++) {
            uint32_t node = cs_get_node(path, node_idx);
            if (!in_mem2(node) || node + kPathNodeSize > mem::kMem2End) break;
            if (!node_name_matches(node, want)) continue;
            if (node_out) *node_out = node;
            return true;
        }
    }
    return false;
}

static float guest_f(uint32_t ea) {
    return (float)ldf32(ea);
}

static void guest_set_f(uint32_t ea, float v) {
    stf32(ea, (double)v);
}

static bool world_coord(float v) {
    return v > -100000.f && v < 100000.f;
}

static float dist2(float ax, float ay, float az, float bx, float by, float bz) {
    float dx = ax - bx;
    float dy = ay - by;
    float dz = az - bz;
    return dx * dx + dy * dy + dz * dz;
}

static bool nearest_world_bone(float tx, float ty, float tz, float* ox, float* oy, float* oz) {
    uint32_t map = ld32(kCourseSelectMap);
    if (!in_mem2(map)) return false;
    uint32_t list = map + 0x20u;
    int models = (int)ld32(list + 0x08u);
    uint32_t tail = ld32(list + 0x04u);
    uint32_t off = ld32(list + 0x0Cu);
    if (models <= 0 || !in_mem2(tail) || off > tail) return false;
    uint32_t item = tail - off;
    uint32_t holder = in_mem2(item) ? ld32(item + 0x04u) : 0;
    uint32_t model = in_mem2(holder) ? ld32(holder) : 0;
    if (!in_mem2(model)) return false;
    uint32_t obj = ld32(model + 0x28u + 0x18u);
    uint32_t block = in_mem2(obj) ? ld32(obj + 0x10u) : 0;
    if (!in_mem2(block)) return false;
    float best = 1.0e30f;
    bool found = false;
    for (uint32_t i = 0; i < 120u; i++) {
        uint32_t mtx = block + i * 0x30u;
        if (!in_mem2(mtx) || mtx + 0x30u >= mem::kMem2End) break;
        float x = guest_f(mtx + 0x0Cu);
        float y = guest_f(mtx + 0x1Cu);
        float z = guest_f(mtx + 0x2Cu);
        if (!world_coord(x) || !world_coord(y) || !world_coord(z)) continue;
        float d = dist2(x, y, z, tx, ty, tz);
        if (d >= best) continue;
        best = d;
        *ox = x;
        *oy = y;
        *oz = z;
        found = true;
    }
    return found && best < 400.f * 400.f;
}

static void shift_quit_view() {
    uint32_t course_node = 0;
    uint32_t start_node = 0;
    if (!opened_node(&course_node)) return;
    if (!find_named_map_node("W2M0", &start_node)) return;
    const float cx = guest_f(course_node + 0x44u);
    const float cy = guest_f(course_node + 0x48u);
    const float cz = guest_f(course_node + 0x4Cu);
    const float sx = guest_f(start_node + 0x44u);
    const float sy = guest_f(start_node + 0x48u);
    const float sz = guest_f(start_node + 0x4Cu);
    if (!world_coord(cx) || !world_coord(cy) || !world_coord(cz)) return;
    if (!world_coord(sx) || !world_coord(sy) || !world_coord(sz)) return;
    const float dx = cx - sx;
    const float dy = cy - sy;
    const float dz = cz - sz;
    if (dx * dx + dy * dy + dz * dz < 1.f) return;

    uint32_t renderer = ld32(kAglRenderer);
    if (!in_mem2(renderer)) return;
    uint32_t count = ld32(renderer + 0xDA8u);
    uint32_t table = ld32(renderer + 0xDACu);
    if (count > 48u) count = 48u;
    if (!in_mem2(table)) return;

    const float ox = sx - cx;
    const float oy = sy - cy;
    const float oz = sz - cz;
    int shifted = 0;
    uint32_t seen[48];
    int nseen = 0;
    for (uint32_t i = 0; i < count; i++) {
        uint32_t layer = ld32(table + i * 4u);
        if (!in_mem2(layer)) continue;
        uint32_t cam = ld32(layer + 0x44u);
        if (!in_mem2(cam) || cam + 0x30u >= mem::kMem2End) continue;
        bool dup = false;
        for (int s = 0; s < nseen; s++) {
            if (seen[s] == cam) dup = true;
        }
        if (dup) continue;
        if (nseen < 48) seen[nseen++] = cam;
        const float m00 = guest_f(cam + 0x00u);
        const float m01 = guest_f(cam + 0x04u);
        const float m02 = guest_f(cam + 0x08u);
        const float m03 = guest_f(cam + 0x0Cu);
        const float m10 = guest_f(cam + 0x10u);
        const float m11 = guest_f(cam + 0x14u);
        const float m12 = guest_f(cam + 0x18u);
        const float m13 = guest_f(cam + 0x1Cu);
        const float m20 = guest_f(cam + 0x20u);
        const float m21 = guest_f(cam + 0x24u);
        const float m22 = guest_f(cam + 0x28u);
        if (!world_coord(m00) || !world_coord(m03) || !world_coord(m13)) continue;
        const float vx = m00 * sx + m01 * sy + m02 * sz + m03;
        const float vy = m10 * sx + m11 * sy + m12 * sz + m13;
        const float ux = m00 * cx + m01 * cy + m02 * cz + m03;
        const float uy = m10 * cx + m11 * cy + m12 * cz + m13;
        const float start_c = vx * vx + vy * vy;
        const float course_c = ux * ux + uy * uy;
        if (!(start_c + 100.f < course_c && start_c < 800.f * 800.f)) continue;
        const float rx = m00 * ox + m01 * oy + m02 * oz;
        const float ry = m10 * ox + m11 * oy + m12 * oz;
        const float rz = m20 * ox + m21 * oy + m22 * oz;
        guest_set_f(cam + 0x0Cu, m03 + rx);
        guest_set_f(cam + 0x1Cu, m13 + ry);
        guest_set_f(cam + 0x2Cu, guest_f(cam + 0x2Cu) + rz);
        shifted++;
    }
    for (uint32_t i = 0; i < count; i++) {
        uint32_t layer = ld32(table + i * 4u);
        if (!in_mem2(layer)) continue;
        uint32_t mgr = ld32(layer + 0x420u);
        if (!in_mem2(mgr)) continue;
        uint32_t nview = ld32(mgr + 0x4C4u);
        uint32_t views = ld32(mgr + 0x4C8u);
        if (nview == 0 || nview > 8u || !in_mem2(views)) continue;
        for (uint32_t v = 0; v < nview; v++) {
            uint32_t mtx = views + v * 0x80u + 0x04u;
            if (!in_mem2(mtx) || mtx + 0x30u >= mem::kMem2End) continue;
            const float m00 = guest_f(mtx + 0x00u);
            const float m01 = guest_f(mtx + 0x04u);
            const float m02 = guest_f(mtx + 0x08u);
            const float m03 = guest_f(mtx + 0x0Cu);
            const float m10 = guest_f(mtx + 0x10u);
            const float m11 = guest_f(mtx + 0x14u);
            const float m12 = guest_f(mtx + 0x18u);
            const float m13 = guest_f(mtx + 0x1Cu);
            const float m20 = guest_f(mtx + 0x20u);
            const float m21 = guest_f(mtx + 0x24u);
            const float m22 = guest_f(mtx + 0x28u);
            if (!world_coord(m00) || !world_coord(m03) || !world_coord(m13)) continue;
            const float vx = m00 * sx + m01 * sy + m02 * sz + m03;
            const float vy = m10 * sx + m11 * sy + m12 * sz + m13;
            const float ux = m00 * cx + m01 * cy + m02 * cz + m03;
            const float uy = m10 * cx + m11 * cy + m12 * cz + m13;
            const float start_c = vx * vx + vy * vy;
            const float course_c = ux * ux + uy * uy;
            if (!(start_c + 100.f < course_c && start_c < 800.f * 800.f)) continue;
            const float rx = m00 * ox + m01 * oy + m02 * oz;
            const float ry = m10 * ox + m11 * oy + m12 * oz;
            const float rz = m20 * ox + m21 * oy + m22 * oz;
            guest_set_f(mtx + 0x0Cu, m03 + rx);
            guest_set_f(mtx + 0x1Cu, m13 + ry);
            guest_set_f(mtx + 0x2Cu, guest_f(mtx + 0x2Cu) + rz);
            shifted++;
        }
    }
    float bx = cx;
    float by = cy;
    float bz = cz;
    if (nearest_world_bone(cx, cy, cz, &bx, &by, &bz) && 7u < count) {
        uint32_t layer = ld32(table + 7u * 4u);
        uint32_t mgr = in_mem2(layer) ? ld32(layer + 0x420u) : 0;
        uint32_t views = in_mem2(mgr) ? ld32(mgr + 0x4C8u) : 0;
        if (in_mem2(views) && views + 0x34u < mem::kMem2End) {
            uint32_t mtx = views + 0x04u;
            const float dx = bx - sx;
            const float dy = by - sy;
            const float dz = bz - sz;
            const float m00 = guest_f(mtx + 0x00u);
            const float m01 = guest_f(mtx + 0x04u);
            const float m02 = guest_f(mtx + 0x08u);
            const float m10 = guest_f(mtx + 0x10u);
            const float m11 = guest_f(mtx + 0x14u);
            const float m12 = guest_f(mtx + 0x18u);
            const float m20 = guest_f(mtx + 0x20u);
            const float m21 = guest_f(mtx + 0x24u);
            const float m22 = guest_f(mtx + 0x28u);
            guest_set_f(mtx + 0x0Cu, guest_f(mtx + 0x0Cu) - (m00 * dx + m01 * dy + m02 * dz));
            guest_set_f(mtx + 0x1Cu, guest_f(mtx + 0x1Cu) - (m10 * dx + m11 * dy + m12 * dz));
            guest_set_f(mtx + 0x2Cu, guest_f(mtx + 0x2Cu) - (m20 * dx + m21 * dy + m22 * dz));
            shifted++;
        }
    }
    float c7x = 0.f;
    float c7y = 0.f;
    float zcol = 0.f;
    if (7u < count) {
        uint32_t layer = ld32(table + 7u * 4u);
        uint32_t mgr = in_mem2(layer) ? ld32(layer + 0x420u) : 0;
        uint32_t views = in_mem2(mgr) ? ld32(mgr + 0x4C8u) : 0;
        if (in_mem2(views) && views + 0x34u < mem::kMem2End) {
            uint32_t mtx = views + 0x04u;
            c7x = guest_f(mtx + 0x00u) * cx + guest_f(mtx + 0x04u) * cy + guest_f(mtx + 0x08u) * cz + guest_f(mtx + 0x0Cu);
            c7y = guest_f(mtx + 0x10u) * cx + guest_f(mtx + 0x14u) * cy + guest_f(mtx + 0x18u) * cz + guest_f(mtx + 0x1Cu);
            zcol = guest_f(mtx + 0x08u);
        }
    }
    static int logged = 0;
    if (logged < 2) {
        logged++;
        int rail = camera_rail_index(1, 0, "W201");
        uint32_t tag = in_mem2(course_node) ? ld32(course_node + 0x5Cu) : 0;
        LOG("[map] quit view shifted %d rail %d tag %u course7=(%.1f, %.1f) zcol %.3f",
            shifted, rail, tag, c7x, c7y, zcol);
    }
}

static void write_map_camera_pose(uint32_t game, uint32_t world, uint32_t section, uint32_t node_idx) {
    if (game + 0x1ECu > mem::kMem2End) return;
    st32(game + 0x1ACu, world);
    st32(game + 0x1B0u, section);
    st32(game + 0x1B4u, node_idx);
    st32(game + 0x1C8u, world);
    st32(game + 0x1CCu, section);
    st32(game + 0x1D0u, node_idx);
    st32(game + 0x1D4u, world);
    st32(game + 0x1D8u, section);
    st32(game + 0x1DCu, node_idx);
    st32(game + 0x1E0u, world);
    st32(game + 0x1E4u, section);
    st32(game + 0x1E8u, node_idx);
}

static void seed_map_return_pose(uint32_t player, bool pack_create_pos) {
    if (!map_pose_override_active()) return;
    uint8_t world = 0;
    uint8_t course = 0;
    if (!override_course(&world, &course)) return;
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
    write_map_camera_pose(game, world, section, node_idx);
    int rail = camera_rail_index(world, section, "W201");
    if (rail >= 0) st32(game + 0x1B4u, (uint32_t)rail);
    st32(game + 0x2Cu, ld32(game + 0x2Cu) | 8u);
    {
        uint8_t banner[3];
        uint8_t bw = 0;
        uint8_t bc = 0;
        if (!course_banner_state(banner, &bw, &bc)) {
            banner[0] = banner[1] = banner[2] = 0xFF;
        }
        uint32_t field_game = ld32(kFieldGame);
        if (field_game != 0 && (checkpoint_missing() || map_pose_override_active())) {
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
    write_map_camera_pose(game, (uint32_t)world, (uint32_t)section, (uint32_t)node);
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

extern "C" void nsmbu_banner_entry_arm(void) {
    g_banner_entry_armed = true;
}

extern "C" void nsmbu_fill_course_banner_digits(void) {
    if (!g_banner_entry_armed) return;
    g_banner_entry_armed = false;
    uint8_t world = 0;
    uint8_t course = 0;
    if (!override_course(&world, &course)) return;
    write_field_banner_digits(course);
}

extern "C" void nsmbu_directed_open(uint8_t world, uint8_t course) {
    if (!course_pair_ok(world, course)) return;
    g_directed_world = world;
    g_directed_course = course;
    g_directed_pose = true;
    g_have_pack = true;
    uint32_t game = ld32(kMainGame);
    if (game != 0 && in_mem2(game)) {
        st32(game + 0x19Cu, world);
        st32(game + 0x1A8u, course);
    }
    write_field_banner_digits(course);
    if (g_banner_entry_armed) nsmbu_fill_course_banner_digits();
}

extern "C" void nsmbu_map_pose_tick(void) {
    if (!g_directed_pose) return;
    if (ld32(kAreaTask) != 0) return;
    seed_map_return_pose(0, false);
    if (!g_left_course) return;
    static int logged = 0;
    if (logged >= 1) return;
    uint32_t course_node = 0;
    uint32_t start_node = 0;
    if (!opened_node(&course_node) || !find_named_map_node("W2M0", &start_node)) return;
    uint32_t renderer = ld32(kAglRenderer);
    if (!in_mem2(renderer)) return;
    uint32_t count = ld32(renderer + 0xDA8u);
    uint32_t table = ld32(renderer + 0xDACu);
    if (7u >= count || !in_mem2(table)) return;
    (void)count;
    (void)table;
    logged++;
    uint32_t map = ld32(kCourseSelectMap);
    if (!in_mem2(map)) {
        LOG("[map] no course select map");
        return;
    }
    uint32_t list = map + 0x20u;
    int models = (int)ld32(list + 0x08u);
    uint32_t tail = ld32(list + 0x04u);
    uint32_t off = ld32(list + 0x0Cu);
    LOG("[map] world2 models %d tail %08X off %X", models, tail, off);
    if (models <= 0 || !in_mem2(tail) || off > tail) return;
    uint32_t item = tail - off;
    uint32_t holder = in_mem2(item) ? ld32(item + 0x04u) : 0;
    uint32_t model = in_mem2(holder) ? ld32(holder) : 0;
    LOG("[map] model %08X holder %08X", model, holder);
    if (!in_mem2(model) || model + 0x40u >= mem::kMem2End) return;
    uint32_t obj = ld32(model + 0x28u + 0x18u);
    uint32_t block = in_mem2(obj) ? ld32(obj + 0x10u) : 0;
    LOG("[map] g3d %08X block %08X", obj, block);
    if (!in_mem2(block)) return;
    int hits = 0;
    for (uint32_t i = 0; i < 120u && hits < 24; i++) {
        uint32_t mtx = block + i * 0x30u;
        if (mtx + 0x30u >= mem::kMem2End) break;
        float x = guest_f(mtx + 0x0Cu);
        float y = guest_f(mtx + 0x1Cu);
        float z = guest_f(mtx + 0x2Cu);
        bool band = (z > 400.f && z < 500.f) || (z > 700.f && z < 800.f);
        if (!band || !world_coord(x) || !world_coord(y)) continue;
        LOG("[map] bone %u (%.0f, %.0f, %.0f) fwd (%.2f, %.2f, %.2f)", i, x, y, z,
            guest_f(mtx + 0x08u), guest_f(mtx + 0x18u), guest_f(mtx + 0x28u));
        hits++;
    }
    if (hits == 0) LOG("[map] no bone near the course");
}

extern "C" void nsmbu_align_map_camera(void) {
    if (!g_left_course || ld32(kAreaTask) != 0) return;
    seed_map_return_pose(0, false);
    shift_quit_view();
}

extern "C" void hook_02994164(Cpu* c) {
    uint32_t factory = ld32(c->r[5] + 4);
    if (factory == GC(0x0202B4C8)) {
        g_left_course = false;
        g_directed_pose = false;
    }
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

extern "C" void hook_02632418(Cpu*) {}

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
    if (!map_pose_override_active()) return;
    seed_map_return_pose(0, false);
    uint32_t node = 0;
    if (!opened_node(&node)) return;
    c->r[3] = node;
    if (copied_reg >= 0) c->r[copied_reg] = ld32(node + 0x50u);
}

extern "C" void site_0227CCF4(Cpu* c) {
    retarget_focus_node(c, 8);
}

extern "C" void site_0227E2A8(Cpu* c) {
    retarget_focus_node(c, 11);
}
