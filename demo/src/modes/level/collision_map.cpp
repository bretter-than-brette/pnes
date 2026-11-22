#include "collision_map.hpp"

#include "levels.hpp"                  // levelHeight, kHudRows
#include "../../graphics/metatiles.hpp"   // GetMetatileCollisions, MetatileCollision
#include "../../banks.hpp"             // FIXED, LEVEL_CODE

#include <array>

using namespace br0::intsh;

namespace level {

// Composite-metatile window: ViewMap[slot*levelHeight + row] holds the metatile
// for that cell (dynamic over static), and the occupancy planes beside it are
// what let Blocked and CollectCoins answer in constant time.
#ifdef TARGET_NES
#define colmap_cold __attribute__((section(".bss")))
#else
#define colmap_cold
#endif

colmap_cold u8  ViewMap[kColMapWidth * levelHeight];
colmap_cold u16 ColMapBaseCol;
colmap_cold u8  ColMapOrigin;

static constexpr std::array<u16, kColMapWidth> kSlotOffset = []() constexpr {
    std::array<u16, kColMapWidth> a{};
    for (u8 i = 0; i < kColMapWidth; i++) a[i] = static_cast<u16>(i * levelHeight);
    return a;
}();

static constexpr std::array<u8, levelHeight> kBitLo = []() constexpr {
    std::array<u8, levelHeight> a{};
    for (u8 r = 0; r < levelHeight; r++) a[r] = r < 8 ? static_cast<u8>(1u << r) : 0;
    return a;
}();
static constexpr std::array<u8, levelHeight> kBitHi = []() constexpr {
    std::array<u8, levelHeight> a{};
    for (u8 r = 0; r < levelHeight; r++) a[r] = r >= 8 ? static_cast<u8>(1u << (r - 8)) : 0;
    return a;
}();

static constexpr std::array<u8, levelHeight + 1> kPrefLo = []() constexpr {
    std::array<u8, levelHeight + 1> a{};
    for (u8 k = 0; k <= levelHeight; k++)
        a[k] = k >= 8 ? 0xFF : static_cast<u8>((1u << k) - 1u);
    return a;
}();
static constexpr std::array<u8, levelHeight + 1> kPrefHi = []() constexpr {
    std::array<u8, levelHeight + 1> a{};
    for (u8 k = 0; k <= levelHeight; k++)
        a[k] = k <= 8 ? 0 : static_cast<u8>((1u << (k - 8)) - 1u);
    return a;
}();

namespace {
    colmap_cold Cursor        colLeftStat;
    colmap_cold Cursor        colRightStat;
    colmap_cold DynamicCursor colLeftDyn;
    colmap_cold DynamicCursor colRightDyn;


    colmap_cold u8 ColSolidLo[kColMapWidth];
    colmap_cold u8 ColSolidHi[kColMapWidth];
    colmap_cold u8 ColCollLo[kColMapWidth];
    colmap_cold u8 ColCollHi[kColMapWidth];

    colmap_cold const u8* ColStatBase[kColMapWidth];

    colmap_cold const u8* ColRunLen[kColMapWidth];    // -> DynLengths
    colmap_cold u8*       ColRunData[kColMapWidth];   // -> DynData
    colmap_cold u8        ColRunProg[kColMapWidth];

    colmap_cold u16 ColMapMaxBase;

    colmap_cold u16  colMemoL;
    colmap_cold u16  colMemoR;
    colmap_cold u8   colMemoA;   // slot for colMemoL; kNoSlot = outside window = air
    colmap_cold u8   colMemoB;   // slot for colMemoR; == colMemoA when single-column
    colmap_cold bool colMemoValid;

    constexpr u8 kNoSlot = 0xFF;
}

colmap_cold CoinPick coinPick;

colmap_cold CoinGate coinGate1;
colmap_cold CoinGate coinGate2;

enum : u8 { kScanOffField = 0, kScanEmpty = 1, kScanTook = 2 };

AI static void ColMapMasks(const u8 slot, const u8* col) {
    u8 sLo = 0, sHi = 0, cLo = 0, cHi = 0;
    #pragma clang loop unroll(disable)
    for (u8 r = 0; r < levelHeight; r++) {
        const MetatileCollision cls = GetMetatileCollisions(col[r]);
        if (cls == MetatileCollision::Solid) {
            sLo |= kBitLo[r]; sHi |= kBitHi[r];
        } else if (cls == MetatileCollision::Collect) {
            cLo |= kBitLo[r]; cHi |= kBitHi[r];
        }
    }
    ColSolidLo[slot] = sLo; ColSolidHi[slot] = sHi;
    ColCollLo[slot]  = cLo; ColCollHi[slot]  = cHi;
}

NI LEVEL_CODE void ColMapStamp(const u8 slot, Cursor stat, DynamicCursor dyn) {
    u8* col = ViewMap + kSlotOffset[slot];

    ColStatBase[slot] = stat.dp;
    ColRunLen[slot]   = dyn.lp;
    ColRunData[slot]  = dyn.dp;
    ColRunProg[slot]  = dyn.progress;

    const u8* sdat = stat.dp;
    const u8* ddat = dyn.dp;
    const u8* dlen = dyn.lp;
    u8        dp   = dyn.progress;

    // ~90 cycles per cell, ~36 of it register shuffling rather than memory: the
    // loop touches four pointers and the 6502 has one register that can index
    // (zp),y, so Y alternates on every cell.
    //
    // Two source-level fixes failed (2026-10-24) and should not be re-tried.
    // Walking `col` as a pointer gave a byte-identical function -- LLVM
    // strength-reduces it back -- and a pointer-compare loop was worse. The
    // shuffling is the register allocator, so moving it needs asm or a different
    // data layout.
    //
    // Note what is NOT the problem: `*dlen` and `*sdat` read banked PRG, but
    // window 2 is pinned to the level's bank for the whole session, and a read
    // from mapped ROM costs a 6502 exactly what RAM costs.
    #pragma clang loop unroll(disable)
    for (u8 r = 0; r < levelHeight; r++) {
        const u8 d = *ddat;
        col[r] = d ? d : *sdat;                 // dynamic-over-static
        ++sdat;                                  // flat: one byte per cell
        if (++dp >= *dlen) { ++dlen; ++ddat; dp = 0; }
    }

    ColMapMasks(slot, col);
}

NI LEVEL_CODE void ColMapSeed(const u16 leftCol, Cursor stat, DynamicCursor dyn) {
    ColMapBaseCol = leftCol;
    ColMapOrigin  = 0;
    ColMapMaxBase = nColumns > ColMapWidth()
                  ? static_cast<u16>(nColumns - ColMapWidth()) : 0;
    colLeftStat   = stat;          // leftmost held column (base), row 0
    colLeftDyn    = dyn;
    coinGate1 = { 0, 0, false };
    coinGate2 = coinGate1;
    colMemoValid = false;   // whole window just changed under the memo
    const auto width = ColMapWidth();
    for (u8 i = 0; i < width; i++) {
        if (i == width - 1) {          // rightmost held, row 0
            colRightStat = stat;
            colRightDyn  = dyn;
        }
        ColMapStamp(i, stat, dyn);     // stamps column i into slot i (copies consumed)
        stat.Move(levelHeight);        // advance masters to the next column's row 0
        dyn.Move(levelHeight);
    }
}

AI
static void AdvancePairForward(Cursor& s, DynamicCursor& d) {
    s.dp += levelHeight;
    const u8* dlp  = d.lp;  u8* ddp = d.dp;  u8 dprog = d.progress;
    #pragma clang loop unroll(disable)
    for (u8 i = levelHeight; i != 0; --i) {
        if (++dprog >= *dlp) { ++dlp; ++ddp; dprog = 0; }
    }
    d.lp = dlp; d.dp = ddp; d.progress = dprog;
}

AI
static void AdvancePairBackward(Cursor& s, DynamicCursor& d) {
    s.dp -= levelHeight;
    const u8* dlp  = d.lp;  u8* ddp = d.dp;  u8 dprog = d.progress;
    #pragma clang loop unroll(disable)
    for (u8 i = levelHeight; i != 0; --i) {
        if (dprog == 0) {
            if (dlp == DynLengths) break;
            --dlp; --ddp; dprog = *dlp;
        }
        --dprog;
    }
    d.lp = dlp; d.dp = ddp; d.progress = dprog;
}

NI LEVEL_CODE static void ColMapSlideTo(const u16 desired) {
    u8 colIter = 0;
    while (ColMapBaseCol < desired) {         // window slides right
        AdvancePairForward(colRightStat, colRightDyn);    // entering column
        ColMapSlideRight(colRightStat, colRightDyn);      // base++
        AdvancePairForward(colLeftStat, colLeftDyn);      // re-anchor left edge
    }
    while (ColMapBaseCol > desired) {          // window slides left
        AdvancePairBackward(colLeftStat, colLeftDyn);     // entering column
        ColMapSlideLeft(colLeftStat, colLeftDyn);         // base--
        AdvancePairBackward(colRightStat, colRightDyn);   // re-anchor right edge
    }

}

NI LEVEL_CODE void ColMapTrack(const u16 camLeftCol) {
    u16 desired = camLeftCol > 4 ? static_cast<u16>(camLeftCol - 4) : 0;
    if (desired > ColMapMaxBase) desired = ColMapMaxBase;   // hoisted, see its decl
    if (ColMapBaseCol != desired) ColMapSlideTo(desired);
}

LEVEL_CODE void ColMapSlideRight(const Cursor &newRightStat, DynamicCursor newRightDyn) {
    ColMapStamp(ColMapOrigin, newRightStat, newRightDyn);
    ColMapBaseCol++;
    if (++ColMapOrigin >= ColMapWidth()) ColMapOrigin = 0;
    colMemoValid = false;   // bases are relative to the window -- see the memo's decl
}

LEVEL_CODE void ColMapSlideLeft(const Cursor &newLeftStat, DynamicCursor newLeftDyn) {
    ColMapBaseCol--;
    if (ColMapOrigin == 0) ColMapOrigin = ColMapWidth() - 1;
    else                   ColMapOrigin--;
    ColMapStamp(ColMapOrigin, newLeftStat, newLeftDyn);
    colMemoValid = false;   // bases are relative to the window -- see the memo's decl
}

AI static u8 ColMapSlot(const u16 col) {
    const u16 d = col - ColMapBaseCol;
    if (d >= ColMapWidth()) return kNoSlot;       // outside window = air
    u8 slot = ColMapOrigin + static_cast<u8>(d);
    if (slot >= ColMapWidth()) slot -= ColMapWidth();
    return slot;
}

AI static void ColMemoFor(const u16 colL, const u16 colR) {
    if (colMemoValid && colL == colMemoL && colR == colMemoR) return;
    colMemoL     = colL;
    colMemoR     = colR;
    colMemoA     = ColMapSlot(colL);
    colMemoB     = colR == colL ? colMemoA : ColMapSlot(colR);
    colMemoValid = true;
}

AI static bool SolidHits(const u8 slot, const u8 mLo, const u8 mHi) {
    if (slot == kNoSlot) return false;
    return ((ColSolidLo[slot] & mLo) | (ColSolidHi[slot] & mHi)) != 0;
}
AI static bool CollectHits(const u8 slot, const u8 mLo, const u8 mHi) {
    if (slot == kNoSlot) return false;
    return ((ColCollLo[slot] & mLo) | (ColCollHi[slot] & mHi)) != 0;
}

FIXED const u8* ColMapColumn(const u16 col) {
    const u8 slot = ColMapSlot(col);
    if (slot == kNoSlot) return nullptr;          // outside window = air (caller skips)
    return ViewMap + kSlotOffset[slot];
}

// AABB block test on world-pixel inputs, with the HUD strip removed so the row
// index lands in level space.
//
// There is no per-cell loop: the swept column pair resolves through the memo, the
// row extent becomes one 14-bit mask out of the prefix tables, and the answer is
// an AND against each column's occupancy plane -- constant time in the AABB's
// height, with no attribute decode. `collectBlocks` picks the audience: true
// bonks on coins too.
//
// PRECONDITION: w <= 17, so the AABB spans at most TWO metatile columns. A wider
// actor needs a general colL..colR loop restored, not a longer memo -- this form
// would silently skip the middle columns.
//
// NI because LTO was duplicating the whole body into each of Player::Update's
// three call sites; FIXED because it must be mapped whenever level code runs.
NI FIXED bool Blocked(const u16 px, const u16 py, const vec2<u8> dimensions, const bool collectBlocks) {
    const u8 w = dimensions.x; const u8 h = dimensions.y;
    i16 rowTop = static_cast<i16>(py >> 4) - kHudRows;
    i16 rowBot = static_cast<i16>((py + h - 1) >> 4) - kHudRows;
    if (rowBot < 0 || rowTop >= levelHeight) return false;   // AABB entirely off-field
    if (rowTop < 0)            rowTop = 0;
    if (rowBot >= levelHeight) rowBot = levelHeight - 1;

    const u8 rowT = static_cast<u8>(rowTop);
    const u8 rowB = static_cast<u8>(rowBot);

    const u16 colL = px >> 4;
    const u16 colR = (px + w - 1) >> 4;
    ColMemoFor(colL, colR);

    const u8 mLo = static_cast<u8>(kPrefLo[rowB + 1] ^ kPrefLo[rowT]);
    const u8 mHi = static_cast<u8>(kPrefHi[rowB + 1] ^ kPrefHi[rowT]);

    if (SolidHits(colMemoA, mLo, mHi)) return true;
    if (collectBlocks && CollectHits(colMemoA, mLo, mHi)) return true;
    if (colMemoB != colMemoA) {
        if (SolidHits(colMemoB, mLo, mHi)) return true;
        if (collectBlocks && CollectHits(colMemoB, mLo, mHi)) return true;
    }
    return false;
}

NI FIXED static bool TakeFromColumn(const u8 slot, const u8 rowT, const u8 rowB, const u16 col) {
    u8* const colp = ViewMap + kSlotOffset[slot];
    DynamicCursor pd{ ColRunLen[slot], ColRunData[slot], ColRunProg[slot] };
    if (rowT != 0) pd.Move(static_cast<i8>(rowT));

    for (u8 r = rowT; ; r++) {
        if (GetMetatileCollisions(colp[r]) == MetatileCollision::Collect && pd.Fetch() != 0) {
            *pd.dp = 0;                              // permanent removal
            const u8 reveal = ColStatBase[slot][r];  // static plane is flat: direct index
            colp[r] = reveal;                        // window shows it gone immediately

            const u8 bLo = kBitLo[r], bHi = kBitHi[r];
            ColCollLo[slot] = static_cast<u8>(ColCollLo[slot] & ~bLo);
            ColCollHi[slot] = static_cast<u8>(ColCollHi[slot] & ~bHi);
            const MetatileCollision rc = GetMetatileCollisions(reveal);
            if (rc == MetatileCollision::Solid) {
                ColSolidLo[slot] |= bLo; ColSolidHi[slot] |= bHi;
            } else if (rc == MetatileCollision::Collect) {
                ColCollLo[slot] |= bLo; ColCollHi[slot] |= bHi;
            }

            coinPick = { col, r, reveal };
            return true;
        }
        if (r == rowB) return false;
        pd.Move(1);                                  // next row, same column
    }
}

// Collect one coin overlapping the AABB, through the same projection and memo.
// The common frame -- no coin in range -- is two ANDs and a return, with no cell
// visited: the occupancy planes already know. What stood here was a per-player
// cursor pair nudged by the player's travel every call, a 16-bit multiply to
// compute that nudge, and a per-cell pre-scan that re-derived the memo's work.
//
// At most one coin per call, which bounds both the VRAM-queue burst and the CPU
// burst; spreading a cluster over a few frames is not perceptible. Both players
// share it outright, there being no per-player state left.
NI FIXED static u8 CollectCoinsImpl(const u16 px, const u16 py, const vec2<u8> dimensions) {
    const u8 w = dimensions.x; const u8 h = dimensions.y;
    i16 sTop = static_cast<i16>(py >> 4) - kHudRows;
    i16 sBot = static_cast<i16>((py + h - 1) >> 4) - kHudRows;
    if (sBot < 0 || sTop >= levelHeight) return kScanOffField;
    if (sTop < 0)            sTop = 0;
    if (sBot >= levelHeight) sBot = levelHeight - 1;
    const u8 rowTop = static_cast<u8>(sTop);
    const u8 rowBot = static_cast<u8>(sBot);

    const u16 colL = px >> 4;
    const u16 colR = (px + w - 1) >> 4;
    ColMemoFor(colL, colR);

    const u8 mLo = static_cast<u8>(kPrefLo[rowBot + 1] ^ kPrefLo[rowTop]);
    const u8 mHi = static_cast<u8>(kPrefHi[rowBot + 1] ^ kPrefHi[rowTop]);

    if (CollectHits(colMemoA, mLo, mHi)
        && TakeFromColumn(colMemoA, rowTop, rowBot, colL)) return kScanTook;
    if (colMemoB != colMemoA && CollectHits(colMemoB, mLo, mHi)
        && TakeFromColumn(colMemoB, rowTop, rowBot, colR)) return kScanTook;

    return kScanEmpty;
}

NI FIXED bool CollectCoins(const u16 px, const u16 py, const vec2<u8> dimensions) {
    const u8 r = CollectCoinsImpl(px, py, dimensions);
    coinGate1 = { px, py, r == kScanEmpty };
    return r == kScanTook;
}

NI FIXED bool CollectCoins2(const u16 px, const u16 py, const vec2<u8> dimensions) {
    const u8 r = CollectCoinsImpl(px, py, dimensions);
    coinGate2 = { px, py, r == kScanEmpty };
    return r == kScanTook;
}

}   // namespace level
