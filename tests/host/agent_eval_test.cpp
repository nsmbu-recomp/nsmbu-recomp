#include "agent/agent_eval.h"

#include <cstdint>
#include <cstdio>
#include <string>

static int fails = 0;

static void check(bool cond, const char* what) {
    if (cond) return;
    std::fprintf(stderr, "FAIL %s\n", what);
    ++fails;
}

static int32_t g_n = 7;
static uint32_t g_pair[2] = {1, 2};
static char g_text[] = "hi";

static const void* n_addr() { return &g_n; }
static const void* pair_addr() { return g_pair; }
static const void* text_addr() { return g_text; }
static const void* null_addr() { return nullptr; }

static mods::json::Value echo_n(const mods::json::Value& args) { return args.get("n"); }

static mods::json::Value empty_args() {
    mods::json::Value args;
    args.type = mods::json::Value::Object;
    return args;
}

int main() {
    check(agent_eval::add_call({"sample.call", "echo n", true, echo_n}), "register call");
    check(!agent_eval::add_call({"sample.call", "again", true, echo_n}), "duplicate call");
    check(!agent_eval::add_call({"ping", "again", true, echo_n}), "duplicate ping");
    check(agent_eval::add_read({"sample.n", "scalar", agent_eval::Type::S32, 1, n_addr}), "register scalar");
    check(agent_eval::add_read({"sample.pair", "array", agent_eval::Type::U32, 2, pair_addr}), "register array");
    check(agent_eval::add_read({"sample.text", "string", agent_eval::Type::CStr, 8, text_addr}), "register string");
    check(agent_eval::add_read({"sample.missing", "null", agent_eval::Type::U8, 1, null_addr}), "register null");
    check(!agent_eval::add_read({"sample.n", "dup", agent_eval::Type::S32, 1, n_addr}), "duplicate read");
    check(!agent_eval::add_read({"sample.zero", "zero", agent_eval::Type::U8, 0, n_addr}), "reject zero count");
    check(!agent_eval::add_read({"sample.big", "big", agent_eval::Type::U8, 4097, n_addr}), "reject huge read");

    mods::json::Value args = empty_args();
    args.object["n"] = 4;
    std::string err;
    check(agent_eval::invoke("sample.call", args, err) == "4", "call value");
    check(err.empty(), "call error stays empty");
    check(agent_eval::invoke("sample.n", empty_args(), err) == "7", "scalar");
    check(agent_eval::invoke("sample.pair", empty_args(), err) == "[1,2]", "array");
    check(agent_eval::invoke("sample.text", empty_args(), err) == "\"hi\"", "string");
    check(agent_eval::invoke("sample.missing", empty_args(), err).empty(), "null address returns empty");
    check(err == "null address", "null address");
    check(agent_eval::invoke("no.such", empty_args(), err).empty(), "unknown eval returns empty");
    check(err == "unknown eval", "unknown eval");
    check(agent_eval::invoke("ping", args, err) == "{\"n\":4}", "ping");

    const std::string list = agent_eval::list_json();
    check(list.find("\"kind\":\"call\"") != std::string::npos, "list has a call");
    check(list.find("\"kind\":\"read\"") != std::string::npos, "list has a read");
    check(list.find("\"name\":\"sample.n\"") != std::string::npos, "list names the scalar");
    check(list.find("\"type\":\"s32\"") != std::string::npos, "list scalar type");
    check(list.find("\"type\":\"u32\"") != std::string::npos, "list array type");
    check(list.find("\"type\":\"cstr\"") != std::string::npos, "list string type");
    check(list.find("\"count\":2") != std::string::npos, "list array count");

    bool observe = false;
    check(agent_eval::lookup("sample.n", observe) && observe, "a read is observable");
    check(agent_eval::lookup("ping", observe) && observe, "ping is observable");
    check(!agent_eval::lookup("no.such", observe), "lookup misses an unknown name");

    if (fails) {
        std::fprintf(stderr, "%d failures\n", fails);
        return 1;
    }
    return 0;
}
