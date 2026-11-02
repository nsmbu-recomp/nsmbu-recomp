#pragma once
#include "loader.h"
#include <array>
#include <cstddef>
#include <mutex>
#include <unordered_map>
#include <unordered_set>
#include <vector>

namespace gfxvk {
struct PassFormats {
    std::array<VkFormat, 8> colors{};
    uint32_t colorCount = 0;
    VkFormat depth = VK_FORMAT_UNDEFINED;
    bool stencil = false;
    bool operator==(const PassFormats&) const = default;
};
struct RenderPassKey {
    PassFormats formats;
    VkAttachmentLoadOp colorLoad = VK_ATTACHMENT_LOAD_OP_LOAD;
    bool operator==(const RenderPassKey&) const = default;
};
struct FramebufferKey {
    VkRenderPass compatiblePass = VK_NULL_HANDLE;
    std::array<VkImageView, 9> views{};
    uint32_t width = 0, height = 0;
    bool operator==(const FramebufferKey&) const = default;
};
struct RenderPassKeyHash { size_t operator()(const RenderPassKey&) const; };
struct FramebufferKeyHash { size_t operator()(const FramebufferKey&) const; };
struct PassApi {
    PFN_vkCreateRenderPass createRenderPass = nullptr;
    PFN_vkDestroyRenderPass destroyRenderPass = nullptr;
    PFN_vkCreateFramebuffer createFramebuffer = nullptr;
    PFN_vkDestroyFramebuffer destroyFramebuffer = nullptr;
    PFN_vkCmdBeginRenderPass beginRenderPass = nullptr;
    PFN_vkCmdEndRenderPass endRenderPass = nullptr;
};
class PassCaches {
public:
    PassCaches(VkDevice device, PassApi api): device_(device), api_(api) {}
    PassCaches(const PassCaches&) = delete;
    PassCaches& operator=(const PassCaches&) = delete;
    ~PassCaches();
    VkRenderPass render_pass(const PassFormats&, VkAttachmentLoadOp colorLoad);
    VkFramebuffer framebuffer(const PassFormats&, const FramebufferKey&);
    std::vector<VkFramebuffer> forget_views(const VkImageView*, size_t count);
    void destroy_framebuffers(const std::vector<VkFramebuffer>&);
    void reset();
    const PassApi& api() const { return api_; }
private:
    VkDevice device_;
    PassApi api_;
    std::mutex renderPassMutex_;
    std::unordered_map<RenderPassKey, VkRenderPass, RenderPassKeyHash> renderPasses_;
    std::unordered_map<FramebufferKey, VkFramebuffer, FramebufferKeyHash> framebuffers_;
    std::unordered_map<VkImageView, std::unordered_set<FramebufferKey, FramebufferKeyHash>> byView_;
};
}
