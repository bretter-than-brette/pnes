#include <pnes/video.hpp>
#include <pnes/technology.hpp>
#include <pnes/interrupts.hpp>

// Every function here is placed in the project's own video bank where one is
// configured; see technology.hpp's MODULE_PLACEMENT for why that costs inlining.
#ifdef PNES_VIDEO_SECTION
#define VIDEO_BANK MODULE_PLACEMENT(PNES_VIDEO_SECTION)
#else
#define VIDEO_BANK
#endif
#include <intsh>
using namespace br0::intsh;

const u16 video::PatternTables = 0;
constexpr u16 NameTables       = 0x2000;
constexpr u16 PaletteTables    = 0x3f00;
constexpr u16 nVideoRam        = 0x800;
namespace ppu {
tech::wo_register<raw::PPUCTRL>   PPUCTRL;
tech::wo_register<raw::PPUMASK>   PPUMASK;
}   // namespace ppu

namespace oam {
tech::wo_register<ppu::raw::OAMADDR> OAMADDR;
tech::wo_register<ppu::raw::OAMDMA>  OAMDMA;
}   // namespace oam

static u16 xy_to_nt_addr(const u16 x, const u16 y) {
    constexpr u16 base = 0x2000;
    const u16 nt_h = static_cast<u16>((x >> 5 & 1) << 10);
    const u16 col  = x & 0x1F;
    if (y < 30) {
        return base + nt_h + (y << 5) + col;
    }
    const u16 nt_v = static_cast<u16>((y / 30) << 11);
    const u16 row  = y % 30;
    return base + nt_h + nt_v + row * 32 + col;
}

static u16 xy_to_at_addr(const u16 x, const u16 y) {
    constexpr u16 base = 0x2000;
    const u16 nt_h = static_cast<u16>((x >> 5 & 1) << 10);
    const u16 col  = x & 0x1F;
    if (y < 30) {
        return base + nt_h + 0x3C0 + ((y >> 2) << 3) + (col >> 2);
    }
    const u16 nt_v = static_cast<u16>((y / 30) << 11);
    const u16 row  = y % 30;
    return base + nt_h + nt_v + 0x3C0 + (row / 4) * 8 + (col / 4);
}

namespace video {

VIDEO_BANK void WaitForPresent() {
    while (!irq::nmi_done) {}
    irq::nmi_done = false;
}

}   // namespace video

namespace ppu {

VIDEO_BANK void EnableRendering(const u8 ppuCtrl_, const u8 ppuMask_) {
    PPUCTRL = 0x80 | ppuCtrl_;   // each assignment writes shadow + NMI enable
    PPUMASK = ppuMask_;
}

__attribute__((weak))
VIDEO_BANK void Flush(const u8 nt, const u8 at) {
    tech::peek(raw::PPUSTATUS);
    tech::poke(raw::PPUADDR, NameTables >> 8);
    tech::poke(raw::PPUADDR, NameTables & 0xFF);

    for (auto page = 0; page < nVideoRam / 0x400; page++) {
        for (auto nt_hunk = 0; nt_hunk < 0xf0; nt_hunk++) {
            tech::poke(raw::PPUDATA, nt);
            tech::poke(raw::PPUDATA, nt);
            tech::poke(raw::PPUDATA, nt);
            tech::poke(raw::PPUDATA, nt);
        }
        for (u8 at_hunk = 0; at_hunk < 0x10; at_hunk++) {
            tech::poke(raw::PPUDATA, at);
            tech::poke(raw::PPUDATA, at);
            tech::poke(raw::PPUDATA, at);
            tech::poke(raw::PPUDATA, at);

        }
    }
}

__attribute__((hot))
AI void SetScroll(const vec2<u16> pos) {
    const u16 x = pos.x;
    u16 y = pos.y;

    if (y >= 240) {
        y -= 240;
        y ^= 0x100;
    }

    const auto nt = x >> 8 & 0x01
               | y >> 7 & 0x02;

    PPUCTRL = PPUCTRL & 0xFC | nt;

    // The four-write sequence ($2006/$2005/$2005/$2006), which sets X and Y
    // and takes effect immediately, mid-frame. The plain two-$2005 form could
    // not do Y: $2005 only stages t, and t's vertical bits do not reach v until
    // the next pre-render line. The one write that copies t into v is the SECOND
    // $2006, and it carries only t's low byte -- so the high bits have to be
    // staged first and the low byte must land last. Hence four writes, in this
    // order and no other.
    //
    // $2002 resets the write toggle first: the sequence is positional, and
    // entering at w == 1 makes the leading $2006 latch a bogus v on the spot.
    tech::peek(raw::PPUSTATUS);

    tech::poke(raw::PPUADDR, static_cast<u8>(nt << 2));

    tech::poke(raw::PPUSCROLL, static_cast<u8>(y & 0xFF));

    tech::poke(raw::PPUSCROLL, static_cast<u8>(x & 0xFF));

    // v = t, which is what makes the whole thing land now. Not redundant with
    // the two writes above -- it is the same coarse X/Y re-expressed in t's low
    // byte. The cast is load-bearing: (y & 0xF8) << 2 only equals coarse Y's low
    // 3 bits in 7:5 AFTER truncation to 8 bits, and C++ would promote instead.
    tech::poke(raw::PPUADDR,
               static_cast<u8>((y & 0xF8) << 2 | (x & 0xFF) >> 3));
}

__attribute__((hot))
AI void WriteFromBufferToNameTable(
    const u16 address, const u8* source, const u8 sBuffer, const u8 polarity
) {
    u8 ctrl = PPUCTRL & ~ctrl::POLARITY;
    if (polarity) ctrl = ctrl | ctrl::POLARITY;
    PPUCTRL = ctrl;   // one write back: shadow + hardware

    tech::peek(raw::PPUSTATUS);
    tech::poke(raw::PPUADDR, static_cast<u8>(address >> 8));
    tech::poke(raw::PPUADDR, static_cast<u8>(address & 0xFF));

    for (auto i = 0; i < sBuffer; i++) {
        tech::poke(raw::PPUDATA, source[i]);
    }
}

__attribute__((hot))
AI void WriteFromBufferToNameTable(
    const vec2<u16> pos, const u8* source, const u8 sBuffer, const u8 polarity
) {
    WriteFromBufferToNameTable(xy_to_nt_addr(pos.x, pos.y), source, sBuffer, polarity);
}

__attribute__((hot))
AI void WriteRepeatedToNameTable(
    const u16 address, const u8 value, const u8 amt, const u8 polarity
) {
    u8 ctrl = PPUCTRL & ~ctrl::POLARITY;
    if (polarity) ctrl = ctrl | ctrl::POLARITY;
    PPUCTRL = ctrl;   // one write back: shadow + hardware

    tech::peek(raw::PPUSTATUS);
    tech::poke(raw::PPUADDR, static_cast<u8>(address >> 8));
    tech::poke(raw::PPUADDR, static_cast<u8>(address & 0xFF));

    for (u8 i = 0; i < amt; i++) {
        tech::poke(raw::PPUDATA, value);
    }
}

__attribute__((hot))
AI void WriteRepeatedToNameTable(
    const vec2<u16> pos, const u8 value, const u8 amt, const u8 polarity
) {
    WriteRepeatedToNameTable(xy_to_nt_addr(pos.x, pos.y), value, amt, polarity);
}

__attribute__((hot))
AI void WriteSingleToNameTable(const u16 address, const u8 value) {
    tech::peek(raw::PPUSTATUS);
    tech::poke(raw::PPUADDR, static_cast<u8>(address >> 8));
    tech::poke(raw::PPUADDR, static_cast<u8>(address & 0xFF));
    tech::poke(raw::PPUDATA, value);
}

__attribute__((hot))
VIDEO_BANK void WriteSingleToNameTable(const vec2<u16> pos, const u8 value) {
    WriteSingleToNameTable(xy_to_nt_addr(pos.x, pos.y), value);
}

template <typename Idx>
__attribute__((hot))
VIDEO_BANK void WriteFromProviderToNameTable(
    const u16 address, u8 (*fn)(Idx), const u8 amt, const u8 polarity
) {
    u8 ctrl = PPUCTRL & ~ctrl::POLARITY;
    if (polarity) ctrl = ctrl | ctrl::POLARITY;
    PPUCTRL = ctrl;   // one write back: shadow + hardware

    tech::peek(raw::PPUSTATUS);
    tech::poke(raw::PPUADDR, static_cast<u8>(address >> 8));
    tech::poke(raw::PPUADDR, static_cast<u8>(address & 0xFF));

    for (Idx i = 0; i < amt; ++i) {
        tech::poke(raw::PPUDATA, fn(i));
    }
}

template void WriteFromProviderToNameTable<u8>(u16, u8 (*)(u8), u8, u8);
template void WriteFromProviderToNameTable<u16>(u16, u8 (*)(u16), u8, u8);

template <typename Idx>
__attribute__((hot))
VIDEO_BANK void WriteFromProviderToNameTable(
    const vec2<u16> pos, u8 (*fn)(Idx), const u8 amt, const u8 polarity
) {
    WriteFromProviderToNameTable(xy_to_nt_addr(pos.x, pos.y), fn, amt, polarity);
}

template void WriteFromProviderToNameTable<u8>(vec2<u16>, u8 (*)(u8), u8, u8);
template void WriteFromProviderToNameTable<u16>(vec2<u16>, u8 (*)(u16), u8, u8);

__attribute__((hot))
AI void WriteFromBufferToAttributeTable(
    const u16 address, const u8* source, const u8 sBuffer, const u8 polarity
) {
    PPUCTRL = PPUCTRL & ~ctrl::POLARITY;

    if (polarity) {
        const u8 hi = static_cast<u8>(address >> 8);
        const u8 lo = static_cast<u8>(address & 0xFF);
        for (u8 i = 0; i < sBuffer; i++) {
            tech::peek(raw::PPUSTATUS);
            tech::poke(raw::PPUADDR, hi);
            tech::poke(raw::PPUADDR, static_cast<u8>(lo + (i << 3)));
            tech::poke(raw::PPUDATA, source[i]);
        }
        return;
    }

    tech::peek(raw::PPUSTATUS);
    tech::poke(raw::PPUADDR, static_cast<u8>(address >> 8));
    tech::poke(raw::PPUADDR, static_cast<u8>(address & 0xFF));
    for (u8 i = 0; i < sBuffer; i++) {
        tech::poke(raw::PPUDATA, source[i]);
    }
}

__attribute__((hot))
AI void WriteFromBufferToAttributeTable(
    const vec2<u16> pos, const u8* source, const u8 sBuffer, const u8 polarity
) {
    WriteFromBufferToAttributeTable(xy_to_at_addr(pos.x, pos.y), source, sBuffer, polarity);
}

AI void WriteSingleToAttributeTable(const u16 address, const u8 value) {
    tech::peek(raw::PPUSTATUS);
    tech::poke(raw::PPUADDR, static_cast<u8>(address >> 8));
    tech::poke(raw::PPUADDR, static_cast<u8>(address & 0xFF));
    tech::poke(raw::PPUDATA, value);
}

AI void WriteSingleToAttributeTable(const vec2<u16> pos, const u8 value) {
    WriteSingleToAttributeTable(xy_to_at_addr(pos.x, pos.y), value);
}

template <typename Idx>
__attribute__((hot))
AI void WriteFromProviderToAttributeTable(
    const u16 address, u8 (*fn)(Idx), const u8 amt, const u8 polarity
) {
    PPUCTRL = PPUCTRL & ~ctrl::POLARITY;

    if (polarity) {
        for (Idx i = 0; i < amt; ++i) {
            const u16 addr = static_cast<u16>(address + (static_cast<u16>(i) << 3));
            tech::peek(raw::PPUSTATUS);
            tech::poke(raw::PPUADDR, static_cast<u8>(addr >> 8));
            tech::poke(raw::PPUADDR, static_cast<u8>(addr & 0xFF));
            tech::poke(raw::PPUDATA, fn(i));
        }
        return;
    }

    tech::peek(raw::PPUSTATUS);
    tech::poke(raw::PPUADDR, static_cast<u8>(address >> 8));
    tech::poke(raw::PPUADDR, static_cast<u8>(address & 0xFF));
    for (Idx i = 0; i < amt; ++i) {
        tech::poke(raw::PPUDATA, fn(i));
    }
}

template void WriteFromProviderToAttributeTable<u8>(u16, u8 (*)(u8), u8, u8);
template void WriteFromProviderToAttributeTable<u16>(u16, u8 (*)(u16), u8, u8);

template <typename Idx>
__attribute__((hot))
AI void WriteFromProviderToAttributeTable(
    const vec2<u16> pos, u8 (*fn)(Idx), const u8 amt, const u8 polarity
) {
    WriteFromProviderToAttributeTable(xy_to_at_addr(pos.x, pos.y), fn, amt, polarity);
}

template void WriteFromProviderToAttributeTable<u8>(vec2<u16>, u8 (*)(u8), u8, u8);
template void WriteFromProviderToAttributeTable<u16>(vec2<u16>, u8 (*)(u16), u8, u8);

VIDEO_BANK u16 CartesianToAddress(const vec2<u16>& pos) {
    return xy_to_nt_addr(pos.x, pos.y);
}

AI scroll_t CartesianToScroll(const vec2<u16> pos) {
    const u16 px = pos.x;
    auto y = pos.y;

    if (y >= 240) { y -= 240; y ^= 0x100; }

    const auto nt = static_cast<u8>(px >> 8 & 0x01 | y >> 7 & 0x02);

    return (scroll_t){{
        static_cast<u8>(nt << 2),                               // 1. $2006
        static_cast<u8>(y & 0xFF),                              // 2. $2005 (Y)
        static_cast<u8>(px & 0xFF),                             // 3. $2005 (X)
        static_cast<u8>((y & 0xF8) << 2 | (px & 0xFF) >> 3),    // 4. $2006
    }};
}

__attribute__((hot))
AI void StageScroll(const scroll_t s) {
    PPUCTRL = PPUCTRL & 0xFC | s.data[0] >> 2;

    tech::peek(raw::PPUSTATUS);   // reset w; the sequence is positional
    tech::poke(raw::PPUADDR,   s.data[0]);
    tech::poke(raw::PPUSCROLL, s.data[1]);
    tech::poke(raw::PPUSCROLL, s.data[2]);
}

__attribute__((hot))
AI void LatchScroll(const scroll_t s) {
    tech::poke(raw::PPUADDR, s.data[3]);
}

__attribute__((hot))
AI void SetColorPriority(const u8 priority) {
    u8 mask = PPUMASK & ~(mask::RED | mask::GREEN | mask::BLUE);
    mask = mask | priority & (mask::RED | mask::GREEN | mask::BLUE);
    PPUMASK = mask;   // one write back: shadow + hardware
}

namespace pal {

__attribute__((hot))
VIDEO_BANK void WriteFromBuffer(const u8 offset, const u8* source, const u8 sBuffer) {
    tech::peek(raw::PPUSTATUS);
    tech::poke(raw::PPUADDR, static_cast<u8>((offset + PaletteTables) >> 8));
    tech::poke(raw::PPUADDR, static_cast<u8>(offset + PaletteTables & 0xFF));

    for (auto i = 0; i < sBuffer; i++) {
        tech::poke(raw::PPUDATA, source[i]);
    }
}

AI void WriteSingle(const u8 offset, const u8 value) {
    tech::peek(raw::PPUSTATUS);
    tech::poke(raw::PPUADDR, static_cast<u8>((offset + PaletteTables) >> 8));
    tech::poke(raw::PPUADDR, static_cast<u8>(offset + PaletteTables & 0xFF));
    tech::poke(raw::PPUDATA, value);
}

}   // namespace pal

}   // namespace ppu

namespace oam {

AI void RefreshSprites(const sprite_t *oam) {
    u16 addr;
    __builtin_memcpy(&addr, &oam, sizeof addr);
    tech::poke(ppu::raw::OAMDMA, static_cast<u8>(addr >> 8));
}

VIDEO_BANK void OAMFromBuffer(sprite_t *oam, const u8 slot, const u16 off,
                   const u8 width, const u8 *src, const u16 count) {
    (void)width;
    u8 *dst = reinterpret_cast<u8 *>(oam) + static_cast<u16>(slot) * spriteStride + off;
    const u8 *s = src + off;
    for (u16 i = 0; i < count; i++)
        dst[i * spriteStride] = s[i * spriteStride];
}

VIDEO_BANK void OAMFromProvider(sprite_t *oam, const u8 slot, const u16 off,
                     const u8 width, oam_t (*fn)(u16), const u16 count) {
    (void)width;
    u8 *base = reinterpret_cast<u8 *>(oam) + static_cast<u16>(slot) * spriteStride + off;
    for (u16 i = 0; i < count; i++)
        base[i * spriteStride] = fn(i);
}

}   // namespace oam

namespace ppu {

VIDEO_BANK void StreamFromVideoMemory(const u16 offset, atomic u8* target, const u8 size) {
    tech::peek(raw::PPUSTATUS);
    tech::poke(raw::PPUADDR, static_cast<u8>(offset >> 8));
    tech::poke(raw::PPUADDR, static_cast<u8>(offset & 0xFF));
    for (auto i = 0; i < size; i++) {
        target[i] = tech::peek(raw::PPUDATA);
    }
}

}   // namespace ppu

__attribute__((hot))
VIDEO_BANK void video::WaitThenReactToSpriteZero(const vec2<u16> pos, void (*fn)(), atomic u8* latch) {
    (void)pos;

    while (!*latch) {
        while (  tech::peek(ppu::raw::PPUSTATUS) & 0x40)  { }  // wait for pre-render to clear stale hit
        while (!(tech::peek(ppu::raw::PPUSTATUS) & 0x40)) { }  // wait for actual sprite 0 hit
        fn();
        *latch = true;
    }
}

/**
 * @brief Times a vblank interval to identify the console's video system.
 *
 * The standard NESdev routine, and the reason to prefer it is the missed-flag
 * case: reading $2002 clears bit 7, so a read can land exactly on the PPU
 * setting it and lose that vblank -- and the interval measured is then TWO
 * frames. A threshold comparison reads that as the slower console; the published
 * counts separate the two cases instead, being distinct in Y alone ($09/$0A/$0B
 * against $13/$15/$17), so `cmp #16` tells them apart and `lsr` folds a double
 * onto a single. Loop is 12.005 cycles per iteration.
 *
 * Dendy refreshes at 50Hz despite its NTSC-like clock, so it reports true;
 * anything unclassified falls back to 60Hz, which is the safer wrong answer.
 *
 * Polls PPUSTATUS rather than NMI because of when it runs -- from a global
 * constructor, before game code has enabled rendering or NMI. $2000 is zeroed
 * first, which the published routine does not do because it assumes it runs at
 * the top of reset: PPUCTRL powers up indeterminate, and a set NMI-enable bit
 * would fire into the middle of the count.
 */
bool video::DetectRefresh() {
    u8 system, countLo, countHi;
    __asm__ volatile(
        "lda #0\n\t"
        "sta $2000\n\t"        // NMI off, so nothing interrupts the count.
        "ldx #0\n\t"
        "ldy #0\n\t"
        "1:\n\t"               // Sync to a flag edge.
        "bit $2002\n\t"
        "bpl 1b\n\t"
        "2:\n\t"               // Count 12.005 cycles per iteration until the next.
        "inx\n\t"
        "bne 3f\n\t"
        "iny\n\t"
        "3:\n\t"
        "bit $2002\n\t"
        "bpl 2b\n\t"
        "tya\n\t"              // Classify on the high byte alone (see above).
        "cmp #16\n\t"
        "bcc 4f\n\t"
        "lsr a\n\t"            // Two vblanks were measured; fold onto one.
        "4:\n\t"
        "clc\n\t"
        "adc #$f7\n\t"         // -9: $09/$0A/$0B -> 0/1/2.
        "cmp #3\n\t"
        "bcc 5f\n\t"
        "lda #3\n\t"           // Unclassified.
        "5:"
        // Register outputs, not "=m", and that is load-bearing: llvm-mos expands
        // an "=m" operand into a zero-page POINTER PAIR and addresses the
        // variable indirectly, so `sta %0` overwrites the pointer's low byte
        // instead. It compiles clean, disassembles plausibly, and the value never
        // arrives -- this returned false on every console until these became
        // register constraints.
        : "=a"(system), "=x"(countLo), "=y"(countHi));

    (void)countLo;
    (void)countHi;

    return system == 1 || system == 2;
}

/// NES's ::RESET has no irq::init hook to carry this. Priority 102, after
/// mmc3.cpp's SyncBankShadows at 101: this spins for up to two frames, so there
/// is no reason to do it with the bank shadows still unsynced.
__attribute__((constructor(102)))
static void DetectRefreshOnBoot() { video::is50Hz = video::DetectRefresh(); }
