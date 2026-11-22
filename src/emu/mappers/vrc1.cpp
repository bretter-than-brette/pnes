/**
 * @file vrc1.cpp
 * @brief Emu-side VRC1 register storage and tile-address translation.
 *        Counterpart to src/nes/mappers/vrc1.cpp, which owns the real port
 *        writes and is only built for the NES target.
 *
 * The registers are declared unconditionally in vrc1.hpp, so they need a
 * definition on every target; SwitchBank and SwitchCHRBank already branch their
 * hardware-poke path on TARGET_NES internally.
 */
#include <pnes/mappers/vrc1.hpp>
#include <pnes/video.hpp>

tech::wo_register<0x8000> VRC1::window1Control;
tech::wo_register<0xa000> VRC1::window2Control;
tech::wo_register<0xc000> VRC1::window3Control;

tech::wo_register<0x9000> VRC1::chrHighBits;
tech::wo_register<0xe000> VRC1::chr0Control;
tech::wo_register<0xf000> VRC1::chr1Control;

u8 VRC1::chrBanks[2] = {};

u32 VRC1::GetTileLMA(const u16 tileVMA) {
    constexpr u32 kWindow = 0x1000;
    const u16 vma = tileVMA & 0x1FFF;
    const u32 lma = vma < 0x1000
        ? static_cast<u32>(chrBanks[0]) * kWindow + vma
        : static_cast<u32>(chrBanks[1]) * kWindow + (vma - 0x1000);
    return lma % ppu::chrRomBytes;
}

u32 ppu::ResolveTile(const u16 tileVMA) {
    return VRC1::GetTileLMA(tileVMA);
}
