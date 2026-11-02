
#pragma once
#include "Cafe/HW/Latte/Renderer/Renderer.h"
class VulkanRenderer : public Renderer {
public:
    VulkanRenderer() : Renderer(RendererAPI::Vulkan) {}
};
