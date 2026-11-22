#pragma once

#include <intsh>
using namespace br0::intsh;

#ifdef TARGET_NES

// Also needed off-NES, where CMake may not pass the define through.
#ifndef ALTERNATIVE_NAMETABLE
#define ALTERNATIVE_NAMETABLE 0
#endif

/// The low-level primitive: 16 unchecked positional bytes the caller computed.
/// They land in .nes2_header, which the mapper's linker script places ahead of
/// PRG/CHR. Prefer ::HEADER, which computes every byte from named fields.
#define NES2_HEADER(b0, b1, b2, b3, b4, b5, b6, b7, b8, b9, b10, b11, b12, b13, b14, b15) \
    extern "C" [[gnu::section(".nes2_header"), gnu::used, gnu::retain]]                  \
    constexpr u8 _nes2_header[16] = {                                                    \
        (b0), (b1), (b2), (b3), (b4), (b5), (b6), (b7),                                  \
        (b8), (b9), (b10), (b11), (b12), (b13), (b14), (b15)                             \
    }

enum class Mirroring : u8 {
    Horizontal = 0,
    Mapper     = 0,
    Vertical   = 1,
};

enum class Timing : u8 {
    NTSC  = 0,
    PAL   = 1,
    MULTI = 2,
    DENDY = 3,
};

namespace nes2 {
    /// Driven by the ::SAVE_FILE define, which exists only when saving is on --
    /// CMake gates it on truthiness. Hence the definedness test, and hence no
    /// `#ifndef SAVE_FILE` fallback: one would make the define exist for every TU
    /// and mmc3.cpp's own `#ifdef` would always take the saving arm.
#ifdef SAVE_FILE
    constexpr bool save_file = true;
#else
    constexpr bool save_file = false;
#endif

    constexpr unsigned long long odd_part(unsigned long long bytes, unsigned &e) {
        e = 0;
        while (bytes != 0 && (bytes & 1) == 0) { bytes >>= 1; ++e; }
        return bytes;
    }

    constexpr bool exponent_representable(unsigned long long bytes) {
        if (bytes == 0) return true;
        unsigned e;
        return odd_part(bytes, e) <= 7;
    }

    constexpr u8 exponent_byte(unsigned long long bytes) {
        if (bytes == 0) return 0;
        unsigned e;
        const auto odd = odd_part(bytes, e);
        return static_cast<u8>((e << 2) | ((odd - 1) / 2));
    }

    /// $F in the size field is reserved to select the exponent form, so $EFF is
    /// the largest linear quotient.
    constexpr bool linear_representable(unsigned long long bytes, unsigned long long unit) {
        return bytes % unit == 0 && (bytes / unit) <= 0xEFF;
    }

    /// The spec permits the exponent form only where the linear one cannot
    /// express the size, so prefer linear wherever both apply.
    constexpr bool rom_size_representable(unsigned long long bytes, unsigned long long unit) {
        return linear_representable(bytes, unit) || exponent_representable(bytes);
    }

    constexpr u8 rom_size_byte(unsigned long long bytes, unsigned long long unit) {
        if (bytes == 0) return 0;
        if (linear_representable(bytes, unit)) {
            return static_cast<u8>((bytes / unit) & 0xFF);
        }
        return exponent_byte(bytes);
    }

    constexpr u8 rom_msb_nibble(unsigned long long bytes, unsigned long long unit) {
        if (bytes == 0) return 0x0;
        if (linear_representable(bytes, unit)) {
            return static_cast<u8>((bytes / unit >> 8) & 0xF);
        }
        return 0xF;
    }

    constexpr bool ram_size_representable(unsigned long long bytes) {
        if (bytes == 0) return true;
        unsigned long long v = 128;
        unsigned n = 1;
        while (v < bytes && n < 15) { v <<= 1; ++n; }
        return v == bytes;
    }

    constexpr u8 ram_shift(unsigned long long bytes) {
        if (bytes == 0) return 0;
        unsigned long long v = 128;
        unsigned n = 1;
        while (v < bytes) { v <<= 1; ++n; }
        return static_cast<u8>(n);
    }
} // namespace nes2

/// Console type is always NES/Famicom and the unsupported fields are zero, so
/// neither is a parameter. Mapper, submapper, four-screen wiring and the battery
/// bit come from compile defines rather than arguments: each is a fact about the
/// board being built. The battery bit and @p prg_nvram_bytes must agree, and a
/// static_assert enforces it both ways -- a non-volatile chip with the bit clear
/// silently produces no save file at all.
#define HEADER(mirroring,                                                                        \
               prg_rom_bytes, chr_rom_bytes,                                                     \
               prg_ram_bytes, prg_nvram_bytes,                                                   \
               chr_ram_bytes, chr_nvram_bytes,                                                   \
               timing)                                                                           \
    static_assert(::nes2::rom_size_representable(prg_rom_bytes, 16384),                          \
        "PRG-ROM size is not representable in NES2.0's header: it must be a "                    \
        "multiple of 16 KiB no larger than 0xEFF units, or (2^E)*(1,3,5 or 7) bytes");            \
    static_assert(::nes2::rom_size_representable(chr_rom_bytes, 8192),                           \
        "CHR-ROM size is not representable in NES2.0's header: it must be a "                    \
        "multiple of 8 KiB no larger than 0xEFF units, or (2^E)*(1,3,5 or 7) bytes");             \
    static_assert(::nes2::ram_size_representable(prg_ram_bytes),                                 \
        "PRG-RAM size must be 0 (absent) or 64 << n bytes, n in 1..15 (128 B .. 2 MiB)");         \
    static_assert(::nes2::ram_size_representable(prg_nvram_bytes),                                \
        "PRG-NVRAM/EEPROM size must be 0 (absent) or 64 << n bytes, n in 1..15");                 \
    static_assert(::nes2::ram_size_representable(chr_ram_bytes),                                 \
        "CHR-RAM size must be 0 (absent) or 64 << n bytes, n in 1..15");                          \
    static_assert(::nes2::ram_size_representable(chr_nvram_bytes),                               \
        "CHR-NVRAM size must be 0 (absent) or 64 << n bytes, n in 1..15");                        \
    static_assert(::nes2::save_file == ((prg_nvram_bytes) != 0),                                 \
        "SAVE_FILE and this HEADER()'s prg_nvram_bytes disagree: a battery-backed cart must "     \
        "declare the non-volatile chip the battery backs, and a cart that declares one must set " \
        "SAVE_FILE in local.cmake -- otherwise the ROM header's battery bit stays clear and no "  \
        "emulator ever creates a save file for it");                                              \
    static_assert(MAPPER <= 0xFFF, "MAPPER exceeds NES2.0's 12-bit mapper number field");         \
    static_assert(SUBMAPPER <= 0xF, "SUBMAPPER exceeds NES2.0's 4-bit submapper field");          \
    NES2_HEADER(                                                                                 \
        'N', 'E', 'S', 0x1a,                                                                     \
        ::nes2::rom_size_byte(prg_rom_bytes, 16384),                                              \
        ::nes2::rom_size_byte(chr_rom_bytes, 8192),                                                \
        (static_cast<u8>(mirroring) | (static_cast<u8>(::nes2::save_file) << 1) |                \
            (static_cast<u8>((ALTERNATIVE_NAMETABLE) != 0) << 3) | ((MAPPER & 0xF) << 4)),       \
        (0b1000 | (((MAPPER >> 4) & 0xF) << 4)),                                                 \
        (((MAPPER >> 8) & 0xF) | (SUBMAPPER << 4)),                                               \
        ((::nes2::rom_msb_nibble(chr_rom_bytes, 8192) << 4) |                                     \
            ::nes2::rom_msb_nibble(prg_rom_bytes, 16384)),                                        \
        ((::nes2::ram_shift(prg_nvram_bytes) << 4) | ::nes2::ram_shift(prg_ram_bytes)),           \
        ((::nes2::ram_shift(chr_nvram_bytes) << 4) | ::nes2::ram_shift(chr_ram_bytes)),           \
        static_cast<u8>(timing),                                                                 \
        0, 0, 0                                                                                  \
    )

#else
#define NES2_HEADER(...)
#define HEADER(...)
#endif
