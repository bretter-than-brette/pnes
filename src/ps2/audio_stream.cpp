/**
 * @file audio_stream.cpp
 * @brief PS2 music: SPU2-format ADPCM streamed through audsrv and decoded on the
 *        EE. The default backend (-DPS2_MUSIC=stream).
 *
 * The one backend that imposes nothing on the asset -- the shared wav is used at
 * any length, transcoded like every other platform's. The alternatives each give
 * something up: src/ps2/audio.cpp decodes Vorbis on the EE, which is a
 * floating-point MDCT codec on a 294MHz scalar core and is what held this
 * backend at ~55fps; src/ps2/audio_spu2.cpp costs the EE nothing but needs the
 * whole sample resident in ~756KB of sound RAM, so it only suits a short loop.
 *
 * ADPCM on the EE lands much nearer the free end: ~8 integer ops per sample
 * against Vorbis's hundreds of floating-point ones plus cache misses -- a
 * fraction of a percent of the EE against roughly a quarter of it.
 */
#include "internal.hpp"
#include <pnes/audio.hpp>

#include <sifrpc.h>
#include <loadfile.h>
#include <kernel.h>
#include <audsrv.h>
#include <delaythread.h>
#include <sbv_patches.h>
#include <cstring>

#include <pnes/logger.hpp>

#ifndef PS2_STREAM_RATE
#error "PS2_STREAM_RATE must be defined by the build (the rate pc_audio.adp was encoded at)"
#endif

static const unsigned char adpcm_data[] = {
#embed <pc_audio.adp>   // resolved via --embed-dir (set in the ps2 CMake branch)
};
static constexpr int adpcm_len = static_cast<int>(sizeof(adpcm_data));

alignas(16) static const unsigned char audsrv_irx[] = {
#embed <audsrv.irx>
};
static constexpr int audsrv_irx_len = static_cast<int>(sizeof(audsrv_irx));

static constexpr int SAMPLE_RATE = PS2_STREAM_RATE;

static constexpr int CHANNELS    = 2;

static constexpr int CHUNK_FRAMES = 2048;
static constexpr int CHUNK_BYTES  = CHUNK_FRAMES * CHANNELS * static_cast<int>(sizeof(s16));

static constexpr int ADP_HEADER_BYTES  = 16;
static constexpr int ADP_BLOCK_BYTES   = 16;
static constexpr int ADP_BLOCK_SAMPLES = 28;

float *audio::pcm_buffer = nullptr;
u32 audio::pcm_buffer_size = 0;

namespace {

alignas(16) s16 chunk[CHUNK_FRAMES * CHANNELS];

const int F0[5] = {0,  60, 115,  98, 122};
const int F1[5] = {0,   0, -52, -55, -60};

const int totalBlocks = (adpcm_len - ADP_HEADER_BYTES) / ADP_BLOCK_BYTES;

s16 decoded[ADP_BLOCK_SAMPLES];   // the block fill() is currently handing out
int curBlock   = -1;              // which block decoded[] holds
int posInBlock = ADP_BLOCK_SAMPLES;
int h1 = 0, h2 = 0;               // predictor history, carried across blocks
int loopBlock  = 0;               // block the stream restarts at on EOF

bool          running = false;
volatile bool playing = false;
volatile bool paused  = false;

// TrackPlay runs on the main thread while the decode thread is mid-chunk, so it
// only posts the index here. Plain volatile with one writer and one reader.
volatile int pending_track = -1;

void decode_block(const int blockIndex) {
    const unsigned char *blk = adpcm_data + ADP_HEADER_BYTES + blockIndex * ADP_BLOCK_BYTES;

    const int shift = blk[0] & 0x0F;
    int       filt  = (blk[0] >> 4) & 0x0F;
    if (filt > 4) filt = 0;   // the format defines 0..4; be inert on anything else

    for (int n = 0; n < ADP_BLOCK_SAMPLES; n++) {
        const int byte = blk[2 + (n >> 1)];
        const int nib  = ((n & 1) == 0) ? (byte & 0x0F) : (byte >> 4);

        int s = static_cast<s16>(static_cast<u16>(nib << 12));
        s >>= shift;

        int pred = s + ((F0[filt] * h1 + F1[filt] * h2 + 32) >> 6);
        if (pred >  32767) pred =  32767;
        if (pred < -32768) pred = -32768;

        decoded[n] = static_cast<s16>(pred);
        h2 = h1;
        h1 = pred;
    }
}

void seek_to_loop() {
    curBlock   = loopBlock - 1;   // fill() advances before decoding
    posInBlock = ADP_BLOCK_SAMPLES;
    h1 = h2 = 0;
}

void fill() {
    int got = 0;
    while (got < CHUNK_FRAMES) {
        if (posInBlock >= ADP_BLOCK_SAMPLES) {
            int next = curBlock + 1;
            if (next >= totalBlocks) {
                next = loopBlock;
                h1 = h2 = 0;   // see seek_to_loop
            }
            curBlock = next;
            decode_block(curBlock);
            posInBlock = 0;
        }

        int take = CHUNK_FRAMES - got;
        if (const int avail = ADP_BLOCK_SAMPLES - posInBlock; take > avail) take = avail;

        s16       *dst = chunk + got * CHANNELS;
        const s16 *src = decoded + posInBlock;
        for (int i = 0; i < take; i++) {
            dst[i * 2]     = src[i];
            dst[i * 2 + 1] = src[i];
        }

        got        += take;
        posInBlock += take;
    }
}

void audio_thread(void *) {
    while (running) {
        const int req = pending_track;
        if (req >= 0) {
            pending_track = -1;
        }
        if (req >= 0) {
            loopBlock = tracks[req].loop_start > 0.0f
                ? static_cast<int>(tracks[req].loop_start * static_cast<float>(SAMPLE_RATE))
                  / ADP_BLOCK_SAMPLES
                : 0;
            if (loopBlock >= totalBlocks) loopBlock = 0;
            seek_to_loop();
            playing = true;
            paused  = false;
        }

        if (!playing || paused) {
            memset(chunk, 0, sizeof(chunk));
        } else {
            fill();
        }

        audsrv_wait_audio(CHUNK_BYTES);
        audsrv_play_audio(reinterpret_cast<const char *>(chunk), CHUNK_BYTES);

        DelayThread(2000);
    }
    ExitThread();
}

alignas(16) u8 audio_thread_stack[0x4000];

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

    audsrv_fmt_t format;
    format.bits     = 16;
    format.freq     = SAMPLE_RATE;
    format.channels = CHANNELS;

    if (const int err = audsrv_set_format(&format); err < 0) {
        log("audio::Init: audsrv_set_format(%dHz, 16-bit, %dch) rejected (%d), bailing",
            SAMPLE_RATE, CHANNELS, err);
        return;
    }
    audsrv_set_volume(static_cast<int>(music_volume) * MAX_VOLUME / 100);

    if (totalBlocks < 1) {
        log("audio::Init: embedded ADPCM too small (%d bytes), no music", adpcm_len);
        return;
    }
    seek_to_loop();

    running = true;

    ee_thread_t thread{};
    thread.func       = reinterpret_cast<void *>(audio_thread);
    thread.stack      = audio_thread_stack;
    thread.stack_size = sizeof(audio_thread_stack);
    thread.gp_reg     = &_gp;

    ee_thread_status_t mainStatus{};
    const s32 referResult  = ReferThreadStatus(GetThreadId(), &mainStatus);
    const s32 mainPriority = referResult >= 0 ? mainStatus.current_priority : 50;
    thread.initial_priority = mainPriority > 5 ? mainPriority - 5 : 0;
    thread.attr             = 0;
    thread.option           = 0;

    const s32 tid = CreateThread(&thread);
    if (tid < 0) {
        log("audio::Init: CreateThread failed (%d), no music", tid);
        running = false;
        return;
    }
    StartThread(tid, nullptr);

    log("audio::Init: ADPCM stream ready, %d bytes, %d blocks, %dHz mono",
        adpcm_len, totalBlocks, SAMPLE_RATE);
}

void audio::Update() {
}

void audio::TrackVolume(const u8 percent) {
    music_volume = percent > 100 ? 100 : percent;
    audsrv_set_volume(static_cast<int>(music_volume) * MAX_VOLUME / 100);
}

void audio::TrackPlay(const u8 index) {
    if (index >= nTracks) return;
    pending_track = index;
}

void audio::TrackStop() {
    playing = false;
}

void audio::TrackPause(const u8 pause) {
    (void)pause;  // match the SDL/Switch/Wii U/PSP backends: toggle regardless of argument
    paused = !paused;
}

void audio::SfxPlay(u8 /*index*/, u8 /*channel*/, u8 /*volume*/) {}
void audio::SfxSamplePlay(u8 /*index*/) {}
void audio::SfxStop(u8 /*channel*/) {}
void audio::SfxPause(u8 /*channel*/, u8 /*pause*/) {}
