
#pragma once
#include "Cafe/HW/Latte/ISA/LatteReg.h"
#include "Cafe/HW/Latte/LatteAddrLib/LatteAddrLib.h"

struct GX2Surface
{
	 betype<Latte::E_DIM> dim;
	 uint32be width;
	 uint32be height;
	 uint32be depth;
	 uint32be numLevels;
	 betype<Latte::E_GX2SURFFMT> format;
	 uint32be aa;
	 uint32be resFlag;
	 uint32be imageSize;
	 uint32be imagePtr;
	 uint32be mipSize;
	 uint32be mipPtr;
	 betype<Latte::E_GX2TILEMODE> tileMode;
	 uint32be swizzle;
	 uint32be alignment;
	 uint32be pitch;
	 uint32be mipOffset[13];
};
static_assert(sizeof(GX2Surface) == 0x74);
