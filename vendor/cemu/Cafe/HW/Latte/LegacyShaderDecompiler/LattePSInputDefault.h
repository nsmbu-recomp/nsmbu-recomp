#pragma once
#include <cstdint>

inline const char* LattePSInputDefaultGLSL(uint32_t spiPsInputCntl)
{
	static const char* const defaults[4] = { "vec4(0.0, 0.0, 0.0, 0.0)", "vec4(0.0, 0.0, 0.0, 1.0)",
		"vec4(1.0, 1.0, 1.0, 0.0)", "vec4(1.0, 1.0, 1.0, 1.0)" };
	return defaults[(spiPsInputCntl >> 8) & 3];
}
