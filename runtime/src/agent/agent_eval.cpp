#include "agent/agent_eval.h"

#include <cmath>
#include <cstdint>
#include <cstring>
#include <string>
#include <utility>
#include <vector>

namespace agent_eval {
namespace {

struct CallEntry {
    std::string name;
    std::string help;
    bool observe = false;
    mods::json::Value (*fn)(const mods::json::Value& args) = nullptr;
};

struct ReadEntry {
    std::string name;
    std::string help;
    Type type = Type::U8;
    int count = 0;
    const void* (*addr)() = nullptr;
};

std::vector<CallEntry> g_calls;
std::vector<ReadEntry> g_reads;

bool name_taken(const std::string& name) {
    for (const CallEntry& call : g_calls)
        if (call.name == name) return true;
    for (const ReadEntry& read : g_reads)
        if (read.name == name) return true;
    return false;
}

int elem_size(Type type) {
    switch (type) {
    case Type::Bool:
    case Type::U8:
    case Type::Bytes:
    case Type::CStr:
        return 1;
    case Type::U16:
        return 2;
    case Type::U32:
    case Type::S32:
    case Type::F32:
        return 4;
    case Type::U64:
    case Type::S64:
    case Type::F64:
        return 8;
    }
    return 0;
}

bool copy_size(Type type, int count, int& bytes) {
    if (count < 1) return false;
    const int elem = elem_size(type);
    if (elem < 1) return false;
    if (type == Type::Bytes || type == Type::CStr) {
        bytes = count;
    } else {
        if (count > 4096 / elem) return false;
        bytes = count * elem;
    }
    return bytes >= 1 && bytes <= 4096;
}

const char* type_name(Type type) {
    switch (type) {
    case Type::Bool:
        return "bool";
    case Type::U8:
        return "u8";
    case Type::U16:
        return "u16";
    case Type::U32:
        return "u32";
    case Type::U64:
        return "u64";
    case Type::S32:
        return "s32";
    case Type::S64:
        return "s64";
    case Type::F32:
        return "f32";
    case Type::F64:
        return "f64";
    case Type::Bytes:
        return "bytes";
    case Type::CStr:
        return "cstr";
    }
    return "";
}

bool exact_u64(uint64_t n) { return n <= (uint64_t(1) << 53); }

mods::json::Value encode_u64(uint64_t n) {
    if (exact_u64(n)) return mods::json::Value(double(n));
    return mods::json::Value(std::to_string(n));
}

mods::json::Value encode_s64(int64_t n) {
    if (n >= 0) return encode_u64(uint64_t(n));
    if (exact_u64(uint64_t(-n))) return mods::json::Value(double(n));
    return mods::json::Value(std::to_string(n));
}

mods::json::Value encode_bytes(const uint8_t* p, int n) {
    static const char* hex = "0123456789abcdef";
    std::string s(size_t(n) * 2, '0');
    for (int i = 0; i < n; i++) {
        s[size_t(i) * 2] = hex[p[i] >> 4];
        s[size_t(i) * 2 + 1] = hex[p[i] & 15];
    }
    return mods::json::Value(std::move(s));
}

bool encode_one(Type type, const uint8_t* p, mods::json::Value& out, std::string& err) {
    switch (type) {
    case Type::Bool:
        out = p[0] != 0;
        return true;
    case Type::U8:
        out = int(p[0]);
        return true;
    case Type::U16: {
        uint16_t n = 0;
        std::memcpy(&n, p, 2);
        out = int(n);
        return true;
    }
    case Type::U32: {
        uint32_t n = 0;
        std::memcpy(&n, p, 4);
        out = encode_u64(n);
        return true;
    }
    case Type::U64: {
        uint64_t n = 0;
        std::memcpy(&n, p, 8);
        out = encode_u64(n);
        return true;
    }
    case Type::S32: {
        int32_t n = 0;
        std::memcpy(&n, p, 4);
        out = encode_s64(n);
        return true;
    }
    case Type::S64: {
        int64_t n = 0;
        std::memcpy(&n, p, 8);
        out = encode_s64(n);
        return true;
    }
    case Type::F32: {
        float n = 0;
        std::memcpy(&n, p, 4);
        if (!std::isfinite(n)) {
            err = "non-finite";
            return false;
        }
        out = double(n);
        return true;
    }
    case Type::F64: {
        double n = 0;
        std::memcpy(&n, p, 8);
        if (!std::isfinite(n)) {
            err = "non-finite";
            return false;
        }
        out = n;
        return true;
    }
    case Type::Bytes:
    case Type::CStr:
        err = "bad type";
        return false;
    }
    err = "bad type";
    return false;
}

bool encode_read(const ReadEntry& read, mods::json::Value& out, std::string& err) {
    const void* addr = read.addr();
    if (!addr) {
        err = "null address";
        return false;
    }
    const auto* p = static_cast<const uint8_t*>(addr);
    if (read.type == Type::Bytes) {
        out = encode_bytes(p, read.count);
        return true;
    }
    if (read.type == Type::CStr) {
        int n = 0;
        while (n < read.count && p[n] != 0) n++;
        out = std::string(reinterpret_cast<const char*>(p), size_t(n));
        return true;
    }
    const int stride = elem_size(read.type);
    if (read.count == 1) return encode_one(read.type, p, out, err);
    out.type = mods::json::Value::Array;
    for (int i = 0; i < read.count; i++) {
        mods::json::Value item;
        if (!encode_one(read.type, p + i * stride, item, err)) return false;
        out.array.push_back(std::move(item));
    }
    return true;
}

mods::json::Value ping_call(const mods::json::Value& args) { return args; }

struct PingReg {
    PingReg() { add_call(Call{"ping", "Return the JSON arguments.", true, ping_call}); }
};

PingReg g_ping;

}

bool add_call(const Call& call) {
    if (!call.name || !call.name[0] || !call.fn) return false;
    const std::string name = call.name;
    if (name_taken(name)) return false;
    CallEntry entry;
    entry.name = name;
    entry.help = call.help ? call.help : "";
    entry.observe = call.observe;
    entry.fn = call.fn;
    g_calls.push_back(std::move(entry));
    return true;
}

bool add_read(const Read& read) {
    if (!read.name || !read.name[0] || !read.addr) return false;
    int bytes = 0;
    if (!copy_size(read.type, read.count, bytes)) return false;
    const std::string name = read.name;
    if (name_taken(name)) return false;
    ReadEntry entry;
    entry.name = name;
    entry.help = read.help ? read.help : "";
    entry.type = read.type;
    entry.count = read.count;
    entry.addr = read.addr;
    g_reads.push_back(std::move(entry));
    return true;
}

bool lookup(const std::string& name, bool& observe) {
    for (const CallEntry& call : g_calls) {
        if (call.name != name) continue;
        observe = call.observe;
        return true;
    }
    for (const ReadEntry& read : g_reads) {
        if (read.name != name) continue;
        observe = true;
        return true;
    }
    return false;
}

std::string list_json() {
    struct Row {
        std::string name;
        mods::json::Value value;
    };
    std::vector<Row> rows;
    for (const CallEntry& call : g_calls) {
        mods::json::Value row;
        row.type = mods::json::Value::Object;
        row.object["name"] = call.name;
        row.object["kind"] = "call";
        row.object["help"] = call.help;
        row.object["observe"] = call.observe;
        rows.push_back(Row{call.name, std::move(row)});
    }
    for (const ReadEntry& read : g_reads) {
        mods::json::Value row;
        row.type = mods::json::Value::Object;
        row.object["name"] = read.name;
        row.object["kind"] = "read";
        row.object["help"] = read.help;
        row.object["observe"] = true;
        row.object["type"] = type_name(read.type);
        row.object["count"] = read.count;
        rows.push_back(Row{read.name, std::move(row)});
    }
    for (size_t i = 1; i < rows.size(); i++) {
        Row cur = std::move(rows[i]);
        size_t j = i;
        while (j > 0 && rows[j - 1].name > cur.name) {
            rows[j] = std::move(rows[j - 1]);
            j--;
        }
        rows[j] = std::move(cur);
    }
    mods::json::Value evals;
    evals.type = mods::json::Value::Array;
    for (Row& row : rows) evals.array.push_back(std::move(row.value));
    mods::json::Value v;
    v.type = mods::json::Value::Object;
    v.object["evals"] = std::move(evals);
    return mods::json::dump(v);
}

std::string invoke(const std::string& name, const mods::json::Value& args, std::string& err) {
    err.clear();
    try {
        for (const CallEntry& call : g_calls) {
            if (call.name != name) continue;
            return mods::json::dump(call.fn(args));
        }
        for (const ReadEntry& read : g_reads) {
            if (read.name != name) continue;
            mods::json::Value value;
            if (!encode_read(read, value, err)) return "";
            return mods::json::dump(value);
        }
        err = "unknown eval";
        return "";
    } catch (const std::exception& e) {
        err = e.what();
        return "";
    }
}

}
