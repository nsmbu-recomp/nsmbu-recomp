#pragma once
enum { VPAD_MAX_READ_BUFS = 16 };
struct VPADStatus { unsigned char raw[0xC0]; };
