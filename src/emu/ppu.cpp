/**
 * @file ppu.cpp
 * @brief Portable software NES PPU shared by every emulated-PPU backend: the
 *        per-pixel emulator, the nametable/attribute/palette/OAM writes, the
 *        scroll model and the sprite-zero machinery.
 *
 * Depends only on the shared PPU state and the interrupt queue, never on SDL, GX
 * or any display API; each backend links this and supplies presentation.
 */
#include "emu.hpp"

#include <pnes/video.hpp>
#include <pnes/interrupts.hpp>

#include <bit>
#include <cstdlib>
#include <cstring>

#include "pnes/technology.hpp"

u8 *paletteRAM;
u8 *VideoRAM;
int quit;
bool mirroring;

namespace ppu {
u8 PPUCTRL;
u8 PPUMASK;
u32 chrGeneration;
}   // namespace ppu

const u8 *patternTable = CHR_ROM;

static video::spriteZeroHandler_t sprite0_zero;

void video::SetSpriteZeroHandler(const vec2<u16> pos, void (*fn)()) {
    sprite0_zero = (video::spriteZeroHandler_t){ .method = fn, .pos = pos };
}

__attribute__((weak)) u32 ppu::ResolveTile(const u16 tileVMA) {
    return tileVMA;
}

__attribute__((weak)) extern const u8 ppu::nametableCount = 2;

__attribute__((weak)) u8 ppu::ReadNametable(const u16 logical) {
    return VideoRAM[logical];
}

__attribute__((weak)) void ppu::WriteNametable(const u16 logical, const u8 value) {
    VideoRAM[logical] = value;
}

static constexpr u32 nes_rgb[64] = {
    0xFF626262, 0xFF012090, 0xFF1B0CA4, 0xFF3B009E,
    0xFF520080, 0xFF5A004E, 0xFF521610, 0xFF3F2E00,
    0xFF234400, 0xFF0A5200, 0xFF005804, 0xFF004E30,
    0xFF003C62, 0xFF000000, 0xFF000000, 0xFF000000,
    0xFFABABAB, 0xFF1F56D8, 0xFF423CF2, 0xFF6E24EC,
    0xFF9218C4, 0xFF9E1A80, 0xFF933434, 0xFF7A5200,
    0xFF576E00, 0xFF2E8400, 0xFF118E0E, 0xFF008848,
    0xFF007898, 0xFF000000, 0xFF000000, 0xFF000000,
    0xFFFBFBFB, 0xFF6BA4FF, 0xFF8C88FF, 0xFFB87AFF,
    0xFFE072FF, 0xFFF076D0, 0xFFE88C78, 0xFFCCA830,
    0xFFA8C410, 0xFF7EDC24, 0xFF5AE84E, 0xFF48E490,
    0xFF48D4E0, 0xFF4E4E4E, 0xFF000000, 0xFF000000,
    0xFFFBFBFB, 0xFFBED4FF, 0xFFCACAFF, 0xFFDCC4FF,
    0xFFECC0FF, 0xFFF2C0EA, 0xFFF2C8C4, 0xFFE8D4A4,
    0xFFD8E09C, 0xFFC8EC9C, 0xFFBCF0AC, 0xFFB4F0CC,
    0xFFB4E8F0, 0xFFB8B8B8, 0xFF000000, 0xFF000000,
};

#ifdef _WIN32
// Anchor for the merged CHR section on MSVC/COFF (every other target gets
// __start_chr_rom from the linker). Inline asm labels bypass extern "C" name
// decoration, so this has to spell whatever the active ABI mangles to: i686
// cdecl prepends a second underscore, x86_64 does not.
#if defined(_M_IX86) || defined(__i386__)
  #define PNES_CHR_ROM_ANCHOR "__chr_rom"
#else
  #define PNES_CHR_ROM_ANCHOR "_chr_rom"
#endif
__asm__(
    ".pushsection chr_rom$a,\"dr\"\n"
    ".global " PNES_CHR_ROM_ANCHOR "\n"
    PNES_CHR_ROM_ANCHOR ":\n"
    ".popsection\n"
);
#endif

static struct oam::sprite_t oamShadow[OAM_SPRITES];

// Shared PPU state, platform-independent plain RAM shadows, referenced by every
// backend.
namespace emu {
u16 scrollX;
u16 scrollY;
bool scrollWritten;
}   // namespace emu

#pragma region PPU_EMU

/* Per-pixel NES PPU emulator.
 *
 * Scroll model: a mid-frame write of EITHER axis lands immediately, because that
 * is what ppu::SetScroll does on hardware. Real hardware re-latches v's
 * horizontal bits from t once per scanline but its vertical bits only once per
 * frame -- that asymmetry belongs to the $2005 write PAIR, not to the hardware,
 * and the four-write sequence SetScroll uses copies t into v on the spot.
 *
 * scrollX is read fresh every segment, so an X write is live on the next pixel.
 * scrollY seeds ppu_y and then free-runs, but an IRQ handler that writes a scroll
 * re-anchors ppu_y at the scanline it fired on.
 *
 * IRQ dispatch: each scanline is split at the px of the next queued IRQ, and the
 * handler fires before that pixel renders, so the next pixel sees whatever it
 * changed.
 */
namespace emu {

void GenerateFrame(u32 *fb, const int stride) {
    const int vpw = video::viewport_px();
    const int vph = video::viewport_py();

    const emu::NtGeometry geo = emu::ComputeNtGeometry();
    const int world_w  = geo.worldW;
    const int world_h  = geo.worldH;
    const int spr_base = (ppu::PPUCTRL & ppu::ctrl::SPRITE_ADDR) ? 0x1000 : 0x0000;
    const int spr_h    = (ppu::PPUCTRL & ppu::ctrl::SPRITE_SIZE) ? 16 : 8;

    int ppu_y = (int)scrollY;

    for (int py = 0; py < vph; py++) {
        int line_spr[64];
        int n_line = 0;
        if (ppu::PPUMASK & ppu::mask::SPRITE) {
            for (size_t s = 0; s < OAM_SPRITES && n_line < 64; s++) {
                if (const int sy = static_cast<int>(oamShadow[s].y) + 1; py >= sy && py < sy + spr_h)
                    line_spr[n_line++] = static_cast<int>(s);
            }
        }

        int seg_start = 0;
        while (seg_start < vpw) {
            int seg_end = vpw;
            int fire    = 0;
            if (irq::irqPendingValid) {
                const vec2<u16>& pos = irq::irqPosition;
                if (static_cast<int>(pos.y) < py
                    || (static_cast<int>(pos.y) == py && static_cast<int>(pos.x) < seg_start)) {
                    irq::irqPendingValid = false;
                } else if (static_cast<int>(pos.y) == py) {
                    seg_end = static_cast<int>(pos.x);
                    fire    = 1;
                }
            }

            const int wy        = ppu_y % world_h;
            const int tile_row  = wy / 8;
            const int local_row = tile_row % geo.tileH;
            const int nt_row    = tile_row / geo.tileH;
            const int fine_y    = wy & 7;

            const bool bg_on    = ppu::PPUMASK & ppu::mask::BG;
            const bool bg_left  = ppu::PPUMASK & 0x02;
            const int  row32    = local_row * geo.tileW;
            const int  at_roff  = (local_row >> 2) * geo.atW;    // (local_row / 4) * atW
            const int  at_rbits = ((local_row >> 1) & 1) * 4;
            const int  ntrow_b  = nt_row * geo.gridW;
            const int  chr_tbl  = (ppu::PPUCTRL & ppu::ctrl::BG_ADDR) ? 0x1000 : 0;

            const int  wx0      = (static_cast<int>(scrollX) + seg_start) % world_w;
            const int  tcol0    = wx0 >> 3;
            int        fine_x   = wx0 & 7;
            int        local_col = tcol0 % geo.tileW;
            int        nt_col    = tcol0 / geo.tileW;

            u8 plane0 = 0, plane1 = 0, tile_pal = 0;
            auto load_tile = [&]() {
                const int nt_off = (nt_col + ntrow_b) * geo.pageBytes;
                const u8 tile_id = ppu::ReadNametable(static_cast<u16>(nt_off + row32 + local_col));
                const u8 attr    = ppu::ReadNametable(static_cast<u16>(nt_off + geo.ntBytes + at_roff + (local_col >> 2)));
                tile_pal = (attr >> (((local_col >> 1) & 1) * 2 + at_rbits)) & 3;
                const int chr_base = chr_tbl + tile_id * 16 + fine_y;
                const u32 chr_lma  = ppu::ResolveTile(static_cast<u16>(chr_base));
                plane0 = patternTable[chr_lma];
                plane1 = patternTable[chr_lma + 8];
            };
            if (bg_on) load_tile();

            for (int px = seg_start; px < seg_end; px++) {

                int bg_cidx = 0;
                u8  bg_pal  = 0;
                if (bg_on && (bg_left || px >= 8)) {
                    const int bit = 7 - fine_x;
                    bg_cidx = ((plane0 >> bit) & 1) | (((plane1 >> bit) & 1) << 1);
                    bg_pal  = tile_pal;
                }
                const int bg_opaque = bg_cidx != 0;

                int     spr_hit    = 0;
                int     spr_behind = 0;
                u8 spr_nes    = 0;
                if ((ppu::PPUMASK & ppu::mask::SPRITE) && ((ppu::PPUMASK & 0x04) || px >= 8)) {
                    for (int k = 0; k < n_line; k++) {
                        const auto [y, tile, attributes, x] = oamShadow[line_spr[k]];
                        const int sx  = (int)x;
                        if (px < sx || px >= sx + 8) continue;
                        const int sy      = static_cast<int>(y + 1);
                        const u8 att = attributes;
                        const int row     = (att & 0x80) ? (spr_h - 1 - (py - sy)) : (py - sy);
                        const int col_bit = (att & 0x40) ? (px - sx) : (7 - (px - sx));
                        const int addr    = (spr_h == 16)
                            ? ((tile & 1) ? 0x1000 : 0x0000) + (tile & 0xFE) * 16 + (row >> 3) * 16 + (row & 7)
                            : spr_base + tile * 16 + row;
                        const u32 spr_lma = ppu::ResolveTile(static_cast<u16>(addr));
                        const int cidx    = ((patternTable[spr_lma]      >> col_bit) & 1)
                                          | (((patternTable[spr_lma + 8]  >> col_bit) & 1) << 1);
                        if (cidx == 0) continue;
                        spr_nes    = paletteRAM[0x10 + (att & 0x03) * 4 + cidx];
                        spr_behind = att & 0x20;
                        spr_hit    = 1;
                        break;
                    }
                }

                u8 final_nes;
                if      (spr_hit && (!spr_behind || !bg_opaque)) final_nes = spr_nes;
                else if (bg_opaque)                               final_nes = paletteRAM[bg_pal * 4 + bg_cidx];
                else                                              final_nes = paletteRAM[0];

                if (ppu::PPUMASK & 0x01) final_nes &= 0x30;

                u32 col = nes_rgb[final_nes & 0x3F];

                if (ppu::PPUMASK & 0xE0) {
                    u32 r = (col >> 16) & 0xFF;
                    u32 g = (col >>  8) & 0xFF;
                    u32 b =  col        & 0xFF;
                    if (ppu::PPUMASK & 0x20) { g = g * 3 / 4; b = b * 3 / 4; }
                    if (ppu::PPUMASK & 0x40) { r = r * 3 / 4; b = b * 3 / 4; }
                    if (ppu::PPUMASK & 0x80) { r = r * 3 / 4; g = g * 3 / 4; }
                    col = 0xFF000000u | (r << 16) | (g << 8) | b;
                }

                fb[py * stride + px] = col;

                if (++fine_x == 8) {
                    fine_x = 0;
                    if (++local_col == geo.tileW) {
                        local_col = 0;
                        if (++nt_col == geo.gridW) nt_col = 0;
                    }
                    if (bg_on) load_tile();
                }
            }

            if (fire) {
                irq::irqPendingValid = false;
                scrollWritten = false;
                if (irq::irqHandler) irq::irqHandler();
                if (scrollWritten) ppu_y = static_cast<int>(scrollY);
            }

            seg_start = seg_end;
        }

        ppu_y++;
    }
}

void InitMemory(const unsigned vram_bytes) {
    paletteRAM = static_cast<u8 *>(malloc(32));
    VideoRAM   = static_cast<u8 *>(malloc(vram_bytes));
}

void GenerateBands(const band_emit_fn emit) {
    const int vph = video::viewport_py();

    int  band_start = 0;
    u16  cur_xs = scrollX;
    int ys_base = scrollY;

    for (int py = 0; py < vph; py++) {
        if (irq::irqPendingValid) {
            const vec2<u16>& pos = irq::irqPosition;
            if (static_cast<int>(pos.y) < py) {
                irq::irqPendingValid = false;   // stale — past without firing
            } else if (static_cast<int>(pos.y) == py) {
                if (py > band_start) {
                    emit(band_start, py, cur_xs, static_cast<u16>(ys_base + band_start));
                    band_start = py;
                }
                irq::irqPendingValid = false;
                scrollWritten = false;
                if (irq::irqHandler) irq::irqHandler();
                if (scrollWritten)
                    ys_base = static_cast<int>(scrollY) - py;
                cur_xs = scrollX;
            }
        }
    }

    if (band_start < vph) emit(band_start, vph, cur_xs, static_cast<u16>(ys_base + band_start));
}

const oam::sprite_t* OamShadow() { return oamShadow; }

}   // namespace emu

#pragma endregion

// The one write side of the page layout: everything that reads it (each
// backend's band draw) goes through emu::ComputeNtGeometry, so the two cannot
// disagree.
inline static u16 xy_to_nt_addr(u16 x, u16 y) {
    const emu::NtGeometry geo = emu::ComputeNtGeometry();
    const u16 nt_h = static_cast<u16>((x / geo.tileW) % geo.gridW);
    const u16 nt_v = static_cast<u16>((y / geo.tileH) % geo.gridH);
    const u16 col  = static_cast<u16>(x % geo.tileW);
    const u16 row  = static_cast<u16>(y % geo.tileH);

    return static_cast<u16>((nt_h + nt_v * geo.gridW) * geo.pageBytes + row * geo.tileW + col);
}

inline static u16 xy_to_at_addr(u16 x, u16 y) {
    const emu::NtGeometry geo = emu::ComputeNtGeometry();
    const u16 nt_h = static_cast<u16>((x / geo.tileW) % geo.gridW);
    const u16 nt_v = static_cast<u16>((y / geo.tileH) % geo.gridH);
    const u16 col  = static_cast<u16>(x % geo.tileW);
    const u16 row  = static_cast<u16>(y % geo.tileH);

    return static_cast<u16>((nt_h + nt_v * geo.gridW) * geo.pageBytes
         + geo.ntBytes + (row / 4) * geo.atW + (col / 4));
}

inline static void nt_addr_to_xy(const u16 address, u16& x, u16& y) {
    const emu::NtGeometry geo = emu::ComputeNtGeometry();
    const u16 pageIndex = static_cast<u16>(address / geo.pageBytes);
    const u16 nt_h       = static_cast<u16>(pageIndex % geo.gridW);
    const u16 nt_v       = static_cast<u16>(pageIndex / geo.gridW);
    const u16 local      = static_cast<u16>(address % geo.pageBytes);
    x = static_cast<u16>(nt_h * geo.tileW + local % geo.tileW);
    y = static_cast<u16>(nt_v * geo.tileH + local / geo.tileW);
}

inline static void at_addr_to_xy(const u16 address, u16& x, u16& y) {
    const emu::NtGeometry geo = emu::ComputeNtGeometry();
    const u16 pageIndex = static_cast<u16>(address / geo.pageBytes);
    const u16 nt_h       = static_cast<u16>(pageIndex % geo.gridW);
    const u16 nt_v       = static_cast<u16>(pageIndex / geo.gridW);
    const u16 local      = static_cast<u16>(address % geo.pageBytes - geo.ntBytes);
    x = static_cast<u16>(nt_h * geo.tileW + (local % geo.atW) * 4);
    y = static_cast<u16>(nt_v * geo.tileH + (local / geo.atW) * 4);
}

namespace ppu {

void EnableRendering(u8 ppuCtrl_, u8 ppuMask_) {
    ppu::PPUMASK = ppuMask_;
    ppu::PPUCTRL = ppuCtrl_;
}

// Weak, so a board that routes nametable reads elsewhere can override it.
__attribute__((weak))
void Flush(const u8 nt, const u8 at) {
    const emu::NtGeometry geo = emu::ComputeNtGeometry();
    const int pageCount = geo.gridW * geo.gridH;
    for (int page = 0; page < pageCount; page++) {
        const int base = page * geo.pageBytes;
        for (int i = 0; i < geo.ntBytes; i++) {
            ppu::WriteNametable(static_cast<u16>(base + i), nt);
        }
        for (int i = 0; i < geo.atBytes; i++) {
            ppu::WriteNametable(static_cast<u16>(base + geo.ntBytes + i), at);
        }
    }
}

void WriteFromBufferToNameTable(
    const vec2<u16> pos, const u8* source, const u8 sBuffer, u8 polarity
) {
    const u16 x = pos.x, y = pos.y;
    ppu::PPUCTRL &= ~ppu::ctrl::POLARITY;
    if (polarity) ppu::PPUCTRL |= ppu::ctrl::POLARITY;
    const bool vertical = ppu::PPUCTRL & ppu::ctrl::POLARITY;
    for (u8 i = 0; i < sBuffer; i++) {
        const u16 addr = vertical ? xy_to_nt_addr(x, static_cast<u16>(y + i))
                                   : xy_to_nt_addr(static_cast<u16>(x + i), y);
        ppu::WriteNametable(addr, source[i]);
    }
}

void WriteFromBufferToNameTable(
    const u16 address, const u8* source, const u8 sBuffer, const u8 polarity
) {
    u16 x, y;
    nt_addr_to_xy(address, x, y);
    WriteFromBufferToNameTable({x, y}, source, sBuffer, polarity);
}

void WriteRepeatedToNameTable(
    const vec2<u16> pos, const u8 value, const u8 amt, u8 polarity
) {
    const u16 x = pos.x, y = pos.y;
    ppu::PPUCTRL &= ~ppu::ctrl::POLARITY;
    if (polarity) ppu::PPUCTRL |= ppu::ctrl::POLARITY;
    const bool vertical = ppu::PPUCTRL & ppu::ctrl::POLARITY;

    for (u8 i = 0; i < amt; i++) {
        const u16 addr = vertical ? xy_to_nt_addr(x, static_cast<u16>(y + i))
                                   : xy_to_nt_addr(static_cast<u16>(x + i), y);
        ppu::WriteNametable(addr, value);
    }
}

void WriteRepeatedToNameTable(
    const u16 address, const u8 value, const u8 amt, const u8 polarity
) {
    u16 x, y;
    nt_addr_to_xy(address, x, y);
    WriteRepeatedToNameTable({x, y}, value, amt, polarity);
}

void WriteSingleToNameTable(const vec2<u16> pos, u8 value) {
    const u16 offset = xy_to_nt_addr(pos.x, pos.y);
    ppu::WriteNametable(offset, value);
}

void WriteSingleToNameTable(const u16 address, u8 value) {
    ppu::WriteNametable(address, value);
}

void SetScroll(const vec2<u16> pos) {
    emu::scrollX = pos.x; emu::scrollY = pos.y;
    emu::scrollWritten = true;
}

void StageScroll(const scroll_t) {}

void LatchScroll(const scroll_t s) {
    SetScroll(s);
}

template <typename Idx>
void WriteFromProviderToNameTable(
    const vec2<u16> pos, u8 (*fn)(Idx), const u8 amt,
    const u8 polarity
) {
    const u16 x = pos.x, y = pos.y;
    ppu::PPUCTRL &= ~ppu::ctrl::POLARITY;
    if (polarity) ppu::PPUCTRL |= ppu::ctrl::POLARITY;
    const bool vertical = ppu::PPUCTRL & ppu::ctrl::POLARITY;

    for (Idx i = 0; i < amt; ++i) {
        const u16 addr = vertical ? xy_to_nt_addr(x, static_cast<u16>(y + i))
                                   : xy_to_nt_addr(static_cast<u16>(x + i), y);
        ppu::WriteNametable(addr, fn(i));
    }
}

template void WriteFromProviderToNameTable<u8>(vec2<u16>, u8 (*)(u8), u8, u8);
template void WriteFromProviderToNameTable<u16>(vec2<u16>, u8 (*)(u16), u8, u8);

template <typename Idx>
void WriteFromProviderToNameTable(
    const u16 address, u8 (*fn)(Idx), const u8 amt, const u8 polarity
) {
    u16 x, y;
    nt_addr_to_xy(address, x, y);
    WriteFromProviderToNameTable({x, y}, fn, amt, polarity);
}

template void WriteFromProviderToNameTable<u8>(u16, u8 (*)(u8), u8, u8);
template void WriteFromProviderToNameTable<u16>(u16, u8 (*)(u16), u8, u8);

void WriteFromBufferToAttributeTable(
    const vec2<u16> pos, const u8* source,
    const u8 sBuffer, const u8 polarity
) {
    const u16 x = pos.x, y = pos.y;
    if (polarity) {
        const u16 offset = xy_to_at_addr(x, y);
        const u16 atStride = emu::ComputeNtGeometry().atW;
        for (u8 i = 0; i < sBuffer; i++) {
            ppu::WriteNametable(static_cast<u16>(offset + i * atStride), source[i]);
        }
    } else {
        for (u8 i = 0; i < sBuffer; i++) {
            ppu::WriteNametable(xy_to_at_addr(static_cast<u16>(x + i * 4), y), source[i]);
        }
    }
}

void WriteFromBufferToAttributeTable(
    const u16 address, const u8* source, const u8 sBuffer, const u8 polarity
) {
    u16 x, y;
    at_addr_to_xy(address, x, y);
    WriteFromBufferToAttributeTable({x, y}, source, sBuffer, polarity);
}

void WriteRepeatedToAttributeTable(
    const vec2<u16> pos, const u8 value, const u8 amt, const u8 polarity
) {
    const u16 x = pos.x, y = pos.y;
    if (polarity) {
        const u16 offset = xy_to_at_addr(x, y);
        const u16 atStride = emu::ComputeNtGeometry().atW;
        for (u8 i = 0; i < amt; i++) {
            ppu::WriteNametable(static_cast<u16>(offset + i * atStride), value);
        }
        return;
    }
    for (u8 i = 0; i < amt; i++) {
        ppu::WriteNametable(xy_to_at_addr(static_cast<u16>(x + i * 4), y), value);
    }
}

void WriteRepeatedToAttributeTable(
    const u16 address, const u8 value, const u8 amt, const u8 polarity
) {
    u16 x, y;
    at_addr_to_xy(address, x, y);
    WriteRepeatedToAttributeTable({x, y}, value, amt, polarity);
}

void WriteSingleToAttributeTable(const vec2<u16> pos, const u8 value) {
    const u16 offset = xy_to_at_addr(pos.x, pos.y);
    ppu::WriteNametable(offset, value);
}

void WriteSingleToAttributeTable(const u16 address, const u8 value) {
    ppu::WriteNametable(address, value);
}

template <typename Idx>
void WriteFromProviderToAttributeTable(
    const vec2<u16> pos, u8 (*fn)(Idx), const u8 amt,
    const u8 polarity
) {
    const u16 x = pos.x, y = pos.y;
    if (polarity) {
        const u16 offset = xy_to_at_addr(x, y);
        const u16 atStride = emu::ComputeNtGeometry().atW;
        for (Idx i = 0; i < amt; ++i) {
            ppu::WriteNametable(static_cast<u16>(offset + i * atStride), fn(i));
        }
        return;
    }
    for (Idx i = 0; i < amt; ++i) {
        const u16 addr = xy_to_at_addr(static_cast<u16>(x + i * 4), y);
        ppu::WriteNametable(addr, fn(i));
    }
}

template void WriteFromProviderToAttributeTable<u8>(vec2<u16>, u8 (*)(u8), u8, u8);
template void WriteFromProviderToAttributeTable<u16>(vec2<u16>, u8 (*)(u16), u8, u8);

template <typename Idx>
void WriteFromProviderToAttributeTable(
    const u16 address, u8 (*fn)(Idx), const u8 amt, const u8 polarity
) {
    u16 x, y;
    at_addr_to_xy(address, x, y);
    WriteFromProviderToAttributeTable({x, y}, fn, amt, polarity);
}

template void WriteFromProviderToAttributeTable<u8>(u16, u8 (*)(u8), u8, u8);
template void WriteFromProviderToAttributeTable<u16>(u16, u8 (*)(u16), u8, u8);

u16 CartesianToAddress(const vec2<u16>& pos) {
    return xy_to_nt_addr(pos.x, pos.y);
}

scroll_t CartesianToScroll(const vec2<u16> pos) {
    return (scroll_t){ .x = pos.x, .y = pos.y };
}

void SetColorPriority(const u8 priority) {
    ppu::PPUMASK = (ppu::PPUMASK & ~(ppu::mask::RED | ppu::mask::GREEN | ppu::mask::BLUE)) |
        (priority & (ppu::mask::RED | ppu::mask::GREEN | ppu::mask::BLUE)
    );
}

namespace pal {

void WriteFromBuffer(const u8 offset, const u8* source, const u8 sBuffer) {
    memcpy(paletteRAM + offset, source, sBuffer);
}

void WriteSingle(const u8 offset, const u8 value) {
    paletteRAM[offset] = value;
}

}   // namespace pal

}   // namespace ppu

namespace oam {

void OAMFromBuffer(sprite_t* oam, const u8 slot, const u16 off,
                   const u8 width, const u8* src, const u16 count) {
    u8* dst = reinterpret_cast<u8 *>(oam) + static_cast<size_t>(slot) * spriteStride + off;
    const u8* s = src + off;
    for (u16 i = 0; i < count; i++)
        memcpy(dst + static_cast<size_t>(i) * spriteStride, s + static_cast<size_t>(i) * spriteStride, width);
}

void OAMFromProvider(sprite_t* oam, const u8 slot, const u16 off,
                     const u8 width, oam_t (*fn)(u16), const u16 count) {
    u8* base = reinterpret_cast<u8 *>(oam) + static_cast<size_t>(slot) * spriteStride + off;
    const size_t skip = (width < sizeof(oam_t) && std::endian::native == std::endian::big)
                        ? sizeof(oam_t) - width : 0;
    for (u16 i = 0; i < count; i++) {
        oam_t v = fn(i);
        memcpy(base + static_cast<size_t>(i) * spriteStride,
               reinterpret_cast<const u8 *>(&v) + skip, width);
    }
}

void RefreshSprites(const sprite_t* oam) {
    memcpy(oamShadow, oam, OAM_SPRITES * sizeof(struct sprite_t));
}

}   // namespace oam

namespace ppu {

void StreamFromVideoMemory(const u16 offset, atomic u8* target, const u8 size) {
    for (u8 i = 0; i < size; i++) {
        target[i] = ppu::ReadNametable(static_cast<u16>(offset + i));
    }
}

}   // namespace ppu

void video::WaitThenReactToSpriteZero(const vec2<u16> pos, void (*fn)(), atomic u8* latch) {
    *latch = true;
    video::SetSpriteZeroHandler(pos, fn);
    irq::irqHandler      = fn;
    irq::irqPosition     = pos;
    irq::irqPendingValid = true;
}
