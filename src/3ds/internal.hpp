/**
 * @file internal.hpp
 * @brief Shared declarations for the 3DS backend (libctru + citro2d). Presents
 *        the shared PPU core GPU-natively rather than rasterising: the PICA200
 *        draws the nametable and sprites as textured quads, with the palette
 *        baked into RGBA8 CHR atlases since it has no paletted texture format.
 */
#ifndef CTR_INTERNAL_H
#define CTR_INTERNAL_H

#include <intsh>
using namespace br0::intsh;

extern void nmi_vector();
extern int quit;

#endif // CTR_INTERNAL_H
