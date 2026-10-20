#include "core/runtime.h"
#include "mods/packages.h"
#include "mods/sdk/nsmbu.h"
#include "guest_addr.h"

#include <cstdio>
#include <cstring>
#include <string>

extern "C" void f_02994164_orig(Cpu* c);
extern "C" void nsmbu_note_scene_src(uint32_t src, uint32_t factory);
extern "C" void f_024BAD4C_orig(Cpu* c);
extern "C" void f_024BD86C_orig(Cpu* c);
extern "C" void f_025D8B28_orig(Cpu* c);
extern "C" void f_02672C90_orig(Cpu* c);
extern "C" void f_02681BFC_orig(Cpu* c);
extern "C" void f_0267CB3C_orig(Cpu* c);
extern "C" void f_0252F900_orig(Cpu* c);
extern "C" void f_025CF020_orig(Cpu* c);

namespace threads {
Cpu* current();
}

static const uint32_t kAreaTask = GD(0x101D1598);
static const uint32_t kCourseInfo = GD(0x101D18EC);
static const uint32_t kCourseTask = GD(0x101D15F0);
static const uint32_t kFieldGame = GD(0x101D1604);
static const uint32_t kMainGame = GD(0x101E7D7C);

static uint32_t g_world_course_textbox = 0;
static uint32_t g_world_course_multi2d = 0;

static void write_three_glyphs(uint32_t base, const uint8_t g[3]) {
    if (base == 0) return;
    for (int i = 0; i < 3; i++) st8(base + (uint32_t)i, g[i]);
}

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

static void seed_maingame_course_tags(uint8_t world, uint8_t course) {
    uint32_t game = ld32(kMainGame);
    if (game == 0) return;
    st32(game + 0x19Cu, world);
    st32(game + 0x1A8u, course);
}

static bool in_mem2(uint32_t ea) {
    return ea >= mem::kMem2Start && ea < mem::kMem2End;
}

static std::string textbox_name_string(uint32_t textbox) {
    if (!in_mem2(textbox) || textbox + 0xACu > mem::kMem2End) return {};
    uint32_t name_p = ld32(textbox + 0xA8);
    if (!in_mem2(name_p)) return {};
    uint8_t b0 = ld8(name_p);
    if (b0 >= 0x20 && b0 < 0x7F) return mem::read_cstr(name_p);
    if (name_p + 8u > mem::kMem2End) return {};
    uint32_t q = ld32(name_p + 4);
    if (in_mem2(q) && ld8(q) >= 0x20 && ld8(q) < 0x7F) return mem::read_cstr(q);
    q = ld32(name_p);
    if (in_mem2(q) && ld8(q) >= 0x20 && ld8(q) < 0x7F) return mem::read_cstr(q);
    return {};
}

static void reapply_world_course_textbox(Cpu* c) {
    if (c == nullptr) c = threads::current();
    if (c == nullptr) return;
    uint8_t world = 0;
    uint8_t course = 0;
    if (read_course_info(&world, &course)) seed_maingame_course_tags(world, course);
    uint32_t task = ld32(kCourseTask);
    uint32_t pregame = task != 0 ? ld32(task + 0xCC) : 0;
    uint32_t multi2d = in_mem2(pregame) ? ld32(pregame) : g_world_course_multi2d;
    uint32_t textbox = g_world_course_textbox;
    if (textbox == 0 && in_mem2(pregame) && pregame + 0x800u <= mem::kMem2End) {
        for (uint32_t off = 0; off < 0x800; off += 4) {
            uint32_t cand = ld32(pregame + off);
            if (!in_mem2(cand)) continue;
            std::string name = textbox_name_string(cand);
            if (name.find("WorldCourse") == std::string::npos) continue;
            textbox = cand;
            g_world_course_textbox = cand;
            g_world_course_multi2d = multi2d;
            static int find_logged = 0;
            if (find_logged++ < 4)
                LOG("[banner-text] scan hit off=%03X tb=%08X name=%s course=%u", off, cand, name.c_str(),
                    course);
            break;
        }
    }
    if (textbox == 0) return;
    static int logged = 0;
    if (logged++ < 8)
        LOG("[banner-text] reapply tb=%08X m2d=%08X course=%u", textbox, multi2d, course);
    uint32_t saved[6] = {c->r[3], c->r[4], c->r[5], c->r[6], c->r[7], c->r[8]};
    c->r[3] = textbox;
    c->r[4] = 0;
    f_02681BFC_orig(c);
    c->r[3] = saved[0];
    c->r[4] = saved[1];
    c->r[5] = saved[2];
    c->r[6] = saved[3];
    c->r[7] = saved[4];
    c->r[8] = saved[5];
}

static void fill_course_banner_digits(Cpu* c, uint32_t glyph_base, bool refresh) {
    if (mods::lua_sdk::story_leave_pending()) return;
    uint8_t g[3];
    uint8_t world = 0;
    uint8_t course = 0;
    if (!course_banner_state(g, &world, &course)) return;
    seed_maingame_course_tags(world, course);
    if (glyph_base != 0) {
        write_three_glyphs(glyph_base, g);
        write_three_glyphs(glyph_base + 0x12D, g);
    }
    uint32_t task = ld32(kCourseTask);
    if (task != 0) {
        write_three_glyphs(task + 0x134, g);
        write_three_glyphs(task + 0x261, g);
    }
    uint32_t field_game = ld32(kFieldGame);
    if (field_game != 0) {
        uint32_t data = field_game + 0x10;
        write_three_glyphs(data + 0x10, g);
        write_three_glyphs(data + 0x13D, g);
    }
    if (refresh) reapply_world_course_textbox(c != nullptr ? c : threads::current());
}

extern "C" void nsmbu_fill_course_banner_digits(void) {
    fill_course_banner_digits(threads::current(), 0, true);
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

extern "C" void hook_02994164(Cpu* c) {
    uint32_t factory = ld32(c->r[5] + 4);
    nsmbu_note_scene_src(c->r[4], factory);
    f_02994164_orig(c);
    mods::packages::scene_change(factory);
}

extern "C" void hook_029CBBB4(Cpu*) {}

extern "C" void hook_024BD86C(Cpu* c) {
    if (mods::lua_sdk::story_leave_pending() && ld32(kCourseTask) != 0) return;
    f_024BD86C_orig(c);
}

extern "C" void hook_024BAD4C(Cpu* c) {
    uint32_t dest = c->r[3];
    f_024BAD4C_orig(c);
    fill_course_banner_digits(c, dest + 0x10, true);
}

extern "C" void site_024BD96C(Cpu* c) {
    uint32_t task = ld32(kCourseTask);
    uint32_t base = task != 0 ? task + 0x134 : c->r[31] + 0x134;
    fill_course_banner_digits(c, base, true);
}

extern "C" void site_024BE280(Cpu* c) {
    fill_course_banner_digits(c, 0, true);
}

extern "C" void hook_025D8B28(Cpu* c) {
    fill_course_banner_digits(c, 0, false);
    f_025D8B28_orig(c);
    reapply_world_course_textbox(c);
}

extern "C" void hook_0267CB3C(Cpu* c) {
    if (mods::lua_sdk::story_leave_pending()) return;
    uint8_t course = 0;
    uint32_t before = c->r[4];
    if (read_course_info(nullptr, &course)) {
        if (before == 0xFFFFFFFFu || (int32_t)before < 0)
            c->r[4] = course;
        else if (course <= 11)
            c->r[4] = course;
    }
    static int logged = 0;
    if (logged++ < 40)
        LOG("[banner-cb3c] in=%u out=%u course=%u lr=%08X", before, c->r[4], course, c->lr);
    f_0267CB3C_orig(c);
}

extern "C" void hook_02681BFC(Cpu* c) {
    if (mods::lua_sdk::story_leave_pending()) return;
    uint32_t textbox = c->r[3];
    uint8_t world = 0;
    uint8_t course = 0;
    if (read_course_info(&world, &course)) seed_maingame_course_tags(world, course);
    f_02681BFC_orig(c);
    std::string name = textbox_name_string(textbox);
    static int logged = 0;
    if (logged++ < 32)
        LOG("[banner-text] expand tb=%08X course=%u lr=%08X name=%s", textbox, course, c->lr,
            name.empty() ? "?" : name.c_str());
    if (name.find("WorldCourse") != std::string::npos) {
        g_world_course_textbox = textbox;
        uint32_t task = ld32(kCourseTask);
        if (task != 0) {
            uint32_t pregame = ld32(task + 0xCC);
            if (pregame != 0) g_world_course_multi2d = ld32(pregame);
        }
        static int cap = 0;
        if (cap++ < 4)
            LOG("[banner-text] capture name=%s tb=%08X m2d=%08X course=%u", name.c_str(), textbox,
                g_world_course_multi2d, course);
    }
}

extern "C" void hook_02672C90(Cpu* c) {
    uint32_t multi2d = c->r[3];
    uint32_t textbox = c->r[4];
    uint8_t world = 0;
    uint8_t course = 0;
    if (read_course_info(&world, &course)) seed_maingame_course_tags(world, course);
    f_02672C90_orig(c);
    if (textbox_name_string(textbox).find("WorldCourse") != std::string::npos) {
        g_world_course_textbox = textbox;
        g_world_course_multi2d = multi2d;
    }
}
