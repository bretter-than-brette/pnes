/**
 * @file audio.cpp
 * @brief Audio stub for DS: the entry points exist so the library links
 *        and the demo runs silent.
 *
 * The DS has 4 MB of main RAM, so there is no room to bake the demo's PCM tracks
 * into the ROM -- per the project's DS plan a user supplies a synthesiser
 * instead. The signatures match the SDL backend exactly, so a real libnds/maxmod
 * mixer later touches only this file.
 */
#include <pnes/audio.hpp>

float *audio::pcm_buffer = nullptr;
u32 audio::pcm_buffer_size = 0;

void audio::Init(u8) {}
void audio::Update() {}

void audio::TrackPlay(u8 /*index*/) {}
void audio::TrackPause(u8 /*pause*/) {}
void audio::TrackStop() {}
void audio::TrackVolume(u8 /*percent*/) {}

void audio::SfxPlay(u8 /*index*/, u8 /*channel*/, u8 /*volume*/) {}
void audio::SfxSamplePlay(u8 /*index*/) {}
void audio::SfxStop(u8 /*channel*/) {}
void audio::SfxPause(u8 /*channel*/, u8 /*pause*/) {}
