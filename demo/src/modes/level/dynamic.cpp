#include "dynamic.hpp"

#include "levels.hpp"
#include "../../graphics/metatiles.hpp"

#include <array>
#include <intsh>

using namespace br0::intsh;

namespace level {
// Coin edits to the dynamic plane, and the VRAM queue that pushes the resulting
// tile writes out in vblank.

    u8 DynData[DynRunCapacity];

    const u8* DynLengths = nullptr;
    const u8* DynDataROM = nullptr;

    DynamicCursor dynEdgeR;
    DynamicCursor dynEdgeL;

    void DynamicCursor::Move(i8 amt) {
        const u8* dlp = lp;
        u8*       ddp = dp;
        u8 p = progress;
        if (amt > 0) {
            #pragma clang loop unroll(disable)
            for (u8 n = static_cast<u8>(amt); n != 0; --n) {
                if (++p >= *dlp) { ++dlp; ++ddp; p = 0; }
            }
        } else if (amt < 0) {
            #pragma clang loop unroll(disable)
            for (u8 n = static_cast<u8>(0 - static_cast<u8>(amt)); n != 0; --n) {
                if (p == 0) {
                    if (dlp == DynLengths) break;
                    --dlp; --ddp; p = *dlp;
                }
                --p;
            }
        }
        lp = dlp; dp = ddp; progress = p;
    }

    void DynamicCursor::Seek(i16 amt) {
        const u8* dlp = lp;
        u8*       ddp = dp;
        u8 p = progress;
        if (amt > 0) {
            #pragma clang loop unroll(disable)
            for (u16 n = static_cast<u16>(amt); n != 0; --n) {
                if (++p >= *dlp) { ++dlp; ++ddp; p = 0; }
            }
        } else if (amt < 0) {
            #pragma clang loop unroll(disable)
            for (u16 n = static_cast<u16>(0 - static_cast<u16>(amt)); n != 0; --n) {
                if (p == 0) {
                    if (dlp == DynLengths) break;
                    --dlp; --ddp; p = *dlp;
                }
                --p;
            }
        }
        lp = dlp; dp = ddp; progress = p;
    }

    DynHit CollidesDynamic(const DynamicCursor& origin, const u16 px, const u16 py, const u8 w, const u8 h) {
        const u16 rowTop = py >> 4;
        if (rowTop >= levelHeight) return {false, 0, 0};

        u16 rowBot = (py + h - 1) >> 4;
        if (rowBot >= levelHeight) rowBot = levelHeight - 1;

        const u8 cols = ((px + w - 1) >> 4) - (px >> 4);
        const u8 rows = static_cast<u8>(rowBot - rowTop);

        DynamicCursor probe = origin;
        for (u8 c = 0; ; c++) {
            for (u8 r = 0; r <= rows; r++) {
                if (const u8 id = probe.Fetch(); id != 0)
                    return {true, id, probe.Run()};
                if (r != rows) probe.Move(1);                // next row, same column
            }
            if (c == cols) break;
            probe.Move(static_cast<i16>(levelHeight - rows)); // cross to next column, row 0
        }
        return {false, 0, 0};
    }

    namespace {

        using DynRule = void (*)(const DynHit&, Cursor);

        void RuleNop(const DynHit&, Cursor) {}

        void RuleReveal(const DynHit& self, const Cursor &under) {
            DynData[self.run] = 0;            // consume: this run is now air
            const u8 beneath = under.Fetch(); // static metatile to expose
            (void)beneath;                    // STUB: queue a nametable patch of `beneath`
        }

        void RuleCoin(const DynHit& self, Cursor under) {
            RuleReveal(self, under);
        }

        constexpr u8 DYN_COIN = 0x62;   // coin metatile id (see metatiles.cpp)

        constexpr std::array<DynRule, 256> BuildDynRules() {
            std::array<DynRule, 256> t{};
            for (auto& e : t) e = &RuleNop;
            t[DYN_COIN] = &RuleCoin;
            return t;
        }

        constexpr std::array<DynRule, 256> DynRemovalRules = BuildDynRules();

    }   // namespace

    void RemoveDynamic(const DynHit& h, const Cursor &under) {
        DynRemovalRules[h.id](h, under);
    }

    void LoadDynamicLayer(const u8* dynLengthsROM, const u8* dynDataROM, u16 runCount) {
        DynLengths = dynLengthsROM;
        DynDataROM = dynDataROM;
        for (u16 i = 0; i < runCount; ++i)
            DynData[i] = dynDataROM ? dynDataROM[i] : 0;
    }

}   // namespace level
