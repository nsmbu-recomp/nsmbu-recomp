#include "pass.h"
#include <cassert>

namespace gfxvk {
namespace { PassCaches* caches = nullptr; }
void init_pass_caches() {
    if (!R.renderPasses || caches) return;
    caches = new PassCaches(R.device, PassApi{
        vkCreateRenderPass, vkDestroyRenderPass, vkCreateFramebuffer, vkDestroyFramebuffer,
        vkCmdBeginRenderPass, vkCmdEndRenderPass});
}
PassCaches& pass_caches() {
    if (!caches) throw std::logic_error("render-pass cache is not initialized");
    return *caches;
}
PreparedPass prepare_pass(PassCaches& cache, const VkRenderingInfo& info, const PassBegin& pass) {
    assert(info.layerCount == 1 && !info.viewMask && !info.flags && !info.pNext);
    assert(info.colorAttachmentCount == pass.formats.colorCount);
    PreparedPass prepared;
    FramebufferKey key;
    key.width = info.renderArea.offset.x + info.renderArea.extent.width;
    key.height = info.renderArea.offset.y + info.renderArea.extent.height;
    VkAttachmentLoadOp load = VK_ATTACHMENT_LOAD_OP_LOAD;
    bool haveLoad = false;
    uint32_t clearCount = 0;
    for (uint32_t i = 0; i < info.colorAttachmentCount; ++i) {
        const auto& attachment = info.pColorAttachments[i];
        key.views[i] = attachment.imageView;
        assert(bool(attachment.imageView) == (pass.formats.colors[i] != VK_FORMAT_UNDEFINED));
        if (!attachment.imageView) continue;
        assert(attachment.imageLayout == VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL);
        assert(pass.layouts[i] == attachment.imageLayout);
        assert(attachment.storeOp == VK_ATTACHMENT_STORE_OP_STORE && !attachment.resolveImageView && !attachment.pNext);
        if (haveLoad && load != attachment.loadOp) throw std::invalid_argument("mixed pass colour load operations");
        load = attachment.loadOp; haveLoad = true;
        prepared.clears[clearCount++] = attachment.clearValue;
    }
    if (info.pDepthAttachment) {
        const auto& attachment = *info.pDepthAttachment;
        key.views[8] = attachment.imageView;
        assert(attachment.imageLayout == VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL);
        assert(pass.layouts[8] == attachment.imageLayout);
        assert((attachment.loadOp == VK_ATTACHMENT_LOAD_OP_LOAD || attachment.loadOp == VK_ATTACHMENT_LOAD_OP_DONT_CARE) &&
               attachment.storeOp == VK_ATTACHMENT_STORE_OP_STORE);
        assert(!attachment.resolveImageView && !attachment.pNext);
    }
    assert(bool(info.pDepthAttachment) == (pass.formats.depth != VK_FORMAT_UNDEFINED));
    assert(bool(info.pStencilAttachment) == pass.formats.stencil);
    assert(!info.pStencilAttachment || info.pStencilAttachment->imageView == key.views[8]);
    key.compatiblePass = cache.render_pass(pass.formats, VK_ATTACHMENT_LOAD_OP_LOAD);
    prepared.info.renderPass = load == VK_ATTACHMENT_LOAD_OP_LOAD ? key.compatiblePass : cache.render_pass(pass.formats, load);
    prepared.info.framebuffer = cache.framebuffer(pass.formats, key);
    prepared.info.renderArea = info.renderArea;
    prepared.info.clearValueCount = load == VK_ATTACHMENT_LOAD_OP_CLEAR ? clearCount : 0;
    return prepared;
}
void begin_render_pass(VkCommandBuffer cmd, const VkRenderingInfo& info, const PassBegin& pass) {
    prepare_pass(pass_caches(), info, pass).begin(cmd, pass_caches().api());
}
void end_render_pass(VkCommandBuffer cmd) { vkCmdEndRenderPass(cmd); }
void retire_image_views(const VkImageView* views, size_t count) {
    if (!caches || !count) return;
    caches->destroy_framebuffers(caches->forget_views(views, count));
}
}
