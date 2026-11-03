#pragma once

#include <Foundation/Foundation.hpp>
#include <Metal/Metal.hpp>

#include "Cafe/HW/Latte/Core/LatteConst.h"

struct MetalPixelFormatSupport
{
	bool m_supportsR8Unorm_sRGB;
	bool m_supportsRG8Unorm_sRGB;
	bool m_supportsPacked16BitFormats;
	bool m_supportsDepth24Unorm_Stencil8;

	MetalPixelFormatSupport() = default;
	MetalPixelFormatSupport(MTL::Device* device)
	{
        m_supportsR8Unorm_sRGB = device->supportsFamily(MTL::GPUFamilyApple1);
        m_supportsRG8Unorm_sRGB = device->supportsFamily(MTL::GPUFamilyApple1);
        m_supportsPacked16BitFormats = device->supportsFamily(MTL::GPUFamilyApple1);
        m_supportsDepth24Unorm_Stencil8 = device->depth24Stencil8PixelFormatSupported();
	}
};

struct MetalQueryRange
{
    uint32 begin;
	uint32 end;
};

#define MAX_MTL_BUFFERS 31

#define MTL_RESERVED_BUFFERS 3
#define MAX_MTL_VERTEX_BUFFERS (MAX_MTL_BUFFERS - MTL_RESERVED_BUFFERS)
#define GET_MTL_VERTEX_BUFFER_INDEX(index) (MAX_MTL_VERTEX_BUFFERS - index - 1)

#define MAX_MTL_TEXTURES 31
#define MAX_MTL_SAMPLERS 16

#define GET_HELPER_BUFFER_BINDING(index) (28 + index)
#define GET_HELPER_TEXTURE_BINDING(index) (29 + index)
#define GET_HELPER_SAMPLER_BINDING(index) (14 + index)

constexpr uint32 INVALID_UINT32 = std::numeric_limits<uint32>::max();
constexpr size_t INVALID_OFFSET = std::numeric_limits<size_t>::max();

inline size_t Align(size_t size, size_t alignment)
{
    return (size + alignment - 1) & ~(alignment - 1);
}

__attribute__((unused)) static inline void StackAutoRelease(void* object)
{
    (*(NS::Object**)object)->release();
}

#define NS_STACK_SCOPED __attribute__((cleanup(StackAutoRelease))) __attribute__((unused))

inline NS::String* ToNSString(const char* str)
{
    return NS::String::string(str, NS::ASCIIStringEncoding);
}

inline NS::String* ToNSString(const std::string& str)
{
    return ToNSString(str.c_str());
}

inline NS::URL* ToNSURL(const char* str)
{
    return NS::URL::fileURLWithPath(ToNSString(str));
}

inline NS::URL* ToNSURL(const std::string& str)
{
    return ToNSURL(str.c_str());
}

inline NS::String* GetLabel(const std::string& label, const void* identifier)
{
    return ToNSString(label + " (" + std::to_string(reinterpret_cast<uintptr_t>(identifier)) + ")");
}

constexpr MTL::RenderStages ALL_MTL_RENDER_STAGES = MTL::RenderStageVertex | MTL::RenderStageObject | MTL::RenderStageMesh | MTL::RenderStageFragment;

inline bool IsValidDepthTextureType(Latte::E_DIM dim)
{
    return (dim == Latte::E_DIM::DIM_2D || dim == Latte::E_DIM::DIM_2D_MSAA || dim == Latte::E_DIM::DIM_2D_ARRAY || dim == Latte::E_DIM::DIM_2D_ARRAY_MSAA || dim == Latte::E_DIM::DIM_CUBEMAP);
}

inline bool CommandBufferCompleted(MTL::CommandBuffer* commandBuffer)
{
    auto status = commandBuffer->status();
    return (status == MTL::CommandBufferStatusCompleted || status == MTL::CommandBufferStatusError);
}

inline bool FormatIsRenderable(Latte::E_GX2SURFFMT format)
{
    return !Latte::IsCompressedFormat(format);
}

template <typename... T>
inline bool executeCommand(fmt::format_string<T...> fmt, T&&... args) {
    std::string command = fmt::format(fmt, std::forward<T>(args)...);
#if TARGET_OS_IPHONE
    int res = -1; // no shell on iOS
#else
    int res = system(command.c_str());
#endif
    if (res != 0)
    {
        cemuLog_log(LogType::Force, "command \"{}\" failed with exit code {}", command, res);
        return false;
    }

    return true;
}

inline uint32 GetVerticesPerPrimitive(LattePrimitiveMode primitiveMode)
{
    switch (primitiveMode)
    {
    case LattePrimitiveMode::POINTS:
        return 1;
    case LattePrimitiveMode::LINES:
        return 2;
    case LattePrimitiveMode::LINE_STRIP:

        return 2;
    case LattePrimitiveMode::TRIANGLES:
        return 3;
    case LattePrimitiveMode::RECTS:
        return 3;
    default:
        cemuLog_log(LogType::Force, "Unimplemented primitive type {}", primitiveMode);
        return 0;
    }
}

inline bool PrimitiveRequiresConnection(LattePrimitiveMode primitiveMode)
{
    if (primitiveMode == LattePrimitiveMode::LINE_STRIP)
        return true;
    else
        return false;
}

inline bool UseRectEmulation(const LatteContextRegister& lcr) {
    const LattePrimitiveMode primitiveMode = lcr.VGT_PRIMITIVE_TYPE.get_PRIMITIVE_MODE();
    return (primitiveMode == Latte::LATTE_VGT_PRIMITIVE_TYPE::E_PRIMITIVE_TYPE::RECTS);
}

inline bool UseGeometryShader(const LatteContextRegister& lcr, bool hasGeometryShader) {
    return hasGeometryShader || UseRectEmulation(lcr);
}
