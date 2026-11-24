
#pragma once
#include <cstdint>

#include "audio/audio_output_mode.h"

namespace audio {

constexpr int kRate = 48000;

void init();
void push(const int16_t* stereo, int frames);
int buffered_frames();
int target_frames();
void stats(uint64_t& underrun, uint64_t& dropped);
void flush();

bool muted();
void set_muted(bool on);
bool mute_env_override();

float volume();
void set_volume(float v);
bool volume_env_override();

OutputSelect::Source output_source();
void set_output_source(OutputSelect::Source s);
bool output_env_override();
const char* output_source_id(OutputSelect::Source s);
const char* output_source_label(OutputSelect::Source s);

}
