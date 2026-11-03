

#pragma once
#include <cstdint>
#include <string>

namespace gfxvk {
void peek_z(const uint32_t*, uint32_t);
void init();
void run_main_loop();
void draw(const uint32_t* regs, uint32_t prim, uint32_t count, uint32_t indexType, uint32_t indexAddr,
          uint32_t baseVertex, uint32_t instances);
void clear_color(const uint32_t* regs, uint32_t colorBuffer, const float rgba[4]);
void clear_depth_stencil(const uint32_t* regs, uint32_t depthBuffer, float depth, uint32_t stencil, uint32_t flags);
void copy_surface(uint32_t src, uint32_t srcMip, uint32_t srcSlice, uint32_t dst, uint32_t dstMip, uint32_t dstSlice);
void copy_to_scan(uint32_t colorBuffer, uint32_t target);
void swap();
void repeat_present();
void set_frame_aspect(float a);
bool target_aspect_factors(uint32_t w, uint32_t h, float& kx, float& ky);
uint64_t frames_completed();
void with_autorelease_pool(void (*fn)());
void set_tv_format(uint32_t gx2Format, bool tv);
void invalidate(uint32_t flags, uint32_t addr, uint32_t size);
void flush();
void flush_async();
void wait_idle();
void write_back_linear_targets();
uint64_t frame_count();
void request_tv_dump(const std::string& path, int frames_ahead);
void request_drc_dump(const std::string& path, int frames_ahead);
void request_capture();
void ss_reset_surfaces();
void save_renderer_caches();
int renderer_smoke_test();
std::string device_description();
namespace vk { void reset_shader_memoization(); }

#if defined(__APPLE__) && !defined(NSMBU_SDL_HOST)

void init_appkit(void* tvLayer, void* drcLayer);
void screen_changed(int screen, bool visible);
#endif
}
