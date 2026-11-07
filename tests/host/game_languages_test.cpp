
#include <cassert>
#include <cstdio>
#include <filesystem>
#include <string>
#include "text/game_languages.h"

namespace config {
std::string game_dir, save_dir;
}

namespace fs = std::filesystem;

int main(int argc, char** argv) {
    assert(argc == 2);
    assert(std::string(game_lang::name(1)) == "English" && std::string(game_lang::name(11)) == "Chinese (Taiwan)" &&
           std::string(game_lang::name(12)) == "?" && std::string(game_lang::name(-1)) == "?");

    const fs::path root = fs::path(argv[1]);
    std::error_code ec;
    fs::remove_all(root, ec);
    fs::create_directories(root / "content" / "US" / "English", ec);
    fs::create_directories(root / "content" / "US" / "French", ec);
    fs::create_directories(root / "content" / "US" / "font", ec);
    fs::create_directories(root / "content" / "US" / "timestamp", ec);
    config::game_dir = root.string();

    assert(game_lang::carried(1) && game_lang::carried(2) && !game_lang::carried(5) && !game_lang::carried(3));
    assert(game_lang::usable(2) == 2 && game_lang::usable(5) == 1 && game_lang::usable(3) == 1 &&
           game_lang::usable(12) == 1 && game_lang::usable(-1) == 1);
    assert(game_lang::choose(7).language == 1);
    assert(game_lang::choose(2).language == 2);

    const fs::path empty = root / "empty";
    fs::create_directories(empty, ec);
    config::game_dir = empty.string();
    assert(game_lang::carried(1) && !game_lang::carried(2) && game_lang::usable(2) == 1);

    assert(game_lang::started() == -1);
    game_lang::set_started(5);
    assert(game_lang::started() == 5);
    assert(game_lang::current().language == 5);

    fs::remove_all(root, ec);
    puts("game_languages_test: language names, disc carried, fallback to English, started state passed");
}
