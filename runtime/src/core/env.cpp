#include "core/env.h"

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
    static thread_local std::string expanded;
    expanded = path[1] == '/' ? std::string(home) + (path + 1) : home;
    return expanded.c_str();
}

const char* path(const Var& var) {
    const char* e = var();
    return expand_path(e);
}

const Var vulkan_cpu_paths[vulkan_cpu_path_count] = {
    Var("NSMBU_VK_REUSE_UNIFORM_SNAPSHOTS"),
    Var("NSMBU_VK_REUSE_FEEDBACK_IMAGES"),
    Var("NSMBU_VK_SKIP_REDUNDANT_BINDS"),
    Var("NSMBU_VK_DESCRIPTOR_RANKS"),
    Var("NSMBU_VK_PIPELINE_LOOKASIDE"),
    Var("NSMBU_VK_SHADER_ADDRESS_MEMO"),
    Var("NSMBU_VK_FETCH_MEMO"),
    Var("NSMBU_VK_SPECIALIZE_INDICES"),
    Var("NSMBU_VK_SHADER_STATE_MEMO"),
    Var("NSMBU_VK_SKIP_VERTEX_BINDS"),
    Var("NSMBU_VK_SAMPLER_MEMO"),
    Var("NSMBU_VK_SPARSE_HASH_MEMO"),
    Var("NSMBU_VK_SHADER_KEY_DIRTY"),
    Var("NSMBU_VK_REUSE_VERTEX_SNAPSHOTS"),
    Var("NSMBU_VK_VERTEX_HISTORY_REUSE"),
};

}
