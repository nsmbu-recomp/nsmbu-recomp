
#include "loader.h"
#include <stdexcept>
#include <string>

namespace gfxvk {
PFN_vkGetInstanceProcAddr vkGetInstanceProcAddr = nullptr;
PFN_vkEnumerateInstanceVersion vkEnumerateInstanceVersion = nullptr;
PFN_vkEnumerateInstanceLayerProperties vkEnumerateInstanceLayerProperties = nullptr;
#define NSMBU_VK_DEFINE(name) PFN_##name name = nullptr;
NSMBU_VK_GLOBAL_FUNCTIONS(NSMBU_VK_DEFINE)
NSMBU_VK_INSTANCE_FUNCTIONS(NSMBU_VK_DEFINE)
NSMBU_VK_DEVICE_FUNCTIONS(NSMBU_VK_DEFINE)
NSMBU_VK_RENDERING_FUNCTIONS(NSMBU_VK_DEFINE)
#undef NSMBU_VK_DEFINE

namespace {
[[noreturn]] void missing(const char *name) {
  throw std::runtime_error(std::string("the Vulkan driver does not provide ") + name +
                           "; update the graphics driver");
}
}

void load_global_functions(PFN_vkGetInstanceProcAddr gipa) {
  if (!gipa)
    throw std::runtime_error("the Vulkan loader has no vkGetInstanceProcAddr");
  vkGetInstanceProcAddr = gipa;
#define NSMBU_VK_LOAD(name) \
  if (!(name = reinterpret_cast<PFN_##name>(gipa(nullptr, #name)))) missing(#name);
  NSMBU_VK_GLOBAL_FUNCTIONS(NSMBU_VK_LOAD)
#undef NSMBU_VK_LOAD

  vkEnumerateInstanceVersion =
      reinterpret_cast<PFN_vkEnumerateInstanceVersion>(gipa(nullptr, "vkEnumerateInstanceVersion"));

  vkEnumerateInstanceLayerProperties = reinterpret_cast<PFN_vkEnumerateInstanceLayerProperties>(
      gipa(nullptr, "vkEnumerateInstanceLayerProperties"));
}

void load_instance_functions(VkInstance instance) {
#define NSMBU_VK_LOAD(name) \
  if (!(name = reinterpret_cast<PFN_##name>(vkGetInstanceProcAddr(instance, #name)))) missing(#name);
  NSMBU_VK_INSTANCE_FUNCTIONS(NSMBU_VK_LOAD)
#undef NSMBU_VK_LOAD
}

void load_device_functions(VkDevice device, bool khrDynamicRendering, bool renderPasses) {
#define NSMBU_VK_LOAD(name) \
  if (!(name = reinterpret_cast<PFN_##name>(vkGetDeviceProcAddr(device, #name)))) missing(#name);
  NSMBU_VK_DEVICE_FUNCTIONS(NSMBU_VK_LOAD)
#undef NSMBU_VK_LOAD
  if (renderPasses) return;

#define NSMBU_VK_LOAD(name)                                                                          \
  {                                                                                                 \
    const char *n = khrDynamicRendering ? #name "KHR" : #name;                                     \
    if (!(name = reinterpret_cast<PFN_##name>(vkGetDeviceProcAddr(device, n)))) missing(n);        \
  }
  NSMBU_VK_RENDERING_FUNCTIONS(NSMBU_VK_LOAD)
#undef NSMBU_VK_LOAD
}
}
