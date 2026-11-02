

#pragma once
#ifndef VK_NO_PROTOTYPES
#error "the Vulkan renderer is built with VK_NO_PROTOTYPES (CMakeLists.txt)"
#endif
#include <vulkan/vulkan.h>

#define NSMBU_VK_GLOBAL_FUNCTIONS(X) \
  X(vkCreateInstance) X(vkEnumerateInstanceExtensionProperties)

#define NSMBU_VK_INSTANCE_FUNCTIONS(X) \
  X(vkCreateDevice) X(vkDestroySurfaceKHR) X(vkEnumerateDeviceExtensionProperties) X(vkEnumeratePhysicalDevices) \
  X(vkGetDeviceProcAddr) X(vkGetPhysicalDeviceFeatures) X(vkGetPhysicalDeviceFeatures2) \
  X(vkGetPhysicalDeviceFormatProperties) X(vkGetPhysicalDeviceMemoryProperties) X(vkGetPhysicalDeviceProperties) \
  X(vkGetPhysicalDeviceQueueFamilyProperties) X(vkGetPhysicalDeviceSurfaceCapabilitiesKHR) \
  X(vkGetPhysicalDeviceSurfaceFormatsKHR) X(vkGetPhysicalDeviceSurfacePresentModesKHR) \
  X(vkGetPhysicalDeviceSurfaceSupportKHR)

#define NSMBU_VK_DEVICE_FUNCTIONS(X) \
  X(vkAcquireNextImageKHR) X(vkAllocateCommandBuffers) X(vkAllocateDescriptorSets) X(vkAllocateMemory) \
  X(vkBeginCommandBuffer) X(vkBindBufferMemory) X(vkBindImageMemory) X(vkCmdBindDescriptorSets) \
  X(vkCmdBindIndexBuffer) X(vkCmdBindPipeline) X(vkCmdBindVertexBuffers) X(vkCmdBlitImage) \
  X(vkCmdClearColorImage) X(vkCmdClearDepthStencilImage) X(vkCmdCopyBuffer) X(vkCmdCopyBufferToImage) \
  X(vkCmdCopyImage) X(vkCmdCopyImageToBuffer) X(vkCmdDraw) X(vkCmdDrawIndexed) X(vkCmdPipelineBarrier) \
  X(vkCmdPushConstants) X(vkCmdResetQueryPool) X(vkCmdSetBlendConstants) X(vkCmdSetDepthBias) X(vkCmdSetScissor) \
  X(vkCmdSetStencilReference) X(vkCmdSetStencilWriteMask) X(vkCmdSetViewport) X(vkCmdWriteTimestamp) X(vkCreateBuffer) \
  X(vkCreateCommandPool) X(vkCreateDescriptorPool) X(vkCreateDescriptorSetLayout) X(vkCreateFence) \
  X(vkCreateComputePipelines) X(vkCmdDispatch) X(vkCreateGraphicsPipelines) X(vkCreateImage) X(vkCreateImageView) X(vkCreatePipelineCache) \
  X(vkCreatePipelineLayout) X(vkCreateQueryPool) X(vkCreateSampler) X(vkCreateSemaphore) \
  X(vkCreateShaderModule) X(vkCreateSwapchainKHR) X(vkDestroyBuffer) X(vkDestroyDescriptorSetLayout) \
  X(vkDestroyImage) X(vkDestroyImageView) X(vkDestroyPipeline) X(vkDestroyPipelineLayout) \
  X(vkDestroyQueryPool) X(vkDestroySampler) X(vkDestroySemaphore) X(vkDestroyShaderModule) \
  X(vkDestroySwapchainKHR) X(vkDeviceWaitIdle) X(vkEndCommandBuffer) X(vkFreeMemory) \
  X(vkGetBufferMemoryRequirements) X(vkGetDeviceQueue) X(vkGetFenceStatus) X(vkGetImageMemoryRequirements) \
  X(vkGetPipelineCacheData) X(vkGetQueryPoolResults) X(vkGetSwapchainImagesKHR) X(vkMapMemory) \
  X(vkQueuePresentKHR) X(vkQueueSubmit) X(vkQueueWaitIdle) X(vkResetCommandPool) X(vkResetDescriptorPool) \
  X(vkResetFences) X(vkUnmapMemory) X(vkUpdateDescriptorSets) X(vkWaitForFences) \
  X(vkCreateRenderPass) X(vkDestroyRenderPass) X(vkCreateFramebuffer) X(vkDestroyFramebuffer) \
  X(vkCmdBeginRenderPass) X(vkCmdEndRenderPass)

#define NSMBU_VK_RENDERING_FUNCTIONS(X) X(vkCmdBeginRendering) X(vkCmdEndRendering)

namespace gfxvk {
extern PFN_vkGetInstanceProcAddr vkGetInstanceProcAddr;
extern PFN_vkEnumerateInstanceVersion vkEnumerateInstanceVersion;
extern PFN_vkEnumerateInstanceLayerProperties vkEnumerateInstanceLayerProperties;
#define NSMBU_VK_DECLARE(name) extern PFN_##name name;
NSMBU_VK_GLOBAL_FUNCTIONS(NSMBU_VK_DECLARE)
NSMBU_VK_INSTANCE_FUNCTIONS(NSMBU_VK_DECLARE)
NSMBU_VK_DEVICE_FUNCTIONS(NSMBU_VK_DECLARE)
NSMBU_VK_RENDERING_FUNCTIONS(NSMBU_VK_DECLARE)
#undef NSMBU_VK_DECLARE

void load_global_functions(PFN_vkGetInstanceProcAddr gipa);
void load_instance_functions(VkInstance instance);

void load_device_functions(VkDevice device, bool khrDynamicRendering, bool renderPasses);
}
