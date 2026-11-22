/**
 * @file input.cpp
 * @brief Controller polling for the DS and DSi backend (libnds keypad). One
 *        player, with X/Y mirrored onto Start/Select so the larger buttons work.
 *
 * Only the lower panel is a digitizer, so a touch is always on panel 1 and
 * video::panel_rect(1) places it inside the viewport -- which makes the whole
 * conversion one add per axis, the panels being 1:1 with viewport pixels.
 */
#include "internal.hpp"
#include <pnes/input.hpp>
#include <pnes/video.hpp>

#include <nds.h>

void input::PollControllers(u8* port1, u8* port2) {
    scanKeys();
    const u32 held = keysHeld();

    u8 state = 0;
    if (held & KEY_A)                 state |= A;
    if (held & KEY_B)                 state |= B;
    if (held & (KEY_SELECT | KEY_Y))  state |= SELECT;
    if (held & (KEY_START  | KEY_X))  state |= START;
    if (held & KEY_UP)                state |= UP;
    if (held & KEY_DOWN)              state |= DOWN;
    if (held & KEY_LEFT)              state |= LEFT;
    if (held & KEY_RIGHT)             state |= RIGHT;

    *port1 = state;
    *port2 = 0;

    touchPosition touch;
    const bool touching = touchRead(&touch);

    constexpr video::PanelRect bottom = video::panel_rect(1);
    input::click.Set({static_cast<u16>(touch.px + bottom.x),
                      static_cast<u16>(touch.py + bottom.y)}, touching);
}
