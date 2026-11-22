/**
 * @file emu.hpp
 * @brief Shared state and entry points of the portable software PPU: the frame
 *        and band renderers, the nametable geometry, and the scroll model.
 */
#ifndef EMU_H
#define EMU_H

#include <intsh>
#include <pnes/video.hpp>
using namespace br0::intsh;

namespace oam { struct sprite_t; }

namespace emu {

/// Nametable/attribute geometry for the linked board and current viewport. Every
/// backend that walks nametable VRAM addresses it through this, which is what
/// keeps each read side byte-for-byte consistent with the one write side.
struct NtGeometry {
    int gridW, gridH;    ///< Nametables wide/tall the addressable world grids as.
    int tileW, tileH;    ///< One nametable's size in tiles (NES-native 32x30).
    int atW, atH;        ///< One nametable's attribute-table size, in 4-tile cells.
    int ntBytes;          ///< Tile-plane bytes in one nametable (tileW * tileH).
    int atBytes;          ///< Attribute-plane bytes in one nametable (atW * atH).
    int pageBytes;        ///< Total bytes in one nametable (ntBytes + atBytes).
    int worldW, worldH;   ///< Total addressable pixels before the background wraps.
};

inline NtGeometry ComputeNtGeometry() {
    const int twMin = static_cast<int>(video::viewport_tx()) < 32 ? 32 : static_cast<int>(video::viewport_tx());
    const int th    = static_cast<int>(video::viewport_ty()) < 30 ? 30 : static_cast<int>(video::viewport_ty());

    // Width is rounded up to a whole number of attribute bytes (4 tiles): an
    // attribute byte spans a 4x4 block, so a width that is not a multiple of 4
    // puts the last byte half outside the page AND throws every later page's
    // attribute grid out of phase with its tile grid. Real NES pages are 32
    // wide, so nothing in the NES model ever had to think about it; the 3DS's
    // 50-tile viewport was the first width to break it. Up rather than down,
    // since down would make the plane narrower than the viewport.
    const int tw = (twMin + 3) & ~3;

    const int gridW = static_cast<int>(video::nametable_grid_w());
    const int gridH = static_cast<int>(video::nametable_grid_h());

    const int atW      = (tw + 3) >> 2;
    const int atH      = (th + 3) >> 2;
    const int ntBytes  = tw * th;
    const int atBytes  = atW * atH;

    return NtGeometry{
        gridW, gridH, tw, th, atW, atH, ntBytes, atBytes, ntBytes + atBytes,
        gridW * tw * 8, gridH * th * 8
    };
}

void GenerateFrame(u32* fb, int stride);

void InitMemory(unsigned vram_bytes);

using band_emit_fn = void (*)(int y0, int y1, u16 xscroll, u16 yscroll);

/// Drives the frame's raster IRQ timeline without per-pixel work, for the
/// backends that let their own hardware draw the tilemap: fires each queued IRQ
/// at its scanline (running game logic, which may move the scroll) and calls
/// @p emit for each band between them. Shares the scroll and sprite-zero
/// machinery with GenerateFrame, including the mid-frame Y re-anchor, so the two
/// cannot disagree about a split.
void GenerateBands(band_emit_fn emit);

/// The scroll the next segment renders with. A write lands immediately on both
/// axes, matching ppu::SetScroll's four-write sequence on hardware.
extern u16 scrollX;
extern u16 scrollY;

extern bool scrollWritten;

const oam::sprite_t* OamShadow();

}   // namespace emu

#endif // EMU_H
