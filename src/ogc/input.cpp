/**
 * @file input.cpp
 * @brief Controller polling for the GameCube and Wii backend.
 *
 * GameCube pads come through PAD on both consoles; the GC pad has no Select, so
 * Z stands in for it. On Wii, WPAD supports two grips and which one applies is
 * read from WPAD_Probe's extension type every frame -- the grip decides which way
 * the D-pad points, so it cannot be a compile-time choice. A bare Remote is held
 * sideways (D-pad rotated a quarter turn, 2 is A and 1 is B); a Remote with a
 * Nunchuk is held upright, keeping the D-pad's real orientation and adding the
 * stick. Results are OR'd onto the matching GC port.
 */
#include "internal.hpp"
#include <pnes/input.hpp>

#include <ogc/pad.h>
#ifdef TARGET_WII
#include <wiiuse/wpad.h>
#endif

#ifdef TARGET_WII
/// The Nunchuk reports its own calibrated centre in the extension's calibration
/// block, so there is no STICK_CENTER companion -- the centre is read per device
/// rather than assumed to be 128.
static constexpr int STICK_DEADZONE = 40;
#endif

void ogc_input_init() {
    PAD_Init();
#ifdef TARGET_WII
    WPAD_Init();
#endif
}

static u8 read_pad(const int chan) {
    const u32 held = PAD_ButtonsHeld(chan);
    u8 state = 0;
    if (held & PAD_BUTTON_A)     state |= input::A;
    if (held & PAD_BUTTON_B)     state |= input::B;
    if (held & PAD_TRIGGER_Z)    state |= input::SELECT;  // GC pad has no Select
    if (held & PAD_BUTTON_START) state |= input::START;
    if (held & PAD_BUTTON_UP)    state |= input::UP;
    if (held & PAD_BUTTON_DOWN)  state |= input::DOWN;
    if (held & PAD_BUTTON_LEFT)  state |= input::LEFT;
    if (held & PAD_BUTTON_RIGHT) state |= input::RIGHT;
    return state;
}

#ifdef TARGET_WII
static u8 read_wpad_sideways(const u32 held) {
    u8 state = 0;
    if (held & WPAD_BUTTON_2)     state |= input::A;
    if (held & WPAD_BUTTON_1)     state |= input::B;
    if (held & WPAD_BUTTON_MINUS) state |= input::SELECT;
    if (held & WPAD_BUTTON_PLUS)  state |= input::START;
    if (held & WPAD_BUTTON_RIGHT) state |= input::UP;
    if (held & WPAD_BUTTON_LEFT)  state |= input::DOWN;
    if (held & WPAD_BUTTON_UP)    state |= input::LEFT;
    if (held & WPAD_BUTTON_DOWN)  state |= input::RIGHT;
    return state;
}

static u8 read_wpad_nunchuk(const int chan, const u32 held) {
    u8 state = 0;
    if (held & (WPAD_BUTTON_A | WPAD_NUNCHUK_BUTTON_Z)) state |= input::A;
    if (held & (WPAD_BUTTON_B | WPAD_NUNCHUK_BUTTON_C)) state |= input::B;
    if (held & WPAD_BUTTON_MINUS) state |= input::SELECT;
    if (held & WPAD_BUTTON_PLUS)  state |= input::START;
    if (held & WPAD_BUTTON_UP)    state |= input::UP;
    if (held & WPAD_BUTTON_DOWN)  state |= input::DOWN;
    if (held & WPAD_BUTTON_LEFT)  state |= input::LEFT;
    if (held & WPAD_BUTTON_RIGHT) state |= input::RIGHT;

    expansion_t exp;
    WPAD_Expansion(chan, &exp);
    if (exp.type == WPAD_EXP_NUNCHUK) {
        const joystick_t& js = exp.nunchuk.js;
        const int dx = static_cast<int>(js.pos.x) - static_cast<int>(js.center.x);
        const int dy = static_cast<int>(js.pos.y) - static_cast<int>(js.center.y);
        if (dx < -STICK_DEADZONE) state |= input::LEFT;
        if (dx >  STICK_DEADZONE) state |= input::RIGHT;
        if (dy >  STICK_DEADZONE) state |= input::UP;
        if (dy < -STICK_DEADZONE) state |= input::DOWN;
    }
    return state;
}

static u8 read_wpad(const int chan) {
    u32 type = WPAD_EXP_NONE;
    if (WPAD_Probe(chan, &type) != WPAD_ERR_NONE) return 0;

    const u32 held = WPAD_ButtonsHeld(chan);

    if (type == WPAD_EXP_NUNCHUK) return read_wpad_nunchuk(chan, held);
    return read_wpad_sideways(held);
}
#endif

void input::PollControllers(u8* port1, u8* port2) {
    PAD_ScanPads();
    u8 p1 = read_pad(0);
    u8 p2 = read_pad(1);

#ifdef TARGET_WII
    WPAD_ScanPads();
    p1 |= read_wpad(WPAD_CHAN_0);
    p2 |= read_wpad(WPAD_CHAN_1);
#endif

    *port1 = p1;
    *port2 = p2;
}
