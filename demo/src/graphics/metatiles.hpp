#pragma once
#include <intsh>
#include <array>
#include "../banks.hpp"   // LEVEL_GRAPHICS
using namespace br0::intsh;

#ifdef TARGET_NES
// Metatile ids and their collision classes.
extern std::array<u8, 256> Metatiles_UL;   // top-left  CHR tile
extern std::array<u8, 256> Metatiles_UR;   // top-right CHR tile
extern std::array<u8, 256> Metatiles_BL;   // bottom-left  CHR tile
extern std::array<u8, 256> Metatiles_BR;   // bottom-right CHR tile
extern std::array<u8, 256> Metatiles_ATTR; // 5th plane, packed per id:

void LoadMetatilePlanes();
#else
extern const std::array<u8, 256> Metatiles_UL;
extern const std::array<u8, 256> Metatiles_UR;
extern const std::array<u8, 256> Metatiles_BL;
extern const std::array<u8, 256> Metatiles_BR;
extern const std::array<u8, 256> Metatiles_ATTR;

inline void LoadMetatilePlanes() {}
#endif

inline constexpr u8 MetatileCollisionMask = 0b1111'1100;
inline constexpr u8 MetatilePaletteMask   = 0b0000'0011;

enum class MetatileCollision : u8 {
    None    = 0x00,   // no collision
    Solid   = 0x04,   // blocks the actor
    Collect = 0x08,   // collectable
};

constexpr u8 operator|(MetatileCollision c, u8 palette) {
    return static_cast<u8>(c) | (palette & MetatilePaletteMask);
}

constexpr bool IsBlocking(MetatileCollision c) {
    return c == MetatileCollision::Solid || c == MetatileCollision::Collect;
}

inline MetatileCollision GetMetatileCollisions(const u8 metatile) {
    return static_cast<MetatileCollision>(Metatiles_ATTR[metatile] & MetatileCollisionMask);
}