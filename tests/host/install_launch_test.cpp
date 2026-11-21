#include "core/install_launch.h"

#include <cstdio>
#include <string>

static int failures = 0;

static void check(bool ok, const char* what) {
    if (!ok) {
        std::fprintf(stderr, "FAIL: %s\n", what);
        failures++;
    }
}

int main() {
    const install_launch::Paths extracted =
        install_launch::from_install_json("/games/NSMBU/data", "{\n \"game_dir\": \"game\"\n}\n");
    check(extracted.error.empty(), "extracted: no error");
    check(extracted.game == "/games/NSMBU/data/game", "extracted game folder");
    check(extracted.save == "/games/NSMBU/data/nsmbu/save", "extracted saves");

    const install_launch::Paths folder = install_launch::from_install_json(
        "/games/NSMBU/data", "{\"game_dir\": \"/dumps/nsmbu\"}");
    check(folder.game == "/dumps/nsmbu", "in-place folder stays put");
    check(folder.save == "/games/NSMBU/data/nsmbu/save", "saves stay in the data folder");

    const install_launch::Paths windows = install_launch::from_install_json(
        "D:\\NSMBU\\data", "{\"game_dir\": \"C:\\\\dumps\\\\nsmbu\"}");
    check(windows.game == "C:\\dumps\\nsmbu", "windows path unescaped");
    check(windows.save == "D:\\NSMBU\\data\\nsmbu/save", "windows save join");

    const install_launch::Paths missing = install_launch::from_install_json("/data", "");
    check(!missing.error.empty() && missing.game.empty(), "missing record");

    const install_launch::Paths bare = install_launch::from_install_json("/data", "{\"version\": \"1\"}");
    check(bare.error.find("no game folder") != std::string::npos, "record without game_dir");

    return failures ? 1 : 0;
}
