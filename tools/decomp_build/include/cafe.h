#pragma once
#include <types.h>
typedef int BOOL;
enum { CACHE_BLOCK_SIZE = 32, LL_CACHE_FETCH_SIZE = 64 };

struct OSMutex { unsigned char raw[0x2C]; };
struct OSEvent { unsigned char raw[0x20]; };
struct OSThread { unsigned char raw[0x100]; };
struct OSMessageQueue { unsigned char raw[0x34]; };

extern "C" void OSGetSystemTime(u32* hi, u32* lo);
inline u64 OSGetSystemTime() {
    u32 hi = 0, lo = 0;
    OSGetSystemTime(&hi, &lo);
    return ((u64)hi << 32) | lo;
}
inline void* OSGetThreadSpecific(u32) { return nullptr; }
inline u32 OSGetCoreId() { return 1; }
extern "C" void* OSBlockSet(void* dst, int val, u32 size);
extern "C" void* OSBlockMove(void* dst, const void* src, u32 size, int flush);
inline void DCInvalidateRange(const void*, u32) {}
inline void DCFlushRange(const void*, u32) {}
inline void DCStoreRange(const void*, u32) {}
