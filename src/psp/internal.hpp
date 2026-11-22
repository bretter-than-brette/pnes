/**
 * @file internal.hpp
 * @brief Shared declarations for the PSP backend (pspsdk, raw VRAM). Presents the
 *        shared software PPU: the core rasterises into a staging buffer and the
 *        CPU blits it straight into VRAM. No GU at all -- see src/psp/video.cpp.
 */
#ifndef PSP_INTERNAL_H
#define PSP_INTERNAL_H

#include <intsh>
using namespace br0::intsh;

extern void nmi_vector();
extern int quit;

void psp_setup_exit_callback();

void input_init();

#endif // PSP_INTERNAL_H
