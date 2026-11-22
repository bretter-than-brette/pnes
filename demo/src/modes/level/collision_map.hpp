#pragma once

#include "cursor.hpp"
#include "dynamic.hpp"                 // DynamicCursor (dynamic-plane producer edge)
#include "../../graphics/metatiles.hpp"   // MetatileCollision (query decodes per cell)
#include <pnes/video.hpp>      // video::viewport_tx (window width tracks the viewport)
#include <pnes/types.hpp>      // vec2
#include <intsh>
using namespace br0::intsh;

namespace level {

// Shared composite-metatile window, one byte per cell.
//
// Both planes -- static walls/ground and dynamic coins -- are RLE-compressed, so
// decoding either per query is the cost this avoids: a column is decoded ONCE on
// entry into a flat RAM window holding the composite metatile id (dynamic over
// static). Collision is then a flat index plus a one-mask class decode, and only
// the single column entering as the camera scrolls re-decodes.
//
// The audience split falls out of that class: Solid blocks everyone, Collect
// blocks the player only (enemies test == Solid, so they pass through coins),
// None is air.
//
// The window is the viewport plus 4 columns either side, centred on the camera,
// and it is a ring -- a world column maps base-relative to the leftmost column
// held, so the non-power-of-2 width needs a subtract and one conditional wrap
// rather than a modulo.

#ifdef TARGET_NES
constexpr u8 kColMapWidth = 24;                 // array bound AND ring modulus (folds)
constexpr u8 ColMapWidth() { return kColMapWidth; }
#else
constexpr u8 kColMapWidth = 128;                // array bound only (super-ultrawide cap)
inline u16 ColMapWidth() { return static_cast<u16>((video::viewport_tx() >> 1) + 8); }
#endif

extern u8 ViewMap[];
extern u16 ColMapBaseCol;
extern u8  ColMapOrigin;

const u8* ColMapColumn(u16 col);

void ColMapStamp(u8 slot, Cursor stat, DynamicCursor dyn);

void ColMapSeed(u16 leftCol, Cursor stat, DynamicCursor dyn);

void ColMapTrack(u16 camLeftCol);

void ColMapSlideRight(const Cursor &newRightStat, DynamicCursor newRightDyn);
void ColMapSlideLeft(const Cursor &newLeftStat, DynamicCursor newLeftDyn);

bool Blocked(u16 px, u16 py, vec2<u8> dimensions, bool collectBlocks);

struct CoinPick { u16 col; u8 row; u8 reveal; };

extern CoinPick coinPick;

bool CollectCoins(u16 px, u16 py, vec2<u8> dimensions);
bool CollectCoins2(u16 px, u16 py, vec2<u8> dimensions);

struct CoinGate { u16 px; u16 py; bool dry; };
extern CoinGate coinGate1;
extern CoinGate coinGate2;

AI inline bool CoinGateSkips(const CoinGate& g, const u16 px, const u16 py) {
    if (!g.dry) return false;
    return ((px ^ g.px) & 0xFFF0u) == 0
        && ((py ^ g.py) & 0xFFF0u) == 0
        && ((px & 0xFu) == 0) == ((g.px & 0xFu) == 0)
        && ((py & 0xFu) == 0) == ((g.py & 0xFu) == 0);
}

}   // namespace level
