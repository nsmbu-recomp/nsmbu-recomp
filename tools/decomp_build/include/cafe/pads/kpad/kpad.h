#pragma once
enum { KPAD_MAX_READ_BUFS = 16, WPAD_MAX_CONTROLLERS = 4 };
struct KPADStatus { unsigned char raw[0x80]; };
struct KPADUnifiedWpadStatus { unsigned char raw[0x80]; };
