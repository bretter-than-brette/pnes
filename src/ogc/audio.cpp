/**
 * @file audio.cpp
 * @brief Music and sound effects for the GameCube and Wii backend (libogc/ASND).
 *
 * Real DSP-ADPCM decode is not reachable from this toolchain: ASND and AESND
 * expose only raw-PCM voice formats, and the ADPCM microcode Nintendo's SDK used
 * was never released. So the desktop wav is transcoded to a Vorbis OGG at build
 * time, embedded, and decoded here with stb_vorbis.
 *
 * libogc has no SDL audio device, so playback is polled from audio::Update().
 * ASND gives a voice two buffers, one playing and one queued, so Update()
 * decodes the next chunk into whichever is idle. Buffers hold ~0.37s, far more
 * than a frame, so an occasional missed Update does not starve playback -- and
 * 24MB of GC RAM could not hold the source decoded anyway.
 *
 * Effects are embedded as raw PCM rather than Vorbis and played straight out of
 * .rodata: 48kHz stereo s16 big-endian IS an ASND voice format, so nothing is
 * decoded or allocated. The list is generated, because the table is the
 * application's -- #embed needs a literal path at compile time while effects
 * arrive through audio::SetSfx at runtime, so CMake writes sfx_embed.inc and
 * load_sfx matches by NAME, so a glob reordering cannot swap two effects.
 *
 * An SD-card route is still below behind an `#if 0`: it lost because Dolphin
 * emulates neither an SD Gecko nor an SD2SP2, so a card-loading GameCube build
 * can never make a sound where this gets tested. See
 * .ignore/ogc-sfx-sd-gecko/NOTES.md.
 */
#include "internal.hpp"
#include <pnes/audio.hpp>

#include <asndlib.h>
#include <gctypes.h>
#include <ogc/cache.h>
#include <cstdio>
#include <cstring>

#define STB_VORBIS_NO_PUSHDATA_API
#define STB_VORBIS_NO_STDIO
#include <stb_vorbis.c>

static const unsigned char ogg_data[] = {
#embed <pc_audio.ogg>   // resolved via --embed-dir (set in the gc/wii CMake branch)
};
static constexpr int ogg_len = static_cast<int>(sizeof(ogg_data));

float *audio::pcm_buffer = nullptr;
u32 audio::pcm_buffer_size = 0;

namespace {

constexpr s32 VOICE          = 0;  // ASND reserves voice 0, by convention, for a music/OGG player
constexpr int CHANNELS       = 2;  // pc_audio.wav is stereo; this backend doesn't need to generalize past that
constexpr int BUFFER_FRAMES  = 4096;   // ~93ms @ 44.1kHz
constexpr int BUFFER_SHORTS  = BUFFER_FRAMES * CHANNELS;

alignas(32) i16 buf[2][BUFFER_SHORTS];

stb_vorbis *vorbis     = nullptr;
int         src_rate   = 44100;
u32         loop_frame = 0;      // stb_vorbis_seek() target on EOF
int         next_buf   = 0;      // which half of buf[] Update() fills next
bool        playing    = false;
bool        paused     = false;
bool        primed     = false;  // buf[0] was decoded ahead in Init(), not TrackPlay()
u8          volume     = 100;
bool        pending    = false;  // buf[next_buf] is decoded and waiting on ASND_AddVoice()

void fill(const int which) {
    i16 *dst = buf[which];
    int got = 0, stalls = 0;
    while (got < BUFFER_FRAMES && stalls < 4) {
        const int n = stb_vorbis_get_samples_short_interleaved(
            vorbis, CHANNELS, dst + got * CHANNELS, (BUFFER_FRAMES - got) * CHANNELS);
        if (n == 0) {
            stb_vorbis_seek(vorbis, loop_frame);
            stalls++;
            continue;
        }
        got += n;
        stalls = 0;
    }
    if (got < BUFFER_FRAMES) {
        memset(dst + got * CHANNELS, 0, static_cast<size_t>(BUFFER_FRAMES - got) * CHANNELS * sizeof(i16));
    }
}


// Voice 0 is the music, so effects take the slots above it.
constexpr s32 SFX_VOICE_BASE = VOICE + 1;
constexpr int N_SFX_VOICES   = audio::sfxChannels;
static_assert(SFX_VOICE_BASE + N_SFX_VOICES <= MAX_SND_VOICES,
              "audio::sfxChannels asks for more ASND voices than libogc has "
              "(MAX_SND_VOICES, asndlib.h) once the music has taken voice 0");

constexpr s32 SFX_RATE = 48000;

struct sfx_runtime_t { u8 *data; u32 len; };
sfx_runtime_t sfx_info[256] = {};

struct sfx_embed_t { const char *name; const unsigned char *data; u32 len; };

#include <sfx_embed.inc>

bool names_match(const char *const fp, const char *const name) {
    const char *slash = strrchr(fp, '/');
    const char *const base = slash ? slash + 1 : fp;
    const char *const dot = strrchr(base, '.');
    const size_t len = dot ? static_cast<size_t>(dot - base) : strlen(base);
    return strlen(name) == len && strncmp(base, name, len) == 0;
}

void load_sfx() {
    for (u8 i = 0; i < nSfx; i++) {
        for (const sfx_embed_t &e : sfx_embeds) {
            if (!names_match(sfxs[i].fp, e.name)) continue;

    // Truncated to a whole 32 bytes: ASND wants an aligned length, and the
    // tail of a 0.4s effect is inaudible.
            const u32 len = e.len & ~31u;
            if (len == 0) break;

    // The embedded bytes ARE the voice buffer, so they only need flushing out
    // of the data cache before the DSP reads them.
            DCFlushRange(const_cast<unsigned char *>(e.data), len);

            sfx_info[i] = { const_cast<unsigned char *>(e.data), len };
            break;
        }
    }
}

#if 0

void sfx_path(const char *const root, const char *const fp, char *const out, const size_t cap) {
    snprintf(out, cap, "%sassets/%s", root, fp);
    char *dot = strrchr(out, '.');
    if (dot && (cap - static_cast<size_t>(dot - out)) >= 5) memcpy(dot, ".raw", 5);  // incl. NUL
}

bool sfx_exists(const char *const base, const char *const fp) {
    char path[384];
    sfx_path(base, fp, path, sizeof(path));
    FILE *const f = fopen(path, "rb");
    if (!f) return false;
    fclose(f);
    return true;
}

const char *app_dir() {
    static char dir[320];
    const struct __argv *const a = __system_argv;
    if (!a || a->argvMagic != ARGV_MAGIC || a->argc < 1 || !a->argv || !a->argv[0]) return nullptr;

    snprintf(dir, sizeof(dir), "%s", a->argv[0]);
    char *const slash = strrchr(dir, '/');
    if (!slash) return nullptr;     // a bare filename names no directory
    slash[1] = '\0';                // keep the '/': bases are concatenated as-is
    return dir;
}

bool scan_for_sfx(const char *const root, const char *const fp, char *const base, const size_t cap) {
    DIR *const d = opendir(root);
    if (!d) return false;
    bool found = false;
    while (const dirent *const e = readdir(d)) {
        if (e->d_name[0] == '.') continue;          // ".", "..", and dotfiles
        char cand[320];
        snprintf(cand, sizeof(cand), "%s%s/", root, e->d_name);
        if (sfx_exists(cand, fp)) {
            snprintf(base, cap, "%s", cand);
            found = true;
            break;
        }
    }
    closedir(d);
    return found;
}

const char *resolve_sfx_base(const char *const fp) {
    static char base[320];

    if (const char *const dir = app_dir(); dir && sfx_exists(dir, fp)) {
        snprintf(base, sizeof(base), "%s", dir);
        return base;
    }

    if (sfx_exists("", fp)) { base[0] = '\0'; return base; }

    static const char *const kRoots[] = { "sd:/", "carda:/", "cardb:/", "usb:/" };
    for (const char *const root : kRoots) {
        if (sfx_exists(root, fp)) {
            snprintf(base, sizeof(base), "%s", root);
            return base;
        }
    }

    for (const char *const root : kRoots) {
        if (scan_for_sfx(root, fp, base, sizeof(base))) return base;
        char apps[320];
        snprintf(apps, sizeof(apps), "%sapps/", root);
        if (scan_for_sfx(apps, fp, base, sizeof(base))) return base;
    }

    return nullptr;
}

void load_sfx() {
    const char *const base = resolve_sfx_base(sfxs[0].fp);
    if (!base) return;                          // not on any mounted device

    char path[384];
    for (u8 i = 0; i < nSfx; i++) {
        sfx_path(base, sfxs[i].fp, path, sizeof(path));
        FILE *const f = fopen(path, "rb");
        if (!f) continue;                       // no such effect under the base

        fseek(f, 0, SEEK_END);
        const long sz = ftell(f);
        fseek(f, 0, SEEK_SET);
        if (sz <= 0) { fclose(f); continue; }

        const u32 len = (static_cast<u32>(sz) + 31u) & ~31u;
        u8 *const data = static_cast<u8 *>(memalign(32, len));
        if (!data) { fclose(f); continue; }

        const bool ok = fread(data, 1, static_cast<size_t>(sz), f) == static_cast<size_t>(sz);
        fclose(f);
        if (!ok) { free(data); continue; }
        memset(data + sz, 0, len - static_cast<u32>(sz));

        DCFlushRange(data, len);

        sfx_info[i] = { data, len };
    }
}
#endif  // the SD-card route


} // namespace

void audio::Init(u8) {
    ASND_Init();
    ASND_Pause(0);

    int err = 0;
    vorbis = stb_vorbis_open_memory(ogg_data, ogg_len, &err, nullptr);
    if (!vorbis) return;
    src_rate = stb_vorbis_get_info(vorbis).sample_rate;

    if (nTracks > 0) {
        loop_frame = tracks[0].loop_start > 0.0f
            ? static_cast<u32>(tracks[0].loop_start * static_cast<float>(src_rate))
            : 0;
        fill(0);
        primed = true;
    }

    if (nSfx > 0 && sfxs) load_sfx();
}

void audio::Update() {
    if (!vorbis || !playing || paused) return;

    if (!pending) {
        if (!ASND_TestVoiceBufferReady(VOICE)) return;
        next_buf ^= 1;
        fill(next_buf);
        pending = true;
    }

    if (ASND_AddVoice(VOICE, buf[next_buf], BUFFER_SHORTS * sizeof(i16)) == SND_OK) {
        pending = false;
    }
}

void audio::TrackPlay(const u8 index) {
    if (!vorbis || index >= nTracks) return;

    ASND_StopVoice(VOICE);

    if (!(index == 0 && primed)) {
        stb_vorbis_seek_start(vorbis);
        loop_frame = tracks[index].loop_start > 0.0f
            ? static_cast<u32>(tracks[index].loop_start * static_cast<float>(src_rate))
            : 0;
        fill(0);
    }
    primed  = false;
    pending = false;

    next_buf = 0;
    const s32 vol = static_cast<s32>(volume) * MAX_VOLUME / 100;
    ASND_SetVoice(VOICE, VOICE_STEREO_16BIT, src_rate, 0,
                  buf[0], BUFFER_SHORTS * sizeof(i16),
                  vol, vol, nullptr);
    playing = true;
    paused  = false;
}

void audio::TrackStop() {
    ASND_StopVoice(VOICE);
    playing = false;
}

void audio::TrackPause(const u8 pause) {
    (void)pause;  // match the SDL/Wii U backends: toggle regardless of argument
    paused = !paused;
    ASND_PauseVoice(VOICE, paused);
}

void audio::TrackVolume(const u8 percent) {
    volume = percent > 100 ? 100 : percent;
    const s32 vol = static_cast<s32>(volume) * MAX_VOLUME / 100;
    if (playing) ASND_ChangeVolumeVoice(VOICE, vol, vol);
}

void audio::SfxPlay(const u8 index, const u8 channel, const u8 volume) {
    if (index >= nSfx || channel >= N_SFX_VOICES) return;
    const sfx_runtime_t &s = sfx_info[index];
    if (!s.data || s.len == 0) return;   // never loaded: no storage, or no such file
    ASND_SetVoice(SFX_VOICE_BASE + channel, VOICE_STEREO_16BIT, SFX_RATE, 0,
                  s.data, static_cast<s32>(s.len), volume, volume, nullptr);
}

void audio::SfxSamplePlay(const u8 index) { audio::SfxPlay(index, 1, 0xFF); }

void audio::SfxStop(const u8 channel) {
    if (channel >= N_SFX_VOICES) return;
    ASND_StopVoice(SFX_VOICE_BASE + channel);
}

void audio::SfxPause(const u8 channel, const u8 pause) {
    if (channel >= N_SFX_VOICES) return;
    ASND_PauseVoice(SFX_VOICE_BASE + channel, pause != 0);
}
