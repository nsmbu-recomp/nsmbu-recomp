#pragma once
#include "Cafe/OS/libs/gx2/GX2_Surface.h"
#include "Cafe/HW/Latte/ISA/LatteReg.h"

namespace GX2 {
struct GX2Texture {
     GX2Surface surface;
     uint32be viewFirstMip;
     uint32be viewNumMips;
     uint32be viewFirstSlice;
     uint32be viewNumSlices;
     uint32be compSel;
     betype<Latte::LATTE_SQ_TEX_RESOURCE_WORD0_N> regTexWord0;
     betype<Latte::LATTE_SQ_TEX_RESOURCE_WORD1_N> regTexWord1;
     betype<Latte::LATTE_SQ_TEX_RESOURCE_WORD4_N> regTexWord4;
     betype<Latte::LATTE_SQ_TEX_RESOURCE_WORD5_N> regTexWord5;
     betype<Latte::LATTE_SQ_TEX_RESOURCE_WORD6_N> regTexWord6;
};
static_assert(sizeof(GX2Texture) == 0x9C);

struct GX2Sampler {
    betype<Latte::LATTE_SQ_TEX_SAMPLER_WORD0_0> word0;
    betype<Latte::LATTE_SQ_TEX_SAMPLER_WORD1_0> word1;
    betype<Latte::LATTE_SQ_TEX_SAMPLER_WORD2_0> word2;
};
static_assert(sizeof(GX2Sampler) == 12);

using Latte::LATTE_SQ_TEX_SAMPLER_WORD0_0;
void GX2InitTextureRegs(GX2Texture* texture);
void GX2InitSampler(GX2Sampler* sampler, LATTE_SQ_TEX_SAMPLER_WORD0_0::E_CLAMP clampXYZ, LATTE_SQ_TEX_SAMPLER_WORD0_0::E_XY_FILTER filterMinMag);
void GX2InitSamplerXYFilter(GX2Sampler* sampler, LATTE_SQ_TEX_SAMPLER_WORD0_0::E_XY_FILTER magFilter, LATTE_SQ_TEX_SAMPLER_WORD0_0::E_XY_FILTER minFilter, uint32 maxAnisoRatio);
void GX2InitSamplerZMFilter(GX2Sampler* sampler, LATTE_SQ_TEX_SAMPLER_WORD0_0::E_Z_FILTER zFilter, LATTE_SQ_TEX_SAMPLER_WORD0_0::E_Z_FILTER mipFilter);
void GX2InitSamplerLOD(GX2Sampler* sampler, float minLod, float maxLod, float lodBias);
void GX2InitSamplerClamping(GX2Sampler* sampler, LATTE_SQ_TEX_SAMPLER_WORD0_0::E_CLAMP clampX, LATTE_SQ_TEX_SAMPLER_WORD0_0::E_CLAMP clampY, LATTE_SQ_TEX_SAMPLER_WORD0_0::E_CLAMP clampZ);
void GX2InitSamplerBorderType(GX2Sampler* sampler, LATTE_SQ_TEX_SAMPLER_WORD0_0::E_BORDER_COLOR_TYPE borderColorType);
void GX2InitSamplerDepthCompare(GX2Sampler* sampler, LATTE_SQ_TEX_SAMPLER_WORD0_0::E_DEPTH_COMPARE depthCompareFunction);
}

namespace GX2 {
struct GX2ColorBuffer {
     GX2Surface surface;
     uint32be viewMip;
     uint32be viewFirstSlice;
     uint32be viewNumSlices;
     uint32be auxData;
     uint32be auxSize2;
     uint32be reg_size;
     uint32be reg_info;
     uint32be reg_view;
     uint32be reg_mask;
     uint32be reg4;
};
static_assert(sizeof(GX2ColorBuffer) == 0x9C);

struct GX2DepthBuffer {
     GX2Surface surface;
     uint32be viewMip;
     uint32be viewFirstSlice;
     uint32be viewNumSlices;
     uint32be hiZPtr;
     uint32be hiZSize;
     float32be clearDepth;
     uint32be clearStencil;
     uint32be reg_size;
     uint32be reg_view;
     uint32be reg_base;
     uint32be reg_htile_surface;
     uint32be reg_prefetch_limit;
     uint32be reg_preload_control;
     uint32be reg_poly_offset_db_fmt_cntl;
};
static_assert(sizeof(GX2DepthBuffer) == 0xAC);

void GX2InitColorBufferRegs(GX2ColorBuffer* colorBuffer);
void GX2InitDepthBufferRegs(GX2DepthBuffer* depthBuffer);
}
