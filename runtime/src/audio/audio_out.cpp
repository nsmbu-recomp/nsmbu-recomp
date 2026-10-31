
#include "audio/audio_out.h"

#include "core/env.h"
#if defined(__APPLE__) && !defined(NSMBU_SDL_HOST)
#include <AudioToolbox/AudioToolbox.h>
#else
#include <SDL3/SDL.h>
#endif

#include <atomic>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <mutex>
#include <thread>
#include <vector>

#include "overlay/hostui.h"
#include "core/runtime.h"

namespace audio {
namespace {

constexpr int kCapacity = 1 << 15;
constexpr int kTarget = kRate * 40 / 1000;

int16_t g_ring[kCapacity * 2];
std::atomic<uint32_t> g_read{0}, g_write{0};
std::atomic<bool> g_started{false};
std::atomic<bool> g_flush{false};
std::atomic<bool> g_silent{false};
std::atomic<bool> g_muted{false};
std::atomic<float> g_volume{1.0f};
std::atomic<int> g_source{(int)OutputSelect::kAuto};
#if defined(__APPLE__) && !defined(NSMBU_SDL_HOST)
AudioComponentInstance g_unit = nullptr;
#if TARGET_OS_IPHONE
std::atomic<float> g_gain{1.0f}; // RemoteIO has no volume parameter; render() scales the samples
#endif
#else
SDL_AudioStream* g_unit = nullptr;
#endif

std::atomic<uint64_t> g_underrun{0}, g_dropped{0};

FILE* g_dump = nullptr;
uint32_t g_dump_frames = 0;

float clamp_volume(float v) {
    if (!(v == v)) return 1.0f;
    if (v < 0.0f) return 0.0f;
    if (v > 1.0f) return 1.0f;
    return v;
}

void apply_device_gain() {
    const float gain = g_muted.load(std::memory_order_relaxed) ? 0.0f : g_volume.load(std::memory_order_relaxed);
#if defined(__APPLE__) && !defined(NSMBU_SDL_HOST)
#if TARGET_OS_IPHONE
    g_gain.store(gain, std::memory_order_relaxed);
#else
    if (g_unit)
        AudioUnitSetParameter(g_unit, kHALOutputParam_Volume, kAudioUnitScope_Global, 0, (AudioUnitParameterValue)gain, 0);
#endif
#else
    if (g_unit) SDL_SetAudioStreamGain(g_unit, gain);
#endif
}

void write_wav_header() {
    uint32_t data = g_dump_frames * 4;
    uint8_t h[44];
    auto u32 = [&](int o, uint32_t v) { memcpy(h + o, &v, 4); };
    auto u16 = [&](int o, uint16_t v) { memcpy(h + o, &v, 2); };
    memcpy(h, "RIFF", 4);
    u32(4, 36 + data);
    memcpy(h + 8, "WAVEfmt ", 8);
    u32(16, 16);
    u16(20, 1);
    u16(22, 2);
    u32(24, kRate);
    u32(28, kRate * 4);
    u16(32, 4);
    u16(34, 16);
    memcpy(h + 36, "data", 4);
    u32(40, data);
    long pos = ftell(g_dump);
    fseek(g_dump, 0, SEEK_SET);
    fwrite(h, 1, 44, g_dump);
    fseek(g_dump, pos, SEEK_SET);
    fflush(g_dump);
}

void pull(int16_t* out, uint32_t frames) {
    uint32_t r = g_read.load(std::memory_order_relaxed), w = g_write.load(std::memory_order_acquire);
    if (g_flush.exchange(false)) r = w;
    uint32_t avail = w - r, n = std::min<uint32_t>(avail, frames);
    for (uint32_t i = 0; i < n; i++) {
        uint32_t idx = (r + i) & (kCapacity - 1);
        out[i * 2] = g_ring[idx * 2];
        out[i * 2 + 1] = g_ring[idx * 2 + 1];
    }
    if (n < frames) {
        memset(out + n * 2, 0, (frames - n) * 4);
        g_underrun += frames - n;
    }
    g_read.store(r + n, std::memory_order_release);
}
#if defined(__APPLE__) && !defined(NSMBU_SDL_HOST)
OSStatus render(void*, AudioUnitRenderActionFlags*, const AudioTimeStamp*, UInt32, UInt32 frames, AudioBufferList* io) {
    pull((int16_t*)io->mBuffers[0].mData, frames);
#if TARGET_OS_IPHONE
    const float gain = g_gain.load(std::memory_order_relaxed);
    if (gain < 1.0f) {
        int16_t* s = (int16_t*)io->mBuffers[0].mData;
        for (UInt32 i = 0; i < frames * 2; i++) s[i] = (int16_t)(s[i] * gain);
    }
#endif
    return noErr;
}
#else
void SDLCALL render(void*, SDL_AudioStream* stream, int additional, int) {
    int16_t samples[1024 * 2];
    while (additional > 0) {
        uint32_t frames = std::min(additional / 4, 1024);
        if (!frames) break;
        pull(samples, frames);
        if (!SDL_PutAudioStreamData(stream, samples, (int)frames * 4)) break;
        additional -= (int)frames * 4;
    }
}
#endif

void silent_clock() {
    const int frames = kRate * 3 / 1000;
    std::vector<int16_t> buf((size_t)frames * 2);
    auto next = std::chrono::steady_clock::now();
    while (g_silent.load(std::memory_order_acquire)) {
        next += std::chrono::milliseconds(3);
        pull(buf.data(), (uint32_t)frames);
        std::this_thread::sleep_until(next);
        auto now = std::chrono::steady_clock::now();
        if (now - next > std::chrono::milliseconds(30)) next = now;
    }
}

void load_prefs() {
    std::string v;
    if (const char* e = env::AUDIO_VOLUME()) {
        g_volume.store(clamp_volume((float)atof(e)), std::memory_order_relaxed);
    } else if (hostui::get("audioVolume", v) && !v.empty()) {
        g_volume.store(clamp_volume((float)atof(v.c_str())), std::memory_order_relaxed);
    }
    if (env::NO_AUDIO()) {
        g_muted.store(true, std::memory_order_relaxed);
    } else if (hostui::get("audioMute", v)) {
        g_muted.store(v == "1", std::memory_order_relaxed);
    }
    if (const char* e = env::AUDIO_OUTPUT()) {
        g_source.store((int)OutputSelect::parse(e), std::memory_order_relaxed);
    } else if (hostui::get("audioOutput", v) && !v.empty()) {
        g_source.store((int)OutputSelect::parse(v.c_str()), std::memory_order_relaxed);
    }
}

}

void init() {
    if (g_started.exchange(true)) return;
    load_prefs();
    if (const char* p = env::AUDIO_DUMP()) {
        g_dump = fopen(p, "wb");
        if (g_dump) {
            write_wav_header();
            fseek(g_dump, 44, SEEK_SET);
        }
    }
    if (env::NO_AUDIO()) {
        g_silent.store(true, std::memory_order_release);
        std::thread(silent_clock).detach();
        LOG("[audio] output off");
        return;
    }

#if defined(__APPLE__) && !defined(NSMBU_SDL_HOST)
    AudioComponentDescription desc{};
    desc.componentType = kAudioUnitType_Output;
#if TARGET_OS_IPHONE
    desc.componentSubType = kAudioUnitSubType_RemoteIO;
#else
    desc.componentSubType = kAudioUnitSubType_DefaultOutput;
#endif
    desc.componentManufacturer = kAudioUnitManufacturer_Apple;
    AudioComponent comp = AudioComponentFindNext(nullptr, &desc);
    if (!comp || AudioComponentInstanceNew(comp, &g_unit) != noErr) {
        LOG("[audio] no output device");
        g_unit = nullptr;
        return;
    }
    AudioStreamBasicDescription fmt{};
    fmt.mSampleRate = kRate;
    fmt.mFormatID = kAudioFormatLinearPCM;
    fmt.mFormatFlags = kAudioFormatFlagIsSignedInteger | kAudioFormatFlagIsPacked;
    fmt.mChannelsPerFrame = 2;
    fmt.mBitsPerChannel = 16;
    fmt.mFramesPerPacket = 1;
    fmt.mBytesPerFrame = 4;
    fmt.mBytesPerPacket = 4;
    AudioUnitSetProperty(g_unit, kAudioUnitProperty_StreamFormat, kAudioUnitScope_Input, 0, &fmt, sizeof fmt);
    AURenderCallbackStruct cb{render, nullptr};
    AudioUnitSetProperty(g_unit, kAudioUnitProperty_SetRenderCallback, kAudioUnitScope_Input, 0, &cb, sizeof cb);
    apply_device_gain();
    if (AudioUnitInitialize(g_unit) != noErr || AudioOutputUnitStart(g_unit) != noErr) {
        LOG("[audio] failed to start output unit");
        return;
    }
    LOG("[audio] CoreAudio output started (48 kHz stereo)");
#else
    if (!SDL_InitSubSystem(SDL_INIT_AUDIO)) {
        LOG("[audio] SDL audio initialization: %s", SDL_GetError());
        return;
    }
    SDL_AudioSpec spec{SDL_AUDIO_S16, 2, kRate};
    g_unit = SDL_OpenAudioDeviceStream(SDL_AUDIO_DEVICE_DEFAULT_PLAYBACK, &spec, render, nullptr);
    if (!g_unit) {
        LOG("[audio] no output device: %s", SDL_GetError());
        return;
    }
    apply_device_gain();
    if (!SDL_ResumeAudioStreamDevice(g_unit)) {
        LOG("[audio] failed to start: %s", SDL_GetError());
        SDL_DestroyAudioStream(g_unit);
        g_unit = nullptr;
        return;
    }
    LOG("[audio] SDL output started (48 kHz stereo)");
#endif
}

void push(const int16_t* stereo, int frames) {
    if (g_dump) {
        fwrite(stereo, 4, frames, g_dump);
        g_dump_frames += frames;
        if (g_dump_frames % kRate < (uint32_t)frames) write_wav_header();
    }
    if (!g_unit && !g_silent.load(std::memory_order_acquire)) return;
    uint32_t w = g_write.load(std::memory_order_relaxed), r = g_read.load(std::memory_order_acquire);
    if (w - r + frames > kCapacity) {
        g_dropped += frames;
        return;
    }
    for (int i = 0; i < frames; i++) {
        uint32_t idx = (w + i) & (kCapacity - 1);
        g_ring[idx * 2] = stereo[i * 2];
        g_ring[idx * 2 + 1] = stereo[i * 2 + 1];
    }
    g_write.store(w + frames, std::memory_order_release);
}

int buffered_frames() {
    if (!g_unit && !g_silent.load(std::memory_order_acquire)) return kTarget;
    return (int)(g_write.load(std::memory_order_acquire) - g_read.load(std::memory_order_acquire));
}

int target_frames() { return kTarget; }

void flush() { g_flush = true; }

void stats(uint64_t& underrun, uint64_t& dropped) {
    underrun = g_underrun;
    dropped = g_dropped;
}

bool muted() { return g_muted.load(std::memory_order_relaxed); }
void set_muted(bool on) {
    if (mute_env_override()) return;
    if (g_muted.exchange(on, std::memory_order_relaxed) != on) {
        LOG("[audio] mute %s", on ? "on" : "off");
        apply_device_gain();
    }
}
bool mute_env_override() { return env::NO_AUDIO() != nullptr; }

float volume() { return g_volume.load(std::memory_order_relaxed); }
void set_volume(float v) {
    if (volume_env_override()) return;
    float n = clamp_volume(v);
    float prev = g_volume.exchange(n, std::memory_order_relaxed);
    if (prev != n) apply_device_gain();
}
bool volume_env_override() { return env::AUDIO_VOLUME() != nullptr; }

OutputSelect::Source output_source() {
    return (OutputSelect::Source)g_source.load(std::memory_order_relaxed);
}
void set_output_source(OutputSelect::Source s) {
    if (output_env_override()) return;
    int prev = g_source.exchange((int)s, std::memory_order_relaxed);
    if (prev != (int)s) LOG("[audio] output %s", output_source_label(s));
}
bool output_env_override() { return env::AUDIO_OUTPUT() != nullptr; }

const char* output_source_id(OutputSelect::Source s) {
    switch (s) {
    case OutputSelect::kTv: return "tv";
    case OutputSelect::kGamePad: return "gamepad";
    default: return "auto";
    }
}
const char* output_source_label(OutputSelect::Source s) {
    switch (s) {
    case OutputSelect::kTv: return "TV";
    case OutputSelect::kGamePad: return "GamePad";
    default: return "Auto (Off-TV Play)";
    }
}

}
