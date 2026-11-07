#pragma once
#include <cstdint>
#include <array>
#include <memory>
#include <condition_variable>
#include <mutex>
#include <string>
#include <vector>
#include "shader_source.h"
#include "Cafe/HW/Latte/LegacyShaderDecompiler/LatteDecompiler.h"

struct LatteFetchShader;
namespace gfxvk::vk {
struct DescriptorRankPlan {
    static constexpr uint8_t unused = 255;
    std::array<uint8_t, 16> blocks;
    std::array<uint8_t, LATTE_NUM_MAX_TEX_UNITS> textures;
    uint8_t support = unused, count = 0;
    bool valid = false;
    DescriptorRankPlan() { blocks.fill(unused); textures.fill(unused); }
};
DescriptorRankPlan make_descriptor_rank_plan(const LatteDecompilerShaderResourceMapping& mapping,
                                             const LatteDecompilerShader& shader);

struct SpirvCompile {
    std::mutex mutex;
    std::condition_variable wake;
    bool done = false;
    bool accounted = false;
    uint64_t ns = 0;
    SpirvWords words;
    std::string error;
};
struct Shader {
    uint64_t key = 0;
    uint64_t pipelineId = 0;
    bool vertex = false;
    LatteDecompilerShader* dec = nullptr;
    LatteDecompilerShaderResourceMapping mapping;
    DescriptorRankPlan descriptorRanks;
    LatteDecompilerOutputUniformOffsets uniforms;
    SpirvWords spirvWords;
    std::string glsl;
    std::string translatedGlsl;
    std::string error;
    ShaderSourceId sourceId;
    uint64_t spirvKey = 0;
    std::shared_ptr<SpirvCompile> compile;
    const std::vector<uint32_t>& spirv() const {
        static const std::vector<uint32_t> none;
        return spirvWords ? *spirvWords : none;
    }
    bool ready() const { return dec && !spirv().empty() && error.empty(); }
    bool compiling() const { return compile != nullptr; }
};
void collect_shader(Shader& shader, uint64_t frame);
void wait_shader(Shader& shader, uint64_t frame);
void publish_finished_shaders(uint64_t frame);
void select_renderer();

LatteFetchShader* get_fetch_shader(const uint32_t* regs, uint64_t* keyOut, uint64_t frame = ~uint64_t{0});

Shader* translate(const uint32_t* regs, bool vertex, LatteFetchShader* fetchShader, uint64_t fsKey,
    uint64_t frame = ~uint64_t{0}, uint64_t stateGeneration = ~uint64_t{0}, const Shader* linkedVs = nullptr);

struct ShaderStats {
    uint64_t stateHashLookups = 0, stateHashMemoHits = 0, stateHashBytes = 0;
    uint64_t fetchLookups = 0, fetchLastHits = 0;
    uint64_t lookups = 0, lastHits = 0, variantHits = 0, compiles = 0, compileNs = 0;
    uint64_t decompileNs = 0, spirvCompiles = 0, spirvCompileNs = 0, diskHits = 0, spirvReuseHits = 0;
    uint64_t diskLoads = 0, diskLoadNs = 0, diskLoadWaitNs = 0, diskSaves = 0, diskSaveNs = 0, diskSavedBytes = 0, diskSnapshotNs = 0;

    uint64_t programs = 0, shaders = 0, variantKeys = 0, variantAliases = 0, moduleAliases = 0;
    uint64_t verifyChecks = 0, verifyViolations = 0, verifySplitKeys = 0, verifyNs = 0;
};
ShaderStats shader_stats();

void start_shader_cache_load();
void checkpoint_shader_cache(uint64_t frame);
bool shader_cache_dirty();
uint64_t shader_cache_changed_frame();
bool save_shader_cache();
std::vector<uint32_t> compile_compute(const std::string& source, std::string* error = nullptr);
std::vector<uint32_t> compile_glsl(const std::string& source, bool vertex, std::string* error = nullptr);

void pack_uniforms_into(const uint32_t* regs, const Shader& shader,
    std::vector<uint8_t>& data, float scaleX = 1.0f, float scaleY = 1.0f, float orthoKx = 1.0f, float orthoKy = 1.0f);
std::vector<uint8_t> pack_uniforms(const uint32_t* regs, const Shader& shader,
    float scaleX = 1.0f, float scaleY = 1.0f, float orthoKx = 1.0f, float orthoKy = 1.0f);

void reset_shader_memoization();

void clear_shader_cache();
}
