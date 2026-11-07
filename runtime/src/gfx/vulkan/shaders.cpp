
#include "shaders.h"
#include "gfx/aspect_view.h"
#include "mods/cemu_pack.h"
#include "mods/shader_interface.h"
#include "graphic_pack_hash.h"
#include "gfx/area_sample.h"
#include "exact_state_memo.h"
#include "gfx/render_prof.h"
#include "perf_metrics.h"
#include "Cafe/HW/Latte/Core/FetchShader.h"
#include "Cafe/HW/Latte/Core/LatteShader.h"
#include "Cafe/HW/Latte/ISA/RegDefines.h"
#include "Cafe/HW/Latte/LegacyShaderDecompiler/LattePSInputDefault.h"
#include "Cafe/HW/Latte/Renderer/Vulkan/VulkanRenderer.h"
#include "gx2/gx2.h"
#include "gx2/gx2_regs.h"
#include "ppc.h"
#include "util/helpers/StringBuf.h"
#include <glslang/Public/ShaderLang.h>
#include <glslang/Public/ResourceLimits.h>
#include <glslang/SPIRV/GlslangToSpv.h>
#include <memory>
#include <filesystem>
#include <glslang/build_info.h>
#include "platform/host.h"
#include <array>
#include <algorithm>
#include <cmath>
#include <atomic>
#include <chrono>
#include <condition_variable>
#include <mutex>
#include <future>
#include <unordered_map>

#include "core/env.h"
#include "shader_job.h"
#include "shader_disk_cache.h"
LatteDecompilerShader* FinishDecompiledShader(LatteDecompilerOutput_t& output);
LatteFetchShader* LatteShaderRecompiler_createFetchShader(LatteFetchShader::CacheHash hash,
    uint32* regs, uint32* code, uint32 size);

void log_msg(const char* fmt, ...);
namespace gfxvk::vk {
using Latte::REGADDR;
namespace {
std::unordered_map<uint64_t, std::unique_ptr<Shader>> shaders;
std::unordered_map<uint64_t, LatteFetchShader*> fetchShaders;
struct ProgSlot {
    uint32_t address = 0, size = 0;
    uint64_t frame = ~uint64_t{0}, hash = 0;
    bool live = false;
};
std::array<ProgSlot, 128> programSlots;
struct LastShader {
    const uint32_t* regs = nullptr;
    LatteFetchShader* fetch = nullptr;
    uint64_t frame = 0, generation = 0, fsKey = 0;
    uint32_t primitive = 0;
    Shader* shader = nullptr;
};
LastShader lastShaders[2];
struct LastFetch {
    uint64_t frame = ~uint64_t{0}, key = 0;
    uint32_t address = 0, size = 0, count = 0;
    bool compact = false;
    LatteFetchShader* fetch = nullptr;
};
LastFetch lastFetch;
ShaderStats stats;
ExactStateMemo<3 + GPU7_PS_MAX_INPUTS + 32 + 4> stateMemo;

uint64_t hash_bytes(const void* bytes, size_t size, uint64_t hash = 0x9E3779B97F4A7C15ull) {
    const auto* p = static_cast<const uint8_t*>(bytes);
    size_t i=0;
    for (; i+8<=size; i+=8) {
        uint64_t word;
        memcpy(&word,p+i,8);
        hash=(hash^word)*0xFF51AFD7ED558CCDull;
        hash^=hash>>32;
    }
    for (; i<size; ++i) hash=(hash^p[i])*0x100000001B3ull;
    return hash^(hash>>29);
}

constexpr char cacheRecipe[] = "WWVKSC02:glsl450:vk1.1:spv1.3:noopt:resources1:strictmul1:"
    GLSLANG_VERSION_FLAVOR;
constexpr size_t maxCacheBytes = diskcache::kMaxBytes;
constexpr size_t maxSourceBytes = diskcache::kMaxSourceBytes;
constexpr size_t maxCacheEntries = diskcache::kMaxEntries;
using DiskShader = diskcache::Entry;
std::unordered_map<uint64_t, DiskShader> diskShaders;
diskcache::WordPool diskWords;
std::unordered_map<uint64_t, std::pair<ShaderSourceId, std::shared_ptr<SpirvCompile>>> pendingCompiles;
std::vector<Shader*> pendingShaderRecords;
std::string diskPath;
std::atomic<bool> diskInitialized{false};
bool diskDirty = false;
uint64_t diskChangedFrame = 0, diskAttemptFrame = 0, diskRevision = 0;
size_t diskBytes = diskcache::kHeaderBytes;
uint64_t cache_checksum(const uint8_t* p, size_t n) { return diskcache::checksum(p, n); }
uint64_t cache_fingerprint() {
    return cache_checksum(reinterpret_cast<const uint8_t*>(cacheRecipe),sizeof(cacheRecipe)) ^
        (uint64_t(GLSLANG_VERSION_MAJOR)<<48) ^ (uint64_t(GLSLANG_VERSION_MINOR)<<32) ^
        (uint64_t(GLSLANG_VERSION_PATCH)<<16);
}
uint64_t source_cache_key(const std::string& text, bool vertex) {
    return cache_checksum(reinterpret_cast<const uint8_t*>(text.data()), text.size()) ^
        cache_fingerprint() ^ (vertex ? 0x9E3779B97F4A7C15ull : 0xD1B54A32D192ED03ull);
}
struct DiskLoadResult {
    diskcache::Map shaders;
    diskcache::WordPool words;
    std::string path;
    size_t bytes = diskcache::kHeaderBytes;
    uint64_t loadNs = 0;
    bool loaded = false;
    bool legacy = false;
};
std::mutex diskLoadMu;
std::condition_variable diskLoadCv;
std::future<DiskLoadResult> diskLoadJob;
bool diskLoadStarted = false;
DiskLoadResult read_shader_disk(std::string path) {
    DiskLoadResult result;
    result.path = std::move(path);
    const auto start = std::chrono::steady_clock::now();
    try { host::set_thread_name("Vulkan shaders"); } catch (...) {}
    try {
        if (!result.path.empty()) {
            std::error_code ec;
            const auto length = std::filesystem::file_size(result.path, ec);
            if (!ec && length >= 40 && length <= maxCacheBytes) {
                std::vector<uint8_t> bytes(static_cast<size_t>(length));
                if (FILE* file = fopen(result.path.c_str(), "rb")) {
                    const bool read = fread(bytes.data(), 1, bytes.size(), file) == bytes.size() &&
                        fgetc(file) == EOF && !ferror(file);
                    fclose(file);
                    if (read) {
                        const auto decoded = diskcache::decode(bytes, cache_fingerprint(), result.shaders, &result.words);
                        if (decoded.ok) {
                            result.loaded = true;
                            result.bytes = decoded.bytes;
                            result.legacy = decoded.legacy;
                        } else {
                            result.shaders.clear();
                            result.words = {};
                        }
                    }
                }
            }
        }
    } catch (...) { result.shaders.clear(); result.loaded = false; }
    result.loadNs = std::chrono::duration_cast<std::chrono::nanoseconds>(
        std::chrono::steady_clock::now() - start).count();
    return result;
}
std::string shader_disk_path() {
    const char* overridePath = env::VK_SHADER_CACHE();
    const char* general = env::SHADER_CACHE();
    if ((overridePath && !strcmp(overridePath, "0")) ||
        (!overridePath && general && !strcmp(general, "0"))) return {};
    const std::string dir = overridePath ? overridePath : host::config_dir() + "/shadercache/vulkan-shaders";
    if (dir.empty()) return {};
    return dir + "/spirv.bin";
}
void apply_disk_load(DiskLoadResult result) {
    diskPath = std::move(result.path);
    if (result.loaded) {
        diskShaders = std::move(result.shaders);
        diskWords = std::move(result.words);
        diskBytes = result.bytes;
        ++stats.diskLoads;
        if (result.legacy) {
            ++diskRevision;
            diskDirty = !diskPath.empty();
            diskChangedFrame = 0;
        }
    }
    stats.diskLoadNs += result.loadNs;
    diskInitialized.store(true, std::memory_order_release);
}
void initialize_disk_cache() {
    if (diskInitialized.load(std::memory_order_acquire)) return;
    std::future<DiskLoadResult> job;
    {
        std::unique_lock lock(diskLoadMu);
        if (diskInitialized.load(std::memory_order_relaxed)) return;
        if (!diskLoadStarted) {
            diskLoadJob = std::async(std::launch::async, read_shader_disk, shader_disk_path());
            diskLoadStarted = true;
        }
        if (diskLoadJob.valid()) job = std::move(diskLoadJob);
        else {
            diskLoadCv.wait(lock, [] { return diskInitialized.load(std::memory_order_relaxed); });
            return;
        }
    }
    const auto waitStart = std::chrono::steady_clock::now();
    DiskLoadResult result = job.get();
    stats.diskLoadWaitNs += std::chrono::duration_cast<std::chrono::nanoseconds>(
        std::chrono::steady_clock::now() - waitStart).count();
    {
        std::lock_guard lock(diskLoadMu);
        if (!diskInitialized.load(std::memory_order_relaxed)) apply_disk_load(std::move(result));
    }
    diskLoadCv.notify_all();
}

uint64_t program_hash(uint32_t address, uint32_t size, uint64_t frame) {
    if (frame == ~uint64_t{0}) return hash_bytes(ppc_ptr(address), size);
    const uint32_t start = (address >> 8) & (programSlots.size() - 1);
    for (uint32_t probe = 0; probe < 4; ++probe) {
        ProgSlot& entry = programSlots[(start + probe) & (programSlots.size() - 1)];
        if (entry.live && entry.address == address && entry.size == size) {
            if (entry.frame != frame) {
                entry.hash = hash_bytes(ppc_ptr(address), size);
                entry.frame = frame;
            }
            return entry.hash;
        }
        if (!entry.live) {
            entry = {address, size, frame, hash_bytes(ppc_ptr(address), size), true};
            return entry.hash;
        }
    }
    ProgSlot& entry = programSlots[start];
    entry = {address, size, frame, hash_bytes(ppc_ptr(address), size), true};
    return entry.hash;
}

constexpr size_t kLinkageWords = 3 + GPU7_PS_MAX_INPUTS + 32 + 4;
size_t gather_linkage(const uint32_t* regs, bool vertex, const LatteFetchShader* fetch, uint32_t* out) {
    size_t count = 0;
    uint32_t flags = (regs[REGADDR::VGT_GS_MODE] & 3) != 0 ? 1u : 0u;
    if (vertex) {

        uint32_t attributes = 0;
        if (fetch)
            for (const auto& group : fetch->bufferGroups) attributes += group.attribCount;
        if (attributes <= 4 * 32) {
            struct SemLut { uint32_t words[32]; uint8_t loc[256]; bool ready; };
            static SemLut semLut{};
            const uint32_t* sem = regs + mmSQ_VTX_SEMANTIC_0;
            if (!semLut.ready || memcmp(semLut.words, sem, sizeof semLut.words) != 0) {
                memcpy(semLut.words, sem, sizeof semLut.words);
                memset(semLut.loc, 0xFF, sizeof semLut.loc);
                for (uint32_t f = 0; f < 32; ++f) {
                    uint32_t id = semLut.words[f];
                    if (id < 256 && semLut.loc[id] == 0xFF) semLut.loc[id] = uint8_t(f);
                }
                semLut.ready = true;
            }
            uint32_t packed = 0, bytes = 0;
            if (fetch)
                for (const auto& group : fetch->bufferGroups)
                    for (int i = 0; i < group.attribCount; ++i) {
                        uint32_t id = group.attrib[i].semanticId;
                        uint32_t location = id < 256 ? semLut.loc[id] : 0xFFu;
                        packed |= location << (8 * (bytes++ & 3));
                        if ((bytes & 3) == 0) { out[count++] = packed; packed = 0; }
                    }
            if (bytes & 3) out[count++] = packed;
            out[count++] = attributes;
        } else {
            std::memcpy(out + count, regs + mmSQ_VTX_SEMANTIC_0, 32 * sizeof(uint32_t));
            count += 32;
        }
        const uint32_t vte = regs[REGADDR::PA_CL_VTE_CNTL];
        const uint32_t primitive = regs[REGADDR::VGT_PRIMITIVE_TYPE] & 0x3F;
        flags |= (primitive == 1 ? 2u : 0u) | (regs[mmVGT_STRMOUT_EN] ? 4u : 0u) |
                 ((vte & 0x15) != 0x15 ? 8u : 0u) |
                 (regs[REGADDR::PA_CL_CLIP_CNTL] & (1u << 19) ? 16u : 0u);
        if (mods::cemu::has_shaders()) {

            out[count++] = vte;
            out[count++] = primitive == 0x11 ? 1u : 0u;
        }
    } else {

        const uint32_t control0 = regs[mmSPI_PS_IN_CONTROL_0];
        out[count++] = control0 & 0x03FFFD3Fu;
        const uint32_t inputs = std::min<uint32_t>(control0 & 0x3F, GPU7_PS_MAX_INPUTS);
        for (uint32_t i = 0; i < inputs; ++i) out[count++] = regs[mmSPI_PS_INPUT_CNTL_0 + i] & 0x14FFu;
        out[count++] = regs[mmSPI_PS_IN_CONTROL_1] & 0x1FF00u;
        out[count++] = regs[mmCB_SHADER_MASK];
        const uint32_t alpha = regs[REGADDR::SX_ALPHA_TEST_CONTROL];
        out[count++] = alpha & 8 ? alpha & 0xF : 0;
        flags |= regs[mmSPI_INTERP_CONTROL_0] & (1u << 1) ? 32u : 0u;
    }
    out[count++] = flags;
    return count;
}

struct ProgramUse {
    bool known = false;
    uint8_t unitCount = 0;
    std::array<uint8_t, LATTE_NUM_MAX_TEX_UNITS> units{};
    uint32_t exports = 0;
    bool streamout = false;
    Shader* failed = nullptr;
};
std::unordered_map<uint64_t, ProgramUse> programUses;
std::unordered_map<uint64_t, Shader*> variants;
std::unordered_multimap<uint64_t, Shader*> shadersByOutput;
std::unordered_multimap<uint64_t, Shader*> shadersByModule;

struct PsLink {
    bool linked = false;
    std::bitset<256> exports;
    uint32_t unfed = 0;
};
PsLink ps_link(const uint32_t* regs, const Shader* vs) {
    PsLink link;
    if (!vs || !vs->dec) return link;
    link.linked = true;
    const uint32_t mask = vs->dec->outputParameterMask;
    for (uint32_t i = 0; i < 32; ++i)
        if (mask & (1u << i)) link.exports.set((regs[mmSPI_VS_OUT_ID_0 + i / 4] >> (8 * (i % 4))) & 0xFF);
    const uint32_t control0 = regs[mmSPI_PS_IN_CONTROL_0];
    const uint32_t inputs = std::min<uint32_t>(control0 & 0x3F, GPU7_PS_MAX_INPUTS);
    const uint32_t position = (control0 >> 8) & 1 ? (control0 >> 10) & 0x1F : 0xFFFFFFFFu;
    for (uint32_t f = 0; f < inputs; ++f)
        if (f != position && !link.exports.test(regs[mmSPI_PS_INPUT_CNTL_0 + f] & 0xFF)) link.unfed |= 1u << f;
    return link;
}
uint64_t variant_hash(const uint32_t* regs, bool vertex, const ProgramUse& use, uint64_t linkage,
                      const PsLink& link) {
    std::array<uint32_t, LATTE_NUM_MAX_TEX_UNITS + 32 + 4 + 1 + GPU7_PS_MAX_INPUTS> words;
    size_t count = 0;
    const uint32_t base = vertex ? REGADDR::SQ_TEX_RESOURCE_WORD0_N_VS : REGADDR::SQ_TEX_RESOURCE_WORD0_N_PS;
    for (uint32_t i = 0; i < use.unitCount; ++i) {
        const uint32_t* w = regs + base + use.units[i] * 7;

        words[count++] = (w[0] & 7) | (((w[4] >> 8) & 3) == 1 ? 8u : 0u);
    }
    if (vertex) {

        if (use.exports) {
            const uint32_t control0 = regs[mmSPI_PS_IN_CONTROL_0];
            const uint32_t inputs = std::min<uint32_t>(control0 & 0x3F, GPU7_PS_MAX_INPUTS);
            const uint32_t position = (control0 >> 8) & 1 ? (control0 >> 10) & 0x1F : 0xFFFFFFFFu;
            for (uint32_t i = 0; i < 32; ++i) {
                if (!(use.exports & (1u << i))) continue;
                const uint32_t semantic = (regs[mmSPI_VS_OUT_ID_0 + i / 4] >> (8 * (i % 4))) & 0xFF;
                uint32_t word = 0x7FFFFFFFu;
                for (uint32_t f = 0; f < inputs; ++f) {
                    if (f == position) continue;
                    const uint32_t control = regs[mmSPI_PS_INPUT_CNTL_0 + f];
                    if ((control & 0xFF) != semantic) continue;
                    word = 0x80000000u | semantic | (f << 8) | (control & (1u << 10) ? 1u << 16 : 0) |
                           (control & (1u << 12) ? 1u << 17 : 0);
                    break;
                }
                words[count++] = word;
            }
        }
        if (use.streamout)
            for (uint32_t buffer = 0; buffer < 4; ++buffer)
                words[count++] = regs[mmVGT_STRMOUT_VTX_STRIDE_0 + buffer * 4];
    } else {

        words[count++] = link.linked ? link.unfed : 0xFFFFFFFFu;
        for (uint32_t f = 0; f < GPU7_PS_MAX_INPUTS; ++f)
            if (link.unfed & (1u << f)) words[count++] = (regs[mmSPI_PS_INPUT_CNTL_0 + f] >> 8) & 3;
    }
    return hash_bytes(words.data(), count * sizeof(uint32_t), linkage ^ 0xC2B2AE3D27D4EB4Full);
}

uint64_t legacy_state_hash(const uint32_t* regs, uint64_t hash, bool vertex) {
    std::array<uint32_t,105+5*LATTE_NUM_MAX_TEX_UNITS> state;
    size_t count=0;
    auto append = [&](const uint32_t* words,size_t length) {
        memcpy(state.data()+count,words,length*sizeof(uint32_t));
        count+=length;
    };
    auto put = [&](uint32_t first,uint32_t length) { append(regs+first,length); };
    put(mmSQ_VTX_SEMANTIC_0, 32);
    put(mmSPI_VS_OUT_ID_0, 10);
    put(mmSPI_VS_OUT_CONFIG, 1); put(mmPA_CL_VS_OUT_CNTL, 1);
    put(mmSPI_PS_IN_CONTROL_0, 2); put(mmSPI_PS_INPUT_CNTL_0, 32);
    uint32_t primitiveState[] = {regs[REGADDR::VGT_PRIMITIVE_TYPE] & 0x3F,
        regs[mmSPI_INTERP_CONTROL_0] & (1u << 1), regs[mmVGT_STRMOUT_EN]};
    append(primitiveState,3);
    if (regs[mmVGT_STRMOUT_EN])
        for (uint32_t buffer = 0; buffer < 4; ++buffer)
            put(mmVGT_STRMOUT_VTX_STRIDE_0 + buffer * 4, 1);
    put(REGADDR::VGT_GS_MODE, 1); put(REGADDR::SQ_CONFIG, 1); put(mmCB_SHADER_MASK, 1);
    put(mmCB_SHADER_CONTROL, 1); put(mmDB_SHADER_CONTROL, 1);
    put(mmSPI_INPUT_Z, 1); put(REGADDR::SX_ALPHA_TEST_CONTROL, 1);
    uint32_t transformState[] = {regs[REGADDR::PA_CL_VTE_CNTL] & 0x3F,
        regs[REGADDR::PA_CL_CLIP_CNTL] & (1u << 19),
        regs[REGADDR::DB_DEPTH_CONTROL] & 0x83};
    append(transformState,3);
    put(REGADDR::CB_COLOR_CONTROL, 1); put(REGADDR::CB_TARGET_MASK, 1);
    put(mmCB_COLOR0_INFO, 8);
    uint32_t base = vertex ? REGADDR::SQ_TEX_RESOURCE_WORD0_N_VS : REGADDR::SQ_TEX_RESOURCE_WORD0_N_PS;
    for (uint32_t t = 0; t < LATTE_NUM_MAX_TEX_UNITS; ++t) {
        const auto* w = regs + base + t * 7;
        uint32_t relevant[] = {(w[0] & 7) | (w[4] & 0x300), w[1] & 0x3F00000};
        append(relevant,2);
    }
    for (uint32_t t = 0; t < LATTE_NUM_MAX_TEX_UNITS * 3; ++t) {
        uint32_t compare = regs[REGADDR::SQ_TEX_SAMPLER_WORD0_0 + t * 3] & 0xF8000000;
        append(&compare,1);
    }
    return hash_bytes(state.data(),count*sizeof(uint32_t),hash);
}

enum MissGroup : uint8_t {
    kMgSemantic, kMgVsOutId, kMgVsOutConfig, kMgVsOutCntl, kMgPsInControl, kMgPsInputCntl, kMgPrimitive,
    kMgPointSprite, kMgStreamout, kMgGsMode, kMgSqConfig, kMgCbShaderMask, kMgCbShaderControl,
    kMgDbShaderControl, kMgInputZ, kMgAlphaTest, kMgVteCntl, kMgClipCntl, kMgDepthControl, kMgCbColorControl,
    kMgTargetMask, kMgColorInfo, kMgTexUsed, kMgTexUnused, kMgSamplerUsed, kMgSamplerUnused, kMgFetch, kMgCount
};
const char* const missGroupNames[kMgCount] = {
    "VTX_SEMANTIC", "SPI_VS_OUT_ID", "SPI_VS_OUT_CONFIG", "PA_CL_VS_OUT_CNTL", "SPI_PS_IN_CONTROL",
    "SPI_PS_INPUT_CNTL", "primitive type", "point sprite", "streamout", "VGT_GS_MODE", "SQ_CONFIG",
    "CB_SHADER_MASK", "CB_SHADER_CONTROL", "DB_SHADER_CONTROL", "SPI_INPUT_Z", "alpha test", "PA_CL_VTE_CNTL",
    "PA_CL_CLIP_CNTL", "DB_DEPTH_CONTROL", "CB_COLOR_CONTROL", "CB_TARGET_MASK", "CB_COLOR_INFO",
    "tex words (used units)", "tex words (unused units)", "sampler compare (used)", "sampler compare (unused)",
    "fetch shader"};
void miss_words(const uint32_t* regs, bool vertex, uint64_t fsKey, std::vector<uint32_t>& words,
                std::vector<uint8_t>* groups, const LatteDecompilerShader* dec) {
    words.clear();
    if (groups) groups->clear();
    auto put = [&](uint32_t value, uint8_t group) {
        words.push_back(value);
        if (groups) groups->push_back(group);
    };
    auto range = [&](uint32_t first, uint32_t n, uint8_t group) { for (uint32_t i = 0; i < n; ++i) put(regs[first + i], group); };
    range(mmSQ_VTX_SEMANTIC_0, 32, kMgSemantic); range(mmSPI_VS_OUT_ID_0, 10, kMgVsOutId);
    range(mmSPI_VS_OUT_CONFIG, 1, kMgVsOutConfig); range(mmPA_CL_VS_OUT_CNTL, 1, kMgVsOutCntl);
    range(mmSPI_PS_IN_CONTROL_0, 2, kMgPsInControl); range(mmSPI_PS_INPUT_CNTL_0, 32, kMgPsInputCntl);
    put(regs[REGADDR::VGT_PRIMITIVE_TYPE] & 0x3F, kMgPrimitive);
    put(regs[mmSPI_INTERP_CONTROL_0] & (1u << 1), kMgPointSprite);
    put(regs[mmVGT_STRMOUT_EN], kMgStreamout);
    for (uint32_t b = 0; b < 4; ++b) put(regs[mmVGT_STRMOUT_EN] ? regs[mmVGT_STRMOUT_VTX_STRIDE_0 + b * 4] : 0, kMgStreamout);
    range(REGADDR::VGT_GS_MODE, 1, kMgGsMode); range(REGADDR::SQ_CONFIG, 1, kMgSqConfig);
    range(mmCB_SHADER_MASK, 1, kMgCbShaderMask); range(mmCB_SHADER_CONTROL, 1, kMgCbShaderControl);
    range(mmDB_SHADER_CONTROL, 1, kMgDbShaderControl); range(mmSPI_INPUT_Z, 1, kMgInputZ);
    range(REGADDR::SX_ALPHA_TEST_CONTROL, 1, kMgAlphaTest);
    put(regs[REGADDR::PA_CL_VTE_CNTL] & 0x3F, kMgVteCntl);
    put(regs[REGADDR::PA_CL_CLIP_CNTL] & (1u << 19), kMgClipCntl);
    put(regs[REGADDR::DB_DEPTH_CONTROL] & 0x83, kMgDepthControl);
    range(REGADDR::CB_COLOR_CONTROL, 1, kMgCbColorControl); range(REGADDR::CB_TARGET_MASK, 1, kMgTargetMask);
    range(mmCB_COLOR0_INFO, 8, kMgColorInfo);
    std::array<bool, LATTE_NUM_MAX_TEX_UNITS> usedUnit{};
    std::array<bool, LATTE_NUM_MAX_TEX_UNITS * 3> usedSampler{};
    if (dec)
        for (int i = 0; i < dec->textureUnitListCount; ++i) {
            const uint32_t unit = dec->textureUnitList[i];
            if (unit >= LATTE_NUM_MAX_TEX_UNITS) continue;
            usedUnit[unit] = true;
            const uint32_t sampler = dec->textureUnitSamplerAssignment[unit];
            if (sampler < 18) usedSampler[(vertex ? 18 : 0) + sampler] = true;
        }
    uint32_t base = vertex ? REGADDR::SQ_TEX_RESOURCE_WORD0_N_VS : REGADDR::SQ_TEX_RESOURCE_WORD0_N_PS;
    for (uint32_t t = 0; t < LATTE_NUM_MAX_TEX_UNITS; ++t) {
        const auto* w = regs + base + t * 7;
        const uint8_t g = usedUnit[t] ? kMgTexUsed : kMgTexUnused;
        put((w[0] & 7) | (w[4] & 0x300), g);
        put(w[1] & 0x3F00000, g);
    }
    for (uint32_t t = 0; t < LATTE_NUM_MAX_TEX_UNITS * 3; ++t)
        put(regs[REGADDR::SQ_TEX_SAMPLER_WORD0_0 + t * 3] & 0xF8000000, usedSampler[t] ? kMgSamplerUsed : kMgSamplerUnused);
    put(uint32_t(fsKey), kMgFetch);
    put(uint32_t(fsKey >> 32), kMgFetch);
}
std::unordered_map<uint64_t, std::vector<std::vector<uint32_t>>> missVariants;
void record_shader_variant(const uint32_t* regs, bool vertex, uint64_t base, uint64_t fsKey,
                           const LatteDecompilerShader* dec) {
    if (!rprof::enabled()) return;
    std::vector<uint32_t> words;
    std::vector<uint8_t> groups;
    miss_words(regs, vertex, vertex ? fsKey : 0, words, &groups, dec);
    auto& variants = missVariants[base];
    const char* names[kMgCount];
    int count = 0;
    bool onlyUnused = false;
    if (!variants.empty()) {
        const std::vector<uint32_t>* nearest = nullptr;
        size_t best = SIZE_MAX;
        for (const auto& v : variants) {
            size_t d = 0;
            for (size_t i = 0; i < v.size() && i < words.size(); ++i) d += v[i] != words[i];
            if (d < best) { best = d; nearest = &v; }
        }
        std::array<bool, kMgCount> differs{};
        for (size_t i = 0; i < words.size() && i < nearest->size(); ++i)
            if ((*nearest)[i] != words[i]) differs[groups[i]] = true;
        onlyUnused = best > 0;
        for (int g = 0; g < kMgCount; ++g)
            if (differs[g]) {
                names[count++] = missGroupNames[g];
                if (g != kMgTexUnused && g != kMgSamplerUnused) onlyUnused = false;
            }
        if (!count) names[count++] = "none (equal words)";
    }
    rprof::shader_variant(variants.empty(), onlyUnused, names, count);
    variants.push_back(std::move(words));
}
void free_decompiler(LatteDecompilerShader* shader) {
    if (!shader) return;
    delete shader->strBuf_shaderSource;
    delete shader;
}

uint32_t key_verify_rate() {
    static const uint32_t rate = [] {
        const char* value = env::VK_SHADER_KEY_VERIFY();
        if (!value || !*value) return 0u;
        long parsed = strtol(value, nullptr, 10);
        return parsed > 0 ? uint32_t(std::min<long>(parsed, 1 << 20)) : 0u;
    }();
    return rate;
}
uint64_t output_hash(const Shader& shader) {
    uint64_t hash = hash_bytes(shader.glsl.data(), shader.glsl.size(), shader.vertex ? 0x5151 : 0xA2A2);
    hash = hash_bytes(&shader.mapping, sizeof shader.mapping, hash);
    hash = hash_bytes(&shader.uniforms, sizeof shader.uniforms, hash);
    return hash_bytes(&shader.descriptorRanks, sizeof shader.descriptorRanks, hash);
}
bool same_output(const Shader& a, const Shader& b) {
    return a.vertex == b.vertex && a.glsl == b.glsl &&
           !std::memcmp(&a.mapping, &b.mapping, sizeof a.mapping) &&
           !std::memcmp(&a.uniforms, &b.uniforms, sizeof a.uniforms) &&
           !std::memcmp(&a.descriptorRanks, &b.descriptorRanks, sizeof a.descriptorRanks);
}

std::mutex decompilerMutex;
bool decompile(Shader& shader, const uint32_t* regs, bool vertex, LatteFetchShader* fetch,
               uint32_t address, uint32_t size, uint64_t base, bool packReplacement, const PsLink& link,
               const uint8_t* program = nullptr) {
    std::lock_guard translationLock(decompilerMutex);
    const uint8_t* bytes = program ? program : static_cast<const uint8_t*>(ppc_ptr(address));
    uint8_t* data = const_cast<uint8_t*>(bytes);
    if (!g_renderer || g_renderer->GetType() != RendererAPI::Vulkan) {
        shader.error = "Vulkan renderer was not selected before shader translation"; return false;
    }
    if ((regs[Latte::REGADDR::VGT_GS_MODE] & 3) != 0) {
        shader.error = "geometry shaders are not implemented by this Vulkan backend"; return false;
    }
    if (vertex && !fetch) { shader.error = "vertex shader has no fetch program"; return false; }
    LatteShader_UpdatePSInputs(const_cast<uint32_t*>(regs));
    LatteDecompilerOptions options;
    if (!vertex && link.linked) {
        options.linkPSInputsToVS = true;
        options.vsOutputSemantics = link.exports;
    }

    static const bool strictMul = !env::STRICT_MUL() || strcmp(env::STRICT_MUL(), "0");
    options.strictMul = strictMul;
    uint64_t packBase=0;
    if(mods::cemu::has_shaders()) {
        packBase=cemu_pack_hash::base(data,size,regs,vertex,fetch);
        options.legacyGraphicPackUniforms=!vertex&&mods::cemu::legacy_pixel_uniforms(packBase);
    }
    const uint64_t decompilerBase=mods::cemu::has_shaders()?packBase:base;
    if (!vertex) options.areaSampledTextures = ::gfx::area_sample::units_for_pixel_shader(data, size);
    LatteDecompilerOutput_t output{};
    if (vertex) LatteDecompiler_DecompileVertexShader(decompilerBase, const_cast<uint32_t*>(regs),
        data, size, fetch, options, &output);
    else LatteDecompiler_DecompilePixelShader(decompilerBase, const_cast<uint32_t*>(regs),
        data, size, options, &output);
    if (!output.shader || output.shader->hasError || !output.shader->strBuf_shaderSource) {
        free_decompiler(output.shader); shader.error = "Latte GLSL translation failed"; return false;
    }
    if (options.areaSampledTextures && packReplacement && mods::cemu::has_shaders() &&
        !mods::cemu::shader_source(packBase, cemu_pack_hash::auxiliary(*output.shader, regs, vertex), vertex).empty()) {

        free_decompiler(output.shader);
        options.areaSampledTextures = 0;
        output = LatteDecompilerOutput_t{};
        LatteDecompiler_DecompilePixelShader(decompilerBase, const_cast<uint32_t*>(regs), data, size, options, &output);
        if (!output.shader || output.shader->hasError || !output.shader->strBuf_shaderSource) {
            free_decompiler(output.shader); shader.error = "Latte GLSL translation failed"; return false;
        }
    }
    shader.dec = FinishDecompiledShader(output);
    shader.mapping = output.resourceMappingVK;
    shader.descriptorRanks = make_descriptor_rank_plan(shader.mapping, *shader.dec);
    shader.uniforms = output.uniformOffsetsVK;
    shader.glsl = shader.dec->strBuf_shaderSource->c_str();
    if (options.areaSampledTextures && ::gfx::area_sample::rewrite(shader.glsl, options.areaSampledTextures, false) <= 0)
        fprintf(stderr, "[vulkan] pixel shader %08X: area-sampled taps not applied\n", address);
    if (vertex) {

        auto main = shader.glsl.find("void main(");
        if (main != std::string::npos)
            shader.glsl.insert(main, "invariant gl_Position;\n");
    }
    if(packReplacement && mods::cemu::has_shaders()) {
        auto packAux=cemu_pack_hash::auxiliary(*shader.dec,regs,vertex);
        auto custom=mods::cemu::shader_source(packBase,packAux,vertex);
        if(!custom.empty()) {
            if(!vertex&&link.unfed) {

                const char* values[32]={};
                for(uint32_t f=0;f<GPU7_PS_MAX_INPUTS&&f<32;f++)
                    if(link.unfed&(1u<<f))values[f]=LattePSInputDefaultGLSL(regs[mmSPI_PS_INPUT_CNTL_0+f]);
                custom=mods::cemu::const_pixel_inputs(std::move(custom),values);
            }
            std::string error;
            auto replacement=compile_glsl(custom,vertex,&error);
            std::string originalError;
            auto original=compile_glsl(shader.glsl,vertex,&originalError);
            bool accepted=!replacement.empty()&&!original.empty()&&
                mods::cemu::compatible_shader_interface(original,replacement,error);
            if(accepted && key_verify_rate()) shader.translatedGlsl=shader.glsl;
            if(accepted)shader.glsl=std::move(custom);
            if(error.empty()&&!accepted)error=originalError.empty()?"Shader compilation failed":originalError;
            mods::cemu::report_shader(packBase,packAux,vertex,accepted,error);
            fprintf(stderr,"[cemu-pack] %016llx_%016llx_%s %s%s\n",
                (unsigned long long)packBase,(unsigned long long)packAux,vertex?"vs":"ps",
                accepted?"applied":"rejected: ",accepted?"":error.c_str());
        }
    }
    return true;
}
std::unordered_map<uint64_t, uint64_t> verifiedKeys;
void first_difference(const std::string& a, const std::string& b, std::string& left, std::string& right) {
    size_t at = 0;
    while (at < a.size() && at < b.size() && a[at] == b[at]) ++at;
    auto line = [&](const std::string& s) {
        size_t begin = s.rfind('\n', at ? at - 1 : 0);
        begin = begin == std::string::npos || at == 0 ? 0 : begin + 1;
        size_t end = s.find('\n', at);
        return s.substr(begin, std::min<size_t>((end == std::string::npos ? s.size() : end) - begin, 160));
    };
    left = line(a); right = line(b);
}
void verify_key(const uint32_t* regs, bool vertex, LatteFetchShader* fetch, uint64_t fsKey,
                uint32_t address, uint32_t size, uint64_t base, uint64_t key, const Shader& shader,
                const PsLink& link) {
    const uint32_t rate = key_verify_rate();
    if (!rate || !shader.dec) return;
    uint64_t legacy = legacy_state_hash(regs, base, vertex) ^ (vertex ? fsKey * 31 : 0);
    if (vertex && mods::cemu::has_shaders())
        legacy ^= uint64_t(regs[REGADDR::PA_CL_VTE_CNTL]) * 0x9E3779B97F4A7C15ull;
    auto [it, inserted] = verifiedKeys.emplace(legacy, key);
    if (!inserted) {
        if (it->second != key) ++stats.verifySplitKeys;
        return;
    }
    if (rate > 1 && ((legacy * 0x9E3779B97F4A7C15ull) >> 40) % rate) return;
    auto started = std::chrono::steady_clock::now();
    Shader reference;
    reference.vertex = vertex;
    bool built = decompile(reference, regs, vertex, fetch, address, size, base, false, link);
    ++stats.verifyChecks;
    const std::string& translated = shader.translatedGlsl.empty() ? shader.glsl : shader.translatedGlsl;
    if (!built || reference.glsl != translated ||
        std::memcmp(&reference.mapping, &shader.mapping, sizeof shader.mapping) ||
        std::memcmp(&reference.uniforms, &shader.uniforms, sizeof shader.uniforms) ||
        std::memcmp(&reference.descriptorRanks, &shader.descriptorRanks, sizeof shader.descriptorRanks)) {
        ++stats.verifyViolations;
        std::string left, right;
        first_difference(translated, reference.glsl, left, right);
        fprintf(stderr, "[vulkan shader key] VIOLATION %s %08X key %016llx legacy %016llx: %s; cached \"%s\" vs now \"%s\"\n",
                vertex ? "vs" : "ps", address, (unsigned long long)key, (unsigned long long)legacy,
                !built ? reference.error.c_str() : reference.glsl != translated ? "GLSL differs" : "binding metadata differs",
                left.c_str(), right.c_str());
    }
    free_decompiler(reference.dec);
    stats.verifyNs += std::chrono::duration_cast<std::chrono::nanoseconds>(
        std::chrono::steady_clock::now() - started).count();
}
}

void start_shader_cache_load() {
    std::lock_guard lock(diskLoadMu);
    if (diskInitialized.load(std::memory_order_relaxed) || diskLoadStarted) return;
    diskLoadJob = std::async(std::launch::async, read_shader_disk, shader_disk_path());
    diskLoadStarted = true;
}

void select_renderer() {
    finish_shader_jobs();
    g_renderer = std::make_unique<VulkanRenderer>();
}

static std::vector<uint32_t> compile_stage(const std::string& source, EShLanguage stage, std::string* error) {
    static std::mutex glslangMutex;
    std::lock_guard glslangLock(glslangMutex);
    static std::once_flag init;
    static bool initialized = false;
    std::call_once(init, [] { initialized = glslang::InitializeProcess(); });
    if (error) error->clear();
    auto fail = [&](const std::string& reason) { if (error) *error = reason; return std::vector<uint32_t>{}; };
    if (!initialized) return fail("glslang initialization failed");
    glslang::TShader shader(stage);
    const char* text = source.c_str();
    shader.setStrings(&text, 1);
    shader.setEnvInput(glslang::EShSourceGlsl, stage, glslang::EShClientVulkan, 100);
    shader.setEnvClient(glslang::EShClientVulkan, glslang::EShTargetVulkan_1_1);
    shader.setEnvTarget(glslang::EShTargetSpv, glslang::EShTargetSpv_1_3);
    EShMessages messages = static_cast<EShMessages>(EShMsgSpvRules | EShMsgVulkanRules);
    if (!shader.parse(GetDefaultResources(), 450, false, messages))
        return fail(std::string(shader.getInfoLog()) + shader.getInfoDebugLog());
    glslang::TProgram program;
    program.addShader(&shader);
    if (!program.link(messages)) return fail(program.getInfoLog());
    std::vector<uint32_t> words;
    glslang::SpvOptions options;
    options.disableOptimizer = true;
    glslang::GlslangToSpv(*program.getIntermediate(stage), words, &options);
    if (words.empty()) return fail("glslang emitted empty SPIR-V");
    return words;
}

std::vector<uint32_t> compile_glsl(const std::string& source, bool vertex, std::string* error) {
    return compile_stage(source, vertex ? EShLangVertex : EShLangFragment, error);
}
std::vector<uint32_t> compile_compute(const std::string& source, std::string* error) {
    return compile_stage(source, EShLangCompute, error);
}

DescriptorRankPlan make_descriptor_rank_plan(const LatteDecompilerShaderResourceMapping& mapping,
                                             const LatteDecompilerShader& shader) {
    DescriptorRankPlan plan;
    static_assert(17 + LATTE_NUM_MAX_TEX_UNITS < DescriptorRankPlan::unused);
    struct Binding { int value; uint8_t kind, slot; };
    std::array<Binding, 17 + LATTE_NUM_MAX_TEX_UNITS> bindings{};
    size_t count = 0;
    auto add = [&](int value, uint8_t kind, uint8_t slot) {
        if (value >= 0) bindings[count++] = {value, kind, slot};
    };
    for (uint8_t i = 0; i < 16; ++i) add(mapping.uniformBuffersBindingPoint[i], 0, i);
    add(mapping.uniformVarsBufferBindingPoint, 1, 0);
    for (uint8_t i = 0; i < LATTE_NUM_MAX_TEX_UNITS; ++i)
        add(mapping.textureUnitToBindingPoint[i], 2, i);

    std::array<bool, LATTE_NUM_MAX_TEX_UNITS> seen{};
    if (shader.textureUnitListCount < 0 || shader.textureUnitListCount > LATTE_NUM_MAX_TEX_UNITS)
        return plan;
    for (int i = 0; i < shader.textureUnitListCount; ++i) {
        const auto unit = shader.textureUnitList[i];
        if (unit >= LATTE_NUM_MAX_TEX_UNITS) return plan;
        if (mapping.textureUnitToBindingPoint[unit] >= 0) {
            if (seen[unit]) return plan;
            seen[unit] = true;
        }
    }
    std::sort(bindings.begin(), bindings.begin() + count,
              [](const auto& a, const auto& b) { return a.value < b.value; });
    for (size_t rank = 0; rank < count; ++rank) {
        if (rank && bindings[rank-1].value == bindings[rank].value) return DescriptorRankPlan{};
        const auto& binding = bindings[rank];
        if (binding.kind == 0) plan.blocks[binding.slot] = uint8_t(rank);
        else if (binding.kind == 1) plan.support = uint8_t(rank);
        else plan.textures[binding.slot] = uint8_t(rank);
    }
    plan.count = uint8_t(count);
    plan.valid = true;
    return plan;
}

LatteFetchShader* get_fetch_shader(const uint32_t* regs, uint64_t* keyOut, uint64_t frame) {
    ++stats.fetchLookups;
    if (keyOut) *keyOut = 0;
    static const bool memo = [] {
        const char* e = env::VK_FETCH_MEMO();
        return e && !std::strcmp(e, "1");
    }();
    const bool cacheLast = memo && frame != ~uint64_t{0};
    if (!cacheLast) lastFetch = {};
    uint32_t address = regs[mmSQ_PGM_START_FS] << 8;
    if (!address) return nullptr;
    bool compact = ld32(address) == 0x57574653;
    uint32_t count = compact ? ld32(address + 4) : 0;
    if (compact && count > 64) return nullptr;
    uint32_t size = compact ? 16 + count * 16 : regs[mmSQ_PGM_START_FS + 1] << 3;
    if (!size || size > 0x1000 || uint64_t(address) + size > 0x100000000ull) return nullptr;

    if (cacheLast && lastFetch.fetch && lastFetch.frame == frame &&
        lastFetch.address == address && lastFetch.size == size &&
        lastFetch.compact == compact && lastFetch.count == count) {
        ++stats.fetchLastHits;
        if (keyOut) *keyOut = lastFetch.key;
        return lastFetch.fetch;
    }
    uint64_t key = program_hash(address, size, frame);
    if (keyOut) *keyOut = key;
    auto remember = [&](LatteFetchShader* fetch) {
        if (cacheLast && fetch)
            lastFetch = {frame, key, address, size, count, compact, fetch};
        return fetch;
    };
    if (auto it = fetchShaders.find(key); it != fetchShaders.end()) return remember(it->second);
    auto* fetch = compact ? gx2::build_fetch_shader(address)
        : LatteShaderRecompiler_createFetchShader(key, const_cast<uint32_t*>(regs),
                                                  reinterpret_cast<uint32_t*>(ppc_ptr(address)), size);
    fetchShaders.emplace(key, fetch);
    return remember(fetch);
}

void release_pending_compile(uint64_t key, const std::shared_ptr<SpirvCompile>& compile) {
    auto it = pendingCompiles.find(key);
    if (it != pendingCompiles.end() && it->second.second == compile)
        pendingCompiles.erase(it);
}
void assign_pipeline_module(Shader& shader) {
    shader.pipelineId = shader.key;
    const auto& words = shader.spirv();
    if (words.empty()) return;
    uint64_t module = hash_bytes(words.data(), words.size() * 4, shader.vertex ? 0x77 : 0x88);
    module = hash_bytes(&shader.mapping, sizeof shader.mapping, module);
    for (auto [it, end] = shadersByModule.equal_range(module); it != end; ++it) {
        if (it->second == &shader) continue;
        if (it->second->spirv() == words &&
            !std::memcmp(&it->second->mapping, &shader.mapping, sizeof shader.mapping)) {
            shader.pipelineId = it->second->pipelineId;
            ++stats.moduleAliases;
            return;
        }
    }
    shadersByModule.emplace(module, &shader);
}
void remember_spirv(Shader& shader, uint64_t frame) {
    if (!shader.spirvWords || shader.sourceId.length > maxSourceBytes ||
        !diskcache::spirv_valid(*shader.spirvWords)) return;
    auto cached = diskShaders.find(shader.spirvKey);
    if (cached == diskShaders.end() && diskShaders.size() >= maxCacheEntries) return;
    if (cached != diskShaders.end() && cached->second.vertex == shader.vertex &&
        cached->second.source == shader.sourceId && cached->second.words &&
        *cached->second.words == *shader.spirvWords) {
        shader.spirvWords = cached->second.words;
        return;
    }
    bool fresh = false;
    shader.spirvWords = diskWords.intern(shader.spirvWords, fresh);
    const size_t oldBytes = cached == diskShaders.end() ? 0 : diskcache::record_bytes(0, false);
    const size_t newBytes = diskcache::record_bytes(shader.spirvWords->size(), fresh);
    if (diskBytes < oldBytes || newBytes > maxCacheBytes - (diskBytes - oldBytes)) return;
    DiskShader entry;
    entry.source = shader.sourceId;
    entry.words = shader.spirvWords;
    entry.vertex = shader.vertex;
    diskShaders.insert_or_assign(shader.spirvKey, std::move(entry));
    diskBytes = diskBytes - oldBytes + newBytes;
    ++diskRevision;
    diskDirty = !diskPath.empty();
    diskChangedFrame = frame;
}
void collect_shader(Shader& shader, uint64_t frame) {
    if (!shader.compile) return;
    SpirvWords words;
    std::string error;
    uint64_t ns = 0;
    bool account = false;
    std::shared_ptr<SpirvCompile> compile;
    {
        std::unique_lock lock(shader.compile->mutex);
        if (!shader.compile->done) return;
        compile = shader.compile;
        words = shader.compile->words;
        error = shader.compile->error;
        ns = shader.compile->ns;
        account = !shader.compile->accounted;
        shader.compile->accounted = true;
    }
    shader.spirvWords = std::move(words);
    shader.error = std::move(error);
    shader.compile.reset();
    if (account) {
        ++stats.spirvCompiles;
        stats.spirvCompileNs += ns;
        stats.compileNs += ns;
        perf::shader_recomp(double(ns) / 1e6);
    }
    initialize_disk_cache();
    remember_spirv(shader, frame);
    assign_pipeline_module(shader);
    release_pending_compile(shader.spirvKey, compile);
    if (!shader.error.empty())
        fprintf(stderr, "[vulkan] shader %016llX: %s\n", (unsigned long long)shader.key, shader.error.c_str());
}
void poll_pending_shaders(uint64_t frame) {
    size_t retained = 0;
    for (auto* shader : pendingShaderRecords) {
        if (shader->compile) collect_shader(*shader, frame);
        if (shader->compile) pendingShaderRecords[retained++] = shader;
    }
    pendingShaderRecords.resize(retained);
}

Shader* translate(const uint32_t* regs, bool vertex, LatteFetchShader* fetch, uint64_t fsKey,
                  uint64_t frame, uint64_t stateGeneration, const Shader* linkedVs) {
    poll_pending_shaders(frame);
    ++stats.lookups;
    auto& last = lastShaders[vertex ? 0 : 1];
    uint32_t primitive = vertex ? regs[REGADDR::VGT_PRIMITIVE_TYPE] & 0x3F : 0;
    bool cacheLast = frame != ~uint64_t{0} && stateGeneration != ~uint64_t{0};
    if (!cacheLast) { last = {}; stateMemo.reset(); }
    if (cacheLast && last.shader && last.regs == regs && last.frame == frame &&
        last.generation == stateGeneration && last.fsKey == fsKey &&
        last.fetch == fetch && (!vertex || last.primitive == primitive)) {
        ++stats.lastHits;
        collect_shader(*last.shader, frame);
        return last.shader;
    }
    auto remember = [&](Shader* shader) {
        if (cacheLast) last = {regs, fetch, frame, stateGeneration, fsKey, primitive, shader};
        return shader;
    };
    uint32_t start = vertex ? mmSQ_PGM_START_VS : mmSQ_PGM_START_PS;
    uint32_t address = regs[start] << 8, size = regs[start + 1] << 3;
    if (!address || !size || size > 0x100000 || uint64_t(address) + size > 0x100000000ull) return nullptr;
    uint64_t base = program_hash(address, size, frame) ^ (vertex ? 0x1111 : 0x2222);

    std::array<uint32_t, kLinkageWords> words;
    const size_t count = gather_linkage(regs, vertex, fetch, words.data());
    const uint64_t seed = base ^ (vertex ? fsKey * 31 : 0);
    ++stats.stateHashLookups;
    static const bool memoEnabled = [] {
        const char* value = env::VK_SHADER_STATE_MEMO();
        return value && !strcmp(value,"1");
    }();
    uint64_t linkage;
    if (memoEnabled && cacheLast && stateMemo.find(vertex, words.data(), count, seed, linkage)) {
        ++stats.stateHashMemoHits;
    } else {
        stats.stateHashBytes += count * sizeof(uint32_t);
        linkage = hash_bytes(words.data(), count * sizeof(uint32_t), seed);
        if (memoEnabled && cacheLast) stateMemo.remember(vertex, words.data(), count, seed, linkage);
    }

    const PsLink link = vertex ? PsLink{} : ps_link(regs, linkedVs);
    auto& use = programUses[linkage];
    if (use.failed) return remember(use.failed);
    if (use.known) {
        const uint64_t key = variant_hash(regs, vertex, use, linkage, link);
        if (auto it = variants.find(key); it != variants.end()) {
            ++stats.variantHits;
            collect_shader(*it->second, frame);
            verify_key(regs, vertex, fetch, fsKey, address, size, base, key, *it->second, link);
            return remember(it->second);
        }
    }
    auto owned = std::make_unique<Shader>();
    Shader* shader = owned.get();
    shader->vertex = vertex;
    const bool background = frame != ~uint64_t{0} && shader_jobs_async();
    const auto started = std::chrono::steady_clock::now();
    const bool decompiled = decompile(*shader, regs, vertex, fetch, address, size, base, true, link);
    const uint64_t decompileNs = std::chrono::duration_cast<std::chrono::nanoseconds>(
        std::chrono::steady_clock::now() - started).count();
    ++stats.compiles;
    stats.compileNs += decompileNs;
    if (!decompiled) {

        shader->key = shader->pipelineId = linkage ^ 0xFA17EDull;
        use.failed = shader;
        shaders.emplace(shader->key, std::move(owned));
        fprintf(stderr, "[vulkan] shader %08X: %s\n", address, shader->error.c_str());
        return remember(shader);
    }
    record_shader_variant(regs, vertex, base, fsKey, shader->dec);
    if (!use.known) {
        const auto& dec = *shader->dec;
        use.unitCount = uint8_t(std::clamp(int(dec.textureUnitListCount), 0, int(LATTE_NUM_MAX_TEX_UNITS)));
        for (uint32_t i = 0; i < use.unitCount; ++i)
            use.units[i] = std::min<uint8_t>(dec.textureUnitList[i], LATTE_NUM_MAX_TEX_UNITS - 1);
        use.exports = vertex ? dec.outputParameterMask : 0;
        use.streamout = vertex && dec.hasStreamoutBufferWrite;
        use.known = true;
    }
    const uint64_t key = variant_hash(regs, vertex, use, linkage, link);
    if (link.unfed) {
        static int reported = 0;
        if (reported < 20) {
            ++reported;
            ::log_msg("[vulkan] pixel shader %08X: %d inputs without a vertex shader output read their default value",
                      address, __builtin_popcount(link.unfed));
        }
    }
    stats.decompileNs += decompileNs;
    perf::shader_recomp(double(decompileNs) / 1e6);
    if (key_verify_rate()) {
        uint64_t legacy = legacy_state_hash(regs, base, vertex) ^ (vertex ? fsKey * 31 : 0);
        if (vertex && mods::cemu::has_shaders())
            legacy ^= uint64_t(regs[REGADDR::PA_CL_VTE_CNTL]) * 0x9E3779B97F4A7C15ull;
        verifiedKeys.emplace(legacy, key);
    }

    const uint64_t output = output_hash(*shader);
    for (auto [it, end] = shadersByOutput.equal_range(output); it != end; ++it)
        if (same_output(*it->second, *shader)) {
            ++stats.variantAliases;
            free_decompiler(shader->dec);
            variants.emplace(key, it->second);
            return remember(it->second);
        }
    shader->key = key;
    shaders.emplace(key, std::move(owned));
    variants.emplace(key, shader);
    shadersByOutput.emplace(output, shader);
    remember(shader);
    initialize_disk_cache();
    shader->sourceId = shader_source_id(shader->glsl);
    shader->spirvKey = source_cache_key(shader->glsl, vertex);
    auto cached = diskShaders.find(shader->spirvKey);
    if (cached != diskShaders.end() && cached->second.vertex == vertex &&
        cached->second.source == shader->sourceId && cached->second.words) {
        shader->spirvWords = cached->second.words;
        if (cached->second.fromDisk) ++stats.diskHits;
        else ++stats.spirvReuseHits;
        assign_pipeline_module(*shader);
    } else if (background) {
        auto pending = pendingCompiles.find(shader->spirvKey);
        if (pending != pendingCompiles.end() && pending->second.first == shader->sourceId)
            shader->compile = pending->second.second;
        if (shader->compile) {
            ++stats.spirvReuseHits;
        } else {
            shader->compile = std::make_shared<SpirvCompile>();
            pendingCompiles.insert_or_assign(shader->spirvKey,
                std::make_pair(shader->sourceId, shader->compile));
            pendingShaderRecords.push_back(shader);
            enqueue_spirv_compile(shader->glsl, vertex, shader->compile);
        }
        shader->pipelineId = shader->key;
    } else {
        const auto compileStart = std::chrono::steady_clock::now();
        auto compiled = compile_glsl(shader->glsl, vertex, &shader->error);
        shader->spirvWords = compiled.empty() ? nullptr :
            std::make_shared<const std::vector<uint32_t>>(std::move(compiled));
        const uint64_t spirvNs = std::chrono::duration_cast<std::chrono::nanoseconds>(
            std::chrono::steady_clock::now() - compileStart).count();
        ++stats.spirvCompiles;
        stats.spirvCompileNs += spirvNs;
        stats.compileNs += spirvNs;
        perf::shader_recomp(double(spirvNs) / 1e6);
        remember_spirv(*shader, frame);
        assign_pipeline_module(*shader);
    }
    if (!shader->error.empty()) fprintf(stderr, "[vulkan] shader %08X: %s\n", address, shader->error.c_str());
    return shader;
}

void publish_finished_shaders(uint64_t frame) {
    poll_pending_shaders(frame);
}

namespace {
using DiskSnapshot = std::vector<std::pair<uint64_t,DiskShader>>;
struct DiskSaveResult { uint64_t revision=0, bytes=0, ns=0; bool saved=false; };
std::future<DiskSaveResult> diskSaveJob;
DiskSnapshot snapshot_disk_shaders() {
    auto start=std::chrono::steady_clock::now();
    DiskSnapshot snapshot;
    snapshot.reserve(diskShaders.size());
    for(const auto& entry:diskShaders) snapshot.push_back(entry);
    stats.diskSnapshotNs+=std::chrono::duration_cast<std::chrono::nanoseconds>(
        std::chrono::steady_clock::now()-start).count();
    return snapshot;
}

DiskSaveResult write_shader_snapshot(DiskSnapshot snapshot,std::string path,uint64_t revision) {
    auto start=std::chrono::steady_clock::now();
    DiskSaveResult result; result.revision=revision;
    auto finish=[&] {
        result.ns=std::chrono::duration_cast<std::chrono::nanoseconds>(
            std::chrono::steady_clock::now()-start).count();
        return result;
    };
    std::string temporary;
    try {
        const auto bytes = diskcache::encode(snapshot, cache_fingerprint());
        if (bytes.empty()) return finish();
        std::error_code ec;
        auto parent=std::filesystem::path(path).parent_path();
        if(!parent.empty()) std::filesystem::create_directories(parent,ec);
        if(ec) return finish();
        temporary=path+".tmp-"+std::to_string(start.time_since_epoch().count());
        FILE* file=fopen(temporary.c_str(),"wb");
        if(!file) return finish();
        bool wrote=fwrite(bytes.data(),1,bytes.size(),file)==bytes.size();
        if(fclose(file)!=0) wrote=false;
        result.saved=wrote && host::replace_file(temporary,path);
        if(result.saved) result.bytes=bytes.size();
    } catch(...) { result.saved=false; }
    if(!result.saved && !temporary.empty()) { std::error_code ec; std::filesystem::remove(temporary,ec); }
    return finish();
}
bool finish_disk_save(bool wait) {
    if(!diskSaveJob.valid() || (!wait && diskSaveJob.wait_for(std::chrono::seconds(0))!=std::future_status::ready))
        return false;
    try {
        auto result=diskSaveJob.get();
        stats.diskSaveNs+=result.ns;
        if(result.saved) {
            ++stats.diskSaves; stats.diskSavedBytes=result.bytes;
            diskDirty=diskRevision!=result.revision;
        } else diskDirty=true;
        return result.saved;
    } catch(...) { diskDirty=true; return false; }
}
}
bool shader_cache_dirty() { return diskDirty; }
uint64_t shader_cache_changed_frame() { return diskChangedFrame; }
bool save_shader_cache() {

    bool saved=finish_disk_save(true);
    if(!diskDirty || diskPath.empty()) return saved;
    try {
        auto snapshot=snapshot_disk_shaders();
        auto result=write_shader_snapshot(std::move(snapshot),diskPath,diskRevision);
        stats.diskSaveNs+=result.ns;
        if(result.saved) {
            diskDirty=false; ++stats.diskSaves; stats.diskSavedBytes=result.bytes;
        }
        return result.saved;
    } catch(...) { return false; }
}
void checkpoint_shader_cache(uint64_t frame) {
    finish_disk_save(false);
    if(diskSaveJob.valid() || !diskDirty || diskPath.empty() || diskChangedFrame==~uint64_t{0} ||
       frame<diskChangedFrame || frame-diskChangedFrame<120 ||
       frame<diskAttemptFrame || frame-diskAttemptFrame<120) return;
    diskAttemptFrame=frame;
    try {
        auto snapshot=snapshot_disk_shaders();
        diskSaveJob=std::async(std::launch::async,write_shader_snapshot,
                              std::move(snapshot),diskPath,diskRevision);
    } catch(...) {  }
}

ShaderStats shader_stats() {
    ShaderStats result = stats;
    result.programs = programUses.size();
    result.shaders = shaders.size();
    result.variantKeys = variants.size();
    return result;
}

void pack_uniforms_into(const uint32_t* regs, const Shader& shader,
                        std::vector<uint8_t>& data, float scaleX, float scaleY, float orthoKx, float orthoKy) {
    const auto& offsets = shader.uniforms;
    data.resize((std::max(offsets.offset_endOfBlock, 16) + 15) & ~15);

    std::fill(data.begin(), data.end(), uint8_t{0});
    if (!shader.dec) return;
    auto copy = [&](int offset, const void* src, size_t size) {
        if (offset >= 0 && size_t(offset) + size <= data.size()) memcpy(data.data() + offset, src, size);
    };
    auto put = [&](int offset, float value) { copy(offset, &value, sizeof value); };
    auto bitsf = [](uint32_t value) { float result; memcpy(&result, &value, 4); return result; };
    uint32_t aluBase = mmSQ_ALU_CONSTANT0_0 + (shader.vertex ? 0x400 : 0);
    uint32_t blockBase = shader.vertex ? mmSQ_VTX_UNIFORM_BLOCK_START : mmSQ_PS_UNIFORM_BLOCK_START;
    if (offsets.offset_remapped >= 0) {
        for (const auto& entry : shader.dec->list_remappedUniformEntries_register)
            copy(offsets.offset_remapped + entry.mappedIndexOffset, regs + aluBase + entry.indexOffset / 4, 16);
        for (const auto& group : shader.dec->list_remappedUniformEntries_bufferGroups) {
            uint32_t address = regs[blockBase + group.kcacheBankIdOffset / 4];
            if (!address) continue;
            for (const auto& entry : group.entries)
                copy(offsets.offset_remapped + entry.mappedIndexOffset, ppc_ptr(address + entry.indexOffset), 16);
        }
    }
    if (offsets.offset_uniformRegister >= 0)
        copy(offsets.offset_uniformRegister, regs + aluBase, size_t(offsets.count_uniformRegister) * 16);
    put(offsets.offset_alphaTestRef, bitsf(regs[Latte::REGADDR::SX_ALPHA_REF]));
    float point = float(regs[Latte::REGADDR::PA_SU_POINT_SIZE] & 0xFFFF) / 8.0f;
    put(offsets.offset_pointSize, (point == 0 ? 0.125f : point) * scaleX);
    if (offsets.offset_windowSpaceToClipSpaceTransform >= 0) {
        float width = 2.0f * bitsf(regs[Latte::REGADDR::PA_CL_VPORT_XSCALE]);
        float height = -2.0f * bitsf(regs[Latte::REGADDR::PA_CL_VPORT_YSCALE]);
        put(offsets.offset_windowSpaceToClipSpaceTransform, width != 0 ? 2.0f / width : 0);
        put(offsets.offset_windowSpaceToClipSpaceTransform + 4, height != 0 ? 2.0f / height : 0);
    }
    if (offsets.offset_fragCoordScale >= 0) {
        float scale[] = {scaleX != 0 ? 1.0f / scaleX : 1.0f, scaleY != 0 ? 1.0f / scaleY : 1.0f, 0, 0};
        copy(offsets.offset_fragCoordScale, scale, sizeof scale);
    }
    for (int unit = 0; unit < LATTE_NUM_MAX_TEX_UNITS; ++unit) {
        if (offsets.offset_texScale[unit] < 0) continue;
        float scale[] = {1, 1};
        copy(offsets.offset_texScale[unit], scale, sizeof scale);
    }
    if (orthoKx != 1.0f || orthoKy != 1.0f) {
        float vw = std::fabs(2.0f * bitsf(regs[Latte::REGADDR::PA_CL_VPORT_XSCALE]));
        float vh = std::fabs(2.0f * bitsf(regs[Latte::REGADDR::PA_CL_VPORT_YSCALE]));
        aspect::view::scale_guest_ortho(reinterpret_cast<float*>(data.data()), data.size() / sizeof(float), vw, vh, orthoKx, orthoKy);
    }
}

std::vector<uint8_t> pack_uniforms(const uint32_t* regs, const Shader& shader,
                                   float scaleX, float scaleY, float orthoKx, float orthoKy) {
    std::vector<uint8_t> data;
    pack_uniforms_into(regs, shader, data, scaleX, scaleY, orthoKx, orthoKy);
    return data;
}

void reset_shader_memoization() {
    stateMemo.reset();
    lastFetch = {};
    programSlots = {};
    lastShaders[0] = {}; lastShaders[1] = {};
}

void clear_shader_cache() {
    for (auto& [key, shader] : shaders) free_decompiler(shader->dec);
    shaders.clear();
    variants.clear();
    programUses.clear();
    shadersByOutput.clear();
    shadersByModule.clear();
    verifiedKeys.clear();
    pendingCompiles.clear();
    pendingShaderRecords.clear();
    reset_shader_memoization();

}
}
