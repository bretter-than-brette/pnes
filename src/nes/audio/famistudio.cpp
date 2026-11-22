#include <pnes/audio.hpp>
#include <pnes/technology.hpp>   // CREATE_SEGMENT_KEYWORD

/**
 * @brief Placement keyword for this module's code; the section is the consuming
 *        project's choice, supplied as PNES_AUDIO_SECTION.
 *
 * The point: this has to land in the SAME PRG bank as the engine it drives. Once
 * they are neighbours, nothing here switches banks at all -- a caller long-calls
 * audio::Update(), and famistudio_update() inside it is an ordinary call because
 * the caller's own switch brought both in together. Which is why nothing here
 * includes a mapper header or names a bank.
 *
 * Compulsory, not defaulted: a default would silently place this module wherever
 * the library guessed, which is the wrong-bank-with-no-diagnostic failure the
 * rest of this design exists to make impossible.
 */
#ifndef PNES_AUDIO_SECTION
#error "PNES_AUDIO_SECTION is not set. It names the ELF section this \
module's code is placed in, and MUST be the same bank the audio engine is \
assembled into (see the project's FamiStudio config). Set it from CMake -- \
local.cmake.example has a worked example."
#endif
#define AUDIO_BANK MODULE_PLACEMENT(PNES_AUDIO_SECTION)

#include <intsh>
using namespace br0::intsh;

#if !FAMISTUDIO_CFG_NTSC_SUPPORT && !FAMISTUDIO_CFG_PAL_SUPPORT
    #error "FamiStudio: neither NTSC nor PAL support enabled"
#endif

#ifdef __mos__
    #define FASTCALL __attribute__((cc65_fastcall)) void
#else
    #define FASTCALL void
#endif

// The engine's own entry points, as ca65 exports. FASTCALL because llvm-mos
// passes the first arguments in registers, which is what the engine expects.
extern "C" FASTCALL famistudio_music_play(u8 song_index);
extern "C" FASTCALL famistudio_music_pause(u8 pause);
extern "C" FASTCALL famistudio_music_stop(void);
#if FAMISTUDIO_CFG_SFX_SUPPORT
extern "C" FASTCALL famistudio_sfx_play(u8 sfx_index, u8 channel);
#endif
#if FAMISTUDIO_CFG_DPCM_SUPPORT
extern "C" FASTCALL famistudio_sfx_sample_play(u8 sample_index);
#endif
extern "C" FASTCALL famistudio_update(void);

extern "C" {
__attribute__((used)) u8 famistudio_region_scratch;
}

#if FAMISTUDIO_CFG_SFX_SUPPORT
// FamiStudio hardcodes its effect symbol as `sounds` whatever the project is
// called, so this names it directly rather than taking it from the application.
extern "C" { extern const u8 _sounds[]; }
static constexpr const u8* sfx = _sounds;
#endif

AUDIO_BANK void audio::Init(const u8 region) {
    famistudio_region_scratch = region ? 0 : 1;

    __asm__ volatile (
        "ldx #<%0\n"
        "ldy #>%0\n"
        "lda famistudio_region_scratch\n"
        "jsr famistudio_init\n"
        :
        : "i"(tracks)
        : "memory", "a", "x", "y", "c", "v"
    );

#if FAMISTUDIO_CFG_SFX_SUPPORT
    __asm__ volatile (
        "ldx #<%0\n"
        "ldy #>%0\n"
        "jsr famistudio_sfx_init\n"
        :
        : "i"(sfx)
        : "memory", "a", "x", "y", "c", "v"
    );
#endif
}

AUDIO_BANK void audio::TrackPlay(const u8 index) {
    famistudio_music_play(index);
}

AUDIO_BANK void audio::TrackPause(const u8 pause) {
    famistudio_music_pause(pause);
}

AUDIO_BANK void audio::TrackStop() {
    famistudio_music_stop();
}

AUDIO_BANK void audio::TrackVolume(const u8 percent) {
    (void)percent;
}

AUDIO_BANK void audio::Update() {
    famistudio_update();
}

AUDIO_BANK void audio::SfxPlay(const u8 index, const u8 channel, const u8) {
#if FAMISTUDIO_CFG_SFX_SUPPORT
    famistudio_sfx_play(index, channel);
#else
    (void)index; (void)channel;
#endif
}

// FamiStudio's engine has no entry point that cancels an effect, so this cannot
// do what it is named for -- audio.hpp warns at the call site. See
// .ignore/famistudio-sfx-stop/NOTES.md.
AUDIO_BANK void audio::SfxStop(const u8 channel) {
    (void)channel;
}

AUDIO_BANK void audio::SfxPause(const u8 channel, const u8 pause) {
    (void)channel; (void)pause;
}

AUDIO_BANK void audio::SfxSamplePlay(const u8 index) {
#if FAMISTUDIO_CFG_DPCM_SUPPORT
    famistudio_sfx_sample_play(index);
#else
    (void)index;
#endif
}
