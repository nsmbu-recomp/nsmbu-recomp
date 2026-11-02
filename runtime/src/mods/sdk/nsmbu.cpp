#include "nsmbu.h"
#include "mods/lua_runtime.h"
#include "mods/lua_guest.h"
#include "mods/lua_gui.h"
#include "mods/lua_press.h"
#include "mods/packages.h"
#include "guest_addr.h"
#include "input/input.h"
#include "core/runtime.h"
#include "gfx/renderer.h"
#include <atomic>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <map>
#include <mutex>
#include <string>
#include <vector>

extern "C" {
#include "lua.h"
#include "lauxlib.h"
#include "lualib.h"
}

extern "C" void nsmbu_fill_course_banner_digits(void);
extern "C" void nsmbu_banner_entry_arm(void);
extern "C" void nsmbu_mark_course_left(void);

namespace mods::lua_sdk {
std::atomic<bool> g_story_leave_queued{false};
std::atomic<uint64_t> g_story_load_generation{0};
namespace {
using lua_mod::Instance;
using json::Value;

Instance* self(lua_State* L) {
    return static_cast<Instance*>(lua_touserdata(L, lua_upvalueindex(1)));
}

int fail(lua_State* L, const char* msg) {
    lua_pushnil(L);
    lua_pushstring(L, msg);
    return 2;
}

bool push_loaded(lua_State* L, const char* name) {
    lua_getglobal(L, "package");
    if (!lua_istable(L, -1)) {
        lua_pop(L, 1);
        lua_pushnil(L);
        return false;
    }
    lua_getfield(L, -1, "loaded");
    lua_remove(L, -2);
    if (!lua_istable(L, -1)) {
        lua_pop(L, 1);
        lua_pushnil(L);
        return false;
    }
    lua_getfield(L, -1, name);
    lua_remove(L, -2);
    if (!lua_istable(L, -1)) {
        lua_pop(L, 1);
        lua_pushnil(L);
        return false;
    }
    return true;
}

void set_loaded(lua_State* L, const char* name) {
    lua_getglobal(L, "package");
    lua_getfield(L, -1, "loaded");
    lua_pushvalue(L, -3);
    lua_setfield(L, -2, name);
    lua_pop(L, 3);
}

void refresh_package(Instance& inst) {
    lua_State* L = static_cast<lua_State*>(inst.state);
    if (!L) return;
    if (!push_loaded(L, "nsmbu")) {
        lua_pop(L, 1);
        return;
    }
    lua_getfield(L, -1, "Package");
    if (!lua_istable(L, -1)) {
        lua_pop(L, 1);
        lua_newtable(L);
        lua_pushvalue(L, -1);
        lua_setfield(L, -3, "Package");
    }
    lua_pushlstring(L, inst.id.data(), inst.id.size());
    lua_setfield(L, -2, "id");
    lua_pushlstring(L, inst.version.data(), inst.version.size());
    lua_setfield(L, -2, "version");
    lua_pushlstring(L, inst.path.data(), inst.path.size());
    lua_setfield(L, -2, "path");
    lua_pop(L, 2);
}

void bind(lua_State* L, Instance* inst, const char* name, lua_CFunction fn) {
    lua_pushlightuserdata(L, inst);
    lua_pushcclosure(L, fn, 1);
    lua_setfield(L, -2, name);
}

void bind_guest(lua_State* L, const char* name, lua_CFunction fn) {
    lua_pushcfunction(L, fn);
    lua_setfield(L, -2, name);
}

void set_empty_module(lua_State* L, const char* name) {
    lua_newtable(L);
    set_loaded(L, name);
}

void attach_loaded(lua_State* L, const char* name, const char* field) {
    if (push_loaded(L, name)) lua_setfield(L, -2, field);
    else lua_pop(L, 1);
}

int l_log(lua_State* L) {
    Instance* inst = self(L);
    if (!lua_isstring(L, 1)) return fail(L, "message must be a string");
    LOG("[mod:%s] %s", inst->id.c_str(), lua_tostring(L, 1));
    lua_pushboolean(L, 1);
    return 1;
}

int l_log_int(lua_State* L) {
    Instance* inst = self(L);
    if (!lua_isstring(L, 1)) return fail(L, "label must be a string");
    if (!lua_isnumber(L, 2)) return fail(L, "value must be a number");
    LOG("[mod:%s] %s %lld", inst->id.c_str(), lua_tostring(L, 1),
        (long long)lua_tointeger(L, 2));
    lua_pushboolean(L, 1);
    return 1;
}

int l_log_hex(lua_State* L) {
    Instance* inst = self(L);
    if (!lua_isstring(L, 1)) return fail(L, "label must be a string");
    if (!lua_isnumber(L, 2)) return fail(L, "value must be a number");
    LOG("[mod:%s] %s %08X", inst->id.c_str(), lua_tostring(L, 1),
        (unsigned)(uint32_t)lua_tointeger(L, 2));
    lua_pushboolean(L, 1);
    return 1;
}

int l_log_float(lua_State* L) {
    Instance* inst = self(L);
    if (!lua_isstring(L, 1)) return fail(L, "label must be a string");
    if (!lua_isnumber(L, 2)) return fail(L, "value must be a number");
    LOG("[mod:%s] %s %.17g", inst->id.c_str(), lua_tostring(L, 1),
            (double)lua_tonumber(L, 2));
    lua_pushboolean(L, 1);
    return 1;
}

int l_status(lua_State* L) {
    Instance* inst = self(L);
    if (!lua_isstring(L, 1)) return fail(L, "message must be a string");
    size_t len = 0;
    const char* msg = lua_tolstring(L, 1, &len);
    if (len > 1024) len = 1024;
    inst->status_line.assign(msg, len);
    lua_pushboolean(L, 1);
    return 1;
}

int l_logic_step(lua_State* L) {
    Instance* inst = self(L);
    lua_pushnumber(L, (lua_Number)inst->step);
    return 1;
}

int l_logic_dt(lua_State* L) {
    lua_pushnumber(L, 1.0 / 60.0);
    return 1;
}

int l_config_string(lua_State* L) {
    Instance* inst = self(L);
    const char* id = luaL_checkstring(L, 1);
    if (!lua_isstring(L, 2)) return fail(L, "fallback must be a string");
    const auto& v = inst->config.get(id);
    if (v.type == Value::Null || v.type != Value::String) {
        lua_pushvalue(L, 2);
        return 1;
    }
    lua_pushlstring(L, v.text.data(), v.text.size());
    return 1;
}

int l_config_number(lua_State* L) {
    Instance* inst = self(L);
    const char* id = luaL_checkstring(L, 1);
    if (!lua_isnumber(L, 2)) return fail(L, "fallback must be a number");
    const auto& v = inst->config.get(id);
    if (v.type == Value::Null || v.type != Value::Number) {
        lua_pushvalue(L, 2);
        return 1;
    }
    lua_pushnumber(L, v.number);
    return 1;
}

int l_config_bool(lua_State* L) {
    Instance* inst = self(L);
    const char* id = luaL_checkstring(L, 1);
    if (!lua_isboolean(L, 2)) return fail(L, "fallback must be a boolean");
    const auto& v = inst->config.get(id);
    if (v.type == Value::Null || v.type != Value::Bool) {
        lua_pushvalue(L, 2);
        return 1;
    }
    lua_pushboolean(L, v.boolean);
    return 1;
}

uint32_t addr_arg(lua_State* L, int idx) { return (uint32_t)(int64_t)luaL_checknumber(L, idx); }

int guest_read_u8(lua_State* L) {
    std::string err;
    uint8_t v;
    if (!lua_guest::read_u8(addr_arg(L, 1), v, err)) return fail(L, err.c_str());
    lua_pushinteger(L, v);
    return 1;
}
int guest_read_s8(lua_State* L) {
    std::string err;
    int8_t v;
    if (!lua_guest::read_s8(addr_arg(L, 1), v, err)) return fail(L, err.c_str());
    lua_pushinteger(L, v);
    return 1;
}
int guest_read_u16(lua_State* L) {
    std::string err;
    uint16_t v;
    if (!lua_guest::read_u16(addr_arg(L, 1), v, err)) return fail(L, err.c_str());
    lua_pushinteger(L, v);
    return 1;
}
int guest_read_s16(lua_State* L) {
    std::string err;
    int16_t v;
    if (!lua_guest::read_s16(addr_arg(L, 1), v, err)) return fail(L, err.c_str());
    lua_pushinteger(L, v);
    return 1;
}
int guest_read_u32(lua_State* L) {
    std::string err;
    uint32_t v;
    if (!lua_guest::read_u32(addr_arg(L, 1), v, err)) return fail(L, err.c_str());
    lua_pushnumber(L, (lua_Number)v);
    return 1;
}
int guest_read_s32(lua_State* L) {
    std::string err;
    int32_t v;
    if (!lua_guest::read_s32(addr_arg(L, 1), v, err)) return fail(L, err.c_str());
    lua_pushnumber(L, (lua_Number)v);
    return 1;
}
int guest_read_f32(lua_State* L) {
    std::string err;
    float v;
    if (!lua_guest::read_f32(addr_arg(L, 1), v, err)) return fail(L, err.c_str());
    lua_pushnumber(L, v);
    return 1;
}
int guest_read_f64(lua_State* L) {
    std::string err;
    double v;
    if (!lua_guest::read_f64(addr_arg(L, 1), v, err)) return fail(L, err.c_str());
    lua_pushnumber(L, v);
    return 1;
}
int guest_read(lua_State* L) {
    uint32_t addr = addr_arg(L, 1);
    size_t size = (size_t)luaL_checkinteger(L, 2);
    std::string err, out;
    if (!lua_guest::read_bytes(addr, size, out, err)) return fail(L, err.c_str());
    lua_pushlstring(L, out.data(), out.size());
    return 1;
}

int guest_write_u8(lua_State* L) {
    std::string err;
    if (!lua_guest::write_u8(addr_arg(L, 1), (uint8_t)(uint32_t)luaL_checknumber(L, 2), err))
        return fail(L, err.c_str());
    lua_pushboolean(L, 1);
    return 1;
}
int guest_write_s8(lua_State* L) {
    std::string err;
    if (!lua_guest::write_s8(addr_arg(L, 1), (int8_t)(int32_t)luaL_checknumber(L, 2), err))
        return fail(L, err.c_str());
    lua_pushboolean(L, 1);
    return 1;
}
int guest_write_u16(lua_State* L) {
    std::string err;
    if (!lua_guest::write_u16(addr_arg(L, 1), (uint16_t)(uint32_t)luaL_checknumber(L, 2), err))
        return fail(L, err.c_str());
    lua_pushboolean(L, 1);
    return 1;
}
int guest_write_s16(lua_State* L) {
    std::string err;
    if (!lua_guest::write_s16(addr_arg(L, 1), (int16_t)(int32_t)luaL_checknumber(L, 2), err))
        return fail(L, err.c_str());
    lua_pushboolean(L, 1);
    return 1;
}
int guest_write_u32(lua_State* L) {
    std::string err;
    if (!lua_guest::write_u32(addr_arg(L, 1), (uint32_t)(int64_t)luaL_checknumber(L, 2), err))
        return fail(L, err.c_str());
    lua_pushboolean(L, 1);
    return 1;
}
int guest_write_s32(lua_State* L) {
    std::string err;
    if (!lua_guest::write_s32(addr_arg(L, 1), (int32_t)(int64_t)luaL_checknumber(L, 2), err))
        return fail(L, err.c_str());
    lua_pushboolean(L, 1);
    return 1;
}
int guest_write_f32(lua_State* L) {
    std::string err;
    if (!lua_guest::write_f32(addr_arg(L, 1), (float)luaL_checknumber(L, 2), err))
        return fail(L, err.c_str());
    lua_pushboolean(L, 1);
    return 1;
}
int guest_write_f64(lua_State* L) {
    std::string err;
    if (!lua_guest::write_f64(addr_arg(L, 1), (double)luaL_checknumber(L, 2), err))
        return fail(L, err.c_str());
    lua_pushboolean(L, 1);
    return 1;
}
int guest_write(lua_State* L) {
    uint32_t addr = addr_arg(L, 1);
    size_t len = 0;
    const char* data = luaL_checklstring(L, 2, &len);
    std::string err;
    if (!lua_guest::write_bytes(addr, data, len, err)) return fail(L, err.c_str());
    lua_pushboolean(L, 1);
    return 1;
}

bool button_bit(const char* name, uint32_t& bit) {
    static const struct { const char* name; uint32_t bit; } kTable[] = {
        {"A", input::kA},       {"B", input::kB},       {"X", input::kX},
        {"Y", input::kY},       {"L", input::kL},       {"R", input::kR},
        {"ZL", input::kZL},     {"ZR", input::kZR},     {"Plus", input::kPlus},
        {"Minus", input::kMinus}, {"Home", input::kHome},
        {"Up", input::kUp},     {"Down", input::kDown},
        {"Left", input::kLeft}, {"Right", input::kRight},
        {"StickL", input::kStickL}, {"StickR", input::kStickR},
    };
    for (const auto& entry : kTable) {
        if (std::strcmp(entry.name, name) != 0) continue;
        bit = entry.bit;
        return true;
    }
    return false;
}

bool button_mask(lua_State* L, int arg, uint32_t& mask) {
    if (lua_type(L, arg) == LUA_TSTRING) {
        uint32_t bit = 0;
        if (!button_bit(lua_tostring(L, arg), bit)) return false;
        mask |= bit;
        return true;
    }
    if (lua_type(L, arg) != LUA_TTABLE) return false;
    lua_pushnil(L);
    while (lua_next(L, arg) != 0) {
        uint32_t bit = 0;
        if (lua_type(L, -1) != LUA_TSTRING || !button_bit(lua_tostring(L, -1), bit)) {
            lua_pop(L, 2);
            return false;
        }
        mask |= bit;
        lua_pop(L, 1);
    }
    return true;
}

int l_input_press(lua_State* L) {
    Instance* inst = self(L);
    uint32_t mask = 0;
    if (!button_mask(L, 1, mask)) return fail(L, "bad type");
    if (!lua_isnumber(L, 2)) return fail(L, "steps must be a number");
    double steps = lua_tonumber(L, 2);
    if (steps < 1) steps = 1;
    if (steps > 600) steps = 600;
    lua_press::press(inst, mask, (uint64_t)steps, inst->step);
    lua_pushboolean(L, 1);
    return 1;
}

struct InputSample {
    uint64_t step = UINT64_MAX;
    input::PadState pad;
    uint32_t previous = 0;
};

std::mutex g_input_mu;
std::map<const Instance*, InputSample> g_input_samples;

input::PadState sample_input(Instance* inst, uint32_t& previous) {
    std::lock_guard guard(g_input_mu);
    InputSample& sample = g_input_samples[inst];
    input::PadState now = input::read();
    now.buttons |= lua_press::active(inst->step);
    if (sample.step != inst->step) {
        sample.previous = sample.pad.buttons;
        sample.pad = now;
        sample.step = inst->step;
    }
    previous = sample.previous;
    return sample.pad;
}

bool input_button(lua_State* L, uint32_t& bit) {
    if (!lua_isstring(L, 1)) return false;
    return button_bit(lua_tostring(L, 1), bit);
}

int l_input_call(lua_State* L) {
    Instance* inst = self(L);
    uint32_t previous = 0;
    const input::PadState pad = sample_input(inst, previous);
    lua_newtable(L);
    lua_pushnumber(L, (lua_Number)pad.buttons);
    lua_setfield(L, -2, "buttons");
    lua_pushnumber(L, (lua_Number)pad.lx);
    lua_setfield(L, -2, "lx");
    lua_pushnumber(L, (lua_Number)pad.ly);
    lua_setfield(L, -2, "ly");
    lua_pushnumber(L, (lua_Number)pad.rx);
    lua_setfield(L, -2, "rx");
    lua_pushnumber(L, (lua_Number)pad.ry);
    lua_setfield(L, -2, "ry");
    lua_pushboolean(L, pad.touch);
    lua_setfield(L, -2, "touch");
    lua_pushnumber(L, (lua_Number)pad.tx);
    lua_setfield(L, -2, "tx");
    lua_pushnumber(L, (lua_Number)pad.ty);
    lua_setfield(L, -2, "ty");
    return 1;
}

int l_input_held(lua_State* L) {
    Instance* inst = self(L);
    uint32_t bit = 0;
    if (!input_button(L, bit)) return fail(L, "bad type");
    uint32_t previous = 0;
    const input::PadState pad = sample_input(inst, previous);
    lua_pushboolean(L, (pad.buttons & bit) != 0);
    return 1;
}

int l_input_pressed(lua_State* L) {
    Instance* inst = self(L);
    uint32_t bit = 0;
    if (!input_button(L, bit)) return fail(L, "bad type");
    uint32_t previous = 0;
    const input::PadState pad = sample_input(inst, previous);
    lua_pushboolean(L, (pad.buttons & bit) != 0 && (previous & bit) == 0);
    return 1;
}

int l_gui_clear(lua_State* L) {
    Instance* inst = self(L);
    lua_gui::clear(inst->id);
    lua_pushboolean(L, 1);
    return 1;
}

bool gui_number(lua_State* L, int arg, float& out) {
    if (!lua_isnumber(L, arg)) return false;
    out = (float)lua_tonumber(L, arg);
    return true;
}

bool gui_optional(lua_State* L, int arg, float& out, float fallback) {
    if (lua_isnoneornil(L, arg)) {
        out = fallback;
        return true;
    }
    return gui_number(L, arg, out);
}

bool gui_color(lua_State* L, int arg, uint32_t& out) {
    if (!lua_isnumber(L, arg)) return false;
    out = (uint32_t)lua_tonumber(L, arg);
    return true;
}

int l_gui_rect(lua_State* L) {
    Instance* inst = self(L);
    float x = 0, y = 0, w = 0, h = 0, thickness = 2, rounding = 0;
    uint32_t color = 0;
    if (!gui_number(L, 1, x) || !gui_number(L, 2, y) || !gui_number(L, 3, w) || !gui_number(L, 4, h) ||
        !gui_color(L, 5, color))
        return fail(L, "bad type");
    if (!gui_optional(L, 6, thickness, 2) || !gui_optional(L, 7, rounding, 0)) return fail(L, "bad type");
    if (w < 0) {
        x += w;
        w = -w;
    }
    if (h < 0) {
        y += h;
        h = -h;
    }
    lua_gui::Command c;
    c.kind = lua_gui::Kind::Rect;
    c.x0 = x;
    c.y0 = y;
    c.x1 = x + w;
    c.y1 = y + h;
    c.color = color;
    c.thickness = thickness;
    c.rounding = rounding;
    lua_gui::push(inst->id, c);
    lua_pushboolean(L, 1);
    return 1;
}

int l_gui_rect_filled(lua_State* L) {
    Instance* inst = self(L);
    float x = 0, y = 0, w = 0, h = 0, rounding = 0;
    uint32_t color = 0;
    if (!gui_number(L, 1, x) || !gui_number(L, 2, y) || !gui_number(L, 3, w) || !gui_number(L, 4, h) ||
        !gui_color(L, 5, color))
        return fail(L, "bad type");
    if (!gui_optional(L, 6, rounding, 0)) return fail(L, "bad type");
    if (w < 0) {
        x += w;
        w = -w;
    }
    if (h < 0) {
        y += h;
        h = -h;
    }
    lua_gui::Command c;
    c.kind = lua_gui::Kind::RectFilled;
    c.x0 = x;
    c.y0 = y;
    c.x1 = x + w;
    c.y1 = y + h;
    c.color = color;
    c.rounding = rounding;
    lua_gui::push(inst->id, c);
    lua_pushboolean(L, 1);
    return 1;
}

int l_gui_line(lua_State* L) {
    Instance* inst = self(L);
    float x0 = 0, y0 = 0, x1 = 0, y1 = 0, thickness = 2;
    uint32_t color = 0;
    if (!gui_number(L, 1, x0) || !gui_number(L, 2, y0) || !gui_number(L, 3, x1) || !gui_number(L, 4, y1) ||
        !gui_color(L, 5, color))
        return fail(L, "bad type");
    if (!gui_optional(L, 6, thickness, 2)) return fail(L, "bad type");
    lua_gui::Command c;
    c.kind = lua_gui::Kind::Line;
    c.x0 = x0;
    c.y0 = y0;
    c.x1 = x1;
    c.y1 = y1;
    c.color = color;
    c.thickness = thickness;
    lua_gui::push(inst->id, c);
    lua_pushboolean(L, 1);
    return 1;
}

int l_gui_circle(lua_State* L) {
    Instance* inst = self(L);
    float x = 0, y = 0, r = 0, thickness = 2;
    uint32_t color = 0;
    if (!gui_number(L, 1, x) || !gui_number(L, 2, y) || !gui_number(L, 3, r) || !gui_color(L, 4, color))
        return fail(L, "bad type");
    if (!gui_optional(L, 5, thickness, 2)) return fail(L, "bad type");
    lua_gui::Command c;
    c.kind = lua_gui::Kind::Circle;
    c.x0 = x;
    c.y0 = y;
    c.x1 = r;
    c.color = color;
    c.thickness = thickness;
    lua_gui::push(inst->id, c);
    lua_pushboolean(L, 1);
    return 1;
}

int l_gui_circle_filled(lua_State* L) {
    Instance* inst = self(L);
    float x = 0, y = 0, r = 0;
    uint32_t color = 0;
    if (!gui_number(L, 1, x) || !gui_number(L, 2, y) || !gui_number(L, 3, r) || !gui_color(L, 4, color))
        return fail(L, "bad type");
    lua_gui::Command c;
    c.kind = lua_gui::Kind::CircleFilled;
    c.x0 = x;
    c.y0 = y;
    c.x1 = r;
    c.color = color;
    lua_gui::push(inst->id, c);
    lua_pushboolean(L, 1);
    return 1;
}

int l_gui_triangle_filled(lua_State* L) {
    Instance* inst = self(L);
    float x0 = 0, y0 = 0, x1 = 0, y1 = 0, x2 = 0, y2 = 0;
    uint32_t color = 0;
    if (!gui_number(L, 1, x0) || !gui_number(L, 2, y0) || !gui_number(L, 3, x1) || !gui_number(L, 4, y1) ||
        !gui_number(L, 5, x2) || !gui_number(L, 6, y2) || !gui_color(L, 7, color))
        return fail(L, "bad type");
    lua_gui::Command c;
    c.kind = lua_gui::Kind::TriangleFilled;
    c.x0 = x0;
    c.y0 = y0;
    c.x1 = x1;
    c.y1 = y1;
    c.x2 = x2;
    c.y2 = y2;
    c.color = color;
    lua_gui::push(inst->id, c);
    lua_pushboolean(L, 1);
    return 1;
}

int l_gui_text(lua_State* L) {
    Instance* inst = self(L);
    float x = 0, y = 0, scale = 1;
    uint32_t color = 0;
    if (!gui_number(L, 1, x) || !gui_number(L, 2, y) || !lua_isstring(L, 3) || !gui_color(L, 4, color))
        return fail(L, "bad type");
    if (!gui_optional(L, 5, scale, 1)) return fail(L, "bad type");
    lua_gui::Command c;
    c.kind = lua_gui::Kind::Text;
    c.x0 = x;
    c.y0 = y;
    c.color = color;
    c.scale = scale;
    c.text = lua_tostring(L, 3);
    lua_gui::push(inst->id, c);
    lua_pushboolean(L, 1);
    return 1;
}

const uint32_t kCourseTimerSingleton = GD(0x101D15F4);
const uint32_t kFieldGameSingleton = GD(0x101D1604);
const uint32_t kPlayerMgrSingleton = GD(0x101E6994);
const uint32_t kActorMgrSingleton = GD(0x101C30C8);
const uint32_t kProfileList = GD(0x101ED8DC);
const uint32_t kActorMgrCreateLater = GC(0x02009954);
const uint32_t kTypeInfoActor = GD(0x101E9CC0);
const uint32_t kCourseSelectInstance = GD(0x101CC96C);
const uint32_t kCourseTaskInstance = GD(0x101D15F0);
const uint32_t kSceneSingleton = GD(0x101E7B28);
const uint32_t kSetNextScene = GC(0x02994164);
const uint32_t kSetCourseStory = GC(0x029941D0);
const uint32_t kSubjectSelectFile = GC(0x024CE594);
const uint32_t kAppEnterFileState = GC(0x0297D268);
const uint32_t kAppConfirmFile = GC(0x0297EC3C);
const uint32_t kOpenFirstSave = GC(0x029872BC);
const uint32_t kApplySaveFile = GC(0x029D7648);
const uint32_t kMainGame = GD(0x101E7D7C);
constexpr uint32_t kSinglePlayerDrc = 0x14u;
const uint32_t kSaveMgr = GD(0x101E7DA4);
const uint32_t kSaveApplyFlag = GD(0x101E7D9C);
const uint32_t kCourseInfo = GD(0x101D18EC);
constexpr uint32_t kSaveFileCount = 3u;
constexpr uint32_t kSaveApplyFlagOff = 0x11u;
const uint32_t kFaderBusy = GC(0x02983B28);
const uint32_t kSubjectMgr = GD(0x101D1874);

const uint32_t kAreaTaskInstance = GD(0x101D1598);
const uint32_t kCourseSelectMgr = GD(0x101CBE14);
const uint32_t kRequestCourseOut = GC(0x024B77DC);
constexpr uint32_t kMapPlayerProfile = 854u;
constexpr uint32_t kPathNodeSize = 0x68u;
const uint32_t kFlowVtbl = GD(0x10177E4C);
const uint32_t kMenuVtbl = GD(0x10175F88);
constexpr uint32_t kSceneNowCreatorOff = 0x58u;
constexpr uint32_t kSceneNextTypeOff = 0x5Cu;
constexpr uint32_t kSceneTaskMgrOff = 0x64u;
constexpr uint32_t kSceneFaderOff = 0x68u;
constexpr uint32_t kTaskParentOff = 0x0u;
constexpr uint32_t kTaskChildOff = 0x4u;
constexpr uint32_t kTaskSiblingOff = 0x8u;
constexpr uint32_t kTaskListNextOff = 0x4u;
constexpr uint32_t kTaskListDataOff = 0x8u;
constexpr uint32_t kTaskMgrPrepareOff = 0x48u;
constexpr uint32_t kTaskMgrPrepareDoneOff = 0x54u;
constexpr uint32_t kTaskMgrActiveOff = 0x60u;
constexpr uint32_t kTaskMgrStaticOff = 0x6Cu;
constexpr uint32_t kTaskMgrRootOff = 0xB0u;
constexpr uint32_t kTaskClassFactoryOff = 0x6Cu;
constexpr uint32_t kTaskVtblOff = 0x70u;
constexpr uint32_t kSubjectFileOff = 0x1E7Eu;
constexpr uint32_t kSubjectModeOff = 0x1E7Fu;
constexpr uint8_t kSubjectModeActive = 1;
constexpr uint32_t kAppStateMethodOff = 0x1DCu;
constexpr uint32_t kAppGateSelect = 0xDB1u;
constexpr uint32_t kAppGateStory = 0xDB2u;
constexpr uint32_t kSaveDataOff = 0x24u;
constexpr uint32_t kLastFileOff = 0x07u;
constexpr uint32_t kCourseInfoNextWorldOff = 0x1Cu;
constexpr uint32_t kCourseInfoNextCourseOff = 0x1Du;
constexpr uint32_t kCourseTaskSrcOff = 0x10u;
const uint32_t kCreatorBoot = GC(0x0202930C);
const uint32_t kCreatorFlow = GC(0x0297B9F8);
const uint32_t kCreatorFileSelect = GC(0x0298AD3C);
const uint32_t kCreatorSaveSelect = GC(0x024C11F4);
const uint32_t kCreatorTitle = GC(0x0202B51C);
const uint32_t kCreatorMenu = GC(0x0202B61C);
const uint32_t kCreatorMap = GC(0x020292B8);
const uint32_t kCreatorCourseTask = GC(0x0202B4C8);
const uint32_t kCreatorCourseSelect = GC(0x0202B86C);
constexpr uint32_t kClassIdTask = 0x00000002u;

constexpr uint32_t kActorMgrPtrCache = 0x6A20u;
constexpr uint32_t kActorIdxToPtr = 0x4u;
constexpr uint32_t kCacheArraySize = 0x0u;
constexpr uint32_t kCacheArray = 0x4u;
constexpr uint32_t kCacheLiveEnd = 0xCu;
constexpr uint32_t kActorId = 0x4u;
constexpr uint32_t kActorProfile = 0x8u;
constexpr uint32_t kActorIsMap = 0xDu;
constexpr uint32_t kActorIsActive = 0xEu;
constexpr uint32_t kActorDeleteFlag = 0xFu;
constexpr uint32_t kActorParam0 = 0x10u;
constexpr uint32_t kActorParam1 = 0x14u;
constexpr uint32_t kActorVptr = 0x4Cu;
constexpr uint32_t kDerivedCheckSlot = 0xCu;
constexpr uint32_t kTimerTime = 0x14u;
constexpr uint32_t kCourseCoinNum = 0x144u;
constexpr uint32_t kCurrentScore = 0x148u;
constexpr uint32_t kPlayerData0 = 0x24u;
constexpr uint32_t kPlayerDataSize = 0x48u;
constexpr uint32_t kEntryOff = 0x0u;
constexpr uint32_t kLifeOff = 0x4u;
constexpr uint32_t kModeOff = 0x8u;
constexpr uint32_t kCoinOff = 0x34u;
constexpr uint32_t kPlayerObjects = 0x20u;
constexpr uint32_t kPlayerNo = 0x54u;
constexpr uint32_t kLayer = 0x56u;
constexpr uint32_t kPosX = 0x6Cu;
constexpr uint32_t kPosY = 0x70u;
constexpr uint32_t kPosZ = 0x74u;
constexpr uint32_t kSpeedX = 0x78u;
constexpr uint32_t kSpeedY = 0x7Cu;
constexpr uint32_t kSpeedZ = 0x80u;
constexpr uint32_t kScaleX = 0x90u;
constexpr uint32_t kScaleY = 0x94u;
constexpr uint32_t kScaleZ = 0x98u;
constexpr uint32_t kActorMode = 0x500u;
constexpr uint32_t kNowBgCross = 0x1AC8u;
constexpr uint32_t kBgCrossIsFoot = 1u;

constexpr int kPlayerCount = 4;
constexpr uint32_t kActorArraySize = 520;
constexpr const char* kPlayerMeta = "nsmbu.player";
constexpr const char* kActorMeta = "nsmbu.level.actor";

thread_local Cpu* t_guest_cpu = nullptr;
bool g_actor_shape_warned = false;

bool guest_ok(uint32_t addr, size_t size, std::string& err) {
    return lua_guest::check(addr, size, err);
}

bool read_ptr(uint32_t addr, uint32_t& out, std::string& err) {
    uint32_t p = 0;
    if (!lua_guest::read_u32(addr, p, err)) return false;
    if (p == 0 || !guest_ok(p, 4, err)) {
        err = "not available";
        return false;
    }
    out = p;
    return true;
}

bool singleton(uint32_t addr, uint32_t& out, std::string& err) {
    return read_ptr(addr, out, err);
}

bool field_game(uint32_t& out, std::string& err) {
    return singleton(kFieldGameSingleton, out, err);
}

bool player_mgr(uint32_t& out, std::string& err) {
    return singleton(kPlayerMgrSingleton, out, err);
}

bool slot_addr(int index, uint32_t& out, std::string& err) {
    if (index < 0 || index >= kPlayerCount) {
        err = "bad player index";
        return false;
    }
    uint32_t field = 0;
    if (!field_game(field, err)) return false;
    out = field + kPlayerData0 + (uint32_t)index * kPlayerDataSize;
    return true;
}

bool actor_addr(int index, uint32_t& out, std::string& err) {
    if (index < 0 || index >= kPlayerCount) {
        err = "bad player index";
        return false;
    }
    uint32_t mgr = 0;
    if (!player_mgr(mgr, err)) return false;
    uint32_t actor = 0;
    if (!lua_guest::read_u32(mgr + kPlayerObjects + (uint32_t)index * 4u, actor, err)) return false;
    if (actor == 0 || !guest_ok(actor, 4, err)) {
        err = "no player actor";
        return false;
    }
    out = actor;
    return true;
}

bool player_entered(int index, bool& out, std::string& err) {
    uint32_t slot = 0;
    if (!slot_addr(index, slot, err)) return false;
    uint8_t entry = 0;
    if (!lua_guest::read_u8(slot + kEntryOff, entry, err)) return false;
    out = entry != 0;
    return true;
}

int player_index(lua_State* L) {
    luaL_checktype(L, 1, LUA_TTABLE);
    lua_getfield(L, 1, "index");
    if (!lua_isnumber(L, -1)) luaL_error(L, "player index missing");
    int index = (int)lua_tointeger(L, -1);
    lua_pop(L, 1);
    return index;
}

void push_player(lua_State* L, int index) {
    lua_newtable(L);
    lua_pushinteger(L, index);
    lua_setfield(L, -2, "index");
    luaL_getmetatable(L, kPlayerMeta);
    lua_setmetatable(L, -2);
}

void push_vec3(lua_State* L, float x, float y, float z) {
    lua_newtable(L);
    lua_pushnumber(L, x);
    lua_setfield(L, -2, "x");
    lua_pushnumber(L, y);
    lua_setfield(L, -2, "y");
    lua_pushnumber(L, z);
    lua_setfield(L, -2, "z");
}

bool read_xyz(lua_State* L, int arg, float& x, float& y, float& z, std::string& err) {
    if (lua_istable(L, arg)) {
        lua_getfield(L, arg, "x");
        if (!lua_isnumber(L, -1)) {
            lua_pop(L, 1);
            err = "bad type";
            return false;
        }
        x = (float)lua_tonumber(L, -1);
        lua_pop(L, 1);
        lua_getfield(L, arg, "y");
        if (!lua_isnumber(L, -1)) {
            lua_pop(L, 1);
            err = "bad type";
            return false;
        }
        y = (float)lua_tonumber(L, -1);
        lua_pop(L, 1);
        lua_getfield(L, arg, "z");
        if (!lua_isnumber(L, -1)) {
            lua_pop(L, 1);
            err = "bad type";
            return false;
        }
        z = (float)lua_tonumber(L, -1);
        lua_pop(L, 1);
        return true;
    }
    if (!lua_isnumber(L, arg) || !lua_isnumber(L, arg + 1) || !lua_isnumber(L, arg + 2)) {
        err = "bad type";
        return false;
    }
    x = (float)lua_tonumber(L, arg);
    y = (float)lua_tonumber(L, arg + 1);
    z = (float)lua_tonumber(L, arg + 2);
    return true;
}

bool read_vec3(uint32_t base, uint32_t ox, uint32_t oy, uint32_t oz, float& x, float& y, float& z,
               std::string& err) {
    if (!lua_guest::read_f32(base + ox, x, err)) return false;
    if (!lua_guest::read_f32(base + oy, y, err)) return false;
    if (!lua_guest::read_f32(base + oz, z, err)) return false;
    return true;
}

bool write_vec3(uint32_t base, uint32_t ox, uint32_t oy, uint32_t oz, float x, float y, float z,
                std::string& err) {
    if (!lua_guest::write_f32(base + ox, x, err)) return false;
    if (!lua_guest::write_f32(base + oy, y, err)) return false;
    if (!lua_guest::write_f32(base + oz, z, err)) return false;
    return true;
}

int l_course_active(lua_State* L) {
    uint32_t timer = 0;
    std::string err;
    lua_pushboolean(L, singleton(kCourseTimerSingleton, timer, err) ? 1 : 0);
    return 1;
}

int l_course_time(lua_State* L) {
    uint32_t timer = 0;
    std::string err;
    if (!singleton(kCourseTimerSingleton, timer, err)) return fail(L, err.c_str());
    uint32_t units = 0;
    if (!lua_guest::read_u32(timer + kTimerTime, units, err)) return fail(L, err.c_str());
    lua_pushnumber(L, (lua_Number)(units >> 12));
    return 1;
}

int l_course_set_time(lua_State* L) {
    if (!lua_isnumber(L, 1)) return fail(L, "bad type");
    double seconds = lua_tonumber(L, 1);
    if (seconds < 0) seconds = 0;
    if (seconds > 999) seconds = 999;
    uint32_t timer = 0;
    std::string err;
    if (!singleton(kCourseTimerSingleton, timer, err)) return fail(L, err.c_str());
    uint32_t units = ((uint32_t)seconds) << 12;
    if (!lua_guest::write_u32(timer + kTimerTime, units, err)) return fail(L, err.c_str());
    lua_pushboolean(L, 1);
    return 1;
}

int l_course_add_time(lua_State* L) {
    if (!lua_isnumber(L, 1)) return fail(L, "bad type");
    double delta = lua_tonumber(L, 1);
    uint32_t timer = 0;
    std::string err;
    if (!singleton(kCourseTimerSingleton, timer, err)) return fail(L, err.c_str());
    uint32_t units = 0;
    if (!lua_guest::read_u32(timer + kTimerTime, units, err)) return fail(L, err.c_str());
    double seconds = (double)(units >> 12) + delta;
    if (seconds < 0) seconds = 0;
    if (seconds > 999) seconds = 999;
    units = ((uint32_t)seconds) << 12;
    if (!lua_guest::write_u32(timer + kTimerTime, units, err)) return fail(L, err.c_str());
    lua_pushboolean(L, 1);
    return 1;
}

int l_course_coins(lua_State* L) {
    uint32_t field = 0;
    std::string err;
    if (!field_game(field, err)) return fail(L, err.c_str());
    int32_t coins = 0;
    if (!lua_guest::read_s32(field + kCourseCoinNum, coins, err)) return fail(L, err.c_str());
    lua_pushnumber(L, (lua_Number)coins);
    return 1;
}

int l_course_set_coins(lua_State* L) {
    if (!lua_isnumber(L, 1)) return fail(L, "bad type");
    uint32_t field = 0;
    std::string err;
    if (!field_game(field, err)) return fail(L, err.c_str());
    int32_t coins = (int32_t)lua_tonumber(L, 1);
    if (!lua_guest::write_s32(field + kCourseCoinNum, coins, err)) return fail(L, err.c_str());
    lua_pushboolean(L, 1);
    return 1;
}

int l_course_score(lua_State* L) {
    uint32_t field = 0;
    std::string err;
    if (!field_game(field, err)) return fail(L, err.c_str());
    uint32_t score = 0;
    if (!lua_guest::read_u32(field + kCurrentScore, score, err)) return fail(L, err.c_str());
    lua_pushnumber(L, (lua_Number)score);
    return 1;
}

int l_course_set_score(lua_State* L) {
    if (!lua_isnumber(L, 1)) return fail(L, "bad type");
    uint32_t field = 0;
    std::string err;
    if (!field_game(field, err)) return fail(L, err.c_str());
    uint32_t score = (uint32_t)lua_tonumber(L, 1);
    if (!lua_guest::write_u32(field + kCurrentScore, score, err)) return fail(L, err.c_str());
    lua_pushboolean(L, 1);
    return 1;
}

int l_player(lua_State* L) {
    if (!lua_isnumber(L, 1)) return fail(L, "bad type");
    int index = (int)lua_tointeger(L, 1);
    if (index < 0 || index >= kPlayerCount) return fail(L, "bad player index");
    uint32_t field = 0;
    std::string err;
    if (!field_game(field, err)) return fail(L, err.c_str());
    push_player(L, index);
    return 1;
}

int l_players(lua_State* L) {
    lua_newtable(L);
    std::string err;
    for (int i = 0; i < kPlayerCount; ++i) {
        bool entered = false;
        if (!player_entered(i, entered, err)) continue;
        if (!entered) continue;
        push_player(L, i);
        lua_rawseti(L, -2, (int)lua_objlen(L, -2) + 1);
    }
    return 1;
}

int l_player_entered(lua_State* L) {
    int index = player_index(L);
    bool entered = false;
    std::string err;
    if (!player_entered(index, entered, err)) return fail(L, err.c_str());
    lua_pushboolean(L, entered ? 1 : 0);
    return 1;
}

int l_player_lives(lua_State* L) {
    int index = player_index(L);
    bool entered = false;
    std::string err;
    if (!player_entered(index, entered, err)) return fail(L, err.c_str());
    if (!entered) return fail(L, "not entered");
    uint32_t slot = 0;
    if (!slot_addr(index, slot, err)) return fail(L, err.c_str());
    int32_t lives = 0;
    if (!lua_guest::read_s32(slot + kLifeOff, lives, err)) return fail(L, err.c_str());
    lua_pushnumber(L, (lua_Number)lives);
    return 1;
}

int l_player_set_lives(lua_State* L) {
    int index = player_index(L);
    if (!lua_isnumber(L, 2)) return fail(L, "bad type");
    bool entered = false;
    std::string err;
    if (!player_entered(index, entered, err)) return fail(L, err.c_str());
    if (!entered) return fail(L, "not entered");
    uint32_t slot = 0;
    if (!slot_addr(index, slot, err)) return fail(L, err.c_str());
    int32_t lives = (int32_t)lua_tonumber(L, 2);
    if (!lua_guest::write_s32(slot + kLifeOff, lives, err)) return fail(L, err.c_str());
    lua_pushboolean(L, 1);
    return 1;
}

int l_player_mode(lua_State* L) {
    int index = player_index(L);
    bool entered = false;
    std::string err;
    if (!player_entered(index, entered, err)) return fail(L, err.c_str());
    if (!entered) return fail(L, "not entered");
    uint32_t slot = 0;
    if (!slot_addr(index, slot, err)) return fail(L, err.c_str());
    int32_t mode = 0;
    if (!lua_guest::read_s32(slot + kModeOff, mode, err)) return fail(L, err.c_str());
    uint32_t actor = 0;
    if (actor_addr(index, actor, err)) {
        int32_t actor_mode = 0;
        if (lua_guest::read_s32(actor + kActorMode, actor_mode, err) && actor_mode >= 0 &&
            actor_mode <= 8)
            mode = actor_mode;
    }
    if (mode < 0 || mode > 8) return fail(L, "bad mode");
    lua_pushnumber(L, (lua_Number)mode);
    return 1;
}

int l_player_set_mode(lua_State* L) {
    int index = player_index(L);
    if (!lua_isnumber(L, 2)) return fail(L, "bad type");
    int32_t mode = (int32_t)lua_tonumber(L, 2);
    if (mode < 0 || mode > 8) return fail(L, "bad mode");
    bool entered = false;
    std::string err;
    if (!player_entered(index, entered, err)) return fail(L, err.c_str());
    if (!entered) return fail(L, "not entered");
    uint32_t slot = 0;
    if (!slot_addr(index, slot, err)) return fail(L, err.c_str());
    if (!lua_guest::write_s32(slot + kModeOff, mode, err)) return fail(L, err.c_str());
    uint32_t actor = 0;
    if (actor_addr(index, actor, err)) {
        if (!lua_guest::write_s32(actor + kActorMode, mode, err)) return fail(L, err.c_str());
    }
    lua_pushboolean(L, 1);
    return 1;
}

int l_player_coins(lua_State* L) {
    int index = player_index(L);
    bool entered = false;
    std::string err;
    if (!player_entered(index, entered, err)) return fail(L, err.c_str());
    if (!entered) return fail(L, "not entered");
    uint32_t slot = 0;
    if (!slot_addr(index, slot, err)) return fail(L, err.c_str());
    int32_t coins = 0;
    if (!lua_guest::read_s32(slot + kCoinOff, coins, err)) return fail(L, err.c_str());
    lua_pushnumber(L, (lua_Number)coins);
    return 1;
}

int l_player_set_coins(lua_State* L) {
    int index = player_index(L);
    if (!lua_isnumber(L, 2)) return fail(L, "bad type");
    bool entered = false;
    std::string err;
    if (!player_entered(index, entered, err)) return fail(L, err.c_str());
    if (!entered) return fail(L, "not entered");
    uint32_t slot = 0;
    if (!slot_addr(index, slot, err)) return fail(L, err.c_str());
    int32_t coins = (int32_t)lua_tonumber(L, 2);
    if (!lua_guest::write_s32(slot + kCoinOff, coins, err)) return fail(L, err.c_str());
    lua_pushboolean(L, 1);
    return 1;
}

int l_player_speed_y(lua_State* L) {
    int index = player_index(L);
    uint32_t actor = 0;
    std::string err;
    if (!actor_addr(index, actor, err)) return fail(L, err.c_str());
    float speed = 0;
    if (!lua_guest::read_f32(actor + kSpeedY, speed, err)) return fail(L, err.c_str());
    lua_pushnumber(L, speed);
    return 1;
}

int l_player_set_speed_y(lua_State* L) {
    int index = player_index(L);
    if (!lua_isnumber(L, 2)) return fail(L, "bad type");
    uint32_t actor = 0;
    std::string err;
    if (!actor_addr(index, actor, err)) return fail(L, err.c_str());
    float speed = (float)lua_tonumber(L, 2);
    if (!lua_guest::write_f32(actor + kSpeedY, speed, err)) return fail(L, err.c_str());
    lua_pushboolean(L, 1);
    return 1;
}

int l_player_pos(lua_State* L) {
    int index = player_index(L);
    uint32_t actor = 0;
    std::string err;
    if (!actor_addr(index, actor, err)) return fail(L, err.c_str());
    float x = 0, y = 0, z = 0;
    if (!read_vec3(actor, kPosX, kPosY, kPosZ, x, y, z, err)) return fail(L, err.c_str());
    push_vec3(L, x, y, z);
    return 1;
}

int l_player_set_pos(lua_State* L) {
    int index = player_index(L);
    float x = 0, y = 0, z = 0;
    std::string err;
    if (!read_xyz(L, 2, x, y, z, err)) return fail(L, err.c_str());
    uint32_t actor = 0;
    if (!actor_addr(index, actor, err)) return fail(L, err.c_str());
    if (!write_vec3(actor, kPosX, kPosY, kPosZ, x, y, z, err)) return fail(L, err.c_str());
    lua_pushboolean(L, 1);
    return 1;
}

int l_player_speed(lua_State* L) {
    int index = player_index(L);
    uint32_t actor = 0;
    std::string err;
    if (!actor_addr(index, actor, err)) return fail(L, err.c_str());
    float x = 0, y = 0, z = 0;
    if (!read_vec3(actor, kSpeedX, kSpeedY, kSpeedZ, x, y, z, err)) return fail(L, err.c_str());
    push_vec3(L, x, y, z);
    return 1;
}

int l_player_set_speed(lua_State* L) {
    int index = player_index(L);
    float x = 0, y = 0, z = 0;
    std::string err;
    if (!read_xyz(L, 2, x, y, z, err)) return fail(L, err.c_str());
    uint32_t actor = 0;
    if (!actor_addr(index, actor, err)) return fail(L, err.c_str());
    if (!write_vec3(actor, kSpeedX, kSpeedY, kSpeedZ, x, y, z, err)) return fail(L, err.c_str());
    lua_pushboolean(L, 1);
    return 1;
}

int l_player_grounded(lua_State* L) {
    int index = player_index(L);
    uint32_t actor = 0;
    std::string err;
    if (!actor_addr(index, actor, err)) return fail(L, err.c_str());
    uint32_t cross = 0;
    if (!lua_guest::read_u32(actor + kNowBgCross, cross, err)) return fail(L, err.c_str());
    lua_pushboolean(L, (cross & kBgCrossIsFoot) ? 1 : 0);
    return 1;
}

int l_player_actor(lua_State* L) {
    int index = player_index(L);
    uint32_t actor = 0;
    std::string err;
    if (!actor_addr(index, actor, err)) return fail(L, err.c_str());
    lua_pushnumber(L, (lua_Number)actor);
    return 1;
}

bool actor_mgr(uint32_t& out, std::string& err) {
    return read_ptr(kActorMgrSingleton, out, err);
}

bool live_actor_range(uint32_t& begin, uint32_t& end, std::string& err) {
    uint32_t mgr = 0;
    if (!actor_mgr(mgr, err)) return false;
    uint32_t cache = mgr + kActorMgrPtrCache;
    uint32_t array = 0, size = 0, live = 0;
    if (!lua_guest::read_u32(cache + kCacheArray, array, err)) return false;
    if (!lua_guest::read_u32(cache + kCacheArraySize, size, err)) return false;
    if (!lua_guest::read_u32(cache + kCacheLiveEnd, live, err)) return false;
    if (size != kActorArraySize || array == 0 || !guest_ok(array, size * 4, err)) {
        if (!g_actor_shape_warned) {
            g_actor_shape_warned = true;
            fprintf(stderr, "[mods] nsmbu.level actor cache mismatch: mgr=%08x array=%08x size=%u live=%08x\n",
                    mgr, array, size, live);
        }
        err = "actor array not ready";
        return false;
    }
    if (live < array || live > array + size * 4 || ((live - array) & 3)) {
        err = "actor array not ready";
        return false;
    }
    begin = array;
    end = live;
    return true;
}

void push_actor(lua_State* L, uint32_t addr) {
    lua_newtable(L);
    lua_pushnumber(L, (lua_Number)addr);
    lua_setfield(L, -2, "addr");
    luaL_getmetatable(L, kActorMeta);
    lua_setmetatable(L, -2);
}

bool handle_addr(lua_State* L, uint32_t& out, std::string& err) {
    luaL_checktype(L, 1, LUA_TTABLE);
    lua_getfield(L, 1, "addr");
    if (!lua_isnumber(L, -1)) {
        lua_pop(L, 1);
        err = "bad actor handle";
        return false;
    }
    out = (uint32_t)lua_tonumber(L, -1);
    lua_pop(L, 1);
    if (!guest_ok(out, kActorVptr + 4, err)) {
        err = "actor gone";
        return false;
    }
    return true;
}

bool actor_profile_id(uint32_t actor, uint32_t& out, std::string& err) {
    uint32_t profile = 0;
    if (!lua_guest::read_u32(actor + kActorProfile, profile, err)) return false;
    if (profile == 0 || !guest_ok(profile, 0x14, err)) {
        err = "actor gone";
        return false;
    }
    if (!lua_guest::read_u32(profile + kActorId, out, err)) return false;
    return true;
}

bool actor_is_actor(uint32_t actor, bool& out, std::string& err) {
    if (!t_guest_cpu) {
        err = "no guest cpu";
        return false;
    }
    uint32_t vt = 0;
    if (!lua_guest::read_u32(actor + kActorVptr, vt, err)) return false;
    if (vt == 0 || !guest_ok(vt, kDerivedCheckSlot + 4, err)) {
        out = false;
        return true;
    }
    uint32_t fn = 0;
    if (!lua_guest::read_u32(vt + kDerivedCheckSlot, fn, err)) return false;
    if (fn == 0 || !guest_code_valid(fn)) {
        out = false;
        return true;
    }
    out = guest_call(t_guest_cpu, fn, {actor, kTypeInfoActor}) != 0;
    return true;
}

int l_level_actors(lua_State* L) {
    uint32_t begin = 0, end = 0;
    std::string err;
    if (!live_actor_range(begin, end, err)) return fail(L, err.c_str());
    lua_newtable(L);
    int n = 0;
    for (uint32_t slot = begin; slot < end; slot += kActorIdxToPtr) {
        uint32_t actor = 0;
        if (!lua_guest::read_u32(slot, actor, err) || actor == 0 ||
            !guest_ok(actor, kActorVptr + 4, err))
            continue;
        push_actor(L, actor);
        lua_rawseti(L, -2, ++n);
    }
    return 1;
}

int l_level_count(lua_State* L) {
    uint32_t begin = 0, end = 0;
    std::string err;
    if (!live_actor_range(begin, end, err)) return fail(L, err.c_str());
    bool filter = lua_isnumber(L, 1) ? true : false;
    uint32_t want = filter ? (uint32_t)lua_tonumber(L, 1) : 0;
    int count = 0;
    for (uint32_t slot = begin; slot < end; slot += kActorIdxToPtr) {
        uint32_t actor = 0;
        if (!lua_guest::read_u32(slot, actor, err) || actor == 0 ||
            !guest_ok(actor, kActorVptr + 4, err))
            continue;
        if (filter) {
            uint32_t id = 0;
            if (!actor_profile_id(actor, id, err)) continue;
            if (id != want) continue;
        }
        ++count;
    }
    lua_pushnumber(L, count);
    return 1;
}

int l_level_spawn(lua_State* L) {
    if (!t_guest_cpu) return fail(L, "no guest cpu");
    if (!lua_isnumber(L, 1)) return fail(L, "bad type");
    double id = lua_tonumber(L, 1);
    if (id < 0 || id >= 913) return fail(L, "bad profile id");
    float x = 0, y = 0, z = 0;
    std::string err;
    if (!read_xyz(L, 2, x, y, z, err)) return fail(L, err.c_str());
    uint32_t param0 = 0, param1 = 0, layer = 0;
    if (lua_istable(L, 5)) {
        lua_getfield(L, 5, "param0");
        if (lua_isnumber(L, -1)) param0 = (uint32_t)lua_tonumber(L, -1);
        lua_pop(L, 1);
        lua_getfield(L, 5, "param1");
        if (lua_isnumber(L, -1)) param1 = (uint32_t)lua_tonumber(L, -1);
        lua_pop(L, 1);
        lua_getfield(L, 5, "layer");
        if (lua_isnumber(L, -1)) layer = (uint32_t)lua_tonumber(L, -1) & 0xFF;
        lua_pop(L, 1);
    }
    uint32_t mgr = 0;
    if (!actor_mgr(mgr, err)) return fail(L, err.c_str());
    uint32_t profile = 0;
    if (!lua_guest::read_u32(kProfileList + (uint32_t)id * 4, profile, err))
        return fail(L, err.c_str());
    if (profile == 0 || !guest_ok(profile, 0x14, err)) return fail(L, "unknown profile");
    uint32_t registered = 0;
    if (!lua_guest::read_u32(profile + kActorId, registered, err)) return fail(L, err.c_str());
    if (registered != (uint32_t)id) return fail(L, "unknown profile");
    static const uint32_t s_param = mem::host_alloc(0x2C, 0x20);
    if (s_param == 0) return fail(L, "out of memory");
    st32(s_param + 0x00, param0);
    st32(s_param + 0x04, param1);
    st32(s_param + 0x08, 0);
    st32(s_param + 0x0C, profile);
    stf32(s_param + 0x10, x);
    stf32(s_param + 0x14, y);
    stf32(s_param + 0x18, z);
    st32(s_param + 0x1C, 0);
    st8(s_param + 0x20, (uint8_t)layer);
    st8(s_param + 0x21, 0);
    st8(s_param + 0x22, 0);
    st8(s_param + 0x24, 0);
    st8(s_param + 0x25, 0);
    st8(s_param + 0x26, 0);
    st32(s_param + 0x28, 0);
    guest_call(t_guest_cpu, kActorMgrCreateLater, {mgr, s_param});
    lua_pushboolean(L, 1);
    return 1;
}

int l_actor_address(lua_State* L) {
    uint32_t actor = 0;
    std::string err;
    if (!handle_addr(L, actor, err)) return fail(L, err.c_str());
    lua_pushnumber(L, (lua_Number)actor);
    return 1;
}

int l_actor_id(lua_State* L) {
    uint32_t actor = 0;
    std::string err;
    if (!handle_addr(L, actor, err)) return fail(L, err.c_str());
    uint32_t id = 0;
    if (!lua_guest::read_u32(actor + kActorId, id, err)) return fail(L, err.c_str());
    lua_pushnumber(L, (lua_Number)id);
    return 1;
}

int l_actor_profile_id(lua_State* L) {
    uint32_t actor = 0;
    std::string err;
    if (!handle_addr(L, actor, err)) return fail(L, err.c_str());
    uint32_t id = 0;
    if (!actor_profile_id(actor, id, err)) return fail(L, err.c_str());
    lua_pushnumber(L, (lua_Number)id);
    return 1;
}

int l_actor_is_actor(lua_State* L) {
    uint32_t actor = 0;
    std::string err;
    if (!handle_addr(L, actor, err)) return fail(L, err.c_str());
    bool out = false;
    if (!actor_is_actor(actor, out, err)) return fail(L, err.c_str());
    lua_pushboolean(L, out ? 1 : 0);
    return 1;
}

int l_actor_is_map(lua_State* L) {
    uint32_t actor = 0;
    std::string err;
    if (!handle_addr(L, actor, err)) return fail(L, err.c_str());
    uint8_t map = 0;
    if (!lua_guest::read_u8(actor + kActorIsMap, map, err)) return fail(L, err.c_str());
    lua_pushboolean(L, map ? 1 : 0);
    return 1;
}

int l_actor_active(lua_State* L) {
    uint32_t actor = 0;
    std::string err;
    if (!handle_addr(L, actor, err)) return fail(L, err.c_str());
    uint8_t active = 0;
    if (!lua_guest::read_u8(actor + kActorIsActive, active, err)) return fail(L, err.c_str());
    lua_pushboolean(L, active ? 1 : 0);
    return 1;
}

int l_actor_layer(lua_State* L) {
    uint32_t actor = 0;
    std::string err;
    if (!handle_addr(L, actor, err)) return fail(L, err.c_str());
    uint8_t layer = 0;
    if (!lua_guest::read_u8(actor + kLayer, layer, err)) return fail(L, err.c_str());
    lua_pushnumber(L, layer);
    return 1;
}

int l_actor_player_no(lua_State* L) {
    uint32_t actor = 0;
    std::string err;
    if (!handle_addr(L, actor, err)) return fail(L, err.c_str());
    int8_t no = 0;
    if (!lua_guest::read_s8(actor + kPlayerNo, no, err)) return fail(L, err.c_str());
    lua_pushnumber(L, no);
    return 1;
}

int l_actor_param0(lua_State* L) {
    uint32_t actor = 0;
    std::string err;
    if (!handle_addr(L, actor, err)) return fail(L, err.c_str());
    uint32_t param = 0;
    if (!lua_guest::read_u32(actor + kActorParam0, param, err)) return fail(L, err.c_str());
    lua_pushnumber(L, (lua_Number)param);
    return 1;
}

int l_actor_param1(lua_State* L) {
    uint32_t actor = 0;
    std::string err;
    if (!handle_addr(L, actor, err)) return fail(L, err.c_str());
    uint32_t param = 0;
    if (!lua_guest::read_u32(actor + kActorParam1, param, err)) return fail(L, err.c_str());
    lua_pushnumber(L, (lua_Number)param);
    return 1;
}

int l_actor_pos(lua_State* L) {
    uint32_t actor = 0;
    std::string err;
    if (!handle_addr(L, actor, err)) return fail(L, err.c_str());
    bool is_actor_value = false;
    if (!actor_is_actor(actor, is_actor_value, err)) return fail(L, err.c_str());
    if (!is_actor_value) return fail(L, "not an Actor");
    float x = 0, y = 0, z = 0;
    if (!read_vec3(actor, kPosX, kPosY, kPosZ, x, y, z, err)) return fail(L, err.c_str());
    push_vec3(L, x, y, z);
    return 1;
}

int l_actor_set_pos(lua_State* L) {
    float x = 0, y = 0, z = 0;
    std::string err;
    if (!read_xyz(L, 2, x, y, z, err)) return fail(L, err.c_str());
    uint32_t actor = 0;
    if (!handle_addr(L, actor, err)) return fail(L, err.c_str());
    bool is_actor_value = false;
    if (!actor_is_actor(actor, is_actor_value, err)) return fail(L, err.c_str());
    if (!is_actor_value) return fail(L, "not an Actor");
    if (!write_vec3(actor, kPosX, kPosY, kPosZ, x, y, z, err)) return fail(L, err.c_str());
    lua_pushboolean(L, 1);
    return 1;
}

int l_actor_speed(lua_State* L) {
    uint32_t actor = 0;
    std::string err;
    if (!handle_addr(L, actor, err)) return fail(L, err.c_str());
    bool is_actor_value = false;
    if (!actor_is_actor(actor, is_actor_value, err)) return fail(L, err.c_str());
    if (!is_actor_value) return fail(L, "not an Actor");
    float x = 0, y = 0, z = 0;
    if (!read_vec3(actor, kSpeedX, kSpeedY, kSpeedZ, x, y, z, err)) return fail(L, err.c_str());
    push_vec3(L, x, y, z);
    return 1;
}

int l_actor_set_speed(lua_State* L) {
    float x = 0, y = 0, z = 0;
    std::string err;
    if (!read_xyz(L, 2, x, y, z, err)) return fail(L, err.c_str());
    uint32_t actor = 0;
    if (!handle_addr(L, actor, err)) return fail(L, err.c_str());
    bool is_actor_value = false;
    if (!actor_is_actor(actor, is_actor_value, err)) return fail(L, err.c_str());
    if (!is_actor_value) return fail(L, "not an Actor");
    if (!write_vec3(actor, kSpeedX, kSpeedY, kSpeedZ, x, y, z, err)) return fail(L, err.c_str());
    lua_pushboolean(L, 1);
    return 1;
}

int l_actor_scale(lua_State* L) {
    uint32_t actor = 0;
    std::string err;
    if (!handle_addr(L, actor, err)) return fail(L, err.c_str());
    bool is_actor_value = false;
    if (!actor_is_actor(actor, is_actor_value, err)) return fail(L, err.c_str());
    if (!is_actor_value) return fail(L, "not an Actor");
    float x = 0, y = 0, z = 0;
    if (!read_vec3(actor, kScaleX, kScaleY, kScaleZ, x, y, z, err)) return fail(L, err.c_str());
    push_vec3(L, x, y, z);
    return 1;
}

int l_actor_set_scale(lua_State* L) {
    float x = 0, y = 0, z = 0;
    std::string err;
    if (!read_xyz(L, 2, x, y, z, err)) return fail(L, err.c_str());
    uint32_t actor = 0;
    if (!handle_addr(L, actor, err)) return fail(L, err.c_str());
    bool is_actor_value = false;
    if (!actor_is_actor(actor, is_actor_value, err)) return fail(L, err.c_str());
    if (!is_actor_value) return fail(L, "not an Actor");
    if (!write_vec3(actor, kScaleX, kScaleY, kScaleZ, x, y, z, err)) return fail(L, err.c_str());
    lua_pushboolean(L, 1);
    return 1;
}

int l_actor_delete(lua_State* L) {
    uint32_t actor = 0;
    std::string err;
    if (!handle_addr(L, actor, err)) return fail(L, err.c_str());
    if (!lua_guest::write_u8(actor + kActorDeleteFlag, 1, err)) return fail(L, err.c_str());
    lua_pushboolean(L, 1);
    return 1;
}

int l_player_call(lua_State* L) {
    lua_remove(L, 1);
    return l_player(L);
}

void install_player_meta(lua_State* L) {
    if (luaL_newmetatable(L, kPlayerMeta)) {
        lua_newtable(L);
        bind_guest(L, "entered", l_player_entered);
        bind_guest(L, "lives", l_player_lives);
        bind_guest(L, "set_lives", l_player_set_lives);
        bind_guest(L, "mode", l_player_mode);
        bind_guest(L, "set_mode", l_player_set_mode);
        bind_guest(L, "coins", l_player_coins);
        bind_guest(L, "set_coins", l_player_set_coins);
        bind_guest(L, "speed_y", l_player_speed_y);
        bind_guest(L, "set_speed_y", l_player_set_speed_y);
        bind_guest(L, "pos", l_player_pos);
        bind_guest(L, "set_pos", l_player_set_pos);
        bind_guest(L, "speed", l_player_speed);
        bind_guest(L, "set_speed", l_player_set_speed);
        bind_guest(L, "grounded", l_player_grounded);
        bind_guest(L, "actor", l_player_actor);
        lua_setfield(L, -2, "__index");
    }
    lua_pop(L, 1);
}

void install_actor_meta(lua_State* L) {
    if (luaL_newmetatable(L, kActorMeta)) {
        lua_newtable(L);
        bind_guest(L, "address", l_actor_address);
        bind_guest(L, "id", l_actor_id);
        bind_guest(L, "profile_id", l_actor_profile_id);
        bind_guest(L, "is_actor", l_actor_is_actor);
        bind_guest(L, "is_map_actor", l_actor_is_map);
        bind_guest(L, "active", l_actor_active);
        bind_guest(L, "layer", l_actor_layer);
        bind_guest(L, "player_no", l_actor_player_no);
        bind_guest(L, "param0", l_actor_param0);
        bind_guest(L, "param1", l_actor_param1);
        bind_guest(L, "pos", l_actor_pos);
        bind_guest(L, "set_pos", l_actor_set_pos);
        bind_guest(L, "speed", l_actor_speed);
        bind_guest(L, "set_speed", l_actor_set_speed);
        bind_guest(L, "scale", l_actor_scale);
        bind_guest(L, "set_scale", l_actor_set_scale);
        bind_guest(L, "delete", l_actor_delete);
        lua_setfield(L, -2, "__index");
    }
    lua_pop(L, 1);
}

int l_scene_installed(lua_State* L, uint32_t where) {
    uint32_t inst;
    std::string err;
    if (!lua_guest::read_u32(where, inst, err)) return fail(L, err.c_str());
    lua_pushboolean(L, inst != 0);
    return 1;
}

bool scene_object(uint32_t& out, std::string& err) {
    uint32_t scene = 0;
    if (!lua_guest::read_u32(kSceneSingleton, scene, err)) return false;
    if (scene == 0) {
        err = "scene not ready";
        return false;
    }
    out = scene;
    return true;
}

bool scene_now_creator(uint32_t& out, std::string& err) {
    uint32_t scene = 0;
    if (!scene_object(scene, err)) return false;
    uint32_t creator = 0;
    if (!lua_guest::read_u32(scene + kSceneNowCreatorOff, creator, err)) return false;
    if (creator == 0) {
        err = "scene not ready";
        return false;
    }
    out = creator;
    return true;
}

bool scene_state_is(lua_State* L, uint32_t creator) {
    uint32_t now = 0;
    std::string err;
    if (!scene_now_creator(now, err)) return fail(L, err.c_str());
    lua_pushboolean(L, now == creator);
    return 1;
}

int l_scene_title(lua_State* L) { return scene_state_is(L, kCreatorTitle); }

int l_scene_file_select(lua_State* L) { return scene_state_is(L, kCreatorFileSelect); }

int l_scene_save_select(lua_State* L) { return scene_state_is(L, kCreatorSaveSelect); }

int l_scene_course_task(lua_State* L) { return scene_state_is(L, kCreatorCourseTask); }

int l_scene_file_index(lua_State* L) {
    uint32_t mgr = 0;
    std::string err;
    if (!lua_guest::read_u32(kSubjectMgr, mgr, err)) return fail(L, err.c_str());
    if (mgr == 0) return fail(L, "scene not ready");
    uint8_t mode = 0;
    if (!lua_guest::read_u8(mgr + kSubjectModeOff, mode, err)) return fail(L, err.c_str());
    if (mode != kSubjectModeActive) return fail(L, "file not ready");
    uint8_t file = 0;
    if (!lua_guest::read_u8(mgr + kSubjectFileOff, file, err)) return fail(L, err.c_str());
    lua_pushnumber(L, (lua_Number)file);
    return 1;
}

int l_scene_flow(lua_State* L) { return scene_state_is(L, kCreatorFlow); }

int l_scene_map_phase(lua_State* L) { return scene_state_is(L, kCreatorMap); }

int l_scene_phase(lua_State* L) {
    uint32_t now = 0;
    std::string err;
    if (!scene_now_creator(now, err)) return fail(L, err.c_str());
    lua_pushnumber(L, (lua_Number)now);
    return 1;
}

int l_scene_course_select(lua_State* L) { return l_scene_installed(L, kCourseSelectInstance); }

int l_scene_course(lua_State* L) { return l_scene_installed(L, kCourseTaskInstance); }

uint32_t g_scene_request_src = 0;
uint32_t g_match_factory = 0;

extern "C" void nsmbu_note_scene_src(uint32_t src, uint32_t factory) {
    if (src == 0) return;
    if (factory == kCreatorTitle || factory == kCreatorFileSelect || factory == kCreatorFlow ||
        factory == kCreatorMenu)
        g_scene_request_src = src;
}

bool task_is_flow(uint32_t task, bool& match, std::string& err) {
    match = false;
    uint32_t vt = 0;
    if (!lua_guest::read_u32(task + kTaskVtblOff, vt, err)) return false;
    if (vt == kFlowVtbl) {
        match = true;
        return true;
    }
    uint32_t factory = 0;
    if (!lua_guest::read_u32(task + kTaskClassFactoryOff, factory, err)) return false;
    match = factory == kCreatorFlow;
    return true;
}

bool task_is_menu(uint32_t task, bool& match, std::string& err) {
    match = false;
    uint32_t vt = 0;
    if (!lua_guest::read_u32(task + kTaskVtblOff, vt, err)) return false;
    if (vt == kMenuVtbl) {
        match = true;
        return true;
    }
    uint32_t factory = 0;
    if (!lua_guest::read_u32(task + kTaskClassFactoryOff, factory, err)) return false;
    match = factory == kCreatorMenu;
    return true;
}

bool find_task_in_tree(uint32_t start, bool (*is_match)(uint32_t, bool&, std::string&), uint32_t& out,
                      std::string& err) {
    if (start == 0) return false;
    uint32_t root = start;
    for (int i = 0; i < 16; i++) {
        uint32_t parent = 0;
        if (!lua_guest::read_u32(root + kTaskParentOff, parent, err)) return false;
        if (parent == 0 || parent == root) break;
        root = parent;
    }
    uint32_t stack[48];
    uint32_t seen[48];
    int n = 0;
    int sn = 0;
    stack[n++] = root;
    while (n > 0) {
        uint32_t task = stack[--n];
        bool dup = false;
        for (int i = 0; i < sn; i++) {
            if (seen[i] == task) dup = true;
        }
        if (dup) continue;
        if (sn < 48) seen[sn++] = task;
        bool match = false;
        if (!is_match(task, match, err)) return false;
        if (match) {
            out = task;
            return true;
        }
        uint32_t child = 0;
        uint32_t sibling = 0;
        if (!lua_guest::read_u32(task + kTaskChildOff, child, err)) return false;
        if (!lua_guest::read_u32(task + kTaskSiblingOff, sibling, err)) return false;
        if (sibling != 0 && n < 48) stack[n++] = sibling;
        if (child != 0 && n < 48) stack[n++] = child;
    }
    return false;
}

bool find_flow_in_tree(uint32_t start, uint32_t& out, std::string& err) {
    return find_task_in_tree(start, task_is_flow, out, err);
}

bool find_menu_in_tree(uint32_t start, uint32_t& out, std::string& err) {
    return find_task_in_tree(start, task_is_menu, out, err);
}

bool find_task_in_list(uint32_t sentinel, bool (*is_match)(uint32_t, bool&, std::string&), uint32_t prefer,
                      uint32_t& out, bool& saw_prefer, std::string& err) {
    uint32_t node = 0;
    if (!lua_guest::read_u32(sentinel + kTaskListNextOff, node, err)) return false;
    for (int i = 0; i < 64; i++) {
        if (node == 0 || node == sentinel) return false;
        uint32_t task = 0;
        if (!lua_guest::read_u32(node + kTaskListDataOff, task, err)) return false;
        if (task != 0) {
            bool match = false;
            if (!is_match(task, match, err)) return false;
            if (match) {
                out = task;
                return true;
            }
            if (task == prefer) saw_prefer = true;
        }
        if (!lua_guest::read_u32(node + kTaskListNextOff, node, err)) return false;
    }
    return false;
}

bool find_flow_in_list(uint32_t sentinel, uint32_t prefer, uint32_t& out, bool& saw_prefer, std::string& err) {
    return find_task_in_list(sentinel, task_is_flow, prefer, out, saw_prefer, err);
}

bool find_menu_in_list(uint32_t sentinel, uint32_t prefer, uint32_t& out, bool& saw_prefer, std::string& err) {
    return find_task_in_list(sentinel, task_is_menu, prefer, out, saw_prefer, err);
}

bool scene_app_object(uint32_t& out, std::string& err) {
    uint32_t scene = 0;
    if (!scene_object(scene, err)) return false;
    uint32_t mgr = 0;
    if (!lua_guest::read_u32(scene + kSceneTaskMgrOff, mgr, err)) return false;
    bool saw_prefer = false;
    if (mgr != 0) {
        const uint32_t lists[] = {
            kTaskMgrPrepareOff, kTaskMgrPrepareDoneOff, kTaskMgrActiveOff, kTaskMgrStaticOff,
        };
        for (uint32_t off : lists) {
            if (find_flow_in_list(mgr + off, g_scene_request_src, out, saw_prefer, err)) return true;
            if (!err.empty()) return false;
        }
        uint32_t root = 0;
        if (!lua_guest::read_u32(mgr + kTaskMgrRootOff, root, err)) return false;
        if (find_flow_in_tree(root, out, err)) return true;
        if (!err.empty()) return false;
    }
    uint32_t area = 0;
    if (!lua_guest::read_u32(kAreaTaskInstance, area, err)) return false;
    if (find_flow_in_tree(area, out, err)) return true;
    if (!err.empty()) return false;
    if (saw_prefer && g_scene_request_src != 0) {
        out = g_scene_request_src;
        return true;
    }
    err = "flow task missing";
    return false;
}

bool scene_menu_object(uint32_t& out, std::string& err) {
    uint32_t scene = 0;
    if (!scene_object(scene, err)) return false;
    uint32_t mgr = 0;
    if (!lua_guest::read_u32(scene + kSceneTaskMgrOff, mgr, err)) return false;
    bool saw_prefer = false;
    if (mgr != 0) {
        const uint32_t lists[] = {
            kTaskMgrPrepareOff, kTaskMgrPrepareDoneOff, kTaskMgrActiveOff, kTaskMgrStaticOff,
        };
        for (uint32_t off : lists) {
            if (find_menu_in_list(mgr + off, 0, out, saw_prefer, err)) return true;
            if (!err.empty()) return false;
        }
        uint32_t root = 0;
        if (!lua_guest::read_u32(mgr + kTaskMgrRootOff, root, err)) return false;
        if (find_menu_in_tree(root, out, err)) return true;
        if (!err.empty()) return false;
    }
    uint32_t area = 0;
    if (!lua_guest::read_u32(kAreaTaskInstance, area, err)) return false;
    if (find_menu_in_tree(area, out, err)) return true;
    if (!err.empty()) return false;
    err = "menu missing";
    return false;
}

bool menu_state_ready(uint32_t menu, std::string& err) {
    uint32_t factory = 0;
    if (!lua_guest::read_u32(menu + kAppStateMethodOff, factory, err)) return false;
    if (factory == 0) {
        err = "state not ready";
        return false;
    }
    uint32_t factory_vt = 0;
    if (!lua_guest::read_u32(factory, factory_vt, err)) return false;
    if (factory_vt == 0) {
        err = "state not ready";
        return false;
    }
    uint32_t state = 0;
    if (!lua_guest::read_u32(menu + kAppStateMethodOff + 8, state, err)) return false;
    if (state != 0) {
        uint32_t state_vt = 0;
        if (!lua_guest::read_u32(state, state_vt, err)) return false;
        if (state_vt == 0) {
            err = "state not ready";
            return false;
        }
    }
    return true;
}

bool scene_change_open(std::string& err) {
    if (!t_guest_cpu) {
        err = "no guest cpu";
        return false;
    }
    uint32_t scene = 0;
    if (!scene_object(scene, err)) return false;
    uint32_t next_type = 0;
    if (!lua_guest::read_u32(scene + kSceneNextTypeOff, next_type, err)) return false;
    if (next_type != 0) {
        char buf[64];
        snprintf(buf, sizeof buf, "scene busy next=%08X", next_type);
        err = buf;
        return false;
    }
    uint32_t fader = 0;
    if (!lua_guest::read_u32(scene + kSceneFaderOff, fader, err)) return false;
    if (fader == 0) {
        err = "scene busy fader=none";
        return false;
    }
    uint32_t busy = guest_call(t_guest_cpu, kFaderBusy, {fader});
    if (busy != 0) {
        char buf[64];
        snprintf(buf, sizeof buf, "scene busy fader=%08X", busy);
        err = buf;
        return false;
    }
    return true;
}

bool scene_transition(uint32_t creator, uint32_t fader, std::string& err) {
    if (!scene_change_open(err)) return false;
    uint32_t scene = 0;
    uint32_t app = 0;
    if (!scene_object(scene, err)) return false;
    if (!scene_app_object(app, err)) return false;
    uint32_t pair = mem::host_alloc(8, 4);
    st32(pair, kClassIdTask);
    st32(pair + 4, creator);
    if (guest_call(t_guest_cpu, kSetNextScene, {scene, app, pair, fader, 0}) == 0) {
        err = "scene busy";
        return false;
    }
    return true;
}

int l_scene_open_title(lua_State* L) {
    std::string err;
    if (!scene_transition(kCreatorTitle, 0, err)) return fail(L, err.c_str());
    lua_pushboolean(L, 1);
    return 1;
}

bool continue_source(uint32_t& out, std::string& err) {
    uint32_t course = 0;
    if (!lua_guest::read_u32(kCourseTaskInstance, course, err)) return false;
    if (course != 0) {
        uint32_t head = 0;
        if (!lua_guest::read_u32(course, head, err)) return false;
        if (head != 0) {
            uint32_t src = 0;
            if (!lua_guest::read_u32(head + kCourseTaskSrcOff, src, err)) return false;
            if (src != 0) {
                out = src;
                return true;
            }
        }
    }
    return scene_app_object(out, err);
}

bool save_file_arg(lua_State* L, uint32_t& file, std::string& err) {
    file = 0;
    if (lua_gettop(L) >= 1 && !lua_isnil(L, 1)) {
        if (!lua_isnumber(L, 1)) {
            err = "bad type";
            return false;
        }
        lua_Number n = lua_tonumber(L, 1);
        if (n < 0 || n >= (lua_Number)kSaveFileCount || n != (lua_Number)(uint32_t)n) {
            err = "bad file";
            return false;
        }
        file = (uint32_t)n;
    }
    return true;
}

bool apply_save_file(uint32_t file, int32_t world, int32_t course, bool entry, std::string& err) {
    if (!scene_change_open(err)) return false;
    uint32_t mgr = 0;
    if (!lua_guest::read_u32(kSaveMgr, mgr, err)) return false;
    if (mgr == 0) {
        err = "file not ready";
        return false;
    }
    uint32_t savedata = 0;
    if (!lua_guest::read_u32(mgr + kSaveDataOff, savedata, err)) return false;
    if (savedata == 0) {
        err = "file not ready";
        return false;
    }
    if (!lua_guest::write_u8(savedata + kLastFileOff, (uint8_t)file, err)) return false;
    uint32_t field = 0;
    if (!lua_guest::read_u32(kFieldGameSingleton, field, err)) return false;
    if (field == 0) {
        err = "file not ready";
        return false;
    }
    if (entry) {
        uint32_t info = 0;
        if (!lua_guest::read_u32(kCourseInfo, info, err)) return false;
        if (info == 0) {
            err = "file not ready";
            return false;
        }
        if (!lua_guest::write_u8(info + kCourseInfoNextWorldOff, (uint8_t)world, err)) return false;
        if (!lua_guest::write_u8(info + kCourseInfoNextCourseOff, (uint8_t)course, err)) return false;
        ::nsmbu_banner_entry_arm();
    }
    uint32_t gate = 0;
    if (!lua_guest::read_u32(kSaveApplyFlag, gate, err)) return false;
    if (gate != 0 && !lua_guest::write_u8(gate + kSaveApplyFlagOff, 0, err)) return false;
    if (!dispatch::lookup(kApplySaveFile)) {
        err = "applySaveFile is not in this build; install the matching 1.3.0 update";
        return false;
    }
    guest_call(t_guest_cpu, kApplySaveFile, {mgr, file});
    uint32_t game = 0;
    if (!lua_guest::read_u32(kMainGame, game, err)) return false;
    if (game == 0) {
        err = "file not ready";
        return false;
    }
    if (!lua_guest::write_u8(game + kSinglePlayerDrc, input::pro_controller() ? 0 : 1, err)) return false;
    return true;
}

int l_scene_apply_save(lua_State* L) {
    uint32_t file = 0;
    std::string err;
    if (!save_file_arg(L, file, err)) return fail(L, err.c_str());
    int32_t world = 0;
    int32_t course = 0;
    bool entry = lua_gettop(L) >= 2;
    if (entry) {
        if (!lua_isnumber(L, 2) || !lua_isnumber(L, 3)) return fail(L, "bad type");
        world = (int32_t)lua_tonumber(L, 2);
        course = (int32_t)lua_tonumber(L, 3);
        if (world < 0 || world > 8 || course < 0 || course > 255) return fail(L, "bad entry");
    }
    if (!apply_save_file(file, world, course, entry, err)) return fail(L, err.c_str());
    lua_pushboolean(L, 1);
    return 1;
}

int l_scene_open_first_save(lua_State* L) {
    uint32_t file = 0;
    std::string err;
    if (!save_file_arg(L, file, err)) return fail(L, err.c_str());
    if (!apply_save_file(file, 0, 0, false, err)) return fail(L, err.c_str());
    uint32_t src = 0;
    if (!continue_source(src, err)) return fail(L, err.c_str());
    guest_call(t_guest_cpu, kOpenFirstSave, {src});
    lua_pushboolean(L, 1);
    return 1;
}

int l_scene_open_file_select(lua_State* L) {
    std::string err;
    if (!scene_change_open(err)) return fail(L, err.c_str());
    uint32_t menu = 0;
    if (!scene_menu_object(menu, err)) return fail(L, err.c_str());
    if (!menu_state_ready(menu, err)) return fail(L, err.c_str());
    uint32_t mgr = 0;
    if (!lua_guest::read_u32(kSubjectMgr, mgr, err)) return fail(L, err.c_str());
    if (mgr == 0) return fail(L, "file not ready");
    if (guest_call(t_guest_cpu, kSubjectSelectFile, {mgr, 0}) == 0) return fail(L, "file not ready");
    guest_call(t_guest_cpu, kAppEnterFileState, {menu});
    lua_pushboolean(L, 1);
    return 1;
}

int l_scene_open_course_select(lua_State* L) {
    std::string err;
    if (!scene_transition(kCreatorCourseSelect, 0x000A000Au, err)) return fail(L, err.c_str());
    lua_pushboolean(L, 1);
    return 1;
}

bool leave_course_impl(std::string& err) {
    if (!t_guest_cpu) {
        err = "no guest cpu";
        return false;
    }
    uint32_t area = 0;
    if (!lua_guest::read_u32(kAreaTaskInstance, area, err)) return false;
    if (area == 0) {
        err = "no course";
        return false;
    }
    guest_call(t_guest_cpu, kRequestCourseOut, {area});
    nsmbu_mark_course_left();
    return true;
}

int l_scene_leave_course(lua_State* L) {
    std::string err;
    if (!leave_course_impl(err)) return fail(L, err.c_str());
    lua_pushboolean(L, 1);
    return 1;
}

int l_scene_restart(lua_State* L) {
    (void)luaL_optstring(L, 1, "playtest");
    if (!render::restart()) return fail(L, "restart failed");
    lua_pushboolean(L, 1);
    return 1;
}

int l_scene_confirm_file(lua_State* L) {
    std::string err;
    if (!scene_change_open(err)) return fail(L, err.c_str());
    uint32_t menu = 0;
    if (!scene_menu_object(menu, err)) return fail(L, err.c_str());
    if (!menu_state_ready(menu, err)) return fail(L, err.c_str());
    if (!lua_guest::write_u8(menu + kAppGateSelect, 1, err)) return fail(L, err.c_str());
    if (!lua_guest::write_u8(menu + kAppGateStory, 1, err)) return fail(L, err.c_str());
    guest_call(t_guest_cpu, kAppConfirmFile, {menu});
    lua_pushboolean(L, 1);
    return 1;
}

bool scene_story_bounds(int32_t world, int32_t course) {
    return world >= 0 && world <= 8 && course >= 0 && course <= 255;
}

bool parse_course_pack(const char* s, size_t len, int32_t& world, int32_t& course) {
    if (len == 0) return false;
    size_t i = 0;
    if (s[i] < '1' || s[i] > '9') return false;
    int w = s[i++] - '0';
    if (i >= len || s[i++] != '-') return false;
    if (i >= len || s[i] < '1' || s[i] > '9') return false;
    int p = s[i++] - '0';
    while (i < len) {
        if (s[i] < '0' || s[i] > '9') return false;
        if (p > 256) return false;
        p = p * 10 + (s[i++] - '0');
    }
    if (p < 1 || p > 256) return false;
    world = w - 1;
    course = p - 1;
    return scene_story_bounds(world, course);
}

bool task_factory_match(uint32_t task, bool& match, std::string& err) {
    match = false;
    uint32_t factory = 0;
    if (!lua_guest::read_u32(task + kTaskClassFactoryOff, factory, err)) return false;
    match = factory != 0 && factory == g_match_factory;
    return true;
}

bool singleton_task(uint32_t addr, uint32_t& out, std::string& err) {
    uint32_t task = 0;
    if (!lua_guest::read_u32(addr, task, err)) return false;
    if (task == 0) return false;
    uint32_t vt = 0;
    if (!lua_guest::read_u32(task + kTaskVtblOff, vt, err)) return false;
    if (vt == 0) return false;
    out = task;
    return true;
}

bool scene_story_source(uint32_t& out, std::string& err) {
    if (scene_app_object(out, err)) return true;
    if (err != "flow task missing") return false;
    err.clear();
    uint32_t scene = 0;
    if (!scene_object(scene, err)) return false;
    uint32_t creator = 0;
    if (!lua_guest::read_u32(scene + kSceneNowCreatorOff, creator, err)) return false;
    if (creator == kCreatorCourseSelect && singleton_task(kCourseSelectInstance, out, err)) return true;
    if (!err.empty()) return false;
    if (creator == kCreatorCourseTask && singleton_task(kCourseTaskInstance, out, err)) return true;
    if (!err.empty()) return false;
    if (creator != 0) {
        g_match_factory = creator;
        uint32_t mgr = 0;
        if (!lua_guest::read_u32(scene + kSceneTaskMgrOff, mgr, err)) return false;
        bool saw = false;
        if (mgr != 0) {
            const uint32_t lists[] = {
                kTaskMgrPrepareOff, kTaskMgrPrepareDoneOff, kTaskMgrActiveOff, kTaskMgrStaticOff,
            };
            for (uint32_t off : lists) {
                if (find_task_in_list(mgr + off, task_factory_match, 0, out, saw, err)) return true;
                if (!err.empty()) return false;
            }
            uint32_t root = 0;
            if (!lua_guest::read_u32(mgr + kTaskMgrRootOff, root, err)) return false;
            if (find_task_in_tree(root, task_factory_match, out, err)) return true;
            if (!err.empty()) return false;
        }
        uint32_t area = 0;
        if (!lua_guest::read_u32(kAreaTaskInstance, area, err)) return false;
        if (find_task_in_tree(area, task_factory_match, out, err)) return true;
        if (!err.empty()) return false;
    }
    if (singleton_task(kCourseSelectInstance, out, err)) return true;
    if (!err.empty()) return false;
    if (singleton_task(kCourseTaskInstance, out, err)) return true;
    if (!err.empty()) return false;
    err = "flow task missing";
    return false;
}

bool scene_load_story_transition(int32_t world, int32_t course, std::string& err) {
    if (!t_guest_cpu) {
        err = "no guest cpu";
        return false;
    }
    uint32_t live_course = 0;
    std::string live_err;
    lua_guest::read_u32(kCourseTaskInstance, live_course, live_err);
    if (live_course != 0) {
        if (!g_story_leave_queued.load(std::memory_order_acquire)) {
            if (!scene_change_open(err)) return false;
            uint32_t scene = 0;
            uint32_t app = 0;
            if (!scene_object(scene, err)) return false;
            if (!scene_story_source(app, err)) return false;
            if (app == 0) {
                err = "flow task missing";
                return false;
            }
            uint32_t pair = mem::host_alloc(8, 4);
            st32(pair, kClassIdTask);
            st32(pair + 4, kCreatorTitle);
            if (guest_call(t_guest_cpu, kSetNextScene, {scene, app, pair, 0, 0}) == 0) {
                err = "scene busy";
                return false;
            }
            g_story_leave_queued.store(true, std::memory_order_release);
        }
        err = "leaving course";
        return false;
    }
    if (!scene_change_open(err)) return false;
    uint32_t scene = 0;
    uint32_t app = 0;
    if (!scene_object(scene, err)) return false;
    if (!scene_story_source(app, err)) return false;
    if (app == 0) {
        err = "flow task missing";
        return false;
    }
    g_story_leave_queued.store(false, std::memory_order_release);
    uint32_t info = ld32(GD(0x101D18EC));
    if (info != 0) {
        st8(info + 0x1C, ld8(info + 0x10));
        st8(info + 0x1D, ld8(info + 0x11));
    }
    if (guest_call(t_guest_cpu, kSetCourseStory, {scene, app, (uint32_t)world, (uint32_t)course}) == 0) {
        err = "scene busy";
        return false;
    }
    g_story_load_generation.fetch_add(1, std::memory_order_acq_rel);
    ::nsmbu_banner_entry_arm();
    ::nsmbu_fill_course_banner_digits();
    return true;
}

struct NativeSkip {
    bool active = false;
    bool done = false;
    bool opened = false;
    bool requested = false;
    int file_select_steps = 0;
    int popup_pulses = 0;
    uint8_t world = 0;
    uint8_t course = 0;
    std::string last;
};

NativeSkip g_native_skip;
char g_native_skip_owner;

void native_note(uint64_t step, const std::string& message) {
    if (message == g_native_skip.last) return;
    g_native_skip.last = message;
    LOG("[open] step %llu %s", (unsigned long long)step, message.c_str());
}

void native_intro_skip_tick_impl(uint64_t step) {
    static const std::string pack = mods::packages::open_pack_value();
    if (pack.empty()) return;
    if (!g_native_skip.active) {
        int world = 0;
        int course = 0;
        if (std::sscanf(pack.c_str(), "%d-%d", &world, &course) != 2) return;
        if (world < 1 || world > 9 || course < 1 || course > 256) return;
        g_native_skip.active = true;
        g_native_skip.world = (uint8_t)(world - 1);
        g_native_skip.course = (uint8_t)(course - 1);
        native_note(step, "wait boot");
    }
    if (g_native_skip.done) return;

    uint32_t creator = 0;
    std::string err;
    if (!scene_now_creator(creator, err)) {
        native_note(step, "wait scene");
        return;
    }
    if (creator == kCreatorSaveSelect) {
        g_native_skip.done = true;
        native_note(step, "unexpected challenge select");
        return;
    }
    uint32_t task = 0;
    std::string task_err;
    const bool in_course = lua_guest::read_u32(kCourseTaskInstance, task, task_err) && task != 0;
    if (g_native_skip.requested) {
        if (creator == kCreatorCourseTask || in_course) {
            g_native_skip.done = true;
            native_note(step, "course up");
        }
        return;
    }
    if (creator == kCreatorBoot) {
        native_note(step, "wait boot");
        return;
    }
    if (creator == kCreatorFileSelect) {
        g_native_skip.file_select_steps++;
        if (g_native_skip.popup_pulses < 4) {
            if (g_native_skip.file_select_steps % 30 == 0) {
                lua_press::press(&g_native_skip_owner, input::kA, 3, step);
                g_native_skip.popup_pulses++;
                native_note(step, "press A file_select");
            }
        } else {
            std::string title_err;
            if (scene_transition(kCreatorTitle, 0, title_err)) native_note(step, "open_title");
            else native_note(step, "open_title " + title_err);
        }
        return;
    }
    g_native_skip.file_select_steps = 0;
    if (!g_native_skip.opened) {
        std::string save_err;
        if (!apply_save_file(0, g_native_skip.world, g_native_skip.course, true, save_err)) {
            native_note(step, "apply_save " + save_err);
            return;
        }
        g_native_skip.opened = true;
        char message[64];
        std::snprintf(message, sizeof message, "apply save 1 entry %d-%d",
                      g_native_skip.world + 1, g_native_skip.course + 1);
        native_note(step, message);
    }
    if (!g_native_skip.requested) {
        std::string load_err;
        if (!scene_load_story_transition(g_native_skip.world, g_native_skip.course, load_err)) {
            native_note(step, "load_pack " + load_err);
            return;
        }
        g_native_skip.requested = true;
        char message[64];
        std::snprintf(message, sizeof message, "load_pack %d-%d",
                      g_native_skip.world + 1, g_native_skip.course + 1);
        native_note(step, message);
    }
}

int l_scene_load_story(lua_State* L) {
    if (!lua_isnumber(L, 1) || !lua_isnumber(L, 2)) return fail(L, "bad type");
    int32_t world = (int32_t)lua_tonumber(L, 1);
    int32_t course = (int32_t)lua_tonumber(L, 2);
    if (world < 0 || world > 8) return fail(L, "bad world");
    if (course < 0 || course > 255) return fail(L, "bad course");
    std::string err;
    if (!scene_load_story_transition(world, course, err)) return fail(L, err.c_str());
    lua_pushboolean(L, 1);
    return 1;
}

int l_scene_load_pack(lua_State* L) {
    if (!lua_isstring(L, 1)) return fail(L, "bad type");
    size_t len = 0;
    const char* name = lua_tolstring(L, 1, &len);
    int32_t world = 0;
    int32_t course = 0;
    if (!parse_course_pack(name, len, world, course)) return fail(L, "bad pack");
    std::string err;
    if (!scene_load_story_transition(world, course, err)) return fail(L, err.c_str());
    lua_pushboolean(L, 1);
    return 1;
}

int l_hooks_call(lua_State* L) {
    if (!t_guest_cpu) return fail(L, "no guest cpu");
    if (!lua_isnumber(L, 1)) return fail(L, "bad type");
    lua_Number addr = lua_tonumber(L, 1);
    if (addr < 0 || addr > (lua_Number)0xFFFFFFFFu || addr != (lua_Number)(uint32_t)addr) return fail(L, "bad address");
    uint32_t fn = (uint32_t)addr;
    if (!dispatch::lookup(fn)) return fail(L, "not a game function");
    uint32_t regs[8] = {};
    if (lua_gettop(L) >= 2 && !lua_isnil(L, 2)) {
        if (!lua_istable(L, 2)) return fail(L, "bad type");
        static const char* names[] = {"r3", "r4", "r5", "r6", "r7", "r8", "r9", "r10"};
        for (int i = 0; i < 8; i++) {
            lua_getfield(L, 2, names[i]);
            if (!lua_isnil(L, -1)) {
                if (!lua_isnumber(L, -1)) {
                    lua_pop(L, 1);
                    return fail(L, "bad type");
                }
                lua_Number v = lua_tonumber(L, -1);
                if (v < 0 || v > (lua_Number)0xFFFFFFFFu || v != (lua_Number)(uint32_t)v) {
                    lua_pop(L, 1);
                    return fail(L, "bad register");
                }
                regs[i] = (uint32_t)v;
            }
            lua_pop(L, 1);
        }
    }
    static thread_local int depth = 0;
    if (depth >= 8) return fail(L, "call nested too deep");
    depth++;
    guest_call(t_guest_cpu, fn, {regs[0], regs[1], regs[2], regs[3], regs[4], regs[5], regs[6], regs[7]});
    depth--;
    lua_pushboolean(L, 1);
    return 1;
}

void install_game(lua_State* L) {
    install_player_meta(L);
    install_actor_meta(L);

    lua_newtable(L);
    bind_guest(L, "active", l_course_active);
    bind_guest(L, "time", l_course_time);
    bind_guest(L, "set_time", l_course_set_time);
    bind_guest(L, "add_time", l_course_add_time);
    bind_guest(L, "coins", l_course_coins);
    bind_guest(L, "set_coins", l_course_set_coins);
    bind_guest(L, "score", l_course_score);
    bind_guest(L, "set_score", l_course_set_score);
    bind_guest(L, "actors", l_level_actors);
    bind_guest(L, "count", l_level_count);
    bind_guest(L, "spawn", l_level_spawn);
    set_loaded(L, "nsmbu.Level");

    lua_newtable(L);
    bind_guest(L, "get_player", l_player);
    bind_guest(L, "get_players", l_players);
    set_loaded(L, "nsmbu.Player");

    lua_newtable(L);
    bind_guest(L, "title", l_scene_title);
    bind_guest(L, "file_select", l_scene_file_select);
    bind_guest(L, "save_select", l_scene_save_select);
    bind_guest(L, "course_task", l_scene_course_task);
    bind_guest(L, "file_index", l_scene_file_index);
    bind_guest(L, "flow_phase", l_scene_flow);
    bind_guest(L, "map_phase", l_scene_map_phase);
    bind_guest(L, "phase", l_scene_phase);
    bind_guest(L, "course_select", l_scene_course_select);
    bind_guest(L, "course", l_scene_course);
    bind_guest(L, "open_title", l_scene_open_title);
    bind_guest(L, "apply_save", l_scene_apply_save);
    bind_guest(L, "open_first_save", l_scene_open_first_save);
    bind_guest(L, "open_file_select", l_scene_open_file_select);
    bind_guest(L, "open_course_select", l_scene_open_course_select);
    bind_guest(L, "leave_course", l_scene_leave_course);
    bind_guest(L, "restart", l_scene_restart);
    bind_guest(L, "confirm_file", l_scene_confirm_file);
    bind_guest(L, "load_story", l_scene_load_story);
    bind_guest(L, "load_pack", l_scene_load_pack);
    lua_newtable(L);
    lua_pushnumber(L, (lua_Number)kCreatorBoot);
    lua_setfield(L, -2, "Boot");
    lua_pushnumber(L, (lua_Number)kCreatorFlow);
    lua_setfield(L, -2, "Flow");
    lua_pushnumber(L, (lua_Number)kCreatorFileSelect);
    lua_setfield(L, -2, "FileSelect");
    lua_pushnumber(L, (lua_Number)kCreatorSaveSelect);
    lua_setfield(L, -2, "SaveSelect");
    lua_pushnumber(L, (lua_Number)kCreatorTitle);
    lua_setfield(L, -2, "Title");
    lua_pushnumber(L, (lua_Number)kCreatorMenu);
    lua_setfield(L, -2, "Menu");
    lua_pushnumber(L, (lua_Number)kCreatorMap);
    lua_setfield(L, -2, "Map");
    lua_pushnumber(L, (lua_Number)kCreatorCourseTask);
    lua_setfield(L, -2, "CourseTask");
    lua_pushnumber(L, (lua_Number)kCreatorCourseSelect);
    lua_setfield(L, -2, "CourseSelect");
    lua_setfield(L, -2, "Factory");
    set_loaded(L, "nsmbu.Scene");
}

void install_global(lua_State* L) {
    if (!push_loaded(L, "nsmbu")) {
        lua_pop(L, 1);
        return;
    }
    attach_loaded(L, "nsmbu.Config", "Config");
    attach_loaded(L, "nsmbu.Guest", "Guest");
    attach_loaded(L, "nsmbu.Fn", "Fn");
    attach_loaded(L, "nsmbu.Hooks", "Hooks");
    attach_loaded(L, "nsmbu.Input", "Input");
    attach_loaded(L, "nsmbu.Gui", "Gui");
    attach_loaded(L, "nsmbu.Data", "Data");
    attach_loaded(L, "nsmbu.Enemy", "Enemy");
    attach_loaded(L, "nsmbu.Boss", "Boss");
    attach_loaded(L, "nsmbu.Blocks", "Blocks");
    attach_loaded(L, "nsmbu.Env", "Env");
    attach_loaded(L, "nsmbu.Level", "Level");
    attach_loaded(L, "nsmbu.Player", "Player");
    attach_loaded(L, "nsmbu.Scene", "Scene");
    lua_setglobal(L, "nsmbu");
}

void install_modules(lua_State* L, Instance* inst) {
    lua_newtable(L);

    bind(L, inst, "log", l_log);
    bind(L, inst, "log_int", l_log_int);
    bind(L, inst, "log_hex", l_log_hex);
    bind(L, inst, "log_float", l_log_float);
    bind(L, inst, "status", l_status);
    bind(L, inst, "logic_step", l_logic_step);
    bind(L, inst, "logic_dt", l_logic_dt);

    lua_newtable(L);
    lua_pushlstring(L, inst->id.data(), inst->id.size());
    lua_setfield(L, -2, "id");
    lua_pushlstring(L, inst->version.data(), inst->version.size());
    lua_setfield(L, -2, "version");
    lua_pushlstring(L, inst->path.data(), inst->path.size());
    lua_setfield(L, -2, "path");
    lua_setfield(L, -2, "Package");

    set_loaded(L, "nsmbu");

    lua_newtable(L);
    bind(L, inst, "string", l_config_string);
    bind(L, inst, "number", l_config_number);
    bind(L, inst, "bool", l_config_bool);
    set_loaded(L, "nsmbu.Config");

    lua_newtable(L);
    bind_guest(L, "read_u8", guest_read_u8);
    bind_guest(L, "read_s8", guest_read_s8);
    bind_guest(L, "read_u16", guest_read_u16);
    bind_guest(L, "read_s16", guest_read_s16);
    bind_guest(L, "read_u32", guest_read_u32);
    bind_guest(L, "read_s32", guest_read_s32);
    bind_guest(L, "read_f32", guest_read_f32);
    bind_guest(L, "read_f64", guest_read_f64);
    bind_guest(L, "read", guest_read);
    bind_guest(L, "write_u8", guest_write_u8);
    bind_guest(L, "write_s8", guest_write_s8);
    bind_guest(L, "write_u16", guest_write_u16);
    bind_guest(L, "write_s16", guest_write_s16);
    bind_guest(L, "write_u32", guest_write_u32);
    bind_guest(L, "write_s32", guest_write_s32);
    bind_guest(L, "write_f32", guest_write_f32);
    bind_guest(L, "write_f64", guest_write_f64);
    bind_guest(L, "write", guest_write);
    set_loaded(L, "nsmbu.Guest");

    set_empty_module(L, "nsmbu.Fn");
    lua_newtable(L);
    bind_guest(L, "call", l_hooks_call);
    set_loaded(L, "nsmbu.Hooks");
    lua_newtable(L);
    bind(L, inst, "press", l_input_press);
    lua_newtable(L);
    bind(L, inst, "held", l_input_held);
    bind(L, inst, "pressed", l_input_pressed);
    static const struct { const char* name; uint32_t bit; } kButtons[] = {
        {"A", input::kA},       {"B", input::kB},       {"X", input::kX},
        {"Y", input::kY},       {"L", input::kL},       {"R", input::kR},
        {"ZL", input::kZL},     {"ZR", input::kZR},     {"Plus", input::kPlus},
        {"Minus", input::kMinus}, {"Home", input::kHome},
        {"Up", input::kUp},     {"Down", input::kDown},
        {"Left", input::kLeft}, {"Right", input::kRight},
        {"StickL", input::kStickL}, {"StickR", input::kStickR},
    };
    for (const auto& entry : kButtons) {
        lua_pushnumber(L, (lua_Number)entry.bit);
        lua_setfield(L, -2, entry.name);
    }
    lua_setfield(L, -2, "button");
    lua_newtable(L);
    bind(L, inst, "__call", l_input_call);
    lua_setmetatable(L, -2);
    set_loaded(L, "nsmbu.Input");

    lua_newtable(L);
    bind(L, inst, "clear", l_gui_clear);
    bind(L, inst, "rect", l_gui_rect);
    bind(L, inst, "rect_filled", l_gui_rect_filled);
    bind(L, inst, "line", l_gui_line);
    bind(L, inst, "circle", l_gui_circle);
    bind(L, inst, "circle_filled", l_gui_circle_filled);
    bind(L, inst, "triangle_filled", l_gui_triangle_filled);
    bind(L, inst, "text", l_gui_text);
    set_loaded(L, "nsmbu.Gui");
    set_empty_module(L, "nsmbu.Data");
    set_empty_module(L, "nsmbu.Enemy");
    set_empty_module(L, "nsmbu.Boss");
    set_empty_module(L, "nsmbu.Blocks");
    set_empty_module(L, "nsmbu.Env");

    install_game(L);
}
}

void forget(Instance* inst) {
    if (!inst) return;
    lua_gui::drop(inst->id);
    std::lock_guard guard(g_input_mu);
    g_input_samples.erase(inst);
}

void install(lua_State* L, Instance* inst) {
    install_modules(L, inst);
    install_global(L);
}

std::atomic<int> g_load_story{-1};
std::mutex g_load_story_mu;
std::string g_load_story_status;

bool story_leave_pending() {
    return g_story_leave_queued.load(std::memory_order_acquire);
}

uint64_t story_load_generation() {
    return g_story_load_generation.load(std::memory_order_acquire);
}

void native_intro_skip_tick(uint64_t step) {
    native_intro_skip_tick_impl(step);
}

namespace {

bool pack_bytes(uint8_t world, uint8_t course) {
    return world <= 8 && course <= 255;
}

void format_pack(uint8_t world, uint8_t course, std::string& out) {
    char buf[16];
    std::snprintf(buf, sizeof buf, "%u-%u", unsigned(world) + 1u, unsigned(course) + 1u);
    out = buf;
}

bool read_ident(uint32_t ea, std::string& out) {
    out.clear();
    for (uint32_t i = 0; i < 16; i++) {
        uint8_t c = 0;
        std::string err;
        if (!lua_guest::read_u8(ea + i, c, err)) return false;
        if (c == 0) break;
        const bool ok = (c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '_';
        if (!ok) break;
        out.push_back((char)c);
    }
    return !out.empty();
}

void fill_map_place(SceneReport& out) {
    std::string err;
    uint32_t area = 0;
    if (lua_guest::read_u32(kAreaTaskInstance, area, err)) {
        out.area_known = true;
        out.area = area != 0;
    }
    if (!out.map || !out.world_known || !out.node_known) return;
    uint32_t cs = 0;
    err.clear();
    if (lua_guest::read_u32(kCourseSelectInstance, cs, err) && cs != 0) {
        uint8_t lock = 0;
        err.clear();
        if (lua_guest::read_u8(cs + 0xDC9u, lock, err)) {
            out.map_lock_known = true;
            out.map_lock = lock;
        }
    }
    uint32_t game = 0;
    err.clear();
    if (!lua_guest::read_u32(kMainGame, game, err) || game == 0) return;
    int32_t section = 0;
    err.clear();
    if (!lua_guest::read_s32(game + 0x1A0u, section, err)) return;
    if (out.world < 0 || section < 0 || section > 1 || out.node < 0 || out.node > 255) return;
    uint32_t mgr = 0;
    err.clear();
    if (!lua_guest::read_u32(kCourseSelectMgr, mgr, err) || mgr == 0) return;
    uint32_t path = 0;
    err.clear();
    const uint32_t path_off = 0x60u + (uint32_t)(out.world * 2 + section) * 4u;
    if (!lua_guest::read_u32(mgr + path_off, path, err) || path == 0) return;
    const uint32_t base = path + 0x10u + (uint32_t)out.node * kPathNodeSize;
    uint32_t name_ptr = 0;
    err.clear();
    if (!lua_guest::read_u32(base, name_ptr, err) || name_ptr == 0) return;
    std::string name;
    if (!read_ident(name_ptr, name)) return;
    float nx = 0;
    float ny = 0;
    err.clear();
    if (!lua_guest::read_f32(base + 0x44u, nx, err)) return;
    err.clear();
    if (!lua_guest::read_f32(base + 0x48u, ny, err)) return;
    int32_t title = -1;
    err.clear();
    lua_guest::read_s32(base + 0x50u, title, err);
    out.node_name = std::move(name);
    out.node_pos_known = true;
    out.node_x = nx;
    out.node_y = ny;
    out.map_title_known = true;
    out.map_title = title;

    uint32_t begin = 0;
    uint32_t end = 0;
    err.clear();
    if (!live_actor_range(begin, end, err)) return;
    int players = 0;
    bool got = false;
    float px = 0;
    float py = 0;
    for (uint32_t slot = begin; slot < end; slot += kActorIdxToPtr) {
        uint32_t actor = 0;
        err.clear();
        if (!lua_guest::read_u32(slot, actor, err) || actor == 0) continue;
        uint32_t id = 0;
        if (!actor_profile_id(actor, id, err) || id != kMapPlayerProfile) continue;
        players++;
        if (got) continue;
        float x = 0;
        float y = 0;
        err.clear();
        if (!lua_guest::read_f32(actor + 0x50u, x, err)) continue;
        err.clear();
        if (!lua_guest::read_f32(actor + 0x54u, y, err)) continue;
        px = x;
        py = y;
        got = true;
    }
    out.map_players_known = true;
    out.map_players = players;
    if (got) {
        out.map_player_known = true;
        out.map_x = px;
        out.map_y = py;
    }
}

}

bool leave_course(std::string& err) {
    return leave_course_impl(err);
}

void scene_report(SceneReport& out) {
    out = {};
    std::string err;
    uint32_t creator = 0;
    if (scene_now_creator(creator, err)) {
        if (creator == kCreatorBoot) out.scene = "Boot";
        else if (creator == kCreatorFlow) out.scene = "Flow";
        else if (creator == kCreatorFileSelect) out.scene = "FileSelect";
        else if (creator == kCreatorSaveSelect) out.scene = "SaveSelect";
        else if (creator == kCreatorTitle) out.scene = "Title";
        else if (creator == kCreatorMenu) out.scene = "Menu";
        else if (creator == kCreatorMap) out.scene = "Map";
        else if (creator == kCreatorCourseTask) out.scene = "CourseTask";
        else if (creator == kCreatorCourseSelect) out.scene = "CourseSelect";
    }

    uint32_t map = 0;
    err.clear();
    if (lua_guest::read_u32(kCourseSelectInstance, map, err)) {
        out.map_known = true;
        out.map = map != 0;
    }
    uint32_t course = 0;
    err.clear();
    if (lua_guest::read_u32(kCourseTaskInstance, course, err)) {
        out.course_known = true;
        out.course = course != 0;
    }

    uint32_t info = 0;
    err.clear();
    if (lua_guest::read_u32(kCourseInfo, info, err) && info != 0) {
        uint8_t now_w = 0, now_c = 0, next_w = 0, next_c = 0;
        err.clear();
        const bool now_ok = lua_guest::read_u8(info + 0x10u, now_w, err) && lua_guest::read_u8(info + 0x11u, now_c, err);
        err.clear();
        const bool next_ok = lua_guest::read_u8(info + kCourseInfoNextWorldOff, next_w, err) &&
                             lua_guest::read_u8(info + kCourseInfoNextCourseOff, next_c, err);
        const bool now_pack = now_ok && pack_bytes(now_w, now_c);
        const bool next_pack = next_ok && pack_bytes(next_w, next_c);
        if (now_pack) format_pack(now_w, now_c, out.pack);
        if (now_pack && next_pack && (next_w != now_w || next_c != now_c))
            format_pack(next_w, next_c, out.entering);
    }

    uint32_t mgr = 0;
    err.clear();
    if (lua_guest::read_u32(kSubjectMgr, mgr, err) && mgr != 0) {
        uint8_t mode = 0;
        uint8_t file = 0;
        err.clear();
        if (lua_guest::read_u8(mgr + kSubjectModeOff, mode, err) && mode == kSubjectModeActive &&
            lua_guest::read_u8(mgr + kSubjectFileOff, file, err)) {
            out.file_known = true;
            out.file = file;
        }
    }

    uint32_t field = 0;
    err.clear();
    if (lua_guest::read_u32(kFieldGameSingleton, field, err) && field != 0) {
        int32_t cp = 0;
        err.clear();
        if (lua_guest::read_s32(field + 0x10u, cp, err)) {
            out.cp_known = true;
            out.cp = cp;
        }
    }

    if (out.map) {
        uint32_t game = 0;
        err.clear();
        if (lua_guest::read_u32(kMainGame, game, err) && game != 0) {
            int32_t world = 0;
            int32_t node = 0;
            err.clear();
            if (lua_guest::read_s32(game + 0x19Cu, world, err)) {
                out.world_known = true;
                out.world = world;
            }
            err.clear();
            if (lua_guest::read_s32(game + 0x1A4u, node, err)) {
                out.node_known = true;
                out.node = node;
            }
        }
    }
    fill_map_place(out);
}

void request_load_story(int world, int course) {
    if (world < 0 || world > 8 || course < 0 || course > 255) return;
    {
        std::lock_guard<std::mutex> lk(g_load_story_mu);
        g_load_story_status.clear();
    }
    g_load_story.store((world << 8) | course, std::memory_order_release);
    LOG("[overlay] load story requested %d-%d", world + 1, course + 1);
}

void drain_load_story() {
    const int req = g_load_story.exchange(-1, std::memory_order_acq_rel);
    if (req < 0) return;
    std::string err;
    const int world = req >> 8;
    const int course = req & 0xff;
    if (!scene_load_story_transition(world, course, err)) {
        int expected = -1;
        g_load_story.compare_exchange_strong(expected, req, std::memory_order_acq_rel);
        std::lock_guard<std::mutex> lk(g_load_story_mu);
        if (g_load_story_status != err) {
            g_load_story_status = err;
            LOG("[overlay] load story %d-%d: %s", world + 1, course + 1, err.c_str());
        }
        return;
    }
    std::lock_guard<std::mutex> lk(g_load_story_mu);
    g_load_story_status.clear();
    LOG("[overlay] load story %d-%d", world + 1, course + 1);
}

std::string load_story_status() {
    std::lock_guard<std::mutex> lk(g_load_story_mu);
    return g_load_story_status;
}

void set_cpu(Cpu* c) {
    t_guest_cpu = c;
    if (c) drain_load_story();
}

Cpu* cpu() {
    return t_guest_cpu;
}

bool load_story_course(int32_t world, int32_t course, std::string& err) {
    return scene_load_story_transition(world, course, err);
}

bool open_course_select(std::string& err) {
    return scene_transition(kCreatorCourseSelect, 0x000A000Au, err);
}

}
