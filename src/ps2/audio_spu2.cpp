/**
 * @file audio_spu2.cpp
 * @brief PS2 music via SPU2 hardware ADPCM (audsrv). Selected by
 *        -DPS2_MUSIC=adpcm; one of three mutually exclusive backends.
 *
 * Costs the EE nothing per frame: the track is encoded to SPU2 ADPCM at build
 * time, uploaded once into the SPU2's own sound RAM, and played on a hardware
 * voice with the loop flag set. No decode thread, no callback, no priority
 * tradeoff -- which is how PS2 titles actually did music.
 *
 * The tradeoff is real: a resident sample has to fit SPU2 RAM. ADPCM is 16 bytes
 * per 28 samples, so ~2MB holds about 166s at 22.05kHz. The current asset is
 * 4m29s and loops whole, so the ceiling is ~13.6kHz mono and the build defaults
 * to 11.025kHz. Better fidelity needs a shorter loop, not a different codec.
 */
#include "internal.hpp"
#include <pnes/audio.hpp>

#include <sifrpc.h>
#include <loadfile.h>
#include <kernel.h>
#include <audsrv.h>
#include <sbv_patches.h>

#include <pnes/logger.hpp>

alignas(16) static unsigned char adpcm_data[] = {
#embed <pc_audio.adp>   // resolved via --embed-dir (set in the ps2 CMake branch)
};
static constexpr int adpcm_len = static_cast<int>(sizeof(adpcm_data));

static constexpr int ADP_HEADER_BYTES = 16;
static constexpr int ADP_BLOCK_BYTES  = 16;

alignas(16) static const unsigned char audsrv_irx[] = {
#embed <audsrv.irx>
};
static constexpr int audsrv_irx_len = static_cast<int>(sizeof(audsrv_irx));

float *audio::pcm_buffer = nullptr;
u32 audio::pcm_buffer_size = 0;

namespace {

// The encoder writes a terminator on the last block; looping needs the loop
// flag there instead, or the voice stops at the end rather than wrapping.
void fix_loop_terminator() {
    if (adpcm_len <= ADP_HEADER_BYTES + ADP_BLOCK_BYTES) return;

    const int blocks = (adpcm_len - ADP_HEADER_BYTES) / ADP_BLOCK_BYTES;
    if (blocks < 1) return;
    const int lastFlag = ADP_HEADER_BYTES + (blocks - 1) * ADP_BLOCK_BYTES + 1;

    adpcm_data[lastFlag] &= static_cast<unsigned char>(~0x04);

    SyncDCache(adpcm_data, adpcm_data + adpcm_len);
}

audsrv_adpcm_t sample{};       // filled by audsrv_load_adpcm from the header
bool           loaded  = false;
int            channel = -1;   // SPU2 voice the track is playing on, -1 if none
bool           paused  = false;

} // namespace

static u8 music_volume = 100;

void audio::Init(u8) {
    SifLoadModule("rom0:LIBSD", 0, nullptr);

    sbv_patch_enable_lmb();
    sbv_patch_disable_prefix_check();

    SifExecModuleBuffer(const_cast<unsigned char *>(audsrv_irx), audsrv_irx_len, 0, nullptr, nullptr);

    if (audsrv_init() != 0) {
        log("audio::Init: audsrv_init failed, bailing");
        return;
    }

    if (const int err = audsrv_adpcm_init(); err < 0) {
        log("audio::Init: audsrv_adpcm_init failed (%d), bailing", err);
        return;
    }
    audsrv_set_volume(static_cast<int>(music_volume) * MAX_VOLUME / 100);

    fix_loop_terminator();

    const int loadResult = audsrv_load_adpcm(&sample, adpcm_data, adpcm_len);
    if (loadResult < 0) {   // see the note on audsrv_adpcm_init above
        log("audio::Init: audsrv_load_adpcm failed (%d) for %d bytes, no music", loadResult, adpcm_len);
        return;
    }

    const int rawLoop = sample.loop;   // what audsrv left it as; logged below

    sample.loop = 1;

    loaded = true;
    log("audio::Init: SPU2 ADPCM loaded, %d bytes, ret=%d, pitch=%d loop=%d(raw %d) size=%d",
        adpcm_len, loadResult, sample.pitch, sample.loop, rawLoop, sample.size);
}

void audio::Update() {
}

void audio::TrackVolume(const u8 percent) {
    music_volume = percent > 100 ? 100 : percent;
    audsrv_set_volume(static_cast<int>(music_volume) * MAX_VOLUME / 100);
}

void audio::TrackPlay(const u8 index) {
    if (!loaded || index >= nTracks) return;

    if (channel < 0) {
        channel = audsrv_ch_play_adpcm(-1, &sample);   // -1: any free voice
        if (channel < 0) {
            log("audio::TrackPlay: audsrv_ch_play_adpcm failed (%d)", channel);
            return;
        }
    }

    audsrv_adpcm_set_volume_and_pan(channel, MAX_VOLUME, 0);
    paused = false;
}

void audio::TrackStop() {
    if (channel < 0) return;

    audsrv_adpcm_set_volume_and_pan(channel, 0, 0);
}

void audio::TrackPause(const u8 pause) {
    (void)pause;  // match the SDL/Switch/Wii U/PSP backends: toggle regardless of argument
    if (channel < 0) return;

    paused = !paused;
    audsrv_adpcm_set_volume_and_pan(channel, paused ? 0 : MAX_VOLUME, 0);
}

void audio::SfxPlay(u8 /*index*/, u8 /*channel*/, u8 /*volume*/) {}
void audio::SfxSamplePlay(u8 /*index*/) {}
void audio::SfxStop(u8 /*channel*/) {}
void audio::SfxPause(u8 /*channel*/, u8 /*pause*/) {}
