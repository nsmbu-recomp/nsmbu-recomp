#pragma once

#include "mods/mod_json.h"

#include <string>

namespace agent_eval {

enum class Type {
    Bool,
    U8,
    U16,
    U32,
    U64,
    S32,
    S64,
    F32,
    F64,
    Bytes,
    CStr,
};

struct Call {
    const char* name;
    const char* help;
    bool observe;
    mods::json::Value (*fn)(const mods::json::Value& args);
};

struct Read {
    const char* name;
    const char* help;
    Type type;
    int count;
    const void* (*addr)();
};

// Register from static initialization, before the agent thread starts.
// Returns false when the name is empty, the callback is missing, the copy exceeds 4096 bytes,
// the count is below 1, or the name is already registered.
bool add_call(const Call& call);
bool add_read(const Read& read);

// Reads are always observable. Returns false when the name is not registered.
bool lookup(const std::string& name, bool& observe);

std::string list_json();

// On failure returns an empty string and sets err.
std::string invoke(const std::string& name, const mods::json::Value& args, std::string& err);

}
