#pragma once
#include <pnes/headers/nes2.hpp>

// $6000-$7FFF is declared PRG-NVRAM with the battery bit set, taken from the
// SAVE_FILE define rather than an argument. MMC3 makes this all-or-nothing: one
// 8 KiB chip at that window, battery-backed entirely or not at all. HEADER()
// static_asserts that SAVE_FILE and prg_nvram_bytes cannot drift apart.
HEADER(
    Mirroring::Horizontal,
    262144, 16384,
    0, 8192,
    0, 0,
    // Timing::MULTI, not NTSC or PAL, and no longer tied to any build flag: this
    // ROM genuinely runs on both. Declaring one region would be a claim the image
    // no longer makes -- and the wrong one is what an auto-region emulator would
    // then act on.
    Timing::MULTI
);