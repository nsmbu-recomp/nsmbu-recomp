#include "core/env.h"

#include <algorithm>
#include <cctype>
#include <map>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

#if defined(_WIN32)
#include <stdlib.h>
#endif

namespace env {
namespace {

struct Slot {
    std::string value;
    int rank;
};

struct State {
    std::map<std::string, const Var*> catalog;
    std::unordered_map<std::string, Slot> values;
};

State& state() {
    static State s;
    return s;
}

const char* lookup(const char* key) {
    auto it = state().values.find(key);
    if (it == state().values.end()) return nullptr;
    return it->second.value.c_str();
}

void put(const char* key, const char* value, int rank) {
    if (!key) return;
    if (!value) {
        state().values.erase(key);
        return;
    }
    auto& values = state().values;
    auto it = values.find(key);
    if (it != values.end() && it->second.rank > rank) return;
    values[key] = Slot{value, rank};
}

std::string flag_of(const std::string& key) {
    std::string s = key;
    if (s.rfind("NSMBU_", 0) == 0) s.erase(0, 6);
    for (char& c : s) {
        if (c == '_') c = '-';
        else if (c >= 'A' && c <= 'Z') c = char(c - 'A' + 'a');
    }
    return s;
}

bool redacted(const std::string& key) {
    return key.find("KEY") != std::string::npos || key.find("TOKEN") != std::string::npos ||
        key.find("SECRET") != std::string::npos || key.find("PASSWORD") != std::string::npos;
}

std::string one_line(const std::string& value) {
    std::string out = value;
    for (char& c : out) {
        if (c == '\n' || c == '\r') c = ' ';
    }
    return out;
}

const Var* find_flag(const std::string& name) {
    for (const auto& entry : state().catalog) {
        if (flag_of(entry.first) == name) return entry.second;
    }
    return nullptr;
}

void apply_one(const std::string& name, const char* value, int rank) {
    if (const Var* var = find_flag(name)) put(var->key, value, rank);
}

} // namespace

Var::Var(const char* k) : key(k) {
    if (k) state().catalog.emplace(k, this);
}

const char* Var::operator()() const { return lookup(key); }

bool Var::present() const { return lookup(key) != nullptr; }

bool Var::on() const {
    const char* e = lookup(key);
    return e && *e && std::strcmp(e, "0") != 0;
}

bool Var::enabled() const {
    const char* e = lookup(key);
    return e && std::atoi(e) != 0;
}

int Var::integer_or(int fallback) const {
    const char* e = lookup(key);
    return e ? std::atoi(e) : fallback;
}

double Var::real_or(double fallback) const {
    const char* e = lookup(key);
    return e ? std::atof(e) : fallback;
}

unsigned long long Var::u64_or(unsigned long long fallback) const {
    const char* e = lookup(key);
    return e ? std::strtoull(e, nullptr, 10) : fallback;
}

unsigned long Var::ul_or(unsigned long fallback, int base) const {
    const char* e = lookup(key);
    return e ? std::strtoul(e, nullptr, base) : fallback;
}

void set(const Var& var, const char* value) { put(var.key, value, 2); }

void set_new(const Var& var, const char* value) { put(var.key, value, 0); }

void set_named(const char* key, const char* value) { put(key, value, 2); }

void unset(const Var& var) { put(var.key, nullptr, 2); }

void apply_flags(int argc, char** argv) {
    for (int i = 1; i < argc; i++) {
        const char* a = argv[i];
        if (!a || a[0] != '-' || a[1] != '-') continue;
        if (a[2] == '\0') break;
        const char* eq = std::strchr(a + 2, '=');
        std::string name;
        const char* value = "1";
        if (eq) {
            name.assign(a + 2, eq);
            value = eq + 1;
        } else {
            name = a + 2;
            if (i + 1 < argc && argv[i + 1] && argv[i + 1][0] != '-') {
                if (find_flag(name)) value = argv[++i];
            }
        }
        apply_one(name, value, 2);
    }
}

void apply_file_line(const char* line) {
    if (!line) return;
    while (*line == ' ' || *line == '\t') line++;
    if (*line == '\0' || *line == '#' || *line == '\n' || *line == '\r') return;
    std::string text = line;
    while (!text.empty() && (text.back() == '\n' || text.back() == '\r' || text.back() == ' ' || text.back() == '\t'))
        text.pop_back();
    if (text.rfind("--", 0) != 0) return;
    const auto eq = text.find('=');
    const auto sp = text.find(' ');
    std::string name;
    const char* value = "1";
    std::string stored;
    if (eq != std::string::npos && (sp == std::string::npos || eq < sp)) {
        name = text.substr(2, eq - 2);
        stored = text.substr(eq + 1);
        value = stored.c_str();
    } else if (sp != std::string::npos) {
        name = text.substr(2, sp - 2);
        stored = text.substr(sp + 1);
        value = stored.c_str();
    } else {
        name = text.substr(2);
    }
    apply_one(name, value, 1);
}

std::string assignments() {
    std::vector<std::pair<std::string, std::string>> rows;
    rows.reserve(state().values.size());
    for (const auto& entry : state().values) rows.emplace_back(entry.first, entry.second.value);
    std::sort(rows.begin(), rows.end());
    std::string out;
    for (const auto& row : rows) {
        if (redacted(row.first)) out += row.first + "=<redacted>\n";
        else out += row.first + "=" + one_line(row.second) + "\n";
    }
    return out;
}

void print_flags(std::FILE* out) {
    std::vector<std::pair<std::string, std::string>> rows;
    for (const auto& entry : state().catalog) rows.emplace_back(flag_of(entry.first), entry.first);
    std::sort(rows.begin(), rows.end());
    std::fprintf(out, "\nSettings (a bare flag sets 1; --name=value sets a value):\n");
    for (const auto& row : rows)
        std::fprintf(out, "  --%-32s %s\n", row.first.c_str(), row.second.c_str());
}

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

}
