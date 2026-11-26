// The decompiled frame loop calls Cafe GX2 and OS functions by name.
// Those names are HLE imports in this port. This file forwards the calls.
#include "core/runtime.h"
#include <cstring>

extern "C" {
void imp_gx2_GX2SetContextState(Cpu*);
void imp_gx2_GX2Flush(Cpu*);
void imp_gx2_GX2DrawDone(Cpu*);
void imp_gx2_GX2SwapScanBuffers(Cpu*);
void imp_gx2_GX2WaitForVsync(Cpu*);
void imp_gx2_GX2GetSwapStatus(Cpu*);
void imp_gx2_GX2GPUTimeToCPUTime(Cpu*);
void imp_gx2_GX2SampleTopGPUCycle(Cpu*);
void imp_gx2_GX2SampleBottomGPUCycle(Cpu*);
void imp_coreinit_OSGetSystemTime(Cpu*);
}

namespace {
uint32_t guest_ptr(void* p) { return (uint32_t)(uintptr_t)p; }

uint32_t scratch() {
    static uint32_t buf = mem::runtime_alloc(32, 16);
    return buf;
}
}

extern "C" void GX2SetContextState(void* state) {
    Cpu c{};
    c.r[3] = guest_ptr(state);
    imp_gx2_GX2SetContextState(&c);
}
extern "C" void GX2Flush() {
    Cpu c{};
    imp_gx2_GX2Flush(&c);
}
extern "C" void GX2DrawDone() {
    Cpu c{};
    imp_gx2_GX2DrawDone(&c);
}
extern "C" void GX2SwapScanBuffers() {
    Cpu c{};
    imp_gx2_GX2SwapScanBuffers(&c);
}
extern "C" void GX2WaitForVsync() {
    Cpu c{};
    imp_gx2_GX2WaitForVsync(&c);
}
extern "C" void GX2GetSwapStatus(uint32_t* a, uint32_t* b, uint32_t* c0, uint32_t* d) {
    uint32_t buf = scratch();
    Cpu c{};
    c.r[3] = buf;
    c.r[4] = buf + 4;
    c.r[5] = buf + 8;
    c.r[6] = buf + 16;
    imp_gx2_GX2GetSwapStatus(&c);
    if (a) *a = ld32(buf);
    if (b) *b = ld32(buf + 4);
    if (c0) *c0 = (uint32_t)ld64(buf + 8);
    if (d) *d = (uint32_t)ld64(buf + 16);
}
extern "C" uint64_t GX2GPUTimeToCPUTime(uint32_t begin, uint32_t end) {
    Cpu c{};
    c.r[3] = begin;
    c.r[4] = end;
    imp_gx2_GX2GPUTimeToCPUTime(&c);
    return ((uint64_t)c.r[3] << 32) | c.r[4];
}
extern "C" void GX2SampleTopGPUCycle(void* p) {
    Cpu c{};
    c.r[3] = guest_ptr(p);
    imp_gx2_GX2SampleTopGPUCycle(&c);
}
extern "C" void GX2SampleBottomGPUCycle(void* p) {
    Cpu c{};
    c.r[3] = guest_ptr(p);
    imp_gx2_GX2SampleBottomGPUCycle(&c);
}
extern "C" void* OSBlockSet(void* dst, int val, uint32_t size) {
    return memset(dst, val, size);
}
extern "C" void* OSBlockMove(void* dst, const void* src, uint32_t size, int) {
    return memmove(dst, src, size);
}
extern "C" void OSGetSystemTime(uint32_t* hi, uint32_t* lo) {
    Cpu c{};
    imp_coreinit_OSGetSystemTime(&c);
    if (hi) *hi = c.r[3];
    if (lo) *lo = c.r[4];
}
