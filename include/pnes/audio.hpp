/**
 * @file audio.hpp
 * @brief Music and sound-effect playback over two unlike backends: the NES
 *        drives FamiStudio's engine on byte streams in ROM, everything else
 *        decodes PCM and mixes it.
 *
 * An application defines ::tracks itself, registers effects with ::audio::SetSfx
 * off-NES (on NES the backend names the effect bank itself), then calls the
 * play/update helpers.
 */
#pragma once
#include <intsh>
using namespace br0::intsh;


/*
 * pnes ships no audio driver: these are plain prototypes, and a definition
 * arrives when the project's chosen backend is compiled in. FamiStudio's is
 * placed by the project (PNES_AUDIO_SECTION) into the same PRG bank as the
 * engine it drives -- that adjacency is what lets it use plain calls.
 */

/// Warns at the call site, so it costs nothing unless the call exists -- unlike a
/// #warning, which fires in every TU that includes this header.
#if defined(TARGET_NES) && defined(__has_attribute)
#  if __has_attribute(warning)
#    define PNES_SFX_UNSUPPORTED __attribute__((warning( \
         "this NES audio driver cannot stop or pause an effect: FamiStudio's " \
         "engine has no entry point for it, so the call does nothing -- see " \
         ".ignore/famistudio-sfx-stop/NOTES.md")))
#  endif
#endif
#ifndef PNES_SFX_UNSUPPORTED
#  define PNES_SFX_UNSUPPORTED
#endif

namespace audio {

#ifndef TARGET_NES
typedef struct {
    const char* fp;               /**< Filesystem path to the PCM/ogg/wav source. */
    float    loop_start;          /**< Byte offset where the loop begins (0 = loop the whole song). */
} music_t;

typedef struct {
    const char* fp;          /**< Filesystem path to the PCM/ogg/wav source. */
} sfx_t;
#endif

/// ::SfxPlay's channel is an index into these, and nothing checks it. Differs by
/// platform deliberately: an APU stream costs real channel hardware.
#ifdef TARGET_NES
#if !defined(FAMISTUDIO_CFG_SFX_STREAMS)
#error "FAMISTUDIO_CFG_SFX_STREAMS is not defined. CMakeLists.txt passes it \
as a compile definition, derived from PNES_SFX_STREAMS -- the same value it \
feeds to the engine's own config. audio::sfxChannels cannot be honest without it."
#endif
constexpr u8 sfxChannels = FAMISTUDIO_CFG_SFX_STREAMS;
#else
constexpr u8 sfxChannels = 4;
#endif

} // namespace audio

#ifndef TARGET_NES
namespace audio {
extern float *pcm_buffer;
extern u32 pcm_buffer_size;
}
/// Defined by the application, in ordinary C++: an array of ::audio::music_t and
/// its count, at file scope. A ::TRACKS macro used to wrap those two lines
/// differently per platform, so one name meant two unrelated things.
extern const audio::music_t tracks[];
extern const u8 nTracks;

/// Library state, not application API: owned by src/all/audio.cpp and written
/// only by ::audio::SetSfx. Null and zero until that runs, so every reader must
/// bound-check against ::nSfx.
extern const audio::sfx_t* sfxs;
extern u8 nSfx;
#else
/// Defined by the application, since the export's symbol carries the FamiStudio
/// project's name. The pointer itself is const because audio::Init hands it to
/// famistudio_init through an inline-asm "i" operand, so it must be a link-time
/// constant -- and this extern is what gives it external linkage at all.
extern const u8* const tracks;

/* No `sfx` counterpart: FamiStudio hardcodes its effect symbol (always
 * `sounds`), so the backend names it privately. Only the music symbol carries
 * the project's name. */


#endif

namespace audio {

void TrackPlay(u8 index);

void TrackPause(u8 pause);

void TrackStop();

/// 0-100, linear in amplitude and clamped at unity. Music only, except on the
/// PS2 whose audsrv volume is device-wide. A no-op on NES, whose engine has no
/// master level -- FamiStudio's volume lives per note.
void TrackVolume(u8 percent);

/// Starting an effect on a busy channel cuts the old one off, as FamiStudio's own
/// engine does. @p volume is a byte this layer does not interpret: 0-255 linear
/// on SDL3/Switch/Wii U, straight to ASND on GC/Wii, /255 for ndsp on 3DS,
/// discarded on NES.
void SfxPlay(u8 index, u8 channel, u8 volume) ;

/// Not supported on NES, and a call site there warns rather than going silent:
/// FamiStudio's engine exposes eight entry points and not one cancels an effect.
/// Workarounds are in .ignore/famistudio-sfx-stop/NOTES.md.
PNES_SFX_UNSUPPORTED void SfxStop(u8 channel);

PNES_SFX_UNSUPPORTED void SfxPause(u8 channel, u8 pause);

/// On NES this drives the DMC channel, so @p index selects from the DPCM sample
/// list the MUSIC export carries -- not an ::sfxs index. Elsewhere every effect
/// is already a one-shot and this forwards to ::SfxPlay.
void SfxSamplePlay(u8 index);

#ifndef TARGET_NES
/// Call it before ::Init, which is where the backends that decode up front read
/// it; no later call makes them go back. The table is not copied, so it must
/// outlive every audio call. Not declared on NES, where the effect bank is a ROM
/// symbol the backend names itself.
void SetSfx(const sfx_t* table, u8 count);
#endif

/// May fail, and never says so: the API goes inert and the game plays on silently
/// rather than every call site handling an error it cannot act on.
///
/// @note The 3DS needs dspfirm.cdc dumped from the player's own console, so
/// without it the game is mute; src/3ds/audio.cpp leaves a note on the SD card.
void Init(u8 region = 0);

/// The engine walks its song data while executing, so code and data must be
/// mapped together and cannot share a window. The mapper is a template parameter
/// rather than an include, which keeps this in the portable header.
template <typename Mapper, typename CodeTag, typename DataTag, typename Block>
decltype(auto) InBanks(Block &&block) {
#ifdef TARGET_NES
    return Mapper::template CallPairedBlock<CodeTag, DataTag>(
        static_cast<Block &&>(block));
#else
    return block();
#endif
}

void Update();

} // namespace audio