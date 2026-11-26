#pragma once
#include <stddef.h>
#include <stdint.h>

// sead's public headers select the Cafe layout with this macro.
// The size checks assume 32-bit pointers, so this translation unit drops them.
#define cafe 1
#define static_assert(...)
#define NW_RELEASE 1
#define SEAD_MATH_CAFE_RSQRT_USE_INTRINSICS 1
#include <cafe/gx2.h>
#include <cafe/fs.h>
#include <prim/seadPtrUtil.h>
#include <decomp_guest.h>
