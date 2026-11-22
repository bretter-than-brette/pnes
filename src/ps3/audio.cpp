/**
 * @file audio.cpp
 * @brief Music playback for the PS3 backend (PSL1GHT libaudio).
 *
 * The PSP/PS2/Wii U shape: the desktop wav is transcoded to a small Vorbis OGG at
 * build time, embedded, decoded here with stb_vorbis and streamed from a
 * dedicated thread, so Update() is a no-op.
 *
 * Two things are easier here than on the PS2 and both are why that branch looks
 * complicated: there is no IOP module to embed and load (libaudio is PPU-side,
 * audioInit() is the whole equivalent), and no format conversion, since the port
 * buffer is 32-bit float -- exactly what stb_vorbis produces -- at a fixed 48kHz.
 */
#include "internal.hpp"
#include <pnes/audio.hpp>

#include <audio/audio.h>
#include <lv2/thread.h>
#include <sys/thread.h>
#include <sys/event_queue.h>
#include <sys/systime.h>

#include <atomic>
#include <cstring>

#define STB_VORBIS_NO_PUSHDATA_API
#define STB_VORBIS_NO_STDIO
#include <stb_vorbis.c>

static const unsigned char ogg_data[] = {
#embed <pc_audio.ogg>   // resolved via --embed-dir (set in the ps3 CMake branch)
};
static constexpr int ogg_len = static_cast<int>(sizeof(ogg_data));

static constexpr int SAMPLE_RATE = 48000;
static constexpr int CHANNELS    = 2;

float *audio::pcm_buffer = nullptr;
u32 audio::pcm_buffer_size = 0;

namespace {

stb_vorbis *vorbis     = nullptr;
u32         loop_frame = 0;      // stb_vorbis_seek() target on EOF
bool        playing    = false;
bool        paused     = false;

u32                 port_num  = 0;
audioPortConfig     port_cfg  = {};
sys_event_queue_t   snd_queue = 0;
sys_ipc_key_t       snd_key   = 0;
sys_ppu_thread_t    athread   = 0;
bool                opened    = false;

std::atomic<bool> running{false};

// std::atomic is usable here, unlike on the EE: the PPU has real CAS. One writer
// (TrackPlay on the main thread), one reader (the decode thread).
std::atomic<int> pending_track{-1};

std::atomic<u8> volume{100};

void fill(float *dst, const int frames) {
    int got = 0, stalls = 0;
    while (got < frames && stalls < 4) {
        const int n = stb_vorbis_get_samples_float_interleaved(
            vorbis, CHANNELS, dst + got * CHANNELS, (frames - got) * CHANNELS);
        if (n == 0) {
            stb_vorbis_seek(vorbis, loop_frame);
            stalls++;
            continue;
        }
        got += n;
        stalls = 0;
    }
    if (got < frames) {
        std::memset(dst + got * CHANNELS, 0,
                    static_cast<size_t>(frames - got) * CHANNELS * sizeof(float));
    }

    if (const u8 vol = volume.load(std::memory_order_relaxed); vol != 100) {
        const float gain = static_cast<float>(vol) / 100.0f;
        for (int i = 0; i < frames * CHANNELS; i++) dst[i] *= gain;
    }
}

enum class ReadIndexKind { Pointer, OwnCounter };

// libaudio reports its read index through a shared block whose layout differs
// between firmware revisions, so the usable one is resolved at runtime rather
// than assumed.
ReadIndexKind resolve_read_index(const u64 **out) {
    const u64 raw = static_cast<u64>(port_cfg.readIndex);
    if (raw > port_cfg.numBlocks && raw >= 0x10000) {
        *out = reinterpret_cast<const u64 *>(raw);
        ps3_trace("audio: readIndex looks like an address (Cell reading)");
        return ReadIndexKind::Pointer;
    }
    *out = nullptr;
    ps3_trace("audio: readIndex is not an address -- counting blocks locally");
    return ReadIndexKind::OwnCounter;
}

void audio_thread(void *) {
    const u64 *read_index = nullptr;
    const ReadIndexKind kind = resolve_read_index(&read_index);

    float *const data_start = reinterpret_cast<float *>(static_cast<u64>(port_cfg.audioDataStart));
    const u32 block_floats = static_cast<u32>(port_cfg.channelCount) * AUDIO_BLOCK_SAMPLES;
    u64 own_block = 0;   // used only when `kind` is OwnCounter

    while (running.load(std::memory_order_relaxed)) {
        sys_event_t event;
        if (sysEventQueueReceive(snd_queue, &event, 100 * 1000) != 0) continue;

        const u64 cur = kind == ReadIndexKind::Pointer ? *read_index : own_block++;
        const u64 next_block = (cur + 1) % port_cfg.numBlocks;
        float *dst = data_start + next_block * block_floats;

        const int req = pending_track.exchange(-1, std::memory_order_relaxed);
        if (req >= 0 && vorbis) {
            stb_vorbis_seek_start(vorbis);
            loop_frame = tracks[req].loop_start > 0.0f
                ? static_cast<u32>(tracks[req].loop_start * static_cast<float>(SAMPLE_RATE))
                : 0;
            playing = true;
            paused  = false;
        }

        if (!vorbis || !playing || paused) {
            std::memset(dst, 0, static_cast<size_t>(block_floats) * sizeof(float));
        } else {
            fill(dst, AUDIO_BLOCK_SAMPLES);
        }
    }

    sysThreadExit(0);
}

} // namespace

void audio::Init(u8) {
    ps3_trace("audio::Init: entered");

    int err = 0;
    vorbis = stb_vorbis_open_memory(ogg_data, ogg_len, &err, nullptr);
    if (!vorbis) {
        ps3_trace("audio::Init: stb_vorbis_open_memory FAILED");
        return;
    }

    if (nTracks > 0) {
        loop_frame = tracks[0].loop_start > 0.0f
            ? static_cast<u32>(tracks[0].loop_start * static_cast<float>(SAMPLE_RATE))
            : 0;
    }

    if (audioInit() != 0) {
        ps3_trace("audio::Init: audioInit FAILED");
        return;
    }

    audioPortParam params;
    std::memset(&params, 0, sizeof(params));
    params.numChannels = AUDIO_PORT_2CH;
    params.numBlocks   = AUDIO_BLOCK_8;
    params.attrib      = AUDIO_PORT_INITLEVEL;
    params.level       = 1.0f;

    if (audioPortOpen(&params, &port_num) != 0) {
        ps3_trace("audio::Init: audioPortOpen FAILED");
        audioQuit();
        return;
    }
    if (audioGetPortConfig(port_num, &port_cfg) != 0 || port_cfg.audioDataStart == 0) {
        ps3_trace("audio::Init: audioGetPortConfig FAILED");
        audioPortClose(port_num);
        audioQuit();
        return;
    }
    opened = true;

    audioCreateNotifyEventQueue(&snd_queue, &snd_key);
    audioSetNotifyEventQueue(snd_key);
    sysEventQueueDrain(snd_queue);

    audioPortStart(port_num);

    running.store(true, std::memory_order_relaxed);
    if (sysThreadCreate(&athread, audio_thread, nullptr, 1000, 0x10000, THREAD_JOINABLE,
                        const_cast<char *>("audio_thread")) != 0) {
        running.store(false, std::memory_order_relaxed);
        ps3_trace("audio::Init: sysThreadCreate FAILED -- no music this run");
        return;
    }
    ps3_trace("audio::Init: complete");
}

void audio::Update() {
}

void audio::TrackPlay(const u8 index) {
    if (!vorbis || index >= nTracks) return;
    pending_track.store(index, std::memory_order_relaxed);
}

void audio::TrackVolume(const u8 percent) {
    volume.store(percent > 100 ? 100 : percent, std::memory_order_relaxed);
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
