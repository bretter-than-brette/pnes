#include "levels.hpp"
#include "collision_map.hpp"   // ColMapColumn: read the composited column the window already built
#include "../../graphics/metatiles.hpp"
#include "../../banks.hpp"     // ACTORS, level_graphics_tag
#include <pnes/mappers/mmc3.hpp>  // mmc3::CallInBlock
#include <pnes/video.hpp>         // ppu::WriteFromProviderToNameTable/WriteFromBufferToAttributeTable

#include <intsh>
#include <pnes/technology.hpp>

#include "types.hpp"
using namespace br0::intsh;

namespace level {
    Cursor edgeR;
    Cursor edgeL;
    u16 nColumns;
    u16 maxCameraX;   // derived from nColumns; see levels.hpp

    const u8* TileData;   // static plane: flat column-major ROM array, see types.hpp

    u8 MetatileBuffer[14];
    u8 AttributeBuffer[8];
    u8 attr_half;

// Level data and the bank each one lives in. Window 2 is pinned to the current
// level bank for the whole session, so a level read costs what a RAM read costs.
    static constexpr u8 kPalShifted[4][4] = {
        { 0,  0,   0,   0 },
        { 1,  4,  16,  64 },
        { 2,  8,  32, 128 },
        { 3, 12,  48, 192 },
    };

    static constexpr u8 kNextAttrIdx[14] = {0,1,1,2,2,3,3,4,4,5,5,6,6,7};
    static constexpr u8 kPrevAttrIdx[14] = {7,6,6,5,5,4,4,3,3,2,2,1,1,0};


    LEVEL_DATA const u8 TileData_1_1[] = {
        #include "../../../tiled/include/1-1_st"
    };

    LEVEL_DYNAMIC const u8 DynData_1_1[] = {
        #include "../../../tiled/include/1-1_dt"
    };

    LEVEL_DATA const u8 DynLengths_1_1[] = {
        #include "../../../tiled/include/1-1_dl"
    };

    const Level Levels[] = {
        {TileData_1_1, sizeof(TileData_1_1) / levelHeight,
         DynData_1_1, DynLengths_1_1, sizeof(DynLengths_1_1)}
    };

    NI LEVEL_CODE bool LoadLevel(const u16 n) {
        TileData = Levels[n].TileData;
        nColumns = Levels[n].nColumns;
        maxCameraX = nColumns > viewport_mx()
                   ? static_cast<u16>((nColumns - viewport_mx()) << 4) : 0;

        mmc3::SwitchBank(mmc3::window2Control, LevelDynamicBank(n));
        LoadDynamicLayer(Levels[n].DynLengths, Levels[n].DynData, Levels[n].DynRuns);

        mmc3::SwitchBank(mmc3::window2Control, LevelGraphicsBank(n));
        LoadMetatilePlanes();

        mmc3::SwitchBank(mmc3::window2Control, LevelDataBank(n));
        return true;
    }

    AI
    u8 GetNextMetaTile() {
        const u8 s = edgeR.Fetch();
        const u8 d = dynEdgeR.Fetch();
        edgeR.Move(1);
        dynEdgeR.Move(1);
        return d ? d : s;
    }

    AI
    u8 GetPrevMetaTile() {
        const u8 s = edgeL.Fetch();
        const u8 d = dynEdgeL.Fetch();
        edgeL.Move(-1);
        dynEdgeL.Move(-1);
        return d ? d : s;
    }

    AI
    u8 GetNextWrite(const u8 step) {
        u8 m;
        if (~step & 1) {
            if (step == 0) {
                const u8 mask = attr_half ? 0x33 : 0x00;
                for (auto & j : AttributeBuffer)
                    j &= mask;
                AttributeBuffer[0] |= 0x0F;   // HUD rows 0-1 share attr_idx 0's top quadrant with level rows 2-3; pin it to palette 3
                AttributeBuffer[7] |= 0xF0;   // mirror of the above at the other end: the 8-byte attribute write spans tile rows 0-31, but the level is only rows 0-29, so attr_idx 7's BOTTOM quadrant (rows 30-31) is UI band -- pin it to palette 3 too
            }
            m = GetNextMetaTile();
            MetatileBuffer[step >> 1] = m;
        } else {
            m = MetatileBuffer[step >> 1];
        }
        if (~step & 1) {
            const u8 tile_row  = 2 + step;
            const u8 attr_idx  = tile_row >> 2;
            const u8 pal       = Metatiles_ATTR[m] & MetatilePaletteMask;
            const u8 is_bottom = tile_row >> 1 & 1;
            const u8 shift     = attr_half
                                    ? (is_bottom ? 6 : 2)
                                    : is_bottom ? 4 : 0;
            AttributeBuffer[attr_idx] |= kPalShifted[pal][shift >> 1];
        }
        return step & 1 ? Metatiles_UR[m] : Metatiles_UL[m];
    }

    AI
    u8 GetPrevWrite(const u8 step) {
        u8 m;
        if (~step & 1) {
            if (step == 0) {
                const u8 mask = attr_half ? 0x00 : 0xCC;
                for (auto & j : AttributeBuffer)
                    j &= mask;
                AttributeBuffer[0] |= 0x0F;   // HUD rows 0-1 share attr_idx 0's top quadrant with level rows 2-3; pin it to palette 3
                AttributeBuffer[7] |= 0xF0;   // mirror of the above at the other end: the 8-byte attribute write spans tile rows 0-31, but the level is only rows 0-29, so attr_idx 7's BOTTOM quadrant (rows 30-31) is UI band -- pin it to palette 3 too
            }
            m = GetPrevMetaTile();
            MetatileBuffer[step >> 1] = m;
        } else {
            m = MetatileBuffer[step >> 1];
        }
        if (~step & 1) {
            const u8 tile_row  = 29 - step;
            const u8 attr_idx  = tile_row >> 2;
            const u8 pal       = Metatiles_ATTR[m] & MetatilePaletteMask;
            const u8 is_bottom = tile_row >> 1 & 1;
            const u8 shift     = attr_half
                                    ? (is_bottom ? 6 : 2)
                                    : is_bottom ? 4 : 0;
            AttributeBuffer[attr_idx] |= kPalShifted[pal][shift >> 1];
        }
        return step & 1 ? Metatiles_UL[m] : Metatiles_UR[m];
    }

    AI
    u8 GetCurrentNext(const u8 step) {
        const u8 m = MetatileBuffer[step >> 1];
        return step & 1 ? Metatiles_BR[m] : Metatiles_BL[m];
    }

    AI
    u8 GetCurrentPrev(const u8 step) {
        const u8 m = MetatileBuffer[step >> 1];
        return step & 1 ? Metatiles_BL[m] : Metatiles_BR[m];
    }

    ACTORS void BuildNextColumn(u8* buf, const u16 worldCol) {
        const u8* col = ColMapColumn(worldCol);
        if (!col) return;

        attr_half = worldCol & 1;
        const u8 mask = attr_half ? 0x33 : 0x00;
        for (auto & j : AttributeBuffer) j &= mask;
        AttributeBuffer[0] |= 0x0F;   // HUD rows 0-1 share attr_idx 0's top quadrant with level rows 2-3; pin it to palette 3
        AttributeBuffer[7] |= 0xF0;   // mirror of the above at the other end: the 8-byte attribute write spans tile rows 0-31, but the level is only rows 0-29, so attr_idx 7's BOTTOM quadrant (rows 30-31) is UI band -- pin it to palette 3 too

        const u8 shiftIdxEven = attr_half ? 3 : 2;
        const u8 shiftIdxOdd  = attr_half ? 1 : 0;

        for (u8 mt = 0; mt < 14; ++mt) {
            const u8 m   = col[mt];                  // top-to-bottom, slot s row mt
            const u8 pal = Metatiles_ATTR[m] & MetatilePaletteMask;
            const u8 shiftIdx = mt & 1 ? shiftIdxOdd : shiftIdxEven;
            AttributeBuffer[kNextAttrIdx[mt]] |= kPalShifted[pal][shiftIdx];

            const u8 step      = mt << 1;             // even tile-step: 0,2,..,26
            buf[step]          = Metatiles_UL[m];
            buf[step + 1]      = Metatiles_UR[m];
            buf[28 + step]     = Metatiles_BL[m];
            buf[28 + step + 1] = Metatiles_BR[m];
        }
    }

    NI LEVEL_CODE void PopulateNameTableColumns(const u16 nCols) {
        edgeR    = { TileData };
        dynEdgeR = { DynLengths, DynData, 0 };

        for (u16 i = 0; i < nCols; i += 2) {
            attr_half = (i >> 1) & 1;
            ppu::WriteFromProviderToNameTable({i, 2}, GetNextWrite, 28, 1);
            ppu::WriteFromProviderToNameTable({static_cast<u16>(i + 1), 2}, GetCurrentNext, 28, 1);
            ppu::WriteFromBufferToAttributeTable({static_cast<u16>(i & ~3), 2}, AttributeBuffer, 8, 1);
        }
    }

    ACTORS void BuildPrevColumn(u8* buf, const u16 worldCol) {
        const u8* col = ColMapColumn(worldCol);
        if (!col) return;

        attr_half = worldCol & 1;
        const u8 ap = attr_half ^ 1;
        const u8 mask = ap ? 0xCC : 0x00;
        for (auto & j : AttributeBuffer) j &= mask;
        AttributeBuffer[0] |= 0x0F;   // HUD rows 0-1 share attr_idx 0's top quadrant with level rows 2-3; pin it to palette 3
        AttributeBuffer[7] |= 0xF0;   // mirror of the above at the other end: the 8-byte attribute write spans tile rows 0-31, but the level is only rows 0-29, so attr_idx 7's BOTTOM quadrant (rows 30-31) is UI band -- pin it to palette 3 too

        const u8 shiftIdxEven = ap ? 0 : 1;
        const u8 shiftIdxOdd  = ap ? 2 : 3;

        for (u8 mt = 0; mt < 14; ++mt) {
            const u8 m   = col[13 - mt];              // bottom-to-top, mirroring edgeL's walk
            const u8 pal = Metatiles_ATTR[m] & MetatilePaletteMask;
            const u8 shiftIdx = mt & 1 ? shiftIdxOdd : shiftIdxEven;
            AttributeBuffer[kPrevAttrIdx[mt]] |= kPalShifted[pal][shiftIdx];

            const u8 step  = mt << 1;
            buf[54 - step] = Metatiles_UL[m];
            buf[55 - step] = Metatiles_UR[m];
            buf[26 - step] = Metatiles_BL[m];
            buf[27 - step] = Metatiles_BR[m];
        }
    }

}