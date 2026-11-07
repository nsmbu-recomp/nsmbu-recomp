#include "course_hot_reload.h"

#include "core/env.h"
#include "core/runtime.h"
#include "guest_addr.h"
#include "mods/content.h"
#include "mods/sdk/nsmbu.h"

#define XXH_INLINE_ALL
#include "xxhash.h"

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <mutex>
#include <string>
#include <utility>
#include <vector>

namespace course_hot_reload {
namespace {

namespace fs = std::filesystem;
using Clock = std::chrono::steady_clock;

const uint32_t kCourseTask = GD(0x101D15F0);
const uint32_t kCourseInfo = GD(0x101D18EC);

struct FileStat {
    bool exists = false;
    uint64_t size = 0;
    int64_t mtime = 0;

    bool operator==(const FileStat& other) const {
        return exists == other.exists && size == other.size && mtime == other.mtime;
    }

    bool operator!=(const FileStat& other) const { return !(*this == other); }
};

std::atomic<bool> g_enabled{false};
std::atomic<bool> g_force{false};
std::mutex g_info_mu;
std::string g_pack;
std::string g_path;
std::string g_status = "no active course";
bool g_live = false;
bool g_reloading = false;
bool g_have_hash = false;
uint64_t g_hash = 0;
uint64_t g_reload_generation = 0;
uint8_t g_reload_world = 0;
uint8_t g_reload_course = 0;
uint32_t g_reload_task = 0;
Clock::time_point g_reload_done_at;
bool g_reload_done_seen = false;
FileStat g_checked;
FileStat g_pending;
int g_stable = 0;
Clock::time_point g_last_poll;
Clock::time_point g_reload_at;
bool g_polled = false;

bool env_locked_impl() { return env::HOT_RELOAD_COURSES.present(); }

bool enabled_impl() {
    if (env_locked_impl()) return env::HOT_RELOAD_COURSES.on();
    return g_enabled.load(std::memory_order_relaxed);
}

void set_status(std::string status) {
    std::lock_guard<std::mutex> lk(g_info_mu);
    g_status = std::move(status);
}

bool read_course(uint8_t& world, uint8_t& course) {
    if (ld32(kCourseTask) == 0) return false;
    const uint32_t info = ld32(kCourseInfo);
    if (info == 0) return false;
    const uint8_t now_world = ld8(info + 0x10);
    const uint8_t now_course = ld8(info + 0x11);
    const uint8_t next_world = ld8(info + 0x1C);
    const uint8_t next_course = ld8(info + 0x1D);
    const bool now_ok = now_world <= 8;
    const bool next_ok = next_world <= 8;
    if (now_ok) {
        world = now_world;
        course = now_course;
        return true;
    }
    if (next_ok) {
        world = next_world;
        course = next_course;
        return true;
    }
    return false;
}

std::string pack_path(const std::string& pack) {
    const std::string guest = "/vol/content/Common/course_res_pack/" + pack + ".szs";
    std::string host = mods::content::replacement(guest);
    if (!host.empty()) return host;
    return (fs::path(config::game_dir) / "content" / "Common" / "course_res_pack" / (pack + ".szs")).string();
}

FileStat stat_file(const std::string& path) {
    std::error_code ec;
    FileStat out;
    out.size = fs::file_size(fs::path(path), ec);
    if (ec) return out;
    const fs::file_time_type time = fs::last_write_time(fs::path(path), ec);
    if (ec) return out;
    out.exists = true;
    out.mtime = (int64_t)time.time_since_epoch().count();
    return out;
}

bool yaz0_valid(const std::vector<uint8_t>& data, std::string& reason) {
    if (data.size() < 16) {
        reason = "short Yaz0 header";
        return false;
    }
    if (std::memcmp(data.data(), "Yaz0", 4) != 0) {
        reason = "not Yaz0";
        return false;
    }
    const uint64_t out = ((uint64_t)data[4] << 24) | ((uint64_t)data[5] << 16) | ((uint64_t)data[6] << 8) | data[7];
    if (out < 0x20 || out > 0x40000000ull) {
        reason = "bad Yaz0 size";
        return false;
    }

    size_t src = 16;
    uint64_t dst = 0;
    while (dst < out) {
        if (src >= data.size()) {
            reason = "truncated Yaz0";
            return false;
        }
        uint8_t code = data[src++];
        for (int bit = 0; bit < 8 && dst < out; bit++, code = (uint8_t)(code << 1)) {
            if (code & 0x80) {
                if (src >= data.size()) {
                    reason = "truncated Yaz0";
                    return false;
                }
                src++;
                dst++;
                continue;
            }
            if (src + 1 >= data.size()) {
                reason = "truncated Yaz0";
                return false;
            }
            const uint32_t b1 = data[src];
            const uint32_t b2 = data[src + 1];
            src += 2;
            const uint64_t dist = (((b1 & 0x0fu) << 8) | b2) + 1;
            uint64_t len = b1 >> 4;
            if (len == 0) {
                if (src >= data.size()) {
                    reason = "truncated Yaz0";
                    return false;
                }
                len = data[src++] + 0x12;
            } else {
                len += 2;
            }
            if (dist > dst) {
                reason = "bad Yaz0 back-reference";
                return false;
            }
            dst += len;
        }
    }
    return true;
}

bool read_valid(const std::string& path, const FileStat& expected, std::vector<uint8_t>& data, std::string& reason) {
    std::ifstream in(fs::path(path), std::ios::binary);
    if (!in) {
        reason = "cannot open";
        return false;
    }
    in.seekg(0, std::ios::end);
    const std::streamoff end = in.tellg();
    if (end < 0 || (uint64_t)end != expected.size) {
        reason = "size changed";
        return false;
    }
    in.seekg(0, std::ios::beg);
    data.resize((size_t)end);
    if (end > 0) {
        in.read(reinterpret_cast<char*>(data.data()), end);
        if (in.gcount() != end) {
            reason = "short read";
            return false;
        }
    }
    return yaz0_valid(data, reason);
}

uint64_t hash_data(const std::vector<uint8_t>& data) {
    return XXH3_64bits(data.data(), data.size());
}

void queue_reload(uint8_t world, uint8_t course, const std::string& pack) {
    mods::scene::request_load_story(world, course);
    g_reloading = true;
    g_reload_at = Clock::now();
    g_reload_generation = mods::scene::story_load_generation();
    g_reload_world = world;
    g_reload_course = course;
    g_reload_task = ld32(kCourseTask);
    g_reload_done_seen = false;
    set_status("reload requested");
    LOG("[course-reload] %s changed, reloading", pack.c_str());
}

void baseline(const std::string& path) {
    g_checked = stat_file(path);
    g_pending = {};
    g_stable = 0;
    g_have_hash = false;
    std::vector<uint8_t> data;
    std::string reason;
    if (g_checked.exists && read_valid(path, g_checked, data, reason)) {
        g_hash = hash_data(data);
        g_have_hash = true;
    }
    set_status("watching");
    LOG("[course-reload] watching %s", path.c_str());
}

}

bool enabled() { return enabled_impl(); }

void set_enabled(bool on) {
    if (!env_locked_impl()) g_enabled.store(on, std::memory_order_relaxed);
}

bool env_locked() { return env_locked_impl(); }

void request_reload() { g_force.store(true, std::memory_order_relaxed); }

Info info() {
    Info out;
    out.enabled = enabled_impl();
    out.env_locked = env_locked_impl();
    std::lock_guard<std::mutex> lk(g_info_mu);
    out.pack = g_pack;
    out.path = g_path;
    out.status = g_status;
    return out;
}

void tick() {
    const int poll_ms = std::clamp(env::HOT_RELOAD_POLL_MS.integer_or(250), 50, 5000);
    const Clock::time_point now = Clock::now();
    if (g_polled && now - g_last_poll < std::chrono::milliseconds(poll_ms)) return;
    g_polled = true;
    g_last_poll = now;

    uint8_t guest_world = 0;
    uint8_t guest_course = 0;
    const bool live = read_course(guest_world, guest_course);
    uint8_t world = guest_world;
    uint8_t course = guest_course;
    if (g_reloading) {
        world = g_reload_world;
        course = g_reload_course;
    }
    std::string pack;
    std::string path;
    if (live) {
        char name[32];
        std::snprintf(name, sizeof name, "%d-%d", world + 1, course + 1);
        pack = name;
        path = pack_path(pack);
    }

    std::string previous_pack;
    std::string previous_path;
    {
        std::lock_guard<std::mutex> lk(g_info_mu);
        previous_pack = g_pack;
        previous_path = g_path;
    }
    const bool path_changed = live && (pack != previous_pack || path != previous_path);
    if (live) {
        std::lock_guard<std::mutex> lk(g_info_mu);
        g_pack = pack;
        g_path = path;
    }
    const bool was_live = g_live;
    g_live = live;

    if (!live) {
        if (g_reloading) set_status("leaving course");
        else set_status("no active course");
        return;
    }

    if (path_changed) {
        g_reloading = false;
        baseline(path);
        return;
    }

    if (!was_live && !g_reloading) {
        baseline(path);
        return;
    }

    if (g_force.exchange(false, std::memory_order_relaxed)) {
        if (g_reloading || mods::scene::story_leave_pending()) {
            g_force.store(true, std::memory_order_relaxed);
            set_status("waiting for course transition");
            return;
        }
        const FileStat st = stat_file(path);
        if (!st.exists) {
            set_status("pack missing");
            return;
        }
        std::vector<uint8_t> data;
        std::string reason;
        if (!read_valid(path, st, data, reason)) {
            set_status("invalid: " + reason);
            LOG("[course-reload] %s rejected: %s", pack.c_str(), reason.c_str());
            return;
        }
        g_checked = st;
        g_pending = {};
        g_stable = 0;
        g_have_hash = true;
        g_hash = hash_data(data);
        queue_reload(world, course, pack);
        return;
    }

    const FileStat st = stat_file(path);
    if (!enabled_impl()) {
        g_checked = st;
        g_pending = {};
        g_stable = 0;
        g_have_hash = false;
        set_status("off");
        return;
    }

    if (g_reloading) {
        const bool done = mods::scene::story_load_generation() != g_reload_generation;
        if (done && !g_reload_done_seen) {
            g_reload_done_seen = true;
            g_reload_done_at = now;
        }
        const bool same_course = guest_world == g_reload_world && guest_course == g_reload_course;
        const uint32_t task = ld32(kCourseTask);
        const bool new_task = task != 0 && task != g_reload_task;
        const bool settled = done && same_course &&
            (new_task || (g_reload_done_seen && now - g_reload_done_at > std::chrono::seconds(3)));
        if (settled) {
            g_reloading = false;
            set_status("watching");
            LOG("[course-reload] %s reloaded", pack.c_str());
        } else {
            if (done) set_status("returning to course");
            else set_status(mods::scene::story_leave_pending() ? "leaving course" : "reload requested");
            if (now - g_reload_at > std::chrono::seconds(30)) {
                g_reloading = false;
                set_status("reload timed out");
                LOG("[course-reload] %s reload timed out", pack.c_str());
            }
            return;
        }
    }

    if (!st.exists) {
        g_checked = {};
        set_status("pack missing");
        return;
    }

    if (st != g_checked) {
        if (st != g_pending) {
            g_pending = st;
            g_stable = 1;
            set_status("changed");
            return;
        }
        g_stable++;
        if (g_stable < 2) return;

        std::vector<uint8_t> data;
        std::string reason;
        if (!read_valid(path, st, data, reason)) {
            g_checked = st;
            g_pending = {};
            g_stable = 0;
            set_status("invalid: " + reason);
            LOG("[course-reload] %s rejected: %s", pack.c_str(), reason.c_str());
            return;
        }
        const uint64_t hash = hash_data(data);
        const bool changed = !g_have_hash || hash != g_hash;
        g_checked = st;
        g_pending = {};
        g_stable = 0;
        g_hash = hash;
        g_have_hash = true;
        if (!changed) {
            set_status("watching");
            return;
        }
        if (g_reloading || mods::scene::story_leave_pending()) {
            set_status("waiting for course transition");
            return;
        }
        queue_reload(world, course, pack);
        return;
    }

    if (!g_reloading) set_status("watching");
}

}
