#include "lua_eval.h"
#include "lua_runtime.h"
#include "mod_json.h"

#include <chrono>
#include <cmath>
#include <cstdio>
#include <exception>
#include <filesystem>
#include <mutex>
#include <string>
#include <utility>
#include <vector>

extern "C" {
#include "lua.h"
#include "lauxlib.h"
#include "lualib.h"
}

namespace mods::lua_eval {
namespace {

std::mutex g_mutex;
lua_mod::Instance* g_instance = nullptr;
std::string* g_output = nullptr;
std::chrono::steady_clock::time_point g_deadline;
bool g_timeout_enabled = false;
bool g_truncated = false;
constexpr size_t kOutputLimit = 1u << 20;

int absolute(lua_State* L, int idx) {
    return idx > 0 || idx <= LUA_REGISTRYINDEX ? idx : lua_gettop(L) + idx + 1;
}

bool identifier(const std::string& s) {
    if (s.empty()) return false;
    auto alpha = [](unsigned char c) {
        return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || c == '_';
    };
    if (!alpha(static_cast<unsigned char>(s[0]))) return false;
    for (unsigned char c : s)
        if (!alpha(c) && !(c >= '0' && c <= '9')) return false;
    return true;
}

struct Formatter {
    std::vector<const void*> seen;
};

void format_value(lua_State* L, int idx, Formatter& f, std::string& out, int depth, bool raw);

void format_table(lua_State* L, int raw_idx, Formatter& f, std::string& out, int depth) {
    const void* ptr = lua_topointer(L, raw_idx);
    for (const void* seen : f.seen) {
        if (seen == ptr) {
            out += "{...}";
            return;
        }
    }
    if (depth >= 4) {
        out += "{...}";
        return;
    }
    f.seen.push_back(ptr);
    const int idx = absolute(L, raw_idx);
    const size_t count = lua_objlen(L, idx);
    bool array = count > 0;
    for (size_t i = 1; array && i <= count; ++i) {
        lua_rawgeti(L, idx, int(i));
        if (lua_isnil(L, -1)) array = false;
        lua_pop(L, 1);
    }
    out += '{';
    bool first = true;
    if (array) {
        for (size_t i = 1; i <= count; ++i) {
            if (!first) out += ", ";
            first = false;
            lua_rawgeti(L, idx, int(i));
            format_value(L, -1, f, out, depth + 1, false);
            lua_pop(L, 1);
        }
    }
    lua_pushnil(L);
    while (lua_next(L, idx) != 0) {
        bool skip = false;
        if (lua_type(L, -2) == LUA_TNUMBER) {
            const double key = lua_tonumber(L, -2);
            if (key >= 1.0 && key <= double(count) && std::floor(key) == key) skip = true;
        }
        if (!skip) {
            if (!first) out += ", ";
            first = false;
            if (lua_type(L, -2) == LUA_TSTRING) {
                size_t len = 0;
                const char* key = lua_tolstring(L, -2, &len);
                const std::string name(key, len);
                if (identifier(name)) {
                    out += name;
                } else {
                    out += '[';
                    out += json::quote(name);
                    out += ']';
                }
            } else {
                out += '[';
                format_value(L, -2, f, out, depth + 1, true);
                out += ']';
            }
            out += " = ";
            format_value(L, -1, f, out, depth + 1, false);
        }
        lua_pop(L, 1);
    }
    out += '}';
    f.seen.pop_back();
}

void format_value(lua_State* L, int raw_idx, Formatter& f, std::string& out, int depth, bool raw) {
    const int idx = absolute(L, raw_idx);
    switch (lua_type(L, idx)) {
    case LUA_TNIL:
        out += "nil";
        return;
    case LUA_TBOOLEAN:
        out += lua_toboolean(L, idx) ? "true" : "false";
        return;
    case LUA_TNUMBER: {
        char buf[48];
        std::snprintf(buf, sizeof buf, "%.14g", double(lua_tonumber(L, idx)));
        out += buf;
        return;
    }
    case LUA_TSTRING: {
        size_t len = 0;
        const char* s = lua_tolstring(L, idx, &len);
        if (raw)
            out.append(s, len);
        else
            out += json::quote(std::string(s, len));
        return;
    }
    default:
        break;
    }
    if (luaL_getmetafield(L, idx, "__tostring")) {
        if (lua_isfunction(L, -1)) {
            lua_pushvalue(L, idx);
            if (lua_pcall(L, 1, 1, 0) == 0 && lua_isstring(L, -1)) {
                size_t len = 0;
                const char* s = lua_tolstring(L, -1, &len);
                out.append(s, len);
                lua_pop(L, 1);
                return;
            }
            lua_pop(L, 1);
        }
        lua_pop(L, 1);
    }
    if (lua_istable(L, idx)) {
        format_table(L, idx, f, out, depth);
        return;
    }
    char buf[64];
    std::snprintf(buf, sizeof buf, "<%s: %p>", lua_typename(L, lua_type(L, idx)), lua_topointer(L, idx));
    out += buf;
}

int l_print(lua_State* L) {
    if (!g_output || g_truncated) return 0;
    Formatter f;
    std::string line;
    const int args = lua_gettop(L);
    for (int i = 1; i <= args; ++i) {
        if (i > 1) line.push_back('\t');
        format_value(L, i, f, line, 0, true);
    }
    line.push_back('\n');
    if (g_output->size() + line.size() > kOutputLimit) {
        g_truncated = true;
        return 0;
    }
    g_output->append(line);
    return 0;
}

int l_traceback(lua_State* L) {
    const char* msg = lua_tostring(L, 1);
    if (!msg) msg = "(non-string error)";
    luaL_traceback(L, L, msg, 1);
    return 1;
}

void timeout_hook(lua_State* L, lua_Debug*) {
    if (g_timeout_enabled && std::chrono::steady_clock::now() >= g_deadline)
        luaL_error(L, "eval timed out");
}

lua_State* state() {
    if (!g_instance) {
        lua_mod::LoadInfo info;
        info.id = "eval";
        info.version = "0";
        std::error_code ec;
        const std::filesystem::path cwd = std::filesystem::current_path(ec);
        info.path = ec ? std::string(".") : cwd.string();
        g_instance = lua_mod::create(info).release();
        lua_State* L = static_cast<lua_State*>(g_instance->state);
        lua_pushcfunction(L, l_print);
        lua_setglobal(L, "print");
    }
    return static_cast<lua_State*>(g_instance->state);
}

}

Result run(const std::string& code, int timeout_ms) {
    std::lock_guard<std::mutex> lock(g_mutex);
    Result result;
    std::string output;
    lua_State* L = nullptr;
    try {
        L = state();
    } catch (const std::exception& e) {
        result.ok = false;
        result.output = e.what();
        return result;
    }

    const int base = lua_gettop(L);
    g_output = &output;
    g_truncated = false;
    lua_pushcfunction(L, l_traceback);
    const int errfunc = lua_gettop(L);
    int status = luaL_loadbuffer(L, code.data(), code.size(), "=eval");
    if (status == 0) {
        if (timeout_ms > 0) {
            g_deadline = std::chrono::steady_clock::now() + std::chrono::milliseconds(timeout_ms);
            g_timeout_enabled = true;
            lua_sethook(L, timeout_hook, LUA_MASKCOUNT, 100000);
        }
        status = lua_pcall(L, 0, LUA_MULTRET, errfunc);
        lua_sethook(L, nullptr, 0, 0);
        g_timeout_enabled = false;
    }

    if (g_truncated) output += "[print output truncated]\n";

    if (status != 0) {
        const char* msg = lua_tostring(L, -1);
        output += msg ? msg : "eval failed";
        if (output.empty() || output.back() != '\n') output += '\n';
        result.ok = false;
    } else {
        const int top = lua_gettop(L);
        Formatter f;
        bool first = true;
        for (int i = errfunc + 1; i <= top; ++i) {
            if (!first) output += '\t';
            first = false;
            format_value(L, i, f, output, 0, true);
        }
        if (!first) output += '\n';
    }

    lua_settop(L, base);
    g_output = nullptr;
    result.output = std::move(output);
    return result;
}

}
