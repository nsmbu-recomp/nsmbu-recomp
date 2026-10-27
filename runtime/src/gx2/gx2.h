
#pragma once
#include <algorithm>
#include <cstdint>

struct LatteFetchShader;
namespace GX2 {
struct GX2ColorBuffer;
struct GX2DepthBuffer;
}

namespace gx2 {
struct ShaderKeyDirtyStats {
    uint64_t changedBatches = 0, baselineWouldBumps = 0, actualBumps = 0;
    uint64_t avoidedBumps = 0, maskedWords = 0;
};
ShaderKeyDirtyStats shader_key_dirty_stats();
uint32_t color_buffer_address(const GX2::GX2ColorBuffer* cb);
LatteFetchShader* build_fetch_shader(uint32_t program);

bool uncapped();
void set_uncapped(bool on);
void set_display_vsync_ns(int64_t ns);
bool wide_color_target();
}

namespace gx2 {
constexpr uint32_t kDepthSlicesReg = 0xA002;

constexpr uint32_t kColorTarget3D = 0x80000000u;
constexpr uint32_t color_target_slices(uint32_t tile) { return std::max<uint32_t>((tile >> 16) & 0x7FFF, 1); }
}

namespace gfx {
void init();
void run_main_loop();
void draw(const uint32_t* regs, uint32_t prim, uint32_t count, uint32_t indexType, uint32_t indexAddr,
          uint32_t baseVertex, uint32_t instances);
void clear_color(const uint32_t* regs, uint32_t colorBuffer, const float rgba[4]);
void clear_depth_stencil(const uint32_t* regs, uint32_t depthBuffer, float depth, uint32_t stencil, uint32_t flags);
void copy_surface(uint32_t src, uint32_t srcMip, uint32_t srcSlice, uint32_t dst, uint32_t dstMip, uint32_t dstSlice);
void copy_to_scan(uint32_t colorBuffer, uint32_t target);
void write_back_linear_targets();
void swap();
void set_frame_aspect(float a);

bool target_aspect_factors(uint32_t w, uint32_t h, float& kx, float& ky);
uint64_t frames_completed();
void with_autorelease_pool(void (*fn)());
void set_tv_format(uint32_t gx2Format, bool tv);
void invalidate(uint32_t flags, uint32_t addr, uint32_t size);
void flush();
void wait_idle();
}
