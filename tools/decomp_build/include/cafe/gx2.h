#pragma once
#include <types.h>

typedef u32 GX2SurfaceFormat;
typedef u32 GX2IndexFormat;
typedef u32 GX2CompSel;
typedef u32 GX2EndianSwapMode;
typedef u32 GX2AttribIndexType;
typedef u32 GX2PrimitiveType;
typedef u32 GX2ScanTarget;
typedef u32 GX2CompareFunction;
typedef u32 GX2StencilFunction;
typedef u32 GX2Boolean;
typedef u32 GX2PolygonMode;
typedef u32 GX2TileMode;
typedef u32 GX2AAMode;
typedef u32 GX2SurfaceUse;
typedef u32 GX2SurfaceDim;
typedef u32 GX2PrimitiveMode;
typedef u32 GX2IndexType;
typedef u32 GX2TexClampMode;
typedef u32 GX2TexXYFilterMode;
typedef u32 GX2TexMipFilterMode;
typedef u32 GX2TexAnisoRatio;
typedef u32 GX2TexBorderType;
typedef u32 GX2TexZFilterMode;
typedef u32 GX2FetchShaderType;
typedef u32 GX2ShaderMode;
typedef u32 GX2InvalidateMode;
typedef u32 GX2BlendMode;
typedef u32 GX2BlendCombineMode;
typedef u32 GX2ChannelMask;
typedef u32 GX2LogicOp;
typedef u32 GX2FrontFace;
typedef u32 GX2BlendFunction;
typedef u32 GX2TexClamp;
typedef u32 GX2TexXYFilterType;
typedef u32 GX2BlendCombine;
typedef u32 GX2TexMipFilterType;
typedef u32 GX2FrontFaceMode;
struct GX2PolygonControlReg { unsigned char raw[0x10]; };
struct GX2DepthStencilControlReg { unsigned char raw[0x10]; };
struct GX2AlphaTestReg { unsigned char raw[0x10]; };
struct GX2ColorControlReg { unsigned char raw[0x10]; };
struct GX2BlendControlReg { unsigned char raw[0x10]; };
struct GX2BlendConstantColorReg { unsigned char raw[0x10]; };
typedef u32 GX2AttribFormat;
typedef u32 GX2Component;
typedef u32 GX2RenderTarget;

struct GX2Surface {
    u32 dim;
    u32 width;
    u32 height;
    u32 depth;
    u32 numMips;
    u32 format;
    u32 aa;
    u32 use;
    u32 imageSize;
    void* imagePtr;
    u32 mipSize;
    void* mipPtr;
    u32 tileMode;
    u32 swizzle;
    u32 alignment;
    u32 pitch;
};
struct GX2Texture { GX2Surface surface; u32 compSel; unsigned char raw[0x20]; };
struct GX2Sampler { unsigned char raw[0x20]; };
struct GX2FetchShader { u32 numAttribs; void* data; u32 size; unsigned char raw[0x20]; };
struct GX2AttribStream { unsigned char raw[0x20]; };
struct GX2ColorBuffer { GX2Surface surface; void* auxPtr; u32 auxSize; unsigned char raw[0x20]; };
struct GX2DepthBuffer { GX2Surface surface; void* hiZPtr; u32 hiZSize; unsigned char raw[0x20]; };
struct GX2ContextState { unsigned char raw[0x10]; };
struct GX2StreamOutContext { unsigned char raw[0x10]; };
struct GX2VertexShader { u32 shaderMode; unsigned char raw[0x10]; };
struct GX2PixelShader { u32 shaderMode; unsigned char raw[0x10]; };
struct GX2GeometryShader { u32 shaderMode; unsigned char raw[0x10]; };

inline void GX2SetSurfaceSwizzle(GX2Surface*, u32) {}
inline void GX2CallDisplayList(void*, u32) {}
inline void GX2DirectCallDisplayList(void*, u32) {}

enum {
    GX2_COMPARE_NEVER = 0,
    GX2_COMPARE_LESS = 1,
    GX2_COMPARE_EQUAL = 2,
    GX2_COMPARE_LEQUAL = 3,
    GX2_COMPARE_GREATER = 4,
    GX2_COMPARE_NOTEQUAL = 5,
    GX2_COMPARE_GEQUAL = 6,
    GX2_COMPARE_ALWAYS = 7,
    GX2_BLEND_ZERO = 0,
    GX2_BLEND_ONE = 1,
    GX2_BLEND_SRC_COLOR = 2,
    GX2_BLEND_ONE_MINUS_SRC_COLOR = 3,
    GX2_BLEND_SRC_ALPHA = 4,
    GX2_BLEND_ONE_MINUS_SRC_ALPHA = 5,
    GX2_BLEND_DST_ALPHA = 6,
    GX2_BLEND_ONE_MINUS_DST_ALPHA = 7,
    GX2_BLEND_DST_COLOR = 8,
    GX2_BLEND_ONE_MINUS_DST_COLOR = 9,
    GX2_BLEND_SRC_ALPHA_SATURATE = 10,
    GX2_BLEND_CONSTANT_COLOR = 13,
    GX2_BLEND_ONE_MINUS_CONSTANT_COLOR = 14,
    GX2_BLEND_CONSTANT_ALPHA = 19,
    GX2_BLEND_ONE_MINUS_CONSTANT_ALPHA = 20,
    GX2_BLEND_COMBINE_ADD = 0,
    GX2_BLEND_COMBINE_DST_MINUS_SRC = 1,
    GX2_BLEND_COMBINE_MIN = 2,
    GX2_BLEND_COMBINE_MAX = 3,
    GX2_BLEND_COMBINE_SRC_MINUS_DST = 4,
    GX2_STENCIL_KEEP = 0,
    GX2_STENCIL_ZERO = 1,
    GX2_STENCIL_REPLACE = 2,
    GX2_STENCIL_INCR = 3,
    GX2_STENCIL_DECR = 4,
    GX2_STENCIL_INVERT = 5,
    GX2_STENCIL_INCR_WRAP = 6,
    GX2_STENCIL_DECR_WRAP = 7,
    GX2_POLYGON_MODE_POINT = 0,
    GX2_POLYGON_MODE_LINE = 1,
    GX2_POLYGON_MODE_TRIANGLE = 2,
    GX2_SCAN_TARGET_TV = 1,
    GX2_SCAN_TARGET_DRC_FIRST = 4,
    GX2_DISPLAY_LIST_ALIGNMENT = 0x40,
};


enum {
    GX2_AA_MODE_1X,
    GX2_ALPHA_TO_MASK_0,
    GX2_ATTRIB_FORMAT_10_10_10_2_SNORM,
    GX2_ATTRIB_FORMAT_32_32_32_32_FLOAT,
    GX2_ATTRIB_FORMAT_32_32_32_FLOAT,
    GX2_ATTRIB_FORMAT_32_32_FLOAT,
    GX2_ATTRIB_FORMAT_32_UINT,
    GX2_ATTRIB_FORMAT_8_8_8_8_SNORM,
    GX2_ATTRIB_FORMAT_8_8_8_8_UNORM,
    GX2_ATTRIB_INDEX_INSTANCE_ID,
    GX2_ATTRIB_INDEX_VERTEX_ID,
    GX2_CHANNEL_MASK_A,
    GX2_CHANNEL_MASK_B,
    GX2_CHANNEL_MASK_G,
    GX2_CHANNEL_MASK_NONE,
    GX2_CHANNEL_MASK_R,
    GX2_CHANNEL_MASK_RGBA,
    GX2_CLEAR_BOTH,
    GX2_CLEAR_DEPTH,
    GX2_CLEAR_STENCIL,
    GX2_COMPONENT_C_0,
    GX2_COMPONENT_C_1,
    GX2_COMPONENT_W_A,
    GX2_COMPONENT_X_R,
    GX2_COMPONENT_Y_G,
    GX2_COMPONENT_Z_B,
    GX2_COMP_SEL_X001,
    GX2_COMP_SEL_XY01,
    GX2_COMP_SEL_XYZ1,
    GX2_COMP_SEL_XYZW,
    GX2_CONTEXT_STATE_ALIGNMENT,
    GX2_DEFAULT_BUFFER_ALIGNMENT,
    GX2_DISABLE,
    GX2_ENABLE,
    GX2_ENDIANSWAP_DEFAULT,
    GX2_FALSE,
    GX2_FRONT_FACE_CCW,
    GX2_FRONT_FACE_CW,
    GX2_INDEX_BUFFER_ALIGNMENT,
    GX2_INDEX_FORMAT_U16,
    GX2_INDEX_FORMAT_U32,
    GX2_INIT_ATTRIB_ARGC,
    GX2_INIT_ATTRIB_ARGV,
    GX2_INIT_ATTRIB_CB_BASE,
    GX2_INIT_ATTRIB_CB_SIZE,
    GX2_INIT_ATTRIB_NULL,
    GX2_INVALIDATE_ATTRIB_BUFFER,
    GX2_INVALIDATE_COLOR_BUFFER,
    GX2_INVALIDATE_CPU_ATTRIB_BUFFER,
    GX2_INVALIDATE_CPU_SHADER,
    GX2_INVALIDATE_CPU_TEXTURE,
    GX2_INVALIDATE_CPU_UNIFORM_BLOCK,
    GX2_INVALIDATE_SHADER,
    GX2_INVALIDATE_TEXTURE,
    GX2_INVALIDATE_UNIFORM_BLOCK,
    GX2_LOGIC_OP_COPY,
    GX2_LOGIC_OP_NONE,
    GX2_MAX_ATTRIB_BUFFERS,
    GX2_PRIMITIVE_LINE_LOOP,
    GX2_PRIMITIVE_POINTS,
    GX2_PRIMITIVE_QUADS,
    GX2_PRIMITIVE_QUAD_STRIP,
    GX2_PRIMITIVE_TRIANGLES,
    GX2_PRIMITIVE_TRIANGLE_STRIP,
    GX2_RENDER_TARGET_0,
    GX2_SAMPLER_TYPE_2D,
    GX2_SHADER_ALIGNMENT,
    GX2_SHADER_MODE_GEOMETRY_SHADER,
    GX2_SHADER_MODE_UNIFORM_BLOCK,
    GX2_SHADER_MODE_UNIFORM_REGISTER,
    GX2_SURFACE_DIM_1D_ARRAY,
    GX2_SURFACE_DIM_2D,
    GX2_SURFACE_DIM_2D_ARRAY,
    GX2_SURFACE_DIM_2D_MSAA,
    GX2_SURFACE_DIM_CUBE,
    GX2_SURFACE_FORMAT_D_D24_S8_UNORM,
    GX2_SURFACE_FORMAT_INVALID,
    GX2_SURFACE_FORMAT_TCD_R16_UNORM,
    GX2_SURFACE_FORMAT_TCD_R32_FLOAT,
    GX2_SURFACE_FORMAT_TCS_A2_B10_G10_R10_UNORM,
    GX2_SURFACE_FORMAT_TCS_R10_G10_B10_A2_UNORM,
    GX2_SURFACE_FORMAT_TCS_R5_G6_B5_UNORM,
    GX2_SURFACE_FORMAT_TCS_R8_G8_B8_A8_SRGB,
    GX2_SURFACE_FORMAT_TCS_R8_G8_B8_A8_UNORM,
    GX2_SURFACE_FORMAT_TC_A1_B5_G5_R5_UNORM,
    GX2_SURFACE_FORMAT_TC_A2_B10_G10_R10_UINT,
    GX2_SURFACE_FORMAT_TC_R10_G10_B10_A2_UINT,
    GX2_SURFACE_FORMAT_TC_R11_G11_B10_FLOAT,
    GX2_SURFACE_FORMAT_TC_R16_FLOAT,
    GX2_SURFACE_FORMAT_TC_R16_G16_B16_A16_FLOAT,
    GX2_SURFACE_FORMAT_TC_R16_G16_B16_A16_SINT,
    GX2_SURFACE_FORMAT_TC_R16_G16_B16_A16_SNORM,
    GX2_SURFACE_FORMAT_TC_R16_G16_B16_A16_UINT,
    GX2_SURFACE_FORMAT_TC_R16_G16_B16_A16_UNORM,
    GX2_SURFACE_FORMAT_TC_R16_G16_FLOAT,
    GX2_SURFACE_FORMAT_TC_R16_G16_SINT,
    GX2_SURFACE_FORMAT_TC_R16_G16_SNORM,
    GX2_SURFACE_FORMAT_TC_R16_G16_UINT,
    GX2_SURFACE_FORMAT_TC_R16_G16_UNORM,
    GX2_SURFACE_FORMAT_TC_R16_SINT,
    GX2_SURFACE_FORMAT_TC_R16_SNORM,
    GX2_SURFACE_FORMAT_TC_R16_UINT,
    GX2_SURFACE_FORMAT_TC_R32_G32_B32_A32_FLOAT,
    GX2_SURFACE_FORMAT_TC_R32_G32_B32_A32_SINT,
    GX2_SURFACE_FORMAT_TC_R32_G32_B32_A32_UINT,
    GX2_SURFACE_FORMAT_TC_R32_G32_FLOAT,
    GX2_SURFACE_FORMAT_TC_R32_G32_SINT,
    GX2_SURFACE_FORMAT_TC_R32_G32_UINT,
    GX2_SURFACE_FORMAT_TC_R32_SINT,
    GX2_SURFACE_FORMAT_TC_R32_UINT,
    GX2_SURFACE_FORMAT_TC_R4_G4_B4_A4_UNORM,
    GX2_SURFACE_FORMAT_TC_R5_G5_B5_A1_UNORM,
    GX2_SURFACE_FORMAT_TC_R8_G8_B8_A8_SINT,
    GX2_SURFACE_FORMAT_TC_R8_G8_B8_A8_SNORM,
    GX2_SURFACE_FORMAT_TC_R8_G8_B8_A8_UINT,
    GX2_SURFACE_FORMAT_TC_R8_G8_SINT,
    GX2_SURFACE_FORMAT_TC_R8_G8_SNORM,
    GX2_SURFACE_FORMAT_TC_R8_G8_UINT,
    GX2_SURFACE_FORMAT_TC_R8_G8_UNORM,
    GX2_SURFACE_FORMAT_TC_R8_SINT,
    GX2_SURFACE_FORMAT_TC_R8_SNORM,
    GX2_SURFACE_FORMAT_TC_R8_UINT,
    GX2_SURFACE_FORMAT_TC_R8_UNORM,
    GX2_SURFACE_FORMAT_T_BC1_SRGB,
    GX2_SURFACE_FORMAT_T_BC1_UNORM,
    GX2_SURFACE_FORMAT_T_BC2_SRGB,
    GX2_SURFACE_FORMAT_T_BC2_UNORM,
    GX2_SURFACE_FORMAT_T_BC3_SRGB,
    GX2_SURFACE_FORMAT_T_BC3_UNORM,
    GX2_SURFACE_FORMAT_T_BC4_SNORM,
    GX2_SURFACE_FORMAT_T_BC4_UNORM,
    GX2_SURFACE_FORMAT_T_BC5_SNORM,
    GX2_SURFACE_FORMAT_T_BC5_UNORM,
    GX2_SURFACE_FORMAT_T_R32_G32_B32_FLOAT,
    GX2_SURFACE_FORMAT_T_R32_G32_B32_SINT,
    GX2_SURFACE_FORMAT_T_R32_G32_B32_UINT,
    GX2_SURFACE_USE_COLOR_BUFFER,
    GX2_SURFACE_USE_DEPTH_BUFFER,
    GX2_SURFACE_USE_DEPTH_BUFFER_TEXTURE,
    GX2_SURFACE_USE_TEXTURE,
    GX2_TEX_ANISO_1_TO_1,
    GX2_TEX_BORDER_CLEAR_BLACK,
    GX2_TEX_BORDER_SOLID_BLACK,
    GX2_TEX_BORDER_SOLID_WHITE,
    GX2_TEX_BORDER_USE_REGISTER,
    GX2_TEX_CLAMP_CLAMP,
    GX2_TEX_CLAMP_MIRROR,
    GX2_TEX_CLAMP_MIRROR_ONCE,
    GX2_TEX_CLAMP_WRAP,
    GX2_TEX_XY_FILTER_BILINEAR,
    GX2_TEX_XY_FILTER_POINT,
    GX2_TEX_Z_FILTER_POINT,
    GX2_TEX_Z_FILTER_USE_XY,
    GX2_TILE_MODE_DEFAULT,
    GX2_TILE_MODE_LINEAR_ALIGNED,
    GX2_TILE_MODE_LINEAR_SPECIAL,
    GX2_TRUE,
    GX2_UNIFORM_BLOCK_ALIGNMENT,
    GX2_VAR_TYPE_VEC2,
    GX2_VERTEX_BUFFER_ALIGNMENT
};

template <class... A> inline void GX2Invalidate(A&&...) {}
template <class... A> inline void GX2SetVertexUniformReg(A&&...) {}
template <class... A> inline void GX2SetPixelUniformReg(A&&...) {}
template <class... A> inline void GX2SetAttribBuffer(A&&...) {}
template <class... A> inline void GX2InitAttribStream(A&&...) {}
template <class... A> inline u32 GX2CalcFetchShaderSize(A&&...) { return 0; }
template <class... A> inline void GX2InitFetchShader(A&&...) {}
template <class... A> inline void GX2SetBlendControl(A&&...) {}
template <class... A> inline void GX2SetTargetChannelMasks(A&&...) {}
template <class... A> inline void GX2SetStencilMask(A&&...) {}
template <class... A> inline void GX2SetPolygonControl(A&&...) {}
template <class... A> inline void GX2SetDepthStencilControl(A&&...) {}
template <class... A> inline void GX2SetColorControl(A&&...) {}
template <class... A> inline void GX2SetBlendConstantColor(A&&...) {}
template <class... A> inline void GX2SetAlphaTest(A&&...) {}
template <class... A> inline void GX2InitSamplerBorderType(A&&...) {}
template <class... A> inline void GX2InitDepthBufferHiZEnable(A&&...) {}
template <class... A> inline u32 GX2GetVertexAttribVarLocation(A&&...) { return 0; }
template <class... A> inline u32 GX2GetVertexUniformVarOffset(A&&...) { return 0; }
template <class... A> inline u32 GX2GetPixelUniformVarOffset(A&&...) { return 0; }
template <class... A> inline void GX2SetShaderModeEx(A&&...) {}
template <class... A> inline void GX2SetShaderMode(A&&...) {}
template <class... A> inline void GX2SetPixelTexture(A&&...) {}
template <class... A> inline void GX2SetPixelSampler(A&&...) {}
template <class... A> inline void GX2SetVertexTexture(A&&...) {}
template <class... A> inline void GX2SetVertexSampler(A&&...) {}
template <class... A> inline void GX2SetCullOnlyControl(A&&...) {}
template <class... A> inline void GX2InitSampler(A&&...) {}
template <class... A> inline void GX2DrawIndexed(A&&...) {}
template <class... A> inline void GX2ClearDepthStencilEx(A&&...) {}
template <class... A> inline void GX2ClearBuffersEx(A&&...) {}
template <class... A> inline void GX2CalcSurfaceSizeAndAlignment(A&&...) {}
template <class... A> inline void GX2CalcDepthBufferHiZInfo(A&&...) {}
template <class... A> inline void GX2SetTVEnable(A&&...) {}
template <class... A> inline void GX2SetDRCEnable(A&&...) {}
template <class... A> inline void GX2SetSwapInterval(A&&...) {}
template <class... A> inline void GX2SetAlphaToMask(A&&...) {}
template <class... A> inline void GX2InitTextureRegs(A&&...) {}
template <class... A> inline void GX2InitDepthBuffer(A&&...) {}
template <class... A> inline void GX2InitColorBuffer(A&&...) {}
template <class... A> inline void GX2InitColorBufferAuxPtr(A&&...) {}
template <class... A> inline void GX2SetFetchShader(A&&...) {}
template <class... A> inline void GX2GetCurrentDisplayList(A&&...) {}
template <class... A> inline void GX2SetViewport(A&&...) {}
template <class... A> inline void GX2SetScissor(A&&...) {}
template <class... A> inline void GX2SetVertexShader(A&&...) {}
template <class... A> inline void GX2SetPixelShader(A&&...) {}
template <class... A> inline void GX2SetGeometryShader(A&&...) {}
template <class... A> inline void GX2DrawEx(A&&...) {}
template <class... A> inline void GX2Draw(A&&...) {}
template <class... A> inline void GX2InitColorBufferFTV(A&&...) {}
template <class... A> inline void GX2InitDepthBufferFTV(A&&...) {}

#define GX2_GET_COMP_SEL(x, y, z, w) (((x) & 7) | (((y) & 7) << 3) | (((z) & 7) << 6) | (((w) & 7) << 9))
#define GX2_GET_COMPONENT_X_R(sel) ((sel) & 7)
#define GX2_GET_COMPONENT_Y_G(sel) (((sel) >> 3) & 7)
#define GX2_GET_COMPONENT_Z_B(sel) (((sel) >> 6) & 7)
#define GX2_GET_COMPONENT_W_A(sel) (((sel) >> 9) & 7)
template <class... A> inline u32 GX2BeginDisplayList(A&&...) { return 0; }
template <class... A> inline u32 GX2BeginDisplayListEx(A&&...) { return 0; }
template <class... A> inline u32 GX2CalcColorBufferAuxInfo(A&&...) { return 0; }
template <class... A> inline u32 GX2CalcGeometryShaderInputRingBufferSize(A&&...) { return 0; }
template <class... A> inline u32 GX2CalcGeometryShaderOutputRingBufferSize(A&&...) { return 0; }
template <class... A> inline void GX2ClearColor(A&&...) {}
template <class... A> inline void GX2ClearDepthStencil(A&&...) {}
template <class... A> inline void GX2CopyColorBufferToScanBuffer(A&&...) {}
template <class... A> inline void GX2CopySurface(A&&...) {}
template <class... A> inline void GX2DrawIndexedEx(A&&...) {}
template <class... A> inline void GX2EndDisplayList(A&&...) {}
template <class... A> inline void GX2EndianSwap(A&&...) {}
template <class... A> inline void GX2ExpandAAColorBuffer(A&&...) {}
template <class... A> inline void GX2ExpandDepthBuffer(A&&...) {}
template <class... A> inline u32 GX2GetContextStateDisplayList(A&&...) { return 0; }
template <class... A> inline u32 GX2GetGeometrySamplerVar(A&&...) { return 0; }
template <class... A> inline u32 GX2GetGeometryUniformBlock(A&&...) { return 0; }
template <class... A> inline u32 GX2GetGeometryUniformVarOffset(A&&...) { return 0; }
template <class... A> inline u32 GX2GetPixelSamplerVar(A&&...) { return 0; }
template <class... A> inline u32 GX2GetPixelSamplerVarLocation(A&&...) { return 0; }
template <class... A> inline u32 GX2GetPixelUniformBlock(A&&...) { return 0; }
template <class... A> inline u32 GX2GetVertexSamplerVar(A&&...) { return 0; }
template <class... A> inline u32 GX2GetVertexUniformBlock(A&&...) { return 0; }
template <class... A> inline void GX2Init(A&&...) {}
template <class... A> inline void GX2InitColorBufferPtr(A&&...) {}
template <class... A> inline void GX2InitColorBufferRegs(A&&...) {}
template <class... A> inline void GX2InitDepthBufferHiZPtr(A&&...) {}
template <class... A> inline void GX2InitDepthBufferRegs(A&&...) {}
template <class... A> inline void GX2InitSamplerClamping(A&&...) {}
template <class... A> inline void GX2InitSamplerDepthCompare(A&&...) {}
template <class... A> inline void GX2InitSamplerLOD(A&&...) {}
template <class... A> inline void GX2InitSamplerXYFilter(A&&...) {}
template <class... A> inline void GX2InitSamplerZMFilter(A&&...) {}
template <class... A> inline void GX2InitTexture(A&&...) {}
template <class... A> inline void GX2InitTextureCompSel(A&&...) {}
template <class... A> inline void GX2InitTexturePtrs(A&&...) {}
template <class... A> inline void GX2SetAlphaTestReg(A&&...) {}
template <class... A> inline void GX2SetBlendConstantColorReg(A&&...) {}
template <class... A> inline void GX2SetBlendControlReg(A&&...) {}
template <class... A> inline void GX2SetClearDepthStencil(A&&...) {}
template <class... A> inline void GX2SetColorBuffer(A&&...) {}
template <class... A> inline void GX2SetColorControlReg(A&&...) {}
template <class... A> inline void GX2SetDefaultState(A&&...) {}
template <class... A> inline void GX2SetDepthBuffer(A&&...) {}
template <class... A> inline void GX2SetDepthOnlyControl(A&&...) {}
template <class... A> inline void GX2SetDepthStencilControlReg(A&&...) {}
template <class... A> inline void GX2SetGeometrySampler(A&&...) {}
template <class... A> inline void GX2SetGeometrySamplerBorderColor(A&&...) {}
template <class... A> inline void GX2SetGeometryShaderInputRingBuffer(A&&...) {}
template <class... A> inline void GX2SetGeometryShaderOutputRingBuffer(A&&...) {}
template <class... A> inline void GX2SetGeometryTexture(A&&...) {}
template <class... A> inline void GX2SetGeometryUniformBlock(A&&...) {}
template <class... A> inline void GX2SetLineWidth(A&&...) {}
template <class... A> inline void GX2SetPixelSamplerBorderColor(A&&...) {}
template <class... A> inline void GX2SetPixelUniformBlock(A&&...) {}
template <class... A> inline void GX2SetPolygonControlReg(A&&...) {}
template <class... A> inline void GX2SetPolygonOffset(A&&...) {}
template <class... A> inline void GX2SetPrimitiveRestartIndex(A&&...) {}
template <class... A> inline void GX2SetShaders(A&&...) {}
template <class... A> inline void GX2SetShadersEx(A&&...) {}
template <class... A> inline void GX2SetVertexSamplerBorderColor(A&&...) {}
template <class... A> inline void GX2SetVertexUniformBlock(A&&...) {}
template <class... A> inline void GX2SetupContextState(A&&...) {}
template <class... A> inline void GX2SetupContextStateEx(A&&...) {}
