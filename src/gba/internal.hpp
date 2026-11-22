/**
 * @file internal.hpp
 * @brief Shared declarations for the GBA backend (devkitARM + libgba). Like the
 *        DS backend, this maps the NES PPU onto the console native 2D hardware
 *        rather than rasterising: CHR becomes tile VRAM, the nametable a BG
 *        tilemap, OAM becomes hardware OBJ, and the band timeline a
 *        per-scanline HBlank scroll table.
 */
#ifndef GBA_INTERNAL_H
#define GBA_INTERNAL_H

#include <intsh>
using namespace br0::intsh;

extern void nmi_vector();
extern int quit;

#endif // GBA_INTERNAL_H
