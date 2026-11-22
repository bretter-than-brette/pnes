/**
 * @file audio.cpp
 * @brief Audio stub for the GBA: the entry points exist so the library links and
 *        the demo runs silent.
 *
 * 256 KB of EWRAM leaves no room to bake the demo's PCM tracks into the ROM, so
 * per the project's handheld plan a user supplies a synthesiser. A real GBA mixer
 * later touches only this file -- the legacy PSG channels map naturally onto the
 * NES APU.
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
