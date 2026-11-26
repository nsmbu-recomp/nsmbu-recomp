#pragma once
#include <stdint.h>
extern "C" {
void GX2SampleTopGPUCycle(void* counters);
void GX2SampleBottomGPUCycle(void* counters);
uint64_t GX2GPUTimeToCPUTime(uint32_t begin, uint32_t end);
void GX2WaitForVsync();
}
