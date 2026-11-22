/**
 * @file internal.hpp
 * @brief Shared declarations for the native Wii U backend (devkitPPC + wut + the
 *        SDL2 portlib). The Cafe OS .rpx target, not the vWii .dol the libogc
 *        branch produces.
 *
 * An emulated-PPU build sharing src/emu: the portlib SDL2 wraps GX2, ProcUI, the
 * scan buffers and VPAD/KPAD internally, so the whole backend is ordinary SDL2 --
 * and that is SDL2, entirely separate from the SDL3 desktop backend, so no code
 * is shared with src/SDL3.
 */
#ifndef WIIU_INTERNAL_H
#define WIIU_INTERNAL_H

#include <intsh>
using namespace br0::intsh;

extern void nmi_vector();
extern int quit;

void input_init();

void input_handle_event(const union SDL_Event *e);

#endif // WIIU_INTERNAL_H
