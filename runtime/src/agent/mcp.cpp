#include "agent/mcp.h"

#include "agent/agent_ctl.h"
#include "core/runtime.h"
#include "mods/mod_json.h"

#include <atomic>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <string>
#include <system_error>
#include <thread>
#include <vector>

#ifndef _WIN32
#include <arpa/inet.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <unistd.h>
#endif

namespace agent_mcp {
#ifndef NDEBUG
namespace {

std::atomic<bool> g_stop{false};
std::thread g_thread;
int g_listen = -1;
int g_port = 0;

constexpr int kPort0 = 47616;

std::string b64(const std::string& raw) {
    static const char* tbl = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
    std::string out;
    out.reserve((raw.size() + 2) / 3 * 4);
    for (size_t i = 0; i < raw.size(); i += 3) {
        unsigned n = unsigned(uint8_t(raw[i])) << 16;
        if (i + 1 < raw.size()) n |= unsigned(uint8_t(raw[i + 1])) << 8;
        if (i + 2 < raw.size()) n |= unsigned(uint8_t(raw[i + 2]));
        out.push_back(tbl[(n >> 18) & 63]);
        out.push_back(tbl[(n >> 12) & 63]);
        out.push_back(i + 1 < raw.size() ? tbl[(n >> 6) & 63] : '=');
        out.push_back(i + 2 < raw.size() ? tbl[n & 63] : '=');
    }
    return out;
}

uint32_t button_bits(const std::string& token, std::string& err) {
    struct Item { const char* name; uint32_t bits; };
    static const Item items[] = {
        {"A", 0x8000}, {"B", 0x4000}, {"X", 0x2000}, {"Y", 0x1000},
        {"L", 0x0020}, {"R", 0x0010}, {"ZL", 0x0080}, {"ZR", 0x0040},
        {"Plus", 0x0008}, {"Minus", 0x0004}, {"Home", 0x0002},
        {"Up", 0x0200}, {"Down", 0x0100}, {"Left", 0x0800}, {"Right", 0x0400},
    };
    for (const Item& item : items)
        if (token == item.name) return item.bits;
    char* end = nullptr;
    unsigned long n = std::strtoul(token.c_str(), &end, 16);
    if (end == token.c_str() || *end) {
        err = "unknown button: " + token;
        return 0;
    }
    return uint32_t(n);
}

int next_id() {
    static std::atomic<int> id{1};
    return id.fetch_add(1);
}

mods::json::Value call_game(mods::json::Value req, int timeout_ms) {
    req.object["id"] = next_id();
    const std::string line = agent_ctl::exchange(mods::json::dump(req), timeout_ms);
    try {
        return mods::json::parse(line);
    } catch (const std::exception& e) {
        mods::json::Value v;
        v.type = mods::json::Value::Object;
        v.object["ok"] = false;
        v.object["error"] = e.what();
        return v;
    }
}

std::string status_line(const mods::json::Value& v) {
    std::string s = "frame=" + std::to_string(int(v.get("frame").number));
    s += " step=" + std::to_string(int(v.get("step").number));
    if (v.get("scene").type == mods::json::Value::String) s += " scene=" + v.get("scene").text;
    if (v.get("course").type == mods::json::Value::Bool) s += std::string(" course=") + (v.get("course").boolean ? "1" : "0");
    if (v.get("map").type == mods::json::Value::Bool) s += std::string(" map=") + (v.get("map").boolean ? "1" : "0");
    if (v.get("pack").type == mods::json::Value::String) s += " pack=" + v.get("pack").text;
    if (v.get("entering").type == mods::json::Value::String) s += " entering=" + v.get("entering").text;
    if (v.get("player").type == mods::json::Value::Object) {
        const auto& p = v.get("player");
        s += " player=(" + std::to_string(p.get("x").number) + ", " + std::to_string(p.get("y").number) + ", " +
             std::to_string(p.get("z").number) + ")";
    } else
        s += " player=none";
    return s;
}

mods::json::Value tool_result(const std::string& text, bool error) {
    mods::json::Value content;
    content.type = mods::json::Value::Array;
    mods::json::Value item;
    item.type = mods::json::Value::Object;
    item.object["type"] = "text";
    item.object["text"] = text;
    content.array.push_back(std::move(item));
    mods::json::Value out;
    out.type = mods::json::Value::Object;
    out.object["content"] = std::move(content);
    out.object["isError"] = error;
    return out;
}

mods::json::Value tool_image(const std::string& path, const std::string& png) {
    mods::json::Value content;
    content.type = mods::json::Value::Array;
    mods::json::Value text;
    text.type = mods::json::Value::Object;
    text.object["type"] = "text";
    text.object["text"] = path;
    content.array.push_back(std::move(text));
    mods::json::Value image;
    image.type = mods::json::Value::Object;
    image.object["type"] = "image";
    image.object["data"] = b64(png);
    image.object["mimeType"] = "image/png";
    content.array.push_back(std::move(image));
    mods::json::Value out;
    out.type = mods::json::Value::Object;
    out.object["content"] = std::move(content);
    out.object["isError"] = false;
    return out;
}

mods::json::Value run_tool(const std::string& name, const mods::json::Value& args) {
    mods::json::Value req;
    req.type = mods::json::Value::Object;
    if (name == "status") {
        req.object["cmd"] = "status";
        const mods::json::Value reply = call_game(std::move(req), 10000);
        if (reply.get("ok").type == mods::json::Value::Bool && !reply.get("ok").boolean)
            return tool_result(reply.get("error").string("status failed"), true);
        return tool_result(status_line(reply), false);
    }
    if (name == "press") {
        std::string err;
        const uint32_t bits = button_bits(args.get("button").string(), err);
        if (!err.empty()) return tool_result(err, true);
        req.object["cmd"] = "press";
        req.object["bits"] = double(bits);
        if (args.get("frames").type == mods::json::Value::Number) req.object["frames"] = args.get("frames").number;
        const mods::json::Value reply = call_game(std::move(req), 5000);
        if (reply.get("ok").type == mods::json::Value::Bool && !reply.get("ok").boolean)
            return tool_result(reply.get("error").string("press failed"), true);
        return tool_result("ok", false);
    }
    if (name == "stick" || name == "rstick") {
        req.object["cmd"] = name;
        req.object["x"] = args.get("x").number;
        req.object["y"] = args.get("y").number;
        if (args.get("frames").type == mods::json::Value::Number) req.object["frames"] = args.get("frames").number;
        const mods::json::Value reply = call_game(std::move(req), 5000);
        if (reply.get("ok").type == mods::json::Value::Bool && !reply.get("ok").boolean)
            return tool_result(reply.get("error").string("stick failed"), true);
        return tool_result("ok", false);
    }
    if (name == "screenshot") {
        std::string path = args.get("path").string();
        if (path.empty()) path = ".tmp/mcp-shot.png";
        req.object["cmd"] = "screenshot";
        req.object["path"] = path;
        const mods::json::Value reply = call_game(std::move(req), 30000);
        if (reply.get("ok").type == mods::json::Value::Bool && !reply.get("ok").boolean)
            return tool_result(reply.get("error").string("screenshot failed"), true);
        std::ifstream in(path, std::ios::binary);
        std::string png((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
        if (png.empty()) return tool_result("screenshot file is empty", true);
        return tool_image(path, png);
    }
    if (name == "log") {
        req.object["cmd"] = "log";
        if (args.get("tag").type == mods::json::Value::String) req.object["tag"] = args.get("tag").text;
        mods::json::Value reply = call_game(std::move(req), 5000);
        if (reply.get("ok").type == mods::json::Value::Bool && !reply.get("ok").boolean)
            return tool_result(reply.get("error").string("log failed"), true);
        int limit = 40;
        if (args.get("lines").type == mods::json::Value::Number && args.get("lines").number > 0)
            limit = int(args.get("lines").number);
        const auto& lines = reply.get("lines");
        std::string text;
        if (lines.type == mods::json::Value::Array) {
            const size_t begin = lines.array.size() > size_t(limit) ? lines.array.size() - size_t(limit) : 0;
            for (size_t i = begin; i < lines.array.size(); i++) {
                text += lines.array[i].string();
                text += '\n';
            }
        }
        return tool_result(text, false);
    }
    if (name == "leave" || name == "ui" || name == "session_close") {
        req.object["cmd"] = name == "session_close" ? "quit" : name;
        const mods::json::Value reply = call_game(std::move(req), 10000);
        if (reply.get("ok").type == mods::json::Value::Bool && !reply.get("ok").boolean)
            return tool_result(reply.get("error").string("failed"), true);
        return tool_result(mods::json::dump(reply), false);
    }
    if (name == "goto") {
        const std::string place = args.get("target").string();
        if (place.empty()) return tool_result("missing target", true);
        req.object["cmd"] = "goto";
        req.object["place"] = place;
        const mods::json::Value reply = call_game(std::move(req), 10000);
        if (reply.get("ok").type == mods::json::Value::Bool && !reply.get("ok").boolean)
            return tool_result(reply.get("error").string("goto failed"), true);
        return tool_result("ok", false);
    }
    if (name == "actors") {
        req.object["cmd"] = "actors";
        if (args.get("limit").type == mods::json::Value::Number) req.object["limit"] = args.get("limit").number;
        if (args.get("radius").type == mods::json::Value::Number) req.object["radius"] = args.get("radius").number;
        const mods::json::Value reply = call_game(std::move(req), 10000);
        if (reply.get("ok").type == mods::json::Value::Bool && !reply.get("ok").boolean)
            return tool_result(reply.get("error").string("actors failed"), true);
        return tool_result(mods::json::dump(reply), false);
    }
    if (name == "picture") {
        const auto& paths = args.get("paths");
        if (paths.type != mods::json::Value::Array || paths.array.empty())
            return tool_result("picture needs paths", true);
        req.object["cmd"] = "picture";
        req.object["paths"] = paths;
        const mods::json::Value reply = call_game(std::move(req), 30000);
        if (reply.get("ok").type == mods::json::Value::Bool && !reply.get("ok").boolean)
            return tool_result(reply.get("error").string("picture failed"), true);
        return tool_result(mods::json::dump(reply), false);
    }
    return tool_result("unknown tool", true);
}

mods::json::Value tool_list() {
    auto add = [](mods::json::Value& tools, const char* name, const char* desc) {
        mods::json::Value t;
        t.type = mods::json::Value::Object;
        t.object["name"] = name;
        t.object["description"] = desc;
        mods::json::Value schema;
        schema.type = mods::json::Value::Object;
        schema.object["type"] = "object";
        t.object["inputSchema"] = std::move(schema);
        tools.array.push_back(std::move(t));
    };
    mods::json::Value tools;
    tools.type = mods::json::Value::Array;
    add(tools, "status", "One line: frame, step, scene, course, map, pack, player.");
    add(tools, "press", "Hold a button. Arguments: button (name or hex), frames.");
    add(tools, "stick", "Hold the left stick. Arguments: x, y, frames.");
    add(tools, "rstick", "Hold the right stick. Arguments: x, y, frames.");
    add(tools, "screenshot", "Save the TV picture and return the PNG. Argument: path.");
    add(tools, "log", "Recent log lines. Arguments: tag, lines.");
    add(tools, "leave", "Leave the live course.");
    add(tools, "goto", "Open a place. Argument: target.");
    add(tools, "ui", "UI hit targets.");
    add(tools, "actors", "Nearby actors. Arguments: limit, radius.");
    add(tools, "picture", "Dump TV frames to paths. Argument: paths.");
    add(tools, "session_close", "Quit this game.");
    mods::json::Value result;
    result.type = mods::json::Value::Object;
    result.object["tools"] = std::move(tools);
    return result;
}

mods::json::Value rpc(const mods::json::Value& msg) {
    const std::string method = msg.get("method").string();
    const bool note = msg.get("id").type == mods::json::Value::Null && msg.object.find("id") == msg.object.end();
    if (note) {
        mods::json::Value empty;
        empty.type = mods::json::Value::Null;
        return empty;
    }
    mods::json::Value res;
    res.type = mods::json::Value::Object;
    res.object["jsonrpc"] = "2.0";
    if (msg.get("id").type != mods::json::Value::Null) res.object["id"] = msg.get("id");
    if (method == "initialize") {
        mods::json::Value result;
        result.type = mods::json::Value::Object;
        result.object["protocolVersion"] = "2025-03-26";
        mods::json::Value caps;
        caps.type = mods::json::Value::Object;
        mods::json::Value tools;
        tools.type = mods::json::Value::Object;
        caps.object["tools"] = std::move(tools);
        result.object["capabilities"] = std::move(caps);
        mods::json::Value info;
        info.type = mods::json::Value::Object;
        info.object["name"] = "nsmbu";
        info.object["version"] = "0";
        result.object["serverInfo"] = std::move(info);
        res.object["result"] = std::move(result);
        return res;
    }
    if (method == "tools/list") {
        res.object["result"] = tool_list();
        return res;
    }
    if (method == "tools/call") {
        const auto& params = msg.get("params");
        const std::string name = params.get("name").string();
        const auto& args = params.get("arguments");
        res.object["result"] = run_tool(name, args.type == mods::json::Value::Object ? args : mods::json::Value{});
        return res;
    }
    mods::json::Value err;
    err.type = mods::json::Value::Object;
    err.object["code"] = -32601;
    err.object["message"] = "method not found";
    res.object["error"] = std::move(err);
    return res;
}

std::string http_response(int status, const std::string& body) {
    const char* text = status == 200 ? "OK" : status == 401 ? "Unauthorized" : status == 403 ? "Forbidden" : "Bad Request";
    return "HTTP/1.1 " + std::to_string(status) + " " + text +
           "\r\nContent-Type: application/json\r\nContent-Length: " + std::to_string(body.size()) +
           "\r\nConnection: close\r\n\r\n" + body;
}

bool host_ok(const std::string& host) {
    return host.rfind("127.0.0.1", 0) == 0 || host.rfind("localhost", 0) == 0 || host.rfind("[::1]", 0) == 0;
}

#ifndef _WIN32
void serve(int fd) {
    std::string buf;
    char chunk[4096];
    while (buf.find("\r\n\r\n") == std::string::npos && buf.size() < 16384) {
        ssize_t n = ::read(fd, chunk, sizeof chunk);
        if (n <= 0) {
            ::close(fd);
            return;
        }
        buf.append(chunk, chunk + n);
    }
    const auto hdr_end = buf.find("\r\n\r\n");
    if (hdr_end == std::string::npos) {
        ::close(fd);
        return;
    }
    const std::string head = buf.substr(0, hdr_end);
    std::string body = buf.substr(hdr_end + 4);
    size_t length = 0;
    std::string host;
    std::string method, target;
    {
        const auto line_end = head.find("\r\n");
        const std::string req = head.substr(0, line_end);
        const auto sp1 = req.find(' ');
        const auto sp2 = sp1 == std::string::npos ? std::string::npos : req.find(' ', sp1 + 1);
        if (sp1 != std::string::npos) {
            method = req.substr(0, sp1);
            target = sp2 == std::string::npos ? req.substr(sp1 + 1) : req.substr(sp1 + 1, sp2 - sp1 - 1);
        }
    }
    size_t pos = 0;
    while (pos < head.size()) {
        auto nl = head.find("\r\n", pos);
        if (nl == std::string::npos) nl = head.size();
        const std::string line = head.substr(pos, nl - pos);
        const auto colon = line.find(':');
        if (colon != std::string::npos) {
            std::string key = line.substr(0, colon);
            std::string val = line.substr(colon + 1);
            while (!val.empty() && val[0] == ' ') val.erase(val.begin());
            for (char& c : key) if (c >= 'A' && c <= 'Z') c = char(c - 'A' + 'a');
            if (key == "content-length") length = size_t(std::strtoul(val.c_str(), nullptr, 10));
            else if (key == "host") host = val;
        }
        if (nl == head.size()) break;
        pos = nl + 2;
    }
    if (length > (1 << 20)) length = 0;
    while (body.size() < length) {
        ssize_t n = ::read(fd, chunk, sizeof chunk);
        if (n <= 0) break;
        body.append(chunk, chunk + n);
    }
    body.resize(std::min(body.size(), length));
    std::string out;
    if (method != "POST" || target != "/mcp")
        out = http_response(404, "{\"error\":\"not found\"}");
    else if (!host_ok(host))
        out = http_response(403, "{\"error\":\"host\"}");
    else {
        try {
            const mods::json::Value msg = mods::json::parse(body);
            const bool note = msg.object.find("id") == msg.object.end();
            if (note && msg.get("method").string().rfind("notifications/", 0) == 0)
                out = http_response(202, "");
            else
                out = http_response(200, mods::json::dump(rpc(msg)));
        } catch (const std::exception& e) {
            out = http_response(400, std::string("{\"error\":\"") + e.what() + "\"}");
        }
    }
    size_t off = 0;
    while (off < out.size()) {
        ssize_t n = ::write(fd, out.data() + off, out.size() - off);
        if (n <= 0) break;
        off += size_t(n);
    }
    ::close(fd);
}

#endif

void thread_main() {
#ifndef _WIN32
    g_listen = ::socket(AF_INET, SOCK_STREAM, 0);
    if (g_listen < 0) {
        LOG("[mcp] socket failed");
        return;
    }
    int opt = 1;
    setsockopt(g_listen, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof opt);
    sockaddr_in addr{};
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    g_port = 0;
    for (int port = kPort0; port < kPort0 + 10; port++) {
        addr.sin_port = htons(uint16_t(port));
        if (::bind(g_listen, reinterpret_cast<sockaddr*>(&addr), sizeof addr) == 0) {
            g_port = port;
            break;
        }
    }
    if (!g_port || ::listen(g_listen, 4) < 0) {
        LOG("[mcp] bind failed");
        ::close(g_listen);
        g_listen = -1;
        return;
    }
    std::error_code ec;
    std::filesystem::create_directories(".tmp", ec);
    {
        std::ofstream url(".tmp/mcp.url", std::ios::trunc);
        url << "http://127.0.0.1:" << g_port << "/mcp\n";
    }
    LOG("[mcp] http://127.0.0.1:%d/mcp", g_port);
    while (!g_stop.load()) {
        fd_set fds;
        FD_ZERO(&fds);
        FD_SET(g_listen, &fds);
        timeval tv{0, 200000};
        int r = ::select(g_listen + 1, &fds, nullptr, nullptr, &tv);
        if (r <= 0) continue;
        int fd = ::accept(g_listen, nullptr, nullptr);
        if (fd < 0) continue;
        serve(fd);
    }
    ::close(g_listen);
    g_listen = -1;
#else
    LOG("[mcp] --mcp is not implemented on Windows");
#endif
}

}

void start() {
    if (g_thread.joinable()) return;
    g_stop.store(false);
    g_thread = std::thread(thread_main);
}

void stop() {
    g_stop.store(true);
#ifndef _WIN32
    if (g_listen >= 0) ::shutdown(g_listen, SHUT_RDWR);
#endif
    if (g_thread.joinable()) g_thread.join();
}

#else
void start() {}
void stop() {}
#endif

}
