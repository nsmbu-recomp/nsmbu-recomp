#include "core/env.h"

#include <string>

#if defined(_WIN32)
#include <stdlib.h>
#endif

namespace env {

const char* expand_path(const char* path) {
    if (!path || !path[0]) return path;
    if (path[0] != '~') return path;
    if (path[1] && path[1] != '/') return path;
    const char* home = nullptr;
#if defined(_WIN32)
    home = getenv("USERPROFILE");
    if (!home) home = getenv("HOME");
#else
    home = getenv("HOME");
#endif
    if (!home) home = ".";
    static thread_local std::string out;
    out = path[1] == '/' ? std::string(home) + (path + 1) : home;
    return out.c_str();
}

const char* path(const Var& var) {
    const char* e = var();
    return expand_path(e);
}

const Var vulkan_cpu_paths[vulkan_cpu_path_count] = {
    VK_REUSE_UNIFORM_SNAPSHOTS,
    VK_REUSE_FEEDBACK_IMAGES,
    VK_SKIP_REDUNDANT_BINDS,
    VK_DESCRIPTOR_RANKS,
    VK_PIPELINE_LOOKASIDE,
    VK_SHADER_ADDRESS_MEMO,
    VK_FETCH_MEMO,
    VK_SPECIALIZE_INDICES,
    VK_SHADER_STATE_MEMO,
    VK_SKIP_VERTEX_BINDS,
    VK_SAMPLER_MEMO,
    VK_SPARSE_HASH_MEMO,
    VK_SHADER_KEY_DIRTY,
    VK_REUSE_VERTEX_SNAPSHOTS,
    VK_VERTEX_HISTORY_REUSE
};

void unset(const Var& var) {
#if defined(_WIN32)
    _putenv_s(var.name(), "");
#else
    unsetenv(var.name());
#endif
}

}
