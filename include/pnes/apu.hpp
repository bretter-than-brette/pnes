/**
 * @file apu.hpp
 * @brief The 2A03's write-only register file as ::wo_register shadows, so a read
 *        returns the last value written, plus the ::APU_REGISTERS family for
 *        bracketing an ISR that touches the APU with ::PRESERVE / ::RESTORE.
 *
 * The addresses are meaningless off NES, so those targets get an inert stand-in
 * rather than an absent header.
 */
#pragma once

#ifdef TARGET_NES

#include <pnes/technology.hpp>

namespace apu {
    extern tech::wo_register<0x4000> sq1_vol;       ///< Pulse 1 — duty / envelope / volume
    extern tech::wo_register<0x4001> sq1_sweep;     ///< Pulse 1 — sweep unit
    extern tech::wo_register<0x4002> sq1_lo;        ///< Pulse 1 — timer low 8 bits
    extern tech::wo_register<0x4003> sq1_hi;        ///< Pulse 1 — length counter + timer high

    extern tech::wo_register<0x4004> sq2_vol;       ///< Pulse 2 — duty / envelope / volume
    extern tech::wo_register<0x4005> sq2_sweep;     ///< Pulse 2 — sweep unit
    extern tech::wo_register<0x4006> sq2_lo;        ///< Pulse 2 — timer low 8 bits
    extern tech::wo_register<0x4007> sq2_hi;        ///< Pulse 2 — length counter + timer high

    extern tech::wo_register<0x4008> tri_linear;    ///< Triangle — linear counter control
    extern tech::wo_register<0x400A> tri_lo;        ///< Triangle — timer low 8 bits
    extern tech::wo_register<0x400B> tri_hi;        ///< Triangle — length counter + timer high

    extern tech::wo_register<0x400C> noise_vol;     ///< Noise — envelope / volume
    extern tech::wo_register<0x400E> noise_lo;      ///< Noise — mode + period
    extern tech::wo_register<0x400F> noise_hi;      ///< Noise — length counter

    extern tech::wo_register<0x4010> dmc_freq;      ///< DMC — IRQ enable, loop, rate index
    extern tech::wo_register<0x4011> dmc_raw;       ///< DMC — direct load (7-bit DAC level)
    extern tech::wo_register<0x4012> dmc_start;     ///< DMC — sample start address ($C000 base)
    extern tech::wo_register<0x4013> dmc_len;       ///< DMC — sample byte count

    extern tech::wo_register<0x4015> snd_chn;       ///< Channel enable / length counter status
    extern tech::wo_register<0x4017> frame_counter; ///< Frame counter mode + IRQ inhibit

    inline void DisableFrameIRQ() { frame_counter = static_cast<u8>(frame_counter) | 0xC0; }
    inline void EnableFrameIRQ()  { frame_counter = static_cast<u8>(frame_counter) & ~0x40; }

    inline void EnableDMCIRQ()    { dmc_freq = static_cast<u8>(dmc_freq) | 0x80; }
    inline void DisableDMCIRQ()   { dmc_freq = static_cast<u8>(dmc_freq) & ~0x80; }
}

/// Every ::apu field except the DMC registers and snd_chn, which are
/// hardware-distinct from the melodic channels and managed where needed.
/// APU_REGISTERS_snapshot is the flat storage, sized to match this list.
#define APU_REGISTERS \
    apu::sq1_vol,   apu::sq1_sweep, apu::sq1_lo,  apu::sq1_hi,  \
    apu::sq2_vol,   apu::sq2_sweep, apu::sq2_lo,  apu::sq2_hi,  \
    apu::tri_linear,apu::tri_lo,    apu::tri_hi,                 \
    apu::noise_vol, apu::noise_lo,  apu::noise_hi,               \
    apu::frame_counter

extern u8 APU_REGISTERS_snapshot[15];

#else

#include <pnes/technology.hpp>

/// Inert: no 2A03 to address, and nothing reads these back. Present so portable
/// call sites compile unchanged.
namespace apu {
    extern u8 sq1_vol, sq1_sweep, sq1_lo, sq1_hi;
    extern u8 sq2_vol, sq2_sweep, sq2_lo, sq2_hi;
    extern u8 tri_linear, tri_lo, tri_hi;
    extern u8 noise_vol, noise_lo, noise_hi;
    extern u8 dmc_freq, dmc_raw, dmc_start, dmc_len;
    extern u8 snd_chn, frame_counter;

    inline void DisableFrameIRQ() {}
    inline void EnableFrameIRQ()  {}
    inline void EnableDMCIRQ()    {}
    inline void DisableDMCIRQ()   {}
}

#define APU_REGISTERS \
    apu::sq1_vol,   apu::sq1_sweep, apu::sq1_lo,  apu::sq1_hi,  \
    apu::sq2_vol,   apu::sq2_sweep, apu::sq2_lo,  apu::sq2_hi,  \
    apu::tri_linear,apu::tri_lo,    apu::tri_hi,                 \
    apu::noise_vol, apu::noise_lo,  apu::noise_hi,               \
    apu::frame_counter

extern u8 APU_REGISTERS_snapshot[15];

#endif // TARGET_NES