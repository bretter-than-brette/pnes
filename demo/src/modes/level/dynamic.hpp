#pragma once

#include "cursor.hpp"
#include <intsh>

using namespace br0::intsh;

namespace level {

// The dynamic (coin) plane: RLE in RAM so it can be edited as coins are taken,
// composited over the static plane by collision_map.

constexpr u16 DynRunCapacity = 512;

extern u8 DynData[DynRunCapacity];

extern const u8* DynLengths;
extern const u8* DynDataROM;

class DynamicCursor {
public:
    const u8* lp;   // pointer into DynLengths at the current run
    u8*       dp;   // pointer into DynData (RAM) at the current run
    u8        progress;

    void Move(i8 amt);
    void Seek(i16 amt);

    [[nodiscard]] u8  Fetch() const { return *dp; }                          // RAM peek
    [[nodiscard]] u16 Run()   const { return static_cast<u16>(dp - DynData); } // run index
};

extern DynamicCursor dynEdgeR;   // lockstep with level::edgeR (forward/right edge)
extern DynamicCursor dynEdgeL;   // lockstep with level::edgeL (backward/left edge)

struct DynHit {
    bool hit;
    u8   id;
    u16  run;
};

DynHit CollidesDynamic(const DynamicCursor& origin, u16 px, u16 py, u8 w, u8 h);

void RemoveDynamic(const DynHit& h, const Cursor &under);

void LoadDynamicLayer(const u8* dynLengthsROM, const u8* dynDataROM, u16 runCount);

}   // namespace level
