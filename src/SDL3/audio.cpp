#include <pnes/audio.hpp>
#include <pnes/technology.hpp>
#include <SDL3/SDL.h>

#include <cstdlib>

// stb_vorbis's pushdata API is not used; this is the pulldata build only.
#define STB_VORBIS_NO_PUSHDATA_API
#define STB_VORBIS_NO_STDIO
#include <stb_vorbis.c>

typedef struct {
    u32 offset;
    u32 length;
    u32 loop_start;
} track_runtime_t;

static track_runtime_t track_info[256];
float *audio::pcm_buffer = nullptr;
u32 audio::pcm_buffer_size = 0;
static SDL_AudioStream *music_stream = nullptr;
static u32 playback_pos = 0;
static int current_track = -1;
static int music_playing = 0;
static int music_paused = 0;

static u8 music_volume = 100;

typedef struct {
    u32 offset;   // float index (not bytes) into sfx_pcm_buffer
    u32 length;   // in floats
} sfx_runtime_t;

static sfx_runtime_t sfx_info[256];
static float *sfx_pcm_buffer = nullptr;
static u32 sfx_pcm_buffer_size = 0;   // in floats

static SDL_AudioStream *sfx_stream = nullptr;

static constexpr int N_SFX_VOICES = audio::sfxChannels;
struct sfx_voice_t { u32 pos; u32 end; float gain; bool active; bool paused; };
static sfx_voice_t sfx_voices[N_SFX_VOICES];

static u32 track_offsets[256];
static u32 track_lengths[256];

#define BYTES_PER_SECOND (48000 * 2 * sizeof(float))

static bool IsOggFile(const char *fp) {
    SDL_IOStream *io = SDL_IOFromFile(fp, "rb");
    if (!io) return false;

    char magic[4];
    const size_t got = SDL_ReadIO(io, magic, sizeof(magic));
    SDL_CloseIO(io);
    return got == sizeof(magic) && SDL_memcmp(magic, "OggS", 4) == 0;
}

// Vorbis rather than wav where the asset is one: a 47MB wav is not worth
// shipping, and stb_vorbis decodes it to the same spec SDL wants.
static bool LoadOgg(const char *fp, SDL_AudioSpec *spec, u8 **data, u32 *len) {
    size_t fileLen = 0;
    void *file = SDL_LoadFile(fp, &fileLen);
    if (!file) return false;

    int channels = 0;
    int rate = 0;
    short *pcm = nullptr;
    const int frames = stb_vorbis_decode_memory(
        static_cast<const unsigned char *>(file), static_cast<int>(fileLen),
        &channels, &rate, &pcm);
    SDL_free(file);

    if (frames <= 0 || !pcm || channels <= 0) {
        if (pcm) free(pcm);
        SDL_SetError("stb_vorbis could not decode %s", fp);
        return false;
    }

    const u32 bytes = static_cast<u32>(frames) * static_cast<u32>(channels)
                      * static_cast<u32>(sizeof(short));
    u8 *out = static_cast<u8 *>(SDL_malloc(bytes));
    if (!out) {
        free(pcm);
        return false;
    }
    SDL_memcpy(out, pcm, bytes);
    free(pcm);

    spec->format   = SDL_AUDIO_S16LE;
    spec->channels = channels;
    spec->freq     = rate;
    *data = out;
    *len  = bytes;
    return true;
}

static bool LoadTrack(const char *fp, SDL_AudioSpec *spec, u8 **data, u32 *len) {
    if (IsOggFile(fp)) return LoadOgg(fp, spec, data, len);
    return SDL_LoadWAV(fp, spec, data, len);
}

static void BuildPCMBuffer() {
    constexpr SDL_AudioSpec target = {
        .format = SDL_AUDIO_F32LE,
        .channels = 2,
        .freq = 48000
    };

    auto total = 0;
    for (auto i = 0; i < nTracks; i++) {
        SDL_AudioSpec spec;
        u8 *data;
        u32 len;
        if (LoadTrack(tracks[i].fp, &spec, &data, &len)) {
            u8 *converted = nullptr;
            auto converted_len = 0;
            if (SDL_ConvertAudioSamples(&spec, data, static_cast<int>(len),
                                         &target, &converted, &converted_len)) {
                total += converted_len;
                SDL_free(converted);
            }
            SDL_free(data);
        }
    }

    audio::pcm_buffer = static_cast<float *>(SDL_malloc(total));
    audio::pcm_buffer_size = total;

    auto offset = 0;
    for (u8 i = 0; i < nTracks; i++) {
        SDL_AudioSpec spec;
        u8 *data;
        u32 len;

        if (!LoadTrack(tracks[i].fp, &spec, &data, &len)) {
            SDL_Log("Failed to load %s: %s", tracks[i].fp, SDL_GetError());
            track_info[i].offset = 0;
            track_info[i].length = 0;
            track_info[i].loop_start = 0;
            continue;
        }

        u8 *converted = nullptr;
        auto converted_len = 0;
        if (SDL_ConvertAudioSamples(&spec, data, static_cast<int>(len),
                                     &target, &converted, &converted_len)) {
            SDL_memcpy(reinterpret_cast<u8 *>(audio::pcm_buffer) + offset, converted, converted_len);

            track_info[i].offset = offset;
            track_info[i].length = converted_len;

            if (tracks[i].loop_start > 0.0f) {
                auto loop_bytes = static_cast<u32>(tracks[i].loop_start * BYTES_PER_SECOND);
                if (loop_bytes > static_cast<u32>(converted_len)) loop_bytes = 0;
                track_info[i].loop_start = offset + loop_bytes;
            } else {
                track_info[i].loop_start = offset;
            }

            offset += converted_len;
            SDL_free(converted);
        }

        SDL_free(data);
    }
}


static void BuildSfxPCMBuffer() {
    constexpr SDL_AudioSpec target = {
        .format = SDL_AUDIO_F32LE,
        .channels = 2,
        .freq = 48000
    };

    auto total = 0;
    for (auto i = 0; i < nSfx; i++) {
        SDL_AudioSpec spec;
        u8 *data;
        u32 len;
        if (LoadTrack(sfxs[i].fp, &spec, &data, &len)) {
            u8 *converted = nullptr;
            auto converted_len = 0;
            if (SDL_ConvertAudioSamples(&spec, data, static_cast<int>(len),
                                         &target, &converted, &converted_len)) {
                total += converted_len;
                SDL_free(converted);
            }
            SDL_free(data);
        }
    }
    if (total == 0) {
        if (nSfx) {
            SDL_Log("AUDIO: all %u registered SFX failed to load -- nothing will play. "
                    "Paths are relative to the CURRENT WORKING DIRECTORY, not the "
                    "executable, and the build copies them next to the executable.",
                    static_cast<unsigned>(nSfx));
        }
        return;
    }

    sfx_pcm_buffer = static_cast<float *>(SDL_malloc(total));
    sfx_pcm_buffer_size = total / sizeof(float);

    auto offset = 0;
    for (u8 i = 0; i < nSfx; i++) {
        SDL_AudioSpec spec;
        u8 *data;
        u32 len;

        if (!LoadTrack(sfxs[i].fp, &spec, &data, &len)) {
            SDL_Log("Failed to load %s: %s", sfxs[i].fp, SDL_GetError());
            sfx_info[i].offset = 0;
            sfx_info[i].length = 0;
            continue;
        }

        u8 *converted = nullptr;
        auto converted_len = 0;
        if (SDL_ConvertAudioSamples(&spec, data, static_cast<int>(len),
                                     &target, &converted, &converted_len)) {
            SDL_memcpy(reinterpret_cast<u8 *>(sfx_pcm_buffer) + offset, converted, converted_len);
            sfx_info[i].offset = offset / sizeof(float);
            sfx_info[i].length = converted_len / sizeof(float);
            offset += converted_len;
            SDL_free(converted);
        }

        SDL_free(data);
    }

    u8 decoded = 0;
    for (u8 i = 0; i < nSfx; i++) {
        if (sfx_info[i].length) decoded++;
    }
    SDL_Log("AUDIO: %u/%u SFX decoded into %u frames on %d voices",
            static_cast<unsigned>(decoded), static_cast<unsigned>(nSfx),
            sfx_pcm_buffer_size / 2u, N_SFX_VOICES);
}

void audio::SfxPlay(const u8 index, const u8 channel, const u8 volume) {
    if (index >= nSfx) {
        SDL_Log("AUDIO: SfxPlay(%u) past the %u registered effects",
                static_cast<unsigned>(index), static_cast<unsigned>(nSfx));
        return;
    }
    if (channel >= N_SFX_VOICES) {
        SDL_Log("AUDIO: SfxPlay channel %u past this backend's %d voices",
                static_cast<unsigned>(channel), N_SFX_VOICES);
        return;
    }
    if (sfx_info[index].length == 0) {
        SDL_Log("AUDIO: SfxPlay(%u) has no decoded samples (\"%s\" did not load)",
                static_cast<unsigned>(index), sfxs[index].fp);
        return;
    }
    sfx_voices[channel] = {
        .pos    = sfx_info[index].offset,
        .end    = sfx_info[index].offset + sfx_info[index].length,
        .gain   = static_cast<float>(volume) / 255.0f,
        .active = true
    };
}

void audio::SfxSamplePlay(const u8 index) {
    SfxPlay(index, 1, 0xFF);
}

void audio::SfxStop(const u8 channel) {
    if (channel >= N_SFX_VOICES) return;
    sfx_voices[channel].active = false;
    sfx_voices[channel].paused = false;
}

void audio::SfxPause(const u8 channel, const u8 pause) {
    if (channel >= N_SFX_VOICES) return;
    sfx_voices[channel].paused = pause != 0;
}


static void UpdateSfx() {
    if (!sfx_stream) return;
    constexpr int CHUNK_FLOATS = 2048;  // ~21ms at 48kHz/2ch

    while (SDL_GetAudioStreamQueued(sfx_stream) <= static_cast<int>(CHUNK_FLOATS * sizeof(float) * 4)) {
        float mix[CHUNK_FLOATS] = {};
        bool any_active = false;

        for (auto &v : sfx_voices) {
            if (!v.active || v.paused) continue;
            any_active = true;
            const u32 avail = v.end - v.pos;
            const u32 take = avail < static_cast<u32>(CHUNK_FLOATS) ? avail : static_cast<u32>(CHUNK_FLOATS);
            const float *src = sfx_pcm_buffer + v.pos;
            const float gain = v.gain;
            for (u32 i = 0; i < take; i++) mix[i] += src[i] * gain;
            v.pos += take;
            if (v.pos >= v.end) v.active = false;
        }

        if (!any_active) break;
        SDL_PutAudioStreamData(sfx_stream, mix, sizeof(mix));
    }
}

static void OpenAudioStreams() {
    if (music_stream) return;  // already opened

    constexpr SDL_AudioSpec spec = {
        .format = SDL_AUDIO_F32LE,
        .channels = 2,
        .freq = 48000
    };
    music_stream = SDL_OpenAudioDeviceStream(
        SDL_AUDIO_DEVICE_DEFAULT_PLAYBACK, &spec, nullptr, nullptr);
    if (!music_stream) {
        SDL_Log("AUDIO: stream creation failed: %s", SDL_GetError());
        return;
    }
    SDL_ResumeAudioStreamDevice(music_stream);
    SDL_SetAudioStreamGain(music_stream, static_cast<float>(music_volume) / 100.0f);

    // A second stream for effects, so an effect never has to be mixed into the
    // music chunk and can start on the next device callback.
    sfx_stream = SDL_OpenAudioDeviceStream(
        SDL_AUDIO_DEVICE_DEFAULT_PLAYBACK, &spec, nullptr, nullptr);
    if (!sfx_stream) {
        SDL_Log("AUDIO: SFX stream creation failed: %s", SDL_GetError());
        return;
    }
    SDL_ResumeAudioStreamDevice(sfx_stream);
}

void audio::Init(u8) {
    OpenAudioStreams();

    BuildPCMBuffer();
    BuildSfxPCMBuffer();
}

void audio::Update() {
    if (music_stream && music_playing && !music_paused && current_track >= 0) {
        const auto queued = SDL_GetAudioStreamQueued(music_stream);
        if (queued <= 32768) {
            const track_runtime_t *t = &track_info[current_track];
            const auto end = t->offset + t->length;

            auto to_feed = 32768 - queued;
            while (to_feed > 0) {
                const auto remaining = end - playback_pos;
                const auto chunk = remaining < 16384 ? remaining : 16384;

                SDL_PutAudioStreamData(music_stream,
                    reinterpret_cast<u8 *>(pcm_buffer) + playback_pos, static_cast<int>(chunk));
                playback_pos += chunk;
                to_feed -= static_cast<int>(chunk);

                if (playback_pos >= end) {
                    playback_pos = t->loop_start;
                }
            }
        }
    }

    UpdateSfx();
}

void audio::TrackPlay(const u8 index) {
    if (index >= nTracks) return;
    current_track = index;
    playback_pos = track_info[index].offset;
    music_playing = 1;
    music_paused = 0;
    if (music_stream) SDL_ClearAudioStream(music_stream);
}

void audio::TrackStop() {
    music_playing = 0;
    current_track = -1;
    if (music_stream) SDL_ClearAudioStream(music_stream);
}

void audio::TrackPause(const u8 pause) {
    (void)pause;
    music_paused = !music_paused;
}

void audio::TrackVolume(const u8 percent) {
    music_volume = percent > 100 ? 100 : percent;
    if (music_stream) {
        SDL_SetAudioStreamGain(music_stream, static_cast<float>(music_volume) / 100.0f);
    }
}