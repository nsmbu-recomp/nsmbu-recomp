#include "core/env.h"

#include <cstdio>
#include <cstdlib>
#include <cstring>

static int failures = 0;

static void check(bool ok, const char* what) {
    if (!ok) {
        std::fprintf(stderr, "FAIL: %s\n", what);
        failures++;
    }
}

static void put_env(const char* name, const char* value, int overwrite) {
#ifdef _WIN32
    if (!overwrite && std::getenv(name)) return;
    _putenv_s(name, value);
#else
    setenv(name, value, overwrite);
#endif
}

int main() {
    check(std::strcmp(env::NO_HOST_INPUT.name(), "NSMBU_NO_HOST_INPUT") == 0, "no_host_input name");
    check(std::strcmp(env::VK_STATS.name(), "NSMBU_VK_STATS") == 0, "vk_stats name");

    env::unset(env::NO_HOST_INPUT);
    put_env(env::NO_HOST_INPUT.name(), "first", 0);
    check(env::NO_HOST_INPUT() && std::strcmp(env::NO_HOST_INPUT(), "first") == 0, "set initial");
    put_env(env::NO_HOST_INPUT.name(), "second", 0);
    check(std::strcmp(env::NO_HOST_INPUT(), "first") == 0, "set no overwrite");
    put_env(env::NO_HOST_INPUT.name(), "second", 1);
    check(std::strcmp(env::NO_HOST_INPUT(), "second") == 0, "set overwrite");
    env::unset(env::NO_HOST_INPUT);
    check(env::NO_HOST_INPUT() == nullptr, "unset");

    check(std::strcmp(env::expand_path("/abs"), "/abs") == 0, "expand absolute");
#ifdef _WIN32
    put_env("USERPROFILE", "C:/home/test", 1);
    check(std::strcmp(env::expand_path("~/git/foo"), "C:/home/test/git/foo") == 0, "expand tilde");
    put_env(env::RECORD_INPUT.name(), "~/rec.txt", 1);
    check(std::strcmp(env::path(env::RECORD_INPUT), "C:/home/test/rec.txt") == 0, "path from env");
#else
    put_env("HOME", "/home/test", 1);
    check(std::strcmp(env::expand_path("~/git/foo"), "/home/test/git/foo") == 0, "expand tilde");
    put_env(env::RECORD_INPUT.name(), "~/rec.txt", 1);
    check(std::strcmp(env::path(env::RECORD_INPUT), "/home/test/rec.txt") == 0, "path from env");
#endif
    env::unset(env::RECORD_INPUT);

    if (failures) return 1;
    std::printf("env_test: all checks passed\n");
    return 0;
}
