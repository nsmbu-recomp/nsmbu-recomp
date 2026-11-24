

#include "../platform/filesystem.h"

#include <cstdio>
#include "core/env.h"
#ifndef _WIN32
#include <strings.h>
#endif
#include <mutex>
#include <string>
#include <unordered_map>
#include <vector>

#include "core/runtime.h"
#include "core/write_watch.h"
#include "../mods/content.h"
#include "text/rtl_text.h"

namespace {

enum FSStatus : int32_t {
    FS_OK = 0,
    FS_END = -2,
    FS_EXISTS = -5,
    FS_NOT_FOUND = -6,
    FS_NOT_FILE = -7,
    FS_NOT_DIR = -8,
    FS_ACCESS_ERROR = -10,
};

struct OpenFile {
    FILE* f;
    std::string path;
    std::string mode;
};
struct OpenDir {
    DIR* d;
    std::string path;
    std::string gpath;
    uint32_t read = 0;
};

std::mutex g_fs_mutex;
std::unordered_map<uint32_t, OpenFile> g_files;
std::unordered_map<uint32_t, OpenDir> g_dirs;
uint32_t g_next_handle = 1;
std::string g_cwd = "/vol/content";

std::string normalize_abs(const std::string& p) {
    std::vector<std::string> stack;
    std::string cur;
    for (size_t i = 0; i <= p.size(); i++) {
        if (i == p.size() || p[i] == '/') {
            if (cur == "..") {
                if (!stack.empty()) stack.pop_back();
            } else if (!cur.empty() && cur != ".") stack.push_back(cur);
            cur.clear();
        } else cur.push_back(p[i]);
    }
    std::string out;
    for (const auto& s : stack) { out.push_back('/'); out += s; }
    return out.empty() ? std::string("/") : out;
}

std::string with_cwd(const std::string& guest) {
    if (guest.empty() || guest[0] == '/') return guest;
    std::string base = g_cwd;
    if (!base.empty() && base.back() != '/') base.push_back('/');
    return base + guest;
}

std::string host_path_exact(const std::string& guest) {
    std::string p = with_cwd(guest);
    auto map = [&](const char* prefix, const std::string& root) -> bool {
        size_t n = strlen(prefix);
        if (p.compare(0, n, prefix) == 0) {
            p = root + p.substr(n);
            return true;
        }
        return false;
    };
    if (p == "/vol/aoc/0010" || p.starts_with("/vol/aoc/0010/"))
        return config::game_dir + "/content/RDashRes" + p.substr(strlen("/vol/aoc/0010"));
    if (map("/vol/content", config::game_dir + "/content")) return p;
    if (map("/vol/code", config::game_dir + "/code")) return p;
    if (map("/vol/meta", config::game_dir + "/meta")) return p;
    if (map("/vol/save", config::save_dir)) return p;
    if (!p.empty() && p[0] != '/') return config::game_dir + "/content/" + p;
    return config::game_dir + p;
}

#ifndef _WIN32

std::string resolve_case(const std::string& p) {
    struct stat st;
    if (stat(p.c_str(), &st) == 0) return p;
    static std::mutex mu;
    static std::unordered_map<std::string, std::string> dirs;
    std::lock_guard<std::mutex> lk(mu);
    auto lower = [](std::string s) { for (char& ch : s) ch = (char)tolower((unsigned char)ch); return s; };
    std::string cur = p.compare(0, 1, "/") == 0 ? "/" : "";
    size_t pos = cur.size();
    while (pos <= p.size()) {
        size_t e = p.find('/', pos);
        if (e == std::string::npos) e = p.size();
        std::string comp = p.substr(pos, e - pos);
        pos = e + 1;
        if (comp.empty()) { if (e == p.size()) break; continue; }
        std::string base = cur.empty() ? "" : (cur == "/" ? "/" : cur + "/");
        std::string cand = base + comp;
        std::string key = lower(cand);
        if (auto it = dirs.find(key); it != dirs.end()) { cur = it->second; continue; }
        if (stat(cand.c_str(), &st) != 0) {
            std::string found;
            if (DIR* d = opendir(cur.empty() ? "." : cur.c_str())) {
                while (dirent* de = readdir(d))
                    if (!strcasecmp(de->d_name, comp.c_str())) { found = de->d_name; break; }
                closedir(d);
            }
            if (found.empty()) return cand + (e < p.size() ? p.substr(e) : "");
            cand = base + found;
        }
        if (e < p.size()) dirs[key] = cand;
        cur = cand;
    }
    return cur;
}
#endif

std::string host_path(const std::string& guest) {
#ifdef _WIN32
    return host_path_exact(guest);
#else
    return resolve_case(host_path_exact(guest));
#endif
}

std::string read_path(const std::string& guest,const std::string& mode="rb") {
    auto override=mods::content::replacement(guest,mode);
    if(!override.empty())if(const char* trace=env::TEST_CONTENT_TRACE();trace&&std::string(trace)=="1")LOG("[content-mod] read %s -> %s",guest.c_str(),override.c_str());
    return override.empty()?host_path(guest):override;
}

void make_parent_dirs(const std::string& path) {
    std::error_code ec; std::filesystem::create_directories(std::filesystem::path(path).parent_path(),ec);
}

void fill_stat(uint32_t out, const struct stat& st) {
    memset(mem::ptr(out), 0, 0x64);
    bool dir = S_ISDIR(st.st_mode);
    st32(out + 0x00, dir ? 0x80000000u : 0x01000000u);
    st32(out + 0x04, 0x666);
    st32(out + 0x10, dir ? 0 : (uint32_t)st.st_size);
    st32(out + 0x14, dir ? 0 : (uint32_t)st.st_size);
}

int32_t open_file(const std::string& gpath, const std::string& mode, uint32_t out_handle) {
    std::string hp = read_path(gpath,mode);
    if (mode.find_first_of("wa") != std::string::npos) make_parent_dirs(hp);
    std::string m = mode;
    if (m.find('b') == std::string::npos) m += "b";
    FILE* f = fopen(hp.c_str(), m.c_str());
    if (gpath.compare(0, 9, "/vol/save") == 0) {
        long bytes = -1;
        struct stat st{};
        if (f && mode.find_first_of("wa") == std::string::npos && fstat(fileno(f), &st) == 0) bytes = (long)st.st_size;
        else if (!f && stat(hp.c_str(), &st) == 0) bytes = (long)st.st_size;
        if (f && bytes >= 0)
            LOG("[save] fs open %s (%s) -> ok host %s (%ld bytes)", gpath.c_str(), mode.c_str(), hp.c_str(), bytes);
        else
            LOG("[save] fs open %s (%s) -> %s host %s", gpath.c_str(), mode.c_str(), f ? "ok" : "not found", hp.c_str());
    } else {
        TRACE("[fs] open %s (%s) -> %s", gpath.c_str(), mode.c_str(), f ? "ok" : "not found");
        if (!f && gpath.starts_with("/vol/aoc"))
            LOG("[aoc] missing %s host %s", gpath.c_str(), hp.c_str());
    }
    if (!f) return FS_NOT_FOUND;
    if (mode.find_first_of("wa+") == std::string::npos) {
        const size_t slash = gpath.find_last_of('/');
        if (!mods::content::pack_language(slash == std::string::npos ? gpath : gpath.substr(slash + 1)).empty())
            rtl_text::language_pack_opened(hp);
    }
    std::lock_guard<std::mutex> lk(g_fs_mutex);
    uint32_t h = g_next_handle++;
    g_files[h] = {f, gpath, m};
    st32(out_handle, h);
    return FS_OK;
}

FILE* file(uint32_t h) {
    std::lock_guard<std::mutex> lk(g_fs_mutex);
    auto it = g_files.find(h);
    return it == g_files.end() ? nullptr : it->second.f;
}

int32_t stat_path(const std::string& gpath, uint32_t out) {
    struct stat st;
    if (stat(read_path(gpath).c_str(), &st) != 0) return FS_NOT_FOUND;
    fill_stat(out, st);
    return FS_OK;
}

int32_t change_dir(const std::string& guest) {
    std::string next = normalize_abs(with_cwd(guest));
    struct stat st;
    if (stat(host_path(next).c_str(), &st) != 0 || !S_ISDIR(st.st_mode)) return FS_NOT_FOUND;
    g_cwd = next;
    return FS_OK;
}

uint32_t read_file_at(uint32_t handle, uint32_t dst, uint32_t size, uint32_t count, uint64_t pos) {
    FILE* f = file(handle);
    if (!f || size == 0) return 0;
    if (host::file_seek(f, pos, SEEK_SET) != 0) return 0;
    size_t n;
    {
        BlockingScope b;
        wwatch::HostWrite w(dst, (uint32_t)std::min<uint64_t>((uint64_t)size * count, 0x100000000ull - dst));
        n = fread(mem::ptr(dst), 1, (size_t)size * count, f);
    }
    return (uint32_t)(n / size);
}

int32_t open_dir(const std::string& gpath, uint32_t out_handle) {
    DIR* d = opendir(host_path(gpath).c_str());
    TRACE("[fs] opendir %s -> %s", gpath.c_str(), d ? "ok" : "not found");
    if (!d) return FS_NOT_FOUND;
    std::lock_guard<std::mutex> lk(g_fs_mutex);
    uint32_t h = g_next_handle++;
    g_dirs[h] = {d, host_path(gpath), gpath};
    st32(out_handle, h);
    return FS_OK;
}

}

bool aoc_content_ready() {
    struct stat st;
    std::string path = config::game_dir + "/content/RDashRes/course_res_pack/1-1.szs";
    return stat(path.c_str(), &st) == 0 && !S_ISDIR(st.st_mode);
}

HLE(coreinit, FSInit) {}
HLE(coreinit, FSShutdown) {}
HLE(coreinit, FSAddClient) { ret(c, 0); }
HLE(coreinit, FSDelClient) { ret(c, 0); }
HLE(coreinit, FSInitCmdBlock) { memset(mem::ptr(arg(c, 0)), 0, 0xA80); }
HLE(coreinit, FSSetCmdPriority) { ret(c, 0); }
HLE(coreinit, FSSetStateChangeNotification) {}
HLE(coreinit, FSGetVolumeState) { ret(c, 1); }
HLE(coreinit, FSGetLastError) { ret(c, 0); }
HLE(coreinit, FSGetLastErrorCodeForViewer) { ret(c, 0); }
HLE(coreinit, FSGetCwd) { mem::write_cstr(arg(c, 2), g_cwd.c_str(), arg(c, 3)); ret(c, FS_OK); }

HLE(coreinit, FSChangeDir) { ret(c, change_dir(mem::read_cstr(arg(c, 2)))); }

HLE(coreinit, FSReadFileWithPos) {
    uint64_t pos = arg64(c, 8);
    ret(c, read_file_at(arg(c, 7), arg(c, 2), arg(c, 3), arg(c, 4), pos));
}

HLE(coreinit, FSOpenFile) {
    ret(c, open_file(mem::read_cstr(arg(c, 2)), mem::read_cstr(arg(c, 3)), arg(c, 4)));
}

HLE(coreinit, FSCloseFile) {
    std::lock_guard<std::mutex> lk(g_fs_mutex);
    auto it = g_files.find(arg(c, 2));
    if (it != g_files.end()) {
        fclose(it->second.f);
        g_files.erase(it);
    }
    ret(c, FS_OK);
}

HLE(coreinit, FSReadFile) {
    uint32_t dst = arg(c, 2), size = arg(c, 3), count = arg(c, 4);
    FILE* f = file(arg(c, 5));
    if (!f || size == 0) { ret(c, 0); return; }
    size_t n;
    {
        BlockingScope b;

        wwatch::HostWrite w(dst, (uint32_t)std::min<uint64_t>((uint64_t)size * count, 0x100000000ull - dst));
        n = fread(mem::ptr(dst), 1, (size_t)size * count, f);
    }
    ret(c, (uint32_t)(n / size));
}

HLE(coreinit, FSWriteFile) {
    uint32_t src = arg(c, 2), size = arg(c, 3), count = arg(c, 4);
    FILE* f = file(arg(c, 5));
    if (!f || size == 0) { ret(c, 0); return; }
    size_t n;
    {
        BlockingScope b;
        n = fwrite(mem::ptr(src), 1, (size_t)size * count, f);
        fflush(f);
    }
    ret(c, (uint32_t)(n / size));
}

HLE(coreinit, FSSetPosFile) {
    FILE* f = file(arg(c, 2));
    ret(c, f && host::file_seek(f,arg(c,3),SEEK_SET) == 0 ? FS_OK : FS_ACCESS_ERROR);
}

HLE(coreinit, FSGetStat) { ret(c, stat_path(mem::read_cstr(arg(c, 2)), arg(c, 3))); }

HLE(coreinit, FSGetStatFile) {
    FILE* f = file(arg(c, 2));
    struct stat st;
    if (!f || fstat(fileno(f), &st) != 0) { ret(c, FS_NOT_FOUND); return; }
    fill_stat(arg(c, 3), st);
    ret(c, FS_OK);
}

HLE(coreinit, FSOpenDir) { ret(c, open_dir(mem::read_cstr(arg(c, 2)), arg(c, 3))); }

HLE(coreinit, FSReadDir) {
    uint32_t out = arg(c, 3);
    std::lock_guard<std::mutex> lk(g_fs_mutex);
    auto it = g_dirs.find(arg(c, 2));
    if (it == g_dirs.end()) { ret(c, FS_NOT_DIR); return; }
    struct dirent* de;
    while ((de = readdir(it->second.d))) {
        if (!strcmp(de->d_name, ".") || !strcmp(de->d_name, "..")) continue;
        struct stat st;
        auto path=it->second.path + "/" + de->d_name;
        auto replacement=mods::content::replacement(it->second.gpath + "/" + de->d_name);
        if(!replacement.empty())path=std::move(replacement);
        if (stat(path.c_str(), &st) != 0) continue;
        fill_stat(out, st);
        mem::write_cstr(out + 0x64, de->d_name, 256);
        it->second.read++;
        ret(c, FS_OK);
        return;
    }
    ret(c, FS_END);
}

HLE(coreinit, FSCloseDir) {
    std::lock_guard<std::mutex> lk(g_fs_mutex);
    auto it = g_dirs.find(arg(c, 2));
    if (it != g_dirs.end()) {
        closedir(it->second.d);
        g_dirs.erase(it);
    }
    ret(c, FS_OK);
}

static std::string save_path(uint32_t slot, const std::string& p) {
    std::string base = slot == 0xFF ? "/vol/save/common" : "/vol/save/user";
    return base + (p.empty() || p[0] == '/' ? "" : "/") + p;
}

HLE(nn_save, SAVEInit) { ret(c, 0); }
HLE(nn_save, SAVEShutdown) {}
HLE(nn_save, SAVEInitSaveDir) {
    make_parent_dirs(host_path(save_path(arg(c, 0), "x")));
    ret(c, 0);
}
HLE(nn_save, SAVEFlushQuota) { ret(c, 0); }
HLE(nn_save, SAVEOpenFile) {
    uint32_t slot = arg(c, 2);
    std::string rel = mem::read_cstr(arg(c, 3));
    std::string mode = mem::read_cstr(arg(c, 4));
    std::string gp = save_path(slot, rel);
    LOG("[save] SAVEOpenFile slot=%u %s (%s)", slot, rel.c_str(), mode.c_str());
    ret(c, open_file(gp, mode, arg(c, 5)));
}
HLE(nn_save, SAVEGetStat) { ret(c, stat_path(save_path(arg(c, 2), mem::read_cstr(arg(c, 3))), arg(c, 4))); }
HLE(nn_save, SAVEOpenDir) {
    std::string gp = save_path(arg(c, 2), mem::read_cstr(arg(c, 3)));
    make_parent_dirs(host_path(gp) + "/");
    ret(c, open_dir(gp, arg(c, 4)));
}
HLE(nn_save, SAVEGetSharedDataTitlePath) {
    std::string rel = mem::read_cstr(arg(c, 2));
    std::string out = std::string("/vol/save/common") + (rel.empty() || rel[0] == '/' ? "" : "/") + rel;
    mem::write_cstr(arg(c, 3), out.c_str(), arg(c, 4));
    ret(c, 0);
}
HLE(nn_save, SAVEGetSharedSaveDataPath) {
    std::string rel = mem::read_cstr(arg(c, 1));
    std::string out = save_path(arg(c, 0), rel);
    mem::write_cstr(arg(c, 2), out.c_str(), arg(c, 3));
    ret(c, 0);
}

#include <algorithm>
#include <vector>

#include "save/savestate.h"
void fs_ss_save(ss::Writer& w) {
    std::lock_guard<std::mutex> lk(g_fs_mutex);
    w.u32(g_next_handle);
    std::vector<uint32_t> keys;
    for (auto& [h, f] : g_files) keys.push_back(h);
    std::sort(keys.begin(), keys.end());
    w.u32((uint32_t)keys.size());
    for (uint32_t h : keys) {
        OpenFile& f = g_files[h];
        w.u32(h);
        w.str(f.path);
        w.str(f.mode);
        w.u64((uint64_t)host::file_tell(f.f));
    }
    keys.clear();
    for (auto& [h, d] : g_dirs) keys.push_back(h);
    std::sort(keys.begin(), keys.end());
    w.u32((uint32_t)keys.size());
    for (uint32_t h : keys) {
        w.u32(h);
        w.str(g_dirs[h].gpath);
        w.u32(g_dirs[h].read);
    }
}

void fs_reset_for_reboot() {
    std::lock_guard<std::mutex> lk(g_fs_mutex);
    for (auto& [h, f] : g_files) {
        (void)h;
        fclose(f.f);
    }
    g_files.clear();
    for (auto& [h, d] : g_dirs) {
        (void)h;
        closedir(d.d);
    }
    g_dirs.clear();
    g_next_handle = 1;
}

void fs_ss_load(ss::Reader& r) {
    std::lock_guard<std::mutex> lk(g_fs_mutex);
    uint32_t next = r.u32();
    for (auto& [h, f] : g_files) fclose(f.f);
    g_files.clear();
    for (auto& [h, d] : g_dirs) closedir(d.d);
    g_dirs.clear();
    uint32_t n = r.u32();
    for (uint32_t i = 0; i < n && r.ok; i++) {
        uint32_t h = r.u32();
        std::string gp = r.str(), mode = r.str();
        uint64_t pos = r.u64();

        std::string m = mode.find_first_of("wa+") != std::string::npos ? "r+b" : "rb";
        FILE* f = fopen(read_path(gp,m).c_str(), m.c_str());
        if (!f) {
            LOG("[savestate] cannot reopen %s", gp.c_str());
            continue;
        }
        host::file_seek(f,pos,SEEK_SET);
        g_files[h] = {f, gp, mode};
    }
    n = r.u32();
    for (uint32_t i = 0; i < n && r.ok; i++) {
        uint32_t h = r.u32();
        std::string gp = r.str();
        uint32_t read = r.u32();
        DIR* d = opendir(host_path(gp).c_str());
        if (!d) continue;
        OpenDir od{d, host_path(gp), gp, 0};
        while (od.read < read) {
            struct dirent* de = readdir(d);
            if (!de) break;
            if (!strcmp(de->d_name, ".") || !strcmp(de->d_name, "..")) continue;
            od.read++;
        }
        g_dirs[h] = od;
    }
    g_next_handle = std::max(g_next_handle, next);
}
