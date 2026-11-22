/**
 * @file input.cpp
 * @brief Controller polling for the PS3 backend (PSL1GHT libio). Both the D-pad
 *        and the left stick drive the NES directions.
 */
#include "internal.hpp"
#include <pnes/input.hpp>

#include <io/pad.h>

static constexpr int STICK_CENTER = 128;
static constexpr int STICK_DEADZONE = 40;

static constexpr u32 MAX_TRACKED_PADS = 7;

void input_init() {
    ioPadInit(MAX_TRACKED_PADS);
}

void input_exit() {
    ioPadEnd();
}

void input::PollControllers(u8 *port1, u8 *port2) {
    *port1 = 0;
    *port2 = 0;

    padInfo info;
    if (ioPadGetInfo(&info) != 0) return;
    if (info.connected == 0 || info.status[0] == 0) return;

    padData pad;
    if (ioPadGetData(0, &pad) != 0) return;

    u8 state = 0;
    if (pad.BTN_CROSS)    state |= A;
    if (pad.BTN_CIRCLE)   state |= B;
    if (pad.BTN_SELECT)   state |= SELECT;
    if (pad.BTN_START)    state |= START;

    if (pad.BTN_UP)       state |= UP;
    if (pad.BTN_DOWN)     state |= DOWN;
    if (pad.BTN_LEFT)     state |= LEFT;
    if (pad.BTN_RIGHT)    state |= RIGHT;

    const int dx = static_cast<int>(pad.ANA_L_H) - STICK_CENTER;
    const int dy = static_cast<int>(pad.ANA_L_V) - STICK_CENTER;
    if (dy < -STICK_DEADZONE) state |= UP;
    if (dy >  STICK_DEADZONE) state |= DOWN;
    if (dx < -STICK_DEADZONE) state |= LEFT;
    if (dx >  STICK_DEADZONE) state |= RIGHT;

    *port1 = state;
}
