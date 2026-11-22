/**
 * @file internal.hpp
 * @brief Shared declarations for the Switch backend (devkitA64 + libnx). An
 *        emulated-PPU build: the Tegra is overpowered for a 256x240 frame, so the
 *        core rasterises into a staging buffer and the CPU integer-scales it into
 *        the libnx framebuffer rather than going near deko3d.
 */
#ifndef NX_INTERNAL_H
#define NX_INTERNAL_H

#include <intsh>
using namespace br0::intsh;

extern void nmi_vector();
extern int quit;

void input_init();

#endif // NX_INTERNAL_H
