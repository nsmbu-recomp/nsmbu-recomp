

#include "core/write_watch.h"

#include <atomic>
#include <cassert>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <random>
#include <string>
#include <thread>
#include <vector>

#ifdef _WIN32
#include <windows.h>
#else
#include <sys/mman.h>
#endif

static uint8_t* g_mem;
constexpr uint64_t kSize = 64ull << 20;

static uint64_t old_sparse_hash(uint32_t addr, uint32_t size) {
    uint64_t h = 0xcbf29ce484222325ull;
    uint32_t step = std::max<uint32_t>((size / 256) & ~7u, 8);
    for (uint32_t o = 0; o + 8 <= size; o += step) {
        uint64_t v;
        memcpy(&v, g_mem + addr + o, 8);
        h = (h ^ v) * 0x100000001b3ull;
    }
    return h;
}
static uint64_t full_hash(uint32_t addr, uint32_t size) {
    uint64_t h = 1469598103934665603ull;
    for (uint32_t i = 0; i < size; i++) h = (h ^ g_mem[addr + i]) * 1099511628211ull;
    return h;
}

struct Tex {
    uint32_t base, baseSize, mip, mipSize;
    uint64_t stamp = 0;
    uint64_t hash = 0;
    bool changed() const {
        return wwatch::written_since(base, baseSize, stamp) || wwatch::written_since(mip, mipSize, stamp);
    }
    void check() {
        stamp = std::min(wwatch::arm(base, baseSize), wwatch::arm(mip, mipSize));
        hash = full_hash(base, baseSize) ^ (full_hash(mip, mipSize) * 31);
    }
};

int main() {
#ifdef _WIN32
    g_mem = (uint8_t*)VirtualAlloc(nullptr, kSize, MEM_RESERVE | MEM_COMMIT, PAGE_READWRITE);
#else
    g_mem = (uint8_t*)mmap(nullptr, kSize, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
    if (g_mem == (uint8_t*)MAP_FAILED) g_mem = nullptr;
#endif
    assert(g_mem);
    assert(wwatch::init(g_mem, kSize) && wwatch::active());
    for (uint64_t i = 0; i < kSize; i++) g_mem[i] = (uint8_t)(i * 7);

    Tex t{0x100000, 0x40000, 0x140000, 0x15560};
    t.check();
    assert(!t.changed());
    uint64_t sparse = old_sparse_hash(t.base, t.baseSize);
    g_mem[t.base + 0x400 * 5 + 100] ^= 0xFF;
    assert(old_sparse_hash(t.base, t.baseSize) == sparse);
    assert(t.changed());
    uint32_t found[4];
    uint32_t shift = 0, first = 0, count = 0;
    assert(wwatch::page_span(t.base, t.baseSize, shift, first, count));
    uint32_t n = wwatch::collect_written_pages(t.base, t.baseSize, t.stamp, found, 4);
    uint32_t expect = ((t.base + 0x400 * 5 + 100) >> shift) - first;
    assert(n == 1 && found[0] == expect);
    g_mem[t.base + 0x400 * 5 + 100 + (1u << shift)] ^= 0xFF;
    assert(wwatch::collect_written_pages(t.base, t.baseSize, t.stamp, found, 1) == 0xffffffffu);
    uint64_t before = t.hash;
    t.check();
    assert(t.hash != before && !t.changed());

    g_mem[t.mip + 0x1234] ^= 0x5A;
    assert(t.changed());
    t.check();
    assert(!t.changed());

    Tex other{0x800000, 0x10000, 0x810000, 0x8000};
    other.check();
    for (int i = 0; i < 1000; i++) g_mem[0x2000000 + i * 64] = (uint8_t)i;
    assert(!t.changed() && !other.changed());

    std::mt19937 rng(7);
    uint64_t detected = 0;
    for (int round = 0; round < 200; round++) {
        std::atomic<bool> stop{false};
        std::atomic<uint64_t> writes{0};
        std::thread writer([&, seed = (unsigned)rng()] {
            std::mt19937 r(seed);
            while (!stop.load(std::memory_order_relaxed)) {
                uint32_t o = r() % (t.baseSize + t.mipSize);
                uint32_t a = o < t.baseSize ? t.base + o : t.mip + (o - t.baseSize);
                g_mem[a] = (uint8_t)r();
                writes.fetch_add(1, std::memory_order_relaxed);
            }
        });

        uint64_t target = 1000 + rng() % 20000, seen = 0;
        while (writes.load(std::memory_order_relaxed) < target || seen < 1 + round % 4)
            if (t.changed()) { t.check(); seen++; }
        detected += seen;
        stop = true;
        writer.join();
        if (t.changed()) t.check();
        assert(!t.changed());
        assert(t.hash == (full_hash(t.base, t.baseSize) ^ (full_hash(t.mip, t.mipSize) * 31)));
    }

    auto path = std::filesystem::temp_directory_path() / ("nsmbu-write-watch-" + std::to_string(rng()) + ".bin");
    std::vector<uint8_t> payload(0x8000);
    for (size_t i = 0; i < payload.size(); i++) payload[i] = (uint8_t)(i * 13 + 1);
    FILE* f = fopen(path.string().c_str(), "wb");
    assert(f && fwrite(payload.data(), 1, payload.size(), f) == payload.size());
    fclose(f);
    t.check();
#ifndef _WIN32
    f = fopen(path.string().c_str(), "rb");
    setvbuf(f, nullptr, _IONBF, 0);
    size_t got = fread(g_mem + t.base, 1, payload.size(), f);
    fclose(f);
    assert(got < payload.size());
    if (t.changed()) t.check();
#endif
    {
        f = fopen(path.string().c_str(), "rb");
        setvbuf(f, nullptr, _IONBF, 0);
        wwatch::HostWrite w(t.base, (uint32_t)payload.size());
        assert(fread(g_mem + t.base, 1, payload.size(), f) == payload.size());
        fclose(f);
    }
    assert(!memcmp(g_mem + t.base, payload.data(), payload.size()));
    assert(t.changed());
    t.check();
    assert(!t.changed());
    std::filesystem::remove(path);

    uint64_t faults, pages;
    wwatch::take_stats(faults, pages);
    assert(detected >= 450);
    printf("write_watch_test: in-place texel change between samples, mip-only change, untouched textures, "
           "concurrent writer (200 rounds, %llu changes seen), protected fread passed (%llu write faults, %llu pages protected)\n",
           (unsigned long long)detected, (unsigned long long)faults, (unsigned long long)pages);
    return 0;
}
