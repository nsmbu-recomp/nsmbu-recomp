#pragma once
#include <cstdint>
#include <string>

namespace mods::lua_guest {

void set_base(uint8_t* base);
uint8_t* base();

bool check(uint32_t addr, size_t size, std::string& err);

bool read_u8(uint32_t addr, uint8_t& out, std::string& err);
bool read_s8(uint32_t addr, int8_t& out, std::string& err);
bool read_u16(uint32_t addr, uint16_t& out, std::string& err);
bool read_s16(uint32_t addr, int16_t& out, std::string& err);
bool read_u32(uint32_t addr, uint32_t& out, std::string& err);
bool read_s32(uint32_t addr, int32_t& out, std::string& err);
bool read_f32(uint32_t addr, float& out, std::string& err);
bool read_f64(uint32_t addr, double& out, std::string& err);
bool read_bytes(uint32_t addr, size_t size, std::string& out, std::string& err);

bool write_u8(uint32_t addr, uint8_t v, std::string& err);
bool write_s8(uint32_t addr, int8_t v, std::string& err);
bool write_u16(uint32_t addr, uint16_t v, std::string& err);
bool write_s16(uint32_t addr, int16_t v, std::string& err);
bool write_u32(uint32_t addr, uint32_t v, std::string& err);
bool write_s32(uint32_t addr, int32_t v, std::string& err);
bool write_f32(uint32_t addr, float v, std::string& err);
bool write_f64(uint32_t addr, double v, std::string& err);
bool write_bytes(uint32_t addr, const void* data, size_t size, std::string& err);

}
