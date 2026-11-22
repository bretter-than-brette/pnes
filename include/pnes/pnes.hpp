/**
 * @file pnes.hpp
 * @brief Umbrella header: pulls in every public subsystem and establishes the
 *        compile-time configuration. Exactly one of ::LANDSCAPE or ::PORTRAIT
 *        must be defined; ::TARGET_NES selects the 6502 backend. Include this
 *        rather than the individual headers.
 */
#pragma once
#include <intsh>
using namespace br0::intsh;

#if !defined(__GNUC__) && !defined(__clang__)
#error "pnes requires GCC or Clang. MSVC is not supported."
#endif

#if defined(LANDSCAPE) && defined(PORTRAIT)
#error "Must not define both Mirroring"
#endif

#if !defined(LANDSCAPE) && !defined(PORTRAIT)
#error "Must define Mirroring"
#endif



#ifndef __mos__
    #define interrupt_norecurse
#endif

#ifdef TARGET_NES
#define quit 0
#else
/// The game loop's own exit flag off NES, where a window can be closed. On NES
/// it is a literal 0, so `while (!quit)` folds away.
extern int quit;
#endif

#include "types.hpp"
#include "technology.hpp"
#include "input.hpp"
#include "video.hpp"
#include "audio.hpp"
#include "interrupts.hpp"