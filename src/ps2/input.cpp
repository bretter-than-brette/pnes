/**
 * @file input.cpp
 * @brief Controller polling for the PS2 backend (libpad). SIO2MAN and PADMAN are
 *        BIOS-resident, so no module is embedded for this one.
 */
#include "internal.hpp"
#include <pnes/input.hpp>

#include <sifrpc.h>
#include <loadfile.h>
#include <libpad.h>

static char padBuf[256] __attribute__((aligned(64)));

void input_init() {
    SifLoadModule("rom0:SIO2MAN", 0, nullptr);
    SifLoadModule("rom0:PADMAN", 0, nullptr);

    padInit(0);
    padPortOpen(0, 0, padBuf);
}

void input::PollControllers(u8 *port1, u8 *port2) {
    struct padButtonStatus pad;

    u8 state = 0;
    if (padGetState(0, 0) == PAD_STATE_STABLE && padRead(0, 0, &pad) != 0) {
        const u16 buttons = 0xffffu ^ pad.btns;

        if (buttons & PAD_CROSS)  state |= A;
        if (buttons & PAD_CIRCLE) state |= B;
        if (buttons & PAD_SELECT) state |= SELECT;
        if (buttons & PAD_START)  state |= START;
        if (buttons & PAD_UP)     state |= UP;
        if (buttons & PAD_DOWN)   state |= DOWN;
        if (buttons & PAD_LEFT)   state |= LEFT;
        if (buttons & PAD_RIGHT)  state |= RIGHT;
    }

    *port1 = state;
    *port2 = 0;
}
