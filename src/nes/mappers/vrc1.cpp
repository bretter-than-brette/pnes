#include <pnes/mappers/vrc1.hpp>

tech::wo_register<0x8000> VRC1::window1Control;
tech::wo_register<0xa000> VRC1::window2Control;
tech::wo_register<0xc000> VRC1::window3Control;

extern "C" void _start();

extern "C" FIXED void _reset() {
    tech::poke(0x8000, 0);
    tech::poke(0xa000, 1);
    tech::poke(0xc000, 2);
    _start();
}


// Link-time tests of the banked-call machinery: each tag's layout is
// hand-entered and cross-checked by an ASSERT in the linker script, so a drift
// between the two fails the build rather than executing the wrong bank.
struct window_test_tag {};

template <> struct VRC1::bank_layout<window_test_tag> {
    static constexpr section_t section() {
        return { 0xdfc0, 6 };
    }
};

VRC1_BANKED(".banked_call_window_test", window_test, void, BankedCallWindowTest);

volatile u8 bankedCallWindowTestMarker = 0;

[[gnu::noinline]] void BankedCallWindowTest() {
    bankedCallWindowTestMarker = 1;
}

struct bank3_test_tag {};

template <> struct VRC1::bank_layout<bank3_test_tag> {
    static constexpr section_t section() {
        return { (static_cast<u32>(3) << 16) | 0xc000, 12 };
    }
};

VRC1_BANKED(".prg_rom_bank3", bank3_test, void, BankedCallBank3Test);

volatile u8 bankedCallBank3TestMarker = 0;

[[gnu::noinline]] void BankedCallBank3Test() {
    bankedCallBank3TestMarker = 3;
    VRC1::Call<BankedCallWindowTest>();
}

struct oversized_tag {};

extern "C" const u8 __oversized_domain_size[];

template <> struct VRC1::bank_layout<oversized_tag> {
    static section_t section() {
        return { (static_cast<u32>(4) << 16) | 0xa000, reinterpret_cast<u32>(__oversized_domain_size) };
    }
};

VRC1_BANKED(".prg_rom_oversized", oversized, void, OversizedFuncLow);
VRC1_BANKED(".prg_rom_oversized", oversized, void, OversizedFuncHigh);

volatile u8 oversizedFuncLowMarker = 0;
volatile u8 oversizedFuncHighMarker = 0;

// Deliberately larger than one window, to exercise CallInWindows2: the .fill
// below is what makes the domain oversized.
[[gnu::noinline]] void OversizedFuncLow() {
    oversizedFuncLowMarker = 1;
    __asm__ volatile(".fill 8300, 1, 0xea");
    OversizedFuncHigh();
}

[[gnu::noinline]] void OversizedFuncHigh() {
    oversizedFuncHighMarker = 1;
    VRC1::Call<BankedCallWindowTest>();
}

// Priority 101: re-syncs the Register shadows _reset poked before .bss was
// zeroed, and must run before anything that reads them.
__attribute__((constructor(101)))
static void SyncBankShadows() {
    VRC1::SwitchBank(VRC1::window1Control, 0);
    VRC1::SwitchBank(VRC1::window2Control, 1);
    VRC1::SwitchBank(VRC1::window3Control, 2);
    VRC1::Call<BankedCallWindowTest>();
    VRC1::Call<BankedCallBank3Test>();
    VRC1::Call<OversizedFuncLow>();
}

tech::wo_register<0x9000> VRC1::chrHighBits;
tech::wo_register<0xe000> VRC1::chr0Control;
tech::wo_register<0xf000> VRC1::chr1Control;
