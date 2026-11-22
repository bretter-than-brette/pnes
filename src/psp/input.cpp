/**
 * @file input.cpp
 * @brief Controller polling for the PSP backend (sceCtrl). The analog stick
 *        drives the NES directions alongside the D-pad.
 */
#include "internal.hpp"
#include <pnes/input.hpp>

#include <pspctrl.h>

static constexpr int STICK_CENTER = 128;
static constexpr int STICK_DEADZONE = 40;

void input_init() {
    sceCtrlSetSamplingCycle(0);
    sceCtrlSetSamplingMode(PSP_CTRL_MODE_ANALOG);
}

void input::PollControllers(u8 *port1, u8 *port2) {
    SceCtrlData pad;
    sceCtrlReadBufferPositive(&pad, 1);

    u8 state = 0;
    if (pad.Buttons & PSP_CTRL_CROSS)  state |= A;
    if (pad.Buttons & PSP_CTRL_CIRCLE) state |= B;
    if (pad.Buttons & PSP_CTRL_SELECT) state |= SELECT;
    if (pad.Buttons & PSP_CTRL_START)  state |= START;

    if (pad.Buttons & PSP_CTRL_UP)    state |= UP;
    if (pad.Buttons & PSP_CTRL_DOWN)  state |= DOWN;
    if (pad.Buttons & PSP_CTRL_LEFT)  state |= LEFT;
    if (pad.Buttons & PSP_CTRL_RIGHT) state |= RIGHT;

    const int dx = static_cast<int>(pad.Lx) - STICK_CENTER;
    const int dy = static_cast<int>(pad.Ly) - STICK_CENTER;
    if (dy < -STICK_DEADZONE) state |= UP;
    if (dy >  STICK_DEADZONE) state |= DOWN;
    if (dx < -STICK_DEADZONE) state |= LEFT;
    if (dx >  STICK_DEADZONE) state |= RIGHT;

    *port1 = state;
    *port2 = 0;
}
