/**
 * @file input.cpp
 * @brief Controller polling for the 3DS backend (libctru hid). One player; both
 *        the D-pad and the Circle Pad drive the NES directions, and X/Y stand in
 *        for the tiny Start/Select.
 *
 * Only the lower panel is a digitizer, so a touch is always on panel 1 and
 * video::panel_rect(1) is where that panel sits inside the viewport.
 */
#include "internal.hpp"
#include <pnes/input.hpp>
#include <pnes/video.hpp>

#include <3ds.h>

void input::PollControllers(u8* port1, u8* port2) {
    hidScanInput();
    const u32 held = hidKeysHeld();

    u8 state = 0;
    if (held & KEY_A)                       state |= A;
    if (held & KEY_B)                       state |= B;
    if (held & (KEY_SELECT | KEY_Y))        state |= SELECT;
    if (held & (KEY_START  | KEY_X))        state |= START;
    if (held & (KEY_DUP    | KEY_CPAD_UP))    state |= UP;
    if (held & (KEY_DDOWN  | KEY_CPAD_DOWN))  state |= DOWN;
    if (held & (KEY_DLEFT  | KEY_CPAD_LEFT))  state |= LEFT;
    if (held & (KEY_DRIGHT | KEY_CPAD_RIGHT)) state |= RIGHT;

    *port1 = state;
    *port2 = 0;

    touchPosition touch;
    hidTouchRead(&touch);

    constexpr video::PanelRect bottom = video::panel_rect(1);
    input::click.Set({static_cast<u16>(touch.px + bottom.x),
                      static_cast<u16>(touch.py + bottom.y)},
                     (held & KEY_TOUCH) != 0);
}
