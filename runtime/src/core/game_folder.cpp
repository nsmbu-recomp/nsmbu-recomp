#include "core/game_folder.h"

#include "core/user_error.h"

#include <algorithm>
#include <cctype>
#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

namespace game_folder {
namespace {

namespace fs = std::filesystem;

std::string lower(std::string s) {
    std::transform(s.begin(), s.end(), s.begin(), [](unsigned char c) { return (char)tolower(c); });
    return s;
}

fs::path child_ci(const fs::path& at, const std::string& want) {
    std::error_code ec;
    const std::string target = lower(want);
    for (auto& e : fs::directory_iterator(at, ec)) {
        if (ec) break;
        if (lower(e.path().filename().string()) == target) return e.path();
    }
    return {};
}

fs::path resolve_ci(const fs::path& root, const std::vector<std::string>& parts) {
    fs::path at = root;
    for (const std::string& part : parts) {
        fs::path next = child_ci(at, part);
        if (next.empty()) return {};
        at = std::move(next);
    }
    return at;
}

bool file_ci(const fs::path& root, const std::vector<std::string>& parts) {
    const fs::path p = resolve_ci(root, parts);
    std::error_code ec;
    return !p.empty() && fs::is_regular_file(p, ec) && !ec;
}

std::string meta_title_id(const fs::path& root) {
    const fs::path meta = resolve_ci(root, {"meta", "meta.xml"});
    if (meta.empty()) return {};
    std::ifstream in(meta, std::ios::binary);
    if (!in) return {};
    std::string data((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
    const std::string key = "<title_id";
    const size_t tag = lower(data).find(key);
    if (tag == std::string::npos) return {};
    const size_t gt = data.find('>', tag);
    const size_t lt = gt == std::string::npos ? std::string::npos : data.find('<', gt + 1);
    if (gt == std::string::npos || lt == std::string::npos || lt <= gt + 1) return {};
    std::string id;
    for (size_t i = gt + 1; i < lt; i++) {
        const unsigned char c = (unsigned char)data[i];
        if (std::isxdigit(c)) id.push_back((char)tolower(c));
    }
    return id.size() == 16 ? id : std::string{};
}

std::string format_title_id(const std::string& id) {
    if (id.size() != 16) return id;
    return id.substr(0, 8) + "-" + id.substr(8);
}

const char kFix[] =
    "Use the full game with the 1.3.0 update merged into one folder.\n"
    "Do not point at an Update / 0005000e… folder alone.\n"
    "Run setup again with both the game and the update.";

std::string with_path(const std::string& head, const std::string& root) {
    return head + "\n\nPath:\n" + root + "\n\n" + kFix;
}

}

std::string unusable_reason(const std::string& root) {
    const fs::path base(root);
    std::error_code ec;
    if (!fs::is_directory(base, ec) || ec) return with_path("Game folder not found.", root);
    if (!file_ci(base, {"code", "red-pro2.rpx"})) return with_path("Game folder is missing code/red-pro2.rpx.", root);
    if (resolve_ci(base, {"content"}).empty()) return with_path("Game folder is missing content/.", root);
    if (!file_ci(base, {"meta", "meta.xml"})) return with_path("Game folder is missing meta/meta.xml.", root);

    const std::string tid = meta_title_id(base);
    if (tid.size() == 16 && tid.compare(0, 8, "0005000e") == 0) {
        return with_path("This folder is only the 1.3.0 update (title " + format_title_id(tid) + "), not the full game.",
                         root);
    }
    if (!file_ci(base, {"content", "Common", "course_res_pack", "1-1.szs"})) {
        return with_path("This game folder is incomplete (missing content/Common/course_res_pack/1-1.szs).\n"
                         "That usually means only the update was installed.",
                         root);
    }
    return {};
}

[[noreturn]] void fail(const std::string& reason) { user_error::fail(reason); }

}
