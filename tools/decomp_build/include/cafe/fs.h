#pragma once
#include <types.h>

enum { FS_MAX_MOUNTPATH_SIZE = 128 };

struct FSClient { unsigned char raw[0x1700]; };
struct FSFreeSpaceSize { u64 size; };
typedef u32 FSFileHandle;
typedef u64 FSFilePosition;
typedef u32 FSDirHandle;
typedef s32 FSStatus;
typedef s32 FSRetFlag;
typedef s32 FSVolumeState;
enum { FS_RET_NO_ERROR = 0 };
struct FSStateChangeParams { unsigned char raw[0x20]; };
