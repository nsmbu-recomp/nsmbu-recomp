#pragma once
#include "backend.h"
#include "pass_cache.h"
#include <stdexcept>

namespace gfxvk {
struct PassBegin {
    PassFormats formats;
    std::array<VkImageLayout, 9> layouts{};
};
struct PreparedPass {
    VkRenderPassBeginInfo info{VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO};
    std::array<VkClearValue, 9> clears{};
    void begin(VkCommandBuffer cmd, const PassApi& api) const {
        auto rebound = info; rebound.pClearValues = clears.data();
        api.beginRenderPass(cmd, &rebound, VK_SUBPASS_CONTENTS_INLINE);
    }
};
void init_pass_caches();
PassCaches& pass_caches();
void begin_render_pass(VkCommandBuffer, const VkRenderingInfo&, const PassBegin&);
void end_render_pass(VkCommandBuffer);
void retire_image_views(const VkImageView*, size_t count);
inline PassFormats pass_formats(const VkPipelineRenderingCreateInfo& info) {
    PassFormats formats; formats.colorCount = info.colorAttachmentCount;
    if (formats.colorCount > formats.colors.size()) throw std::invalid_argument("too many pass colour formats");
    for (uint32_t i = 0; i < formats.colorCount; ++i) formats.colors[i] = info.pColorAttachmentFormats[i];
    formats.depth = info.depthAttachmentFormat;
    formats.stencil = info.stencilAttachmentFormat != VK_FORMAT_UNDEFINED;
    return formats;
}
inline PassFormats one_color(VkFormat format) {
    PassFormats formats; formats.colorCount = 1; formats.colors[0] = format;
    return formats;
}
inline void apply_pipeline_pass(VkGraphicsPipelineCreateInfo& info, const VkPipelineRenderingCreateInfo& rendering) {
    if (!R.renderPasses) return;
    info.pNext = nullptr;
    info.renderPass = pass_caches().render_pass(pass_formats(rendering), VK_ATTACHMENT_LOAD_OP_LOAD);
    info.subpass = 0;
}
inline void begin_pass(VkCommandBuffer cmd, const VkRenderingInfo& info, const PassFormats& formats) {
    if (!R.renderPasses) {
        vkCmdBeginRendering(cmd, &info);
        return;
    }
    PassBegin pass; pass.formats = formats;
    for (uint32_t i = 0; i < info.colorAttachmentCount && info.pColorAttachments; ++i)
        pass.layouts[i] = info.pColorAttachments[i].imageLayout;
    if (info.pDepthAttachment) pass.layouts[8] = info.pDepthAttachment->imageLayout;
    begin_render_pass(cmd, info, pass);
}
inline void end_pass(VkCommandBuffer cmd) {
    if (R.renderPasses) end_render_pass(cmd);
    else vkCmdEndRendering(cmd);
}
}
