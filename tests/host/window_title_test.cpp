#include "core/build_info.h"
#include "core/window_title.h"
#include "guest_addr.h"

#include <cstdio>
#include <cstring>

static int failures = 0;

static void check(bool ok, const char* what) {
    if (!ok) {
        std::fprintf(stderr, "FAIL: %s\n", what);
        failures++;
    }
}

static bool ascii_only(const char* s) {
    for (; *s; ++s) {
        if ((unsigned char)*s > 127)
            return false;
    }
    return true;
}

int main() {
    char buf[window_title::kCap];
    char game[window_title::kCap];
    window_title::format_base(buf, sizeof buf);
    window_title::format_game_label(game, sizeof game);
    check(std::strstr(buf, "New Super Mario Bros. U") != nullptr, "app name");
    check(std::strstr(buf, game) != nullptr, "game label");
    check(std::strstr(buf, "Europe") == nullptr && std::strstr(buf, "European") == nullptr, "no region prose");
    check(ascii_only(buf), "ascii only");
    if (build::dev()) {
        check(std::strstr(buf, " git") != nullptr, "local build label");
        check(std::strchr(buf, '+') == nullptr, "no git describe suffix");
    } else
        check(std::strstr(buf, build::version()) != nullptr, "release version");
    return failures ? 1 : 0;
}
