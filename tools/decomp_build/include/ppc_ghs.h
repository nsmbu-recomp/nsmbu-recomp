#pragma once
#include <math.h>

inline float __FRSQRTE(float x) { return 1.0f / sqrtf(x); }
inline float __FNMSUBS(float a, float b, float c) { return c - a * b; }
