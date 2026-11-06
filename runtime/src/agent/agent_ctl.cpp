#include "agent/agent_ctl.h"

#include <atomic>
#include <chrono>
#include <cerrno>
#include <cstdlib>
#include <cstring>
#include <deque>
#include <exception>
#include <filesystem>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

#ifndef _WIN32
#include <fcntl.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/un.h>
#include <unistd.h>
#else
#include <windows.h>
#endif

#ifdef NSMBU_SDL_HOST
#include <SDL3/SDL.h>
#endif

#include "core/env.h"
#include "gfx/renderer.h"
#ifdef NSMBU_VULKAN
#include "gfx/vulkan/api.h"
#endif
#include "hooks/interp.h"
#include "hooks/map_menu_settings.h"
#include "mods/lua_eval.h"
#include "mods/mod_json.h"
#include "mods/sdk/nsmbu.h"
#include "overlay/hostui.h"
#include "overlay/overlay.h"
#include "core/runtime.h"

namespace agent_ctl {
namespace {

std::atomic<bool> g_active{false};
std::mutex g_mu;
std::thread g_thread;
bool g_stop = false;

#ifndef _WIN32
int g_listen = -1;
int g_client = -1;
std::mutex g_write_mu;
#else
HANDLE g_pipe = INVALID_HANDLE_VALUE;
bool g_win_logged = false;
#endif

struct PadOverlay {
    uint32_t buttons = 0;
    float lx = 0, ly = 0, rx = 0, ry = 0;
    bool stick = false;
    bool rstick = false;
    uint64_t until = 0;
    int samples = 0;
};
PadOverlay g_pad;

struct ShotWait {
    int id = 0;
    std::string path;
    std::chrono::steady_clock::time_point started;
    bool requested = false;
    bool drc = false;
    int ahead = 2;
};
std::deque<ShotWait> g_shots;

struct PictureWait {
    int id = 0;
    std::vector<std::string> paths;
    std::vector<int> ahead;
    std::chrono::steady_clock::time_point started;
    bool requested = false;
};
std::deque<PictureWait> g_pictures;
std::deque<std::string> g_out_lines;
std::atomic<bool> g_quit{false};

enum class GuestOp { Status, Eval, Leave, Goto, Ui, Actors };

struct GuestJob {
    int id = 0;
    GuestOp op = GuestOp::Eval;
    std::string code;
    int timeout_ms = 5000;
    int limit = 16;
    float radius = 0;
};
std::deque<GuestJob> g_jobs;
thread_local bool g_servicing = false;

struct ServiceGuard {
    ServiceGuard() { g_servicing = true; }
    ~ServiceGuard() { g_servicing = false; }
};


void queue_line(const std::string& line) {
    std::lock_guard<std::mutex> lk(g_mu);
    g_out_lines.push_back(line);
}

std::string response_ok(int id) {
    mods::json::Value v;
    v.type = mods::json::Value::Object;
    v.object["id"] = id;
    v.object["ok"] = true;
    return mods::json::dump(v);
}

std::string response_err(int id, const std::string& error) {
    mods::json::Value v;
    v.type = mods::json::Value::Object;
    v.object["id"] = id;
    v.object["ok"] = false;
    v.object["error"] = error;
    return mods::json::dump(v);
}

int request_id(const mods::json::Value& req) {
    const auto& idv = req.get("id");
    if (idv.type != mods::json::Value::Number) return -1;
    return int(idv.number);
}

int request_frames(const mods::json::Value& req, int fallback) {
    const auto& fv = req.get("frames");
    if (fv.type == mods::json::Value::Number && fv.number > 0) return int(fv.number);
    return fallback;
}

std::string handle_status(int id) {
    mods::json::Value v;
    v.type = mods::json::Value::Object;
    v.object["id"] = id;
    v.object["ok"] = true;
    v.object["frame"] = double(render::frame_count());
    v.object["step"] = double(interp::logic_steps());
#ifndef _WIN32
    v.object["pid"] = double(getpid());
#else
    v.object["pid"] = double(GetCurrentProcessId());
#endif
    interp::PlayerSnapshot snap;
    if (interp::player_snapshot(snap, 0)) {
        mods::json::Value p;
        p.type = mods::json::Value::Object;
        p.object["x"] = snap.x;
        p.object["y"] = snap.y;
        p.object["z"] = snap.z;
        v.object["player"] = std::move(p);
    } else
        v.object["player"].type = mods::json::Value::Null;
    mods::lua_sdk::SceneReport scene;
    mods::lua_sdk::scene_report(scene);
    if (!scene.scene.empty()) v.object["scene"] = scene.scene;
    if (scene.map_known) v.object["map"] = scene.map;
    if (scene.course_known) v.object["course"] = scene.course;
    if (!scene.pack.empty()) v.object["pack"] = scene.pack;
    if (!scene.entering.empty()) v.object["entering"] = scene.entering;
    if (scene.file_known) v.object["file"] = scene.file;
    if (scene.cp_known) v.object["cp"] = scene.cp;
    if (scene.node_known) v.object["node"] = scene.node;
    if (scene.world_known) v.object["world"] = scene.world;
    if (scene.area_known) v.object["area"] = scene.area;
    if (!scene.node_name.empty()) v.object["node_name"] = scene.node_name;
    if (scene.node_pos_known) {
        v.object["node_x"] = (double)scene.node_x;
        v.object["node_y"] = (double)scene.node_y;
    }
    if (scene.map_player_known) {
        v.object["map_x"] = (double)scene.map_x;
        v.object["map_y"] = (double)scene.map_y;
    }
    if (scene.map_lock_known) v.object["map_lock"] = scene.map_lock;
    if (scene.map_title_known) v.object["map_title"] = scene.map_title;
    if (scene.map_players_known) v.object["map_players"] = scene.map_players;
    return mods::json::dump(v);
}

std::string handle_ui(int id) {
    mods::json::Value v;
    v.type = mods::json::Value::Object;
    v.object["id"] = id;
    v.object["ok"] = true;
    v.object["overlay"] = overlay::is_open();
    const char* tab = overlay::settings_tab();
    if (tab && tab[0]) v.object["tab"] = tab;
    map_menu::MenuProbe menu;
    map_menu::probe(menu);
    v.object["menu"] = menu.open;
    if (menu.open) v.object["cursor"] = menu.cursor;
    mods::json::Value buttons;
    buttons.type = mods::json::Value::Array;
    for (int i = 0; i < menu.count; i++) {
        const map_menu::ButtonProbe& b = menu.buttons[i];
        mods::json::Value row;
        row.type = mods::json::Value::Object;
        row.object["index"] = b.index;
        row.object["name"] = b.name;
        row.object["selected"] = b.selected;
        if (b.rect) {
            row.object["x0"] = (double)b.x0;
            row.object["y0"] = (double)b.y0;
            row.object["x1"] = (double)b.x1;
            row.object["y1"] = (double)b.y1;
        }
        buttons.array.push_back(std::move(row));
    }
    v.object["buttons"] = std::move(buttons);
    return mods::json::dump(v);
}

std::string handle_actors(int id, int limit, float radius) {
    mods::lua_sdk::ActorProbe probe;
    std::string err;
    if (!mods::lua_sdk::probe_actors(limit, radius, probe, err))
        return response_err(id, err.empty() ? "actors failed" : err);
    mods::json::Value v;
    v.type = mods::json::Value::Object;
    v.object["id"] = id;
    v.object["ok"] = true;
    if (probe.power_known) {
        v.object["power"] = probe.power;
        if (probe.power_name[0]) v.object["power_name"] = probe.power_name;
    }
    mods::json::Value rows;
    rows.type = mods::json::Value::Array;
    for (int i = 0; i < probe.count; i++) {
        const mods::lua_sdk::ActorRow& a = probe.rows[i];
        mods::json::Value row;
        row.type = mods::json::Value::Object;
        row.object["profile"] = (double)a.profile;
        row.object["x"] = (double)a.x;
        row.object["y"] = (double)a.y;
        row.object["z"] = (double)a.z;
        row.object["vx"] = (double)a.vx;
        row.object["vy"] = (double)a.vy;
        row.object["vz"] = (double)a.vz;
        rows.array.push_back(std::move(row));
    }
    v.object["actors"] = std::move(rows);
    return mods::json::dump(v);
}

std::string handle_log(int id, const std::string& tag) {
    std::vector<std::string> lines;
    log_ring_copy(lines);
    std::vector<std::string> tags;
    std::string cur;
    for (size_t i = 0; i <= tag.size(); i++) {
        if (i == tag.size() || tag[i] == ',') {
            if (!cur.empty()) tags.push_back("[" + cur + "]");
            cur.clear();
        } else if (tag[i] != ' ')
            cur.push_back(tag[i]);
    }
    mods::json::Value v;
    v.type = mods::json::Value::Object;
    v.object["id"] = id;
    v.object["ok"] = true;
    mods::json::Value out;
    out.type = mods::json::Value::Array;
    for (const std::string& line : lines) {
        if (!tags.empty()) {
            bool hit = false;
            for (const std::string& t : tags)
                if (line.find(t) != std::string::npos) hit = true;
            if (!hit) continue;
        }
        out.array.push_back(line);
    }
    v.object["lines"] = std::move(out);
    return mods::json::dump(v);
}

void apply_press(uint32_t bits, int frames, bool tap) {
    std::lock_guard<std::mutex> lk(g_mu);
    g_pad.buttons = bits;
    g_pad.stick = false;
    g_pad.rstick = false;
    if (tap) {
        g_pad.samples = 1;
        g_pad.until = 0;
    } else {
        g_pad.samples = 0;
        g_pad.until = interp::logic_steps() + uint64_t(frames);
    }
}

void apply_stick(float x, float y, int frames, bool right) {
    const uint64_t until = interp::logic_steps() + uint64_t(frames);
    std::lock_guard<std::mutex> lk(g_mu);
    if (right) {
        g_pad.rx = x;
        g_pad.ry = y;
        g_pad.rstick = true;
    } else {
        g_pad.lx = x;
        g_pad.ly = y;
        g_pad.stick = true;
    }
    g_pad.samples = 0;
    g_pad.until = until;
}

void handle_request(const mods::json::Value& req) {
    const int id = request_id(req);
    if (id < 0) return;
    const std::string cmd = req.get("cmd").string();
    if (cmd == "status") {
        std::lock_guard<std::mutex> lk(g_mu);
        g_jobs.push_back(GuestJob{id, GuestOp::Status, {}, 0});
        return;
    }
    if (cmd == "leave") {
        std::lock_guard<std::mutex> lk(g_mu);
        g_jobs.push_back(GuestJob{id, GuestOp::Leave, {}, 0});
        return;
    }
    if (cmd == "press") {
        const auto& bv = req.get("bits");
        if (bv.type != mods::json::Value::Number) {
            queue_line(response_err(id, "missing bits"));
            return;
        }
        const auto& tapv = req.get("tap");
        const bool tap = tapv.type == mods::json::Value::Bool && tapv.boolean;
        apply_press(uint32_t(bv.number), request_frames(req, 20), tap);
        queue_line(response_ok(id));
        return;
    }
    if (cmd == "stick") {
        apply_stick(float(req.get("x").number), float(req.get("y").number), request_frames(req, 20), false);
        queue_line(response_ok(id));
        return;
    }
    if (cmd == "rstick") {
        apply_stick(float(req.get("x").number), float(req.get("y").number), request_frames(req, 20), true);
        queue_line(response_ok(id));
        return;
    }
    if (cmd == "screenshot") {
        std::string path = req.get("path").string();
        if (path.empty()) {
            queue_line(response_err(id, "missing path"));
            return;
        }
        const bool drc = req.get("drc").type == mods::json::Value::Bool ? req.get("drc").boolean
                         : req.get("drc").type == mods::json::Value::Number && req.get("drc").number != 0;
        int ahead = 2;
        if (req.get("ahead").type == mods::json::Value::Number) ahead = int(req.get("ahead").number);
        if (ahead < 0) ahead = 0;
        std::error_code ec;
        std::filesystem::create_directories(std::filesystem::path(path).parent_path(), ec);
        {
            std::lock_guard<std::mutex> lk(g_mu);
            g_shots.push_back(ShotWait{id, path, std::chrono::steady_clock::now(), false, drc, ahead});
        }
        return;
    }
    if (cmd == "picture") {
        const auto& paths = req.get("paths");
        if (paths.type != mods::json::Value::Array || paths.array.empty() || paths.array.size() > 8) {
            queue_line(response_err(id, "missing paths"));
            return;
        }
        PictureWait wait;
        wait.id = id;
        wait.started = std::chrono::steady_clock::now();
        const auto& aheads = req.get("ahead");
        for (size_t i = 0; i < paths.array.size(); i++) {
            if (paths.array[i].type != mods::json::Value::String || paths.array[i].text.empty()) {
                queue_line(response_err(id, "missing paths"));
                return;
            }
            std::error_code ec;
            std::filesystem::create_directories(std::filesystem::path(paths.array[i].text).parent_path(), ec);
            wait.paths.push_back(paths.array[i].text);
            int ahead = 2 + int(i);
            if (aheads.type == mods::json::Value::Array && i < aheads.array.size() &&
                aheads.array[i].type == mods::json::Value::Number)
                ahead = int(aheads.array[i].number);
            if (ahead < 0) ahead = 0;
            wait.ahead.push_back(ahead);
        }
        std::lock_guard<std::mutex> lk(g_mu);
        g_pictures.push_back(std::move(wait));
        return;
    }
    if (cmd == "log") {
        queue_line(handle_log(id, req.get("tag").string()));
        return;
    }
    if (cmd == "goto") {
        const std::string place = req.get("place").string();
        if (place.empty()) {
            queue_line(response_err(id, "missing place"));
            return;
        }
        std::lock_guard<std::mutex> lk(g_mu);
        g_jobs.push_back(GuestJob{id, GuestOp::Goto, place, 0});
        return;
    }
    if (cmd == "ui") {
        std::lock_guard<std::mutex> lk(g_mu);
        g_jobs.push_back(GuestJob{id, GuestOp::Ui, {}, 0});
        return;
    }
    if (cmd == "actors") {
        GuestJob job;
        job.id = id;
        job.op = GuestOp::Actors;
        if (req.get("limit").type == mods::json::Value::Number) job.limit = int(req.get("limit").number);
        if (req.get("radius").type == mods::json::Value::Number) job.radius = float(req.get("radius").number);
        std::lock_guard<std::mutex> lk(g_mu);
        g_jobs.push_back(std::move(job));
        return;
    }
    if (cmd == "eval") {
        const auto& cv = req.get("code");
        if (cv.type != mods::json::Value::String) {
            queue_line(response_err(id, "missing code"));
            return;
        }
        int timeout_ms = 5000;
        const auto& tv = req.get("timeout");
        if (tv.type == mods::json::Value::Number && tv.number > 0) timeout_ms = int(tv.number);
        std::lock_guard<std::mutex> lk(g_mu);
        g_jobs.push_back(GuestJob{id, GuestOp::Eval, cv.text, timeout_ms});
        return;
    }
    if (cmd == "quit") {
        g_quit.store(true);
        queue_line(response_ok(id));
        return;
    }
    queue_line(response_err(id, "unknown cmd"));
}

void dispatch_line(const std::string& line) {
    if (line.empty()) return;
    try {
        handle_request(mods::json::parse(line));
    } catch (const std::exception& e) {
        queue_line(response_err(0, e.what()));
    }
}

#ifndef _WIN32
void flush_writes() {
    std::deque<std::string> lines;
    {
        std::lock_guard<std::mutex> lk(g_mu);
        lines.swap(g_out_lines);
    }
    if (g_client < 0 || lines.empty()) return;
    std::lock_guard<std::mutex> write_lk(g_write_mu);
    for (const auto& line : lines) {
        std::string out = line;
        out += '\n';
        size_t off = 0;
        while (off < out.size()) {
            ssize_t n = write(g_client, out.data() + off, out.size() - off);
            if (n <= 0) return;
            off += size_t(n);
        }
    }
}

void close_client() {
    if (g_client >= 0) {
        close(g_client);
        g_client = -1;
    }
}

void serve_client(int fd) {
    g_client = fd;
    std::string buf;
    char chunk[4096];
    for (;;) {
        flush_writes();
        ssize_t n = read(fd, chunk, sizeof chunk);
        if (n <= 0) break;
        buf.append(chunk, chunk + n);
        for (;;) {
            auto pos = buf.find('\n');
            if (pos == std::string::npos) break;
            std::string line = buf.substr(0, pos);
            buf.erase(0, pos + 1);
            dispatch_line(line);
            flush_writes();
        }
    }
    close_client();
}

void listen_loop(const std::string& path) {
    g_listen = socket(AF_UNIX, SOCK_STREAM, 0);
    if (g_listen < 0) {
        LOG("[agent] socket: %s", strerror(errno));
        return;
    }
    sockaddr_un addr{};
    addr.sun_family = AF_UNIX;
    if (path.size() >= sizeof addr.sun_path) {
        LOG("[agent] socket path too long");
        close(g_listen);
        g_listen = -1;
        return;
    }
    std::memcpy(addr.sun_path, path.c_str(), path.size() + 1);
    unlink(path.c_str());
    if (bind(g_listen, reinterpret_cast<sockaddr*>(&addr), sizeof addr) < 0) {
        LOG("[agent] bind %s: %s", path.c_str(), strerror(errno));
        close(g_listen);
        g_listen = -1;
        return;
    }
    chmod(path.c_str(), 0666);
    if (listen(g_listen, 4) < 0) {
        LOG("[agent] listen: %s", strerror(errno));
        close(g_listen);
        g_listen = -1;
        unlink(path.c_str());
        return;
    }
    LOG("[agent] listening on %s", path.c_str());
    while (!g_stop) {
        fd_set fds;
        FD_ZERO(&fds);
        FD_SET(g_listen, &fds);
        timeval tv{0, 200000};
        int r = select(g_listen + 1, &fds, nullptr, nullptr, &tv);
        if (r < 0) {
            if (errno == EINTR) continue;
            break;
        }
        if (r == 0) {
            flush_writes();
            continue;
        }
        int fd = accept(g_listen, nullptr, nullptr);
        if (fd < 0) continue;
        close_client();
        serve_client(fd);
    }
    close_client();
    if (g_listen >= 0) {
        close(g_listen);
        g_listen = -1;
    }
    unlink(path.c_str());
}
#else
void listen_loop(const std::string&) {
    if (!g_win_logged) {
        g_win_logged = true;
        LOG("[agent] named pipe control is not implemented; set NSMBU_AGENT_SOCK only on Unix hosts");
    }
}
#endif

void thread_main(std::string path) {
#ifndef _WIN32
    listen_loop(path);
#else
    listen_loop(path);
#endif
}

void expire_pad(uint64_t frame) {
    if (g_pad.until && g_pad.until <= frame) g_pad = {};
}

}

void init() {
    const char* sock = env::AGENT_SOCK();
    if (!sock || !*sock) return;
#ifdef NDEBUG
    LOG("[agent] control socket is disabled in release builds");
    return;
#else
    std::string path = sock;
    std::error_code ec;
    std::filesystem::create_directories(std::filesystem::path(path).parent_path(), ec);
    g_active.store(true);
    g_stop = false;
    LOG("[agent] control enabled (%s)", path.c_str());
    g_thread = std::thread([path] { thread_main(path); });
#endif
}

void shutdown() {
    if (!g_active.load()) return;
    g_stop = true;
#ifndef _WIN32
    if (g_listen >= 0) ::shutdown(g_listen, SHUT_RDWR);
#endif
    if (g_thread.joinable()) g_thread.join();
    g_active.store(false);
}

void service() {
    if (!g_active.load()) return;
#ifndef _WIN32
    flush_writes();
#endif
    {
        std::lock_guard<std::mutex> lk(g_mu);
        expire_pad(interp::logic_steps());
    }
    if (g_quit.exchange(false)) {
#ifdef NSMBU_SDL_HOST
        hostui::post([] {
            SDL_Event ev{};
            ev.type = SDL_EVENT_QUIT;
            SDL_PushEvent(&ev);
        });
#else
        std::_Exit(0);
#endif
    }
    std::vector<std::tuple<int, std::string, bool, int>> to_request;
    std::vector<PictureWait> pictures;
    {
        std::lock_guard<std::mutex> lk(g_mu);
        for (auto& s : g_shots)
            if (!s.requested) {
                s.requested = true;
                to_request.emplace_back(s.id, s.path, s.drc, s.ahead);
            }
        for (auto& p : g_pictures)
            if (!p.requested) {
                p.requested = true;
                pictures.push_back(p);
            }
    }
    for (const auto& shot : to_request) {
        const std::string path = std::get<1>(shot);
        const bool drc = std::get<2>(shot);
        const int ahead = std::get<3>(shot);
        hostui::post([path, drc, ahead] {
#ifdef NSMBU_VULKAN
            if (drc) gfxvk::request_drc_dump(path, ahead);
            else
#endif
                render::request_tv_dump(path, ahead);
        });
    }
    for (const auto& picture : pictures) {
        const std::vector<std::string> paths = picture.paths;
        const std::vector<int> ahead = picture.ahead;
        hostui::post([paths, ahead] {
            for (size_t i = 0; i < paths.size() && i < ahead.size(); i++)
                render::request_tv_dump(paths[i], ahead[i]);
        });
    }
    constexpr auto kTimeout = std::chrono::seconds(30);
    {
        std::lock_guard<std::mutex> lk(g_mu);
        for (auto it = g_shots.begin(); it != g_shots.end();) {
            std::error_code ec;
            if (std::filesystem::exists(it->path, ec)) {
                mods::json::Value v;
                v.type = mods::json::Value::Object;
                v.object["id"] = it->id;
                v.object["ok"] = true;
                v.object["path"] = it->path;
                g_out_lines.push_back(mods::json::dump(v));
                it = g_shots.erase(it);
            } else if (std::chrono::steady_clock::now() - it->started > kTimeout) {
                g_out_lines.push_back(response_err(it->id, "screenshot timeout"));
                it = g_shots.erase(it);
            } else
                ++it;
        }
        for (auto it = g_pictures.begin(); it != g_pictures.end();) {
            if (!it->requested) {
                ++it;
                continue;
            }
            std::error_code ec;
            bool ready = true;
            for (const std::string& path : it->paths) {
                if (!std::filesystem::exists(path, ec)) ready = false;
            }
            if (ready) {
                mods::json::Value v;
                v.type = mods::json::Value::Object;
                v.object["id"] = it->id;
                v.object["ok"] = true;
                mods::json::Value paths;
                paths.type = mods::json::Value::Array;
                for (const std::string& path : it->paths) paths.array.push_back(path);
                v.object["paths"] = std::move(paths);
                g_out_lines.push_back(mods::json::dump(v));
                it = g_pictures.erase(it);
            } else if (std::chrono::steady_clock::now() - it->started > kTimeout) {
                g_out_lines.push_back(response_err(it->id, "picture timeout"));
                it = g_pictures.erase(it);
            } else
                ++it;
        }
    }
#ifndef _WIN32
    flush_writes();
#endif
}

namespace {

bool observable(GuestOp op) {
    return op == GuestOp::Status || op == GuestOp::Ui || op == GuestOp::Actors;
}

bool take_job(GuestJob& job, bool observe_only) {
    std::lock_guard<std::mutex> lk(g_mu);
    for (auto it = g_jobs.begin(); it != g_jobs.end(); ++it) {
        if (!observable(it->op)) continue;
        job = std::move(*it);
        g_jobs.erase(it);
        return true;
    }
    if (observe_only || g_jobs.empty()) return false;
    job = std::move(g_jobs.front());
    g_jobs.pop_front();
    return true;
}

void run_eval(const GuestJob& job) {
    mods::lua_eval::Result result;
    try {
        result = mods::lua_eval::run(job.code, job.timeout_ms);
    } catch (const std::exception& e) {
        result.ok = false;
        result.output = e.what();
    }
    mods::json::Value v;
    v.type = mods::json::Value::Object;
    v.object["id"] = job.id;
    v.object["ok"] = result.ok;
    if (result.ok)
        v.object["output"] = result.output;
    else
        v.object["error"] = result.output;
    queue_line(mods::json::dump(v));
}

void run_leave(const GuestJob& job) {
    std::string err;
    if (!mods::lua_sdk::leave_course(err)) {
        queue_line(response_err(job.id, err.empty() ? "leave failed" : err));
        return;
    }
    queue_line(response_ok(job.id));
}

void run_goto(const GuestJob& job) {
    std::string err;
    if (!mods::lua_sdk::open_place(job.code, err)) {
        queue_line(response_err(job.id, err.empty() ? "goto failed" : err));
        return;
    }
    queue_line(response_ok(job.id));
}

}

void guest_service() {
    if (!g_active.load() || g_servicing) return;
    ServiceGuard guard;
    for (;;) {
        GuestJob job;
        if (!take_job(job, false)) return;
        if (job.op == GuestOp::Status)
            queue_line(handle_status(job.id));
        else if (job.op == GuestOp::Ui)
            queue_line(handle_ui(job.id));
        else if (job.op == GuestOp::Actors)
            queue_line(handle_actors(job.id, job.limit, job.radius));
        else if (job.op == GuestOp::Leave)
            run_leave(job);
        else if (job.op == GuestOp::Goto)
            run_goto(job);
        else
            run_eval(job);
    }
}

void guest_observe() {
    if (!g_active.load() || g_servicing) return;
    ServiceGuard guard;
    for (;;) {
        GuestJob job;
        if (!take_job(job, true)) return;
        if (job.op == GuestOp::Ui)
            queue_line(handle_ui(job.id));
        else if (job.op == GuestOp::Actors)
            queue_line(handle_actors(job.id, job.limit, job.radius));
        else
            queue_line(handle_status(job.id));
    }
}

void apply_pad(input::PadState& pad) {
    if (!g_active.load()) return;
    const uint64_t step = interp::logic_steps();
    std::lock_guard<std::mutex> lk(g_mu);
    if (g_pad.samples > 0) {
        if (interp::hold_pass()) return;
        pad.buttons |= g_pad.buttons;
        if (g_pad.stick) {
            pad.lx = g_pad.lx;
            pad.ly = g_pad.ly;
        }
        if (g_pad.rstick) {
            pad.rx = g_pad.rx;
            pad.ry = g_pad.ry;
        }
        g_pad.samples--;
        if (g_pad.samples <= 0) g_pad = {};
        return;
    }
    expire_pad(step);
    if (!g_pad.until || g_pad.until <= step) return;
    pad.buttons |= g_pad.buttons;
    if (g_pad.stick) {
        pad.lx = g_pad.lx;
        pad.ly = g_pad.ly;
    }
    if (g_pad.rstick) {
        pad.rx = g_pad.rx;
        pad.ry = g_pad.ry;
    }
}

bool active() { return g_active.load(); }

}
