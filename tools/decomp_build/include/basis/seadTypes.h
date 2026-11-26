#pragma once
#include <stddef.h>
#include <stdint.h>

// Host stand-in for sead's Cafe type header.
// Pointers stay host pointers. The Cafe size checks are not used here.
typedef uint8_t u8;
typedef uint16_t u16;
typedef uint32_t u32;
typedef uint64_t u64;
typedef int8_t s8;
typedef int16_t s16;
typedef int32_t s32;
typedef int64_t s64;
typedef float f32;
typedef double f64;
typedef wchar_t char16;
typedef uintptr_t uintptr;
typedef s32 sintptr;
typedef s64 d_sintptr;

#define forceinline __attribute__((always_inline))
#define noinline

#define SEAD_MACRO_UTIL_ROUNDUP(x, y) ((x) + ((y) - 1) & ~((y) - 1))
#define SEAD_MACRO_UTIL_ROUNDDOWN(x, y) ((x) & ~((y) - 1))
