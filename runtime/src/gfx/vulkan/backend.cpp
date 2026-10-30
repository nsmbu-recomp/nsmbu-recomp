#include "core/env.h"
#ifndef VK_ENABLE_BETA_EXTENSIONS
#define VK_ENABLE_BETA_EXTENSIONS
#endif
#ifndef __ANDROID__
#define NSMBU_CREATE_WINDOW_SURFACE SDL_Vulkan_CreateSurface
#else
#define NSMBU_CREATE_WINDOW_SURFACE drivers::create_surface
#endif
#if defined(__APPLE__) && !defined(NSMBU_SDL_HOST)
#define VK_USE_PLATFORM_METAL_EXT
#endif
#include "app_title.h"
#include "core/window_title.h"
#include "core/build_info.h"
#include "backend.h"
#include "device_choice.h"
#include "bc_decode.h"
#include "android_driver.h"
#include "buffer_cache.h"
#include "gfx/render_prof.h"
#include "crash/report_header.h"
#include "present.h"
#include "gfx/display.h"
#include "gfx/frame_dump.h"
#include "gfx/display_modes.h"
#include "gx2/gx2.h"
#include "input/input.h"
#include "mods/mods.h"
#include "agent/agent_ctl.h"
#include "overlay/hostui.h"
#include "overlay/overlay.h"
#ifdef NSMBU_SDL_HOST
#include "platform/input_sdl.h"
#include <SDL3/SDL_vulkan.h>
#endif
#include "platform/display_rate.h"
#include "platform/host.h"
#include "platform/perf_hint.h"
#include "perf_metrics.h"
#include "core/runtime.h"
#include "gfx/screenshot.h"
#include "shaders.h"
#include "settings.h"
#include "save/savestate.h"
#include "sparse_hash_memo.h"
#include "core/write_watch.h"
#include <algorithm>
#include <functional>
#include <map>
#include <array>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <future>
#ifdef __APPLE__
#include <dlfcn.h>
#include <mach-o/dyld.h>
#endif
#include <stdexcept>
#include <string>
#include <thread>
#ifndef _WIN32
#include <time.h>
#endif
#include <vulkan/vulkan_beta.h>

namespace gx2 { void checkpoint_vulkan_caches(); }
namespace render { bool shader_warm_tick(uint64_t activity); }
#include "hooks/interp.h"
#include "hooks/interp_pacing.h"
#include "test/script_frame.h"
#include "gfx/renderer.h"

namespace gfxvk {
Renderer R;
namespace {
bool perf_enabled() {
  static const bool enabled = env::VK_STATS() != nullptr;
  return enabled;
}
bool cpu_only_stats_enabled() {
  static const bool enabled = [] {
    const char* value = env::VK_CPU_ONLY_STATS();
    return value && !std::strcmp(value, "1");
  }();
  return enabled;
}
struct WaitTiming { uint64_t count = 0, ns = 0; };
struct PresentTiming { WaitTiming acquire, present, idle; };
WaitTiming submitWait, presentSubmitWait;
PresentTiming screenTiming[2];
constexpr size_t maxPipelineCacheBytes = 64u * 1024u * 1024u;
std::string pipelineCachePath;
uint64_t pipelineCacheAttemptFrame = 0;
bool compatible_pipeline_cache(const std::vector<uint8_t>& bytes) {

  if (bytes.size() < 16 + VK_UUID_SIZE) return false;
  auto word = [&](size_t offset) {
    return uint32_t(bytes[offset]) | uint32_t(bytes[offset+1]) << 8 |
           uint32_t(bytes[offset+2]) << 16 | uint32_t(bytes[offset+3]) << 24;
  };
  uint32_t headerSize=word(0);
  return headerSize >= 16+VK_UUID_SIZE && headerSize <= bytes.size() &&
         word(4)==VK_PIPELINE_CACHE_HEADER_VERSION_ONE &&
         word(8)==R.properties.vendorID && word(12)==R.properties.deviceID &&
         !std::memcmp(bytes.data()+16,R.properties.pipelineCacheUUID,VK_UUID_SIZE);
}

constexpr std::array<uint8_t,8> pipelineCacheMagic{'W','W','V','K','P','C','0','2'};
constexpr size_t pipelineCacheWrapperSize=24;
uint64_t pipeline_cache_checksum(const uint8_t* bytes,size_t size) {
  uint64_t hash=14695981039346656037ull;
  for (size_t i=0;i<size;++i) hash=(hash^bytes[i])*1099511628211ull;
  return hash;
}
std::array<uint8_t,pipelineCacheWrapperSize> pipeline_cache_wrapper(const std::vector<uint8_t>& bytes) {
  std::array<uint8_t,pipelineCacheWrapperSize> header{};
  std::copy(pipelineCacheMagic.begin(),pipelineCacheMagic.end(),header.begin());
  uint64_t size=bytes.size(),checksum=pipeline_cache_checksum(bytes.data(),bytes.size());
  for (unsigned i=0;i<8;++i) {
    header[8+i]=uint8_t(size>>(i*8));header[16+i]=uint8_t(checksum>>(i*8));
  }
  return header;
}
bool unpack_pipeline_cache(std::vector<uint8_t>& file) {
  if (file.size()<pipelineCacheWrapperSize ||
      !std::equal(pipelineCacheMagic.begin(),pipelineCacheMagic.end(),file.begin())) return false;
  auto word64=[&](size_t offset) {
    uint64_t value=0;
    for (unsigned i=0;i<8;++i) value|=uint64_t(file[offset+i])<<(i*8);
    return value;
  };
  uint64_t size=word64(8);
  if (size<16+VK_UUID_SIZE || size>maxPipelineCacheBytes ||
      size!=file.size()-pipelineCacheWrapperSize) return false;
  if (word64(16)!=pipeline_cache_checksum(file.data()+pipelineCacheWrapperSize,size)) return false;
  file.erase(file.begin(),file.begin()+pipelineCacheWrapperSize);
  return compatible_pipeline_cache(file);
}
void init_pipeline_cache() try {
  if (const char* explicitPath=env::VK_PIPELINE_CACHE()) {
    if (std::strcmp(explicitPath,"0")) {
      pipelineCachePath=explicitPath;
#ifdef __ANDROID__

      char key[32];
      std::snprintf(key,sizeof key,"override-%016llx.bin",static_cast<unsigned long long>(
          pipeline_cache_checksum(reinterpret_cast<const uint8_t*>(explicitPath),std::strlen(explicitPath))));
      pipelineCachePath = drivers::pipeline_directory()+"/"+key;
#endif
    }
  } else if (const char* shaderPath=env::SHADER_CACHE();
             !shaderPath || std::strcmp(shaderPath,"0")) {
    char ids[32];
    std::snprintf(ids,sizeof ids,"vulkan-%08x-%08x-",R.properties.vendorID,R.properties.deviceID);

    pipelineCachePath=shaderPath ? std::string(shaderPath)+"."+ids
                                 :
#ifdef __ANDROID__
                                   drivers::pipeline_directory()+"/"+ids;
#else
                                   host::config_dir()+"/shadercache/"+ids;
#endif
    for (uint8_t byte : R.properties.pipelineCacheUUID) {
      char hex[3]; std::snprintf(hex,sizeof hex,"%02x",byte); pipelineCachePath+=hex;
    }
    pipelineCachePath+=".bin";
#ifdef __ANDROID__
    if (shaderPath) {
      char key[32];
      std::snprintf(key,sizeof key,"shaders-%016llx-",static_cast<unsigned long long>(
          pipeline_cache_checksum(reinterpret_cast<const uint8_t*>(shaderPath),std::strlen(shaderPath))));
      pipelineCachePath = drivers::pipeline_directory()+"/"+key+std::filesystem::path(pipelineCachePath).filename().string();
    }
#endif
  }
  std::vector<uint8_t> bytes;
  if (!pipelineCachePath.empty()) {
    std::error_code ec;
    uintmax_t size=std::filesystem::file_size(pipelineCachePath,ec);
    if (!ec && size>=pipelineCacheWrapperSize+16+VK_UUID_SIZE &&
        size<=maxPipelineCacheBytes+pipelineCacheWrapperSize) {
      bytes.resize(size);
      FILE* file=std::fopen(pipelineCachePath.c_str(),"rb");
      if (file) {
        if (std::fread(bytes.data(),1,bytes.size(),file)!=bytes.size() ||
            std::fgetc(file)!=EOF || std::ferror(file)) bytes.clear();
        std::fclose(file);
        if (!unpack_pipeline_cache(bytes)) bytes.clear();
      } else bytes.clear();
    }
  }
  VkPipelineCacheCreateInfo ci{VK_STRUCTURE_TYPE_PIPELINE_CACHE_CREATE_INFO};
  ci.initialDataSize=bytes.size();ci.pInitialData=bytes.empty()?nullptr:bytes.data();
  VkResult result=vkCreatePipelineCache(R.device,&ci,nullptr,&R.pipelineCache);
  if (result!=VK_SUCCESS && !bytes.empty()) {
    ci.initialDataSize=0;ci.pInitialData=nullptr;
    result=vkCreatePipelineCache(R.device,&ci,nullptr,&R.pipelineCache);
    bytes.clear();
  }
  if (result!=VK_SUCCESS) {
    R.pipelineCache=VK_NULL_HANDLE;
    LOG("[vulkan cache] creation failed (%d); continuing without cache",result);
  } else if (perf_enabled()) {
    LOG("[vulkan cache] loaded %zu bytes; persistence %s",bytes.size(),
        pipelineCachePath.empty()?"disabled":pipelineCachePath.c_str());
  }
} catch (const std::exception& error) {
  pipelineCachePath.clear();
  VkPipelineCacheCreateInfo ci{VK_STRUCTURE_TYPE_PIPELINE_CACHE_CREATE_INFO};
  if (vkCreatePipelineCache(R.device,&ci,nullptr,&R.pipelineCache)!=VK_SUCCESS)
    R.pipelineCache=VK_NULL_HANDLE;
  LOG("[vulkan cache] load disabled after error: %s",error.what());
}

template<class F> VkResult timed_call(WaitTiming& timing, F&& call, int profWait = -1) {
  const bool profiled = profWait >= 0 && rprof::enabled();
  if (!perf_enabled() && !profiled) return call();
  auto start = std::chrono::steady_clock::now();
  VkResult result = call();
  const uint64_t ns = std::chrono::duration_cast<std::chrono::nanoseconds>(
      std::chrono::steady_clock::now() - start).count();
  if (perf_enabled()) { ++timing.count; timing.ns += ns; }
  if (profiled) rprof::add_wait(rprof::Wait(profWait), ns);
  return result;
}
}
namespace {
struct PipelineCacheWriteResult {
  bool written = false;
  uint64_t revision = 0, changedFrame = 0;
  size_t size = 0;
  double elapsedMs = 0, checksumMs = 0;
  std::string error;
};
std::future<PipelineCacheWriteResult> pipelineCacheWrite;

PipelineCacheWriteResult write_pipeline_cache(std::vector<uint8_t> bytes,
                                              std::string path,
                                              uint64_t revision,
                                              uint64_t changedFrame) {
  PipelineCacheWriteResult result;
  result.revision=revision;result.changedFrame=changedFrame;result.size=bytes.size();
  auto start=std::chrono::steady_clock::now();
  std::string temporary;
  try {
    auto wrapper=pipeline_cache_wrapper(bytes);
    result.checksumMs=std::chrono::duration<double,std::milli>(
        std::chrono::steady_clock::now()-start).count();
    std::error_code ec;
    auto parent=std::filesystem::path(path).parent_path();
    if (!parent.empty()) std::filesystem::create_directories(parent,ec);
    if (ec) throw std::runtime_error(ec.message());
    temporary=path+".tmp-"+std::to_string(start.time_since_epoch().count());
    FILE* file=std::fopen(temporary.c_str(),"wb");
    if (file) {
      bool written=std::fwrite(wrapper.data(),1,wrapper.size(),file)==wrapper.size();
      if (written) written=std::fwrite(bytes.data(),1,bytes.size(),file)==bytes.size();
      if (std::fclose(file)) written=false;
      if (written) result.written=host::replace_file(temporary,path);
    }
    if (!result.written) {
      std::filesystem::remove(temporary,ec);
      result.error="file write or replacement failed";
    }
  } catch (const std::exception& error) {
    result.error=error.what();
    if (!temporary.empty()) {
      std::error_code ec;std::filesystem::remove(temporary,ec);
    }
  }
  result.elapsedMs=std::chrono::duration<double,std::milli>(
      std::chrono::steady_clock::now()-start).count();
  return result;
}
void poll_pipeline_cache_write(bool wait) {
  if (!pipelineCacheWrite.valid()) return;
  if (!wait && pipelineCacheWrite.wait_for(std::chrono::seconds(0)) !=
                   std::future_status::ready) return;
  auto result=pipelineCacheWrite.get();

  if (result.written && result.revision==R.pipelineCreates &&
      result.changedFrame==R.pipelineCacheChangedFrame)
    R.pipelineCacheDirty=false;
  if (result.written && perf_enabled())
    LOG("[vulkan cache] persisted %zu payload bytes in %.2f ms (checksum %.2f ms)",
        result.size,result.elapsedMs,result.checksumMs);
  else if (!result.written)
    LOG("[vulkan cache] save skipped: %s",result.error.c_str());
}
void start_pipeline_cache_write() {
  if (pipelineCacheWrite.valid() || !R.pipelineCache ||
      !R.pipelineCacheDirty || pipelineCachePath.empty()) return;

  size_t size=0;
  if (vkGetPipelineCacheData(R.device,R.pipelineCache,&size,nullptr)!=VK_SUCCESS ||
      size<16+VK_UUID_SIZE || size>maxPipelineCacheBytes) return;
  std::vector<uint8_t> bytes(size);
  if (vkGetPipelineCacheData(R.device,R.pipelineCache,&size,bytes.data())!=VK_SUCCESS) return;
  bytes.resize(size);
  if (!compatible_pipeline_cache(bytes)) return;
  pipelineCacheWrite=std::async(std::launch::async,write_pipeline_cache,
      std::move(bytes),pipelineCachePath,R.pipelineCreates,R.pipelineCacheChangedFrame);
}
void checkpoint_pipeline_cache() try {
  poll_pipeline_cache_write(false);
  if (R.pipelineCacheDirty && R.frame-R.pipelineCacheChangedFrame>=120 &&
      R.frame-pipelineCacheAttemptFrame>=120 && !pipelineCacheWrite.valid()) {
    pipelineCacheAttemptFrame=R.frame;
    start_pipeline_cache_write();
  }
} catch (const std::exception& error) {
  LOG("[vulkan cache] checkpoint skipped after error: %s",error.what());
}
}
void save_pipeline_cache() try {

  poll_pipeline_cache_write(true);
  start_pipeline_cache_write();
  poll_pipeline_cache_write(true);
} catch (const std::exception& error) {
  LOG("[vulkan cache] save skipped after error: %s",error.what());
}
void vk_check(VkResult r, const char *op) {
  if (r != VK_SUCCESS)
    throw std::runtime_error(std::string(op) + ": Vulkan result " +
                             std::to_string(r));
}
uint32_t memory_type(uint32_t bits, VkMemoryPropertyFlags flags) {
  VkPhysicalDeviceMemoryProperties p;
  vkGetPhysicalDeviceMemoryProperties(R.physicalDevice, &p);
  for (uint32_t i = 0; i < p.memoryTypeCount; i++)
    if ((bits & (1u << i)) && (p.memoryTypes[i].propertyFlags & flags) == flags)
      return i;
  throw std::runtime_error("No compatible Vulkan memory type");
}
Buffer create_buffer(VkDeviceSize size, VkBufferUsageFlags usage,
                     VkMemoryPropertyFlags flags, VkMemoryPropertyFlags preferred) {
  Buffer b;
  b.size = std::max<VkDeviceSize>(size, 16);
  VkBufferCreateInfo ci{VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO};
  ci.size = b.size;
  ci.usage = usage;
  ci.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
  vk_check(vkCreateBuffer(R.device, &ci, nullptr, &b.buffer), "create buffer");
  VkMemoryRequirements req;
  vkGetBufferMemoryRequirements(R.device, b.buffer, &req);
  VkMemoryAllocateInfo ai{VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO};
  ai.allocationSize = req.size;
  ai.memoryTypeIndex = memory_type(req.memoryTypeBits, flags);
  if (preferred) {
    VkPhysicalDeviceMemoryProperties p;
    vkGetPhysicalDeviceMemoryProperties(R.physicalDevice, &p);
    for (uint32_t i = 0; i < p.memoryTypeCount; i++)
      if ((req.memoryTypeBits & (1u << i)) &&
          (p.memoryTypes[i].propertyFlags & (flags | preferred)) == (flags | preferred)) {
        ai.memoryTypeIndex = i;
        break;
      }
  }
  {
    VkPhysicalDeviceMemoryProperties p;
    vkGetPhysicalDeviceMemoryProperties(R.physicalDevice, &p);
    b.properties = p.memoryTypes[ai.memoryTypeIndex].propertyFlags;
  }
  vk_check(vkAllocateMemory(R.device, &ai, nullptr, &b.memory),
           "allocate buffer memory");
  vk_check(vkBindBufferMemory(R.device, b.buffer, b.memory, 0),
           "bind buffer memory");
  if (flags & VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT)
    vk_check(vkMapMemory(R.device, b.memory, 0, VK_WHOLE_SIZE, 0, &b.mapped),
             "map buffer");
  return b;
}

Buffer create_readback_buffer(VkDeviceSize size) {
  return create_buffer(size, VK_BUFFER_USAGE_TRANSFER_DST_BIT,
                       VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
                       VK_MEMORY_PROPERTY_HOST_CACHED_BIT);
}

namespace {
enum class UploadReads { Auto, Shadow, Direct };
UploadReads upload_reads_mode() {
  static const UploadReads mode = [] {
    const char* e = env::VK_UPLOAD_READS();
    if (!e || !*e || !std::strcmp(e, "auto")) return UploadReads::Auto;
    if (!std::strcmp(e, "shadow")) return UploadReads::Shadow;
    if (!std::strcmp(e, "direct")) return UploadReads::Direct;
    LOG("[vulkan] NSMBU_VK_UPLOAD_READS=%s unknown (auto|shadow|direct): auto", e);
    return UploadReads::Auto;
  }();
  return mode;
}
void note_upload_block(const Buffer& buffer) {
  const VkMemoryPropertyFlags readable =
      VK_MEMORY_PROPERTY_HOST_CACHED_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT;
  const bool first = R.uploadAllocations == 0;
  const bool cached = (buffer.properties & readable) == readable && (first || R.uploadCached);
  const auto mode = upload_reads_mode();
  const bool direct = mode == UploadReads::Direct || (mode == UploadReads::Auto && cached);
  if (first || direct != R.uploadReadsDirect)
    LOG("[vulkan] upload arena memory: %s; reuse checks %s (NSMBU_VK_UPLOAD_READS=%s)",
        cached ? "host-cached" : "not host-cached", direct ? "read the slices" : "keep CPU copies",
        mode == UploadReads::Auto ? "auto" : mode == UploadReads::Shadow ? "shadow" : "direct");
  R.uploadCached = cached;
  R.uploadReadsDirect = direct;
}
}
UploadSlice allocate_upload(VkDeviceSize size, VkDeviceSize alignment) {
  size = std::max<VkDeviceSize>(size,16);
  alignment = std::max<VkDeviceSize>(alignment,4);
  auto slice = [&](Renderer::UploadBlock& block) {
    VkDeviceSize offset = ((block.used + alignment - 1) / alignment) * alignment;
    if (offset > block.buffer.size || size > block.buffer.size - offset)
      return UploadSlice{};
    block.used = offset + size;
    R.uploadBytes += size;
    rprof::add_upload(size);
    return UploadSlice{block.buffer.buffer,offset,size,
                       static_cast<uint8_t*>(block.buffer.mapped)+offset};
  };
  for (auto& block : R.uploadBlocks) {
    auto result = slice(block);
    if (result.buffer) return result;
  }
  auto buffer = create_buffer(std::max<VkDeviceSize>(32ull<<20,size),
      VK_BUFFER_USAGE_VERTEX_BUFFER_BIT | VK_BUFFER_USAGE_INDEX_BUFFER_BIT |
      VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_SRC_BIT,
      VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);
  note_upload_block(buffer);
  R.uploadBlocks.push_back({buffer,0});
  ++R.uploadAllocations;
  return slice(R.uploadBlocks.back());
}
void defer_buffer(Buffer b) { R.garbageBuffers.push_back(b); }
void defer_surface_image(VkImage image, VkDeviceMemory memory,
                         std::vector<VkImageView> views) {
  R.garbageImages.push_back({image, memory, std::move(views)});
}
void forget_texture_views() {
}
namespace {
struct GpuScopeBucket {
  GpuScopeMetadata metadata{};
  double intervalNs=0,maxNs=0;
  uint64_t count=0,draws=0;
};
std::array<GpuScopeBucket,256> gpuScopeBuckets{};
uint32_t gpuScopeBucketCount=0;
struct GpuScopeTotals { uint64_t available=0,unavailable=0,zero=0,dropped=0,bucketDrops=0; } gpuScopeTotals;
bool same_gpu_scope(const GpuScopeMetadata& a,const GpuScopeMetadata& b) {
  return a.kind==b.kind && a.colors==b.colors && a.depth==b.depth &&
      a.extent.width==b.extent.width && a.extent.height==b.extent.height && a.extent.depth==b.extent.depth &&
      a.mips==b.mips && a.layers==b.layers && a.aspects==b.aspects && a.colorCount==b.colorCount &&
      a.slices==b.slices && a.depthSlice==b.depthSlice && a.depthOnly==b.depthOnly;
}
void add_gpu_scope(const Renderer::Submission::GpuScope& scope,double ns) {
  ++gpuScopeTotals.available;
  gpuScopeTotals.zero += ns==0;
  uint32_t i=0;
  while(i<gpuScopeBucketCount && !same_gpu_scope(gpuScopeBuckets[i].metadata,scope.metadata)) ++i;
  if(i==gpuScopeBucketCount) {
    if(i==gpuScopeBuckets.size()) { ++gpuScopeTotals.bucketDrops; return; }
    gpuScopeBuckets[i]={};gpuScopeBuckets[i].metadata=scope.metadata;++gpuScopeBucketCount;
  }
  auto& bucket=gpuScopeBuckets[i];
  bucket.intervalNs+=ns;bucket.maxNs=std::max(bucket.maxNs,ns);
  ++bucket.count;bucket.draws+=scope.draws;
}
}
static bool timestamp_capable(uint32_t bits, double period) {
  return bits > 0 && bits <= 64 && period > 0 && std::isfinite(period);
}
static uint64_t timestamp_elapsed_ticks(uint64_t start, uint64_t end, uint32_t bits) {
  const uint64_t mask = bits == 64 ? UINT64_MAX : (uint64_t{1} << bits) - 1;
  return (end - start) & mask;
}
struct OverlayGpuQuery {
  VkQueryPool pool = VK_NULL_HANDLE;
  uint64_t frame = 0, token = 0;
  bool pending = false;
};
static std::array<OverlayGpuQuery, 256> overlayGpuQueries{};
static void destroy_overlay_gpu_queries() {
  for (auto& q : overlayGpuQueries) {
    if (q.pool) vkDestroyQueryPool(R.device, q.pool, nullptr);
    q = {};
  }
}
static bool overlay_timestamp_capable() {
  return timestamp_capable(R.gpuTimestampValidBits, R.properties.limits.timestampPeriod);
}
static void begin_overlay_gpu_queries(VkCommandBuffer cmd, Renderer::Submission& submission) {
  submission.perfQuery = UINT32_MAX;
  if (!perf::enabled() || !overlay_timestamp_capable()) return;
  static uint64_t lastPoll = UINT64_MAX;
  if (lastPoll != R.frame) {
    lastPoll = R.frame;
    for (auto& q : overlayGpuQueries) {
      if (!q.pending || q.frame >= R.frame) continue;
      struct Result { uint64_t ticks, available; } values[2]{};
      perf::timestamp_read(R.frame - q.frame);
      VkResult result = vkGetQueryPoolResults(R.device, q.pool, 0, 2, sizeof(values), values,
          sizeof(Result), VK_QUERY_RESULT_64_BIT | VK_QUERY_RESULT_WITH_AVAILABILITY_BIT);
      if (result == VK_NOT_READY || (result == VK_SUCCESS &&
          (!values[0].available || !values[1].available))) continue;
      double ns = result == VK_SUCCESS
          ? double(timestamp_elapsed_ticks(values[0].ticks, values[1].ticks, R.gpuTimestampValidBits)) *
                double(R.properties.limits.timestampPeriod) : -1;
      perf::gpu_complete(q.frame, q.token, ns);
      q.pending = false;
    }
  }
  perf::gpu_advance(R.frame);
  uint64_t token = perf::gpu_begin(R.frame);
  if (!token) return;
  for (uint32_t i = 0; i < overlayGpuQueries.size(); ++i) {
    auto& q = overlayGpuQueries[i];
    if (q.pending) continue;
    if (!q.pool) {
      VkQueryPoolCreateInfo info{VK_STRUCTURE_TYPE_QUERY_POOL_CREATE_INFO};
      info.queryType = VK_QUERY_TYPE_TIMESTAMP; info.queryCount = 2;
      if (vkCreateQueryPool(R.device, &info, nullptr, &q.pool) != VK_SUCCESS) {
        perf::gpu_complete(R.frame, token, -1); return;
      }
    }
    q.frame = R.frame; q.token = token; q.pending = true;
    submission.perfQuery = i;
    vkCmdResetQueryPool(cmd, q.pool, 0, 2);
    vkCmdWriteTimestamp(cmd, VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, q.pool, 0);
    perf::timestamp_write();
    return;
  }
  perf::gpu_complete(R.frame, token, -1);
}
static void destroy_gpu_timestamp_queries() {
  destroy_overlay_gpu_queries();
  R.gpuTimestampsEnabled = false;
  R.gpuPassTimestampsEnabled = false;
  for (auto& slot : R.submissions) {
    if (slot.timestampQueries) vkDestroyQueryPool(R.device, slot.timestampQueries, nullptr);
    slot.timestampQueries = VK_NULL_HANDLE;
    slot.timestampRecorded = false;
    slot.gpuScopeCount = 0; slot.activeRenderScope = UINT32_MAX;
  }
}
static void init_gpu_timestamp_queries() {
  const char* requested = env::VK_GPU_TIMESTAMPS();
  const char* passes = env::VK_GPU_PASS_TIMESTAMPS();
  const bool passRequested = passes && !std::strcmp(passes, "1");
  if ((!requested || std::strcmp(requested, "1")) && !passRequested) return;
  if (!timestamp_capable(R.gpuTimestampValidBits, R.properties.limits.timestampPeriod)) {
    LOG("[vulkan GPU timestamps] unsupported: valid bits %u, period %.9g ns",
        R.gpuTimestampValidBits, double(R.properties.limits.timestampPeriod));
    return;
  }
  for (auto& slot : R.submissions) {
    VkQueryPoolCreateInfo info{VK_STRUCTURE_TYPE_QUERY_POOL_CREATE_INFO};
    info.queryType = VK_QUERY_TYPE_TIMESTAMP;
    info.queryCount = passRequested ? 2 + 2 * Renderer::Submission::maxGpuScopes : 2;
    const VkResult result = vkCreateQueryPool(R.device, &info, nullptr, &slot.timestampQueries);
    if (result != VK_SUCCESS) {

      slot.timestampQueries = VK_NULL_HANDLE;
      bc_decode_shutdown();
  destroy_gpu_timestamp_queries();
      LOG("[vulkan GPU timestamps] optional query pools unavailable (%d); disabled", int(result));
      return;
    }
  }
  R.gpuTimestampsEnabled = true;
  R.gpuPassTimestampsEnabled = passRequested;
  if (passRequested) LOG("[vulkan GPU pass timestamps] enabled: 256 bounded scopes/submission; stage intervals may overlap or include stalls; NOT GPU busy time");
  LOG("[vulkan GPU timestamps] enabled: %u valid bits, %.9g ns/tick; submission intervals, NOT GPU busy time",
      R.gpuTimestampValidBits, double(R.properties.limits.timestampPeriod));
}
static void collect_gpu_timestamp_queries(Renderer::Submission& slot) {
  if (!slot.timestampRecorded) return;
  slot.timestampRecorded = false;
  if (!R.gpuTimestampsEnabled) return;
  struct Result { uint64_t ticks, available; } values[2]{};
  const VkResult result = vkGetQueryPoolResults(R.device, slot.timestampQueries, 0, 2,
      sizeof(values), values, sizeof(Result),
      VK_QUERY_RESULT_64_BIT | VK_QUERY_RESULT_WITH_AVAILABILITY_BIT);
  if (result == VK_NOT_READY || (result == VK_SUCCESS &&
      (!values[0].available || !values[1].available))) {
    ++R.gpuTimestampStats.unavailable;
    return;
  }
  if (result != VK_SUCCESS) {
    ++R.gpuTimestampStats.unavailable;
    R.gpuTimestampsEnabled = false;
    LOG("[vulkan GPU timestamps] optional results failed (%d); disabled", int(result));
    return;
  }
  const double ns = double(timestamp_elapsed_ticks(values[0].ticks, values[1].ticks,
      R.gpuTimestampValidBits)) * double(R.properties.limits.timestampPeriod);
  auto& stats = R.gpuTimestampStats;
  ++stats.submissions;
  stats.intervalNs += ns;
  stats.maxIntervalNs = std::max(stats.maxIntervalNs, ns);
  stats.zeroIntervals += ns == 0;
}
static void collect_gpu_pass_queries(Renderer::Submission& slot) {
  const uint32_t count=slot.gpuScopeCount;
  slot.gpuScopeCount=0;slot.activeRenderScope=UINT32_MAX;
  if(!count || !R.gpuTimestampsEnabled || !R.gpuPassTimestampsEnabled) return;
  struct Result { uint64_t ticks,available; };
  std::array<Result,2*Renderer::Submission::maxGpuScopes> values{};
  const VkResult result=vkGetQueryPoolResults(R.device,slot.timestampQueries,2,2*count,
      2*count*sizeof(Result),values.data(),sizeof(Result),
      VK_QUERY_RESULT_64_BIT|VK_QUERY_RESULT_WITH_AVAILABILITY_BIT);
  if(result!=VK_SUCCESS && result!=VK_NOT_READY) {
    gpuScopeTotals.unavailable+=count;R.gpuTimestampsEnabled=false;
    LOG("[vulkan GPU pass timestamps] optional results failed (%d); disabled",int(result));
    return;
  }
  for(uint32_t i=0;i<count;++i) {
    if(!slot.gpuScopes[i].ended || !values[2*i].available || !values[2*i+1].available) {
      ++gpuScopeTotals.unavailable;continue;
    }
    const double ns=double(timestamp_elapsed_ticks(values[2*i].ticks,values[2*i+1].ticks,
        R.gpuTimestampValidBits))*double(R.properties.limits.timestampPeriod);
    add_gpu_scope(slot.gpuScopes[i],ns);
  }
}
static void report_gpu_pass_timestamps(double frames) {
  if(!R.gpuPassTimestampsEnabled) return;
  LOG("[vulkan GPU pass timestamps] %llu available; %llu unavailable; %llu zero; %llu capacity drops; %llu bucket drops; stage intervals NOT GPU busy time%s",
      (unsigned long long)gpuScopeTotals.available,(unsigned long long)gpuScopeTotals.unavailable,
      (unsigned long long)gpuScopeTotals.zero,(unsigned long long)gpuScopeTotals.dropped,
      (unsigned long long)gpuScopeTotals.bucketDrops,
      gpuScopeTotals.available && gpuScopeTotals.available==gpuScopeTotals.zero
          ? "; all-zero/emulated timing UNUSABLE" : "");
  std::array<uint32_t,256> order{};
  for(uint32_t i=0;i<gpuScopeBucketCount;++i) order[i]=i;
  std::sort(order.begin(),order.begin()+gpuScopeBucketCount,[](uint32_t a,uint32_t b) {
    return gpuScopeBuckets[a].intervalNs>gpuScopeBuckets[b].intervalNs;
  });
  for(uint32_t rank=0;rank<std::min(gpuScopeBucketCount,8u);++rank) {
    const auto& b=gpuScopeBuckets[order[rank]];const auto& m=b.metadata;
    char formats[128]{};size_t used=0;
    for(uint32_t i=0;i<m.colors.size();++i) if(m.colors[i]!=VK_FORMAT_UNDEFINED)
      used+=size_t(std::snprintf(formats+used,sizeof(formats)-used,"%s%u:%u",used?",":"",i,uint32_t(m.colors[i])));
    LOG("[vulkan GPU scope %u] %s %ux%ux%u; colors [%s] depth %u; mips %u layers %u aspects 0x%x depth-only %u; %.3f ms/frame %.3f mean %.3f max ms; %.2f scopes/frame %.2f draws/frame",
        rank+1,m.kind?"feedback":"render",m.extent.width,m.extent.height,m.extent.depth,
        formats,uint32_t(m.depth),m.mips,m.layers,m.aspects,m.depthOnly,
        b.intervalNs/1e6/frames,b.intervalNs/1e6/double(b.count),b.maxNs/1e6,
        b.count/frames,b.draws/frames);
  }
  gpuScopeBuckets={};gpuScopeBucketCount=0;gpuScopeTotals={};
}
static void report_gpu_timestamps() {
  static uint64_t previousFrame = 0;
  if (!R.gpuTimestampsEnabled || R.frame - previousFrame < 120) return;
  const auto& stats = R.gpuTimestampStats;
  const double frames = double(R.frame - previousFrame);
  LOG("[vulkan GPU timestamps] submission interval sum %.3f ms/frame; max %.3f ms/submission; %llu submissions; %llu unavailable; %llu zero; NOT GPU busy time%s",
      stats.intervalNs / 1e6 / frames, stats.maxIntervalNs / 1e6,
      (unsigned long long)stats.submissions, (unsigned long long)stats.unavailable,
      (unsigned long long)stats.zeroIntervals,
      stats.submissions && stats.zeroIntervals == stats.submissions
          ? "; all-zero/emulated timing UNUSABLE" : "");
  report_gpu_pass_timestamps(frames);
  R.gpuTimestampStats = {};
  previousFrame = R.frame;
}
VkCommandBuffer command_buffer() {
  if (!R.recording) {
    VkCommandBufferBeginInfo bi{VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO};
    bi.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
    vk_check(vkBeginCommandBuffer(R.cmd, &bi), "begin command buffer");
    R.recording = true;
    begin_overlay_gpu_queries(R.cmd, R.submissions[R.activeSubmission]);
    if (R.gpuTimestampsEnabled) {
      auto& slot = R.submissions[R.activeSubmission];
      vkCmdResetQueryPool(R.cmd, slot.timestampQueries, 0,
          R.gpuPassTimestampsEnabled ? 2 + 2 * Renderer::Submission::maxGpuScopes : 2);
      vkCmdWriteTimestamp(R.cmd, VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, slot.timestampQueries, 0);
      slot.timestampRecorded = true;
      slot.timestampFrame = R.frame;
    }
  }
  return R.cmd;
}
static GpuScopeToken begin_gpu_scope(const GpuScopeMetadata& metadata) {
  if(!R.gpuTimestampsEnabled || !R.gpuPassTimestampsEnabled) return {};
  if(R.rendering) { ++gpuScopeTotals.dropped;return {}; }
  const VkCommandBuffer cmd=command_buffer();
  auto& slot=R.submissions[R.activeSubmission];
  if(slot.gpuScopeCount==Renderer::Submission::maxGpuScopes) { ++gpuScopeTotals.dropped;return {}; }
  const uint32_t index=slot.gpuScopeCount++;
  slot.gpuScopes[index]={metadata,0,R.submissionGeneration,false};
  vkCmdWriteTimestamp(cmd,VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT,slot.timestampQueries,2+2*index);
  return {R.submissionGeneration,index};
}
static void end_gpu_scope(GpuScopeToken token) {
  if(token.index==UINT32_MAX || token.generation!=R.submissionGeneration) return;
  auto& slot=R.submissions[R.activeSubmission];
  if(token.index>=slot.gpuScopeCount || slot.gpuScopes[token.index].ended ||
      slot.gpuScopes[token.index].generation!=token.generation) return;
  if(R.rendering) { ++gpuScopeTotals.dropped;return; }
  vkCmdWriteTimestamp(R.cmd,VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT,slot.timestampQueries,3+2*token.index);
  slot.gpuScopes[token.index].ended=true;
}
void gpu_begin_render_scope(const std::array<Surface*,8>& colors,Surface* depth,
                            const uint32_t* slices,uint32_t depthSlice,uint32_t width,uint32_t height) {
  if(!R.gpuTimestampsEnabled || !R.gpuPassTimestampsEnabled) return;
  GpuScopeMetadata metadata;metadata.extent={width,height,1};metadata.depthOnly=true;
  Surface* target=depth;
  for(uint32_t i=0;i<colors.size();++i) if(colors[i]) {
    metadata.colors[i]=colors[i]->fmt.pixel;metadata.slices[i]=slices[i];
    ++metadata.colorCount;metadata.depthOnly=false;if(target==depth) target=colors[i];
  }
  if(depth) { metadata.depth=depth->fmt.pixel;metadata.depthSlice=depthSlice; }
  if(target) { metadata.mips=target->mips;metadata.layers=target->arrayLayers;metadata.aspects=target->aspect; }
  auto token=begin_gpu_scope(metadata);
  R.submissions[R.activeSubmission].activeRenderScope=token.index;
}
void gpu_count_render_draw() {
  if(!R.gpuTimestampsEnabled || !R.gpuPassTimestampsEnabled) return;
  auto& slot=R.submissions[R.activeSubmission];
  if(slot.activeRenderScope<slot.gpuScopeCount) {
    auto& scope=slot.gpuScopes[slot.activeRenderScope];
    if(scope.generation==R.submissionGeneration && !scope.ended) ++scope.draws;
  }
}
GpuScopeToken gpu_begin_feedback_scope(const Surface& source) {
  if(!R.gpuTimestampsEnabled || !R.gpuPassTimestampsEnabled) return {};
  GpuScopeMetadata metadata;metadata.kind=1;metadata.extent=source.extent;
  metadata.mips=source.mips;metadata.layers=source.arrayLayers;metadata.aspects=source.aspect;
  if(source.fmt.depth) { metadata.depth=source.fmt.pixel;metadata.depthOnly=true; }
  else { metadata.colors[0]=source.fmt.pixel;metadata.colorCount=1; }
  return begin_gpu_scope(metadata);
}
void gpu_end_feedback_scope(GpuScopeToken token) { end_gpu_scope(token); }
void end_encoder() {
  if (R.rendering) {
    vkCmdEndRendering(R.cmd);
    R.rendering = false;
    R.passTracked = false;
    if(R.gpuPassTimestampsEnabled) {
      auto& slot=R.submissions[R.activeSubmission];
      end_gpu_scope({R.submissionGeneration,slot.activeRenderScope});
      slot.activeRenderScope=UINT32_MAX;
    }
  }
}
bool narrow_barriers() {
  static const bool enabled = [] {
    const char* e = env::VK_NARROW_BARRIERS();

    if (e && *e) return std::strcmp(e, "0") != 0;
#ifdef __ANDROID__
    return true;
#else
    return false;
#endif
  }();
  return enabled;
}
void transition_buffer(Buffer& buffer, VkPipelineStageFlags stage, VkAccessFlags access) {
  auto d = derive_dependency(buffer.use, stage, access);
  if (!d.needed) return;
  end_encoder();
  VkBufferMemoryBarrier b{VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER};
  b.srcAccessMask = d.sourceAccess;
  b.dstAccessMask = access;
  b.srcQueueFamilyIndex = b.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
  b.buffer = buffer.buffer;
  b.size = VK_WHOLE_SIZE;
  vkCmdPipelineBarrier(command_buffer(), d.source, d.destination, 0, 0, nullptr, 1, &b, 0, nullptr);
}
void transition_image(Surface *s, VkImageLayout layout,
                      VkPipelineStageFlags stage, VkAccessFlags access) {
  const bool conservativePassBreak = s->layout != layout ||
      layout != VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL || access != VK_ACCESS_SHADER_READ_BIT;
  if (narrow_barriers() && stage == VK_PIPELINE_STAGE_ALL_COMMANDS_BIT &&
      access == (VK_ACCESS_MEMORY_READ_BIT | VK_ACCESS_MEMORY_WRITE_BIT) &&
      layout == VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL) {
    stage = VK_PIPELINE_STAGE_VERTEX_SHADER_BIT | VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT;
    access = VK_ACCESS_SHADER_READ_BIT;
  }
  const bool changed = s->layout != layout;
  const auto dependency = derive_dependency(s->use, stage, access, changed);
  if (narrow_barriers()) {
    if (!dependency.needed) {

      if (conservativePassBreak) end_encoder();
      return;
    }
  } else if (!changed && layout == VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL &&
             access == VK_ACCESS_SHADER_READ_BIT) return;
  end_encoder();
  VkImageMemoryBarrier b{VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER};
  b.oldLayout = s->layout;
  b.newLayout = layout;
  b.srcQueueFamilyIndex = b.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
  b.image = s->image;
  b.subresourceRange = {s->aspect, 0, s->mips, 0, s->arrayLayers};
  b.srcAccessMask = narrow_barriers() ? dependency.sourceAccess :
      (s->layout == VK_IMAGE_LAYOUT_UNDEFINED ? 0 : VK_ACCESS_MEMORY_READ_BIT | VK_ACCESS_MEMORY_WRITE_BIT);
  b.dstAccessMask = access;
  vkCmdPipelineBarrier(command_buffer(), narrow_barriers() ? dependency.source :
      (s->layout == VK_IMAGE_LAYOUT_UNDEFINED ? VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT : VK_PIPELINE_STAGE_ALL_COMMANDS_BIT),
      stage, 0, 0, nullptr, 0, nullptr, 1, &b);
  s->layout = layout;
}
static void cleanup_submission(Renderer::Submission& slot) {
  for (auto& complete : slot.completions) complete();
  slot.completions.clear();

  for (auto& block : slot.uploadBlocks) block.used = 0;
  for (auto b : slot.garbageBuffers) {
    if (b.mapped)
      vkUnmapMemory(R.device, b.memory);
    vkDestroyBuffer(R.device, b.buffer, nullptr);
    vkFreeMemory(R.device, b.memory, nullptr);
  }
  slot.garbageBuffers.clear();
  buffer_cache_free(slot.garbageCacheRegions);
  slot.garbageCacheRegions.clear();
  for (auto &i : slot.garbageImages) {
    for (auto v : i.views)
      if (v)
        vkDestroyImageView(R.device, v, nullptr);
    if (i.image)
      vkDestroyImage(R.device, i.image, nullptr);
    if (i.memory)
      vkFreeMemory(R.device, i.memory, nullptr);
  }
  slot.garbageImages.clear();
  vk_check(vkResetCommandPool(R.device, slot.commandPool, 0),
           "reset command pool");
  vk_check(vkResetDescriptorPool(R.device, slot.descriptorPool, 0),
           "reset descriptor pool");
  ++R.submissionGeneration;
}
static void retire_submission(Renderer::Submission& slot, WaitTiming& timing=submitWait) {
  if (!slot.pending) return;
  VkResult status=vkGetFenceStatus(R.device,slot.fence);
  if (status==VK_NOT_READY) {
    vk_check(timed_call(timing,[&] {
      return vkWaitForFences(R.device,1,&slot.fence,VK_TRUE,UINT64_MAX);
    },rprof::kWaitGpu),"wait submission retirement");
  } else vk_check(status,"submission fence status");
  collect_gpu_timestamp_queries(slot);
  if(R.gpuPassTimestampsEnabled) collect_gpu_pass_queries(slot);
  cleanup_submission(slot);
  slot.pending=false;
}
static void activate_submission(size_t index) {
  auto& slot=R.submissions[index];
  retire_submission(slot);
  R.activeSubmission=index;
  slot.gpuScopeCount=0;slot.activeRenderScope=UINT32_MAX;
  ++R.submissionGeneration;
  R.commandPool=slot.commandPool;R.cmd=slot.cmd;R.fence=slot.fence;
  R.descriptorPool=slot.descriptorPool;
  R.uploadBlocks=std::move(slot.uploadBlocks);
  R.garbageBuffers=std::move(slot.garbageBuffers);
  R.garbageImages=std::move(slot.garbageImages);
  R.garbageCacheRegions=std::move(slot.garbageCacheRegions);
  R.recording=false;R.rendering=false;R.passTracked=false;
}
static void drain_submissions() {

  Renderer::Submission* newest=nullptr;
  for (auto& slot:R.submissions)
    if (slot.pending && (!newest || slot.serial>newest->serial)) newest=&slot;
  if (newest) retire_submission(*newest);
  for (auto& slot:R.submissions) retire_submission(slot);
}
static void submit(VkSemaphore wait = VK_NULL_HANDLE,
                   VkSemaphore signal = VK_NULL_HANDLE, bool asynchronous=false) {
  if (!R.recording)
    return;
  end_encoder();
  auto& recordingSlot = R.submissions[R.activeSubmission];
  if (recordingSlot.perfQuery != UINT32_MAX) {
    auto& q = overlayGpuQueries[recordingSlot.perfQuery];
    vkCmdWriteTimestamp(R.cmd, VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT, q.pool, 1);
    perf::timestamp_write();
    recordingSlot.perfQuery = UINT32_MAX;
  }
  if (recordingSlot.timestampRecorded)
    vkCmdWriteTimestamp(R.cmd, VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT,
                        recordingSlot.timestampQueries, 1);
  vk_check(vkEndCommandBuffer(R.cmd), "end command buffer");
  vk_check(vkResetFences(R.device, 1, &R.fence), "reset fence");
  VkPipelineStageFlags stage = VK_PIPELINE_STAGE_ALL_COMMANDS_BIT;
  VkSubmitInfo si{VK_STRUCTURE_TYPE_SUBMIT_INFO};
  si.commandBufferCount = 1;
  si.pCommandBuffers = &R.cmd;
  if (wait) {
    si.waitSemaphoreCount = 1;
    si.pWaitSemaphores = &wait;
    si.pWaitDstStageMask = &stage;
  }
  if (signal) {
    si.signalSemaphoreCount = 1;
    si.pSignalSemaphores = &signal;
  }
  vk_check(vkQueueSubmit(R.queue, 1, &si, R.fence), "submit graphics");
  R.recording=false;
  auto& slot=R.submissions[R.activeSubmission];
  static uint64_t nextSubmissionSerial=0;
  slot.serial=++nextSubmissionSerial;
  slot.uploadBlocks=std::move(R.uploadBlocks);
  slot.completions=std::move(R.completions);
  slot.garbageBuffers=std::move(R.garbageBuffers);
  slot.garbageImages=std::move(R.garbageImages);
  slot.garbageCacheRegions=std::move(R.garbageCacheRegions);
  slot.pending=true;
  if (asynchronous) {
    activate_submission((R.activeSubmission+1)%R.submissions.size());
  } else {
    retire_submission(slot,wait ? presentSubmitWait : submitWait);
    activate_submission(R.activeSubmission);
  }
}
void flush_async() {

  if (!R.recording && (!R.garbageBuffers.empty() || !R.garbageImages.empty() ||
                       !R.garbageCacheRegions.empty())) command_buffer();
  submit(VK_NULL_HANDLE,VK_NULL_HANDLE,true);
}
void flush() {
  if (!R.recording && (!R.garbageBuffers.empty() || !R.garbageImages.empty() ||
                       !R.garbageCacheRegions.empty())) command_buffer();
  submit();
  drain_submissions();
}
void wait_idle() {
  flush();
  vk_check(vkDeviceWaitIdle(R.device), "device idle");
}
void with_autorelease_pool(void (*fn)()) { host::with_autorelease_pool(fn); }
uint64_t frames_completed() { return R.completed.load(); }
uint64_t frame_count() { return std::atomic_ref<uint64_t>(R.frame).load(); }
static bool has_extension(const std::vector<VkExtensionProperties> &es,
                          const char *name) {
  return std::any_of(es.begin(), es.end(),
                     [&](auto &e) { return !strcmp(e.extensionName, name); });
}
static bool drc_vsync_shared() {
  return R.tv.window && R.tv.visible.load() && R.drc.window && R.drc.visible.load();
}
static int present_mode_without_second_vsync(unsigned offered) {
  if (offered & (1u << kPresentMailbox)) return kPresentMailbox;
  if (offered & (1u << kPresentImmediate)) return kPresentImmediate;
  return kPresentFifo;
}
static void make_swapchain(Screen &s) {
  vk_check(vkDeviceWaitIdle(R.device), "resize device idle");
  VkSurfaceCapabilitiesKHR caps;
  vk_check(vkGetPhysicalDeviceSurfaceCapabilitiesKHR(R.physicalDevice,
                                                     s.surface, &caps),
           "surface capabilities");
  uint32_t n = 0;
  vkGetPhysicalDeviceSurfaceFormatsKHR(R.physicalDevice, s.surface, &n,
                                       nullptr);
  std::vector<VkSurfaceFormatKHR> fs(n);
  vkGetPhysicalDeviceSurfaceFormatsKHR(R.physicalDevice, s.surface, &n,
                                       fs.data());
  if (fs.empty())
    throw std::runtime_error("No presentation formats");

  if (fs.size() == 1 && fs[0].format == VK_FORMAT_UNDEFINED)
    fs[0].format = VK_FORMAT_B8G8R8A8_SRGB;
  VkSurfaceFormatKHR format{};
  bool found = false;
#ifdef NSMBU_SDL_HOST
  const VkFormat preferred = VK_FORMAT_B8G8R8A8_SRGB;
#else

  const VkFormat preferred = s.srgb ? VK_FORMAT_B8G8R8A8_SRGB : VK_FORMAT_B8G8R8A8_UNORM;
#endif
  for (auto f : fs) {
    VkFormatProperties props;
    vkGetPhysicalDeviceFormatProperties(R.physicalDevice, f.format, &props);
    if (!(props.optimalTilingFeatures & VK_FORMAT_FEATURE_BLIT_DST_BIT))
      continue;
    if (!found || f.format == preferred) {
      format = f;
      found = true;
    }
    if (f.format == preferred &&
        f.colorSpace == VK_COLOR_SPACE_SRGB_NONLINEAR_KHR)
      break;
  }
  if (!found)
    throw std::runtime_error("No swapchain format supports image blits");
  VkExtent2D extent = caps.currentExtent;
  if (extent.width == UINT32_MAX)
    extent = {std::clamp<uint32_t>(std::max(s.width.load(), 1),
                                   caps.minImageExtent.width,
                                   caps.maxImageExtent.width),
              std::clamp<uint32_t>(std::max(s.height.load(), 1),
                                   caps.minImageExtent.height,
                                   caps.maxImageExtent.height)};
#ifdef __ANDROID__

  {
    int pw = 0, ph = 0;
    if (s.window && SDL_GetWindowSizeInPixels(s.window, &pw, &ph) && pw > 0 && ph > 0)
      extent = {uint32_t(pw), uint32_t(ph)};
    else if (caps.currentTransform & (VK_SURFACE_TRANSFORM_ROTATE_90_BIT_KHR |
                                      VK_SURFACE_TRANSFORM_ROTATE_270_BIT_KHR))
      std::swap(extent.width, extent.height);
  }
  const VkSurfaceTransformFlagBitsKHR transform =
      (caps.supportedTransforms & VK_SURFACE_TRANSFORM_IDENTITY_BIT_KHR)
          ? VK_SURFACE_TRANSFORM_IDENTITY_BIT_KHR : caps.currentTransform;
  LOG("[vulkan] swapchain %ux%u (surface reports %ux%u, transform %u)", extent.width, extent.height,
      caps.currentExtent.width, caps.currentExtent.height, unsigned(caps.currentTransform));
#else
  const VkSurfaceTransformFlagBitsKHR transform = caps.currentTransform;
#endif
  if (!(caps.supportedUsageFlags & VK_IMAGE_USAGE_TRANSFER_DST_BIT))
    throw std::runtime_error("Swapchain cannot receive scan-buffer blits");
  VkSwapchainCreateInfoKHR ci{VK_STRUCTURE_TYPE_SWAPCHAIN_CREATE_INFO_KHR};
  ci.surface = s.surface;
  ci.minImageCount = caps.minImageCount + 1;
  if (caps.maxImageCount)
    ci.minImageCount = std::min(ci.minImageCount, caps.maxImageCount);
  ci.minImageCount = std::max(ci.minImageCount, caps.minImageCount);
  ci.imageFormat = format.format;
  ci.imageColorSpace = format.colorSpace;
  ci.imageExtent = extent;
  ci.imageArrayLayers = 1;
  VkFormatProperties presentationFormat;
  vkGetPhysicalDeviceFormatProperties(R.physicalDevice,format.format,&presentationFormat);
  const bool shaderPresentation = (caps.supportedUsageFlags & VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT) &&
      (presentationFormat.optimalTilingFeatures & VK_FORMAT_FEATURE_COLOR_ATTACHMENT_BIT);
  const bool captureTransfer = present_capture_requested() &&
      (caps.supportedUsageFlags & VK_IMAGE_USAGE_TRANSFER_SRC_BIT) &&
      (presentationFormat.optimalTilingFeatures & VK_FORMAT_FEATURE_TRANSFER_SRC_BIT);
  ci.imageUsage = VK_IMAGE_USAGE_TRANSFER_DST_BIT |
      (shaderPresentation ? VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT : 0) |
      (captureTransfer ? VK_IMAGE_USAGE_TRANSFER_SRC_BIT : 0);
  ci.imageSharingMode = VK_SHARING_MODE_EXCLUSIVE;
  ci.preTransform = transform;
  ci.compositeAlpha = VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR;
  for (auto flag : {VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR,
                    VK_COMPOSITE_ALPHA_PRE_MULTIPLIED_BIT_KHR,
                    VK_COMPOSITE_ALPHA_POST_MULTIPLIED_BIT_KHR,
                    VK_COMPOSITE_ALPHA_INHERIT_BIT_KHR})
    if (caps.supportedCompositeAlpha & flag) {
      ci.compositeAlpha = flag;
      break;
    }
  uint32_t modeCount = 0;
  vk_check(vkGetPhysicalDeviceSurfacePresentModesKHR(R.physicalDevice,
      s.surface, &modeCount, nullptr), "presentation mode count");
  std::vector<VkPresentModeKHR> modes(modeCount);
  vk_check(vkGetPhysicalDeviceSurfacePresentModesKHR(R.physicalDevice,
      s.surface, &modeCount, modes.data()), "presentation modes");

  static const VkPresentModeKHR kModes[kPresentModes] = {VK_PRESENT_MODE_FIFO_KHR, VK_PRESENT_MODE_MAILBOX_KHR,
                                                         VK_PRESENT_MODE_IMMEDIATE_KHR};
  unsigned offered = 0;
  std::string offeredNames;
  for (int m = 0; m < kPresentModes; m++)
    if (std::find(modes.begin(), modes.end(), kModes[m]) != modes.end()) {
      offered |= 1u << m;
      offeredNames += std::string(offeredNames.empty() ? "" : ", ") + present_mode_name(m);
    }
  if (&s == &R.tv) set_present_modes_offered(offered);
  const int setting = effective_present_mode();
  const bool share = &s == &R.drc && setting == kPresentFifo && drc_vsync_shared();
  int wanted = share ? present_mode_without_second_vsync(offered) : setting;
  const bool fifoMailbox = setting == kPresentFifo && wanted == kPresentFifo && !gx2::uncapped() &&
      (offered & (1u << kPresentMailbox));
  if (fifoMailbox)
    wanted = kPresentMailbox;
  const int chosen = offered & (1u << wanted) ? wanted : kPresentFifo;
  ci.presentMode = kModes[chosen];
  if (chosen != s.presentMode || setting != s.presentWanted || share != s.presentShared)
    LOG("[vulkan] %s present mode %s (available: %s)%s%s", &s == &R.tv ? "TV" : "GamePad", present_mode_name(chosen),
        offeredNames.c_str(), chosen != wanted ? " - the requested mode is not offered" : "",
        fifoMailbox ? ", guest vsync already waits" : "");
  s.presentMode = chosen;
  s.presentWanted = setting;
  s.presentShared = share;

  if (&s == &R.tv) interp::set_present_vsync(chosen != kPresentImmediate);
  ci.clipped = VK_TRUE;
  ci.oldSwapchain = s.swapchain;
  VkSwapchainKHR sc;
  vk_check(vkCreateSwapchainKHR(R.device, &ci, nullptr, &sc),
           "create swapchain");
  reset_present_screen(s);
  if (s.swapchain)
    vkDestroySwapchainKHR(R.device, s.swapchain, nullptr);
  s.swapchain = sc;
  s.swapFormat = format.format;
  s.swapExtent = extent;
  vkGetSwapchainImagesKHR(R.device, sc, &n, nullptr);
  s.images.resize(n);
  vkGetSwapchainImagesKHR(R.device, sc, &n, s.images.data());
  LOG("[vulkan] %s swapchain %u images, present mode %s", &s == &R.tv ? "TV" : "GamePad", n,
      present_mode_name(s.presentMode));
  s.layouts.assign(n, VK_IMAGE_LAYOUT_UNDEFINED);
  s.imageUses.assign(n, {});
  prepare_present_screen(s,shaderPresentation,captureTransfer);
  s.resize = false;
}
#ifdef __ANDROID__

static std::atomic<bool> surfaceLost{false}, surfaceRecreate{false};
static void recreate_surface(Screen &s) {
  if (!SDL_GetPointerProperty(SDL_GetWindowProperties(s.window),
                              SDL_PROP_WINDOW_ANDROID_WINDOW_POINTER, nullptr)) {
    surfaceRecreate = true;
    return;
  }
  vk_check(vkDeviceWaitIdle(R.device), "surface recreation idle");
  reset_present_screen(s);
  if (s.swapchain)
    vkDestroySwapchainKHR(R.device, s.swapchain, nullptr);
  s.swapchain = VK_NULL_HANDLE;
  if (s.surface)
    vkDestroySurfaceKHR(R.instance, s.surface, nullptr);
  s.surface = VK_NULL_HANDLE;
  if (!NSMBU_CREATE_WINDOW_SURFACE(s.window, R.instance, nullptr, &s.surface)) {
    LOG("[vulkan] surface recreation: %s (retrying)", SDL_GetError());
    s.surface = VK_NULL_HANDLE;
    surfaceRecreate = true;
    return;
  }
  surfaceLost = false;
  s.resize = true;
  LOG("[vulkan] presentation surface recreated");
}

static bool SDLCALL lifecycle_watch(void *, SDL_Event *event) {
  if (event->type == SDL_EVENT_WILL_ENTER_BACKGROUND || event->type == SDL_EVENT_DID_ENTER_BACKGROUND) {
    ::hostui::set_app_focused(false);
    if (!surfaceLost.exchange(true))
      LOG("[vulkan] app in the background: presentation paused");
  } else if (event->type == SDL_EVENT_DID_ENTER_FOREGROUND) {
    ::hostui::set_app_focused(true);
    LOG("[vulkan] app in the foreground: new presentation surface");
    R.tv.visible = true;
    surfaceRecreate = true;
  }
  return true;
}
#endif

static bool async_present() {
  static const bool on = [] {
    const char *e = env::VK_ASYNC_PRESENT();
    return !e || std::atoi(e) != 0;
  }();
  return on;
}
struct AsyncPresentState {
  std::array<VkSemaphore, 3> acquire{};
  std::array<uint64_t, 3> serial{};
  std::array<size_t, 3> slot{};
  unsigned next = 0;
  std::vector<VkSemaphore> finished;
};
static AsyncPresentState asyncPresent[2];
static VkSemaphore new_semaphore() {
  VkSemaphoreCreateInfo si{VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO};
  VkSemaphore sem;
  vk_check(vkCreateSemaphore(R.device, &si, nullptr, &sem), "create presentation semaphore");
  return sem;
}
static uint64_t g_acquire_timeout_ns = UINT64_MAX;
static bool g_presented = false;
static void present(Screen &s) {
#ifdef __ANDROID__
  if (&s == &R.tv && s.window) {
    if (surfaceRecreate.exchange(false))
      recreate_surface(s);
    if (surfaceLost || !s.surface)
      return;
  }
#endif
  const int setting = effective_present_mode();
  const bool share = &s == &R.drc && setting == kPresentFifo && drc_vsync_shared();
  if (s.window && s.swapchain && (s.presentWanted != setting || s.presentShared != share))
    make_swapchain(s);
  if (!s.window || !s.visible || s.width <= 0 || s.height <= 0 || !s.scan ||
      !s.scan->image)
    return;
#ifdef NSMBU_SDL_HOST
  {
    const Uint64 flags = SDL_GetWindowFlags(s.window);
    if (flags & (SDL_WINDOW_MINIMIZED | SDL_WINDOW_OCCLUDED | SDL_WINDOW_HIDDEN))
      return;
  }
#endif
  if (!::hostui::run_in_background() && !::hostui::app_focused())
    return;
  VkFormatProperties sourceFormat;
  vkGetPhysicalDeviceFormatProperties(R.physicalDevice, s.scan->fmt.pixel,
                                      &sourceFormat);
  if (!(sourceFormat.optimalTilingFeatures & VK_FORMAT_FEATURE_BLIT_SRC_BIT))
    throw std::runtime_error(
        "Scan-buffer format does not support presentation blits");
  if (s.scan->fmt.kind != FormatInfo::FLOAT)
    throw std::runtime_error("Integer scan buffers cannot be blitted to "
                             "normalized presentation images");
  VkFilter filter = (sourceFormat.optimalTilingFeatures &
                     VK_FORMAT_FEATURE_SAMPLED_IMAGE_FILTER_LINEAR_BIT)
                        ? VK_FILTER_LINEAR
                        : VK_FILTER_NEAREST;
#ifndef NSMBU_SDL_HOST
  if (s.swapchain && (s.swapFormat == VK_FORMAT_B8G8R8A8_SRGB) != s.srgb.load())
    s.resize = true;
#endif
#ifdef __ANDROID__
  if (s.resize || !s.swapchain) {
    try {
      make_swapchain(s);
    } catch (const std::exception &e) {
      if (&s != &R.tv)
        throw;
      LOG("[vulkan] no swapchain (%s); waiting for a new surface", e.what());
      surfaceLost = true;
      return;
    }
  }
#else
  if (s.resize || !s.swapchain)
    make_swapchain(s);
#endif
  uint32_t index;
  auto& timing = screenTiming[&s == &R.tv ? 0 : 1];
  const bool async = async_present() && !present_capture_requested();
  AsyncPresentState &ap = asyncPresent[&s == &R.tv ? 0 : 1];
  VkSemaphore acquireSemaphore = s.acquired;
  unsigned acquireIndex = 0;
  if (async) {
    acquireIndex = ap.next;
    ap.next = (ap.next + 1) % ap.acquire.size();
    if (!ap.acquire[acquireIndex])
      ap.acquire[acquireIndex] = new_semaphore();

    auto &previous = R.submissions[ap.slot[acquireIndex]];
    if (ap.serial[acquireIndex] && previous.pending && previous.serial == ap.serial[acquireIndex])
      retire_submission(previous);
    acquireSemaphore = ap.acquire[acquireIndex];
  }
  VkResult ar = timed_call(timing.acquire, [&] {
    return vkAcquireNextImageKHR(R.device, s.swapchain, g_acquire_timeout_ns,
                                acquireSemaphore, VK_NULL_HANDLE, &index);
  }, rprof::kWaitAcquire);
  if (ar == VK_TIMEOUT || ar == VK_NOT_READY) return;
  if (ar == VK_ERROR_OUT_OF_DATE_KHR) {
    s.resize = true;
    return;
  }
#ifdef __ANDROID__
  if (ar == VK_ERROR_SURFACE_LOST_KHR) {
    surfaceLost = true;
    return;
  }
#endif
  if (ar != VK_SUBOPTIMAL_KHR)
    vk_check(ar, "acquire scan image");
  if (!draw_present_screen(s,index)) {
    transition_image(s.scan.get(), VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
                     VK_PIPELINE_STAGE_TRANSFER_BIT, VK_ACCESS_TRANSFER_READ_BIT);
    VkImageMemoryBarrier b{VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER};
    b.oldLayout = s.layouts[index];
    b.newLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
    b.srcQueueFamilyIndex = b.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    b.image = s.images[index];
    b.subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1};
    b.dstAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
    auto cmd = command_buffer();
    vkCmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT,
                         VK_PIPELINE_STAGE_TRANSFER_BIT, 0, 0, nullptr, 0,
                         nullptr, 1, &b);
    VkClearColorValue black{};
    vkCmdClearColorImage(cmd, s.images[index],
                         VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, &black, 1,
                         &b.subresourceRange);

    b.oldLayout = b.newLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
    b.srcAccessMask = b.dstAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
    vkCmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_TRANSFER_BIT,
                         VK_PIPELINE_STAGE_TRANSFER_BIT, 0, 0, nullptr, 0,
                         nullptr, 1, &b);
    const float ratio =
        std::min(float(s.swapExtent.width) / s.scan->extent.width,
                 float(s.swapExtent.height) / s.scan->extent.height);
    int w = int(s.scan->extent.width * ratio),
        h = int(s.scan->extent.height * ratio);
    int x = (s.swapExtent.width - w) / 2, y = (s.swapExtent.height - h) / 2;
    VkImageBlit blit{};
    blit.srcSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1};
    blit.srcOffsets[1] = {int(s.scan->extent.width), int(s.scan->extent.height),
                          1};
    blit.dstSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1};
    blit.dstOffsets[0] = {x, y, 0};
    blit.dstOffsets[1] = {x + w, y + h, 1};
    vkCmdBlitImage(cmd, s.scan->image, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
                   s.images[index], VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1,
                   &blit, filter);
    b.oldLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
    b.newLayout = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR;
    b.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
    b.dstAccessMask = 0;
    vkCmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_TRANSFER_BIT,
                         VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT, 0, 0, nullptr, 0,
                         nullptr, 1, &b);
    s.layouts[index] = b.newLayout;
    s.imageUses[index] = {};
    derive_dependency(s.imageUses[index], VK_PIPELINE_STAGE_TRANSFER_BIT, VK_ACCESS_TRANSFER_WRITE_BIT);
  }
  record_present_capture(s,index);
  VkSemaphore finishedSemaphore = s.finished;
  if (async) {
    if (ap.finished.size() != s.images.size()) {
      vk_check(vkDeviceWaitIdle(R.device), "presentation semaphores idle");
      for (VkSemaphore f : ap.finished)
        vkDestroySemaphore(R.device, f, nullptr);
      ap.finished.clear();
      for (size_t i = 0; i < s.images.size(); i++)
        ap.finished.push_back(new_semaphore());
    }
    finishedSemaphore = ap.finished[index];
    const size_t slot = R.activeSubmission;
    submit(acquireSemaphore, finishedSemaphore, true);
    ap.slot[acquireIndex] = slot;
    ap.serial[acquireIndex] = R.submissions[slot].serial;
  } else {
    submit(s.acquired, s.finished);
  }
  finish_present_capture(s);
  VkPresentInfoKHR pi{VK_STRUCTURE_TYPE_PRESENT_INFO_KHR};
  pi.waitSemaphoreCount = 1;
  pi.pWaitSemaphores = &finishedSemaphore;
  pi.swapchainCount = 1;
  pi.pSwapchains = &s.swapchain;
  pi.pImageIndices = &index;
  VkResult pr = timed_call(timing.present, [&] {
    return vkQueuePresentKHR(R.queue, &pi);
  }, rprof::kWaitPresent);
  if (pr == VK_SUCCESS || pr == VK_SUBOPTIMAL_KHR) g_presented = true;
#ifdef __ANDROID__

  if (pr == VK_ERROR_OUT_OF_DATE_KHR)
    s.resize = true;
  else if (pr == VK_SUBOPTIMAL_KHR) {
  } else if (pr == VK_ERROR_SURFACE_LOST_KHR)
    surfaceLost = true;
  else
    vk_check(pr, "present scan buffer");
#else
  if (pr == VK_ERROR_OUT_OF_DATE_KHR || pr == VK_SUBOPTIMAL_KHR)
    s.resize = true;
  else
    vk_check(pr, "present scan buffer");
#endif
  if (!async)
    vk_check(timed_call(timing.idle, [&] { return vkQueueWaitIdle(R.queue); }, rprof::kWaitGpu),
             "present completion");
}
void copy_to_scan(uint32_t cb, uint32_t target) {
  Surface *src = surface_from_color_buffer(cb);
  if (!src)
    return;
  Screen &s = target == 4 ? R.drc : R.tv;
  if (!s.scan || s.scan->extent.width != src->extent.width ||
      s.scan->extent.height != src->extent.height ||
      s.scan->fmt.pixel != src->fmt.pixel) {
    if (s.scan)
      destroy_surface_image(s.scan.get());
    s.scan = std::make_unique<Surface>();
    auto *d = s.scan.get();
    d->width = src->extent.width;
    d->height = src->extent.height;
    d->format = src->format;
    d->fmt = src->fmt;
    d->scale = 1;
    create_surface_image(d, false);
  }
  resample(src, s.scan.get(), 1);
  mark_gpu_written(s.scan.get());
}

static void queue_tv_png(const std::string &path);

static void dump_scan(Screen &s, const std::string &path) {
  if (!s.scan || !s.scan->image)
    return;
  try {
    write_rgba_png(path, s.scan->extent.width, s.scan->extent.height,
                   read_surface_rgba(*s.scan, s.srgb.load()));
    LOG("[gfx] wrote %s (%ux%u)", path.c_str(), s.scan->extent.width, s.scan->extent.height);
  } catch (const std::exception &e) {
    LOG("[gfx] cannot write %s: %s", path.c_str(), e.what());
  }
}

static void request_present_dump(const std::string& path) { gfx::request_present_dump(path); }
static std::atomic<bool> captureRequested{false};
void request_capture() { captureRequested = true; }
static void frame_dumps(uint64_t frame) {
  static const std::vector<uint64_t> frames = frame_dump::parse_list(env::DUMP_FRAMES());
  const uint64_t dumpFrame = script_frame::frame(frame);
  if (std::find(frames.begin(), frames.end(), dumpFrame) != frames.end()) {
    dump_scan(R.tv, "frame_" + std::to_string(dumpFrame) + ".png");
    dump_scan(R.drc, "frame_" + std::to_string(dumpFrame) + "_drc.png");
    if (env::DUMP_PRESENT())
      request_present_dump("frame_" + std::to_string(dumpFrame) + "_present.png");
  }

  static const std::vector<uint64_t> loadFrames = [] {
    std::vector<uint64_t> f;
    if (const char* e = env::DUMP_LOAD_FRAMES())
      for (const char* p = e; *p;) {
        char* end;
        auto value = strtoull(p, &end, 10);
        if (end == p) break;
        f.push_back(value);
        p = end;
        while (*p == ',') ++p;
      }
    return f;
  }();
  const uint64_t loaded = loadFrames.empty() ? 0 : ss::last_load_frame();
  if (loaded && frame >= loaded &&
      std::find(loadFrames.begin(), loadFrames.end(), frame - loaded) != loadFrames.end()) {
    const auto stem = "load_frame_" + std::to_string(frame - loaded);
    dump_scan(R.tv, stem + ".png");
    dump_scan(R.drc, stem + "_drc.png");
    if (env::DUMP_PRESENT()) request_present_dump(stem + "_present.png");
    LOG("[gfx] load-relative capture: load frame %llu, frame %llu, offset %llu",
        (unsigned long long)loaded, (unsigned long long)frame,
        (unsigned long long)(frame - loaded));
  }

  static const uint64_t scripted = env::CAPTURE() && !env::CAPTURE_PATH()
                                       ? strtoull(env::CAPTURE(), nullptr, 10) : ~0ull;
  if (captureRequested.exchange(false) || frame == scripted) {
    char stamp[32];
    time_t t = time(nullptr);
    strftime(stamp, sizeof stamp, "%Y%m%d-%H%M%S", localtime(&t));
    host::ensure_log_dir();
    const std::string dir = host::log_dir() + "/" + stamp;
    std::error_code ec;
    std::filesystem::create_directories(dir, ec);
    dump_scan(R.tv, dir + "/tv.png");
    dump_scan(R.drc, dir + "/gamepad.png");
    request_present_dump(dir + "/present.png");
    LOG("[gfx] capture of frame %llu written to %s", (unsigned long long)frame, dir.c_str());
  }

  int title_index = 0;
  static bool title_lit = false;
  if (!title_lit && env::DUMP_TITLE.integer_or(0) > 0 && R.tv.scan && R.tv.scan->image &&
      (frame_dump::title_characters() || interp::course_play_active())) {
    try {
      const std::vector<uint8_t> px = read_surface_rgba(*R.tv.scan, R.tv.srgb.load());
      if (frame_dump::picture_bright(px.data(), (int)R.tv.scan->extent.width, (int)R.tv.scan->extent.height)) {
        title_lit = true;
        frame_dump::title_note_lit();
        if (interp::output_fps() <= 60) frame_dump::title_arm();
      }
    } catch (const std::exception&) {
    }
  }
  if (frame_dump::title_due(title_index)) {
    const frame_dump::PresentMark mark = frame_dump::current_mark();
    char name[64];
    snprintf(name, sizeof name, "title_%04d_%s.png", title_index, mark.blend ? "blend" : "logic");
    queue_tv_png(name);
    LOG("[gfx] title %04d %s t %.3f", title_index, mark.blend ? "interpolated" : "logic", mark.t);
  }
}

namespace {
struct PendingShot {
  std::string path;
  bool tv = false;
  uint64_t frame = 0;
  Buffer buffer{};
  uint32_t width = 0, height = 0;
  bool bgra = false;
  size_t slot = 0;
  uint64_t serial = 0;
};
std::vector<PendingShot> pendingShots;
std::mutex shotBuffersMu;
std::vector<Buffer> shotBuffersDone;
}
static void queue_tv_png(const std::string &path) {
  if (!R.tv.scan || !R.tv.scan->image) return;
  PendingShot p;
  p.path = path;
  p.tv = true;
  p.frame = R.frame + 1;
  try {
    if (!record_screenshot(R.tv, p.buffer, p.width, p.height, p.bgra)) return;
  } catch (const std::exception &e) {
    LOG("[gfx] cannot copy %s: %s", path.c_str(), e.what());
    return;
  }
  p.slot = R.activeSubmission;
  pendingShots.push_back(std::move(p));
}
static void take_screenshots(const gfx::PresentPlan &plan) {
  std::string tvPath, drcPath;
  if (!R.tv.scan || !R.tv.scan->image || !screenshot::take(R.frame + 1, tvPath, drcPath))
    return;
  auto shoot = [&](Screen &s, const std::string &path, bool tv) {
    PendingShot p;
    p.path = path;
    p.tv = tv;
    p.frame = R.frame + 1;
    try {
      if (!record_screenshot(s, p.buffer, p.width, p.height, p.bgra)) {
        screenshot::write_async(path, 0, 0, 0, nullptr, nullptr, p.frame, tv);
        return;
      }
    } catch (const std::exception &e) {
      LOG("[screenshot] %s: %s", path.c_str(), e.what());
      screenshot::write_async(path, 0, 0, 0, nullptr, nullptr, p.frame, tv);
      return;
    }
    p.slot = R.activeSubmission;
    pendingShots.push_back(std::move(p));
  };
  const auto t0 = std::chrono::steady_clock::now();
  shoot(R.tv, tvPath, true);

  if (!drcPath.empty() && R.drc.scan && R.drc.scan->image && (plan.drc_window || plan.pip_on || plan.drc_only))
    shoot(R.drc, drcPath, false);
  else if (!drcPath.empty()) {
    LOG("[screenshot] GamePad picture not shown: only the TV picture saved");
    screenshot::write_async(drcPath, 0, 0, 0, nullptr, nullptr, R.frame + 1, false);
  }
  LOG("[screenshot] frame %llu: recorded in %.2f ms (render thread)", (unsigned long long)(R.frame + 1),
      std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - t0).count());
}
static void service_screenshots() {
  {
    std::lock_guard<std::mutex> lk(shotBuffersMu);
    for (auto &b : shotBuffersDone)
      defer_buffer(b);
    shotBuffersDone.clear();
  }
  for (auto it = pendingShots.begin(); it != pendingShots.end();) {
    auto &slot = R.submissions[it->slot];
    if (!it->serial) {
      it->serial = slot.serial;
      ++it;
      continue;
    }

    const bool done = !slot.pending || slot.serial != it->serial || vkGetFenceStatus(R.device, slot.fence) == VK_SUCCESS;
    if (!done) {
      ++it;
      continue;
    }
    Buffer b = it->buffer;
    std::shared_ptr<const void> owner(nullptr, [b](const void *) {
      std::lock_guard<std::mutex> lk(shotBuffersMu);
      shotBuffersDone.push_back(b);
    });
    screenshot::write_async(it->path, it->width, it->height, size_t(it->width) * 4, static_cast<const uint8_t *>(b.mapped),
                            std::move(owner), it->frame, it->tv, it->bgra);
    it = pendingShots.erase(it);
  }
}
void repeat_present() {
  render::clear_deferred_present();
  Surface *tvScan = R.tv.scan && R.tv.scan->image ? R.tv.scan.get() : nullptr;
  if (!tvScan) {
    render::hold_repeat();
    return;
  }
  Surface *drcScan = R.drc.scan && R.drc.scan->image ? R.drc.scan.get() : nullptr;
#ifdef NSMBU_SDL_HOST
  gfx::g_filter = scale_filter();
  const float layerW = float(R.tv.width.load()), layerH = float(R.tv.height.load());
#else
  const float layerW = float(R.tv.swapExtent.width), layerH = float(R.tv.swapExtent.height);
#endif
  gfx::PresentPlan plan = gfx::display_plan(
      tvScan, float(tvScan->extent.width), float(tvScan->extent.height),
      drcScan, drcScan ? float(drcScan->extent.width) : 0, drcScan ? float(drcScan->extent.height) : 0,
      layerW, layerH, R.frame);
  set_present_plan(&plan);
  gx2::pace_host_present();
  set_overlay_draw(overlay::frame(plan.dw > 0 ? plan.dw : layerW, plan.dh > 0 ? plan.dh : layerH, overlay_renderer_init));
  g_acquire_timeout_ns = 0;
  g_presented = false;
  present(R.tv);
  if (plan.drc_window) present(R.drc);
  g_acquire_timeout_ns = UINT64_MAX;
  if (async_present()) flush_async();
  else flush();
  set_present_plan(nullptr);
  if (g_presented) render::note_host_present();
  else {
    overlay::undo_frame_sample();
    render::hold_repeat();
  }
}
void swap() {
  const int64_t delay = interp::pacing::bunched_present_delay_ns(
      interp::course_play_active(), interp::output_fps(), render::since_host_present_ns());
  if (delay > 0) render::arm_deferred_present_in(delay);
  else render::clear_deferred_present();
  g_presented = false;
  service_screenshots();
  service_captures();
  frame_dumps(R.frame + 1);

  Surface *tvScan = R.tv.scan && R.tv.scan->image ? R.tv.scan.get() : nullptr;
  Surface *drcScan = R.drc.scan && R.drc.scan->image ? R.drc.scan.get() : nullptr;
#ifdef NSMBU_SDL_HOST
  gfx::g_filter = scale_filter();
  const float layerW = float(R.tv.width.load()), layerH = float(R.tv.height.load());
#else
  const float layerW = float(R.tv.swapExtent.width), layerH = float(R.tv.swapExtent.height);
#endif
  gfx::PresentPlan plan = gfx::display_plan(
      tvScan, tvScan ? float(tvScan->extent.width) : 0, tvScan ? float(tvScan->extent.height) : 0,
      drcScan, drcScan ? float(drcScan->extent.width) : 0, drcScan ? float(drcScan->extent.height) : 0,
      layerW, layerH, R.frame + 1);
#ifdef __ANDROID__

  if (perf_hint::fps60_chosen())
    plan.button_dot = interp::mode() != 0 ? 1 : 2;
#endif
  set_present_plan(&plan);

  if (delay > 0) set_overlay_draw(nullptr);
  else {
    gx2::pace_host_present();
    set_overlay_draw(overlay::frame(plan.dw > 0 ? plan.dw : layerW, plan.dh > 0 ? plan.dh : layerH, overlay_renderer_init));
  }
  take_screenshots(plan);
  bool sampled[2] = {};
  if (plan.sample_auto && drcScan) {
    sampled[0] = record_signature(0, *drcScan, R.drc.srgb.load());
    sampled[1] = sampled[0] && tvScan && record_signature(1, *tvScan, R.tv.srgb.load());
  }
  if (delay <= 0) {
    present(R.tv);
    if (plan.drc_window)
      present(R.drc);
  }

  if (async_present() && !sampled[0])
    flush_async();
  else
    flush();
  if (sampled[0]) {
    std::vector<float> d = read_signature(0), t = sampled[1] ? read_signature(1) : std::vector<float>{};
    gfx::display_auto_signature(d, sampled[1] ? &t : nullptr, R.frame + 1);
  }
  for (auto &path : gfx::display_take_present_dumps()) {
    if (!tvScan || plan.dw < 1 || plan.dh < 1)
      continue;
    try {
      write_rgba_png(path, uint32_t(plan.dw), uint32_t(plan.dh),
                     compose_offscreen(R.tv, uint32_t(plan.dw), uint32_t(plan.dh), R.tv.srgb.load()));

      uint32_t drcW = R.drc.swapchain ? R.drc.swapExtent.width : 0, drcH = R.drc.swapchain ? R.drc.swapExtent.height : 0;
#ifdef NSMBU_SDL_HOST
      if (!R.drc.swapchain && R.drc.window)
        drcW = uint32_t(std::max(0, R.drc.width.load())), drcH = uint32_t(std::max(0, R.drc.height.load()));
#endif
      if (plan.drc_window && drcScan && drcW && drcH) {
        std::string p = path;
        size_t dot = p.rfind(".png");
        p.insert(dot == std::string::npos ? p.size() : dot, "_drc");
        write_rgba_png(p, drcW, drcH, compose_offscreen(R.drc, drcW, drcH, R.drc.srgb.load()));
      }
      gfx::display_log_present_dump(path, plan, float(tvScan->extent.width), float(tvScan->extent.height));
    } catch (const std::exception &e) {
      LOG("[gfx] present dump %s failed: %s", path.c_str(), e.what());
    }
  }
  set_present_plan(nullptr);
  if (g_presented) render::note_host_present();
  service_screenshots();
  std::atomic_ref<uint64_t>(R.frame).fetch_add(1);
  R.completed = R.frame;
#ifdef __ANDROID__
  if (tvScan) drivers::frame_done();
#endif
  buffer_cache_end_frame();
  report_gpu_timestamps();
  perf_hint::frame_done();
  checkpoint_pipeline_cache();
  vk::checkpoint_shader_cache(R.frame);
  latch_res_scale();
  if (perf_enabled() || cpu_only_stats_enabled()) {
    static auto start = std::chrono::steady_clock::now();
    static auto previousSwap = start;
    static bool havePreviousSwap = false;
    static std::array<double, 120> swapIntervals{};
    static size_t intervalCount = 0, slowIntervals = 0;
    static uint64_t frame=0,draws=0,bytes=0,allocs=0,passes=0;
    static uint64_t descriptorLookups=0,descriptorHits=0,descriptorAllocations=0,descriptorFastHits=0;
    static uint64_t vertexDeclaredBytes=0,vertexCopiedBytes=0;
    static uint64_t vertexReuseChecks=0,vertexReuseHits=0,vertexReuseBytes=0,vertexReuseCompareNs=0;
    static uint64_t batchSubmissions=0,fetchLookups=0,fetchLastHits=0;
    static uint64_t stateHashLookups=0,stateHashMemoHits=0,stateHashBytes=0;
    static uint64_t vertexBindCalls=0,vertexBindSkips=0;
    static uint64_t vertexHistoryRequests=0,vertexHistoryMatches=0,vertexHistoryBytes=0;
    static uint64_t vertexHistoryReuseChecks=0,vertexHistoryReuseHits=0,vertexHistoryReuseBytes=0;
    static std::array<uint64_t,7> vertexHistoryDistances{};
    static Renderer::CpuPreparationStats previousPreparation;
    static SparseHashStats previousSparse;
    static gx2::ShaderKeyDirtyStats previousShaderDirty;
#ifndef _WIN32
    auto threadCpuNs = []() -> uint64_t {
      timespec time{};
      if (clock_gettime(CLOCK_THREAD_CPUTIME_ID, &time)) return 0;
      return uint64_t(time.tv_sec) * 1000000000ull + uint64_t(time.tv_nsec);
    };
    static uint64_t previousThreadCpu = threadCpuNs();
#endif
    auto now=std::chrono::steady_clock::now();
    if (havePreviousSwap && intervalCount < swapIntervals.size()) {
      double ms = std::chrono::duration<double, std::milli>(now-previousSwap).count();
      swapIntervals[intervalCount++] = ms;
      if (ms > 40.0) ++slowIntervals;
    }
    previousSwap=now;havePreviousSwap=true;
    if (R.frame-frame>=120) {
      double seconds=std::chrono::duration<double>(now-start).count();
#ifndef _WIN32
      const uint64_t currentThreadCpu = threadCpuNs();
      if (previousThreadCpu && currentThreadCpu >= previousThreadCpu)
        LOG("[vulkan thread CPU] %.3f ms/frame (excludes sleeping and GPU waits)",
            double(currentThreadCpu - previousThreadCpu) / 1e6 / (R.frame - frame));
      previousThreadCpu = currentThreadCpu;
#endif
      LOG("[vulkan perf] frame %llu %.2f fps, %.1f draws/frame, %.2f MiB uploads/frame, %llu new upload blocks, %.1f passes/frame",
          (unsigned long long)R.frame,(R.frame-frame)/seconds,
          double(R.drawCount-draws)/(R.frame-frame),double(R.uploadBytes-bytes)/(R.frame-frame)/(1<<20),
          (unsigned long long)(R.uploadAllocations-allocs), double(R.renderPassCount-passes)/(R.frame-frame));
      if (intervalCount) {
        std::sort(swapIntervals.begin(), swapIntervals.begin()+intervalCount);

        size_t p50=(intervalCount+1)/2-1, p95=(intervalCount*95+99)/100-1;
        LOG("[vulkan pacing] %llu intervals p50 %.2f ms p95 %.2f ms max %.2f ms; >40 ms %llu",
            (unsigned long long)intervalCount,swapIntervals[p50],swapIntervals[p95],
            swapIntervals[intervalCount-1],(unsigned long long)slowIntervals);
      }
      intervalCount=0;slowIntervals=0;
      auto ss=vk::shader_stats();
      LOG("[vulkan shaders] lookups %llu last hits %llu variants %llu compiles %llu total compile %.1f ms; %llu programs, %llu keys, %llu shaders (%llu keys shared), %llu sharing a module",
          (unsigned long long)ss.lookups,(unsigned long long)ss.lastHits,(unsigned long long)ss.variantHits,(unsigned long long)ss.compiles,ss.compileNs/1e6,
          (unsigned long long)ss.programs,(unsigned long long)ss.variantKeys,(unsigned long long)ss.shaders,(unsigned long long)ss.variantAliases,(unsigned long long)ss.moduleAliases);
      if (ss.verifyChecks || ss.verifySplitKeys)
        LOG("[vulkan shader key verify] %llu pre-narrowing keys checked, %llu violations, %llu split; %.1f ms",
            (unsigned long long)ss.verifyChecks,(unsigned long long)ss.verifyViolations,
            (unsigned long long)ss.verifySplitKeys,ss.verifyNs/1e6);
      LOG("[vulkan shader disk] hits %llu; memory reuses %llu; SPIR-V compiles %llu %.1f ms; decompile %.1f ms; loads %llu %.1f ms; saves %llu worker %.1f ms %.2f MiB; snapshots %.1f ms",
          (unsigned long long)ss.diskHits,(unsigned long long)ss.spirvReuseHits,(unsigned long long)ss.spirvCompiles,ss.spirvCompileNs/1e6,
          ss.decompileNs/1e6,(unsigned long long)ss.diskLoads,ss.diskLoadNs/1e6,
          (unsigned long long)ss.diskSaves,ss.diskSaveNs/1e6,ss.diskSavedBytes/double(1<<20),ss.diskSnapshotNs/1e6);
      LOG("[vulkan pipelines] creates %llu total %.1f ms",
          (unsigned long long)R.pipelineCreates,R.pipelineCreateNs/1e6);
      {

        extern uint64_t g_stat_full_checks, g_stat_uploads;
        static uint64_t checks=0, uploads=0;
        uint64_t faults, protectedPages;
        wwatch::take_stats(faults, protectedPages);
        LOG("[vulkan textures] %llu full checks, %llu uploads, %llu page write faults, %llu pages protected",
            (unsigned long long)(g_stat_full_checks-checks),(unsigned long long)(g_stat_uploads-uploads),
            (unsigned long long)faults,(unsigned long long)protectedPages);
        checks=g_stat_full_checks;uploads=g_stat_uploads;
      }
      buffer_cache_report(double(R.frame-frame));

      if (perf_enabled()) {
      double frames = double(R.frame-frame);
      LOG("[vulkan fetch memo] %.1f lookups/frame %.1f last hits/frame",
          (ss.fetchLookups-fetchLookups)/frames,(ss.fetchLastHits-fetchLastHits)/frames);
      fetchLookups=ss.fetchLookups;fetchLastHits=ss.fetchLastHits;
      LOG("[vulkan state hash memo] %.1f lookups/frame %.1f hits/frame %.3f MiB hashed/frame",
          (ss.stateHashLookups-stateHashLookups)/frames,
          (ss.stateHashMemoHits-stateHashMemoHits)/frames,
          (ss.stateHashBytes-stateHashBytes)/frames/(1<<20));
      stateHashLookups=ss.stateHashLookups;
      stateHashMemoHits=ss.stateHashMemoHits;stateHashBytes=ss.stateHashBytes;
      uint64_t currentBatchSubmissions=draw_batch_submissions();
      LOG("[vulkan draw batches] %.2f extra submissions/frame",
          (currentBatchSubmissions-batchSubmissions)/frames);
      batchSubmissions=currentBatchSubmissions;
      LOG("[vulkan vertex snapshots] declared %.2f MiB/frame; prepared %.2f MiB/frame",
          (R.vertexDeclaredBytes-vertexDeclaredBytes)/frames/(1<<20),
          (R.vertexCopiedBytes-vertexCopiedBytes)/frames/(1<<20));
      vertexDeclaredBytes=R.vertexDeclaredBytes;vertexCopiedBytes=R.vertexCopiedBytes;
      LOG("[vulkan vertex binds] %.1f actual/frame %.1f skipped/frame",
          (R.vertexBindCalls-vertexBindCalls)/frames,
          (R.vertexBindSkips-vertexBindSkips)/frames);
      vertexBindCalls=R.vertexBindCalls;vertexBindSkips=R.vertexBindSkips;
      LOG("[vulkan vertex history] %.1f requests/frame %.1f nonconsecutive keys/frame %.3f MiB potential/frame; distances 1..7 %.1f %.1f %.1f %.1f %.1f %.1f %.1f",
          (R.vertexHistoryRequests-vertexHistoryRequests)/frames,
          (R.vertexHistoryMatches-vertexHistoryMatches)/frames,
          (R.vertexHistoryBytes-vertexHistoryBytes)/frames/(1<<20),
          (R.vertexHistoryDistances[0]-vertexHistoryDistances[0])/frames,
          (R.vertexHistoryDistances[1]-vertexHistoryDistances[1])/frames,
          (R.vertexHistoryDistances[2]-vertexHistoryDistances[2])/frames,
          (R.vertexHistoryDistances[3]-vertexHistoryDistances[3])/frames,
          (R.vertexHistoryDistances[4]-vertexHistoryDistances[4])/frames,
          (R.vertexHistoryDistances[5]-vertexHistoryDistances[5])/frames,
          (R.vertexHistoryDistances[6]-vertexHistoryDistances[6])/frames);
      vertexHistoryRequests=R.vertexHistoryRequests;vertexHistoryMatches=R.vertexHistoryMatches;
      vertexHistoryBytes=R.vertexHistoryBytes;vertexHistoryDistances=R.vertexHistoryDistances;
      LOG("[vulkan vertex history reuse] %.1f checks/frame %.1f hits/frame %.3f MiB reused/frame",
          (R.vertexHistoryReuseChecks-vertexHistoryReuseChecks)/frames,
          (R.vertexHistoryReuseHits-vertexHistoryReuseHits)/frames,
          (R.vertexHistoryReuseBytes-vertexHistoryReuseBytes)/frames/(1<<20));
      vertexHistoryReuseChecks=R.vertexHistoryReuseChecks;
      vertexHistoryReuseHits=R.vertexHistoryReuseHits;vertexHistoryReuseBytes=R.vertexHistoryReuseBytes;
      LOG("[vulkan vertex reuse] %.1f checks/frame; %.1f hits/frame; %.2f MiB reused/frame; compare %.3f ms/frame",
          (R.vertexReuseChecks-vertexReuseChecks)/frames,
          (R.vertexReuseHits-vertexReuseHits)/frames,
          (R.vertexReuseBytes-vertexReuseBytes)/frames/(1<<20),
          (R.vertexReuseCompareNs-vertexReuseCompareNs)/frames/1e6);
      vertexReuseChecks=R.vertexReuseChecks;vertexReuseHits=R.vertexReuseHits;
      vertexReuseBytes=R.vertexReuseBytes;vertexReuseCompareNs=R.vertexReuseCompareNs;
      uint64_t descriptorChecks=R.descriptorLookups-descriptorLookups;
      uint64_t descriptorReuses=R.descriptorCacheHits-descriptorHits;
      LOG("[vulkan descriptors] %.1f lookups/frame; %.1f allocations/frame; %.1f%% reused; %.1f%% consecutive",
          descriptorChecks/frames,(R.descriptorAllocations-descriptorAllocations)/frames,
          descriptorChecks ? 100.0*descriptorReuses/descriptorChecks : 0.0,
          descriptorChecks ? 100.0*(R.descriptorFastHits-descriptorFastHits)/descriptorChecks : 0.0);
      descriptorLookups=R.descriptorLookups;descriptorHits=R.descriptorCacheHits;
      descriptorAllocations=R.descriptorAllocations;descriptorFastHits=R.descriptorFastHits;
      const auto& prep=R.cpuPreparation;
      const auto sparse=sparse_hash_stats();
      LOG("[vulkan sparse hash memo] %.1f checks/frame %.1f hits/frame %.3f MiB fresh samples/frame %.1f mixed words/frame %.1f overflow/frame",
          (sparse.checks-previousSparse.checks)/frames,
          (sparse.memoHits-previousSparse.memoHits)/frames,
          (sparse.sampleBytes-previousSparse.sampleBytes)/frames/(1<<20),
          (sparse.mixerWords-previousSparse.mixerWords)/frames,
          (sparse.overflows-previousSparse.overflows)/frames);
      previousSparse=sparse;
      const auto dirty=gx2::shader_key_dirty_stats();
      LOG("[vulkan shader key dirty] %.1f changed batches/frame %.1f baseline bumps/frame %.1f actual bumps/frame %.1f avoided/frame %.1f masked words/frame",
          (dirty.changedBatches-previousShaderDirty.changedBatches)/frames,
          (dirty.baselineWouldBumps-previousShaderDirty.baselineWouldBumps)/frames,
          (dirty.actualBumps-previousShaderDirty.actualBumps)/frames,
          (dirty.avoidedBumps-previousShaderDirty.avoidedBumps)/frames,
          (dirty.maskedWords-previousShaderDirty.maskedWords)/frames);
      previousShaderDirty=dirty;
      LOG("[vulkan sampler memo] %.1f requests/frame %.1f hits/frame %.1f map lookups/frame",
          (prep.samplerRequests-previousPreparation.samplerRequests)/frames,
          (prep.samplerMemoHits-previousPreparation.samplerMemoHits)/frames,
          (prep.samplerMapLookups-previousPreparation.samplerMapLookups)/frames);
      LOG("[vulkan uniform preparation] %.1f packs/frame %.3f MiB/frame; %llu scratch growths %.3f KiB added; %.1f snapshot requests/frame %.3f MiB/frame",
          (prep.packCalls-previousPreparation.packCalls)/frames,
          (prep.packBytes-previousPreparation.packBytes)/frames/(1<<20),
          (unsigned long long)(prep.packCapacityGrowths-previousPreparation.packCapacityGrowths),
          (prep.packCapacityGrowthBytes-previousPreparation.packCapacityGrowthBytes)/1024.0,
          (prep.uniformSnapshotCalls-previousPreparation.uniformSnapshotCalls)/frames,
          (prep.uniformSnapshotBytes-previousPreparation.uniformSnapshotBytes)/frames/(1<<20));
      LOG("[vulkan uniform reuse] %.1f checks/frame %.1f comparisons/frame %.1f hits/frame %.3f MiB avoided/frame",
          (prep.uniformReuseChecks-previousPreparation.uniformReuseChecks)/frames,
          (prep.uniformReuseComparisons-previousPreparation.uniformReuseComparisons)/frames,
          (prep.uniformReuseHits-previousPreparation.uniformReuseHits)/frames,
          (prep.uniformReuseBytes-previousPreparation.uniformReuseBytes)/frames/(1<<20));
      const uint64_t candidates=prep.descriptorBindCandidates-previousPreparation.descriptorBindCandidates;
      const double percentage=candidates?100.0/candidates:0.0;
      LOG("[vulkan bind opportunities] %.1f candidates/frame; exact whole %.2f%% VS %.2f%% PS %.2f%%; %.1f actual binds/frame %.1f skipped sets/frame",
          candidates/frames,(prep.descriptorWholeMatches-previousPreparation.descriptorWholeMatches)*percentage,
          (prep.descriptorStageMatches[0]-previousPreparation.descriptorStageMatches[0])*percentage,
          (prep.descriptorStageMatches[1]-previousPreparation.descriptorStageMatches[1])*percentage,
          (prep.descriptorBindCalls-previousPreparation.descriptorBindCalls)/frames,
          (prep.descriptorSkippedSets-previousPreparation.descriptorSkippedSets)/frames);
      LOG("[vulkan pipeline lookup] %.1f requests/frame; %.1f last hits; %.1f lookaside hits; %.1f map lookups",
          (prep.pipelineLookups-previousPreparation.pipelineLookups)/frames,
          (prep.pipelineLastHits-previousPreparation.pipelineLastHits)/frames,
          (prep.pipelineLookasideHits-previousPreparation.pipelineLookasideHits)/frames,
          (prep.pipelineMapLookups-previousPreparation.pipelineMapLookups)/frames);
      previousPreparation=prep;
      LOG("[vulkan waits] submit %.2f/frame %.2f ms/frame; present submit %.2f/frame %.2f ms/frame",
          submitWait.count/frames, submitWait.ns/1e6/frames,
          presentSubmitWait.count/frames, presentSubmitWait.ns/1e6/frames);
      for (int screen = 0; screen < 2; ++screen) {
        auto& t = screenTiming[screen];
        LOG("[vulkan waits %s] acquire %.2f/frame %.2f ms/frame; present %.2f/frame %.2f ms/frame; queue idle %.2f/frame %.2f ms/frame",
            screen ? "DRC" : "TV", t.acquire.count/frames, t.acquire.ns/1e6/frames,
            t.present.count/frames, t.present.ns/1e6/frames,
            t.idle.count/frames, t.idle.ns/1e6/frames);
        t = {};
      }
      submitWait = {}; presentSubmitWait = {};
      }
      start=now;frame=R.frame;draws=R.drawCount;bytes=R.uploadBytes;allocs=R.uploadAllocations;passes=R.renderPassCount;
    }
  }
}
void set_tv_format(uint32_t format, bool tv) {
  (tv ? R.tv : R.drc).srgb = (format & 0x400) != 0;
}
static VKAPI_ATTR VkBool32 VKAPI_CALL debug_message(
    VkDebugUtilsMessageSeverityFlagBitsEXT severity, VkDebugUtilsMessageTypeFlagsEXT,
    const VkDebugUtilsMessengerCallbackDataEXT *data, void *) {
  LOG("[vulkan validation %s] %s: %s",
          severity & VK_DEBUG_UTILS_MESSAGE_SEVERITY_ERROR_BIT_EXT ? "error" : "warning",
          data->pMessageIdName ? data->pMessageIdName : "validation", data->pMessage);
  return VK_FALSE;
}

static uint32_t device_api_version(const VkPhysicalDeviceProperties &p) {
  static const uint32_t forced = [] {
    unsigned major, minor;
    const char *e = env::VK_FORCE_API();
    return e && sscanf(e, "%u.%u", &major, &minor) == 2 ? VK_MAKE_API_VERSION(0, major, minor, 0) : ~0u;
  }();
  return forced < VK_MAKE_API_VERSION(0, VK_API_VERSION_MAJOR(p.apiVersion), VK_API_VERSION_MINOR(p.apiVersion), 0)
             ? forced : p.apiVersion;
}
static std::vector<VkExtensionProperties> device_extensions(VkPhysicalDevice device) {
  uint32_t n = 0;
  vkEnumerateDeviceExtensionProperties(device, nullptr, &n, nullptr);
  std::vector<VkExtensionProperties> des(n);
  vkEnumerateDeviceExtensionProperties(device, nullptr, &n, des.data());
  des.resize(n);
  if (const char *hide = env::VK_HIDE_EXTENSIONS())
    std::erase_if(des, [&](const VkExtensionProperties &e) {
      for (const char *p = hide; *p;) {
        size_t len = strcspn(p, ",");
        if (len == strlen(e.extensionName) && !strncmp(p, e.extensionName, len))
          return true;
        p += len + (p[len] == ',');
      }
      return false;
    });
  return des;
}
static std::string version_text(uint32_t v) {
  return std::to_string(VK_API_VERSION_MAJOR(v)) + "." + std::to_string(VK_API_VERSION_MINOR(v)) + "." +
         std::to_string(VK_API_VERSION_PATCH(v));
}

#ifdef _WIN32
static constexpr bool kWindows = true;
#else
static constexpr bool kWindows = false;
#endif
static std::string driver_version_text(const VkPhysicalDeviceProperties &p) {
  return reporthdr::driver_version(p.vendorID, p.driverVersion, kWindows);
}

std::string device_description() {
  if (!R.properties.deviceName[0]) return "";
  return reporthdr::vulkan_gpu(R.properties.deviceName, R.properties.vendorID, R.properties.driverVersion,
                               R.properties.apiVersion, kWindows);
}

enum class DynamicRendering { None, Core, KHR };
static DynamicRendering dynamic_rendering(VkPhysicalDevice device, const VkPhysicalDeviceProperties &properties,
                                          const std::vector<VkExtensionProperties> &des) {
  const uint32_t api = device_api_version(properties);
  if (api < VK_API_VERSION_1_1)
    return DynamicRendering::None;
  if (api >= VK_API_VERSION_1_3) {
    VkPhysicalDeviceVulkan13Features f13{
        VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_3_FEATURES};
    VkPhysicalDeviceFeatures2 f2{VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FEATURES_2};
    f2.pNext = &f13;
    vkGetPhysicalDeviceFeatures2(device, &f2);
    if (f13.dynamicRendering)
      return DynamicRendering::Core;
  }

  if (!has_extension(des, VK_KHR_DYNAMIC_RENDERING_EXTENSION_NAME) ||
      (api < VK_API_VERSION_1_2 && (!has_extension(des, VK_KHR_DEPTH_STENCIL_RESOLVE_EXTENSION_NAME) ||
                                    !has_extension(des, VK_KHR_CREATE_RENDERPASS_2_EXTENSION_NAME))))
    return DynamicRendering::None;
  VkPhysicalDeviceDynamicRenderingFeaturesKHR fdr{
      VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_DYNAMIC_RENDERING_FEATURES_KHR};
  VkPhysicalDeviceFeatures2 f2{VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FEATURES_2};
  f2.pNext = &fdr;
  vkGetPhysicalDeviceFeatures2(device, &f2);
  return fdr.dynamicRendering ? DynamicRendering::KHR : DynamicRendering::None;
}
static const char kUpdateDriver[] =
#ifdef _WIN32
    "Install the newest graphics driver for this GPU (from the AMD, Intel or NVIDIA website, or through "
    "Windows Update) and start the game again. "
#elif defined(__APPLE__)
    "Vulkan on a Mac needs MoltenVK and the Vulkan loader: brew install vulkan-loader molten-vk. "
#else
    "Install the newest graphics driver for this GPU (on Linux: an up-to-date Mesa, or the vendor's "
    "driver) and start the game again. "
#endif
    "If no newer driver exists, this GPU cannot run the game's Vulkan renderer.";

static void log_instance_layers() {
  if (!vkEnumerateInstanceLayerProperties)
    return;
  uint32_t count = 0;
  if (vkEnumerateInstanceLayerProperties(&count, nullptr) != VK_SUCCESS)
    return;
  std::vector<VkLayerProperties> layers(count);
  if (count && vkEnumerateInstanceLayerProperties(&count, layers.data()) < VK_SUCCESS)
    return;
  layers.resize(count);

  std::string line;
  int lines = 0;
  for (const VkLayerProperties &l : layers) {
    char one[300];
    snprintf(one, sizeof one, "%s%s (%u.%u.%u)", line.empty() ? "" : ", ", l.layerName,
             VK_API_VERSION_MAJOR(l.specVersion), VK_API_VERSION_MINOR(l.specVersion),
             VK_API_VERSION_PATCH(l.specVersion));
    if (!line.empty() && line.size() + strlen(one) > 200) {
      LOG("[vulkan] layers: %s,", line.c_str());
      line.clear();
      if (++lines == 4) {
        LOG("[vulkan] layers: ... %u in all", count);
        return;
      }
      snprintf(one, sizeof one, "%s (%u.%u.%u)", l.layerName, VK_API_VERSION_MAJOR(l.specVersion),
               VK_API_VERSION_MINOR(l.specVersion), VK_API_VERSION_PATCH(l.specVersion));
    }
    line += one;
  }
  LOG("[vulkan] layers: %s", count ? line.c_str() : "none");
}

static void init_device(std::vector<const char *> extensions,
                        const std::function<void()> &create_surfaces) {
  reset_pipeline_lookup_cache();
#ifdef __APPLE__

  setenv("MVK_CONFIG_USE_METAL_ARGUMENT_BUFFERS", "0", 0);

  setenv("MVK_CONFIG_PREFILL_METAL_COMMAND_BUFFERS", "3", 0);
#endif
  uint32_t n;
  uint32_t loaderVersion = VK_API_VERSION_1_0;
  if (vkEnumerateInstanceVersion)
    vkEnumerateInstanceVersion(&loaderVersion);
  if (loaderVersion < VK_API_VERSION_1_1)
    throw std::runtime_error("The Vulkan runtime on this computer supports only Vulkan " +
                             version_text(loaderVersion) + "; the game needs Vulkan 1.1 or newer.\n\n" +
                             kUpdateDriver);
  log_instance_layers();
  uint32_t en = 0;
  vkEnumerateInstanceExtensionProperties(nullptr, &en, nullptr);
  std::vector<VkExtensionProperties> ies(en);
  vkEnumerateInstanceExtensionProperties(nullptr, &en, ies.data());
  if (!has_extension(ies, VK_KHR_SURFACE_EXTENSION_NAME))
    throw std::runtime_error("No Vulkan driver found (MoltenVK on a Mac: brew install molten-vk; "
                             "or point VK_DRIVER_FILES at its MoltenVK_icd.json)");
  for (const char *e : extensions)
    if (!has_extension(ies, e))
      throw std::runtime_error(std::string("the Vulkan driver lacks instance extension ") + e);
  VkInstanceCreateInfo ci{VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO};
  if (has_extension(ies, VK_KHR_PORTABILITY_ENUMERATION_EXTENSION_NAME)) {
    extensions.push_back(VK_KHR_PORTABILITY_ENUMERATION_EXTENSION_NAME);
    ci.flags |= VK_INSTANCE_CREATE_ENUMERATE_PORTABILITY_BIT_KHR;
  }
  VkDebugUtilsMessengerCreateInfoEXT debug{
      VK_STRUCTURE_TYPE_DEBUG_UTILS_MESSENGER_CREATE_INFO_EXT};
  const char *layer = "VK_LAYER_KHRONOS_validation";
  VkValidationFeatureEnableEXT syncFeature = VK_VALIDATION_FEATURE_ENABLE_SYNCHRONIZATION_VALIDATION_EXT;
  VkValidationFeaturesEXT validation{VK_STRUCTURE_TYPE_VALIDATION_FEATURES_EXT};
  validation.enabledValidationFeatureCount = 1;
  validation.pEnabledValidationFeatures = &syncFeature;
  if (env::VK_VALIDATION()) {
    extensions.push_back(VK_EXT_DEBUG_UTILS_EXTENSION_NAME);
    extensions.push_back(VK_EXT_VALIDATION_FEATURES_EXTENSION_NAME);
    ci.enabledLayerCount = 1;
    ci.ppEnabledLayerNames = &layer;
    debug.messageSeverity = VK_DEBUG_UTILS_MESSAGE_SEVERITY_WARNING_BIT_EXT |
                            VK_DEBUG_UTILS_MESSAGE_SEVERITY_ERROR_BIT_EXT;
    debug.messageType = VK_DEBUG_UTILS_MESSAGE_TYPE_GENERAL_BIT_EXT |
                        VK_DEBUG_UTILS_MESSAGE_TYPE_VALIDATION_BIT_EXT |
                        VK_DEBUG_UTILS_MESSAGE_TYPE_PERFORMANCE_BIT_EXT;
    debug.pfnUserCallback = debug_message;
    validation.pNext = &debug;
    ci.pNext = &validation;
  }
  VkApplicationInfo ai{VK_STRUCTURE_TYPE_APPLICATION_INFO};
  ai.pApplicationName = "NSMBU Vulkan";
  ai.apiVersion = VK_API_VERSION_1_3;
  ci.pApplicationInfo = &ai;
  ci.enabledExtensionCount = extensions.size();
  ci.ppEnabledExtensionNames = extensions.data();
  vk_check(vkCreateInstance(&ci, nullptr, &R.instance),
           "create Vulkan instance");
  debug.pNext = nullptr;
  load_instance_functions(R.instance);
  if (env::VK_VALIDATION()) {
    auto create = (PFN_vkCreateDebugUtilsMessengerEXT)vkGetInstanceProcAddr(
        R.instance, "vkCreateDebugUtilsMessengerEXT");
    VkDebugUtilsMessengerEXT messenger;
    if (create)
      vk_check(create(R.instance, &debug, nullptr, &messenger),
               "create validation messenger");
  }
  create_surfaces();
  vkEnumeratePhysicalDevices(R.instance, &n, nullptr);
  std::vector<VkPhysicalDevice> devices(n);
  vkEnumeratePhysicalDevices(R.instance, &n, devices.data());
  devices.resize(n);

  std::string unsuitable;
  struct Found {
    VkPhysicalDevice device = VK_NULL_HANDLE;
    uint32_t queue = UINT32_MAX;
    VkQueueFamilyProperties family{};
    DynamicRendering rendering = DynamicRendering::None;
  };
  std::vector<Found> found(devices.size());
  device_inventory.clear();
  for (size_t i = 0; i < devices.size(); i++) {
    VkPhysicalDeviceProperties properties;
    vkGetPhysicalDeviceProperties(devices[i], &properties);
    const DynamicRendering have = dynamic_rendering(devices[i], properties, device_extensions(devices[i]));
    GpuCandidate info;
    info.name = properties.deviceName;
    info.kind = int(properties.deviceType);
    info.rank = have == DynamicRendering::Core ? 0 : 1;
    char id[32];
    snprintf(id, sizeof id, "%04x:%04x", properties.vendorID, properties.deviceID);
    info.identity = id;
    if (have == DynamicRendering::None) {
      info.unavailable = "Vulkan " + version_text(device_api_version(properties)) + ", driver " +
                         driver_version_text(properties) +
                         (device_api_version(properties) < VK_API_VERSION_1_1 ? "; older than Vulkan 1.1"
                                                                              : "; VK_KHR_dynamic_rendering missing");
    } else {
      uint32_t qn;
      vkGetPhysicalDeviceQueueFamilyProperties(devices[i], &qn, nullptr);
      std::vector<VkQueueFamilyProperties> qs(qn);
      vkGetPhysicalDeviceQueueFamilyProperties(devices[i], &qn, qs.data());
      for (uint32_t q = 0; q < qn; q++) {
        VkBool32 tv = 0, drc = VK_TRUE;
        vkGetPhysicalDeviceSurfaceSupportKHR(devices[i], q, R.tv.surface, &tv);
        if (R.drc.surface)
          vkGetPhysicalDeviceSurfaceSupportKHR(devices[i], q, R.drc.surface, &drc);
        if ((qs[q].queueFlags & VK_QUEUE_GRAPHICS_BIT) && tv && drc) {
          found[i].device = devices[i];
          found[i].queue = q;
          found[i].family = qs[q];
          found[i].rendering = have;
          break;
        }
      }
      if (!found[i].device) info.unavailable = "cannot draw to the game window";
    }
    device_inventory.push_back(info);
  }
  std::map<std::string, int> identityCount;
  for (const auto& info : device_inventory) identityCount[info.identity]++;
  for (auto& info : device_inventory)
    if (identityCount[info.identity] > 1) info.identity += ":" + info.name;
  for (size_t i = 0; i < device_inventory.size(); i++) {
    const auto& info = device_inventory[i];
    LOG("[vulkan] GPU %zu: %s (%s, %s): %s", i, info.name.c_str(), gpu_kind_name(info.kind),
        info.identity.c_str(), info.usable() ? "usable" : info.unavailable.c_str());
    if (!info.usable()) unsuitable += "\n" + info.name + ": " + info.unavailable;
  }
  std::string savedGpu;
  if (hostui::get("gpu", savedGpu) && (savedGpu == "dedicated" || savedGpu == "discrete")) savedGpu.clear();
  set_device_setting(savedGpu);
  const char* gpuEnv = env::VK_GPU();
  const GpuChoice choice = choose_gpu(device_inventory, device_setting(), gpuEnv ? gpuEnv : "");
  if (choice.index >= 0) {
    const auto& slot = found[size_t(choice.index)];
    const auto& info = device_inventory[size_t(choice.index)];
    R.physicalDevice = slot.device;
    R.queueFamily = slot.queue;
    R.computeQueue = (slot.family.queueFlags & VK_QUEUE_COMPUTE_BIT) != 0;
    R.gpuTimestampValidBits = slot.family.timestampValidBits;
    R.dynamicRenderingKHR = slot.rendering == DynamicRendering::KHR;
    active_gpu_name = info.name;
    active_gpu_identity = info.identity;
    active_gpu_kind = info.kind;
    gpu_fallback = choice.fallback.empty() ? "" : choice.fallback + "; using " + info.name + ".";
    LOG("[vulkan] chosen GPU: %s (%s)", info.name.c_str(), choice.reason.c_str());
    if (!gpu_fallback.empty()) LOG("[vulkan] %s", gpu_fallback.c_str());
  }
  if (!R.physicalDevice)
    throw std::runtime_error(
        devices.empty()
            ? std::string("No graphics card with a Vulkan driver was found.\n\n") + kUpdateDriver
            : "The graphics driver does not support the Vulkan features the game needs: Vulkan 1.3, or "
              "Vulkan 1.1 / 1.2 with the VK_KHR_dynamic_rendering extension.\n" + unsuitable +
                  "\n\n" + kUpdateDriver);
  vkGetPhysicalDeviceProperties(R.physicalDevice, &R.properties);
  const uint32_t deviceApi = device_api_version(R.properties);
  LOG("[vulkan] device: %s (Vulkan %s, driver %s%s)", R.properties.deviceName,
      version_text(R.properties.apiVersion).c_str(), driver_version_text(R.properties).c_str(),
      R.dynamicRenderingKHR ? "; dynamic rendering through VK_KHR_dynamic_rendering" : "");
  if (perf_enabled()) {
    const auto& limits=R.properties.limits;
    LOG("[vulkan uniforms] dynamic/set %u; uniform/stage %u; uniform/set %u; resources/stage %u; alignment %llu; range %u",
        limits.maxDescriptorSetUniformBuffersDynamic,limits.maxPerStageDescriptorUniformBuffers,
        limits.maxDescriptorSetUniformBuffers,limits.maxPerStageResources,
        (unsigned long long)limits.minUniformBufferOffsetAlignment,limits.maxUniformBufferRange);
  }
  const std::vector<VkExtensionProperties> des = device_extensions(R.physicalDevice);
  std::vector<const char *> de{VK_KHR_SWAPCHAIN_EXTENSION_NAME};
  if (R.dynamicRenderingKHR) {
    de.push_back(VK_KHR_DYNAMIC_RENDERING_EXTENSION_NAME);
    if (deviceApi < VK_API_VERSION_1_2) {
      de.push_back(VK_KHR_DEPTH_STENCIL_RESOLVE_EXTENSION_NAME);
      de.push_back(VK_KHR_CREATE_RENDERPASS_2_EXTENSION_NAME);
    }
  }
  if (has_extension(des, "VK_KHR_portability_subset"))
    de.push_back("VK_KHR_portability_subset");
  R.portabilitySubset =
      has_extension(des, VK_KHR_PORTABILITY_SUBSET_EXTENSION_NAME);
  VkPhysicalDevicePortabilitySubsetFeaturesKHR portability{
      VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_PORTABILITY_SUBSET_FEATURES_KHR};
  VkPhysicalDeviceVulkan12Features available12{
      VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_2_FEATURES};
  VkPhysicalDeviceFeatures2 features{
      VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FEATURES_2};

  const bool vulkan12 = deviceApi >= VK_API_VERSION_1_2;
  void **tail = &features.pNext;
  if (vulkan12) {
    *tail = &available12;
    tail = &available12.pNext;
  }
  if (R.portabilitySubset)
    *tail = &portability;
  vkGetPhysicalDeviceFeatures2(R.physicalDevice, &features);
  if (R.portabilitySubset) {
    R.imageViewSwizzle = portability.imageViewFormatSwizzle;
    R.imageViewReinterpretation = portability.imageViewFormatReinterpretation;
    R.imageView2DOn3DImage = portability.imageView2DOn3DImage;
    R.samplerMipLodBias = portability.samplerMipLodBias;
    R.separateStencilMaskRef = portability.separateStencilMaskRef;
    R.constantAlphaColorBlendFactors =
        portability.constantAlphaColorBlendFactors;
    R.vertexAttributeAccessBeyondStride =
        portability.vertexAttributeAccessBeyondStride;
  }
  VkPhysicalDeviceVulkan12Features enabled12{
      VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_2_FEATURES};
  enabled12.samplerMirrorClampToEdge = available12.samplerMirrorClampToEdge;
  if (!vulkan12 && has_extension(des, VK_KHR_SAMPLER_MIRROR_CLAMP_TO_EDGE_EXTENSION_NAME)) {
    de.push_back(VK_KHR_SAMPLER_MIRROR_CLAMP_TO_EDGE_EXTENSION_NAME);
    enabled12.samplerMirrorClampToEdge = VK_TRUE;
  }
  R.samplerMirrorClampToEdge = enabled12.samplerMirrorClampToEdge;
  VkPhysicalDeviceFeatures available;
  vkGetPhysicalDeviceFeatures(R.physicalDevice, &available);
  VkPhysicalDeviceFeatures enabled{};

  enabled.textureCompressionBC = available.textureCompressionBC;
  if (!available.textureCompressionBC) LOG("[vulkan] device has no BC texture support; compute upload decoder enabled");
  enabled.samplerAnisotropy = available.samplerAnisotropy;
  enabled.independentBlend = available.independentBlend;
  enabled.depthClamp = available.depthClamp;
  enabled.depthBiasClamp = available.depthBiasClamp;
  enabled.shaderClipDistance = available.shaderClipDistance;
  enabled.shaderCullDistance = available.shaderCullDistance;
  enabled.fillModeNonSolid = available.fillModeNonSolid;
  enabled.largePoints = available.largePoints;
  enabled.dualSrcBlend = available.dualSrcBlend;
  enabled.logicOp = available.logicOp;
  R.enabledFeatures = enabled;
  VkPhysicalDeviceVulkan13Features f13{
      VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_3_FEATURES};
  VkPhysicalDeviceDynamicRenderingFeaturesKHR fdr{
      VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_DYNAMIC_RENDERING_FEATURES_KHR};
  f13.dynamicRendering = fdr.dynamicRendering = VK_TRUE;
  void *chain = R.dynamicRenderingKHR ? static_cast<void *>(&fdr) : &f13;
  tail = R.dynamicRenderingKHR ? &fdr.pNext : &f13.pNext;
  if (vulkan12) {
    *tail = &enabled12;
    tail = &enabled12.pNext;
  }
  if (R.portabilitySubset)
    *tail = &portability;
  float priority = 1;
  VkDeviceQueueCreateInfo qi{VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO};
  qi.queueFamilyIndex = R.queueFamily;
  qi.queueCount = 1;
  qi.pQueuePriorities = &priority;
  VkDeviceCreateInfo di{VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO};
  di.pNext = chain;
  di.queueCreateInfoCount = 1;
  di.pQueueCreateInfos = &qi;
  di.pEnabledFeatures = &enabled;
  di.enabledExtensionCount = de.size();
  di.ppEnabledExtensionNames = de.data();
  vk_check(vkCreateDevice(R.physicalDevice, &di, nullptr, &R.device),
           "create Vulkan device");
  load_device_functions(R.device, R.dynamicRenderingKHR);
  vkGetDeviceQueue(R.device, R.queueFamily, 0, &R.queue);
  init_pipeline_cache();
  for (auto& slot:R.submissions) {
  VkCommandPoolCreateInfo pool{VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO};
  pool.queueFamilyIndex = R.queueFamily;
  pool.flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT;
  vk_check(vkCreateCommandPool(R.device, &pool, nullptr, &slot.commandPool),
           "create command pool");
  VkCommandBufferAllocateInfo ca{
      VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO};
  ca.commandPool = slot.commandPool;
  ca.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
  ca.commandBufferCount = 1;
  vk_check(vkAllocateCommandBuffers(R.device, &ca, &slot.cmd),
           "allocate command buffer");
  VkFenceCreateInfo fi{VK_STRUCTURE_TYPE_FENCE_CREATE_INFO};
  fi.flags = VK_FENCE_CREATE_SIGNALED_BIT;
  vk_check(vkCreateFence(R.device, &fi, nullptr, &slot.fence),
           "create frame fence");
  VkDescriptorPoolSize sizes[] = {
      {VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 65536},
      {VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, 32768},
      {VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER_DYNAMIC, 32768},
      {VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 65536}};
  VkDescriptorPoolCreateInfo dp{VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO};
  dp.maxSets = 32768;
  dp.poolSizeCount = std::size(sizes);
  dp.pPoolSizes = sizes;
  vk_check(vkCreateDescriptorPool(R.device, &dp, nullptr, &slot.descriptorPool),
           "create descriptor pool");
  }
  init_gpu_timestamp_queries();
  activate_submission(0);
  for (Screen *s : {&R.tv, &R.drc})
    if (s->window) {
      VkSemaphoreCreateInfo si{VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO};
      vk_check(vkCreateSemaphore(R.device, &si, nullptr, &s->acquired),
               "create acquire semaphore");
      vk_check(vkCreateSemaphore(R.device, &si, nullptr, &s->finished),
               "create present semaphore");
      make_swapchain(*s);
    }
  vk::select_renderer();
  set_graphics_feature_available(GraphicsFeature::Anisotropy,
                                 R.enabledFeatures.samplerAnisotropy);
  if (const char* path = env::CAPTURE_PATH()) {
    const char* frame = env::CAPTURE();
    request_tv_dump(path, frame ? std::max(0, atoi(frame)) : 120);
  }
}

#ifdef NSMBU_SDL_HOST
#ifdef __linux__
static bool dir_has_plugin(const std::string& dir) {
  std::error_code ec;
  if (!std::filesystem::is_directory(dir, ec)) return false;
  for (const auto& entry : std::filesystem::directory_iterator(dir, ec))
    if (entry.path().extension() == ".so") return true;
  return false;
}

static void use_user_libdecor_plugin() {
  if (const char* e = std::getenv("LIBDECOR_PLUGIN_DIR"))
    if (*e) return;
  const char* system_dirs[] = {
    "/usr/lib/x86_64-linux-gnu/libdecor/plugins-1",
    "/usr/lib/aarch64-linux-gnu/libdecor/plugins-1",
    "/usr/lib64/libdecor/plugins-1",
    "/usr/lib/libdecor/plugins-1",
  };
  for (const char* dir : system_dirs)
    if (dir_has_plugin(dir)) return;
  const char* home = std::getenv("HOME");
  if (!home || !*home) return;
  std::string user = std::string(home) + "/.local/lib/libdecor/plugins-1";
  if (!dir_has_plugin(user)) return;
  setenv("LIBDECOR_PLUGIN_DIR", user.c_str(), 0);
}
#else
static void use_user_libdecor_plugin() {}
#endif

static bool hidden_windows() {
  static const bool hidden = [] { const char* e = env::HIDDEN_WINDOWS(); return e && *e && strcmp(e, "0"); }();
  return hidden;
}

static void set_window_icons() {
  FILE *f = std::fopen((config::game_dir + "/meta/iconTex.tga").c_str(), "rb");
  if (!f)
    return;
  std::vector<uint8_t> d;
  uint8_t buf[65536];
  for (size_t n; (n = std::fread(buf, 1, sizeof buf, f)) > 0;)
    d.insert(d.end(), buf, buf + n);
  std::fclose(f);
  if (d.size() < 18 || d[1] != 0 || d[2] != 2 || d[16] != 32)
    return;
  const uint32_t w = d[12] | d[13] << 8, h = d[14] | d[15] << 8, start = 18 + d[0];
  if (!w || !h || w > 1024 || h > 1024 || d.size() < start + size_t(w) * h * 4)
    return;
  SDL_Surface *icon = SDL_CreateSurface(int(w), int(h), SDL_PIXELFORMAT_BGRA32);
  if (!icon)
    return;
  const bool topDown = d[17] & 0x20;
  for (uint32_t y = 0; y < h; y++)
    std::memcpy(static_cast<uint8_t *>(icon->pixels) + size_t(y) * icon->pitch,
                d.data() + start + size_t(topDown ? y : h - 1 - y) * w * 4, size_t(w) * 4);
  for (Screen *s : {&R.tv, &R.drc})
    if (s->window)
      SDL_SetWindowIcon(s->window, icon);
  SDL_DestroySurface(icon);
}

void init() {
#ifdef __ANDROID__
  SDL_SetHint(SDL_HINT_ORIENTATIONS, "LandscapeLeft LandscapeRight");
  SDL_SetHint(SDL_HINT_ANDROID_TRAP_BACK_BUTTON, "1");
#endif

  if (hidden_windows())
    SDL_SetHint(SDL_HINT_MAC_BACKGROUND_APP, "1");
  use_user_libdecor_plugin();
  if (!SDL_Init(SDL_INIT_VIDEO | SDL_INIT_GAMEPAD | SDL_INIT_AUDIO))
    throw std::runtime_error(SDL_GetError());

  if (!SDL_Vulkan_LoadLibrary(nullptr))
    throw std::runtime_error(std::string("Vulkan is not installed on this computer: the Vulkan runtime "
#ifdef _WIN32
                                         "(vulkan-1.dll) "
#endif
                                         "could not be loaded (") + SDL_GetError() + ").\n\n" + kUpdateDriver);
  auto gipa = reinterpret_cast<PFN_vkGetInstanceProcAddr>(SDL_Vulkan_GetVkGetInstanceProcAddr());
#ifdef __ANDROID__
  if (auto custom = drivers::open_custom()) gipa = custom;
#endif
  load_global_functions(gipa);
#ifdef __ANDROID__
  SDL_AddEventWatch(lifecycle_watch, nullptr);
  const SDL_WindowFlags windowFlags = SDL_WINDOW_VULKAN | SDL_WINDOW_FULLSCREEN;
#else

  const SDL_WindowFlags windowFlags = SDL_WINDOW_VULKAN | SDL_WINDOW_RESIZABLE |
                                      (hidden_windows() ? SDL_WINDOW_HIDDEN : 0);
#endif
  int windowW = 1280, windowH = 720;
#ifndef __ANDROID__
  ::hostui::window_size(windowW, windowH);
#endif
  char initialTitle[window_title::kCap];
  window_title::format_base(initialTitle, sizeof initialTitle);
  R.tv.window = SDL_CreateWindow(initialTitle, windowW, windowH, windowFlags);
  if (!R.tv.window)
    throw std::runtime_error(SDL_GetError());
#ifdef __ANDROID__

#else
  if (!env::NO_GAMEPAD()) {

    R.drc.window = SDL_CreateWindow("GamePad - Vulkan", 854, 480, windowFlags | SDL_WINDOW_HIDDEN);
    if (!R.drc.window)
      throw std::runtime_error(SDL_GetError());
    R.drc.visible = false;
    gfx::g_has_drc_window = true;
  }
#endif
  if (hidden_windows())
    R.tv.visible = R.drc.visible = false;
  set_window_icons();
  uint32_t n;
  const char *const *se = SDL_Vulkan_GetInstanceExtensions(&n);
  if (!se)
    throw std::runtime_error(SDL_GetError());
  for (Screen *s : {&R.tv, &R.drc})
    if (s->window) {
      int width = 0, height = 0;
      if (!SDL_GetWindowSizeInPixels(s->window, &width, &height))
        throw std::runtime_error(SDL_GetError());
      s->width = width;
      s->height = height;
    }
  init_device(std::vector<const char *>(se, se + n), [] {
    if (!NSMBU_CREATE_WINDOW_SURFACE(R.tv.window, R.instance, nullptr,
                                  &R.tv.surface))
      throw std::runtime_error(SDL_GetError());
    if (R.drc.window && !NSMBU_CREATE_WINDOW_SURFACE(R.drc.window, R.instance,
                                                  nullptr, &R.drc.surface))
      throw std::runtime_error(SDL_GetError());
  });
  mods::mouse_init(R.tv.window);
  input::set_prompt_window(R.tv.window);
  input::init();
  install_graphics_menu(R.tv.window);
  ::hostui::load_saved_options();
}
#endif
void save_renderer_caches() {

  reset_feedback_images();
  wait_idle();
  bc_decode_shutdown();
  destroy_gpu_timestamp_queries();
  vk::save_shader_cache();
  save_pipeline_cache();
}
#ifdef NSMBU_SDL_HOST

enum class TouchIn { None, DrcWindow, TvWindow };
static TouchIn touchHeld = TouchIn::None;
static bool drc_window_point(float x, float y, float &tx, float &ty) {
  int pw = 0, ph = 0;
  SDL_GetWindowSizeInPixels(R.drc.window, &pw, &ph);
  const float density = SDL_GetWindowPixelDensity(R.drc.window);
  const float sw = R.drc.scan ? float(R.drc.scan->extent.width) : 854.0f;
  const float sh = R.drc.scan ? float(R.drc.scan->extent.height) : 480.0f;
  const gfx::Box b = gfx::display_layout(float(pw), float(ph), sw, sh);
  if (b.w <= 0 || b.h <= 0) return false;
  tx = (x * density - b.x) / b.w;
  ty = (y * density - b.y) / b.h;
  return tx >= 0 && tx <= 1 && ty >= 0 && ty <= 1;
}

static void tv_window_point(float x, float y, float &nx, float &ny) {
  int w = 0, h = 0;
  SDL_GetWindowSize(R.tv.window, &w, &h);
  nx = w > 0 ? x / float(w) : 0;
  ny = h > 0 ? y / float(h) : 0;
}

static uint64_t buttonDown = 0;
static void view_button_released() {
  if (SDL_GetTicks() - buttonDown >= 600) {
#ifdef __ANDROID__

    perf_hint::set_fps60_chosen(!perf_hint::fps60_chosen());
    ::hostui::graphics_changed();
#endif
  } else {
    ::hostui::set_drc_mode(gfx::next_view());
  }
  buttonDown = 0;
}
#ifdef __ANDROID__
static bool synthHeld = false;
static SDL_FingerID touchFinger = 0, buttonFinger = 0, overlayMenuFinger = 0;

static bool overlay_menu_finger(const SDL_Event& event) {
  const SDL_TouchFingerEvent& f = event.tfinger;
  switch (event.type) {
  case SDL_EVENT_FINGER_DOWN:
    if (input::touch_from_controller(f.touchID)) return true;
    if (overlayMenuFinger) return true;
    overlayMenuFinger = f.fingerID;
    overlay::mouse_move(f.x, f.y, true);
    overlay::mouse_button(0, true, true);
    return true;
  case SDL_EVENT_FINGER_MOTION:
    if (!overlayMenuFinger || f.fingerID != overlayMenuFinger) return false;
    overlay::mouse_move(f.x, f.y, true);
    return true;
  case SDL_EVENT_FINGER_UP:
  case SDL_EVENT_FINGER_CANCELED:
    if (!overlayMenuFinger || f.fingerID != overlayMenuFinger) return false;
    overlayMenuFinger = 0;
    overlay::mouse_move(f.x, f.y, true);
    overlay::mouse_button(0, false, true);
    return true;
  default:
    return false;
  }
}

static bool finger_touch(const SDL_Event& event) {
  const SDL_TouchFingerEvent& f = event.tfinger;
  if (f.windowID != SDL_GetWindowID(R.tv.window)) return false;
  if (overlay::captures()) return overlay_menu_finger(event);
  float tx = 0, ty = 0;
  switch (event.type) {
  case SDL_EVENT_FINGER_DOWN:
    if (input::touch_from_controller(f.touchID)) return true;
    if (!buttonFinger && gfx::view_button_hit(f.x, f.y)) {
      buttonFinger = f.fingerID;
      buttonDown = std::max<uint64_t>(SDL_GetTicks(), 1);
      return true;
    }
    if (touchFinger || !gfx::overlay_hit(f.x, f.y, &tx, &ty)) return false;
    touchFinger = f.fingerID;
    gfx::display_touched();
    input::set_touch(true, tx, ty);
    return true;
  case SDL_EVENT_FINGER_MOTION:
    if (!touchFinger || f.fingerID != touchFinger) return f.fingerID == buttonFinger && buttonFinger;
    if (!gfx::overlay_hit(f.x, f.y, &tx, &ty, true)) tx = ty = 0;
    input::set_touch(true, tx, ty);
    return true;
  case SDL_EVENT_FINGER_UP:
  case SDL_EVENT_FINGER_CANCELED:
    if (buttonFinger && f.fingerID == buttonFinger) {
      buttonFinger = 0;
      if (event.type == SDL_EVENT_FINGER_UP) view_button_released();
      buttonDown = 0;
      return true;
    }
    if (!touchFinger || f.fingerID != touchFinger) return false;
    touchFinger = 0;
    if (!gfx::overlay_hit(f.x, f.y, &tx, &ty, true)) tx = ty = 0;
    input::set_touch(false, tx, ty);
    gfx::display_touched();
    return true;
  default:
    return false;
  }
}
#endif
static bool gamepad_touch(const SDL_Event& event) {
  const SDL_WindowID drcId = R.drc.window ? SDL_GetWindowID(R.drc.window) : 0;
  const SDL_WindowID tvId = SDL_GetWindowID(R.tv.window);
  float tx = 0, ty = 0;
#ifdef __ANDROID__
  if (finger_touch(event)) return true;

  if ((event.type == SDL_EVENT_MOUSE_BUTTON_DOWN || event.type == SDL_EVENT_MOUSE_BUTTON_UP) &&
      event.button.which == SDL_TOUCH_MOUSEID) {
    if (overlay::captures() || event.button.windowID != tvId) return false;
    if (event.type == SDL_EVENT_MOUSE_BUTTON_UP) return std::exchange(synthHeld, false);
    float nx, ny;
    tv_window_point(event.button.x, event.button.y, nx, ny);
    synthHeld = gfx::view_button_hit(nx, ny) || gfx::overlay_hit(nx, ny, &tx, &ty);
    return synthHeld;
  }
  if (event.type == SDL_EVENT_MOUSE_MOTION && event.motion.which == SDL_TOUCH_MOUSEID) return synthHeld;
#endif
  switch (event.type) {
  case SDL_EVENT_MOUSE_BUTTON_DOWN:
    if (event.button.button != SDL_BUTTON_LEFT) return false;
    if (event.button.windowID == tvId && !overlay::captures() && gfx::view_button_enabled()) {
      float nx, ny;
      tv_window_point(event.button.x, event.button.y, nx, ny);
      if (gfx::view_button_hit(nx, ny)) {
        buttonDown = std::max<uint64_t>(SDL_GetTicks(), 1);
        return true;
      }
    }
    if (drcId && event.button.windowID == drcId) {
      if (!drc_window_point(event.button.x, event.button.y, tx, ty)) return true;
      touchHeld = TouchIn::DrcWindow;
    } else if (event.button.windowID == tvId && !overlay::captures()) {
      float nx, ny;
      tv_window_point(event.button.x, event.button.y, nx, ny);
      if (!gfx::overlay_hit(nx, ny, &tx, &ty)) return false;
      touchHeld = TouchIn::TvWindow;
      gfx::display_touched();
    } else {
      return false;
    }
    input::set_touch(true, tx, ty);
    return true;
  case SDL_EVENT_MOUSE_MOTION:
  case SDL_EVENT_MOUSE_BUTTON_UP: {
    const bool up = event.type == SDL_EVENT_MOUSE_BUTTON_UP;
    if (buttonDown && up && event.button.button == SDL_BUTTON_LEFT) {
      view_button_released();
      return true;
    }
    if (touchHeld == TouchIn::None || (up && event.button.button != SDL_BUTTON_LEFT)) return false;
    const float x = up ? event.button.x : event.motion.x, y = up ? event.button.y : event.motion.y;
    if (touchHeld == TouchIn::DrcWindow) {
      drc_window_point(x, y, tx, ty);
      tx = std::clamp(tx, 0.0f, 1.0f);
      ty = std::clamp(ty, 0.0f, 1.0f);
    } else {
      float nx, ny;
      tv_window_point(x, y, nx, ny);
      if (!gfx::overlay_hit(nx, ny, &tx, &ty, true)) tx = ty = 0;
      gfx::display_touched();
    }
    if (up) touchHeld = TouchIn::None;
    input::set_touch(!up, tx, ty);
    return true;
  }
  case SDL_EVENT_WINDOW_FOCUS_LOST:
    if (touchHeld != TouchIn::None) { touchHeld = TouchIn::None; input::set_touch(false, 0, 0); }
    return false;
  default:
    return false;
  }
}

static bool drc_key(const SDL_Event& event) {
  if (event.type != SDL_EVENT_KEY_DOWN || event.key.repeat || event.key.scancode != SDL_SCANCODE_G) return false;
#ifdef __APPLE__
  const SDL_Keymod want = SDL_KMOD_GUI;
#else
  const SDL_Keymod want = SDL_KMOD_CTRL;
#endif
  if (!(event.key.mod & want) || (event.key.mod & (SDL_KMOD_ALT | SDL_KMOD_SHIFT))) return false;
  ::hostui::toggle_drc();
  return true;
}

static bool test_frame_due(const std::vector<uint64_t> &frames, size_t &i) {
  if (i >= frames.size() || frame_count() < frames[i]) return false;
  i++;
  return true;
}
static std::vector<uint64_t> test_frames(env::Var var) {
  std::vector<uint64_t> f;
  if (const char *e = (var)())
    for (const char *p = e; *p;) {
      f.push_back(strtoull(p, (char **)&p, 10));
      while (*p == ',') p++;
    }
  return f;
}

static void test_drc_key() {
  static const std::vector<uint64_t> frames = test_frames(env::TEST_DRC_KEY);
  static size_t i = 0;
  if (test_frame_due(frames, i)) {
    LOG("[display] test: Ctrl+G at frame %llu", (unsigned long long)frame_count());
    ::hostui::toggle_drc();
  }
}

static void toggle_fullscreen(SDL_Window* window) {
  static auto last = std::chrono::steady_clock::time_point{};
  const auto now = std::chrono::steady_clock::now();
  if (last != std::chrono::steady_clock::time_point{} &&
      now - last < std::chrono::milliseconds(500))
    return;
  last = now;
  if (!window || window == R.tv.window) {
    const bool on = !::hostui::fullscreen();
    ::hostui::set_fullscreen(on);
    LOG("[display] TV window %s", on ? "full screen" : "windowed");
    return;
  }
  const bool full = (SDL_GetWindowFlags(window) & SDL_WINDOW_FULLSCREEN) != 0;
  if (!SDL_SetWindowFullscreen(window, !full))
    LOG("[display] GamePad window: switching to %s failed: %s", full ? "windowed" : "full screen", SDL_GetError());
  else
    LOG("[display] GamePad window %s", full ? "windowed" : "full screen");
}

static bool fullscreen_key(const SDL_Event& event) {
  if (event.type != SDL_EVENT_KEY_DOWN || event.key.repeat) return false;
  const bool f11 = event.key.scancode == SDL_SCANCODE_F11 && !(event.key.mod & (SDL_KMOD_CTRL | SDL_KMOD_GUI));
  const bool altEnter = (event.key.scancode == SDL_SCANCODE_RETURN || event.key.scancode == SDL_SCANCODE_KP_ENTER) &&
                        (event.key.mod & SDL_KMOD_ALT);
  if (!f11 && !altEnter) return false;
  SDL_Window* window = SDL_GetWindowFromID(event.key.windowID);
  toggle_fullscreen(window ? window : R.tv.window);
  return true;
}

static void test_fullscreen_key() {
  static const std::vector<uint64_t> frames = test_frames(env::TEST_FULLSCREEN_KEY);
  static size_t i = 0;
  if (test_frame_due(frames, i)) {
    LOG("[display] test: F11 at frame %llu", (unsigned long long)frame_count());
    toggle_fullscreen(R.tv.window);
  }
}

static void quit_game() {
  screenshot::finish();
  gx2::checkpoint_vulkan_caches();
  std::_Exit(0);
}
static bool close_request(const SDL_Event& event) {
  if (event.type != SDL_EVENT_WINDOW_CLOSE_REQUESTED) return false;
  if (event.window.windowID == SDL_GetWindowID(R.tv.window)) quit_game();
  if (R.drc.window && event.window.windowID == SDL_GetWindowID(R.drc.window)) {
    ::hostui::drc_window_closed();
    LOG("[display] GamePad window closed (hidden); the game keeps running");
  }
  return true;
}

void run_main_loop() {
  auto titleTime = std::chrono::steady_clock::now();

  const char* exitAt=env::EXIT_AT_FRAME();
  const uint64_t exitFrame=exitAt ? std::strtoull(exitAt,nullptr,10) : 0;
  for (;;) {
    SDL_Event event;
    while (SDL_PollEvent(&event)) {
      if (event.type == SDL_EVENT_QUIT) quit_game();
      if (close_request(event)) continue;
      if (fullscreen_key(event)) continue;
      if (drc_key(event)) continue;
      if (gamepad_touch(event)) continue;
      input::handle_event(event);
      for (Screen *s : {&R.tv, &R.drc})
        if (s->window && event.window.windowID == SDL_GetWindowID(s->window)) {
          if (event.type == SDL_EVENT_WINDOW_PIXEL_SIZE_CHANGED) {
            s->width = event.window.data1;
            s->height = event.window.data2;
            s->resize = true;
          }
          if (s == &R.tv && event.type == SDL_EVENT_WINDOW_RESIZED)
            ::hostui::window_resized();
          if (s == &R.tv && (event.type == SDL_EVENT_WINDOW_ENTER_FULLSCREEN ||
                             event.type == SDL_EVENT_WINDOW_LEAVE_FULLSCREEN))
            ::hostui::tv_fullscreen_changed();
          if (event.type == SDL_EVENT_WINDOW_MINIMIZED ||
              event.type == SDL_EVENT_WINDOW_OCCLUDED ||
              event.type == SDL_EVENT_WINDOW_HIDDEN)
            s->visible = false;
          if (event.type == SDL_EVENT_WINDOW_SHOWN && s == &R.tv)
            display_rate::poll(R.tv.window);
          if (event.type == SDL_EVENT_WINDOW_RESTORED ||
              event.type == SDL_EVENT_WINDOW_SHOWN ||
              event.type == SDL_EVENT_WINDOW_EXPOSED) {
            const Uint64 flags = SDL_GetWindowFlags(s->window);
            s->visible = !(flags & (SDL_WINDOW_MINIMIZED | SDL_WINDOW_OCCLUDED |
                                    SDL_WINDOW_HIDDEN));
          }
          if (event.type == SDL_EVENT_WINDOW_FOCUS_GAINED ||
              event.type == SDL_EVENT_WINDOW_FOCUS_LOST) {
            bool focused = false;
            for (Screen *w : {&R.tv, &R.drc})
              if (w->window &&
                  (SDL_GetWindowFlags(w->window) & SDL_WINDOW_INPUT_FOCUS))
                focused = true;
            ::hostui::set_app_focused(focused);
          }
        }
    }
    input::update();
    agent_ctl::service();
    ::hostui::run_posted();
    test_drc_key();
    test_fullscreen_key();
    if (const int m = gfx::display_test_mode(frame_count()); m >= 0)
      ::hostui::set_drc_mode(m);
    overlay::set_density(SDL_GetWindowPixelDensity(R.tv.window));
    auto now = std::chrono::steady_clock::now();
    double elapsed = std::chrono::duration<double>(now - titleTime).count();
    static auto polled = now;
    if (now - polled >= std::chrono::milliseconds(500) || polled == now) {
      polled = now;
      display_rate::poll(R.tv.window);
    }
    if (elapsed >= 0.5 && !input::text_prompt_active()) {
      if (exitFrame && frame_count() >= exitFrame) {
        SDL_Event quit{};quit.type=SDL_EVENT_QUIT;SDL_PushEvent(&quit);
      }
      char title[window_title::kCap];
      window_title::format_base(title, sizeof title);
      if (build::dev() && gx2::uncapped())
        std::strncat(title, " | FAST FORWARD", sizeof title - std::strlen(title) - 1);
      static char lastTitle[window_title::kCap];
      if (strcmp(title, lastTitle)) {
        memcpy(lastTitle, title, sizeof lastTitle);
        SDL_SetWindowTitle(R.tv.window, title);
      }
      titleTime = now;
    }
    if (render::shader_warm_tick(vk::shader_stats().spirvCompiles + R.pipelineCreates)) {
      LOG("[warm] done");
      quit_game();
    }
    SDL_Delay(1);
  }
}
#endif

#if defined(__APPLE__) && !defined(NSMBU_SDL_HOST)

static bool image_loaded(const char *part) {
  for (uint32_t i = 0, n = _dyld_image_count(); i < n; i++)
    if (const char *name = _dyld_get_image_name(i))
      if (strstr(name, part))
        return true;
  return false;
}
void init_appkit(void *tvLayer, void *drcLayer) {
  if (const char *e = env::VK_FORCE_INIT_FAIL(); e && *e && strcmp(e, "0"))
    throw std::runtime_error("Vulkan start-up failure forced for testing (NSMBU_VK_FORCE_INIT_FAIL)");

  if (!image_loaded("/libvulkan"))
    throw std::runtime_error("The Vulkan loader (libvulkan) is not installed. "
                             "Install it with: brew install vulkan-loader molten-vk");
  if (!image_loaded("/libglslang"))
    throw std::runtime_error("glslang (shader compiler for Vulkan) is not installed. "
                             "Install it with: brew install glslang");
  load_global_functions(reinterpret_cast<PFN_vkGetInstanceProcAddr>(dlsym(RTLD_DEFAULT, "vkGetInstanceProcAddr")));
  if (!tvLayer)
    throw std::runtime_error("no TV window layer");
  R.tv.window = tvLayer;
  R.drc.window = drcLayer;
  init_device({VK_KHR_SURFACE_EXTENSION_NAME, VK_EXT_METAL_SURFACE_EXTENSION_NAME}, [&] {
    auto create = (PFN_vkCreateMetalSurfaceEXT)vkGetInstanceProcAddr(R.instance, "vkCreateMetalSurfaceEXT");
    if (!create)
      throw std::runtime_error("vkCreateMetalSurfaceEXT unavailable");
    for (Screen *s : {&R.tv, &R.drc}) {
      if (!s->window)
        continue;
      VkMetalSurfaceCreateInfoEXT ci{VK_STRUCTURE_TYPE_METAL_SURFACE_CREATE_INFO_EXT};
      ci.pLayer = static_cast<const CAMetalLayer *>(s->window);
      vk_check(create(R.instance, &ci, nullptr, &s->surface), "create Metal surface");
    }
  });
  LOG("[vulkan] presenting to the AppKit windows through VK_EXT_metal_surface");
}
void screen_changed(int screen, bool visible) {
  Screen &s = screen ? R.drc : R.tv;
  s.visible = visible;
  s.resize = true;
}
#endif
}
