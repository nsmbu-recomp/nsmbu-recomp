#pragma once
#include <cstdint>
#include <memory>
#include <string>
#include <vector>
namespace gfxvk::vk {
struct ShaderSourceId {
    uint64_t low = 0, high = 0;
    uint32_t length = 0;
    bool operator==(const ShaderSourceId&) const = default;
};
using ShaderSource = std::shared_ptr<const std::string>;
using SpirvWords = std::shared_ptr<const std::vector<uint32_t>>;
template<class Map, class Key, class Binary>
void release_pending_shader_source(Map& pending, const Key& key,
        const std::shared_ptr<Binary>& binary) {
    if (auto it = pending.find(key);
        it != pending.end() && it->second.second.lock() == binary)
        pending.erase(it);
}
}
