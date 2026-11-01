#pragma once
#include "mod_json.h"
#include <cstdint>
#include <string>
#include <vector>
namespace mods::packages {
inline constexpr const char* kGameId="nsmbu-usa";
inline constexpr const char* kManagerVersion="1.0.0";
struct Option {
    std::string id,name,description,type;
    json::Value value,default_value;
    double minimum=0,maximum=1,step=1;
    std::vector<std::string> choices, labels;
};
struct View {
    std::string id,name,version,author,description,kind,reason,status;
    bool enabled=false,active=false,compatible=false,restart_required=false,pending_restart=false;
    std::vector<Option> options;
    std::vector<std::string> dependencies,conflicts;
};

void initialize();
void reinitialize();
void set_open_pack(std::string pack);
std::string open_pack_value();
std::string directory();
std::vector<View> list();
bool install(const std::string& source,std::string& error,std::string* installed_id=nullptr);
bool remove(const std::string& id,std::string& error);
bool enable(const std::string& id,bool on,std::string& error);

bool configure(const std::string& id,const std::string& option,const json::Value& value,std::string& error);
void disable_all();

void frame(uint64_t step);
void scene_change(uint32_t factory);
bool refresh(std::string& error);
bool hot_reload();
bool set_hot_reload(bool on,std::string& error);
}
