#include <pnes/audio.hpp>
#include <intsh>
using namespace br0::intsh;

/// The registered SFX table, owned here rather than per backend: every src/all
/// source is compiled into each platform library, so the eight backends that read
/// it share one definition. Nothing here is NES -- that backend names FamiStudio
/// effect bank itself, since the exporter fixes that symbol.
#ifndef TARGET_NES

const audio::sfx_t* sfxs = nullptr;
u8 nSfx = 0;

void audio::SetSfx(const sfx_t* const table, const u8 count) {
    sfxs = table;
    nSfx = table ? count : 0;
}

#endif
