/**
 * @file internal.hpp
 * @brief Shared declarations for the DS and DSi backend (libnds + calico). Maps
 *        the NES PPU onto the console native 2D hardware rather than
 *        rasterising: CHR becomes tile VRAM, the nametable a BG tilemap, OAM
 *        becomes hardware OBJ, and the band timeline a per-scanline HBlank
 *        scroll table.
 */
#ifndef NDS_INTERNAL_H
#define NDS_INTERNAL_H

#include <intsh>
using namespace br0::intsh;

extern void nmi_vector();
extern int quit;

#endif // NDS_INTERNAL_H
