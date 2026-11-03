

#include "guest_addr.h"

__attribute__((weak)) const GuestStep g_guest_code_steps[] = {{0u, 0}};
__attribute__((weak)) const unsigned g_guest_code_step_count = 1u;
__attribute__((weak)) const GuestStep g_guest_data_steps[] = {{0u, 0}};
__attribute__((weak)) const unsigned g_guest_data_step_count = 1u;
__attribute__((weak)) const char g_guest_build_name[] = "USA";
__attribute__((weak)) const char g_guest_build_label[] = "USA 1.3.0";
__attribute__((weak)) const char g_guest_build_title_id[] = "0005000010101d00";
__attribute__((weak)) const int g_guest_build_title_version = 64;

__attribute__((weak)) const uint32_t g_guest_code_lo = 0, g_guest_code_hi = 0;
__attribute__((weak)) const uint32_t g_guest_data_lo = 0, g_guest_data_hi = 0;
__attribute__((weak)) const GuestChanged g_guest_changed_code[] = {{0u, 0u}};
__attribute__((weak)) const unsigned g_guest_changed_code_count = 0;
