#include "core/game_folder.h"
#include "core/user_error.h"

#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <string>

namespace user_error {
[[noreturn]] void fail(const std::string&, IssueKind) { std::abort(); }
}

// Issue #85: Play must show the full path when the game folder is missing.
// Issue #6: an update-only folder is not a playable game.

static int g_failed = 0;

static void check(bool ok, const char* what) {
    if (!ok) {
        std::fprintf(stderr, "FAIL: %s\n", what);
        g_failed++;
    }
}

static void write_tree(const std::filesystem::path& root, const char* title, bool course) {
    std::filesystem::create_directories(root / "code");
    std::filesystem::create_directories(root / "meta");
    std::filesystem::create_directories(root / "content");
    std::ofstream(root / "code" / "red-pro2.rpx").close();
    std::ofstream meta(root / "meta" / "meta.xml");
    meta << "<menu><title_id type=\"hexBinary\" length=\"8\">" << title << "</title_id></menu>";
    if (course) {
        std::filesystem::create_directories(root / "content" / "Common" / "course_res_pack");
        std::ofstream(root / "content" / "Common" / "course_res_pack" / "1-1.szs").close();
    }
}

int main() {
    namespace fs = std::filesystem;
    const std::string missing = game_folder::unusable_reason("missing-game-folder-issue-85");
    check(missing.find("Game folder not found.") != std::string::npos, "missing folder names the failure");
    check(missing.find("Run setup again and choose your copy of the game.") != std::string::npos, "missing folder asks for setup");
    const auto path_at = missing.find("Path:\n");
    check(path_at != std::string::npos, "missing folder has a path");
    if (path_at != std::string::npos) {
        std::string path = missing.substr(path_at + 6);
        const auto end = path.find("\n\n");
        if (end != std::string::npos) path.resize(end);
        check(path != "missing-game-folder-issue-85", "path is not the bare relative name");
        check(fs::path(path).is_absolute(), "path is absolute");
    }

    const fs::path tmp = fs::temp_directory_path() / "nsmbu-game-folder-test";
    std::error_code ec;
    fs::remove_all(tmp, ec);
    write_tree(tmp / "update", "0005000e10101d00", false);
    const std::string update = game_folder::unusable_reason((tmp / "update").string());
    check(update.find("only the 1.3.0 update") != std::string::npos, "update-only folder is rejected");

    write_tree(tmp / "full", "0005000010101d00", true);
    check(game_folder::unusable_reason((tmp / "full").string()).empty(), "full folder is playable");

    write_tree(tmp / "nocourse", "0005000010101d00", false);
    const std::string incomplete = game_folder::unusable_reason((tmp / "nocourse").string());
    check(incomplete.find("1-1.szs") != std::string::npos, "folder without 1-1 is incomplete");
    fs::remove_all(tmp, ec);

    if (g_failed) {
        std::fprintf(stderr, "game_folder_test: %d check(s) failed\n", g_failed);
        return 1;
    }
    std::printf("game_folder_test: all checks passed\n");
    return 0;
}
