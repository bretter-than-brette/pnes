#pragma once
// Shared declarations for the demo: the mode enum the main loop dispatches on,
// the parked-sprite Y every mode uses to hide an actor, and the handful of
// cross-mode values that have nowhere better to live.
#include <pnes/pnes.hpp>

#ifdef TARGET_NES
inline const float silentHeapAmount  __attribute__((used, section(".pnes_log"))) = 0.0f;
inline const float silentStackAmount __attribute__((used, section(".pnes_log"))) = 0.0f;

#endif

enum class eGameModes : u8 {
    Title,  // title screen
    World,  // world map, level select
    Level,  // gameplay, platforming in level
    Pregame,
};

constexpr u8 viewport_mx() { return video::viewport_tx() >> 1; }
constexpr u8 viewport_my() { return video::viewport_ty() >> 1; }

constexpr oam::oam_t ParkedSpriteY() {
#ifdef TARGET_NES
    return 0xf0;
#else
    return video::viewport_py();
#endif
}

#if defined(TARGET_GBA) || defined(TARGET_NDS)
#  define PNES_WINDOWED_PANEL
#endif

#ifdef PNES_WINDOWED_PANEL

constexpr i16 kFrameRows = 240;

constexpr i16 VerticalWindow(const oam::oam_t playerFrameY) {
    constexpr i16 panelH = static_cast<i16>(video::panel_rect(0).h);
    constexpr i16 mid    = static_cast<i16>(panelH >> 1);
    constexpr i16 anchor = static_cast<i16>(kFrameRows - panelH);
    const i16 raw = static_cast<i16>(playerFrameY) - mid;
    return raw < 0 ? 0 : (raw > anchor ? anchor : raw);
}

constexpr oam::oam_t WindowedSpriteY(const oam::oam_t frameY, const oam::oam_t playerFrameY) {
    const i16 y = static_cast<i16>(frameY) - VerticalWindow(playerFrameY);
    return y < 0 ? ParkedSpriteY() : static_cast<oam::oam_t>(y);
}

#endif   // PNES_WINDOWED_PANEL

extern atomic eGameModes gameMode;

extern void (*pNMI)();
extern void (*pIRQ)();

extern u8 scratchpad[];

extern atomic u16 frameCounter;

extern oam::sprite_t OAMBuffer[64];
