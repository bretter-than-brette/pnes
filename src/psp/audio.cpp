/**
 * @file audio.cpp
 * @brief Music playback for the PSP backend (pspaudio).
 *
 * 32-64MB of RAM cannot hold the source decoded, so the wav is transcoded to a
 * small Vorbis OGG at build time, embedded in the EBOOT, and decoded here with
 * stb_vorbis. Unlike the GameCube this console has real threads and a blocking
 * output call, so a dedicated thread decodes one chunk and hands it to
 * sceAudioOutputBlocking() -- which blocks until the hardware has consumed the
 * PREVIOUS chunk, so the loop paces itself. Update() is a no-op.
 *
 * No SFX path yet, and not for want of effects: a project does register a table
 * off NES and the menus play from it, so what is missing is this backend's own
 * decode and mix.
 */
#include "internal.hpp"
#include <pnes/audio.hpp>

#include <pspkernel.h>
#include <pspaudio.h>
#include <cstring>

#define STB_VORBIS_NO_PUSHDATA_API
#define STB_VORBIS_NO_STDIO
#include <stb_vorbis.c>

static const unsigned char ogg_data[] = {
#embed <pc_audio.ogg>   // resolved via --embed-dir (set in the psp CMake branch)
};
static constexpr int ogg_len = static_cast<int>(sizeof(ogg_data));

static constexpr int SAMPLE_RATE = 44100;
static constexpr int CHANNELS    = 2;

static constexpr int CHANNEL = 0;
static constexpr int CHUNK_FRAMES = 2048;

float *audio::pcm_buffer = nullptr;
u32 audio::pcm_buffer_size = 0;

namespace {

i16 chunk[2][CHUNK_FRAMES * CHANNELS];

stb_vorbis *vorbis     = nullptr;
u32         loop_frame = 0;      // stb_vorbis_seek() target on EOF
bool        playing    = false;
bool        paused     = false;

SceUID      athread;
bool        running = false;

// TrackPlay runs on the main thread while the decode thread is inside
// stb_vorbis, which is not safe to call into twice, so it only posts the index
// here. One writer, one reader, and an aligned int is indivisible here.
volatile int pending_track = -1;

volatile u8 volume = 100;

void fill(const int which) {
    i16 *dst = chunk[which];
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
               static_cast<size_t>(CHUNK_FRAMES - got) * CHANNELS * sizeof(i16));
    }
}

int audio_thread(SceSize, void *) {
    int next = 0;
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
            memset(chunk[next], 0, sizeof(chunk[next]));
        } else {
            fill(next);
        }
        sceAudioOutputBlocking(CHANNEL,
                               static_cast<int>(volume) * PSP_AUDIO_VOLUME_MAX / 100,
                               chunk[next]);
        next ^= 1;
    }
    return 0;
}

} // namespace

void audio::Init(u8) {
    int err = 0;
    vorbis = stb_vorbis_open_memory(ogg_data, ogg_len, &err, nullptr);
    if (!vorbis) return;

    if (nTracks > 0) {
        loop_frame = tracks[0].loop_start > 0.0f
            ? static_cast<u32>(tracks[0].loop_start * static_cast<float>(SAMPLE_RATE))
            : 0;
    }

    if (sceAudioChReserve(CHANNEL, CHUNK_FRAMES, PSP_AUDIO_FORMAT_STEREO) < 0) return;

    running = true;
    athread = sceKernelCreateThread("audio_thread", audio_thread, 0x12, 0x10000, 0, nullptr);
    if (athread >= 0) sceKernelStartThread(athread, 0, nullptr);
}

void audio::Update() {
}

void audio::TrackPlay(const u8 index) {
    if (!vorbis || index >= nTracks) return;
    pending_track = index;
}

void audio::TrackStop() {
    playing = false;
}

void audio::TrackVolume(const u8 percent) {
    volume = percent > 100 ? 100 : percent;
}

void audio::TrackPause(const u8 pause) {
    (void)pause;  // match the SDL/Switch/Wii U backends: toggle regardless of argument
    paused = !paused;
}

void audio::SfxPlay(u8 /*index*/, u8 /*channel*/, u8 /*volume*/) {}
void audio::SfxSamplePlay(u8 /*index*/) {}
void audio::SfxStop(u8 /*channel*/) {}
void audio::SfxPause(u8 /*channel*/, u8 /*pause*/) {}
