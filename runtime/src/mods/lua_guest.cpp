#include "lua_guest.h"
#include "ppc.h"
#include <cstring>

namespace mods::lua_guest {
namespace {
uint8_t* g_base = nullptr;
constexpr size_t kMax = 1u << 20;
constexpr uint32_t kMem2Start = 0x10000000;
constexpr uint32_t kMem2End = 0x50000000;
constexpr uint32_t kFgStart = 0xE0000000;
constexpr uint32_t kFgEnd = 0xE0000000 + 0x02800000;
constexpr uint32_t kMem1Start = 0xF4000000;
constexpr uint32_t kMem1End = 0xF4000000 + 0x02000000;

bool in_region(uint32_t addr, size_t size, uint32_t start, uint32_t end) {
    uint64_t a = addr, e = a + size;
    if (e < a) return false;
    if (size == 0) return a >= start && a < end;
    return a >= start && e <= end;
}

uint8_t* at(uint32_t addr) { return base() + addr; }
}

void set_base(uint8_t* base) { g_base = base; }
uint8_t* base() { return g_base ? g_base : PPC_MEM_BASE; }

bool check(uint32_t addr, size_t size, std::string& err) {
    if (size > kMax) { err = "size too large"; return false; }
    if (in_region(addr, size, kMem2Start, kMem2End) ||
        in_region(addr, size, kFgStart, kFgEnd) ||
        in_region(addr, size, kMem1Start, kMem1End)) {
        err.clear();
        return true;
    }
    err = "address out of range";
    return false;
}

bool read_u8(uint32_t addr, uint8_t& out, std::string& err) {
    if (!check(addr, 1, err)) return false;
    out = *at(addr);
    return true;
}
bool read_s8(uint32_t addr, int8_t& out, std::string& err) {
    uint8_t v;
    if (!read_u8(addr, v, err)) return false;
    out = (int8_t)v;
    return true;
}
bool read_u16(uint32_t addr, uint16_t& out, std::string& err) {
    if (!check(addr, 2, err)) return false;
    uint16_t v;
    std::memcpy(&v, at(addr), 2);
    out = __builtin_bswap16(v);
    return true;
}
bool read_s16(uint32_t addr, int16_t& out, std::string& err) {
    uint16_t v;
    if (!read_u16(addr, v, err)) return false;
    out = (int16_t)v;
    return true;
}
bool read_u32(uint32_t addr, uint32_t& out, std::string& err) {
    if (!check(addr, 4, err)) return false;
    uint32_t v;
    std::memcpy(&v, at(addr), 4);
    out = __builtin_bswap32(v);
    return true;
}
bool read_s32(uint32_t addr, int32_t& out, std::string& err) {
    uint32_t v;
    if (!read_u32(addr, v, err)) return false;
    out = (int32_t)v;
    return true;
}
bool read_f32(uint32_t addr, float& out, std::string& err) {
    uint32_t v;
    if (!read_u32(addr, v, err)) return false;
    std::memcpy(&out, &v, 4);
    return true;
}
bool read_f64(uint32_t addr, double& out, std::string& err) {
    if (!check(addr, 8, err)) return false;
    uint64_t v;
    std::memcpy(&v, at(addr), 8);
    v = __builtin_bswap64(v);
    std::memcpy(&out, &v, 8);
    return true;
}
bool read_bytes(uint32_t addr, size_t size, std::string& out, std::string& err) {
    if (!check(addr, size, err)) { out.clear(); return false; }
    out.assign(reinterpret_cast<const char*>(at(addr)), size);
    return true;
}

bool write_u8(uint32_t addr, uint8_t v, std::string& err) {
    if (!check(addr, 1, err)) return false;
    *at(addr) = v;
    return true;
}
bool write_s8(uint32_t addr, int8_t v, std::string& err) { return write_u8(addr, (uint8_t)v, err); }
bool write_u16(uint32_t addr, uint16_t v, std::string& err) {
    if (!check(addr, 2, err)) return false;
    v = __builtin_bswap16(v);
    std::memcpy(at(addr), &v, 2);
    return true;
}
bool write_s16(uint32_t addr, int16_t v, std::string& err) { return write_u16(addr, (uint16_t)v, err); }
bool write_u32(uint32_t addr, uint32_t v, std::string& err) {
    if (!check(addr, 4, err)) return false;
    v = __builtin_bswap32(v);
    std::memcpy(at(addr), &v, 4);
    return true;
}
bool write_s32(uint32_t addr, int32_t v, std::string& err) { return write_u32(addr, (uint32_t)v, err); }
bool write_f32(uint32_t addr, float v, std::string& err) {
    uint32_t u;
    std::memcpy(&u, &v, 4);
    return write_u32(addr, u, err);
}
bool write_f64(uint32_t addr, double v, std::string& err) {
    if (!check(addr, 8, err)) return false;
    uint64_t u;
    std::memcpy(&u, &v, 8);
    u = __builtin_bswap64(u);
    std::memcpy(at(addr), &u, 8);
    return true;
}
bool write_bytes(uint32_t addr, const void* data, size_t size, std::string& err) {
    if (!check(addr, size, err)) return false;
    std::memcpy(at(addr), data, size);
    return true;
}

}
