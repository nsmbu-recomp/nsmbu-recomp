#pragma once

#include <cstdint>
#include <string>

struct lua_State;
struct Cpu;

namespace mods::lua_mod {
struct Instance;
}

namespace mods::lua_sdk {

void install(lua_State* L, lua_mod::Instance* inst);
void forget(lua_mod::Instance* inst);
void set_cpu(Cpu* c);
Cpu* cpu();
void request_load_story(int world, int course);
std::string load_story_status();
bool story_leave_pending();
uint64_t story_load_generation();
void native_intro_skip_tick(uint64_t step);

struct SceneReport {
    std::string scene;
    bool map_known = false;
    bool map = false;
    bool course_known = false;
    bool course = false;
    std::string pack;
    std::string entering;
    bool file_known = false;
    int file = 0;
    bool cp_known = false;
    int32_t cp = 0;
    bool world_known = false;
    int32_t world = 0;
    bool node_known = false;
    int32_t node = 0;
    bool area_known = false;
    bool area = false;
    std::string node_name;
    bool node_pos_known = false;
    float node_x = 0;
    float node_y = 0;
    bool map_player_known = false;
    float map_x = 0;
    float map_y = 0;
    bool map_lock_known = false;
    int map_lock = 0;
    bool map_title_known = false;
    int32_t map_title = -1;
    bool map_players_known = false;
    int map_players = 0;
};

void scene_report(SceneReport& out);
bool leave_course(std::string& err);
bool load_story_course(int32_t world, int32_t course, std::string& err);
bool open_course_select(std::string& err);

}
