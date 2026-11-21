#include "core/env.h"

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>

static int failures = 0;

static void check(bool ok, const char* what) {
    if (!ok) {
        std::fprintf(stderr, "FAIL: %s\n", what);
        failures++;
    }
}

static void put_process_env(const char* name, const char* value) {
#ifdef _WIN32
    _putenv_s(name, value);
#else
    setenv(name, value, 1);
#endif
}

int main() {
    check(std::strcmp(env::NO_HOST_INPUT.name(), "NSMBU_NO_HOST_INPUT") == 0, "no_host_input name");
    check(std::strcmp(env::VK_STATS.name(), "NSMBU_VK_STATS") == 0, "vk_stats name");

    env::unset(env::NO_HOST_INPUT);
    env::set(env::NO_HOST_INPUT, "first");
    check(env::NO_HOST_INPUT() && std::strcmp(env::NO_HOST_INPUT(), "first") == 0, "set initial");
    env::set_new(env::NO_HOST_INPUT, "second");
    check(std::strcmp(env::NO_HOST_INPUT(), "first") == 0, "set_new does not overwrite");
    env::set(env::NO_HOST_INPUT, "second");
    check(std::strcmp(env::NO_HOST_INPUT(), "second") == 0, "set overwrite");
    env::unset(env::NO_HOST_INPUT);
    check(env::NO_HOST_INPUT() == nullptr, "unset");

    put_process_env(env::NO_HOST_INPUT.name(), "from-process");
    check(env::NO_HOST_INPUT() == nullptr, "process environment is ignored");

    char arg0[] = "nsmbu";
    char arg1[] = "--no-audio";
    char arg2[] = "--press=a,b";
    char arg3[] = "--hidden-windows";
    char arg4[] = "0";
    char arg5[] = "--no-audio=0";
    char* argv[] = {arg0, arg1, arg2, arg3, arg4, arg5};
    env::apply_flags(6, argv);
    check(env::NO_AUDIO() && std::strcmp(env::NO_AUDIO(), "0") == 0, "later flag wins");
    check(env::PRESS() && std::strcmp(env::PRESS(), "a,b") == 0, "equals value");
    check(env::HIDDEN_WINDOWS() && std::strcmp(env::HIDDEN_WINDOWS(), "0") == 0, "following value");
    check(env::NO_AUDIO.enabled() == false, "zero is not enabled");

    char game[] = "--game";
    char dump[] = "dump";
    char* argv2[] = {arg0, game, dump};
    env::apply_flags(3, argv2);
    check(env::PRESS() && std::strcmp(env::PRESS(), "a,b") == 0, "unknown flag leaves settings");

    env::apply_file_line("--no-audio=1\n");
    check(std::strcmp(env::NO_AUDIO(), "0") == 0, "file line does not override argv");
    env::apply_file_line("--swkbd-text=hello\n");
    check(env::SWKBD_TEXT() && std::strcmp(env::SWKBD_TEXT(), "hello") == 0, "file line sets a new flag");
    env::set_new(env::SWKBD_TEXT, "default");
    check(std::strcmp(env::SWKBD_TEXT(), "hello") == 0, "default does not override a file line");

    const std::string listed = env::assignments();
    check(listed.find("NSMBU_PRESS=a,b\n") != std::string::npos, "assignments lists a flag");
    check(listed.find("from-process") == std::string::npos, "assignments skips the process environment");

    check(std::strcmp(env::expand_path("/abs"), "/abs") == 0, "expand absolute");
#ifdef _WIN32
    put_process_env("USERPROFILE", "C:/home/test");
    check(std::strcmp(env::expand_path("~/git/foo"), "C:/home/test/git/foo") == 0, "expand tilde");
    env::set(env::RECORD_INPUT, "~/rec.txt");
    check(std::strcmp(env::path(env::RECORD_INPUT), "C:/home/test/rec.txt") == 0, "path from setting");
#else
    put_process_env("HOME", "/home/test");
    check(std::strcmp(env::expand_path("~/git/foo"), "/home/test/git/foo") == 0, "expand tilde");
    env::set(env::RECORD_INPUT, "~/rec.txt");
    check(std::strcmp(env::path(env::RECORD_INPUT), "/home/test/rec.txt") == 0, "path from setting");
#endif
    env::unset(env::RECORD_INPUT);

    if (failures) return 1;
    std::printf("env_test: all checks passed\n");
    return 0;
}
