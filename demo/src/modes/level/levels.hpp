#pragma once

#include "types.hpp"
#include "cursor.hpp"
#include "dynamic.hpp"   // dynEdgeR / dynEdgeL, lockstep with edgeR / edgeL

#include <intsh>
#include <pnes/technology.hpp>
#include <pnes/types.hpp>      // vec2
using namespace br0::intsh;

namespace level {
    constexpr auto levelHeight = 14;
    constexpr i16 kHudRows = 1;
    extern u16 nColumns;

    // Highest cameraX the level allows. Derived from nColumns, so constant for
    // the level -- but it sat in the camera-scroll path and was recomputed every
    // frame the player moved, for an answer that cannot change.
    extern u16 maxCameraX;
    extern const u8* TileData;
    extern const u8 LevelDataAttributes[];
    extern Cursor edgeR;
    extern Cursor edgeL;   // seeded valid at reset, kept in lockstep with edgeR
    extern u8 attr_half;
    extern u8 AttributeBuffer[8];
    extern const Level Levels[];
    u8 GetPrevWrite(u8 step);
    u8 GetNextWrite(u8 step);
    u8 GetCurrentNext(u8 step);
    u8 GetCurrentPrev(u8 step);
    u8 GetPrevMetaTile();
    u8 GetNextMetaTile();
    void BuildNextColumn(u8* buf, u16 worldCol);   // right-scroll: reads window column
    void BuildPrevColumn(u8* buf, u16 worldCol);   // left-scroll: reads window column
    bool LoadLevel(u16 n);

    // Fills nCols nametable columns from world column 0 through the same
    // streaming path the level view and the title preview use. Always at
    // nametable row 2 (below the 2-row HUD), 28 rows tall -- the attribute math
    // is hardwired to that placement. An odd number of metatile columns is
    // allowed: the last attribute byte then carries only its left half.
    void PopulateNameTableColumns(u16 nCols);
}
