/**
 * @file internal.hpp
 * @brief Shared declarations for the PS2 backend (ps2sdk, gsKit).
 *
 * Presents the shared software PPU through the GS rather than any native tile
 * hardware: the core rasterises into an ARGB8888 staging buffer in EE RAM, which
 * gsKit DMAs into GS VRAM as a texture and draws as one scaled quad.
 *
 * This includes <tamtypes.h> FIRST, ahead of any engine header, which is what
 * makes the intsh shim correct: ps2sdk's own u8..s64 typedefs have to be the only
 * ones in scope by the time the engine headers are parsed.
 */
#ifndef PS2_INTERNAL_H
#define PS2_INTERNAL_H

#ifndef _EE
#define _EE
#endif
#include <tamtypes.h>

extern void nmi_vector();
extern int quit;

void input_init();

#endif // PS2_INTERNAL_H
