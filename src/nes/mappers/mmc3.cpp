#include <pnes/mappers/mmc3.hpp>
#include <pnes/video.hpp>
#include <pnes/save.hpp>

atomic u8 mmc3::selectLatch;

mmc3::Register<6> mmc3::window1Control;
mmc3::Register<7> mmc3::window2Control;

mmc3::Register<0> mmc3::chr0Control;
mmc3::Register<1> mmc3::chr1Control;
mmc3::Register<2> mmc3::chr2Control;
mmc3::Register<3> mmc3::chr3Control;
mmc3::Register<4> mmc3::chr4Control;
mmc3::Register<5> mmc3::chr5Control;

tech::wo_register<0xa001> mmc3::prgRamProtect;
tech::wo_register<0xc000> mmc3::irqLatch;

void mmc3::SetMirroring(const bool horizontal) {
    tech::poke(0xa000, horizontal ? 1 : 0);
}

void mmc3::ScheduleScanlineIRQ(const u8 scanline, vec2<u16>) {
    irqLatch = scanline; // $C000: reload value.
    tech::poke(0xc001, 0);     // $C001: force reload at next clock (value ignored).
    tech::poke(0xe001, 0);     // $E001: enable (value ignored).
}

void mmc3::AcknowledgeScanlineIRQ() {
    tech::poke(0xe000, 0); // $E000: disable + acknowledge pending IRQ (value ignored).
}

#if ALTERNATIVE_NAMETABLE == 1
void ppu::Flush(const u8 nt, const u8 at) {
    tech::peek(raw::PPUSTATUS);
    tech::poke(raw::PPUADDR, 0x20);
    tech::poke(raw::PPUADDR, 0x00);

    for (auto page = 0; page < 4; page++) {
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
#endif

extern "C" void _start();

// Linker-provided: the first two banks of the flat image, anchored to the top
// of the ROM file so the arithmetic is size-independent.
extern "C" const char __mmc3_boot_bank_window1[];
extern "C" const char __mmc3_boot_bank_window2[];

AI static inline u8 BootBank(const char *symbol) {
    return static_cast<u8>(reinterpret_cast<__UINTPTR_TYPE__>(symbol));
}

/**
 * @brief Cold-boot init: silence the scanline IRQ unit, point R6/R7 at the flat
 *        image's first two banks, enable PRG-RAM, then fall through to crt0.
 *
 * Runs BEFORE .bss is zeroed, so raw pokes only -- no Register shadows, and no
 * calls to non-::FIXED functions, since the windows are not established yet.
 * Hardware powers up with R0-R7, mirroring, PRG-RAM and the IRQ unit all
 * undefined, so nothing here can be skipped as probably already correct.
 *
 * The $E000 write is safety-critical rather than tidy: if the IRQ-enable latch
 * powers up set, the scanline counter can fire long before anything arms it --
 * A12 clocks on ordinary $2006 writes, and early boot is full of those. The
 * interrupt-disable flag holds it pending, so it surfaces the moment interrupts
 * are enabled: a crash that looks intermittent because it tracks residual state.
 *
 * R0-R5 and $A000 are deliberately not touched -- CHR banks and mirroring are
 * per-game choices rather than bootstrap facts, and getting them wrong is
 * cosmetic rather than fatal.
 */
extern "C" FIXED void _reset() {
    tech::poke(0xe000, 0);    // IRQ: disable generation + acknowledge any pending/residual IRQ.
    tech::poke(0xa001, 0x80); // PRG-RAM: enable, write-permitted.
    tech::poke(0x8000, static_cast<u8>(mmc3::kChrMode) | 6); tech::poke(0x8001, BootBank(__mmc3_boot_bank_window1));
    tech::poke(0x8000, static_cast<u8>(mmc3::kChrMode) | 7); tech::poke(0x8001, BootBank(__mmc3_boot_bank_window2));
    _start();
}

__attribute__((constructor(101)))
static void SyncBankShadows() {
    mmc3::SwitchBank(mmc3::window1Control, BootBank(__mmc3_boot_bank_window1));
    mmc3::SwitchBank(mmc3::window2Control, BootBank(__mmc3_boot_bank_window2));
}

#ifdef SAVE_FILE
namespace save {
    AI Response Save(const void* const, const std::size_t) { return { .done = true, .err = found }; }
    AI Response CreateSave(const void* const, const std::size_t, const Response* const) { return { .done = true, .err = found }; }
    AI Response Load(void* const, const std::size_t) { return { .done = true, .err = found }; }
    AI Response Load(void* const, const std::size_t, const Response* const) { return { .done = true, .err = found }; }
    AI Response GetSaveLocation() { return { .done = true, .err = found }; }
    AI Response GetMetaData (void* const) { return { .done = true, .err = found }; }
    AI Response SetMetaData (const void* const) { return { .done = true, .err = found }; }

// On a battery cart the save IS the hardware, so there is nothing to locate,
// create or unlink: every call answers found, and Delete removes nothing.
    AI Response Delete() { return { .done = true, .err = found }; }
}

#endif
