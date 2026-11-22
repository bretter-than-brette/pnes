/**
 * @file audio.cpp
 * @brief Music and sound effects for the 3DS backend (ndsp).
 *
 * This backend mixes nothing in software, which is why it looks unlike the
 * Switch's despite the two consoles being close cousins: ndsp exposes 24
 * independent hardware voices, each with its own rate, output mix and pause bit.
 * So the shape is libogc's -- voice 0 carries the music, voices 1.. carry one
 * effect each -- and every level the API takes is handed to the DSP rather than
 * multiplied into samples. TrackVolume is a mix on voice 0 alone, SfxStop and
 * SfxPause are exact and nearly free, and an effect starts on the next DSP tick
 * (~5ms) rather than waiting for the music chunk it was mixed into.
 *
 * Music streams as Vorbis off the RomFS, decoded on a dedicated thread: raw PCM
 * would be ~32MB, more than an o3DS gives a .3dsx altogether. Effects are raw
 * PCM in the RomFS, loaded once into LINEAR memory -- the DSP reads a voice's
 * buffer by DMA and cannot see the regular heap.
 */
#include "internal.hpp"
#include <pnes/audio.hpp>

#include <3ds.h>
#include <cstdio>
#include <cstring>
#include <malloc.h>

#define STB_VORBIS_NO_PUSHDATA_API
#include <stb_vorbis.c>

#include <pnes/logger.hpp>

// The rate the ctr branch encodes both asset kinds at: ndsp's own native rate,
// so nothing resamples.

static constexpr float SFX_RATE = 32728.0f;
static constexpr u32   CHANNELS = 2;
static constexpr u32   BYTES_PER_FRAME = CHANNELS * sizeof(i16);

static constexpr int MUSIC_VOICE    = 0;
static constexpr int SFX_VOICE_BASE = MUSIC_VOICE + 1;
static constexpr int N_SFX_VOICES   = audio::sfxChannels;
static_assert(SFX_VOICE_BASE + N_SFX_VOICES <= 24,
              "audio::sfxChannels asks for more ndsp voices than the DSP has "
              "(24) once the music has taken voice 0");

static constexpr u32 CHUNK_FRAMES = 2048;
static constexpr int N_BUF        = 2;

float *audio::pcm_buffer = nullptr;
u32 audio::pcm_buffer_size = 0;

namespace {

stb_vorbis *vorbis     = nullptr;
u32         loop_frame = 0;         // stb_vorbis_seek() target on EOF

bool ndsp_ready  = false;
bool music_ready = false;

// TrackPlay runs on the main thread while the decode thread is inside
// stb_vorbis, which is not safe to call into twice, so it only posts the index
// here. Plain volatile with one writer and one reader.
volatile int  pending_track = -1;
volatile bool playing       = false;
volatile bool music_paused = false;

volatile u8 volume = 100;

i16         *chunk[N_BUF]   = {};
ndspWaveBuf  wavebuf[N_BUF] = {};

struct sfx_runtime_t { i16 *data; u32 frames; };
sfx_runtime_t sfx_info[256] = {};
ndspWaveBuf   sfx_wavebuf[N_SFX_VOICES] = {};

Thread        athread = nullptr;
LightEvent    audio_event;
volatile bool running = false;

void romfs_path(const char *fp, const char *ext, char *out, const size_t cap) {
    snprintf(out, cap, "romfs:/%s", fp);
    char *const dot = strrchr(out, '.');
    const size_t n = strlen(ext) + 1;  // incl. NUL
    if (dot && (cap - static_cast<size_t>(dot - out)) >= n) memcpy(dot, ext, n);
}

void set_voice_level(const int id, const float level) {
    float mix[12] = {};
    mix[0] = level;
    mix[1] = level;
    ndspChnSetMix(id, mix);
}

void setup_music_voice(const float rate) {
    ndspChnWaveBufClear(MUSIC_VOICE);
    ndspChnReset(MUSIC_VOICE);
    for (auto &wb : wavebuf) wb.status = NDSP_WBUF_DONE;
    ndspChnSetInterp(MUSIC_VOICE, NDSP_INTERP_POLYPHASE);
    ndspChnSetRate(MUSIC_VOICE, rate);
    ndspChnSetFormat(MUSIC_VOICE, NDSP_FORMAT_STEREO_PCM16);
    set_voice_level(MUSIC_VOICE, static_cast<float>(volume) / 100.0f);
}

void open_track(const u8 index) {
    if (index >= nTracks) return;

    if (vorbis) { stb_vorbis_close(vorbis); vorbis = nullptr; }

    char path[300];
    romfs_path(tracks[index].fp, ".ogg", path, sizeof(path));

    int err = 0;
    vorbis = stb_vorbis_open_filename(path, &err, nullptr);
    if (!vorbis) return;

    const stb_vorbis_info info = stb_vorbis_get_info(vorbis);
    loop_frame = tracks[index].loop_start > 0.0f
        ? static_cast<u32>(tracks[index].loop_start * static_cast<float>(info.sample_rate))
        : 0;

    setup_music_voice(static_cast<float>(info.sample_rate));
}

void fill(const int which) {
    i16 *const dst = chunk[which];
    u32 got = 0;
    int stalls = 0;
    while (got < CHUNK_FRAMES && stalls < 4) {
        const int n = stb_vorbis_get_samples_short_interleaved(
            vorbis, static_cast<int>(CHANNELS), dst + got * CHANNELS,
            static_cast<int>((CHUNK_FRAMES - got) * CHANNELS));
        if (n <= 0) {
            stb_vorbis_seek(vorbis, loop_frame);
            stalls++;
            continue;
        }
        got += static_cast<u32>(n);
        stalls = 0;
    }
    if (got < CHUNK_FRAMES)
        memset(dst + got * CHANNELS, 0, (CHUNK_FRAMES - got) * BYTES_PER_FRAME);
}

void ndsp_frame(void *) { LightEvent_Signal(&audio_event); }

void audio_thread(void *) {
    while (running) {
        LightEvent_Wait(&audio_event);

        const int req = pending_track;
        if (req >= 0) {
            pending_track = -1;
            open_track(static_cast<u8>(req));
            if (vorbis) {
                stb_vorbis_seek_start(vorbis);
                playing = true;
                ndspChnSetPaused(MUSIC_VOICE, false);
            }
        }

        for (int i = 0; i < N_BUF; i++) {
            if (wavebuf[i].status != NDSP_WBUF_FREE &&
                wavebuf[i].status != NDSP_WBUF_DONE)
                continue;

            if (!vorbis || !playing)
                memset(chunk[i], 0, CHUNK_FRAMES * BYTES_PER_FRAME);
            else
                fill(i);

            DSP_FlushDataCache(chunk[i], CHUNK_FRAMES * BYTES_PER_FRAME);

            wavebuf[i].data_pcm16 = chunk[i];
            wavebuf[i].nsamples   = CHUNK_FRAMES;
            wavebuf[i].looping    = false;
            ndspChnWaveBufAdd(MUSIC_VOICE, &wavebuf[i]);
        }
    }
}

void load_sfx() {
    log("load_sfx: nSfx=%u sfxs=%p", static_cast<unsigned>(nSfx),
          static_cast<const void *>(sfxs));
    for (u8 i = 0; i < nSfx; i++) {
        char path[300];
        romfs_path(sfxs[i].fp, ".raw", path, sizeof(path));

        FILE *const f = fopen(path, "rb");
        if (!f) { log("load_sfx: %s -- fopen failed", path); continue; }  // stays silent
        fseek(f, 0, SEEK_END);
        const long sz = ftell(f);
        fseek(f, 0, SEEK_SET);
        if (sz <= 0) { fclose(f); log("load_sfx: %s -- empty", path); continue; }

        const u32 bytes = static_cast<u32>(sz) & ~(BYTES_PER_FRAME - 1);
        if (bytes == 0) { fclose(f); continue; }
        i16 *const data = static_cast<i16 *>(linearAlloc(bytes));
        if (!data) { fclose(f); log("load_sfx: %s -- linearAlloc(%lu) failed", path,
                                      static_cast<unsigned long>(bytes)); continue; }

        const bool ok = fread(data, 1, bytes, f) == bytes;
        fclose(f);
        if (!ok) { linearFree(data); log("load_sfx: %s -- short read", path); continue; }

        DSP_FlushDataCache(data, bytes);
        sfx_info[i] = { data, bytes / BYTES_PER_FRAME };
        log("load_sfx: %s -- %lu frames", path,
              static_cast<unsigned long>(sfx_info[i].frames));
    }
}

void setup_sfx_voice(const int channel) {
    const int id = SFX_VOICE_BASE + channel;
    ndspChnReset(id);
    ndspChnSetInterp(id, NDSP_INTERP_LINEAR);
    ndspChnSetRate(id, SFX_RATE);
    ndspChnSetFormat(id, NDSP_FORMAT_STEREO_PCM16);
}

/// libctru loads the DSP firmware from the SD card, and Nintendo's cannot be
/// redistributed -- so a player without a dump of their own console's copy gets
/// a mute game. Leaves a plain-text note on the card saying so, release builds
/// included, since the alternative is silence with no explanation.
void note_missing_dsp_firmware(const Result rc) {
    FILE *const f = std::fopen("sdmc:/pnes_no_audio.txt", "w");
    if (!f) return;
    std::fprintf(f,
        "pnes: no audio on this 3DS.\n\n"
        "ndspInit() failed (0x%08lX): the DSP firmware is missing.\n"
        "libctru loads it from  sdmc:/3ds/dspfirm.cdc  and there is no\n"
        "software fallback, so the game runs silently without it.\n\n"
        "To fix it, dump the firmware from this console once:\n"
        "  Luma3DS: Rosalina menu -> Miscellaneous options -> Dump DSP firmware\n"
        "  or run the DSP1 homebrew app, which writes the same file.\n\n"
        "The dump stays valid forever and every homebrew game with sound\n"
        "uses it -- it is not specific to this one.\n",
        static_cast<unsigned long>(rc));
    std::fclose(f);
}

} // namespace

void audio::Init(u8) {
    const Result romfs_rc = romfsInit();
    log("Init: romfsInit -> 0x%08lX", static_cast<unsigned long>(romfs_rc));

    const Result ndsp_rc = ndspInit();
    log("Init: ndspInit -> 0x%08lX", static_cast<unsigned long>(ndsp_rc));
    if (R_FAILED(ndsp_rc)) {
        note_missing_dsp_firmware(ndsp_rc);
        return;
    }
    ndspSetOutputMode(NDSP_OUTPUT_STEREO);

    load_sfx();
    for (int i = 0; i < N_SFX_VOICES; i++) setup_sfx_voice(i);

    setup_music_voice(SFX_RATE);

    ndsp_ready = true;

    for (int i = 0; i < N_BUF; i++) {
        chunk[i] = static_cast<i16 *>(linearAlloc(CHUNK_FRAMES * BYTES_PER_FRAME));
        if (!chunk[i]) { log("Init: music chunk linearAlloc failed"); return; }
        memset(chunk[i], 0, CHUNK_FRAMES * BYTES_PER_FRAME);
        wavebuf[i] = {};
        wavebuf[i].status = NDSP_WBUF_DONE;   // free for the thread's first pass
    }

    LightEvent_Init(&audio_event, RESET_ONESHOT);
    ndspSetCallback(ndsp_frame, nullptr);

    s32 prio = 0x30;
    svcGetThreadPriority(&prio, CUR_THREAD_HANDLE);
    prio -= 1;
    if (prio < 0x18) prio = 0x18;

    running = true;
    athread = threadCreate(audio_thread, nullptr, 64 * 1024, prio, -1, false);
    if (!athread) { running = false; log("Init: threadCreate(prio %ld) failed",
                                           static_cast<long>(prio)); return; }

    music_ready = true;
    log("Init: ready (sfx + music), thread prio %ld", static_cast<long>(prio));
}

void audio::Update() {
}

void audio::TrackPlay(const u8 index) {
    if (!music_ready || index >= nTracks) return;
    pending_track = index;
}

void audio::TrackStop() {
    playing = false;
}

void audio::TrackVolume(const u8 percent) {
    volume = percent > 100 ? 100 : percent;
    if (ndsp_ready) set_voice_level(MUSIC_VOICE, static_cast<float>(volume) / 100.0f);
}

void audio::TrackPause(const u8 pause) {
    (void)pause;  // match the SDL/Switch/PSP backends: toggle regardless of argument
    if (!music_ready) return;
    music_paused = !music_paused;
    ndspChnSetPaused(MUSIC_VOICE, music_paused);
}

void audio::SfxPlay(const u8 index, const u8 channel, const u8 volume) {
#ifndef NDEBUG
    static int logged = 0;
    if (logged < 8) {
        logged++;
        log("SfxPlay: index=%u channel=%u volume=%u ndsp_ready=%d nSfx=%u frames=%lu",
              static_cast<unsigned>(index), static_cast<unsigned>(channel),
              static_cast<unsigned>(volume), ndsp_ready ? 1 : 0,
              static_cast<unsigned>(nSfx),
              static_cast<unsigned long>(index < nSfx ? sfx_info[index].frames : 0));
    }
#endif
    if (!ndsp_ready || index >= nSfx || channel >= N_SFX_VOICES) return;
    if (!sfx_info[index].frames) return;        // not in the image

    const int id = SFX_VOICE_BASE + channel;

    if (ndspChnIsPlaying(id)) {
        ndspChnWaveBufClear(id);
        ndspChnSetPaused(id, false);
    }
    set_voice_level(id, static_cast<float>(volume) / 255.0f);

    sfx_wavebuf[channel] = {};
    sfx_wavebuf[channel].data_pcm16 = sfx_info[index].data;
    sfx_wavebuf[channel].nsamples   = sfx_info[index].frames;
    sfx_wavebuf[channel].looping    = false;
    ndspChnWaveBufAdd(id, &sfx_wavebuf[channel]);
}

void audio::SfxSamplePlay(const u8 index) { audio::SfxPlay(index, 1, 0xFF); }

void audio::SfxStop(const u8 channel) {
    if (!ndsp_ready || channel >= N_SFX_VOICES) return;
    ndspChnWaveBufClear(SFX_VOICE_BASE + channel);
    ndspChnSetPaused(SFX_VOICE_BASE + channel, false);
}

void audio::SfxPause(const u8 channel, const u8 pause) {
    if (!ndsp_ready || channel >= N_SFX_VOICES) return;
    ndspChnSetPaused(SFX_VOICE_BASE + channel, pause != 0);
}
