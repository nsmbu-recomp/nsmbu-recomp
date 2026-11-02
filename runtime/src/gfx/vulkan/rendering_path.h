#pragma once
#include <vulkan/vulkan.h>
#include <array>
#include <cstdint>
#include <cstring>

namespace gfxvk {
enum class RenderingPath { None, Core, KHR, RenderPass };
struct RenderingCapabilities {
    uint32_t api = 0;
    bool core = false, khr = false;
};
struct RenderingChoice {
    RenderingPath path = RenderingPath::None;
    const char* reason = "Vulkan older than 1.1";
};
constexpr uint32_t intel_windows_driver(uint32_t major, uint32_t minor) {
    return (major << 14) | minor;
}
struct RenderingDriverRule {
    uint32_t vendor, before;
    bool windowsOnly;
    const char* reason;
};
inline constexpr std::array renderingDriverRules{
    RenderingDriverRule{0x8086, intel_windows_driver(101, 5000), true,
        "driver rule: Intel Windows < 101.5000"},
};
inline RenderingChoice choose_rendering_path(RenderingCapabilities caps, uint32_t vendor,
        uint32_t driver, bool windows, const char* forceRenderPass) {
    if (caps.api < VK_API_VERSION_1_1) return {};
    const bool forceOn = forceRenderPass && !std::strcmp(forceRenderPass, "1");
    const bool forceOff = forceRenderPass && !std::strcmp(forceRenderPass, "0");
    if (forceOn) return {RenderingPath::RenderPass, "NSMBU_VK_FORCE_RENDERPASS=1"};
    if (!forceOff) for (const auto& rule : renderingDriverRules)
        if (vendor == rule.vendor && (!rule.windowsOnly || windows) && driver < rule.before)
            return {RenderingPath::RenderPass, rule.reason};
    if (caps.api >= VK_API_VERSION_1_3 && caps.core)
        return {RenderingPath::Core, forceOff ? "NSMBU_VK_FORCE_RENDERPASS=0" : "Vulkan 1.3 dynamic rendering"};
    if (caps.khr)
        return {RenderingPath::KHR, forceOff ? "NSMBU_VK_FORCE_RENDERPASS=0" : "VK_KHR_dynamic_rendering"};
    if (forceOff) return {RenderingPath::None, "NSMBU_VK_FORCE_RENDERPASS=0 but dynamic rendering unavailable"};
    return {RenderingPath::RenderPass, "dynamic rendering unavailable"};
}
inline const char* rendering_path_name(RenderingPath path) {
    switch (path) {
    case RenderingPath::Core: return "dynamic rendering (core)";
    case RenderingPath::KHR: return "dynamic rendering (KHR)";
    case RenderingPath::RenderPass: return "render passes";
    default: return "unavailable";
    }
}
}
