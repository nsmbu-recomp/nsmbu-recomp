#pragma once
#include <cafe/gx2.h>
struct GFDHeader { u32 magic; };
inline BOOL _DEMOGFDReadVertexShader(void*, GX2VertexShader**, u32, const void*) { return 0; }
inline BOOL _DEMOGFDReadPixelShader(void*, GX2PixelShader**, u32, const void*) { return 0; }
inline BOOL _DEMOGFDReadGeometryShader(void*, GX2GeometryShader**, u32, const void*) { return 0; }
