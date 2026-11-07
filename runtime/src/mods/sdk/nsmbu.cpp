#include "nsmbu.h"
#include "mods/guest_mem.h"
#include "mods/pad_inject.h"
#include "mods/packages.h"
#include "guest_addr.h"
#include "input/input.h"
#include "core/runtime.h"
#include <algorithm>
#include <atomic>
#include <cmath>
#include <cstdio>
#include <mutex>
#include <string>

extern "C" void nsmbu_fill_course_banner_digits(void);
extern "C" void nsmbu_banner_entry_arm(void);
extern "C" void nsmbu_mark_course_left(void);
extern "C" void nsmbu_directed_open(uint8_t world, uint8_t course);
extern "C" void nsmbu_map_pose_tick(void);

namespace mods::scene {
std::atomic<bool> g_story_leave_queued{false};
std::atomic<uint64_t> g_story_load_generation{0};
namespace {

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

thread_local Cpu* t_guest_cpu = nullptr;
bool g_actor_shape_warned = false;

bool guest_ok(uint32_t addr, size_t size, std::string& err) {
    return guest::check(addr, size, err);
}

bool read_ptr(uint32_t addr, uint32_t& out, std::string& err) {
    uint32_t p = 0;
    if (!guest::read_u32(addr, p, err)) return false;
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
    if (!guest::read_u32(mgr + kPlayerObjects + (uint32_t)index * 4u, actor, err)) return false;
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
    if (!guest::read_u8(slot + kEntryOff, entry, err)) return false;
    out = entry != 0;
    return true;
}

bool read_vec3(uint32_t base, uint32_t ox, uint32_t oy, uint32_t oz, float& x, float& y, float& z,
               std::string& err) {
    if (!guest::read_f32(base + ox, x, err)) return false;
    if (!guest::read_f32(base + oy, y, err)) return false;
    if (!guest::read_f32(base + oz, z, err)) return false;
    return true;
}

bool actor_mgr(uint32_t& out, std::string& err) {
    return read_ptr(kActorMgrSingleton, out, err);
}

bool live_actor_range(uint32_t& begin, uint32_t& end, std::string& err) {
    uint32_t mgr = 0;
    if (!actor_mgr(mgr, err)) return false;
    uint32_t cache = mgr + kActorMgrPtrCache;
    uint32_t array = 0, size = 0, live = 0;
    if (!guest::read_u32(cache + kCacheArray, array, err)) return false;
    if (!guest::read_u32(cache + kCacheArraySize, size, err)) return false;
    if (!guest::read_u32(cache + kCacheLiveEnd, live, err)) return false;
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

bool actor_profile_id(uint32_t actor, uint32_t& out, std::string& err) {
    uint32_t profile = 0;
    if (!guest::read_u32(actor + kActorProfile, profile, err)) return false;
    if (profile == 0 || !guest_ok(profile, 0x14, err)) {
        err = "actor gone";
        return false;
    }
    if (!guest::read_u32(profile + kActorId, out, err)) return false;
    return true;
}

bool actor_is_actor(uint32_t actor, bool& out, std::string& err) {
    if (!t_guest_cpu) {
        err = "no guest cpu";
        return false;
    }
    uint32_t vt = 0;
    if (!guest::read_u32(actor + kActorVptr, vt, err)) return false;
    if (vt == 0 || !guest_ok(vt, kDerivedCheckSlot + 4, err)) {
        out = false;
        return true;
    }
    uint32_t fn = 0;
    if (!guest::read_u32(vt + kDerivedCheckSlot, fn, err)) return false;
    if (fn == 0 || !guest_code_valid(fn)) {
        out = false;
        return true;
    }
    out = guest_call(t_guest_cpu, fn, {actor, kTypeInfoActor}) != 0;
    return true;
}

bool scene_object(uint32_t& out, std::string& err) {
    uint32_t scene = 0;
    if (!guest::read_u32(kSceneSingleton, scene, err)) return false;
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
    if (!guest::read_u32(scene + kSceneNowCreatorOff, creator, err)) return false;
    if (creator == 0) {
        err = "scene not ready";
        return false;
    }
    out = creator;
    return true;
}

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
    if (!guest::read_u32(task + kTaskVtblOff, vt, err)) return false;
    if (vt == kFlowVtbl) {
        match = true;
        return true;
    }
    uint32_t factory = 0;
    if (!guest::read_u32(task + kTaskClassFactoryOff, factory, err)) return false;
    match = factory == kCreatorFlow;
    return true;
}

bool task_is_menu(uint32_t task, bool& match, std::string& err) {
    match = false;
    uint32_t vt = 0;
    if (!guest::read_u32(task + kTaskVtblOff, vt, err)) return false;
    if (vt == kMenuVtbl) {
        match = true;
        return true;
    }
    uint32_t factory = 0;
    if (!guest::read_u32(task + kTaskClassFactoryOff, factory, err)) return false;
    match = factory == kCreatorMenu;
    return true;
}

bool find_task_in_tree(uint32_t start, bool (*is_match)(uint32_t, bool&, std::string&), uint32_t& out,
                      std::string& err) {
    if (start == 0) return false;
    uint32_t root = start;
    for (int i = 0; i < 16; i++) {
        uint32_t parent = 0;
        if (!guest::read_u32(root + kTaskParentOff, parent, err)) return false;
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
        if (!guest::read_u32(task + kTaskChildOff, child, err)) return false;
        if (!guest::read_u32(task + kTaskSiblingOff, sibling, err)) return false;
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
    if (!guest::read_u32(sentinel + kTaskListNextOff, node, err)) return false;
    for (int i = 0; i < 64; i++) {
        if (node == 0 || node == sentinel) return false;
        uint32_t task = 0;
        if (!guest::read_u32(node + kTaskListDataOff, task, err)) return false;
        if (task != 0) {
            bool match = false;
            if (!is_match(task, match, err)) return false;
            if (match) {
                out = task;
                return true;
            }
            if (task == prefer) saw_prefer = true;
        }
        if (!guest::read_u32(node + kTaskListNextOff, node, err)) return false;
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
    if (!guest::read_u32(scene + kSceneTaskMgrOff, mgr, err)) return false;
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
        if (!guest::read_u32(mgr + kTaskMgrRootOff, root, err)) return false;
        if (find_flow_in_tree(root, out, err)) return true;
        if (!err.empty()) return false;
    }
    uint32_t area = 0;
    if (!guest::read_u32(kAreaTaskInstance, area, err)) return false;
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
    if (!guest::read_u32(scene + kSceneTaskMgrOff, mgr, err)) return false;
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
        if (!guest::read_u32(mgr + kTaskMgrRootOff, root, err)) return false;
        if (find_menu_in_tree(root, out, err)) return true;
        if (!err.empty()) return false;
    }
    uint32_t area = 0;
    if (!guest::read_u32(kAreaTaskInstance, area, err)) return false;
    if (find_menu_in_tree(area, out, err)) return true;
    if (!err.empty()) return false;
    err = "menu missing";
    return false;
}

bool menu_state_ready(uint32_t menu, std::string& err) {
    uint32_t factory = 0;
    if (!guest::read_u32(menu + kAppStateMethodOff, factory, err)) return false;
    if (factory == 0) {
        err = "state not ready";
        return false;
    }
    uint32_t factory_vt = 0;
    if (!guest::read_u32(factory, factory_vt, err)) return false;
    if (factory_vt == 0) {
        err = "state not ready";
        return false;
    }
    uint32_t state = 0;
    if (!guest::read_u32(menu + kAppStateMethodOff + 8, state, err)) return false;
    if (state != 0) {
        uint32_t state_vt = 0;
        if (!guest::read_u32(state, state_vt, err)) return false;
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
    if (!guest::read_u32(scene + kSceneNextTypeOff, next_type, err)) return false;
    if (next_type != 0) {
        char buf[64];
        snprintf(buf, sizeof buf, "scene busy next=%08X", next_type);
        err = buf;
        return false;
    }
    uint32_t fader = 0;
    if (!guest::read_u32(scene + kSceneFaderOff, fader, err)) return false;
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

bool continue_source(uint32_t& out, std::string& err) {
    uint32_t course = 0;
    if (!guest::read_u32(kCourseTaskInstance, course, err)) return false;
    if (course != 0) {
        uint32_t head = 0;
        if (!guest::read_u32(course, head, err)) return false;
        if (head != 0) {
            uint32_t src = 0;
            if (!guest::read_u32(head + kCourseTaskSrcOff, src, err)) return false;
            if (src != 0) {
                out = src;
                return true;
            }
        }
    }
    return scene_app_object(out, err);
}

bool apply_save_file(uint32_t file, int32_t world, int32_t course, bool entry, std::string& err) {
    if (!scene_change_open(err)) return false;
    uint32_t mgr = 0;
    if (!guest::read_u32(kSaveMgr, mgr, err)) return false;
    if (mgr == 0) {
        err = "file not ready";
        return false;
    }
    uint32_t savedata = 0;
    if (!guest::read_u32(mgr + kSaveDataOff, savedata, err)) return false;
    if (savedata == 0) {
        err = "file not ready";
        return false;
    }
    if (!guest::write_u8(savedata + kLastFileOff, (uint8_t)file, err)) return false;
    uint32_t field = 0;
    if (!guest::read_u32(kFieldGameSingleton, field, err)) return false;
    if (field == 0) {
        err = "file not ready";
        return false;
    }
    if (entry) {
        uint32_t info = 0;
        if (!guest::read_u32(kCourseInfo, info, err)) return false;
        if (info == 0) {
            err = "file not ready";
            return false;
        }
        if (!guest::write_u8(info + kCourseInfoNextWorldOff, (uint8_t)world, err)) return false;
        if (!guest::write_u8(info + kCourseInfoNextCourseOff, (uint8_t)course, err)) return false;
        ::nsmbu_banner_entry_arm();
    }
    uint32_t gate = 0;
    if (!guest::read_u32(kSaveApplyFlag, gate, err)) return false;
    if (gate != 0 && !guest::write_u8(gate + kSaveApplyFlagOff, 0, err)) return false;
    if (!dispatch::lookup(kApplySaveFile)) {
        err = "applySaveFile is not in this build; install the matching 1.3.0 update";
        return false;
    }
    guest_call(t_guest_cpu, kApplySaveFile, {mgr, file});
    uint32_t game = 0;
    if (!guest::read_u32(kMainGame, game, err)) return false;
    if (game == 0) {
        err = "file not ready";
        return false;
    }
    if (!guest::write_u8(game + kSinglePlayerDrc, input::pro_controller() ? 0 : 1, err)) return false;
    return true;
}

bool leave_course_impl(std::string& err) {
    if (!t_guest_cpu) {
        err = "no guest cpu";
        return false;
    }
    uint32_t area = 0;
    if (!guest::read_u32(kAreaTaskInstance, area, err)) return false;
    if (area == 0) {
        err = "no course";
        return false;
    }
    guest_call(t_guest_cpu, kRequestCourseOut, {area});
    nsmbu_mark_course_left();
    return true;
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
    if (!guest::read_u32(task + kTaskClassFactoryOff, factory, err)) return false;
    match = factory != 0 && factory == g_match_factory;
    return true;
}

bool singleton_task(uint32_t addr, uint32_t& out, std::string& err) {
    uint32_t task = 0;
    if (!guest::read_u32(addr, task, err)) return false;
    if (task == 0) return false;
    uint32_t vt = 0;
    if (!guest::read_u32(task + kTaskVtblOff, vt, err)) return false;
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
    if (!guest::read_u32(scene + kSceneNowCreatorOff, creator, err)) return false;
    if (creator == kCreatorCourseSelect && singleton_task(kCourseSelectInstance, out, err)) return true;
    if (!err.empty()) return false;
    if (creator == kCreatorCourseTask && singleton_task(kCourseTaskInstance, out, err)) return true;
    if (!err.empty()) return false;
    if (creator != 0) {
        g_match_factory = creator;
        uint32_t mgr = 0;
        if (!guest::read_u32(scene + kSceneTaskMgrOff, mgr, err)) return false;
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
            if (!guest::read_u32(mgr + kTaskMgrRootOff, root, err)) return false;
            if (find_task_in_tree(root, task_factory_match, out, err)) return true;
            if (!err.empty()) return false;
        }
        uint32_t area = 0;
        if (!guest::read_u32(kAreaTaskInstance, area, err)) return false;
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
    guest::read_u32(kCourseTaskInstance, live_course, live_err);
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
    if (world >= 0 && world <= 8 && course >= 0 && course <= 255) nsmbu_directed_open((uint8_t)world, (uint8_t)course);
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
    const bool in_course = guest::read_u32(kCourseTaskInstance, task, task_err) && task != 0;
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
                pad_inject::press(&g_native_skip_owner, input::kA, 3, step);
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
    nsmbu_map_pose_tick();
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
        if (!guest::read_u8(ea + i, c, err)) return false;
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
    if (guest::read_u32(kAreaTaskInstance, area, err)) {
        out.area_known = true;
        out.area = area != 0;
    }
    if (!out.map || !out.world_known || !out.node_known) return;
    uint32_t cs = 0;
    err.clear();
    if (guest::read_u32(kCourseSelectInstance, cs, err) && cs != 0) {
        uint8_t lock = 0;
        err.clear();
        if (guest::read_u8(cs + 0xDC9u, lock, err)) {
            out.map_lock_known = true;
            out.map_lock = lock;
        }
    }
    uint32_t game = 0;
    err.clear();
    if (!guest::read_u32(kMainGame, game, err) || game == 0) return;
    int32_t section = 0;
    err.clear();
    if (!guest::read_s32(game + 0x1A0u, section, err)) return;
    if (out.world < 0 || section < 0 || section > 1 || out.node < 0 || out.node > 255) return;
    uint32_t mgr = 0;
    err.clear();
    if (!guest::read_u32(kCourseSelectMgr, mgr, err) || mgr == 0) return;
    uint32_t path = 0;
    err.clear();
    const uint32_t path_off = 0x60u + (uint32_t)(out.world * 2 + section) * 4u;
    if (!guest::read_u32(mgr + path_off, path, err) || path == 0) return;
    const uint32_t base = path + 0x10u + (uint32_t)out.node * kPathNodeSize;
    uint32_t name_ptr = 0;
    err.clear();
    if (!guest::read_u32(base, name_ptr, err) || name_ptr == 0) return;
    std::string name;
    if (!read_ident(name_ptr, name)) return;
    float nx = 0;
    float ny = 0;
    err.clear();
    if (!guest::read_f32(base + 0x44u, nx, err)) return;
    err.clear();
    if (!guest::read_f32(base + 0x48u, ny, err)) return;
    int32_t title = -1;
    err.clear();
    guest::read_s32(base + 0x50u, title, err);
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
        if (!guest::read_u32(slot, actor, err) || actor == 0) continue;
        uint32_t id = 0;
        if (!actor_profile_id(actor, id, err) || id != kMapPlayerProfile) continue;
        players++;
        if (got) continue;
        float x = 0;
        float y = 0;
        err.clear();
        if (!guest::read_f32(actor + 0x50u, x, err)) continue;
        err.clear();
        if (!guest::read_f32(actor + 0x54u, y, err)) continue;
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
    if (guest::read_u32(kCourseSelectInstance, map, err)) {
        out.map_known = true;
        out.map = map != 0;
    }
    uint32_t course = 0;
    err.clear();
    if (guest::read_u32(kCourseTaskInstance, course, err)) {
        out.course_known = true;
        out.course = course != 0;
        if (course != 0) {
            uint8_t a = 0, b = 0, c = 0;
            err.clear();
            if (guest::read_u8(course + 0x134u, a, err) && guest::read_u8(course + 0x135u, b, err) &&
                guest::read_u8(course + 0x136u, c, err)) {
                out.banner_known = true;
                out.banner[0] = a;
                out.banner[1] = b;
                out.banner[2] = c;
            }
        }
    }

    uint32_t info = 0;
    err.clear();
    if (guest::read_u32(kCourseInfo, info, err) && info != 0) {
        uint8_t now_w = 0, now_c = 0, next_w = 0, next_c = 0;
        err.clear();
        const bool now_ok = guest::read_u8(info + 0x10u, now_w, err) && guest::read_u8(info + 0x11u, now_c, err);
        err.clear();
        const bool next_ok = guest::read_u8(info + kCourseInfoNextWorldOff, next_w, err) &&
                             guest::read_u8(info + kCourseInfoNextCourseOff, next_c, err);
        const bool now_pack = now_ok && pack_bytes(now_w, now_c);
        const bool next_pack = next_ok && pack_bytes(next_w, next_c);
        if (now_pack) format_pack(now_w, now_c, out.pack);
        if (now_pack && next_pack && (next_w != now_w || next_c != now_c))
            format_pack(next_w, next_c, out.entering);
    }

    uint32_t mgr = 0;
    err.clear();
    if (guest::read_u32(kSubjectMgr, mgr, err) && mgr != 0) {
        uint8_t mode = 0;
        uint8_t file = 0;
        err.clear();
        if (guest::read_u8(mgr + kSubjectModeOff, mode, err) && mode == kSubjectModeActive &&
            guest::read_u8(mgr + kSubjectFileOff, file, err)) {
            out.file_known = true;
            out.file = file;
        }
    }

    uint32_t field = 0;
    err.clear();
    if (guest::read_u32(kFieldGameSingleton, field, err) && field != 0) {
        int32_t cp = 0;
        err.clear();
        if (guest::read_s32(field + 0x10u, cp, err)) {
            out.cp_known = true;
            out.cp = cp;
        }
    }

    if (out.map) {
        uint32_t game = 0;
        err.clear();
        if (guest::read_u32(kMainGame, game, err) && game != 0) {
            int32_t world = 0;
            int32_t node = 0;
            err.clear();
            if (guest::read_s32(game + 0x19Cu, world, err)) {
                out.world_known = true;
                out.world = world;
            }
            err.clear();
            if (guest::read_s32(game + 0x1A4u, node, err)) {
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

bool open_title(std::string& err) {
    return scene_transition(kCreatorTitle, 0, err);
}

bool open_file_select(std::string& err) {
    if (!t_guest_cpu) {
        err = "no guest cpu";
        return false;
    }
    if (!scene_change_open(err)) return false;
    uint32_t menu = 0;
    if (!scene_menu_object(menu, err)) return false;
    if (!menu_state_ready(menu, err)) return false;
    uint32_t mgr = 0;
    if (!guest::read_u32(kSubjectMgr, mgr, err)) return false;
    if (mgr == 0) {
        err = "file not ready";
        return false;
    }
    if (guest_call(t_guest_cpu, kSubjectSelectFile, {mgr, 0}) == 0) {
        err = "file not ready";
        return false;
    }
    guest_call(t_guest_cpu, kAppEnterFileState, {menu});
    return true;
}

bool open_first_save(uint32_t file, std::string& err) {
    if (!t_guest_cpu) {
        err = "no guest cpu";
        return false;
    }
    if (!apply_save_file(file, 0, 0, false, err)) return false;
    uint32_t src = 0;
    if (!continue_source(src, err)) return false;
    guest_call(t_guest_cpu, kOpenFirstSave, {src});
    return true;
}

bool open_place(const std::string& place, std::string& err) {
    if (place == "title") return open_title(err);
    if (place == "file" || place == "fileselect") return open_file_select(err);
    if (place == "map" || place == "courseselect") return open_first_save(0, err);
    int32_t world = 0;
    int32_t course = 0;
    if (!parse_course_pack(place.c_str(), place.size(), world, course)) {
        err = "unknown place";
        return false;
    }
    return scene_load_story_transition(world, course, err);
}

const char* player_mode_name(int mode) {
    switch (mode) {
    case 0: return "small";
    case 1: return "normal";
    case 2: return "fire";
    case 3: return "mini";
    case 4: return "propeller";
    case 5: return "penguin";
    case 6: return "ice";
    case 7: return "squirrel";
    case 8: return "p-squirrel";
    default: return "";
    }
}

bool probe_actors(int limit, float radius, ActorProbe& out, std::string& err) {
    out = {};
    if (limit < 1) limit = 1;
    if (limit > 64) limit = 64;
    std::string local;
    bool entered = false;
    if (player_entered(0, entered, local) && entered) {
        uint32_t slot = 0;
        int32_t mode = 0;
        if (slot_addr(0, slot, local) && guest::read_s32(slot + kModeOff, mode, local)) {
            out.power_known = true;
            out.power = mode;
            const char* name = player_mode_name(mode);
            if (name[0]) std::snprintf(out.power_name, sizeof out.power_name, "%s", name);
        }
    }
    float px = 0, py = 0, pz = 0;
    bool have = false;
    uint32_t player = 0;
    if (entered && actor_addr(0, player, local) && read_vec3(player, kPosX, kPosY, kPosZ, px, py, pz, local))
        have = true;
    uint32_t begin = 0, end = 0;
    if (!live_actor_range(begin, end, err)) {
        if (!out.power_known) return false;
        err.clear();
        return true;
    }
    struct Cand {
        ActorRow row;
        float dist;
    };
    Cand cand[520];
    int n = 0;
    for (uint32_t slot = begin; slot < end && n < 520; slot += kActorIdxToPtr) {
        uint32_t actor = 0;
        local.clear();
        if (!guest::read_u32(slot, actor, local) || actor == 0 || actor == player) continue;
        if (!guest_ok(actor, kActorVptr + 4, local)) continue;
        bool is_actor_value = false;
        if (!actor_is_actor(actor, is_actor_value, local) || !is_actor_value) continue;
        uint32_t profile = 0;
        float x = 0, y = 0, z = 0, vx = 0, vy = 0, vz = 0;
        if (!actor_profile_id(actor, profile, local)) continue;
        if (!read_vec3(actor, kPosX, kPosY, kPosZ, x, y, z, local)) continue;
        local.clear();
        if (!read_vec3(actor, kSpeedX, kSpeedY, kSpeedZ, vx, vy, vz, local)) {
            vx = vy = vz = 0;
        }
        float dist = 0;
        if (have) {
            const float dx = x - px, dy = y - py, dz = z - pz;
            dist = std::sqrt(dx * dx + dy * dy + dz * dz);
            if (radius > 0.f && dist > radius) continue;
        }
        cand[n].row.profile = profile;
        cand[n].row.x = x;
        cand[n].row.y = y;
        cand[n].row.z = z;
        cand[n].row.vx = vx;
        cand[n].row.vy = vy;
        cand[n].row.vz = vz;
        cand[n].dist = dist;
        n++;
    }
    if (have) {
        std::sort(cand, cand + n, [](const Cand& a, const Cand& b) { return a.dist < b.dist; });
    }
    if (n > limit) n = limit;
    out.count = n;
    for (int i = 0; i < n; i++) out.rows[i] = cand[i].row;
    err.clear();
    return true;
}

}
