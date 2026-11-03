

#pragma once
#include <cstdint>
#include <vector>
#include "buffer_cache_core.h"

namespace gfxvk {
struct UploadSlice;

bool buffer_cache_enabled();
bool buffer_cache_verify();
bufcache::Cache& buffer_cache();

UploadSlice buffer_cache_slice(const bufcache::Entry& entry, uint64_t size);

bool cached_guest_range(uint32_t addr, uint32_t size, int uploadKind, UploadSlice& out,
                        bufcache::Entry** keep = nullptr);
uint64_t buffer_cache_epoch();

bool cached_native_indices(uint32_t addr, uint32_t size, UploadSlice& out, bufcache::Entry*& entry);

void buffer_cache_mismatch(const char* what, const bufcache::Entry& entry, uint32_t size, uint32_t firstDiff);

void buffer_cache_end_frame();
void buffer_cache_invalidate_all();
void buffer_cache_guest_invalidate(uint32_t flags, uint32_t addr, uint32_t size);
void buffer_cache_free(const std::vector<bufcache::Region>& regions);
void buffer_cache_report(double frames);
}
