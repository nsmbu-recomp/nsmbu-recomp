#pragma once
#include <vulkan/vulkan.h>
#include <cstdint>
#include <cstring>

namespace gfxvk {
constexpr uint32_t kVendorIntel = 0x8086;

inline bool attachment_view_usage_wanted(uint32_t vendorID, const char* env) {
    if (env && !std::strcmp(env, "1")) return true;
    if (env && !std::strcmp(env, "0")) return false;
    return vendorID == kVendorIntel;
}

inline VkImageUsageFlags attachment_view_usage(VkImageUsageFlags imageUsage, bool depth) {
    const VkImageUsageFlags want =
        depth ? VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT : VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT;
    return imageUsage & want;
}
}
