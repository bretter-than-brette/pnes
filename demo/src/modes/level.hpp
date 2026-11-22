#pragma once
#include <pnes/pnes.hpp>

#include "level/player.hpp"
#include "../main.hpp"

namespace level {
    extern bool paused;
    extern bool multiplayer;


    enum class eLevelStreamCommands : u8 {
        STREAM_LEVEL_LEFT  = 0x00,
        STREAM_LEVEL_RIGHT = 0x01,
        STREAM_LEVEL_DONE  = 0x02,
        STREAM_LEVEL_SWAP  = 0x04,
    };

    extern u8 port1;
    extern u8 port2;

    extern u8 lastPort1;
    extern u8 lastPort2;

    extern u16 cameraX;

    extern i8 lastDeltaScroll;

    extern atomic u16 lastXWorldSpace;

    extern u8 TileBuffer[56];

    extern Player player1;
#ifdef PLAYER2_SUPPORTED
    extern Player player2;

// The play mode entry points the main loop dispatches to.
    extern bool multiplayer;
#endif

    extern atomic tech::enum_flags<eLevelStreamCommands> levelStreamCommand;

#ifdef TARGET_NES
    void CoinVramPush(u16 address, u8 v0, u8 v1);
#else
    void CoinVramPush(u16 address, u8 value);
#endif

    void UpdateActors();

    void main();

    void FeedOAM();

    void nmi_handler();
    void irq_handler();
}
