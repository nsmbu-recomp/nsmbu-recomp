#pragma once

#include <cstdint>
#include <cstring>
#include <type_traits>

struct Cpu;

namespace decomp {

struct CallFrame {
    uint32_t lr;
    uint32_t ctr;
    uint32_t sp;
    bool live;
};

void bind(Cpu* cpu);
uint32_t guest_gpr(int index);
uint32_t guest_lr();
CallFrame call_begin();
void arg_u32(int slot, uint32_t value);
void arg_f32(int slot, float value);
uint32_t call_end(CallFrame frame, uint32_t fn);
float call_end_f32(CallFrame frame, uint32_t fn);

template <class T>
void push_arg(int& gpr, int& fpr, T value) {
    using U = std::remove_cv_t<std::remove_reference_t<T>>;
    if constexpr (std::is_floating_point_v<U>)
        arg_f32(fpr++, static_cast<float>(value));
    else if constexpr (std::is_pointer_v<U>)
        arg_u32(gpr++, static_cast<uint32_t>(reinterpret_cast<uintptr_t>(value)));
    else
        arg_u32(gpr++, static_cast<uint32_t>(value));
}

template <class R, class... A>
R call(uint32_t fn, A... args) {
    CallFrame frame = call_begin();
    int gpr = 0;
    int fpr = 0;
    (push_arg(gpr, fpr, args), ...);
    if constexpr (std::is_void_v<R>) {
        call_end(frame, fn);
    } else if constexpr (std::is_floating_point_v<R>) {
        return static_cast<R>(call_end_f32(frame, fn));
    } else if constexpr (std::is_pointer_v<R>) {
        return reinterpret_cast<R>(static_cast<uintptr_t>(call_end(frame, fn)));
    } else {
        return static_cast<R>(call_end(frame, fn));
    }
}

struct Be32 {
    unsigned char* p;
    operator uint32_t() const {
        uint32_t raw = 0;
        std::memcpy(&raw, p, 4);
        return __builtin_bswap32(raw);
    }
    Be32& operator=(uint32_t value) {
        value = __builtin_bswap32(value);
        std::memcpy(p, &value, 4);
        return *this;
    }
};

struct Be16 {
    unsigned char* p;
    operator uint16_t() const {
        uint16_t raw = 0;
        std::memcpy(&raw, p, 2);
        return __builtin_bswap16(raw);
    }
    Be16& operator=(uint16_t value) {
        value = __builtin_bswap16(value);
        std::memcpy(p, &value, 2);
        return *this;
    }
};

struct BeF32 {
    unsigned char* p;
    operator float() const {
        uint32_t raw = 0;
        std::memcpy(&raw, p, 4);
        raw = __builtin_bswap32(raw);
        float value = 0;
        std::memcpy(&value, &raw, 4);
        return value;
    }
    BeF32& operator=(float value) {
        uint32_t raw = 0;
        std::memcpy(&raw, &value, 4);
        raw = __builtin_bswap32(raw);
        std::memcpy(p, &raw, 4);
        return *this;
    }
};

template <class T>
struct BePtr {
    unsigned char* p;
    operator T() const {
        uint32_t raw = 0;
        std::memcpy(&raw, p, 4);
        raw = __builtin_bswap32(raw);
        return reinterpret_cast<T>(static_cast<uintptr_t>(raw));
    }
    template <class U>
    explicit operator U*() const {
        uint32_t raw = 0;
        std::memcpy(&raw, p, 4);
        raw = __builtin_bswap32(raw);
        return reinterpret_cast<U*>(static_cast<uintptr_t>(raw));
    }
    BePtr& operator=(T value) {
        uint32_t raw = __builtin_bswap32(static_cast<uint32_t>(reinterpret_cast<uintptr_t>(value)));
        std::memcpy(p, &raw, 4);
        return *this;
    }
};

inline Be32 be32(const void* p) { return Be32{static_cast<unsigned char*>(const_cast<void*>(p))}; }
inline Be32 be32(uintptr_t ea) { return be32(reinterpret_cast<const void*>(ea)); }
inline Be32 be32(uint32_t ea) { return be32(static_cast<uintptr_t>(ea)); }

inline Be16 be16(const void* p) { return Be16{static_cast<unsigned char*>(const_cast<void*>(p))}; }
inline Be16 be16(uintptr_t ea) { return be16(reinterpret_cast<const void*>(ea)); }
inline Be16 be16(uint32_t ea) { return be16(static_cast<uintptr_t>(ea)); }

inline BeF32 bef32(const void* p) { return BeF32{static_cast<unsigned char*>(const_cast<void*>(p))}; }
inline BeF32 bef32(uintptr_t ea) { return bef32(reinterpret_cast<const void*>(ea)); }
inline BeF32 bef32(uint32_t ea) { return bef32(static_cast<uintptr_t>(ea)); }

template <class T>
inline BePtr<T> beptr(const void* p) {
    return BePtr<T>{static_cast<unsigned char*>(const_cast<void*>(p))};
}
template <class T>
inline BePtr<T> beptr(uintptr_t ea) { return beptr<T>(reinterpret_cast<const void*>(ea)); }
template <class T>
inline BePtr<T> beptr(uint32_t ea) { return beptr<T>(static_cast<uintptr_t>(ea)); }

inline uint32_t fn_addr(const void* p) { return static_cast<uint32_t>(be32(const_cast<void*>(p))); }
inline uint32_t fn_addr(uint32_t ea) { return static_cast<uint32_t>(be32(static_cast<uintptr_t>(ea))); }
inline uint32_t as_fn(uint32_t value) { return value; }
inline uint32_t as_fn(Be32 value) { return static_cast<uint32_t>(value); }

template <class R, class... A>
struct GuestFn {
    uint32_t addr;
    R operator()(A... args) const { return call<R>(addr, args...); }
};

template <class R, class... A>
GuestFn<R, A...> make_fn(uint32_t addr) { return GuestFn<R, A...>{addr}; }

}
