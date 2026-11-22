/**
 * @file mmc3.cpp
 * @brief The emu-side half of MMC3: the strong ppu::ResolveTile override that
 *        resolves a tile through the current CHR banks, the scanline IRQ
 *        scheduled by pixel position rather than A12 edges, and the
 *        four-screen nametable count.
 */
#include <pnes/mappers/mmc3.hpp>
#include <pnes/video.hpp>
#include <pnes/interrupts.hpp>

#include <cstdlib>

mmc3::Register<6> mmc3::window1Control;
mmc3::Register<7> mmc3::window2Control;

mmc3::Register<0> mmc3::chr0Control;
mmc3::Register<1> mmc3::chr1Control;
mmc3::Register<2> mmc3::chr2Control;
mmc3::Register<3> mmc3::chr3Control;
mmc3::Register<4> mmc3::chr4Control;
mmc3::Register<5> mmc3::chr5Control;

bool mmc3::shape = true;
u8   mmc3::banks[6] = {};

void mmc3::NotifyCHRWrite(const u8 index, const u8 bank) {
    banks[index] = bank;
    ++ppu::chrGeneration;
}

void mmc3::NotifyModeWrite(const chrMode mode) {
    const bool coarseFirst = (mode == chrMode::coarseFirst);
    if (coarseFirst == shape) return;   // nothing moved; don't invalidate tiles

    shape = coarseFirst;
    ++ppu::chrGeneration;
}

void mmc3::SetMirroring(const bool horizontal) {
    mirroring = horizontal;
}

// Off-NES there is no A12 edge to count, so the split is scheduled at the pixel
// position the caller actually meant -- see mmc3.hpp on why @p position is not
// derived from @p scanline.
void mmc3::ScheduleScanlineIRQ(const u8, const vec2<u16> position) {
    irq::irqHandler      = irq_vector;
    irq::irqPosition     = position;
    irq::irqPendingValid = true;
}

void mmc3::AcknowledgeScanlineIRQ() {
    irq::irqPendingValid = false;
}

/**
 * @brief Resolves @p tileVMA through the currently-selected CHR banks.
 *
 * Both arrangements are live, selected by ::shape, which tracks D7 of the last
 * $8000 write -- the A12 inversion is a real switchable mechanic off-NES, not a
 * build setting, so a game that banks under one chrMode in one scene and the
 * other in the next resolves correctly in both.
 *
 *   fineFirst (D7=1): $0000-$0FFF is R2-R5 (1 KiB), $1000-$1FFF R0/R1 (2 KiB)
 *   coarseFirst (D7=0): the two halves swapped -- the chip's power-on state
 */
u32 mmc3::GetTileLMA(const u16 tileVMA) {
    constexpr u32 k1K = 0x400;
    const u16 vma = tileVMA & 0x1FFF;
    const u8 bank0 = banks[0] & ~1u;
    const u8 bank1 = banks[1] & ~1u;
    u32 lma;

    if (shape) {
        if      (vma < 0x0800) lma = bank0 * k1K + (vma - 0x0000);
        else if (vma < 0x1000) lma = bank1 * k1K + (vma - 0x0800);
        else if (vma < 0x1400) lma = banks[2] * k1K + (vma - 0x1000);
        else if (vma < 0x1800) lma = banks[3] * k1K + (vma - 0x1400);
        else if (vma < 0x1C00) lma = banks[4] * k1K + (vma - 0x1800);
        else                   lma = banks[5] * k1K + (vma - 0x1C00);
    } else {
        if      (vma < 0x0400) lma = banks[2] * k1K + (vma - 0x0000);
        else if (vma < 0x0800) lma = banks[3] * k1K + (vma - 0x0400);
        else if (vma < 0x0C00) lma = banks[4] * k1K + (vma - 0x0800);
        else if (vma < 0x1000) lma = banks[5] * k1K + (vma - 0x0C00);
        else if (vma < 0x1800) lma = bank0 * k1K + (vma - 0x1000);
        else                   lma = bank1 * k1K + (vma - 0x1800);
    }

    return lma % ppu::chrRomBytes;
}

u32 ppu::ResolveTile(const u16 tileVMA) {
    return mmc3::GetTileLMA(tileVMA);
}

#if ALTERNATIVE_NAMETABLE == 1
// Four, because ALTERNATIVE_NAMETABLE selects the four-screen wiring: a real
// extra cartridge VRAM chip, so each quadrant has dedicated storage and the
// mirroring bit plays no part.
extern const u8 ppu::nametableCount = 4;
#endif
