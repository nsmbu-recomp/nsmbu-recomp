#include "lua_gui.h"

#include <map>
#include <mutex>

namespace mods::lua_gui {
namespace {

std::mutex g_mutex;
std::map<std::string, std::vector<Command>> g_owners;

}

void clear(const std::string& owner) {
    std::lock_guard guard(g_mutex);
    auto it = g_owners.find(owner);
    if (it != g_owners.end()) it->second.clear();
}

void push(const std::string& owner, const Command& command) {
    std::lock_guard guard(g_mutex);
    g_owners[owner].push_back(command);
}

void drop(const std::string& owner) {
    std::lock_guard guard(g_mutex);
    g_owners.erase(owner);
}

bool active() {
    std::lock_guard guard(g_mutex);
    for (const auto& entry : g_owners) {
        if (!entry.second.empty()) return true;
    }
    return false;
}

std::vector<Command> commands() {
    std::lock_guard guard(g_mutex);
    std::vector<Command> out;
    for (const auto& entry : g_owners) {
        out.insert(out.end(), entry.second.begin(), entry.second.end());
    }
    return out;
}

}
