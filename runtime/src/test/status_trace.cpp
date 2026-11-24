#include "test/status_trace.h"

#include <cstdio>
#include <cstring>
#include <string>

#include "core/env.h"
#include "core/runtime.h"
#include "hooks/interp.h"
#include "mods/sdk/nsmbu.h"
#include "test/script_frame.h"

namespace render { uint64_t frame_count(); }

namespace status_trace {
namespace {

bool same_name(const char* a, const char* b) {
    for (; *a && *b; a++, b++) {
        const char x = *a >= 'A' && *a <= 'Z' ? char(*a + 32) : *a;
        const char y = *b >= 'A' && *b <= 'Z' ? char(*b + 32) : *b;
        if (x != y) return false;
    }
    return !*a && !*b;
}

const char* flag(bool known, bool value) { return known ? (value ? "1" : "0") : "-"; }

}

void on_logic_step(uint64_t step) {
    static const char* path = env::path(env::TEST_STATUS_TRACE);
    static const char* align = env::TEST_ALIGN_SCENE();
    static const bool on = (path && *path) || (align && *align);
    if (!on) return;
    static FILE* file = path && *path ? std::fopen(path, "a") : nullptr;
    static uint64_t last = ~0ull;
    static bool aligned = false;
    if (step == last) return;
    last = step;

    mods::lua_sdk::SceneReport scene;
    mods::lua_sdk::scene_report(scene);
    if (align && *align && !aligned && same_name(scene.scene.c_str(), align)) {
        aligned = true;
        script_frame::set_align_frame(render::frame_count());
        LOG("[test] align at logic step %llu scene=%s", (unsigned long long)step, scene.scene.c_str());
    }
    if (!file) return;

    char pos[96] = "- - -";
    interp::PlayerSnapshot snap;
    if (interp::player_snapshot(snap, 0)) std::snprintf(pos, sizeof pos, "%.3f %.3f %.3f", snap.x, snap.y, snap.z);
    std::fprintf(file, "%llu %s %s %s %s %s\n", (unsigned long long)step, scene.scene.empty() ? "-" : scene.scene.c_str(),
                 flag(scene.map_known, scene.map), flag(scene.course_known, scene.course),
                 scene.pack.empty() ? "-" : scene.pack.c_str(), pos);
    std::fflush(file);
}

}
