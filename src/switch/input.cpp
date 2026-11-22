/**
 * @file input.cpp
 * @brief Controller polling for the Switch backend (libnx hid/pad). One player,
 *        with both the D-pad and the left stick driving the NES directions.
 *
 * The touch screen fills input::click. Unlike the dual-screen handhelds there is
 * one panel and it IS the viewport, so the conversion is a scale rather than an
 * offset -- the digitizer always reports in the handheld panel's own 1280x720
 * whatever the console is scanning out.
 *
 * Nothing tests for docked mode: docked, no touches arrive, which is already
 * indistinguishable from a finger that is not touching.
 */
#include "internal.hpp"
#include <pnes/input.hpp>
#include <pnes/video.hpp>

#include <switch.h>

static PadState pad;

void input_init() {
    padConfigureInput(1, HidNpadStyleSet_NpadStandard);
    padInitializeDefault(&pad);
    hidInitializeTouchScreen();
}

void input::PollControllers(u8 *port1, u8 *port2) {
    padUpdate(&pad);
    const u64 held = padGetButtons(&pad);

    u8 state = 0;
    if (held & HidNpadButton_A)                              state |= A;
    if (held & HidNpadButton_B)                              state |= B;
    if (held & HidNpadButton_Minus)                          state |= SELECT;
    if (held & HidNpadButton_Plus)                           state |= START;
    if (held & (HidNpadButton_Up    | HidNpadButton_StickLUp))    state |= UP;
    if (held & (HidNpadButton_Down  | HidNpadButton_StickLDown))  state |= DOWN;
    if (held & (HidNpadButton_Left  | HidNpadButton_StickLLeft))  state |= LEFT;
    if (held & (HidNpadButton_Right | HidNpadButton_StickLRight)) state |= RIGHT;

    *port1 = state;
    *port2 = 0;

    constexpr u32 kTouchPanelW = 1280;
    constexpr u32 kTouchPanelH = 720;

    HidTouchScreenState touch = {};
    const bool touching = hidGetTouchScreenStates(&touch, 1) && touch.count > 0;

    vec2<u16> at{0, 0};
    if (touching) {
        const u32 x = static_cast<u32>(touch.touches[0].x) * video::viewport_px() / kTouchPanelW;
        const u32 y = static_cast<u32>(touch.touches[0].y) * video::viewport_py() / kTouchPanelH;
        at = {static_cast<u16>(x < video::viewport_px() ? x : video::viewport_px() - 1),
              static_cast<u16>(y < video::viewport_py() ? y : video::viewport_py() - 1)};
    }

    input::click.Set(at, touching);
}
