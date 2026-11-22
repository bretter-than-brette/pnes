/**
 * @file internal.hpp
 * @brief Shared declarations for the PS3 backend (PSL1GHT, RSX-mapped
 *        framebuffer).
 *
 * Presents the shared software PPU rather than driving native hardware: the Cell
 * PPU is more than enough for a 256x240 frame, so the core rasterises into a
 * staging buffer and the CPU integer-scale-blits that into an RSX-mapped display
 * buffer which gcmSetFlip() presents. No part of the RSX 3D pipeline is used --
 * see src/ps3/video.cpp.
 */
#ifndef PS3_INTERNAL_H
#define PS3_INTERNAL_H

// Must precede every engine header: ppu-types.h's u8..s64 typedefs have to be
// visible before the intsh shim's `using namespace` introduces a second
// unqualified spelling. Same ordering requirement as src/ps2/internal.hpp.
#include <ppu-types.h>

#include <intsh>
using namespace br0::intsh;

extern void nmi_vector();
extern int quit;

void ps3_setup_exit_callback();

void ps3_poll_sysutil();

void input_init();

void input_exit();

// Boot tracing, off by default: the PS3 gives no console before libaudio and
// video are up, so this writes through the lv2 TTY from the first instruction.
#if defined(PS3_BOOT_TRACE) && PS3_BOOT_TRACE
#  include <sys/tty.h>
#  include <cstring>
inline void ps3_trace(const char *msg) {
    u32 written = 0;
    sysTtyWrite(0, "pnes: ", 6, &written);
    sysTtyWrite(0, msg, static_cast<u32>(std::strlen(msg)), &written);
    sysTtyWrite(0, "\n", 1, &written);
}
#else
inline void ps3_trace(const char *) {}
#endif

#endif // PS3_INTERNAL_H
