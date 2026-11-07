#pragma once
#include "shader_present_gate.h"
#include "loader.h"
#include <memory>
#include <string>

namespace gfxvk {
void present_while_shader_pending();
namespace vk {
struct SpirvCompile;
bool shader_jobs_async();
bool enqueue_spirv_compile(const std::string& source, bool vertex, std::shared_ptr<SpirvCompile> done);
void prioritize_spirv_compile(const std::shared_ptr<SpirvCompile>& done);
void wait_spirv_queue();
void finish_shader_jobs();
VkResult create_graphics_pipelines(VkDevice device, VkPipelineCache cache, uint32_t count,
                                   const VkGraphicsPipelineCreateInfo* info,
                                   const VkAllocationCallbacks* allocator, VkPipeline* pipelines);
}
}
