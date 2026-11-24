#pragma once

#include <string>

namespace mods::lua_eval {

struct Result {
    bool ok = true;
    std::string output;
};

Result run(const std::string& code, int timeout_ms);

}
