#pragma once
#include "mod_json.h"
#include <cstdint>
#include <memory>
#include <string>

namespace mods::lua_mod {

struct Config {
    json::Value options;
};

struct LoadInfo {
    std::string id, version, path;
    std::string entry;
    json::Value config;
};

struct Instance {
    std::string id, version, path, entry;
    json::Value config;
    std::string status_line;
    std::string fault_msg;
    bool fault = false;
    uint64_t step = 0;
    void* state = nullptr;

    Instance();
    ~Instance();
    Instance(Instance&&) noexcept;
    Instance& operator=(Instance&&) noexcept;
    Instance(const Instance&) = delete;
    Instance& operator=(const Instance&) = delete;
};

std::unique_ptr<Instance> load(const LoadInfo& info);
void unload(Instance&);
void set_config(Instance&, const json::Value& config);
void config_changed(Instance&);
void logic_step(Instance&, uint64_t step);
void scene_change(Instance&, uint32_t factory);
std::string status(const Instance&);
bool faulted(const Instance&);
std::string fault_message(const Instance&);

}
