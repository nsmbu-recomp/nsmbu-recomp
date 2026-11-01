#include "gfx/aspect_view.h"
#include "gfx/res_presets.h"
#include "gfx/render_mips.h"
#include "bc_decode.h"
#include "mods/cemu_pack.h"

#include "backend.h"
#include "attachment_view_usage.h"
#include "buffer_cache.h"
#include "gfx/render_prof.h"
#include "settings.h"
#include "shaders.h"
#include "sparse_hash_memo.h"
#include "core/write_watch.h"
#include "core/env.h"
#define XXH_INLINE_ALL
#include "../../../third_party/xxhash/xxhash.h"
#include "Cafe/HW/Latte/ISA/LatteReg.h"
#include "Cafe/HW/Latte/ISA/RegDefines.h"
#include "Cafe/HW/Latte/LatteAddrLib/LatteAddrLib.h"
#include "gx2/gx2.h"
#include "gx2_texture_regs.h"
#include "core/runtime.h"
#include <algorithm>
#include <array>
#include <atomic>
#include <cmath>
#include <cstring>
#include <cstdlib>
#include <limits>
#include <stdexcept>
#include <string>
#include <tuple>
#include <unordered_map>
#include <vector>

Latte::E_GX2SURFFMT LatteTexture_ReconstructGX2Format(const Latte::LATTE_SQ_TEX_RESOURCE_WORD1_N&, const Latte::LATTE_SQ_TEX_RESOURCE_WORD4_N&);
namespace gfxvk {

struct CachedGuestLevel {
    LatteAddrLib::AddrSurfaceInfo_OUT info{};
    uint32_t address = 0;
    bool infoValid = false, addressValid = false;
    uint64_t rangeBegin = 0, rangeEnd = 0;
    bool rangeValid = false;
};
struct CachedGuestLayout {
    std::array<uint32_t, 10> descriptor{};
    std::vector<CachedGuestLevel> levels;
    uint64_t mipRangeBegin = UINT64_MAX, mipRangeEnd = 0;
    uint32_t mipRangesCached = 0;
    bool mipRangesComplete = false;
};
static CachedGuestLevel& guest_level(const Surface* s, uint32_t level) {
    std::array<uint32_t, 10> descriptor{s->addr,s->mipAddr,s->width,s->height,s->slices,
        s->format,s->dim,s->tileMode,s->swizzle,s->mips};

    if (!s->guestLayout || s->guestLayout->descriptor != descriptor ||
        s->guestLayout->levels.empty()) {
        auto layout = std::make_shared<CachedGuestLayout>();
        layout->descriptor = descriptor;
        layout->levels.resize(s->mips);
        s->guestLayout = std::move(layout);
    }
    if (level >= s->guestLayout->levels.size()) throw std::runtime_error("GX2 mip level exceeds surface");
    return s->guestLayout->levels[level];
}
static const LatteAddrLib::AddrSurfaceInfo_OUT& guest_info(const Surface* s, uint32_t level) {
    auto& cached = guest_level(s, level);
    if (!cached.infoValid) {
        LatteAddrLib::GX2CalculateSurfaceInfo(static_cast<Latte::E_GX2SURFFMT>(s->format),
            s->width,s->height,s->slices,static_cast<Latte::E_DIM>(s->dim),
            Latte::MakeGX2TileMode(static_cast<Latte::E_HWTILEMODE>(s->tileMode)),0,level,&cached.info);
        cached.infoValid = true;
    }
    return cached.info;
}
static void check_vk(VkResult result, const char* what) {
    if (result != VK_SUCCESS) throw std::runtime_error(std::string("Vulkan surfaces: ")+what+" failed ("+std::to_string(result)+")");
}
static uint64_t content_hash(const uint8_t* p, size_t n) {

    return XXH3_64bits(p, n);
}
uint64_t next_write_seq() { static uint64_t seq=0; return ++seq; }
static float parse_scale(const char* e) {
    return gfx::clamp_res_scale(e ? (float)atof(e) : 1.0f);
}
static std::atomic<float> g_res_requested{parse_scale(env::RES_SCALE())};
static float g_res_frame = g_res_requested.load();
static void latch_aspect();
float res_scale() { return g_res_frame; }
float requested_res_scale() { return g_res_requested.load(std::memory_order_relaxed); }
void set_res_scale(float f) {
    g_res_requested = gfx::clamp_res_scale(f);
    LOG("[gfx] internal resolution %gx", g_res_requested.load());
}
void latch_res_scale() {

    static std::vector<std::pair<uint64_t, float>> at = [] {
        std::vector<std::pair<uint64_t, float>> v;
        if (const char* e = env::RES_SCALE_AT())
            for (char* p = (char*)e; *p;) {
                uint64_t f = strtoull(p, &p, 10);
                if (*p++ != ':') break;
                v.push_back({f, (float)strtod(p, &p)});
                while (*p == ',') p++;
            }
        return v;
    }();
    for (auto& [f, v] : at)
        if (R.frame == f) set_res_scale(v);
    g_res_frame = g_res_requested.load(std::memory_order_relaxed);
    latch_aspect();
}

static std::atomic<float> g_aspect_requested{16.0f / 9.0f};
static float g_aspect_kx = 1.0f, g_aspect_ky = 1.0f;
void set_frame_aspect(float a) { g_aspect_requested.store(a, std::memory_order_relaxed); }
static void latch_aspect() {
    float a = g_aspect_requested.load(std::memory_order_relaxed), base = 16.0f / 9.0f;
    float kx = a >= base ? a / base : 1.0f, ky = a >= base ? 1.0f : base / a;
    if (kx != g_aspect_kx || ky != g_aspect_ky) LOG("[gfx] aspect %.4f: screen targets x%.4f wide, x%.4f tall", a, kx, ky);
    g_aspect_kx = kx;
    g_aspect_ky = ky;
}

static bool screen_shaped(uint32_t width, uint32_t height, bool compressed, uint32_t mips, uint32_t slices) {
    if (compressed || mips > 1 || slices > 1 || width < 32) return false;
    if (aspect::view::gamepad_target(width, height)) return false;
    float r = (float)width * 9.0f / ((float)height * 16.0f);
    return r > 0.97f && r < 1.03f;
}
bool target_aspect_factors(uint32_t w, uint32_t h, float& kx, float& ky) {
    bool on = screen_shaped(w, h, false, 1, 1) && (g_aspect_kx != 1.0f || g_aspect_ky != 1.0f);
    kx = on ? g_aspect_kx : 1.0f;
    ky = on ? g_aspect_ky : 1.0f;
    return on;
}
static void target_aspect(const Surface* s, float& kx, float& ky) {
    uint32_t width,height;
    if(!s->fmt.compressed&&s->mips==1&&mods::cemu::texture_extent(s->width,s->height,s->format,s->slices,s->tileMode,width,height)){
        kx=float(width)/s->width;ky=float(height)/s->height;return;
    }
    bool on = screen_shaped(s->width, s->height, s->fmt.compressed, s->mips, s->slices);
    kx = on ? g_aspect_kx : 1.0f;
    ky = on ? g_aspect_ky : 1.0f;
}

static float target_scale(const Surface* s) {
    uint32_t width,height;
    if(!s->fmt.compressed&&s->mips==1&&mods::cemu::texture_extent(s->width,s->height,s->format,s->slices,s->tileMode,width,height))return 1.0f;
    if (s->fmt.compressed || s->mips > 1) return 1.0f;
    static const float shadow = env::SHADOW_SCALE() ? parse_scale(env::SHADOW_SCALE()) : 0.0f;
    if (shadow && s->isDepth && s->slices > 1) return shadow;
    return res_scale();
}

constexpr uint32_t kDim2D = 1, kDim3D = 2, kDim2DArray = 5;

Surface* color_target(const uint32_t* regs, int i, uint32_t* slice) {
    uint32_t base = regs[mmCB_COLOR0_BASE + i];
    if (!base) return nullptr;
    uint32_t size = regs[mmCB_COLOR0_SIZE + i], info = regs[mmCB_COLOR0_INFO + i];
    uint32_t pitch = ((size & 0x3FF) + 1) * 8;
    uint32_t height = (((size >> 10) & 0xFFFFF) + 1) * 64 / pitch;

    uint32_t tile = regs[mmCB_COLOR0_TILE + i];
    uint32_t w = tile & 0xFFFF, h = regs[mmCB_COLOR0_FRAG + i];
    uint32_t slices = gx2::color_target_slices(tile);
    bool volume = (tile & gx2::kColorTarget3D) != 0;
    if (slice) *slice = slices > 1 ? std::min<uint32_t>(regs[mmCB_COLOR0_VIEW + i] & 0x7FF, slices - 1) : 0;
    static const uint32_t numberBits[8] = {0, 0x200, 0, 0, 0x100, 0x300, 0x400, 0x800};
    SurfaceDesc d;
    d.addr = base;
    d.width = w ? w : pitch;
    d.height = h ? h : height;
    d.pitch = pitch;
    d.format = ((info >> 2) & 0x3F) | numberBits[(info >> 12) & 7];
    d.tileMode = (info >> 8) & 0xF;
    d.slices = slices;
    if (volume && !R.imageView2DOn3DImage) {

        static bool logged = false;
        if (!logged) LOG("[gfx] Vulkan: this device cannot render into 3D texture slices; volume render targets stay empty");
        logged = true;
        volume = false;
    }
    d.dim = volume ? kDim3D : slices > 1 ? kDim2DArray : kDim2D;
    return find_or_create_surface(d, true);
}

Surface* depth_target(const uint32_t* regs, uint32_t* slice) {
    uint32_t base = regs[mmDB_DEPTH_BASE];
    if (!base) return nullptr;
    uint32_t slices = std::max<uint32_t>(regs[gx2::kDepthSlicesReg], 1);
    if (slice) *slice = slices > 1 ? std::min<uint32_t>(regs[mmDB_DEPTH_VIEW] & 0x7FF, slices - 1) : 0;
    uint32_t size = regs[mmDB_DEPTH_SIZE], info = regs[mmDB_DEPTH_INFO];
    uint32_t pitch = ((size & 0x3FF) + 1) * 8;
    uint32_t height = (((size >> 10) & 0xFFFFF) + 1) * 64 / pitch;
    uint32_t wh = regs[mmDB_HTILE_DATA_BASE];
    static const uint32_t fmts[8] = {0, 0x005, 0, 0x011, 0, 0x811, 0x80E, 0x81C};
    SurfaceDesc d;
    d.addr = base;
    d.width = wh ? (wh >> 16) : pitch;
    d.height = wh ? (wh & 0xFFFF) : height;
    d.pitch = pitch;
    d.format = fmts[info & 7];
    d.isDepth = true;
    d.slices = slices;
    d.dim = slices > 1 ? kDim2DArray : kDim2D;
    return find_or_create_surface(d, true);
}

Surface* surface_from_color_buffer(uint32_t addr, uint32_t* firstSlice, uint32_t* numSlices) {
    auto* cb = (GX2::GX2ColorBuffer*)mem::ptr(addr);
    SurfaceDesc d;
    bool volume = cb->surface.dim.value() == Latte::E_DIM::DIM_3D;
    uint32_t slices = cb->surface.dim.value() == Latte::E_DIM::DIM_2D_ARRAY ? std::max<uint32_t>(cb->surface.depth, 1)
                      : volume ? std::max<uint32_t>(cb->surface.depth >> cb->viewMip, 1) : 1;
    d.slices = slices;
    d.dim = volume ? kDim3D : slices > 1 ? kDim2DArray : kDim2D;
    if (firstSlice) *firstSlice = std::min<uint32_t>(cb->viewFirstSlice, slices - 1);
    if (numSlices) *numSlices = std::clamp<uint32_t>(cb->viewNumSlices, 1, slices - std::min<uint32_t>(cb->viewFirstSlice, slices - 1));
    d.addr = gx2::color_buffer_address(cb);
    d.width = std::max<uint32_t>(cb->surface.width >> cb->viewMip, 1);
    d.height = std::max<uint32_t>(cb->surface.height >> cb->viewMip, 1);
    d.pitch = cb->surface.pitch;
    d.format = (uint32_t)cb->surface.format.value();
    d.tileMode = (uint32_t)cb->surface.tileMode.value();
    return find_or_create_surface(d, true);
}

Surface* surface_from_depth_buffer(uint32_t addr, uint32_t* firstSlice, uint32_t* numSlices) {
    auto* db = (GX2::GX2DepthBuffer*)mem::ptr(addr);
    SurfaceDesc d;
    uint32_t slices = db->surface.dim.value() == Latte::E_DIM::DIM_2D_ARRAY ? std::max<uint32_t>(db->surface.depth, 1) : 1;
    d.slices = slices;
    d.dim = slices > 1 ? kDim2DArray : kDim2D;
    if (firstSlice) *firstSlice = std::min<uint32_t>(db->viewFirstSlice, slices - 1);
    if (numSlices) *numSlices = std::clamp<uint32_t>(db->viewNumSlices, 1, slices - std::min<uint32_t>(db->viewFirstSlice, slices - 1));
    d.addr = db->surface.imagePtr;
    d.width = db->surface.width;
    d.height = db->surface.height;
    d.pitch = db->surface.pitch;
    d.format = (uint32_t)db->surface.format.value();
    d.tileMode = (uint32_t)db->surface.tileMode.value();
    d.isDepth = true;
    return find_or_create_surface(d, true);
}

static uint64_t sparse_hash(Surface* s);
uint64_t g_stat_full_checks, g_stat_uploads, g_stat_invalidates, g_stat_invalidated_surfaces, g_stat_upload_bytes;

static Surface* with_mip_chain(Surface* s, const SurfaceDesc& d) {
    static const bool off = env::NO_RT_MIPS() != nullptr;
    uint32_t mips = d.mips;
    if (off || mips <= 1 || s->fmt.depth || s->fmt.compressed || s->fmt.kind != FormatInfo::FLOAT || s->imageType != VK_IMAGE_TYPE_2D || s->arrayLayers != 1 ||
        s->mips != 1 || !s->image)
        return s;
    const uint32_t w = s->extent.width, h = s->extent.height;
    uint32_t full = 1;
    while ((std::max(w, h) >> full) > 0) full++;
    mips = std::min(mips, full);
    if (mips <= 1) return s;
    static std::unordered_map<VkFormat, bool> blittable;
    auto bl = blittable.find(s->fmt.pixel);
    if (bl == blittable.end()) {
        VkFormatProperties fp;
        vkGetPhysicalDeviceFormatProperties(R.physicalDevice, s->fmt.pixel, &fp);
        const VkFormatFeatureFlags need = VK_FORMAT_FEATURE_BLIT_SRC_BIT | VK_FORMAT_FEATURE_BLIT_DST_BIT |
                                          VK_FORMAT_FEATURE_SAMPLED_IMAGE_FILTER_LINEAR_BIT;
        bl = blittable.emplace(s->fmt.pixel, (fp.optimalTilingFeatures & need) == need).first;
    }
    if (!bl->second) return s;

    Surface* levels[16] = {};
    uint64_t key = s->writeSeq * 0x9E3779B97F4A7C15ull;
    for (uint32_t l = 1; l < mips && l < 16; l++) {
        levels[l] = ::gfx::render_mips::game_level(d, l, R.surfaces);
        key = (key ^ (levels[l] ? levels[l]->writeSeq + l : l)) * 0xFF51AFD7ED558CCDull;
    }
    Surface* c = s->mipChain.get();
    if (c && (c->mips != mips || c->extent.width != w || c->extent.height != h || c->fmt.pixel != s->fmt.pixel)) {
        destroy_surface_image(c);
        s->mipChain.reset();
        c = nullptr;
    }
    if (!c) {
        auto chain = std::make_shared<Surface>();
        c = chain.get();
        c->addr = s->addr; c->mipAddr = d.mipAddr;
        c->width = s->width; c->height = s->height; c->slices = 1;
        c->mips = mips; c->format = s->format; c->dim = s->dim; c->fmt = s->fmt;
        c->gpuWritten = true;
        create_surface_image(c, false, s->extent);
        s->mipChain = std::move(chain);
        s->mipChainSeq = ~0ull;
    }
    if (s->mipChainSeq == key) return c;
    transition_image(s, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, VK_PIPELINE_STAGE_TRANSFER_BIT, VK_ACCESS_TRANSFER_READ_BIT);
    for (uint32_t l = 1; l < mips && l < 16; l++)
        if (levels[l]) transition_image(levels[l], VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, VK_PIPELINE_STAGE_TRANSFER_BIT, VK_ACCESS_TRANSFER_READ_BIT);
    transition_image(c, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, VK_PIPELINE_STAGE_TRANSFER_BIT, VK_ACCESS_TRANSFER_WRITE_BIT);
    VkCommandBuffer cmd = command_buffer();
    VkImageCopy cp{};
    cp.srcSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1};
    cp.dstSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1};
    cp.extent = {w, h, 1};
    vkCmdCopyImage(cmd, s->image, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, c->image, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &cp);
    auto to_src = [&](uint32_t level) {
        VkImageMemoryBarrier b{VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER};
        b.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
        b.dstAccessMask = VK_ACCESS_TRANSFER_READ_BIT;
        b.oldLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
        b.newLayout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
        b.srcQueueFamilyIndex = b.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        b.image = c->image;
        b.subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, level, 1, 0, 1};
        vkCmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT, 0, 0, nullptr, 0, nullptr, 1, &b);
    };
    uint32_t fromGame = 0;
    for (uint32_t l = 1; l < mips; l++) {
        to_src(l - 1);
        VkImageBlit b{};
        b.dstSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, l, 0, 1};
        b.dstOffsets[1] = {(int32_t)std::max(w >> l, 1u), (int32_t)std::max(h >> l, 1u), 1};
        Surface* g = l < 16 ? levels[l] : nullptr;
        if (g) {
            b.srcSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1};
            b.srcOffsets[1] = {(int32_t)g->extent.width, (int32_t)g->extent.height, 1};
            vkCmdBlitImage(cmd, g->image, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, c->image, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1,
                           &b, VK_FILTER_LINEAR);
            fromGame++;
        } else {
            b.srcSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, l - 1, 0, 1};
            b.srcOffsets[1] = {(int32_t)std::max(w >> (l - 1), 1u), (int32_t)std::max(h >> (l - 1), 1u), 1};
            vkCmdBlitImage(cmd, c->image, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, c->image, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1,
                           &b, VK_FILTER_LINEAR);
        }
    }
    to_src(mips - 1);

    derive_dependency(c->use, VK_PIPELINE_STAGE_TRANSFER_BIT, VK_ACCESS_TRANSFER_READ_BIT, true);
    c->layout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
    s->mipChainSeq = key;
    static int logged = 0;
    if (logged++ < 8) LOG("[gfx] mip chain for %08X %ux%u: %u levels, %u of them the game's own", s->addr, w, h, mips, fromGame);
    return c;
}

Surface* sampled_texture(const uint32_t* w, bool isDepthSampler) {
    Latte::LATTE_SQ_TEX_RESOURCE_WORD0_N w0;
    Latte::LATTE_SQ_TEX_RESOURCE_WORD1_N w1;
    Latte::LATTE_SQ_TEX_RESOURCE_WORD4_N w4;
    Latte::LATTE_SQ_TEX_RESOURCE_WORD5_N w5;
    memcpy(&w0, &w[0], 4);
    memcpy(&w1, &w[1], 4);
    memcpy(&w4, &w[4], 4);
    memcpy(&w5, &w[5], 4);
    uint32_t addr = w[2] << 8, mipAddr = w[3] << 8;
    if (!addr) return nullptr;
    auto dim = w0.get_DIM();
    uint32_t pitch = (w0.get_PITCH() + 1) << 3;
    uint32_t width = w0.get_WIDTH() + 1;
    uint32_t height = w1.get_HEIGHT() + 1;
    uint32_t depth = w1.get_DEPTH();
    if (dim == Latte::E_DIM::DIM_2D_ARRAY || dim == Latte::E_DIM::DIM_3D || dim == Latte::E_DIM::DIM_2D_ARRAY_MSAA ||
        dim == Latte::E_DIM::DIM_1D_ARRAY)
        depth += 1;
    else {
        if (dim == Latte::E_DIM::DIM_CUBEMAP) depth = 6 * (depth + 1);
        if (depth == 0) depth = 1;
    }
    if (dim == Latte::E_DIM::DIM_1D || dim == Latte::E_DIM::DIM_1D_ARRAY) height = 1;
    auto tileMode = w0.get_TILE_MODE();
    if (Latte::IsCompressedFormat(w1.get_DATA_FORMAT())) pitch /= 4;
    uint32_t swizzle = 0;
    if (Latte::TM_IsMacroTiled(tileMode)) {
        swizzle = addr & 0x700;
        addr &= ~0x700u;
    }
    SurfaceDesc d;
    d.addr = addr;
    d.mipAddr = mipAddr;
    d.width = width;
    d.height = height;
    d.slices = depth;
    d.pitch = pitch;
    d.mips = w5.get_LAST_LEVEL() + 1;
    d.format = (uint32_t)LatteTexture_ReconstructGX2Format(w1, w4);
    d.dim = (uint32_t)dim;
    d.tileMode = (uint32_t)tileMode;
    d.swizzle = swizzle;
    d.isDepth = isDepthSampler;
    Surface* s = find_or_create_surface(d, false);
    upload_surface(s);
    return s && s->gpuWritten && d.mips > 1 && !d.isDepth ? with_mip_chain(s, d) : s;
}

static uint32_t mip_base(Surface* s, uint32_t level);

static void decode_level_slow(Surface* s, uint32_t level, uint32_t base, std::vector<uint8_t>& out, uint32_t& outW,
                              uint32_t& outH, uint32_t& outSlices) {
    const FormatInfo& f = s->fmt;
    uint32_t w = std::max(s->width >> level, 1u), h = std::max(s->height >> level, 1u);
    uint32_t slices = s->dim == (uint32_t)Latte::E_DIM::DIM_3D ? std::max(s->slices >> level, 1u) : s->slices;
    uint32_t bw = f.compressed ? (w + 3) / 4 : w, bh = f.compressed ? (h + 3) / 4 : h;
    outW = w;
    outH = h;
    outSlices = slices;

    const auto& info = guest_info(s, level);
    uint32_t pitch = level == 0 && s->pitch ? s->pitch : info.pitch, height = info.height;
    auto tm = (Latte::E_HWTILEMODE)info.hwTileMode;
    uint32_t bpp = f.bytesPerBlock * 8;
    bool depthData = s->isDepth || f.convert == Convert::D24_R32F;
    uint32_t pipeSwizzle = (s->swizzle >> 8) & 1, bankSwizzle = (s->swizzle >> 9) & 3;

    out.assign((size_t)bw * bh * slices * f.hostBytesPerBlock, 0);
    std::vector<uint8_t> row(bw * f.bytesPerBlock);
    const uint8_t* src = mem::ptr(base);
    for (uint32_t z = 0; z < slices; z++) {
        LatteAddrLib::CachedSurfaceAddrInfo ci;
        bool macro = Latte::TM_IsMacroTiled(tm);
        if (macro)
            LatteAddrLib::SetupCachedSurfaceAddrInfo(&ci, z, 0, bpp, pitch, height, slices, 1, tm, depthData, pipeSwizzle, bankSwizzle);
        for (uint32_t y = 0; y < bh; y++) {
            for (uint32_t x = 0; x < bw; x++) {
                uint32_t off;
                if (tm == Latte::E_HWTILEMODE::TM_LINEAR_GENERAL || tm == Latte::E_HWTILEMODE::TM_LINEAR_ALIGNED)
                    off = LatteAddrLib::ComputeSurfaceAddrFromCoordLinear(x, y, z, 0, bpp, pitch, height, slices);
                else if (!macro)
                    off = LatteAddrLib::ComputeSurfaceAddrFromCoordMicroTiled(x, y, z, bpp, pitch, height, tm, depthData);
                else
                    off = LatteAddrLib::ComputeSurfaceAddrFromCoordMacroTiledCached(x, y, &ci);
                memcpy(&row[x * f.bytesPerBlock], src + off, f.bytesPerBlock);
            }
            uint8_t* dst = &out[((size_t)z * bh + y) * bw * f.hostBytesPerBlock];
            if (f.convert == Convert::NONE) memcpy(dst, row.data(), row.size());
            else convert_row(f.convert, row.data(), dst, bw);
        }
    }
}

template<typename T>
struct CopyPod {
    T value;
} __attribute__((packed, may_alias));
template<typename T>
static T load_pod(const uint8_t* p) { return reinterpret_cast<const CopyPod<T>*>(p)->value; }
template<typename T>
static void store_pod(uint8_t* p, T value) { reinterpret_cast<CopyPod<T>*>(p)->value = value; }
template<uint32_t B>
static uint32_t thin1_elem(uint32_t index) {
    uint32_t elem = index * B;
    if constexpr (B > 4) elem = (elem & 0xFF) | ((elem & ~0xFFu) << 3);
    return elem;
}
template<uint32_t B>
static bool thin1_row_packed(const uint16_t* pixel) {
    if constexpr (B != 1 && B != 4 && B != 8) return false;
    const uint32_t base = thin1_elem<B>(pixel[0]);
    for (uint32_t rx = 0; rx < 8; rx++) {
        uint32_t step = rx;
        if constexpr (B == 8) {
            constexpr uint8_t step8[8] = {0, 1, 4, 5, 8, 9, 12, 13};
            step = step8[rx];
        } else if constexpr (B == 4) {
            constexpr uint8_t step4[8] = {0, 1, 2, 3, 8, 9, 10, 11};
            step = step4[rx];
        }
        if (thin1_elem<B>(pixel[rx]) != base + step * B) return false;
    }
    return true;
}
static bool thin1_row_packed_n(const uint16_t* pixel, uint32_t bytes) {
    if (bytes == 8) return thin1_row_packed<8>(pixel);
    if (bytes == 4) return thin1_row_packed<4>(pixel);
    if (bytes == 1) return thin1_row_packed<1>(pixel);
    return false;
}
template<uint32_t B>
static void copy_thin1_row(uint8_t* dst, const uint8_t* srcTile, const uint16_t* pixel, bool packed) {
    if (packed) {
        if constexpr (B == 8) {
            const uint8_t* src = srcTile + thin1_elem<8>(pixel[0]);
            store_pod<uint64_t>(dst + 0, load_pod<uint64_t>(src + 0));
            store_pod<uint64_t>(dst + 8, load_pod<uint64_t>(src + 8));
            store_pod<uint64_t>(dst + 16, load_pod<uint64_t>(src + 32));
            store_pod<uint64_t>(dst + 24, load_pod<uint64_t>(src + 40));
            store_pod<uint64_t>(dst + 32, load_pod<uint64_t>(src + 64));
            store_pod<uint64_t>(dst + 40, load_pod<uint64_t>(src + 72));
            store_pod<uint64_t>(dst + 48, load_pod<uint64_t>(src + 96));
            store_pod<uint64_t>(dst + 56, load_pod<uint64_t>(src + 104));
            return;
        } else if constexpr (B == 4) {
            const uint8_t* src = srcTile + thin1_elem<4>(pixel[0]);
            store_pod<uint64_t>(dst + 0, load_pod<uint64_t>(src + 0));
            store_pod<uint64_t>(dst + 8, load_pod<uint64_t>(src + 8));
            store_pod<uint64_t>(dst + 16, load_pod<uint64_t>(src + 32));
            store_pod<uint64_t>(dst + 24, load_pod<uint64_t>(src + 40));
            return;
        } else if constexpr (B == 1) {
            store_pod<uint64_t>(dst, load_pod<uint64_t>(srcTile + thin1_elem<1>(pixel[0])));
            return;
        }
    }
    for (uint32_t rx = 0; rx < 8; rx++) {
        const uint8_t* src = srcTile + thin1_elem<B>(pixel[rx]);
        if constexpr (B == 1) *dst = *src;
        else if constexpr (B == 2) store_pod<uint16_t>(dst, load_pod<uint16_t>(src));
        else if constexpr (B == 4) store_pod<uint32_t>(dst, load_pod<uint32_t>(src));
        else store_pod<uint64_t>(dst, load_pod<uint64_t>(src));
        dst += B;
    }
}
template<uint32_t B>
static void copy_thin1_tile(uint8_t* dst, const uint8_t* src, const uint16_t* sliceRows, const bool* packedRow,
                            LatteAddrLib::CachedSurfaceAddrInfo* ci, uint32_t bw, uint32_t bx, uint32_t by) {
    const uint8_t* srcTile = src + LatteAddrLib::ComputeSurfaceAddrFromCoordMacroTiledCached(bx, by, ci);
    for (uint32_t ry = 0; ry < 8; ry++)
        copy_thin1_row<B>(dst + ((size_t)(by + ry) * bw + bx) * B, srcTile, sliceRows + (ry << 3), packedRow[ry]);
}
template<uint32_t B>
static void copy_thin1_tiles(uint8_t* dst, const uint8_t* src, const uint16_t* sliceRows, const bool* packedRow,
                             LatteAddrLib::CachedSurfaceAddrInfo* ci, uint32_t bw, uint32_t bxEnd, uint32_t byEnd) {
    for (uint32_t by = 0; by < byEnd; by += 8)
        for (uint32_t bx = 0; bx < bxEnd; bx += 8)
            copy_thin1_tile<B>(dst, src, sliceRows, packedRow, ci, bw, bx, by);
}
static bool decode_level_fast(Surface* s, uint32_t level, uint32_t base, uint8_t* out, uint32_t bw, uint32_t bh, uint32_t slices) {
    const FormatInfo& f = s->fmt;
    if (!(f.convert == Convert::NONE && f.hostBytesPerBlock == f.bytesPerBlock && f.bytesPerBlock && f.bytesPerBlock <= 8))
        return false;
    if (s->isDepth) return false;
    if (f.compressed) return false;
    const auto& info = guest_info(s, level);
    uint32_t pitch = level == 0 && s->pitch ? s->pitch : info.pitch, height = info.height;
    auto tm = (Latte::E_HWTILEMODE)info.hwTileMode;
    uint32_t bpp = info.bpp;
    if (bpp % 8 || bpp > 256 || !bpp) return false;
    const uint32_t bytes = bpp / 8;
    uint32_t pipeSwizzle = (s->swizzle >> 8) & 1, bankSwizzle = (s->swizzle >> 9) & 3;
    const uint32_t host = f.hostBytesPerBlock;
    if (host != bytes) return false;
    const uint8_t* src = mem::ptr(base);
    LatteAddrLib::CachedSurfaceAddrInfo ci;
    if (tm == Latte::E_HWTILEMODE::TM_2D_TILED_THIN1) {
        const uint16_t* table = ci.microTilePixelIndexTable;
        const uint32_t shuffle = bpp > 32;
        for (uint32_t z = 0; z < slices; z++) {
            LatteAddrLib::SetupCachedSurfaceAddrInfo(&ci, z, 0, bpp, pitch, height, slices, 1, tm, false, pipeSwizzle, bankSwizzle);
            uint8_t* dst = out + (size_t)z * bh * bw * host;
            const uint32_t bxEnd = bw & ~7u, byEnd = bh & ~7u;
            const uint16_t* sliceRows = table + ((z & 7) << 6);
            bool packedRow[8];
            for (uint32_t ry = 0; ry < 8; ry++) packedRow[ry] = thin1_row_packed_n(sliceRows + (ry << 3), bytes);
            switch (bytes) {
            case 8: copy_thin1_tiles<8>(dst, src, sliceRows, packedRow, &ci, bw, bxEnd, byEnd); break;
            case 4: copy_thin1_tiles<4>(dst, src, sliceRows, packedRow, &ci, bw, bxEnd, byEnd); break;
            case 2: copy_thin1_tiles<2>(dst, src, sliceRows, packedRow, &ci, bw, bxEnd, byEnd); break;
            case 1: copy_thin1_tiles<1>(dst, src, sliceRows, packedRow, &ci, bw, bxEnd, byEnd); break;
            default:
                for (uint32_t by = 0; by < byEnd; by += 8) {
                    for (uint32_t bx = 0; bx < bxEnd; bx += 8) {
                        const uint8_t* srcTile = src + LatteAddrLib::ComputeSurfaceAddrFromCoordMacroTiledCached(bx, by, &ci);
                        for (uint32_t ry = 0; ry < 8; ry++) {
                            uint8_t* dstRow = dst + ((size_t)(by + ry) * bw + bx) * host;
                            const uint16_t* pixel = sliceRows + (ry << 3);
                            for (uint32_t rx = 0; rx < 8; rx++) {
                                uint32_t elem = (uint32_t)pixel[rx] * bytes;
                                if (shuffle) elem = (elem & 0xFF) | ((elem & ~0xFFu) << 3);
                                memcpy(dstRow, srcTile + elem, host);
                                dstRow += host;
                            }
                        }
                    }
                }
                break;
            }
            for (uint32_t by = 0; by < byEnd; by += 8) {
                for (uint32_t y = by; y < by + 8; y++) {
                    uint8_t* dstRow = dst + (size_t)y * bw * host;
                    for (uint32_t x = bxEnd; x < bw; x++) {
                        uint32_t off = LatteAddrLib::ComputeSurfaceAddrFromCoordMacroTiledCached(x, y, &ci);
                        memcpy(dstRow + (size_t)x * host, src + off, host);
                    }
                }
            }
            for (uint32_t y = byEnd; y < bh; y++) {
                uint8_t* dstRow = dst + (size_t)y * bw * host;
                for (uint32_t x = 0; x < bw; x++) {
                    uint32_t off = LatteAddrLib::ComputeSurfaceAddrFromCoordMacroTiledCached(x, y, &ci);
                    memcpy(dstRow + (size_t)x * host, src + off, host);
                }
            }
        }
        return true;
    }
    if (tm == Latte::E_HWTILEMODE::TM_LINEAR_ALIGNED) {
        for (uint32_t z = 0; z < slices; z++) {
            const uint8_t* srcSlice = src + (uint32_t)(((uint64)z * height * pitch * bpp) / 8);
            uint8_t* dst = out + (size_t)z * bh * bw * host;
            for (uint32_t y = 0; y < bh; y++)
                memcpy(dst + (size_t)y * bw * host, srcSlice + (size_t)y * pitch * host, (size_t)bw * host);
        }
        return true;
    }
    return false;
}

static bool decode_check() {
    static const bool on = [] { const char* e = env::DECODE_CHECK(); return e && *e == '1'; }();
    return on;
}
static void decode_level(Surface* s, uint32_t level, uint32_t base, std::vector<uint8_t>& out, uint32_t& outW,
                         uint32_t& outH, uint32_t& outSlices) {
    const FormatInfo& f = s->fmt;
    uint32_t w = std::max(s->width >> level, 1u), h = std::max(s->height >> level, 1u);
    uint32_t slices = s->dim == (uint32_t)Latte::E_DIM::DIM_3D ? std::max(s->slices >> level, 1u) : s->slices;
    uint32_t bw = f.compressed ? (w + 3) / 4 : w, bh = f.compressed ? (h + 3) / 4 : h;
    outW = w;
    outH = h;
    outSlices = slices;
    out.assign((size_t)bw * bh * slices * f.hostBytesPerBlock, 0);
    if (!decode_check()) {
        if (decode_level_fast(s, level, base, out.data(), bw, bh, slices)) return;
        decode_level_slow(s, level, base, out, outW, outH, outSlices);
        return;
    }
    std::vector<uint8_t> slow;
    bool fast = decode_level_fast(s, level, base, out.data(), bw, bh, slices);
    if (!fast) {
        decode_level_slow(s, level, base, out, outW, outH, outSlices);
        return;
    }
    uint32_t sw0=0,sh0=0,ss0=0;
    decode_level_slow(s, level, base, slow, sw0, sh0, ss0);
    if (slow != out) {
        size_t diffs = 0, first = SIZE_MAX;
        for (size_t i = 0; i < slow.size() && i < out.size(); i++)
            if (slow[i] != out[i]) { diffs++; if (first == SIZE_MAX) first = i; }
        char dump[280];
        int kd = 0;
        for (size_t i = first; i < slow.size() && kd < 120; i++) {
            if (slow[i] != out[i]) kd += snprintf(dump + kd, sizeof dump - kd, " %zu(S%02X/F%02X)", i, slow[i], out[i]);
        }
        dump[kd] = 0;
        LOG("[decode check] MISMATCH %08X level %u %ux%u fmt %02X tile %u: fast %d, %zu bytes differ, first at %zu:%s",
            s->addr, level, w, h, s->format, s->tileMode, (int)fast, diffs, first, dump);
        throw std::runtime_error("fast texture decode differs from the slow decoder");
    }
}



static uint32_t mip_base(Surface* s, uint32_t level);

static SparseHashStats sparseStats;
SparseHashStats sparse_hash_stats() { return sparseStats; }

namespace {
			struct UploadProbe {
  uint64_t uploads = 0;
  std::unordered_map<uint32_t, std::pair<uint64_t, uint64_t>> count;
  std::unordered_map<uint32_t, bool> seen;
  static bool enabled() { static const bool on = env::UPLOAD_PROFILE() != nullptr; return on; }
  void record(const Surface* s, size_t bytes) {
    ++uploads;
    auto& e = count[s->addr & ~0x7FFull];
    e.first++, e.second += bytes;
    if (seen.emplace(s->addr, true).second) {
      LOG("[upload profile] surface %08X %ux%ux%u mips %u fmt %02X tile %u size %.2f MiB gpuWritten %u",
          s->addr, s->width, s->height, s->slices, s->mips, s->format, s->tileMode, bytes / 1048576.0, s->gpuWritten);
    }
    if (uploads % 300) return;
    std::vector<std::pair<uint32_t, std::pair<uint64_t, uint64_t>>> v;
    for (auto& [a, c] : count) v.push_back({a, c});
    std::sort(v.begin(), v.end(), [](auto& a, auto& b) { return a.second.second > b.second.second; });
    size_t n = std::min<size_t>(8, v.size());
    std::string line;
    for (size_t i = 0; i < n; i++)
      line += std::to_string(v[i].first) + ": x" + std::to_string(v[i].second.first) + " "
          + std::to_string(v[i].second.second / 1048576) + "MiB  ";
    LOG("[upload profile] %u surfaces: %s", (unsigned)count.size(), line.c_str());
  }
};
UploadProbe g_uploadProbe;
}

static uint64_t sparse_hash(Surface* s) {
    static const bool enabled = [] {
        const char* value = env::VK_SPARSE_HASH_MEMO();
        return value && !strcmp(value,"1");
    }();
    static const bool collectStats = env::VK_STATS() != nullptr;
    if(!enabled && !collectStats) {

        uint64_t h = 0xcbf29ce484222325ull;
        for(uint32_t level = 0; level < s->mips; ++level) {
            const auto& info = guest_info(s, level);
            const auto* bytes = mem::ptr(mip_base(s, level));
            size_t size = size_t(info.surfSize), step = std::max<size_t>((size / 256) & ~size_t(7), 8);
            size_t offset = 0;
            for(; offset < size && size - offset >= 8; offset += step) {
                uint64_t value;
                memcpy(&value, bytes + offset, 8);
                h = (h ^ value) * 0x100000001b3ull;
            }
            if(offset < size) {
                uint64_t value = 0;
                memcpy(&value, bytes + offset, size - offset);
                h = (h ^ value) * 0x100000001b3ull;
            }
        }
        return h;
    }
    if(collectStats) ++sparseStats.checks;
    if(!enabled) {
        uint64_t hash = SparseHashMemo<>::basis;
        for(uint32_t level = 0; level < s->mips; ++level) {
            const auto& info = guest_info(s, level);
            const auto counts = sparse_sample_counts(size_t(info.surfSize));
            sparseStats.sampleBytes += counts.bytes;
            sparseStats.mixerWords += counts.words;
            sparse_sample_words(mem::ptr(mip_base(s, level)), size_t(info.surfSize),
                [&](uint64_t value, size_t) {
                    hash = (hash ^ value) * 0x100000001b3ull;
                });
        }
        return hash;
    }
    static const bool largerMemo = [] {
        const char* value = env::VK_SPARSE_HASH_ENTRIES();
        return value && !strcmp(value,"256");
    }();
    auto runMemo = [&](auto& memo) {
        memo.begin();
        for(uint32_t level = 0; level < s->mips; ++level) {
            const auto& info = guest_info(s, level);
            if(collectStats) sparseStats.sampleBytes += sparse_sample_counts(size_t(info.surfSize)).bytes;
            sparse_sample_words(mem::ptr(mip_base(s, level)), size_t(info.surfSize),
                [&](uint64_t value, size_t) { memo.add(value); });
        }
        SparseHashStats discarded;
        return memo.finish(reinterpret_cast<uintptr_t>(s), collectStats ? sparseStats : discarded);
    };
    if(largerMemo) {

        static std::unique_ptr<SparseHashMemo<256>> memo(new SparseHashMemo<256>);
        return runMemo(*memo);
    }
    static SparseHashMemo<> memo;
    return runMemo(memo);
}

static uint32_t level_address(GX2Surface* s, uint32_t level) {
    if (level == 0) return s->imagePtr;
    if (level == 1) return s->mipPtr;
    return s->mipPtr + s->mipOffset[level - 1];
}

static uint32_t element_offset(const LatteAddrLib::AddrSurfaceInfo_OUT& info, Latte::E_HWTILEMODE tm, uint32_t x, uint32_t y,
                               uint32_t slice, uint32_t bpp, uint32_t swizzle, LatteAddrLib::CachedSurfaceAddrInfo* ci, bool depth) {
    if (tm == Latte::E_HWTILEMODE::TM_LINEAR_GENERAL || tm == Latte::E_HWTILEMODE::TM_LINEAR_ALIGNED)
        return LatteAddrLib::ComputeSurfaceAddrFromCoordLinear(x, y, slice, 0, bpp, info.pitch, info.height, info.depth);
    if (!Latte::TM_IsMacroTiled(tm))
        return LatteAddrLib::ComputeSurfaceAddrFromCoordMicroTiled(x, y, slice, bpp, info.pitch, info.height, tm, depth);
    return LatteAddrLib::ComputeSurfaceAddrFromCoordMacroTiledCached(x, y, ci);
}

}

namespace gfxvk {
void create_surface_image(Surface* s, bool forRendering, VkExtent3D explicitExtent) {
    if (!s || !s->width || !s->height || !s->slices || !s->mips)
        throw std::runtime_error("Vulkan surface has empty dimensions");
    if (s->image) throw std::runtime_error("Vulkan surface image must be retired before replacement");
    if (s->fmt.pixel == VK_FORMAT_UNDEFINED)
        throw std::runtime_error("Unsupported GX2 surface format " + std::to_string(s->format));
    auto dim=static_cast<Latte::E_DIM>(s->dim);
    if (dim==Latte::E_DIM::DIM_2D_MSAA || dim==Latte::E_DIM::DIM_2D_ARRAY_MSAA)
        throw std::runtime_error("Vulkan GX2 multisample surfaces require an explicit sample count");
    bool oneD=dim==Latte::E_DIM::DIM_1D || dim==Latte::E_DIM::DIM_1D_ARRAY;
    bool threeD=dim==Latte::E_DIM::DIM_3D;
    bool cube=dim==Latte::E_DIM::DIM_CUBEMAP;
    s->imageType=oneD?VK_IMAGE_TYPE_1D:threeD?VK_IMAGE_TYPE_3D:VK_IMAGE_TYPE_2D;
    s->arrayLayers=threeD?1:s->slices;
    s->viewType=threeD?VK_IMAGE_VIEW_TYPE_3D:oneD?(s->slices>1?VK_IMAGE_VIEW_TYPE_1D_ARRAY:VK_IMAGE_VIEW_TYPE_1D):
        cube?(s->slices>6?VK_IMAGE_VIEW_TYPE_CUBE_ARRAY:VK_IMAGE_VIEW_TYPE_CUBE):
        (s->slices>1?VK_IMAGE_VIEW_TYPE_2D_ARRAY:VK_IMAGE_VIEW_TYPE_2D);
    s->scale=forRendering&&!oneD&&!threeD&&!cube?target_scale(s):1.0f;
    s->ax=s->ay=1.0f;
    if(forRendering&&!oneD&&!threeD&&!cube)target_aspect(s,s->ax,s->ay);
    s->extent={uint32_t(std::ceil(s->width*s->scale*s->ax-0.01f)),oneD?1:uint32_t(std::ceil(s->height*s->scale*s->ay-0.01f)),threeD?s->slices:1};
    if(explicitExtent.width || explicitExtent.height || explicitExtent.depth) {
        if(s->imageType!=VK_IMAGE_TYPE_2D || !explicitExtent.width ||
           !explicitExtent.height || explicitExtent.depth!=1 ||
           explicitExtent.width>R.properties.limits.maxImageDimension2D ||
           explicitExtent.height>R.properties.limits.maxImageDimension2D)
            throw std::runtime_error("Invalid explicit private surface extent");
        s->extent=explicitExtent;
    }
    s->sx=float(s->extent.width)/s->width; s->sy=float(s->extent.height)/s->height;
    s->aspect=s->fmt.depth?VK_IMAGE_ASPECT_DEPTH_BIT:VK_IMAGE_ASPECT_COLOR_BIT;
    if(s->fmt.stencil)s->aspect|=VK_IMAGE_ASPECT_STENCIL_BIT;
    if(cube&&(s->extent.width!=s->extent.height||s->slices%6))throw std::runtime_error("Invalid GX2 cube surface dimensions");
    uint32_t maxDim=std::max({s->extent.width,s->extent.height,s->extent.depth});
    uint32_t maxMips=1; while(maxDim>1){maxDim>>=1;++maxMips;}
    if(s->mips>maxMips)throw std::runtime_error("GX2 surface requests too many mip levels");
    if (s->fmt.compressed) s->bcDecoded = bc_decode_required(format_info(s->format,s->isDepth));
    if (s->bcDecoded) {
        const bool sign = (s->format & 0x200) && (s->format & 0x3f) >= 0x34;
        s->fmt.pixel = sign ? VK_FORMAT_R8G8B8A8_SNORM : (s->format & 0x400) ? VK_FORMAT_R8G8B8A8_SRGB : VK_FORMAT_R8G8B8A8_UNORM;
    }
    VkFormatProperties properties{}; vkGetPhysicalDeviceFormatProperties(R.physicalDevice,s->fmt.pixel,&properties);
    auto features=properties.optimalTilingFeatures;
    VkFormatFeatureFlags required=VK_FORMAT_FEATURE_SAMPLED_IMAGE_BIT|VK_FORMAT_FEATURE_TRANSFER_SRC_BIT|VK_FORMAT_FEATURE_TRANSFER_DST_BIT;
    auto attachment=s->fmt.depth?VK_FORMAT_FEATURE_DEPTH_STENCIL_ATTACHMENT_BIT:VK_FORMAT_FEATURE_COLOR_ATTACHMENT_BIT;
    if(forRendering)required|=attachment;
    if((features&required)!=required)throw std::runtime_error("Vulkan device lacks required features for GX2 format "+std::to_string(s->format));
    VkImageUsageFlags usage=VK_IMAGE_USAGE_SAMPLED_BIT|VK_IMAGE_USAGE_TRANSFER_SRC_BIT|VK_IMAGE_USAGE_TRANSFER_DST_BIT;
    if(!s->fmt.compressed&&(features&attachment))usage|=s->fmt.depth?VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT:VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT;
    VkImageCreateInfo imageInfo{VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO};
    imageInfo.flags=(cube?VK_IMAGE_CREATE_CUBE_COMPATIBLE_BIT:0)|VK_IMAGE_CREATE_MUTABLE_FORMAT_BIT;

    if(threeD&&R.imageView2DOn3DImage&&(usage&VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT))imageInfo.flags|=VK_IMAGE_CREATE_2D_ARRAY_COMPATIBLE_BIT;
    imageInfo.imageType=s->imageType; imageInfo.format=s->fmt.pixel; imageInfo.extent=s->extent;
    imageInfo.mipLevels=s->mips;imageInfo.arrayLayers=s->arrayLayers;imageInfo.samples=VK_SAMPLE_COUNT_1_BIT;
    imageInfo.tiling=VK_IMAGE_TILING_OPTIMAL;imageInfo.usage=usage;s->usage=usage;s->createFlags=imageInfo.flags;imageInfo.sharingMode=VK_SHARING_MODE_EXCLUSIVE;
    imageInfo.initialLayout=VK_IMAGE_LAYOUT_UNDEFINED;
    try {
        check_vk(vkCreateImage(R.device,&imageInfo,nullptr,&s->image),"create image");
        VkMemoryRequirements needs{};vkGetImageMemoryRequirements(R.device,s->image,&needs);
        VkMemoryAllocateInfo allocation{VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO};allocation.allocationSize=needs.size;
        allocation.memoryTypeIndex=memory_type(needs.memoryTypeBits,VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
        check_vk(vkAllocateMemory(R.device,&allocation,nullptr,&s->memory),"allocate image memory");
        check_vk(vkBindImageMemory(R.device,s->image,s->memory,0),"bind image memory");
        VkImageViewCreateInfo viewInfo{VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO};viewInfo.image=s->image;
        viewInfo.viewType=s->viewType;viewInfo.format=s->fmt.pixel;
        viewInfo.subresourceRange={VkImageAspectFlags(s->fmt.depth?VK_IMAGE_ASPECT_DEPTH_BIT:VK_IMAGE_ASPECT_COLOR_BIT),0,s->mips,0,s->arrayLayers};
        check_vk(vkCreateImageView(R.device,&viewInfo,nullptr,&s->view),"create sampling image view");
        s->layout=VK_IMAGE_LAYOUT_UNDEFINED;s->use={};
    } catch(...) {
        if(s->view)vkDestroyImageView(R.device,s->view,nullptr);
        if(s->image)vkDestroyImage(R.device,s->image,nullptr);
        if(s->memory)vkFreeMemory(R.device,s->memory,nullptr);
        s->view=VK_NULL_HANDLE;s->image=VK_NULL_HANDLE;s->memory=VK_NULL_HANDLE;
        throw;
    }
}
static void thin1_forget(Surface* s);
void destroy_surface_image(Surface* s) {
    if (!s) return;
    thin1_forget(s);
    if (s->mipChain) { destroy_surface_image(s->mipChain.get()); s->mipChain.reset(); }
    if(!s->image)return;
    auto views=std::move(s->layerViews);if(s->view)views.push_back(s->view);
    for(auto& [key,view]:s->sampledViews)if(view)views.push_back(view);s->sampledViews.clear();
    defer_surface_image(s->image,s->memory,std::move(views));
    s->image=VK_NULL_HANDLE;s->memory=VK_NULL_HANDLE;s->view=VK_NULL_HANDLE;s->layout=VK_IMAGE_LAYOUT_UNDEFINED;s->use={};
    s->layerViews.clear();
}
VkImageView layer_view(Surface* s,uint32_t layer) {

    bool volume=s&&s->imageType==VK_IMAGE_TYPE_3D;
    uint32_t layers=!s?0:volume?s->extent.depth:s->arrayLayers;
    if(!s||!s->image||layer>=layers)throw std::runtime_error("Vulkan attachment layer is out of range");
    if(volume&&!(s->createFlags&VK_IMAGE_CREATE_2D_ARRAY_COMPATIBLE_BIT))throw std::runtime_error("Rendering into this GX2 volume slice is unsupported");
    if(s->layerViews.size()<layers)s->layerViews.resize(layers,VK_NULL_HANDLE);
    if(!s->layerViews[layer]) {
        VkImageViewCreateInfo info{VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO};info.image=s->image;
        info.viewType=s->imageType==VK_IMAGE_TYPE_1D?VK_IMAGE_VIEW_TYPE_1D:VK_IMAGE_VIEW_TYPE_2D;info.format=s->fmt.pixel;
        info.subresourceRange={s->aspect,0,1,layer,1};
        VkImageViewUsageCreateInfo usage{VK_STRUCTURE_TYPE_IMAGE_VIEW_USAGE_CREATE_INFO};
        usage.usage=R.attachmentViewUsage?attachment_view_usage(s->usage,s->fmt.depth):0;
        if(usage.usage)info.pNext=&usage;
        check_vk(vkCreateImageView(R.device,&info,nullptr,&s->layerViews[layer]),"create attachment layer view");
    }
    return s->layerViews[layer];
}
VkImageView sampled_texture_view(Surface* s,const uint32_t* texWords) {
    if(!s||!s->image||!texWords)throw std::runtime_error("Vulkan sampled view requires a surface and texture descriptor");
    Latte::LATTE_SQ_TEX_RESOURCE_WORD0_N w0;Latte::LATTE_SQ_TEX_RESOURCE_WORD4_N w4;
    memcpy(&w0,texWords,4);memcpy(&w4,texWords+4,4);
    auto dim=w0.get_DIM();VkImageViewType type;
    switch(dim) {
    case Latte::E_DIM::DIM_1D:type=VK_IMAGE_VIEW_TYPE_1D;break;
    case Latte::E_DIM::DIM_1D_ARRAY:type=VK_IMAGE_VIEW_TYPE_1D_ARRAY;break;
    case Latte::E_DIM::DIM_2D:type=VK_IMAGE_VIEW_TYPE_2D;break;
    case Latte::E_DIM::DIM_2D_ARRAY:type=VK_IMAGE_VIEW_TYPE_2D_ARRAY;break;
    case Latte::E_DIM::DIM_3D:type=VK_IMAGE_VIEW_TYPE_3D;break;
    case Latte::E_DIM::DIM_CUBEMAP:type=s->arrayLayers>6?VK_IMAGE_VIEW_TYPE_CUBE_ARRAY:VK_IMAGE_VIEW_TYPE_CUBE;break;
    default:throw std::runtime_error("Unsupported Vulkan sampled texture dimension");
    }
    bool oneD=type==VK_IMAGE_VIEW_TYPE_1D||type==VK_IMAGE_VIEW_TYPE_1D_ARRAY;
    bool threeD=type==VK_IMAGE_VIEW_TYPE_3D;
    bool cube=type==VK_IMAGE_VIEW_TYPE_CUBE||type==VK_IMAGE_VIEW_TYPE_CUBE_ARRAY;
    if((oneD&&s->imageType!=VK_IMAGE_TYPE_1D)||(threeD!=(s->imageType==VK_IMAGE_TYPE_3D))||(!oneD&&!threeD&&s->imageType!=VK_IMAGE_TYPE_2D))
        throw std::runtime_error("Vulkan sampled texture dimension does not match the backing image");
    if(cube&&(s->viewType!=VK_IMAGE_VIEW_TYPE_CUBE&&s->viewType!=VK_IMAGE_VIEW_TYPE_CUBE_ARRAY))
        throw std::runtime_error("Vulkan sampled cube requires a cube-compatible backing image");
    uint32_t selectors[4]={uint32_t(w4.get_DST_SEL_X()),uint32_t(w4.get_DST_SEL_Y()),uint32_t(w4.get_DST_SEL_Z()),uint32_t(w4.get_DST_SEL_W())};
    static const VkComponentSwizzle mapping[8]={VK_COMPONENT_SWIZZLE_R,VK_COMPONENT_SWIZZLE_G,VK_COMPONENT_SWIZZLE_B,VK_COMPONENT_SWIZZLE_A,VK_COMPONENT_SWIZZLE_ZERO,VK_COMPONENT_SWIZZLE_ONE,VK_COMPONENT_SWIZZLE_ZERO,VK_COMPONENT_SWIZZLE_ZERO};
    uint32_t key=uint32_t(type)<<12;for(unsigned i=0;i<4;++i)key|=(s->fmt.depth?i:selectors[i])<<(i*3);
    auto found=s->sampledViews.find(key);if(found!=s->sampledViews.end())return found->second;
    VkImageViewCreateInfo info{VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO};info.image=s->image;info.viewType=type;info.format=s->fmt.pixel;

    if(!s->fmt.depth)info.components={mapping[selectors[0]],mapping[selectors[1]],mapping[selectors[2]],mapping[selectors[3]]};
    uint32_t layers=(type==VK_IMAGE_VIEW_TYPE_1D||type==VK_IMAGE_VIEW_TYPE_2D||threeD)?1:type==VK_IMAGE_VIEW_TYPE_CUBE?6:s->arrayLayers;
    info.subresourceRange={VkImageAspectFlags(s->fmt.depth?VK_IMAGE_ASPECT_DEPTH_BIT:VK_IMAGE_ASPECT_COLOR_BIT),0,s->mips,0,layers};
    VkImageView view=VK_NULL_HANDLE;check_vk(vkCreateImageView(R.device,&info,nullptr,&view),"create sampled texture view");s->sampledViews.emplace(key,view);return view;
}

DepthCopyOverride g_depthCopyOverride=[]{
    const char* e=env::VK_DEPTH_COPY();
    return !e?DepthCopyOverride::None:!strcmp(e,"draw")?DepthCopyOverride::Draw:!strcmp(e,"none")?DepthCopyOverride::Unsupported:DepthCopyOverride::None;
}();
enum class ScaledCopy { Blit, Draw, Unsupported };

static ScaledCopy scaled_copy_mode(const FormatInfo& fmt) {
    VkFormatProperties properties{};vkGetPhysicalDeviceFormatProperties(R.physicalDevice,fmt.pixel,&properties);
    const auto features=properties.optimalTilingFeatures;
    const bool blit=(features&VK_FORMAT_FEATURE_BLIT_SRC_BIT)&&(features&VK_FORMAT_FEATURE_BLIT_DST_BIT);

    const bool draw=fmt.depth&&(features&VK_FORMAT_FEATURE_SAMPLED_IMAGE_BIT)&&(features&VK_FORMAT_FEATURE_DEPTH_STENCIL_ATTACHMENT_BIT);
    ScaledCopy mode=blit?ScaledCopy::Blit:draw?ScaledCopy::Draw:ScaledCopy::Unsupported;
    if(fmt.depth&&g_depthCopyOverride==DepthCopyOverride::Draw&&draw)mode=ScaledCopy::Draw;
    if(fmt.depth&&g_depthCopyOverride==DepthCopyOverride::Unsupported)mode=ScaledCopy::Unsupported;
    if(mode!=ScaledCopy::Blit) {
        static std::vector<VkFormat> logged;
        if(std::find(logged.begin(),logged.end(),fmt.pixel)==logged.end()) {
            logged.push_back(fmt.pixel);
            const char* why=blit?" (NSMBU_VK_DEPTH_COPY)":"";
            if(mode==ScaledCopy::Draw)LOG("[gfx] Vulkan: format %d: scaled depth copies are drawn, not blitted%s",int(fmt.pixel),why);
            else LOG("[gfx] Vulkan: format %d can be neither blitted nor drawn%s; scaled copies of it are cleared or skipped",int(fmt.pixel),why);
        }
    }
    return mode;
}
namespace {
struct DepthCopyParams { float scaleX,scaleY;int32_t lastX,lastY;uint32_t bit; };
struct DepthCopyResources {
    VkDevice device=VK_NULL_HANDLE;
    VkDescriptorSetLayout descriptors=VK_NULL_HANDLE;
    VkPipelineLayout layout=VK_NULL_HANDLE;
    VkSampler sampler=VK_NULL_HANDLE;
    std::unordered_map<uint64_t,VkPipeline> pipelines;
} depthCopy;
const char* depthCopyVertex=R"glsl(#version 450
void main() {
    vec2 p=vec2((gl_VertexIndex<<1)&2,gl_VertexIndex&2);
    gl_Position=vec4(p*2.0-1.0,0.0,1.0);
}
)glsl";
const char* depthCopyFragment=R"glsl(#version 450
layout(set=0,binding=0) uniform sampler2D depthSource;
layout(push_constant) uniform Params { vec2 scale; ivec2 last; uint bit; } p;
void main() {
    ivec2 texel=min(ivec2(gl_FragCoord.xy*p.scale),p.last);
    gl_FragDepth=texelFetch(depthSource,texel,0).r;
}
)glsl";
const char* stencilCopyFragment=R"glsl(#version 450
layout(set=0,binding=1) uniform usampler2D stencilSource;
layout(push_constant) uniform Params { vec2 scale; ivec2 last; uint bit; } p;
void main() {
    ivec2 texel=min(ivec2(gl_FragCoord.xy*p.scale),p.last);
    if((texelFetch(stencilSource,texel,0).r&(1u<<p.bit))==0u)discard;
}
)glsl";
VkShaderModule depth_copy_module(const char* source,bool vertex) {
    std::string error;auto words=vk::compile_glsl(source,vertex,&error);
    if(words.empty()||!error.empty())throw std::runtime_error("Vulkan depth copy shader: "+error);
    VkShaderModuleCreateInfo ci{VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO};ci.codeSize=words.size()*4;ci.pCode=words.data();
    VkShaderModule module=VK_NULL_HANDLE;check_vk(vkCreateShaderModule(R.device,&ci,nullptr,&module),"create depth copy shader");return module;
}
void depth_copy_resources() {
    if(depthCopy.device&&depthCopy.device!=R.device)depthCopy={};
    depthCopy.device=R.device;
    if(!depthCopy.descriptors) {
        VkDescriptorSetLayoutBinding bindings[2]{{0,VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,1,VK_SHADER_STAGE_FRAGMENT_BIT,nullptr},
                                                 {1,VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,1,VK_SHADER_STAGE_FRAGMENT_BIT,nullptr}};
        VkDescriptorSetLayoutCreateInfo ci{VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO};ci.bindingCount=2;ci.pBindings=bindings;
        check_vk(vkCreateDescriptorSetLayout(R.device,&ci,nullptr,&depthCopy.descriptors),"create depth copy descriptors");
    }
    if(!depthCopy.layout) {
        VkPushConstantRange push{VK_SHADER_STAGE_FRAGMENT_BIT,0,sizeof(DepthCopyParams)};
        VkPipelineLayoutCreateInfo ci{VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO};ci.setLayoutCount=1;ci.pSetLayouts=&depthCopy.descriptors;
        ci.pushConstantRangeCount=1;ci.pPushConstantRanges=&push;
        check_vk(vkCreatePipelineLayout(R.device,&ci,nullptr,&depthCopy.layout),"create depth copy layout");
    }
    if(!depthCopy.sampler) {
        VkSamplerCreateInfo ci{VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO};ci.minFilter=ci.magFilter=VK_FILTER_NEAREST;ci.mipmapMode=VK_SAMPLER_MIPMAP_MODE_NEAREST;
        ci.addressModeU=ci.addressModeV=ci.addressModeW=VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
        check_vk(vkCreateSampler(R.device,&ci,nullptr,&depthCopy.sampler),"create depth copy sampler");
    }
}

VkPipeline depth_copy_pipeline(const FormatInfo& fmt,bool stencilPass) {
    depth_copy_resources();
    const uint64_t key=uint64_t(fmt.pixel)<<1|uint64_t(stencilPass);
    if(auto it=depthCopy.pipelines.find(key);it!=depthCopy.pipelines.end())return it->second;
    VkShaderModule vs=VK_NULL_HANDLE,fs=VK_NULL_HANDLE;VkPipeline result=VK_NULL_HANDLE;
    try {
        vs=depth_copy_module(depthCopyVertex,true);fs=depth_copy_module(stencilPass?stencilCopyFragment:depthCopyFragment,false);
        VkPipelineShaderStageCreateInfo stages[2]{};
        for(int i=0;i<2;++i){stages[i].sType=VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;stages[i].stage=i?VK_SHADER_STAGE_FRAGMENT_BIT:VK_SHADER_STAGE_VERTEX_BIT;stages[i].module=i?fs:vs;stages[i].pName="main";}
        VkPipelineVertexInputStateCreateInfo vi{VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO};
        VkPipelineInputAssemblyStateCreateInfo ia{VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO};ia.topology=VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
        VkPipelineViewportStateCreateInfo vp{VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO};vp.viewportCount=vp.scissorCount=1;
        VkPipelineRasterizationStateCreateInfo rs{VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO};rs.polygonMode=VK_POLYGON_MODE_FILL;rs.cullMode=VK_CULL_MODE_NONE;rs.lineWidth=1;
        VkPipelineMultisampleStateCreateInfo ms{VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO};ms.rasterizationSamples=VK_SAMPLE_COUNT_1_BIT;
        VkPipelineDepthStencilStateCreateInfo dss{VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO};
        dss.depthTestEnable=dss.depthWriteEnable=stencilPass?VK_FALSE:VK_TRUE;dss.depthCompareOp=VK_COMPARE_OP_ALWAYS;
        dss.stencilTestEnable=fmt.stencil?VK_TRUE:VK_FALSE;

        dss.front={VK_STENCIL_OP_REPLACE,VK_STENCIL_OP_REPLACE,VK_STENCIL_OP_REPLACE,VK_COMPARE_OP_ALWAYS,0xFF,0xFF,0};dss.back=dss.front;
        VkPipelineColorBlendStateCreateInfo bs{VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO};
        VkDynamicState states[]={VK_DYNAMIC_STATE_VIEWPORT,VK_DYNAMIC_STATE_SCISSOR,VK_DYNAMIC_STATE_STENCIL_WRITE_MASK,VK_DYNAMIC_STATE_STENCIL_REFERENCE};
        VkPipelineDynamicStateCreateInfo ds{VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO};ds.dynamicStateCount=fmt.stencil?4:2;ds.pDynamicStates=states;
        VkPipelineRenderingCreateInfo rendering{VK_STRUCTURE_TYPE_PIPELINE_RENDERING_CREATE_INFO};
        rendering.depthAttachmentFormat=fmt.pixel;rendering.stencilAttachmentFormat=fmt.stencil?fmt.pixel:VK_FORMAT_UNDEFINED;
        VkGraphicsPipelineCreateInfo ci{VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO};ci.pNext=&rendering;ci.stageCount=2;ci.pStages=stages;
        ci.pVertexInputState=&vi;ci.pInputAssemblyState=&ia;ci.pViewportState=&vp;ci.pRasterizationState=&rs;ci.pMultisampleState=&ms;
        ci.pDepthStencilState=&dss;ci.pColorBlendState=&bs;ci.pDynamicState=&ds;ci.layout=depthCopy.layout;
        check_vk(vkCreateGraphicsPipelines(R.device,R.pipelineCache,1,&ci,nullptr,&result),"create depth copy pipeline");
        depthCopy.pipelines.emplace(key,result);++R.pipelineCreates;R.pipelineCacheDirty=true;R.pipelineCacheChangedFrame=R.frame;
    } catch(...) {
        if(result)vkDestroyPipeline(R.device,result,nullptr);
        if(vs)vkDestroyShaderModule(R.device,vs,nullptr);if(fs)vkDestroyShaderModule(R.device,fs,nullptr);
        throw;
    }
    vkDestroyShaderModule(R.device,vs,nullptr);vkDestroyShaderModule(R.device,fs,nullptr);
    return result;
}

VkImageView depth_copy_source_view(Surface* s,VkImageAspectFlags aspect,uint32_t level,uint32_t layer) {
    const uint32_t key=0x80000000u|(aspect==VK_IMAGE_ASPECT_STENCIL_BIT?0x40000000u:0)|(level<<20)|layer;
    if(auto it=s->sampledViews.find(key);it!=s->sampledViews.end())return it->second;
    VkImageViewCreateInfo info{VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO};info.image=s->image;info.viewType=VK_IMAGE_VIEW_TYPE_2D;info.format=s->fmt.pixel;
    info.subresourceRange={aspect,level,1,layer,1};
    VkImageView view=VK_NULL_HANDLE;check_vk(vkCreateImageView(R.device,&info,nullptr,&view),"create depth copy source view");
    s->sampledViews.emplace(key,view);return view;
}
}

static void draw_depth_copy(Surface* src,uint32_t srcLevel,uint32_t srcLayer,uint32_t srcW,uint32_t srcH,
                            Surface* dst,uint32_t dstLayer,uint32_t dstW,uint32_t dstH,uint32_t layers) {
    if(src->image==dst->image)throw std::runtime_error("Vulkan depth copy source and destination alias");
    if(src->fmt.pixel!=dst->fmt.pixel||!src->fmt.depth||src->imageType!=VK_IMAGE_TYPE_2D||dst->imageType!=VK_IMAGE_TYPE_2D)
        throw std::runtime_error("Vulkan depth copy requires 2D depth surfaces of one format");
    if(!(src->usage&VK_IMAGE_USAGE_SAMPLED_BIT)||!(dst->usage&VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT))
        throw std::runtime_error("Vulkan depth copy requires a sampled source and a depth attachment destination");
    if(srcLevel>=src->mips||srcLayer+layers>src->arrayLayers||dstLayer+layers>dst->arrayLayers||!srcW||!srcH||!dstW||!dstH||
       dstW>dst->extent.width||dstH>dst->extent.height)
        throw std::runtime_error("Invalid Vulkan depth copy region");
    const FormatInfo& fmt=src->fmt;
    VkPipeline depthPass=depth_copy_pipeline(fmt,false),bitPass=fmt.stencil?depth_copy_pipeline(fmt,true):VK_NULL_HANDLE;
    end_encoder();
    transition_image(src,VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT,VK_ACCESS_SHADER_READ_BIT);
    transition_image(dst,VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL,
                     VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT|VK_PIPELINE_STAGE_LATE_FRAGMENT_TESTS_BIT,
                     VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_READ_BIT|VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT);
    auto cmd=command_buffer();
    DepthCopyParams params{float(srcW)/float(dstW),float(srcH)/float(dstH),int32_t(srcW)-1,int32_t(srcH)-1,0};
    VkViewport viewport{0,0,float(dstW),float(dstH),0,1};VkRect2D area{{0,0},{dstW,dstH}};
    for(uint32_t i=0;i<layers;++i) {
        VkDescriptorSet set=VK_NULL_HANDLE;
        VkDescriptorSetAllocateInfo allocation{VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO};allocation.descriptorPool=R.descriptorPool;
        allocation.descriptorSetCount=1;allocation.pSetLayouts=&depthCopy.descriptors;
        check_vk(vkAllocateDescriptorSets(R.device,&allocation,&set),"allocate depth copy descriptors");
        VkDescriptorImageInfo images[2]{{depthCopy.sampler,depth_copy_source_view(src,VK_IMAGE_ASPECT_DEPTH_BIT,srcLevel,srcLayer+i),VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL},
                                        {depthCopy.sampler,VK_NULL_HANDLE,VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL}};
        VkWriteDescriptorSet writes[2]{{VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET},{VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET}};
        for(uint32_t b=0;b<2;++b){writes[b].dstSet=set;writes[b].dstBinding=b;writes[b].descriptorCount=1;writes[b].descriptorType=VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;writes[b].pImageInfo=&images[b];}
        if(fmt.stencil)images[1].imageView=depth_copy_source_view(src,VK_IMAGE_ASPECT_STENCIL_BIT,srcLevel,srcLayer+i);
        vkUpdateDescriptorSets(R.device,fmt.stencil?2:1,writes,0,nullptr);
        VkRenderingAttachmentInfo attachment{VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO};
        attachment.imageView=layer_view(dst,dstLayer+i);attachment.imageLayout=VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;

        attachment.loadOp=VK_ATTACHMENT_LOAD_OP_DONT_CARE;attachment.storeOp=VK_ATTACHMENT_STORE_OP_STORE;
        VkRenderingInfo rendering{VK_STRUCTURE_TYPE_RENDERING_INFO};rendering.renderArea=area;rendering.layerCount=1;
        rendering.pDepthAttachment=&attachment;rendering.pStencilAttachment=fmt.stencil?&attachment:nullptr;
        vkCmdBeginRendering(cmd,&rendering);++R.renderPassCount;
        vkCmdSetViewport(cmd,0,1,&viewport);vkCmdSetScissor(cmd,0,1,&area);
        vkCmdBindDescriptorSets(cmd,VK_PIPELINE_BIND_POINT_GRAPHICS,depthCopy.layout,0,1,&set,0,nullptr);
        vkCmdBindPipeline(cmd,VK_PIPELINE_BIND_POINT_GRAPHICS,depthPass);
        if(fmt.stencil) {
            vkCmdSetStencilReference(cmd,VK_STENCIL_FACE_FRONT_AND_BACK,0);
            vkCmdSetStencilWriteMask(cmd,VK_STENCIL_FACE_FRONT_AND_BACK,0xFF);
        }
        params.bit=0;vkCmdPushConstants(cmd,depthCopy.layout,VK_SHADER_STAGE_FRAGMENT_BIT,0,sizeof(params),&params);
        vkCmdDraw(cmd,3,1,0,0);
        if(fmt.stencil) {
            vkCmdBindPipeline(cmd,VK_PIPELINE_BIND_POINT_GRAPHICS,bitPass);
            vkCmdSetStencilReference(cmd,VK_STENCIL_FACE_FRONT_AND_BACK,0xFF);
            for(uint32_t bit=0;bit<8;++bit) {
                vkCmdSetStencilWriteMask(cmd,VK_STENCIL_FACE_FRONT_AND_BACK,1u<<bit);
                params.bit=bit;vkCmdPushConstants(cmd,depthCopy.layout,VK_SHADER_STAGE_FRAGMENT_BIT,0,sizeof(params),&params);
                vkCmdDraw(cmd,3,1,0,0);
            }
        }
        vkCmdEndRendering(cmd);
    }
    transition_image(dst,VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
}

static void clear_unscalable(Surface* dst,uint32_t dstW,uint32_t dstH) {
    if(dstW!=dst->extent.width||dstH!=dst->extent.height)return;
    end_encoder();
    transition_image(dst,VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,VK_PIPELINE_STAGE_TRANSFER_BIT,VK_ACCESS_TRANSFER_WRITE_BIT);
    VkImageSubresourceRange range{dst->aspect,0,dst->mips,0,dst->arrayLayers};
    if(dst->fmt.depth) {
        VkClearDepthStencilValue value{1.0f,0};
        vkCmdClearDepthStencilImage(command_buffer(),dst->image,dst->layout,&value,1,&range);
    } else {
        VkClearColorValue value{};
        vkCmdClearColorImage(command_buffer(),dst->image,dst->layout,&value,1,&range);
    }
    transition_image(dst,VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
}
void resample(Surface* src,Surface* dst,uint32_t slices,float uMax,float vMax,uint32_t dstW,uint32_t dstH) {
    if(!src||!dst||!src->image||!dst->image)throw std::runtime_error("Vulkan resample requires allocated surfaces");
    if(src->image==dst->image)throw std::runtime_error("Vulkan resample source and destination alias");
    if(src->fmt.pixel!=dst->fmt.pixel||src->imageType!=dst->imageType||src->fmt.compressed)
        throw std::runtime_error("Vulkan resample requires matching uncompressed surface formats and dimensions");
    if(slices>src->arrayLayers||slices>dst->arrayLayers)throw std::runtime_error("Vulkan resample layer range exceeds surface");
    if(!dstW)dstW=dst->extent.width;if(!dstH)dstH=dst->extent.height;
    if(dstW>dst->extent.width||dstH>dst->extent.height||!(uMax>0&&uMax<=1&&vMax>0&&vMax<=1))
        throw std::runtime_error("Invalid Vulkan resample extent");
    const uint32_t srcW=std::max(1u,uint32_t(std::lround(src->extent.width*uMax))),srcH=std::max(1u,uint32_t(std::lround(src->extent.height*vMax)));
    const ScaledCopy mode=scaled_copy_mode(src->fmt);

    const bool drawable=mode==ScaledCopy::Draw&&src->imageType==VK_IMAGE_TYPE_2D&&(src->usage&VK_IMAGE_USAGE_SAMPLED_BIT)&&
                        (dst->usage&VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT);
    const bool sameSize=srcW==dstW&&srcH==dstH&&src->extent.depth==dst->extent.depth;
    if(drawable&&!sameSize) {
        draw_depth_copy(src,0,0,srcW,srcH,dst,0,dstW,dstH,slices);
        return;
    }
    if(mode!=ScaledCopy::Blit&&!sameSize) {
        clear_unscalable(dst,dstW,dstH);
        return;
    }
    end_encoder();transition_image(src,VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,VK_PIPELINE_STAGE_TRANSFER_BIT,VK_ACCESS_TRANSFER_READ_BIT);
    transition_image(dst,VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,VK_PIPELINE_STAGE_TRANSFER_BIT,VK_ACCESS_TRANSFER_WRITE_BIT);
    if(mode!=ScaledCopy::Blit) {

        VkImageCopy region{};region.srcSubresource={src->aspect,0,0,slices};region.dstSubresource=region.srcSubresource;
        region.extent={dstW,dstH,dst->extent.depth};
        vkCmdCopyImage(command_buffer(),src->image,src->layout,dst->image,dst->layout,1,&region);
    } else {
        VkFormatProperties properties{};vkGetPhysicalDeviceFormatProperties(R.physicalDevice,src->fmt.pixel,&properties);
        const auto features=properties.optimalTilingFeatures;
        VkImageBlit region{};region.srcSubresource={VkImageAspectFlags(src->fmt.depth?VK_IMAGE_ASPECT_DEPTH_BIT:VK_IMAGE_ASPECT_COLOR_BIT),0,0,slices};
        region.dstSubresource=region.srcSubresource;
        region.srcOffsets[1]={int32_t(srcW),int32_t(srcH),int32_t(src->extent.depth)};
        region.dstOffsets[1]={int32_t(dstW),int32_t(dstH),int32_t(dst->extent.depth)};
        VkFilter filter=!src->fmt.depth&&src->fmt.kind==FormatInfo::FLOAT&&(features&VK_FORMAT_FEATURE_SAMPLED_IMAGE_FILTER_LINEAR_BIT)?VK_FILTER_LINEAR:VK_FILTER_NEAREST;
        vkCmdBlitImage(command_buffer(),src->image,src->layout,dst->image,dst->layout,1,&region,filter);
        if(src->fmt.stencil) {
            region.srcSubresource.aspectMask=region.dstSubresource.aspectMask=VK_IMAGE_ASPECT_STENCIL_BIT;
            vkCmdBlitImage(command_buffer(),src->image,src->layout,dst->image,dst->layout,1,&region,VK_FILTER_NEAREST);
        }
    }
    transition_image(src,VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
    transition_image(dst,VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
}
static Surface* rescale(Surface* s) {
    float wanted=target_scale(s),ax,ay;
    target_aspect(s,ax,ay);
    auto attachment=s->fmt.depth?VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT:VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT;
    if(s->imageType!=VK_IMAGE_TYPE_2D||s->viewType==VK_IMAGE_VIEW_TYPE_CUBE||s->viewType==VK_IMAGE_VIEW_TYPE_CUBE_ARRAY) {

        if(s->usage&attachment)return s;
        throw std::runtime_error("Vulkan GX2 surface cannot be a render target");
    }
    if(s->scale==wanted&&s->ax==ax&&s->ay==ay&&(s->usage&attachment))return s;
    Surface replacement;
    replacement.width=s->width;replacement.height=s->height;replacement.slices=s->slices;replacement.mips=s->mips;
    replacement.dim=s->dim;replacement.format=s->format;replacement.isDepth=s->isDepth;replacement.fmt=s->fmt;
    create_surface_image(&replacement,true);
    try { resample(s,&replacement,s->arrayLayers,1,1,0,0); }
    catch(...) { destroy_surface_image(&replacement);throw; }
    destroy_surface_image(s);
    s->image=replacement.image;s->memory=replacement.memory;s->view=replacement.view;
    s->extent=replacement.extent;s->layout=replacement.layout;s->use=replacement.use;s->usage=replacement.usage;s->createFlags=replacement.createFlags;s->scale=replacement.scale;s->ax=replacement.ax;s->ay=replacement.ay;s->sx=replacement.sx;s->sy=replacement.sy;
    forget_texture_views();return s;
}

static Surface* adopt_newer_alias(Surface* s) {
    if(!s||!s->formatViews||!s->image)return s;
    Surface* newest=nullptr;
    auto range=R.surfaces.equal_range(s->addr);
    for(auto it=range.first;it!=range.second;++it) {
        Surface* o=it->second.get();
        if(o==s||!o->gpuWritten||!o->image||o->isDepth||o->fmt.compressed||o->fmt.convert!=Convert::NONE)continue;
        if(s->gpuWritten&&o->writeSeq<=s->writeSeq)continue;
        if((o->format&0x3F)!=(s->format&0x3F)||o->fmt.hostBytesPerBlock!=s->fmt.hostBytesPerBlock)continue;
        if(o->imageType!=s->imageType||o->arrayLayers!=s->arrayLayers||o->extent.width!=s->extent.width||
           o->extent.height!=s->extent.height||o->extent.depth!=s->extent.depth)continue;
        if(!newest||o->writeSeq>newest->writeSeq)newest=o;
    }
    if(!newest)return s;
    end_encoder();
    transition_image(newest,VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,VK_PIPELINE_STAGE_TRANSFER_BIT,VK_ACCESS_TRANSFER_READ_BIT);
    transition_image(s,VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,VK_PIPELINE_STAGE_TRANSFER_BIT,VK_ACCESS_TRANSFER_WRITE_BIT);
    std::vector<VkImageCopy> regions;
    for(uint32_t mip=0;mip<std::min(s->mips,newest->mips);++mip) {
        VkImageCopy region{};region.srcSubresource={VK_IMAGE_ASPECT_COLOR_BIT,mip,0,s->arrayLayers};region.dstSubresource=region.srcSubresource;
        region.extent={std::max(1u,s->extent.width>>mip),std::max(1u,s->extent.height>>mip),std::max(1u,s->extent.depth>>mip)};
        regions.push_back(region);
    }
    vkCmdCopyImage(command_buffer(),newest->image,newest->layout,s->image,s->layout,uint32_t(regions.size()),regions.data());
    transition_image(newest,VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);transition_image(s,VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
    mark_gpu_written(s);
    return s;
}
static uint32_t g_surface_lookup_epoch = 1;

struct SurfaceLookupMemoSlot {
    uint32_t addr = 0, width = 0, height = 0, slices = 0, format = 0, dim = 0, mips = 0;
    bool isDepth = false, forRendering = false;
    uint32_t epoch = 0;
    Surface* surface = nullptr;
};

static SurfaceLookupMemoSlot g_surface_lookup_memo[64];

void invalidate_surface_lookup() { ++g_surface_lookup_epoch; }

static void store_surface_lookup_memo(const SurfaceDesc& d, bool forRendering, Surface* s) {
    auto& slot = g_surface_lookup_memo[(d.addr >> 8) & 63];
    slot.addr = d.addr;
    slot.width = d.width;
    slot.height = d.height;
    slot.slices = d.slices;
    slot.format = d.format;
    slot.dim = d.dim;
    slot.mips = d.mips;
    slot.isDepth = d.isDepth;
    slot.forRendering = forRendering;
    slot.epoch = g_surface_lookup_epoch;
    slot.surface = s;
}

Surface* find_or_create_surface(const SurfaceDesc& d,bool forRendering) {
    if(!d.addr)return nullptr;
    {
        auto& slot = g_surface_lookup_memo[(d.addr >> 8) & 63];
        if (slot.surface && slot.epoch == g_surface_lookup_epoch && slot.addr == d.addr &&
            slot.width == d.width && slot.height == d.height && slot.slices == d.slices &&
            slot.format == d.format && slot.dim == d.dim && slot.mips == d.mips &&
            slot.isDepth == d.isDepth && slot.forRendering == forRendering) {
            Surface* s = slot.surface;
            if (forRendering) return adopt_newer_alias(rescale(s));
            return s->gpuWritten ? adopt_newer_alias(s) : s;
        }
    }
    auto range=R.surfaces.equal_range(d.addr);
    Surface* exact=nullptr;Surface* rendered=nullptr;
    auto score=[&](Surface* s){return std::make_tuple(s->width==d.width&&s->height==d.height,s->slices==d.slices,s->writeSeq);};

    auto dimType=[](uint32_t dim){auto e=static_cast<Latte::E_DIM>(dim);
        return e==Latte::E_DIM::DIM_1D||e==Latte::E_DIM::DIM_1D_ARRAY?VK_IMAGE_TYPE_1D:e==Latte::E_DIM::DIM_3D?VK_IMAGE_TYPE_3D:VK_IMAGE_TYPE_2D;};
    auto consider=[&](Surface* s){
        if(s->imageType!=dimType(d.dim))return;
        if(!rendered||score(s)>score(rendered))rendered=s;};
    for(auto it=range.first;it!=range.second;++it) {
        auto* s=it->second.get();
        if(!forRendering&&s->isDepth&&!d.isDepth&&s->gpuWritten&&s->width==d.width&&s->height==d.height)consider(s);
        if(s->isDepth!=d.isDepth)continue;

        if(forRendering&&s->mips>1)continue;

        bool sameVolume=(s->imageType==VK_IMAGE_TYPE_3D)==(dimType(d.dim)==VK_IMAGE_TYPE_3D);
        if(s->width==d.width&&s->height==d.height&&s->format==d.format&&s->slices==d.slices&&sameVolume&&
           (forRendering||s->mips>=d.mips||s->gpuWritten)) {
            if(forRendering) {
                if(std::distance(range.first,range.second)==1)store_surface_lookup_memo(d,forRendering,s);
                return adopt_newer_alias(rescale(s));
            }
            if(!exact||s->writeSeq>exact->writeSeq)exact=s;
        } else if(!forRendering&&s->gpuWritten&&(s->format&0x3f)==(d.format&0x3f))consider(s);
    }
    if(exact&&(exact->gpuWritten||!rendered||exact->writeSeq>rendered->writeSeq)) {
        if(std::distance(range.first,range.second)==1 && exact->mips>=d.mips)
            store_surface_lookup_memo(d,forRendering,exact);
        return exact->gpuWritten?adopt_newer_alias(exact):exact;
    }
    if(rendered)return rendered;if(exact)return exact;
    auto s=std::make_unique<Surface>();
    s->addr=d.addr;s->mipAddr=d.mipAddr;s->width=std::max(d.width,1u);s->height=std::max(d.height,1u);
    s->slices=std::max(d.slices,1u);s->pitch=d.pitch;s->mips=forRendering?1:std::max(d.mips,1u);
    s->format=d.format;s->dim=d.dim;s->tileMode=d.tileMode;s->swizzle=d.swizzle;s->isDepth=d.isDepth;
    s->fmt=format_info(d.format,d.isDepth);create_surface_image(s.get(),forRendering);
    ++g_surface_lookup_epoch;
    auto* raw=s.get();R.surfaces.emplace(d.addr,std::move(s));
    {
        auto post = R.surfaces.equal_range(d.addr);
        if(std::distance(post.first,post.second)==1)store_surface_lookup_memo(d,forRendering,raw);
    }

    if(!raw->isDepth&&!raw->fmt.compressed&&raw->fmt.convert==Convert::NONE) {
        auto views=R.surfaces.equal_range(d.addr);
        for(auto it=views.first;it!=views.second;++it) {
            Surface* o=it->second.get();
            if(o!=raw&&!o->isDepth&&!o->fmt.compressed&&o->fmt.convert==Convert::NONE&&o->format!=raw->format&&(o->format&0x3F)==(raw->format&0x3F)&&
               o->fmt.hostBytesPerBlock==raw->fmt.hostBytesPerBlock)
                o->formatViews=raw->formatViews=true;
        }
    }
    if(!raw->isDepth&&static_cast<Latte::E_HWTILEMODE>(raw->tileMode)==Latte::E_HWTILEMODE::TM_LINEAR_ALIGNED)R.linearTargets.push_back(raw);
    return forRendering?adopt_newer_alias(raw):raw;
}
static uint32_t mip_base(Surface* s,uint32_t level) {
    if(!level)return s->addr;
    if(!s->mipAddr)throw std::runtime_error("GX2 texture mip chain address is missing");
    if(level==1)return s->mipAddr;
    auto& cached = guest_level(s, level);
    if (cached.addressValid) return cached.address;
    uint32_t address=0,size=0;sint32 sub=0;
    LatteAddrLib::CalculateMipAndSliceAddr(s->addr,s->mipAddr,static_cast<Latte::E_GX2SURFFMT>(s->format),s->width,s->height,s->slices,
        static_cast<Latte::E_DIM>(s->dim),static_cast<Latte::E_HWTILEMODE>(s->tileMode),s->swizzle,0,level,0,&address,&size,&sub);
    cached.address=address;cached.addressValid=true;
    return cached.address;
}
constexpr uint32_t kThin1PageCap = 96;
constexpr uint32_t kThin1Overflow = 0xffffffffu;

struct Thin1Level {
    uint32_t base = 0, surfSize = 0, bw = 0, bh = 0, bytes = 0;
    uint32_t swizzle = 0, pitch = 0, guestHeight = 0, bpp = 0;
    uint32_t pageShift = 0, firstPage = 0, pageCount = 0;
    std::vector<uint8_t> linear;
    std::vector<uint64_t> pageHash;
    std::vector<uint32_t> tileBegin, tiles;
    bool valid = false;
    void drop() {
        valid = false;
        linear.clear();
        linear.shrink_to_fit();
        pageHash.clear();
        tileBegin.clear();
        tiles.clear();
    }
};

static std::unordered_map<Surface*, std::array<Thin1Level, 16>> g_thin1;
static void thin1_forget(Surface* s) { g_thin1.erase(s); }

static Thin1Level* thin1_find(Surface* s, uint32_t level) {
    auto it = g_thin1.find(s);
    if (it == g_thin1.end() || level >= it->second.size()) return nullptr;
    return &it->second[level];
}

static bool thin1_shape(Surface* s, uint32_t level, uint32_t size, uint32_t& bw, uint32_t& bh) {
    if (!wwatch::active() || s->isDepth || s->bcDecoded || s->fmt.stencil) return false;
    if (s->imageType != VK_IMAGE_TYPE_2D || s->slices != 1 || s->mips > 16 || level >= 16) return false;
    if (s->extent.width != s->width || s->extent.height != s->height) return false;
    const FormatInfo& f = s->fmt;
    if (f.convert != Convert::NONE || f.compressed || f.hostBytesPerBlock != f.bytesPerBlock) return false;
    if (f.bytesPerBlock != 1 && f.bytesPerBlock != 2 && f.bytesPerBlock != 4 && f.bytesPerBlock != 8) return false;
    const auto& info = guest_info(s, level);
    if ((Latte::E_HWTILEMODE)info.hwTileMode != Latte::E_HWTILEMODE::TM_2D_TILED_THIN1) return false;
    bw = std::max(s->width >> level, 1u);
    bh = std::max(s->height >> level, 1u);
    if ((bw & 7) || (bh & 7)) return false;
    size_t pixels = (size_t)bw * bh * f.bytesPerBlock;
    if (pixels < 256 * 1024 || pixels > 8u * 1024 * 1024 || size < 256 * 1024) return false;
    if ((bw / 8) * (bh / 8) > 65536) return false;
    return true;
}

static void thin1_elem_span(const uint16_t* pix, uint32_t bytes, uint32_t& lo, uint32_t& hi) {
    lo = ~0u;
    hi = 0;
    for (uint32_t i = 0; i < 64; i++) {
        uint32_t elem = (uint32_t)pix[i] * bytes;
        if (bytes > 4) elem = (elem & 0xFF) | ((elem & ~0xFFu) << 3);
        lo = std::min(lo, elem);
        hi = std::max(hi, elem + bytes);
    }
}

static uint64_t thin1_page_hash(const Thin1Level& lv, uint32_t base, uint32_t size, uint32_t pi) {
    uint64_t pageBytes = 1ull << lv.pageShift;
    uint64_t start = ((uint64_t)lv.firstPage + pi) << lv.pageShift;
    uint64_t lo = std::max(start, (uint64_t)base);
    uint64_t hi = std::min(start + pageBytes, (uint64_t)base + size);
    if (hi <= lo) return 0;
    return XXH3_64bits(mem::ptr((uint32_t)lo), (size_t)(hi - lo));
}

enum class Thin1Use { Full, Unchanged, Partial };

struct Thin1Choice {
    Thin1Use use = Thin1Use::Full;
    bool store = false;
    uint32_t pageCount = 0;
    uint32_t pages[kThin1PageCap]{};
};

static bool thin1_matches(const Thin1Level& lv, Surface* s, uint32_t level, uint32_t base, uint32_t size, uint32_t bw, uint32_t bh) {
    if (!lv.valid || lv.base != base || lv.surfSize != size || lv.bw != bw || lv.bh != bh) return false;
    if (lv.swizzle != s->swizzle || lv.bytes != s->fmt.bytesPerBlock) return false;
    const auto& info = guest_info(s, level);
    uint32_t pitch = level == 0 && s->pitch ? s->pitch : info.pitch;
    return lv.pitch == pitch && lv.guestHeight == info.height && lv.bpp == info.bpp;
}

static Thin1Choice thin1_choose(Surface* s, uint32_t level, uint32_t base, uint32_t size, uint64_t priorStamp) {
    Thin1Choice choice;
    uint32_t bw = 0, bh = 0;
    if (!thin1_shape(s, level, size, bw, bh)) {
        if (Thin1Level* lv = thin1_find(s, level)) lv->drop();
        return choice;
    }
    Thin1Level& lv = g_thin1[s][level];
    const uint32_t hashBit = 1u << level;
    auto collect = [&](uint32_t* out) {
        return wwatch::collect_written_pages(base, size, priorStamp, out, kThin1PageCap);
    };
    if (lv.valid && thin1_matches(lv, s, level, base, size, bw, bh)) {
        uint32_t n = collect(choice.pages);
        if (n == kThin1Overflow) {
            lv.drop();
            return choice;
        }
        uint32_t kept = 0;
        for (uint32_t i = 0; i < n; i++) {
            uint32_t pi = choice.pages[i];
            if (pi >= lv.pageCount) continue;
            if (thin1_page_hash(lv, base, size, pi) != lv.pageHash[pi]) choice.pages[kept++] = pi;
        }
        if (!kept) {
            if (s->dirty && s->dirtyOob) return choice;
            choice.use = Thin1Use::Unchanged;
            return choice;
        }
        choice.use = Thin1Use::Partial;
        choice.pageCount = kept;
        return choice;
    }
    uint32_t n = collect(choice.pages);
    if (n == kThin1Overflow) {
        lv.drop();
        return choice;
    }
    if (n == 0 && !s->dirtyOob && (s->levelHashValid & hashBit)) {
        choice.use = Thin1Use::Unchanged;
        return choice;
    }
    if (n > 0) choice.store = true;
    return choice;
}

static bool thin1_build(Surface* s, uint32_t level, uint32_t base, uint32_t size, const uint8_t* linear, size_t linearBytes) {
    uint32_t bw = 0, bh = 0;
    if (!thin1_shape(s, level, size, bw, bh)) return false;
    const uint32_t bytes = s->fmt.bytesPerBlock;
    if (linearBytes != (size_t)bw * bh * bytes) return false;
    uint32_t shift = 0, first = 0, count = 0;
    if (!wwatch::page_span(base, size, shift, first, count) || !count || count > 8192) return false;
    const auto& info = guest_info(s, level);
    uint32_t pitch = level == 0 && s->pitch ? s->pitch : info.pitch;
    uint32_t height = info.height, bpp = info.bpp;
    uint32_t pipeSwizzle = (s->swizzle >> 8) & 1, bankSwizzle = (s->swizzle >> 9) & 3;
    LatteAddrLib::CachedSurfaceAddrInfo ci;
    LatteAddrLib::SetupCachedSurfaceAddrInfo(&ci, 0, 0, bpp, pitch, height, 1, 1, Latte::E_HWTILEMODE::TM_2D_TILED_THIN1, false, pipeSwizzle, bankSwizzle);
    uint32_t minE = 0, maxE = 0;
    thin1_elem_span(ci.microTilePixelIndexTable, bytes, minE, maxE);
    if (maxE <= minE) return false;
    uint32_t tilesX = bw / 8, tilesY = bh / 8;
    std::vector<std::vector<uint32_t>> buckets(count);
    for (uint32_t ty = 0; ty < tilesY; ty++) {
        for (uint32_t tx = 0; tx < tilesX; tx++) {
            uint32_t off = LatteAddrLib::ComputeSurfaceAddrFromCoordMacroTiledCached(tx * 8, ty * 8, &ci);
            uint64_t loAddr = (uint64_t)base + off + minE;
            uint64_t hiAddr = (uint64_t)base + off + maxE;
            if (hiAddr <= loAddr) return false;
            uint32_t pageLo = (uint32_t)(loAddr >> shift);
            uint32_t pageHi = (uint32_t)((hiAddr - 1) >> shift);
            if (pageLo < first || pageHi < pageLo) return false;
            uint32_t id = ty * tilesX + tx;
            for (uint32_t p = pageLo; p <= pageHi; p++) {
                uint32_t rel = p - first;
                if (rel >= count) return false;
                buckets[rel].push_back(id);
            }
        }
    }
    Thin1Level& lv = g_thin1[s][level];
    lv.drop();
    lv.base = base;
    lv.surfSize = size;
    lv.bw = bw;
    lv.bh = bh;
    lv.bytes = bytes;
    lv.swizzle = s->swizzle;
    lv.pitch = pitch;
    lv.guestHeight = height;
    lv.bpp = bpp;
    lv.pageShift = shift;
    lv.firstPage = first;
    lv.pageCount = count;
    lv.linear.assign(linear, linear + linearBytes);
    lv.pageHash.resize(count);
    for (uint32_t i = 0; i < count; i++) lv.pageHash[i] = thin1_page_hash(lv, base, size, i);
    lv.tileBegin.resize(count + 1);
    uint32_t n = 0;
    for (uint32_t i = 0; i < count; i++) {
        lv.tileBegin[i] = n;
        n += (uint32_t)buckets[i].size();
    }
    lv.tileBegin[count] = n;
    lv.tiles.resize(n);
    for (uint32_t i = 0; i < count; i++)
        if (!buckets[i].empty()) memcpy(lv.tiles.data() + lv.tileBegin[i], buckets[i].data(), buckets[i].size() * sizeof(uint32_t));
    lv.valid = true;
    return true;
}

template<uint32_t B>
static void thin1_patch_marked(Thin1Level& lv, const uint8_t* src, const uint16_t* sliceRows, const bool* packedRow,
                               LatteAddrLib::CachedSurfaceAddrInfo* ci, const std::vector<uint8_t>& mark, uint32_t tilesX, uint32_t tilesY) {
    for (uint32_t ty = 0; ty < tilesY; ty++)
        for (uint32_t tx = 0; tx < tilesX; tx++)
            if (mark[ty * tilesX + tx])
                copy_thin1_tile<B>(lv.linear.data(), src, sliceRows, packedRow, ci, lv.bw, tx * 8, ty * 8);
}

static size_t thin1_patch(Surface* s, uint32_t level, uint32_t base, const Thin1Choice& choice, Buffer& staging, std::vector<VkBufferImageCopy>& copies) {
    Thin1Level* found = thin1_find(s, level);
    if (!found || !found->valid) throw std::runtime_error("partial texture upload is missing its tile cache");
    Thin1Level& lv = *found;
    uint32_t tilesX = lv.bw / 8, tilesY = lv.bh / 8;
    std::vector<uint8_t> mark((size_t)tilesX * tilesY);
    for (uint32_t i = 0; i < choice.pageCount; i++) {
        uint32_t p = choice.pages[i];
        if (p >= lv.pageCount) continue;
        for (uint32_t t = lv.tileBegin[p]; t < lv.tileBegin[p + 1]; t++) mark[lv.tiles[t]] = 1;
    }
    uint32_t pipeSwizzle = (s->swizzle >> 8) & 1, bankSwizzle = (s->swizzle >> 9) & 3;
    LatteAddrLib::CachedSurfaceAddrInfo ci;
    LatteAddrLib::SetupCachedSurfaceAddrInfo(&ci, 0, 0, lv.bpp, lv.pitch, lv.guestHeight, 1, 1, Latte::E_HWTILEMODE::TM_2D_TILED_THIN1, false, pipeSwizzle, bankSwizzle);
    const uint16_t* sliceRows = ci.microTilePixelIndexTable;
    bool packedRow[8];
    for (uint32_t ry = 0; ry < 8; ry++) packedRow[ry] = thin1_row_packed_n(sliceRows + (ry << 3), lv.bytes);
    const uint8_t* src = mem::ptr(base);
    if (lv.bytes == 8) thin1_patch_marked<8>(lv, src, sliceRows, packedRow, &ci, mark, tilesX, tilesY);
    else if (lv.bytes == 4) thin1_patch_marked<4>(lv, src, sliceRows, packedRow, &ci, mark, tilesX, tilesY);
    else if (lv.bytes == 2) thin1_patch_marked<2>(lv, src, sliceRows, packedRow, &ci, mark, tilesX, tilesY);
    else thin1_patch_marked<1>(lv, src, sliceRows, packedRow, &ci, mark, tilesX, tilesY);
    if (decode_check()) {
        std::vector<uint8_t> full;
        uint32_t w = 0, h = 0, slices = 0;
        decode_level(s, level, base, full, w, h, slices);
        if (full != lv.linear) throw std::runtime_error("partial texture decode differs from the full decoder");
    }
    size_t nbytes = 0;
    for (uint32_t ty = 0; ty < tilesY; ty++) {
        uint32_t tx = 0;
        while (tx < tilesX) {
            if (!mark[ty * tilesX + tx]) { tx++; continue; }
            uint32_t start = tx;
            while (tx < tilesX && mark[ty * tilesX + tx]) tx++;
            nbytes += (size_t)(tx - start) * 8 * 8 * lv.bytes;
        }
    }
    for (uint32_t i = 0; i < choice.pageCount; i++) {
        uint32_t p = choice.pages[i];
        if (p < lv.pageCount) lv.pageHash[p] = thin1_page_hash(lv, base, lv.surfSize, p);
    }
    if (!nbytes) return 0;
    staging = create_buffer(nbytes, VK_BUFFER_USAGE_TRANSFER_SRC_BIT, VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);
    if (!staging.mapped) {
        defer_buffer(staging);
        throw std::runtime_error("Vulkan texture staging allocation is not mapped");
    }
    uint8_t* dst = (uint8_t*)staging.mapped;
    size_t offset = 0;
    for (uint32_t ty = 0; ty < tilesY; ty++) {
        uint32_t tx = 0;
        while (tx < tilesX) {
            if (!mark[ty * tilesX + tx]) { tx++; continue; }
            uint32_t start = tx;
            while (tx < tilesX && mark[ty * tilesX + tx]) tx++;
            uint32_t x = start * 8, y = ty * 8, w = (tx - start) * 8;
            size_t begin = offset;
            for (uint32_t row = 0; row < 8; row++) {
                size_t rowBytes = (size_t)w * lv.bytes;
                memcpy(dst + offset, lv.linear.data() + ((size_t)(y + row) * lv.bw + x) * lv.bytes, rowBytes);
                offset += rowBytes;
            }
            VkBufferImageCopy copy{};
            copy.bufferOffset = begin;
            copy.imageSubresource = {s->aspect, level, 0, 1};
            copy.imageOffset = {(int32_t)x, (int32_t)y, 0};
            copy.imageExtent = {w, 8, 1};
            copies.push_back(copy);
        }
    }
    return nbytes;
}

static bool stage_fast_level(Surface* s, uint32_t level, uint32_t base, Buffer& staging, uint32_t& w, uint32_t& h, size_t& nbytes) {
    const FormatInfo& f = s->fmt;
    if (f.convert != Convert::NONE || s->isDepth || f.compressed || f.stencil || f.hostBytesPerBlock != f.bytesPerBlock || !f.bytesPerBlock || f.bytesPerBlock > 8)
        return false;
    const auto& info = guest_info(s, level);
    auto tm = (Latte::E_HWTILEMODE)info.hwTileMode;
    if (tm != Latte::E_HWTILEMODE::TM_2D_TILED_THIN1 && tm != Latte::E_HWTILEMODE::TM_LINEAR_ALIGNED) return false;
    w = std::max(s->width >> level, 1u);
    h = std::max(s->height >> level, 1u);
    nbytes = (size_t)w * h * f.hostBytesPerBlock;
    if (!nbytes) return false;
    staging = create_buffer(nbytes, VK_BUFFER_USAGE_TRANSFER_SRC_BIT, VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);
    if (!staging.mapped || !decode_level_fast(s, level, base, (uint8_t*)staging.mapped, w, h, 1)) {
        if (staging.buffer) defer_buffer(staging);
        staging = {};
        return false;
    }
    return true;
}

void upload_surface(Surface* s) {
    if(!s||!s->image||s->gpuWritten)return;

    if (s->lastCheckedFrame == R.frame) return;
    s->lastCheckedFrame = R.frame;

    uint32_t levels=s->mips;
    std::array<std::pair<uint32_t,uint32_t>,16> ranges{};
    if(levels>ranges.size())throw std::runtime_error("GX2 texture has too many mip levels");
    for(uint32_t level=0;level<levels;++level) {
        uint32_t base=mip_base(s,level);
        ranges[level]={base,uint32_t(std::min<uint64_t>(guest_info(s,level).surfSize,0x100000000ull-base))};
    }
    s->dataSize=ranges[0].second;
    bool full = s->dirty || !s->watched;
    const uint64_t priorStamp=s->watchStamp;
    if (wwatch::active()) {
        for(uint32_t level=0;level<levels&&!full;++level)full=wwatch::written_since(ranges[level].first,ranges[level].second,priorStamp);
        if (!full) return;

        uint64_t stamp=~0ull;
        for(uint32_t level=0;level<levels;++level)stamp=std::min(stamp,wwatch::arm(ranges[level].first,ranges[level].second));
        s->watchStamp=stamp;
    } else {
        full = full || ((R.frame + (s->addr >> 12)) & 63) == 0;
        uint64_t sparse = sparse_hash(s);
        if (!full && sparse == s->sparseHash) return;
        s->sparseHash = sparse;
    }
    s->watched = true;
    ++g_stat_full_checks;
    uint64_t hashes[16]{};
    uint64_t hash=1469598103934665603ull;
    Thin1Choice choice[16];
    bool anyPartial=false;
    for(uint32_t level=0;level<levels;++level) {
        const uint32_t hashBit=1u<<level;
        if(wwatch::active()) choice[level]=thin1_choose(s,level,ranges[level].first,ranges[level].second,priorStamp);
        if(choice[level].use==Thin1Use::Unchanged&&(s->levelHashValid&hashBit)) hashes[level]=s->levelHash[level];
        else if(choice[level].use==Thin1Use::Partial) { hashes[level]=s->levelHash[level]; anyPartial=true; }
        else {
            bool reuse=!s->dirtyOob&&level<16&&(s->levelHashValid&hashBit)&&wwatch::active()&&
                !wwatch::written_since(ranges[level].first,ranges[level].second,priorStamp);
            hashes[level]=reuse?s->levelHash[level]:content_hash(mem::ptr(ranges[level].first),size_t(ranges[level].second));
        }
        hash=(hash^hashes[level])*1099511628211ull;
    }
    if(!anyPartial&&hash==s->contentHash) {
        if(wwatch::active()) s->dirty=false;
        s->dirtyOob=false;
        return;
    }
    struct Thin1Copy { uint32_t level; Buffer staging; std::vector<VkBufferImageCopy> copies; size_t bytes; };
    std::vector<Thin1Copy> partials;
    bool needGpu=false;
    try {
        for(uint32_t level=0;level<levels;++level) {
            if(choice[level].use!=Thin1Use::Partial) continue;
            Thin1Copy copy;
            copy.level=level;
            copy.bytes=thin1_patch(s,level,ranges[level].first,choice[level],copy.staging,copy.copies);
            if(!copy.bytes) continue;
            needGpu=true;
            partials.push_back(std::move(copy));
        }
    } catch(...) {
        thin1_forget(s);
        s->dirty=true;
        throw;
    }
    for(uint32_t level=0;level<levels&&!needGpu;++level) {
        if(choice[level].use==Thin1Use::Partial) continue;
        const uint32_t hashBit=1u<<level;
        bool hashMatched=!s->dirtyOob&&s->mips<=16&&(s->levelHashValid&hashBit)&&s->levelHash[level]==hashes[level];
        if(!hashMatched) needGpu=true;
    }
    if(!needGpu) {
        if(wwatch::active()) s->dirty=false;
        s->dirtyOob=false;
        return;
    }
    const bool scaleGuest=s->imageType==VK_IMAGE_TYPE_2D&&s->mips==1&&(s->extent.width!=s->width||s->extent.height!=s->height);
    Surface guest;
    Surface* dst=s;
    if(scaleGuest) {
        guest.width=s->width;guest.height=s->height;guest.slices=s->slices;guest.mips=1;
        guest.dim=s->dim;guest.format=s->format;guest.isDepth=s->isDepth;guest.fmt=s->fmt;
        create_surface_image(&guest,false);
        dst=&guest;
    }
    size_t uploaded = 0;
    try {
        end_encoder();transition_image(dst,VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,VK_PIPELINE_STAGE_TRANSFER_BIT,VK_ACCESS_TRANSFER_WRITE_BIT);
        for(auto& part:partials) {
            uploaded+=part.bytes;
            g_stat_upload_bytes+=part.bytes;
            rprof::add_upload(rprof::kUpTexture,part.bytes);
            vkCmdCopyBufferToImage(command_buffer(),part.staging.buffer,dst->image,VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,uint32_t(part.copies.size()),part.copies.data());
            defer_buffer(part.staging);
            part.staging={};
        }
        for(uint32_t level=0;level<s->mips;++level) {
            if(choice[level].use==Thin1Use::Partial) continue;
            const uint32_t hashBit=1u<<level;
            bool hashMatched=!s->dirtyOob&&s->mips<=16&&(s->levelHashValid&hashBit)&&s->levelHash[level]==hashes[level];
            if(hashMatched)continue;
            if(!choice[level].store) if(Thin1Level* lv=thin1_find(s,level)) lv->drop();
            s->levelHash[level]=hashes[level];s->levelHashValid|=hashBit;
            uint32_t base=mip_base(s,level);
            Buffer staged{};
            uint32_t fastW=0,fastH=0;
            size_t fastBytes=0;
            bool simple=!s->bcDecoded&&!dst->fmt.stencil&&dst->imageType!=VK_IMAGE_TYPE_3D&&s->slices==1;
            if(simple&&stage_fast_level(s,level,base,staged,fastW,fastH,fastBytes)) {
                if(choice[level].store&&!thin1_build(s,level,ranges[level].first,ranges[level].second,(const uint8_t*)staged.mapped,fastBytes))
                    if(Thin1Level* lv=thin1_find(s,level)) lv->drop();
                uploaded+=fastBytes;
                g_stat_upload_bytes+=fastBytes;
                rprof::add_upload(rprof::kUpTexture,fastBytes);
                VkBufferImageCopy copy{};copy.imageSubresource={dst->aspect,level,0,1};copy.imageExtent={fastW,fastH,1};
                vkCmdCopyBufferToImage(command_buffer(),staged.buffer,dst->image,VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,1,&copy);
                defer_buffer(staged);
                continue;
            }
            std::vector<uint8_t> data;uint32_t w,h,slices;decode_level(s,level,base,data,w,h,slices);
            if(choice[level].store&&!s->bcDecoded&&!dst->fmt.stencil)
                if(!thin1_build(s,level,ranges[level].first,ranges[level].second,data.data(),data.size()))
                    if(Thin1Level* lv=thin1_find(s,level)) lv->drop();
            if (s->bcDecoded) { bc_decode_upload(dst,level,data,w,h,slices); uploaded+=data.size(); continue; }
            std::vector<VkBufferImageCopy> copies;std::vector<uint8_t> packed;
            bool threeD=dst->imageType==VK_IMAGE_TYPE_3D;
            uint32_t layers=threeD?1:slices;
            if(dst->fmt.stencil) {
                size_t count=size_t(w)*h*slices;
                packed.resize(count*5);
                for(size_t i=0;i<count;++i) {memcpy(packed.data()+i*4,data.data()+i*8,4);packed[count*4+i]=data[i*8+4];}
                VkBufferImageCopy depth{};depth.imageSubresource={VK_IMAGE_ASPECT_DEPTH_BIT,level,0,layers};depth.imageExtent={w,h,threeD?slices:1};
                copies.push_back(depth);auto stencil=depth;stencil.bufferOffset=count*4;stencil.imageSubresource.aspectMask=VK_IMAGE_ASPECT_STENCIL_BIT;copies.push_back(stencil);
            } else {
                packed=std::move(data);
                uploaded+=packed.size();
                VkBufferImageCopy copy{};copy.imageSubresource={dst->aspect,level,0,layers};copy.imageExtent={w,h,threeD?slices:1};copies.push_back(copy);
            }
            Buffer staging=create_buffer(packed.size(),VK_BUFFER_USAGE_TRANSFER_SRC_BIT,VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT|VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);
            if(!staging.mapped){defer_buffer(staging);throw std::runtime_error("Vulkan texture staging allocation is not mapped");}
            memcpy(staging.mapped,packed.data(),packed.size());
            g_stat_upload_bytes+=packed.size();
            rprof::add_upload(rprof::kUpTexture,packed.size());
            vkCmdCopyBufferToImage(command_buffer(),staging.buffer,dst->image,VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,uint32_t(copies.size()),copies.data());
            defer_buffer(staging);
        }
        if(scaleGuest) resample(&guest,s,std::min(guest.arrayLayers,s->arrayLayers),1,1,0,0);
        else transition_image(s,VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
    } catch(...) {
        for(auto& part:partials) if(part.staging.buffer) defer_buffer(part.staging);
        thin1_forget(s);
        s->dirty=true;
        if(scaleGuest) destroy_surface_image(&guest);
        throw;
    }
    if(scaleGuest) destroy_surface_image(&guest);
    s->contentHash=hash;s->writeSeq=next_write_seq();s->dirty=false;s->dirtyOob=false;++g_stat_uploads;
    if (g_uploadProbe.enabled()) g_uploadProbe.record(s, uploaded);
}
void clear_color(const uint32_t*,uint32_t cb,const float rgba[4]) {
    uint32_t first,num;auto* s=surface_from_color_buffer(cb,&first,&num);if(!s)return;
    if(s->fmt.depth||s->fmt.compressed)throw std::runtime_error("GX2 clear color requires an uncompressed color surface");
    VkClearColorValue value{};
    for(unsigned i=0;i<4;++i) {
        double integerValue = std::isnan(rgba[i]) ? 0.0 : double(rgba[i]);
        if(s->fmt.kind==FormatInfo::UINT)value.uint32[i]=uint32_t(std::clamp(integerValue,0.0,4294967295.0));
        else if(s->fmt.kind==FormatInfo::SINT)value.int32[i]=int32_t(std::clamp(integerValue,-2147483648.0,2147483647.0));
        else value.float32[i]=rgba[i];
    }
    end_encoder();
    if(s->imageType==VK_IMAGE_TYPE_3D&&(first||num<s->extent.depth)) {

        transition_image(s,VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT,VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT);
        for(uint32_t z=first;z<first+num;++z) {
            VkRenderingAttachmentInfo a{VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO};
            a.imageView=layer_view(s,z);a.imageLayout=VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
            a.loadOp=VK_ATTACHMENT_LOAD_OP_CLEAR;a.storeOp=VK_ATTACHMENT_STORE_OP_STORE;a.clearValue.color=value;
            VkRenderingInfo ri{VK_STRUCTURE_TYPE_RENDERING_INFO};
            ri.renderArea.extent={s->extent.width,s->extent.height};ri.layerCount=1;ri.colorAttachmentCount=1;ri.pColorAttachments=&a;
            vkCmdBeginRendering(command_buffer(),&ri);vkCmdEndRendering(command_buffer());
        }
        mark_gpu_written(s);return;
    }
    transition_image(s,VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,VK_PIPELINE_STAGE_TRANSFER_BIT,VK_ACCESS_TRANSFER_WRITE_BIT);

    VkImageSubresourceRange range{VK_IMAGE_ASPECT_COLOR_BIT,0,1,s->imageType==VK_IMAGE_TYPE_3D?0:first,s->imageType==VK_IMAGE_TYPE_3D?1:num};
    vkCmdClearColorImage(command_buffer(),s->image,s->layout,&value,1,&range);mark_gpu_written(s);
}
void clear_depth_stencil(const uint32_t*,uint32_t db,float depth,uint32_t stencil,uint32_t flags) {
    uint32_t first,num;auto* s=surface_from_depth_buffer(db,&first,&num);if(!s)return;
    VkImageAspectFlags aspects=0;if(flags&1)aspects|=VK_IMAGE_ASPECT_DEPTH_BIT;if((flags&2)&&s->fmt.stencil)aspects|=VK_IMAGE_ASPECT_STENCIL_BIT;
    if(!aspects)return;
    end_encoder();transition_image(s,VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,VK_PIPELINE_STAGE_TRANSFER_BIT,VK_ACCESS_TRANSFER_WRITE_BIT);
    VkClearDepthStencilValue value{depth,stencil};VkImageSubresourceRange range{aspects,0,1,first,num};
    vkCmdClearDepthStencilImage(command_buffer(),s->image,s->layout,&value,1,&range);mark_gpu_written(s);
}

static bool linear_tile_mode(Latte::E_HWTILEMODE tm) {
    return tm==Latte::E_HWTILEMODE::TM_LINEAR_GENERAL||tm==Latte::E_HWTILEMODE::TM_LINEAR_ALIGNED;
}
static bool can_write_back(const Surface* img) {
    const FormatInfo& f=img->fmt;
    bool ok=!f.compressed&&!f.depth&&f.convert==Convert::NONE&&f.hostBytesPerBlock==f.bytesPerBlock&&img->imageType==VK_IMAGE_TYPE_2D;
    if(!ok) {
        static bool logged=false;
        if(!logged)LOG("[gfx] Vulkan: linear surface %08X (format %X) is not written back to guest memory",img->addr,img->format);
        logged=true;
    }
    return ok;
}

static std::vector<uint8_t> read_guest_texels(Surface* img,uint32_t layer,uint32_t w,uint32_t h) {
    const uint32_t bytes=img->fmt.bytesPerBlock;
    Surface temp;Surface* src=img;
    if(img->extent.width!=img->width||img->extent.height!=img->height) {

        temp.width=img->width;temp.height=img->height;temp.slices=std::max(img->slices,1u);temp.dim=img->dim;temp.format=img->format;temp.fmt=img->fmt;
        create_surface_image(&temp,false);
        src=&temp;
    }
    Buffer b=create_readback_buffer(VkDeviceSize(w)*h*bytes);
    try {
        if(src==&temp)resample(img,&temp,temp.arrayLayers,1,1,0,0);
        transition_image(src,VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,VK_PIPELINE_STAGE_TRANSFER_BIT,VK_ACCESS_TRANSFER_READ_BIT);
        VkBufferImageCopy region{};region.imageSubresource={VK_IMAGE_ASPECT_COLOR_BIT,0,layer,1};region.imageExtent={w,h,1};
        auto cmd=command_buffer();vkCmdCopyImageToBuffer(cmd,src->image,src->layout,b.buffer,1,&region);
        derive_dependency(b.use,VK_PIPELINE_STAGE_TRANSFER_BIT,VK_ACCESS_TRANSFER_WRITE_BIT);
        transition_buffer(b,VK_PIPELINE_STAGE_HOST_BIT,VK_ACCESS_HOST_READ_BIT);
        transition_image(src,VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
        flush();
        const auto* raw=static_cast<const uint8_t*>(b.mapped);
        std::vector<uint8_t> texels(raw,raw+size_t(w)*h*bytes);
        defer_buffer(b);if(src==&temp)destroy_surface_image(&temp);
        return texels;
    } catch(...) { defer_buffer(b);if(src==&temp)destroy_surface_image(&temp);throw; }
}

static void write_back_linear_copy(Surface* img,uint32_t layer,GX2Surface* d,uint32_t dbase,uint32_t dstMip,uint32_t dstSlice,uint32_t w,uint32_t h) {
    LatteAddrLib::AddrSurfaceInfo_OUT di{};
    LatteAddrLib::GX2CalculateSurfaceInfo(d->format,d->width,d->height,d->depth,d->dim,d->tileMode,d->aa,dstMip,&di);
    auto dtm=static_cast<Latte::E_HWTILEMODE>(di.hwTileMode);
    if(!linear_tile_mode(dtm)||dstSlice>=di.depth||!can_write_back(img))return;
    const uint32_t bytes=img->fmt.bytesPerBlock;
    const uint64_t r0=rprof::now_ns();
    auto texels=read_guest_texels(img,layer,w,h);
    for(uint32_t y=0;y<h;++y)for(uint32_t x=0;x<w;++x)
        memcpy(mem::ptr(dbase+element_offset(di,dtm,x,y,dstSlice,bytes*8,d->swizzle,nullptr,false)),texels.data()+(size_t(y)*w+x)*bytes,bytes);
    img->writtenBackSeq=img->writeSeq;
    rprof::add_write_back(0,1,texels.size(),rprof::now_ns()-r0);
}

void write_back_linear_targets() {
    const uint64_t t0=rprof::now_ns();uint64_t readNs=0,bytesDone=0;uint32_t count=0;

    for(Surface* s:R.linearTargets) {
        if(!s->gpuWritten||s->writtenBackSeq==s->writeSeq||!s->image)continue;
        s->writtenBackSeq=s->writeSeq;
        if(s->arrayLayers!=1||!can_write_back(s))continue;
        const uint32_t addr=s->addr;
        const uint32_t bytes=s->fmt.bytesPerBlock,pitch=std::max(s->pitch,s->width);
        const uint64_t r0=rprof::now_ns();
        auto texels=read_guest_texels(s,0,s->width,s->height);
        for(uint32_t y=0;y<s->height;++y)
            memcpy(mem::ptr(addr+y*pitch*bytes),texels.data()+size_t(y)*s->width*bytes,size_t(s->width)*bytes);
        readNs+=rprof::now_ns()-r0;bytesDone+=texels.size();++count;
    }
    rprof::add_write_back(std::max<uint64_t>(1,rprof::now_ns()-t0-readNs),count,bytesDone,readNs);
}
void copy_surface_impl(uint32_t srcAddr,uint32_t srcMip,uint32_t srcSlice,uint32_t dstAddr,uint32_t dstMip,uint32_t dstSlice) {
    auto* s=reinterpret_cast<GX2Surface*>(mem::ptr(srcAddr));auto* d=reinterpret_cast<GX2Surface*>(mem::ptr(dstAddr));
    if(srcMip>=uint32_t(s->numLevels)||dstMip>=uint32_t(d->numLevels))throw std::runtime_error("GX2CopySurface mip is out of range");
    uint32_t sbase=level_address(s,srcMip),dbase=level_address(d,dstMip);
    uint32_t w=std::max<uint32_t>(uint32_t(s->width)>>srcMip,1),h=std::max<uint32_t>(uint32_t(s->height)>>srcMip,1);
    uint32_t dw=std::max<uint32_t>(uint32_t(d->width)>>dstMip,1),dh=std::max<uint32_t>(uint32_t(d->height)>>dstMip,1);
    uint32_t cw=std::min(w,dw),ch=std::min(h,dh);
    Surface* gpuSrc=nullptr;uint32_t gpuLevel=0;

    const uint32_t srcFormat=uint32_t(s->format.value());
    auto better=[&](Surface* image){return !gpuSrc||std::make_pair(image->format==srcFormat,image->writeSeq)>std::make_pair(gpuSrc->format==srcFormat,gpuSrc->writeSeq);};
    for(auto& [addr,image]:R.surfaces) {
        if(!image->gpuWritten)continue;
        if(addr==sbase&&image->width==w&&image->height==h) {if(better(image.get())){gpuSrc=image.get();gpuLevel=0;}}
        else if(addr==uint32_t(s->imagePtr)&&image->mips>srcMip&&std::max(image->width>>srcMip,1u)==w&&std::max(image->height>>srcMip,1u)==h)
            if(better(image.get())){gpuSrc=image.get();gpuLevel=srcMip;}
    }
    if(gpuSrc&&gpuLevel==0)gpuSrc=adopt_newer_alias(gpuSrc);
    if(gpuSrc) {
        if(gpuSrc->imageType==VK_IMAGE_TYPE_3D)throw std::runtime_error("Vulkan GPU GX2CopySurface volume slices are unsupported");
        SurfaceDesc dd;dd.addr=dbase;dd.width=dw;dd.height=dh;dd.pitch=d->pitch;dd.format=uint32_t(d->format.value());
        dd.tileMode=uint32_t(d->tileMode.value());dd.swizzle=d->swizzle;dd.isDepth=gpuSrc->isDepth;
        dd.dim=uint32_t(d->dim.value());dd.slices=std::max<uint32_t>(d->depth,1);
        if(dd.dim==uint32_t(Latte::E_DIM::DIM_2D)||dd.dim==uint32_t(Latte::E_DIM::DIM_1D))dd.slices=1;
        auto* dst=find_or_create_surface(dd,true);
        if(!dst||dst->fmt.pixel!=gpuSrc->fmt.pixel)throw std::runtime_error("Vulkan GPU GX2CopySurface format conversion is unsupported");
        if(srcSlice>=gpuSrc->arrayLayers||dstSlice>=dst->arrayLayers)throw std::runtime_error("GX2CopySurface array slice is out of range");
        if(gpuSrc==dst&&gpuLevel==0&&srcSlice==dstSlice)return;
        bool self=gpuSrc->image==dst->image;
        uint32_t sw=std::min(uint32_t(std::lround(cw*gpuSrc->sx)),std::max(gpuSrc->extent.width>>gpuLevel,1u));
        uint32_t sh=std::min(uint32_t(std::lround(ch*gpuSrc->sy)),std::max(gpuSrc->extent.height>>gpuLevel,1u));
        uint32_t tw=std::min(uint32_t(std::lround(cw*dst->sx)),dst->extent.width),th=std::min(uint32_t(std::lround(ch*dst->sy)),dst->extent.height);
        const ScaledCopy mode=sw!=tw||sh!=th?scaled_copy_mode(dst->fmt):ScaledCopy::Blit;
        if(mode!=ScaledCopy::Blit) {

            if(mode!=ScaledCopy::Draw||self||!(gpuSrc->usage&VK_IMAGE_USAGE_SAMPLED_BIT)||
               !(dst->usage&VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT))return;
            draw_depth_copy(gpuSrc,gpuLevel,srcSlice,sw,sh,dst,dstSlice,tw,th,1);
            mark_gpu_written(dst);
            write_back_linear_copy(dst,dstSlice,d,dbase,dstMip,dstSlice,cw,ch);
            return;
        }
        end_encoder();
        transition_image(gpuSrc,self?VK_IMAGE_LAYOUT_GENERAL:VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,VK_PIPELINE_STAGE_TRANSFER_BIT,VK_ACCESS_TRANSFER_READ_BIT|(self?VK_ACCESS_TRANSFER_WRITE_BIT:0));
        if(!self)transition_image(dst,VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,VK_PIPELINE_STAGE_TRANSFER_BIT,VK_ACCESS_TRANSFER_WRITE_BIT);
        for(auto aspect:{VK_IMAGE_ASPECT_COLOR_BIT,VK_IMAGE_ASPECT_DEPTH_BIT,VK_IMAGE_ASPECT_STENCIL_BIT}) {
            if(!(gpuSrc->aspect&aspect))continue;
            if(sw==tw&&sh==th) {
                VkImageCopy region{};region.srcSubresource={VkImageAspectFlags(aspect),gpuLevel,srcSlice,1};region.dstSubresource={VkImageAspectFlags(aspect),0,dstSlice,1};region.extent={sw,sh,1};
                vkCmdCopyImage(command_buffer(),gpuSrc->image,gpuSrc->layout,dst->image,dst->layout,1,&region);
            } else {
                VkFormatProperties fp{};vkGetPhysicalDeviceFormatProperties(R.physicalDevice,dst->fmt.pixel,&fp);
                VkImageBlit region{};region.srcSubresource={VkImageAspectFlags(aspect),gpuLevel,srcSlice,1};region.dstSubresource={VkImageAspectFlags(aspect),0,dstSlice,1};
                region.srcOffsets[1]={int32_t(sw),int32_t(sh),1};region.dstOffsets[1]={int32_t(tw),int32_t(th),1};
                VkFilter filter=aspect==VK_IMAGE_ASPECT_COLOR_BIT&&dst->fmt.kind==FormatInfo::FLOAT&&(fp.optimalTilingFeatures&VK_FORMAT_FEATURE_SAMPLED_IMAGE_FILTER_LINEAR_BIT)?VK_FILTER_LINEAR:VK_FILTER_NEAREST;
                vkCmdBlitImage(command_buffer(),gpuSrc->image,gpuSrc->layout,dst->image,dst->layout,1,&region,filter);
            }
        }
        transition_image(gpuSrc,VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);if(!self)transition_image(dst,VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
        mark_gpu_written(dst);
        write_back_linear_copy(dst,dstSlice,d,dbase,dstMip,dstSlice,cw,ch);
        return;
    }
    auto sf=cpu_copy_format(uint32_t(s->format.value()));
    auto df=cpu_copy_format(uint32_t(d->format.value()));
    if(sf.pixel==VK_FORMAT_UNDEFINED||df.pixel==VK_FORMAT_UNDEFINED||sf.bytesPerBlock!=df.bytesPerBlock||sf.compressed!=df.compressed)
        throw std::runtime_error("Unsupported CPU GX2CopySurface format layout");
    LatteAddrLib::AddrSurfaceInfo_OUT si{},di{};
    LatteAddrLib::GX2CalculateSurfaceInfo(s->format,s->width,s->height,s->depth,s->dim,s->tileMode,s->aa,srcMip,&si);
    LatteAddrLib::GX2CalculateSurfaceInfo(d->format,d->width,d->height,d->depth,d->dim,d->tileMode,d->aa,dstMip,&di);
    if(srcSlice>=si.depth||dstSlice>=di.depth)throw std::runtime_error("CPU GX2CopySurface slice is out of range");
    auto stm=static_cast<Latte::E_HWTILEMODE>(si.hwTileMode),dtm=static_cast<Latte::E_HWTILEMODE>(di.hwTileMode);
    uint32_t bpp=sf.bytesPerBlock*8,bw=sf.compressed?(cw+3)/4:cw,bh=sf.compressed?(ch+3)/4:ch;
    uint32_t sswz=s->swizzle,dswz=d->swizzle;LatteAddrLib::CachedSurfaceAddrInfo sci{},dci{};
    if(Latte::TM_IsMacroTiled(stm))LatteAddrLib::SetupCachedSurfaceAddrInfo(&sci,srcSlice,0,bpp,si.pitch,si.height,si.depth,1,stm,(sf.depth||sf.convert==Convert::D24_R32F),(sswz>>8)&1,(sswz>>9)&3);
    if(Latte::TM_IsMacroTiled(dtm))LatteAddrLib::SetupCachedSurfaceAddrInfo(&dci,dstSlice,0,bpp,di.pitch,di.height,di.depth,1,dtm,(df.depth||df.convert==Convert::D24_R32F),(dswz>>8)&1,(dswz>>9)&3);

    std::vector<uint8_t> rows(size_t(bw)*bh*sf.bytesPerBlock);
    for(uint32_t y=0;y<bh;++y)for(uint32_t x=0;x<bw;++x) {
        uint32_t offset=element_offset(si,stm,x,y,srcSlice,bpp,sswz,&sci,(sf.depth||sf.convert==Convert::D24_R32F));
        memcpy(rows.data()+(size_t(y)*bw+x)*sf.bytesPerBlock,mem::ptr(sbase+offset),sf.bytesPerBlock);
    }
    for(uint32_t y=0;y<bh;++y)for(uint32_t x=0;x<bw;++x) {
        uint32_t offset=element_offset(di,dtm,x,y,dstSlice,bpp,dswz,&dci,(df.depth||df.convert==Convert::D24_R32F));
        memcpy(mem::ptr(dbase+offset),rows.data()+(size_t(y)*bw+x)*sf.bytesPerBlock,sf.bytesPerBlock);
    }
    for(auto& [address,image]:R.surfaces)if(address==dbase){image->gpuWritten=false;image->dirty=true;image->dirtyOob=true;image->lastCheckedFrame=~0ull;}
}
void copy_surface(uint32_t src,uint32_t srcMip,uint32_t srcSlice,uint32_t dst,uint32_t dstMip,uint32_t dstSlice) {
    copy_surface_impl(src,srcMip,srcSlice,dst,dstMip,dstSlice);
}
void invalidate(uint32_t flags,uint32_t addr,uint32_t size) {
    buffer_cache_guest_invalidate(flags,addr,size);
    ++g_stat_invalidates;if(!(flags&2)||size>=0x10000000)return;
    uint64_t end=uint64_t(addr)+size;
    for(auto& [base,s]:R.surfaces) {
        if(s->gpuWritten||(base>=0xF4000000&&base<0xF6000000))continue;

        if(s->dirty&&s->lastCheckedFrame==~0ull)continue;
        uint64_t bytes=std::max<uint64_t>(s->dataSize,uint64_t(s->pitch)*s->height*s->fmt.bytesPerBlock);
        bool baseHit=uint64_t(base)<end&&uint64_t(addr)<uint64_t(base)+bytes;
        bool mipHit=false;
        if(!baseHit&&s->mipAddr&&s->mips>1) {

            guest_level(s.get(),0);
            auto& layout=*s->guestLayout;
            if(layout.mipRangesComplete) {
                if(layout.mipRangeBegin<end&&uint64_t(addr)<layout.mipRangeEnd)
                    for(uint32_t level=1;level<s->mips&&!mipHit;++level) {
                        const auto& range=layout.levels[level];
                        mipHit=range.rangeBegin<end&&uint64_t(addr)<range.rangeEnd;
                    }
            } else {

                for(uint32_t level=1;level<s->mips&&!mipHit;++level) {
                    const auto& info=guest_info(s.get(),level);
                    uint64_t mip=mip_base(s.get(),level);
                    auto& range=layout.levels[level];
                    if(!range.rangeValid) {
                        range.rangeBegin=mip;range.rangeEnd=mip+info.surfSize;
                        range.rangeValid=true;++layout.mipRangesCached;
                        layout.mipRangeBegin=std::min(layout.mipRangeBegin,range.rangeBegin);
                        layout.mipRangeEnd=std::max(layout.mipRangeEnd,range.rangeEnd);
                    }
                    mipHit=mip<end&&uint64_t(addr)<mip+info.surfSize;
                }
                layout.mipRangesComplete=layout.mipRangesCached==s->mips-1;
            }
        }
        if(baseHit||mipHit) {s->dirty=true;s->lastCheckedFrame=~0ull;++g_stat_invalidated_surfaces;}
    }
}
void ss_reset_surfaces() {
    R.mainDepthAddr = 0;
    for(auto& [addr,s]:R.surfaces){s->guestLayout.reset();s->dirty=true;s->dirtyOob=true;s->lastCheckedFrame=~0ull;}
}
}
