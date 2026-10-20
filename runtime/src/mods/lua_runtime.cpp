#include "lua_runtime.h"
#include "lua_press.h"
#include "sdk/nsmbu.h"
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <set>
#include <stdexcept>

extern "C" {
#include "lua.h"
#include "lauxlib.h"
#include "lualib.h"
}

namespace mods::lua_mod {
namespace {
namespace fs = std::filesystem;

Instance* self(lua_State* L) {
    return static_cast<Instance*>(lua_touserdata(L, lua_upvalueindex(1)));
}

void set_fault(Instance& inst, const char* msg) {
    inst.fault = true;
    inst.fault_msg = msg ? msg : "lua error";
    fprintf(stderr, "[mod:%s] %s\n", inst.id.c_str(), inst.fault_msg.c_str());
}

void nil_global(lua_State* L, const char* name) {
    lua_pushnil(L);
    lua_setglobal(L, name);
}

void open_lib(lua_State* L, lua_CFunction fn, const char* name) {
    lua_pushcfunction(L, fn);
    lua_pushstring(L, name);
    lua_call(L, 1, 0);
}

bool relative_ok(const std::string& name) {
    if (name.empty() || name.size() > 512 || name.find('\\') != std::string::npos ||
        name.find(':') != std::string::npos || name.find('\0') != std::string::npos)
        return false;
    fs::path p(name);
    if (p.is_absolute()) return false;
    for (const auto& part : p)
        if (part == ".." || part == "." || part.empty()) return false;
    return true;
}

bool resolve_under(const fs::path& root, std::string rel, fs::path& out, std::string& err) {
    if (!relative_ok(rel)) {
        err = "path not allowed";
        return false;
    }
    auto root_n = root.lexically_normal();
    out = (root_n / rel).lexically_normal();
    auto check = out.lexically_relative(root_n);
    if (check.empty()) {
        err = "path not allowed";
        return false;
    }
    for (const auto& part : check)
        if (part == "..") {
            err = "path not allowed";
            return false;
        }
    return true;
}

std::string module_to_rel(std::string name) {
    for (char& c : name)
        if (c == '.') c = '/';
    if (name.size() < 4 || name.compare(name.size() - 4, 4, ".lua") != 0) name += ".lua";
    return name;
}

bool load_file(lua_State* L, const std::string& path, std::string& err) {
    std::ifstream file(path, std::ios::binary);
    if (!file) {
        err = "cannot open " + path;
        return false;
    }
    std::string source((std::istreambuf_iterator<char>(file)), std::istreambuf_iterator<char>());
    std::string chunk = "@" + path;
    int status = luaL_loadbuffer(L, source.data(), source.size(), chunk.c_str());
    if (status != 0) {
        err = lua_tostring(L, -1) ? lua_tostring(L, -1) : "load failed";
        lua_pop(L, 1);
        return false;
    }
    return true;
}

bool host_module_name(const char* name) {
    return std::strcmp(name, "nsmbu") == 0 || std::strncmp(name, "nsmbu.", 6) == 0;
}

bool call_fn(Instance& inst, int nargs) {
    lua_State* L = static_cast<lua_State*>(inst.state);
    if (lua_pcall(L, nargs, 0, 0) != 0) {
        set_fault(inst, lua_tostring(L, -1));
        lua_pop(L, 1);
        return false;
    }
    return true;
}

bool push_hook(lua_State* L, const char* name) {
    lua_getglobal(L, "nsmbu");
    if (!lua_istable(L, -1)) {
        lua_pop(L, 1);
        return false;
    }
    lua_getfield(L, -1, "Hooks");
    lua_remove(L, -2);
    if (!lua_istable(L, -1)) {
        lua_pop(L, 1);
        return false;
    }
    lua_getfield(L, -1, name);
    lua_remove(L, -2);
    if (!lua_isfunction(L, -1)) {
        lua_pop(L, 1);
        return false;
    }
    return true;
}

bool fire_hook(Instance& inst, const char* name, int nargs) {
    lua_State* L = static_cast<lua_State*>(inst.state);
    if (!push_hook(L, name)) {
        if (nargs) lua_pop(L, nargs);
        return true;
    }
    if (nargs) lua_insert(L, -1 - nargs);
    return call_fn(inst, nargs);
}

bool fire_logic(Instance& inst) {
    lua_State* L = static_cast<lua_State*>(inst.state);
    lua_pushnumber(L, (lua_Number)inst.step);
    return fire_hook(inst, "onLogicStep", 1);
}

bool fire_config(Instance& inst) { return fire_hook(inst, "onConfigChanged", 0); }

bool fire_loaded(Instance& inst) { return fire_hook(inst, "onLoaded", 0); }

bool fire_unload(Instance& inst) { return fire_hook(inst, "onUnload", 0); }

int l_require(lua_State* L) {
    Instance* inst = self(L);
    const char* modname = luaL_checkstring(L, 1);
    lua_getglobal(L, "package");
    if (!lua_istable(L, -1)) luaL_error(L, "package table missing");
    lua_getfield(L, -1, "loaded");
    if (!lua_istable(L, -1)) luaL_error(L, "package.loaded missing");
    lua_getfield(L, -1, modname);
    if (!lua_isnil(L, -1)) {
        lua_remove(L, -2);
        lua_remove(L, -2);
        return 1;
    }
    lua_pop(L, 1);

    lua_getfield(L, -2, "preload");
    if (lua_istable(L, -1)) {
        lua_getfield(L, -1, modname);
        if (lua_isfunction(L, -1)) {
            lua_pushvalue(L, 1);
            if (lua_pcall(L, 1, 1, 0) != 0) lua_error(L);
            if (lua_isnil(L, -1)) {
                lua_pop(L, 1);
                lua_pushboolean(L, 1);
            }
            lua_pushvalue(L, -1);
            lua_setfield(L, -4, modname);
            lua_remove(L, -2);
            lua_remove(L, -2);
            lua_remove(L, -2);
            return 1;
        }
        lua_pop(L, 1);
    }
    lua_pop(L, 1);

    if (host_module_name(modname)) luaL_error(L, "module not found: %s", modname);

    std::string err;
    fs::path file;
    if (!resolve_under(fs::path(inst->path), module_to_rel(modname), file, err))
        luaL_error(L, "%s", err.c_str());
    if (!fs::is_regular_file(file)) luaL_error(L, "module not found: %s", modname);
    if (!load_file(L, file.string(), err)) luaL_error(L, "%s", err.c_str());
    lua_pushvalue(L, 1);
    if (lua_pcall(L, 1, 1, 0) != 0) lua_error(L);
    if (lua_isnil(L, -1)) {
        lua_pop(L, 1);
        lua_pushboolean(L, 1);
    }
    lua_pushvalue(L, -1);
    lua_setfield(L, -3, modname);
    lua_remove(L, -2);
    lua_remove(L, -2);
    return 1;
}

void sandbox(lua_State* L, Instance* inst) {
    open_lib(L, luaopen_base, "");
    open_lib(L, luaopen_string, LUA_STRLIBNAME);
    open_lib(L, luaopen_table, LUA_TABLIBNAME);
    open_lib(L, luaopen_math, LUA_MATHLIBNAME);
    open_lib(L, luaopen_bit, LUA_BITLIBNAME);

    nil_global(L, "dofile");
    nil_global(L, "loadfile");
    nil_global(L, "loadstring");
    nil_global(L, "io");
    nil_global(L, "os");
    nil_global(L, "debug");
    nil_global(L, "jit");
    nil_global(L, "ffi");

    lua_newtable(L);
    lua_newtable(L);
    lua_setfield(L, -2, "loaded");
    lua_newtable(L);
    lua_setfield(L, -2, "preload");
    lua_pushstring(L, "");
    lua_setfield(L, -2, "path");
    lua_pushstring(L, "");
    lua_setfield(L, -2, "cpath");
    lua_pushnil(L);
    lua_setfield(L, -2, "loadlib");
    lua_setglobal(L, "package");

    lua_pushlightuserdata(L, inst);
    lua_pushcclosure(L, l_require, 1);
    lua_setglobal(L, "require");
}

void close_state(Instance& inst) {
    if (!inst.state) return;
    lua_State* L = static_cast<lua_State*>(inst.state);
    lua_close(L);
    inst.state = nullptr;
}
}

Instance::Instance() = default;
Instance::~Instance() { close_state(*this); }
Instance::Instance(Instance&& o) noexcept
    : id(std::move(o.id)), version(std::move(o.version)), path(std::move(o.path)),
      entry(std::move(o.entry)), config(std::move(o.config)), status_line(std::move(o.status_line)),
      fault_msg(std::move(o.fault_msg)), fault(o.fault), step(o.step), state(o.state) {
    o.state = nullptr;
    lua_press::drop(&o);
}
Instance& Instance::operator=(Instance&& o) noexcept {
    if (this == &o) return *this;
    close_state(*this);
    id = std::move(o.id);
    version = std::move(o.version);
    path = std::move(o.path);
    entry = std::move(o.entry);
    config = std::move(o.config);
    status_line = std::move(o.status_line);
    fault_msg = std::move(o.fault_msg);
    fault = o.fault;
    step = o.step;
    state = o.state;
    o.state = nullptr;
    lua_press::drop(&o);
    return *this;
}

std::unique_ptr<Instance> load(const LoadInfo& info) {
    if (info.id.empty()) throw std::runtime_error("mod id is empty");
    if (info.path.empty()) throw std::runtime_error("mod path is empty");
    if (info.entry.empty()) throw std::runtime_error("mod entry is empty");

    auto inst = std::make_unique<Instance>();
    inst->id = info.id;
    inst->version = info.version;
    inst->path = info.path;
    inst->entry = info.entry;
    inst->config = info.config;

    lua_State* L = luaL_newstate();
    if (!L) throw std::runtime_error("luaL_newstate failed");
    inst->state = L;

    sandbox(L, inst.get());
    lua_sdk::install(L, inst.get());

    std::string err;
    fs::path entry_path;
    if (!resolve_under(fs::path(inst->path), info.entry, entry_path, err))
        throw std::runtime_error(err);
    if (!fs::is_regular_file(entry_path))
        throw std::runtime_error("entry script not found: " + info.entry);
    if (!load_file(L, entry_path.string(), err)) throw std::runtime_error(err);
    if (lua_pcall(L, 0, 0, 0) != 0) {
        std::string msg = lua_tostring(L, -1) ? lua_tostring(L, -1) : "entry failed";
        throw std::runtime_error(msg);
    }
    fire_loaded(*inst);
    return inst;
}

void unload(Instance& inst) {
    if (!inst.state) return;
    fire_unload(inst);
    lua_press::drop(&inst);
    close_state(inst);
}

void set_config(Instance& inst, const json::Value& config) { inst.config = config; }

void config_changed(Instance& inst) {
    if (!inst.state || inst.fault) return;
    fire_config(inst);
}

void logic_step(Instance& inst, uint64_t step) {
    if (!inst.state || inst.fault) return;
    inst.step = step;
    fire_logic(inst);
}

void scene_change(Instance& inst, uint32_t factory) {
    if (!inst.state || inst.fault) return;
    lua_State* L = static_cast<lua_State*>(inst.state);
    lua_pushnumber(L, (lua_Number)factory);
    fire_hook(inst, "onSceneChange", 1);
}

std::string status(const Instance& inst) { return inst.status_line; }
bool faulted(const Instance& inst) { return inst.fault; }
std::string fault_message(const Instance& inst) { return inst.fault_msg; }

}
