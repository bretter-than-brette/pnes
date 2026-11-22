/**
 * @file audio.cpp
 * @brief Music playback for the PS2 backend (audsrv).
 *
 * The desktop wav is transcoded to a small Vorbis OGG at build time and embedded
 * in the ELF, decoded here with stb_vorbis and streamed to audsrv from a
 * dedicated EE thread -- audio::Update() is a no-op. The loop decodes one chunk,
 * waits for ring-buffer room with audsrv_wait_audio(), plays it, and repeats.
 *
 * Two alternatives produced total silence on hardware and should not come back
 * without a way to test them in isolation: audsrv_on_fillbuf() signalling a
 * semaphore (with an empty ring there is no drain event, so the thread parks on
 * its first iteration), and dropping the DelayThread() on the assumption
 * audsrv_wait_audio() yields by itself.
 *
 * Ownership rule relied on throughout: the audio thread is the ONLY caller of
 * any stb_vorbis_* or audsrv_* function. stb_vorbis's decoder is not
 * thread-safe, and audsrv's EE-side client is one SifRpcClientData. The public
 * entry points only set flags.
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

#define STB_VORBIS_NO_PUSHDATA_API
#define STB_VORBIS_NO_STDIO
#include <stb_vorbis.c>

#include <pnes/logger.hpp>

static const unsigned char ogg_data[] = {
#embed <pc_audio.ogg>   // resolved via --embed-dir (set in the ps2 CMake branch)
};
static constexpr int ogg_len = static_cast<int>(sizeof(ogg_data));

// audsrv is not BIOS-resident, unlike SIO2MAN/PADMAN: ps2sdk's prebuilt
// audsrv.irx is embedded in the ELF the same way the OGG is.
alignas(16) static const unsigned char audsrv_irx[] = {
#embed <audsrv.irx>
};
static constexpr int audsrv_irx_len = static_cast<int>(sizeof(audsrv_irx));

static constexpr int SAMPLE_RATE = 44100;
static constexpr int CHANNELS    = 2;

static constexpr int CHUNK_FRAMES = 2048;
static constexpr int CHUNK_BYTES  = CHUNK_FRAMES * CHANNELS * static_cast<int>(sizeof(s16));

float *audio::pcm_buffer = nullptr;
u32 audio::pcm_buffer_size = 0;

namespace {

alignas(16) s16 chunk[CHUNK_FRAMES * CHANNELS];

stb_vorbis *vorbis     = nullptr;
u32         loop_frame = 0;      // stb_vorbis_seek() target on EOF; audio thread only
bool        running    = false;

volatile bool playing = false;
volatile bool paused  = false;

// TrackPlay runs on the main thread while the audio thread is mid-decode, and
// stb_vorbis is not safe to call into from two threads -- a seek landing inside
// a decode call corrupts its state, after which every decode returns 0 samples
// forever: permanent silence, no error, no crash. So TrackPlay only posts the
// index here and the audio thread applies it at the top of its own loop.
//
// Plain volatile int, not std::atomic: the EE has no hardware word-CAS, so
// std::atomic drags in a libatomic this toolchain has not wired up. One writer,
// one reader, and an aligned int is already indivisible on this single core.
volatile int pending_track = -1;

void fill() {
    s16 *dst = chunk;
    int got = 0, stalls = 0;
    while (got < CHUNK_FRAMES && stalls < 4) {
        const int n = stb_vorbis_get_samples_short_interleaved(
            vorbis, CHANNELS, dst + got * CHANNELS, (CHUNK_FRAMES - got) * CHANNELS);
        if (n == 0) {
            stb_vorbis_seek(vorbis, loop_frame);
            stalls++;
            continue;
        }
        got += n;
        stalls = 0;
    }
    if (got < CHUNK_FRAMES) {
        memset(dst + got * CHANNELS, 0,
               static_cast<size_t>(CHUNK_FRAMES - got) * CHANNELS * sizeof(s16));
    }
}

void audio_thread(void *) {
    while (running) {
        const int req = pending_track;
        if (req >= 0) {
            pending_track = -1;
        }
        if (req >= 0 && vorbis) {
            stb_vorbis_seek_start(vorbis);
            loop_frame = tracks[req].loop_start > 0.0f
                ? static_cast<u32>(tracks[req].loop_start * static_cast<float>(SAMPLE_RATE))
                : 0;
            playing = true;
            paused  = false;
        }

        if (!vorbis || !playing || paused) {
            memset(chunk, 0, sizeof(chunk));
        } else {
            fill();
        }

        audsrv_wait_audio(CHUNK_BYTES);
        audsrv_play_audio(reinterpret_cast<const char *>(chunk), CHUNK_BYTES);

        // Load-bearing: removing it produced total silence on hardware, so
        // audsrv_wait_audio() evidently does not reliably yield the EE to a
        // lower-priority thread. DelayThread is a real kernel sleep, which is
        // what forces a deterministic yield window back to main every chunk.
        // 2ms against a ~46ms chunk, so nowhere near an underrun -- and it is
        // also why this thread costs frame rate.
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
    audsrv_set_format(&format);
    audsrv_set_volume(static_cast<int>(music_volume) * MAX_VOLUME / 100);

    int err = 0;
    vorbis = stb_vorbis_open_memory(ogg_data, ogg_len, &err, nullptr);
    if (!vorbis) {
        log("audio::Init: vorbis open failed, err=%d, bailing", err);
        return;
    }

    if (nTracks > 0) {
        loop_frame = tracks[0].loop_start > 0.0f
            ? static_cast<u32>(tracks[0].loop_start * static_cast<float>(SAMPLE_RATE))
            : 0;
    }

    running = true;

    ee_thread_t thread{};
    thread.func       = reinterpret_cast<void *>(audio_thread);
    thread.stack      = audio_thread_stack;
    thread.stack_size = sizeof(audio_thread_stack);
    thread.gp_reg     = &_gp;

    // Strictly HIGHER priority than main (smaller number is more urgent), and
    // the reason is worth recording because it looks backwards. A lower-priority
    // decode thread only gets time that actually exists, and WaitSema returns
    // immediately whenever a vblank is already pending -- which is exactly what
    // happens when a frame overruns. This backend rasterises in software and DMAs
    // a 240KB texture every frame, so that slack is not guaranteed at all, and a
    // thread that can only run in it starves precisely when it must not.
    //
    // Safe above main because the thread holds the CPU for one chunk's decode
    // and then blocks. Queried rather than hardcoded: the relationship to main is
    // the whole point.
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
}

void audio::Update() {
}

void audio::TrackVolume(const u8 percent) {
    music_volume = percent > 100 ? 100 : percent;
    audsrv_set_volume(static_cast<int>(music_volume) * MAX_VOLUME / 100);
}

void audio::TrackPlay(const u8 index) {
    if (!vorbis || index >= nTracks) return;
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
