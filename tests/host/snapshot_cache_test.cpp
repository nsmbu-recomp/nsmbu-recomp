

#include <cassert>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <memory>
#include <vector>

#include "gfx/vulkan/uniform_snapshot.h"
#include "gfx/vulkan/vertex_snapshot_history.h"

using namespace gfxvk;

namespace {

struct Slice {
  uint64_t buffer = 0;
  void* mapped = nullptr;
  size_t size = 0;
};

struct Arena {
  bool poison = true;
  std::vector<std::unique_ptr<uint8_t[]>> mapped;
  std::vector<std::vector<uint8_t>> gpu;
  uint64_t allocations = 0;
  Slice snapshot(const void* data, size_t size) {
    const size_t n = size < 16 ? 16 : size;
    mapped.emplace_back(new uint8_t[n]);
    uint8_t* m = mapped.back().get();
    if (data && size) {
      std::memcpy(m, data, size);
      std::memset(m + size, 0, n - size);
    } else {
      std::memset(m, 0, n);
    }
    gpu.emplace_back(m, m + n);
    if (poison) std::memset(m, 0xCD, n);
    ++allocations;
    return {allocations, m, n};
  }
  const std::vector<uint8_t>& bytes(const Slice& s) const { return gpu.at(s.buffer - 1); }
};

bool slice_holds(const Arena& a, const Slice& s, const void* data, size_t size) {
  return !size || !std::memcmp(a.bytes(s).data(), data, size);
}

struct Hits { uint64_t uniform = 0, vertex = 0, window = 0; };

uint64_t uniform_cache(bool direct) {
  Arena arena;
  arena.poison = !direct;
  UniformSnapshotCache<Slice, int> cache;
  auto make = [&](const void* d, size_t n) { return arena.snapshot(d, n); };
  auto get = [&](int device, uint64_t generation, size_t slot, const void* bytes, size_t size) {
    return cache.get(device, generation, slot, bytes, size, direct, make);
  };
  uint8_t a[64], b[64];
  for (int i = 0; i < 64; ++i) a[i] = uint8_t(i * 7 + 1), b[i] = a[i];
  b[40] ^= 0x55;

  Slice s1 = get(1, 1, 3, a, 64);
  Slice s2 = get(1, 1, 3, a, 64);
  assert(s1.buffer == s2.buffer && arena.allocations == 1);
  assert(cache.counters.hits == 1 && cache.counters.comparisons == 1);
  Slice s3 = get(1, 1, 3, b, 64);
  assert(s3.buffer != s1.buffer && slice_holds(arena, s3, b, 64));
  Slice s4 = get(1, 1, 3, b, 64);
  assert(s4.buffer == s3.buffer && cache.counters.hits == 2);

  Slice s5 = get(1, 1, 3, a, 64);
  assert(s5.buffer != s1.buffer && slice_holds(arena, s5, a, 64) && cache.counters.hits == 2);

  get(1, 1, 3, a, 32);
  assert(cache.counters.checks == 4);

  Slice t1 = get(1, 1, 20, a, 64);
  Slice t2 = get(1, 1, 20, a, 64);
  assert(t1.buffer == t2.buffer);

  uint8_t zero[48] = {}, nonzero[48] = {};
  nonzero[47] = 1;
  Slice z1 = get(1, 1, 5, nullptr, 48);
  Slice z2 = get(1, 1, 5, nullptr, 48);
  Slice z3 = get(1, 1, 5, zero, 48);
  assert(z1.buffer == z2.buffer && z1.buffer == z3.buffer);
  Slice z4 = get(1, 1, 5, nonzero, 48);
  assert(z4.buffer != z1.buffer && slice_holds(arena, z4, nonzero, 48));

  const uint64_t before = arena.allocations;
  get(1, 2, 20, a, 64);
  get(2, 2, 20, a, 64);
  assert(arena.allocations == before + 2);

  const void* seen = nullptr;
  cache.get(2, 2, 7, a, 64, direct,
            [&](const void* d, size_t n) { seen = d; return arena.snapshot(d, n); });

  assert(seen && (direct ? seen == a : seen != a));
  printf("uniform snapshot cache (%s): ok\n", direct ? "direct" : "shadow");
  return cache.counters.hits;
}

uint64_t vertex_history(bool direct) {
  Arena arena;
  arena.poison = !direct;
  VertexSnapshotHistory<Slice> h;
  auto make = [&](const void* d, size_t n) { return arena.snapshot(d, n); };
  std::vector<uint8_t> p(256), q(256);
  for (size_t i = 0; i < p.size(); ++i) p[i] = uint8_t(i * 3), q[i] = uint8_t(i * 5 + 1);
  uint64_t hits = 0;

  auto get = [&](uint32_t address, const std::vector<uint8_t>& data, bool keepHistory) {
    const uint32_t size = uint32_t(data.size());
    auto* candidate = VertexSnapshotHistory<Slice>::matches(h.last, address, size) ? &h.last
                      : keepHistory ? h.secondary(address, size) : nullptr;
    if (candidate && VertexSnapshotHistory<Slice>::equal(*candidate, data.data())) {
      ++hits;
      if (candidate != &h.last) h.promote();
      return h.last.slice;
    }
    return h.remember(address, size, data.data(), keepHistory, direct, make);
  };
  Slice a = get(0x1000, p, true);
  Slice b = get(0x1000, p, true);
  assert(a.buffer == b.buffer && hits == 1 && slice_holds(arena, a, p.data(), p.size()));
  assert(h.last.direct == direct && h.last.bytes.empty() == direct);
  Slice c = get(0x2000, q, true);
  Slice d = get(0x1000, p, true);
  assert(hits == 2 && d.buffer == a.buffer && c.buffer != a.buffer);
  Slice e = get(0x2000, q, true);
  assert(hits == 3 && e.buffer == c.buffer);

  std::vector<uint8_t> q2 = q;
  q2[100] ^= 1;
  Slice f = get(0x2000, q2, true);
  assert(hits == 3 && f.buffer != c.buffer && slice_holds(arena, f, q2.data(), q2.size()));
  Slice g = get(0x2000, q2, true);
  assert(hits == 4 && g.buffer == f.buffer);

  h.reset();
  get(0x1000, p, false);
  get(0x2000, q, false);
  get(0x1000, p, false);
  assert(hits == 4);

  h.reset();
  assert(!h.last.slice.buffer && !h.previous.slice.buffer);
  printf("vertex snapshot history (%s): ok\n", direct ? "direct" : "shadow");
  return hits;
}

uint64_t vertex_window(bool direct) {
  Arena arena;
  arena.poison = !direct;
  uint64_t hits = 0;
  VertexWindowEntry<Slice> entry;
  std::vector<uint8_t> data(512);
  for (size_t i = 0; i < data.size(); ++i) data[i] = uint8_t(i * 11);
  auto get = [&](uint32_t begin, uint32_t length, bool& hit) {
    hit = entry.matches(0x4000, 512, begin, length) && entry.equal(data.data() + begin);
    if (hit) { ++hits; return entry.slice; }
    const uint8_t* copy = entry.remember(0x4000, 512, begin, length, data.data() + begin, direct);
    assert(direct ? copy == data.data() + begin && entry.bytes.empty() : copy == entry.bytes.data());
    std::vector<uint8_t> full(512, 0);
    std::memcpy(full.data() + begin, copy, length);
    entry.slice = arena.snapshot(full.data(), full.size());
    return entry.slice;
  };
  bool hit;
  Slice a = get(64, 128, hit);
  assert(!hit);
  Slice b = get(64, 128, hit);
  assert(hit && a.buffer == b.buffer);
  data[100] ^= 1;
  Slice c = get(64, 128, hit);
  assert(!hit && c.buffer != a.buffer && slice_holds(arena, c, data.data(), 0) &&
         !std::memcmp(arena.bytes(c).data() + 64, data.data() + 64, 128));
  data[300] ^= 1;
  get(64, 128, hit);
  assert(hit);
  get(32, 128, hit);
  assert(!hit);
  entry.clear();
  get(32, 128, hit);
  assert(!hit);
  printf("vertex window entry (%s): ok\n", direct ? "direct" : "shadow");
  return hits;
}

void direct_reads_compare_mapped() {
  Arena arena;
  auto make = [&](const void* d, size_t n) { return arena.snapshot(d, n); };
  uint8_t a[64];
  for (int i = 0; i < 64; ++i) a[i] = uint8_t(i + 1);
  UniformSnapshotCache<Slice, int> cache;
  cache.get(1, 1, 3, a, 64, true, make);
  cache.get(1, 1, 3, a, 64, true, make);
  assert(cache.counters.comparisons == 1 && cache.counters.hits == 0);
  VertexSnapshotHistory<Slice> h;
  h.remember(0x1000, 64, a, true, true, make);
  assert(!VertexSnapshotHistory<Slice>::equal(h.last, a));
  VertexWindowEntry<Slice> w;
  w.remember(0x4000, 64, 0, 64, a, true);
  w.slice = arena.snapshot(a, 64);
  assert(!w.equal(a));
  printf("direct reads compare mapped bytes: ok\n");
}

}

int main() {
  Hits shadow, direct;
  shadow.uniform = uniform_cache(false);
  shadow.vertex = vertex_history(false);
  shadow.window = vertex_window(false);
  direct.uniform = uniform_cache(true);
  direct.vertex = vertex_history(true);
  direct.window = vertex_window(true);

  assert(shadow.uniform == direct.uniform && shadow.vertex == direct.vertex &&
         shadow.window == direct.window);
  printf("identical hits: uniform %llu, vertex %llu, window %llu\n",
         (unsigned long long)shadow.uniform, (unsigned long long)shadow.vertex,
         (unsigned long long)shadow.window);
  direct_reads_compare_mapped();
  printf("snapshot caches: all ok\n");
  return 0;
}
