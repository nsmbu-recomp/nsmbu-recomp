#pragma once

#include <string>

// --install DATA: the game folder and save folder recorded by setup (install.json).
// A disc image or Cemu archive is extracted under DATA/game. An extracted folder is
// used where the player left it, so game_dir in that file may be outside DATA.

namespace install_launch {

struct Paths {
    std::string game;
    std::string save;
    std::string error;
};

inline bool absolute_path(const std::string& path) {
    if (path.empty()) return false;
    if (path[0] == '/' || path[0] == '\\') return true;
    return path.size() > 2 && path[1] == ':' && (path[2] == '/' || path[2] == '\\');
}

inline std::string json_string_field(const std::string& text, const char* key) {
    const std::string pat = std::string("\"") + key + "\"";
    const size_t at = text.find(pat);
    if (at == std::string::npos) return {};
    const size_t colon = text.find(':', at + pat.size());
    if (colon == std::string::npos) return {};
    const size_t quote = text.find('"', colon + 1);
    if (quote == std::string::npos) return {};
    std::string out;
    for (size_t i = quote + 1; i < text.size(); i++) {
        if (text[i] == '\\' && i + 1 < text.size()) {
            const char n = text[++i];
            out += n == 'n' ? '\n' : n == 't' ? '\t' : n;
            continue;
        }
        if (text[i] == '"') break;
        out += text[i];
    }
    return out;
}

inline std::string join_path(const std::string& dir, const std::string& rel) {
    if (dir.empty()) return rel;
    const char last = dir.back();
    if (last == '/' || last == '\\') return dir + rel;
    const char sep = dir.find('\\') != std::string::npos && dir.find('/') == std::string::npos ? '\\' : '/';
    return dir + sep + rel;
}

inline std::string against_data_dir(const std::string& path, const std::string& data_dir) {
    if (path.empty() || data_dir.empty() || absolute_path(path)) return path;
    return join_path(data_dir, path);
}

inline Paths from_install_json(const std::string& data_dir, const std::string& json) {
    Paths paths;
    if (json.empty()) {
        paths.error = "--install: no setup record at " + data_dir + "/install.json";
        return paths;
    }
    const std::string game = json_string_field(json, "game_dir");
    if (game.empty()) {
        paths.error = "--install: setup record has no game folder";
        return paths;
    }
    paths.game = against_data_dir(game, data_dir);
    paths.save = join_path(data_dir, "nsmbu/save");
    return paths;
}

}
