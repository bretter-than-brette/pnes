/**
 * @file input.cpp
 * @brief Controller polling for the native Wii U backend (SDL2 GameController).
 *
 * The portlib SDL2 exposes the GamePad, Pro Controllers and Wiimotes through the
 * standard GameController API, mapped here onto the engine's NES bitmask. Two
 * players, assigned by DEVICE IDENTITY rather than enumeration order: the GamePad
 * is always player 1.
 *
 * The GamePad's touch screen fills input::click through SDL2's own touch device
 * rather than wut's VPAD -- the SDL joystick driver is already calling VPADRead
 * and feeding SDL_SendTouch, so reading VPAD again would be two consumers of one
 * sample stream. That is why the touch read sits after the controller update.
 * The GamePad mirrors the TV frame, so a normalized touch maps straight onto the
 * viewport with no panel offset.
 */
#include "internal.hpp"
#include <pnes/input.hpp>
#include <pnes/video.hpp>

#include <SDL2/SDL.h>

static SDL_GameController *pads[2] = { nullptr, nullptr };

/// Keyed on SDL_JoystickGetDevicePlayerIndex, which the driver maps to a stable
/// identity (0 is the GamePad, 1..4 the Wii Remotes). device_index is merely a
/// position in the registration list, so slot 0 means "whoever registered
/// first", not "the GamePad".
static void rebuild_pads() {
    for (auto &pad : pads) {
        if (pad) {
            SDL_GameControllerClose(pad);
            pad = nullptr;
        }
    }

    int gamepad_dev = -1;   // device_index holding identity 0
    int wpad_dev    = -1;   // device_index of the lowest-numbered Remote
    int wpad_id     = 0;    // that Remote's identity, 0 = none found yet

    const int n = SDL_NumJoysticks();
    for (int i = 0; i < n; i++) {
        if (!SDL_IsGameController(i)) continue;

        const int id = SDL_JoystickGetDevicePlayerIndex(i);
        if (id == 0) {
            gamepad_dev = i;
        } else if (id >= 1 && id <= 4 && (wpad_id == 0 || id < wpad_id)) {
            wpad_dev = i;
            wpad_id  = id;
        }
    }

    if (gamepad_dev >= 0) pads[0] = SDL_GameControllerOpen(gamepad_dev);
    if (wpad_dev    >= 0) pads[1] = SDL_GameControllerOpen(wpad_dev);

    if (!pads[0] && pads[1]) {
        pads[0] = pads[1];
        pads[1] = nullptr;
    }
}

void input_init() {
    rebuild_pads();
}

void input_handle_event(const union SDL_Event *e) {
    if (e->type == SDL_JOYDEVICEADDED || e->type == SDL_JOYDEVICEREMOVED) {
        rebuild_pads();
    }
}

static u8 read_pad(SDL_GameController *gc) {
    if (!gc) return 0;

    u8 state = 0;
    if (SDL_GameControllerGetButton(gc, SDL_CONTROLLER_BUTTON_A))          state |= input::A;
    if (SDL_GameControllerGetButton(gc, SDL_CONTROLLER_BUTTON_B))          state |= input::B;
    if (SDL_GameControllerGetButton(gc, SDL_CONTROLLER_BUTTON_BACK))       state |= input::SELECT;
    if (SDL_GameControllerGetButton(gc, SDL_CONTROLLER_BUTTON_START))      state |= input::START;
    if (SDL_GameControllerGetButton(gc, SDL_CONTROLLER_BUTTON_DPAD_UP))    state |= input::UP;
    if (SDL_GameControllerGetButton(gc, SDL_CONTROLLER_BUTTON_DPAD_DOWN))  state |= input::DOWN;
    if (SDL_GameControllerGetButton(gc, SDL_CONTROLLER_BUTTON_DPAD_LEFT))  state |= input::LEFT;
    if (SDL_GameControllerGetButton(gc, SDL_CONTROLLER_BUTTON_DPAD_RIGHT)) state |= input::RIGHT;
    return state;
}

void input::PollControllers(u8 *port1, u8 *port2) {
    SDL_GameControllerUpdate();

    *port1 = read_pad(pads[0]);
    *port2 = read_pad(pads[1]);

    bool touching = false;
    vec2<u16> at{0, 0};
    if (SDL_GetNumTouchDevices() > 0) {
        const SDL_TouchID dev = SDL_GetTouchDevice(0);
        if (const SDL_Finger* finger = SDL_GetTouchFinger(dev, 0)) {
            touching = true;
            const float fx = finger->x * static_cast<float>(video::viewport_px());
            const float fy = finger->y * static_cast<float>(video::viewport_py());
            const u32 x = fx < 0.0f ? 0u : static_cast<u32>(fx);
            const u32 y = fy < 0.0f ? 0u : static_cast<u32>(fy);
            at = {static_cast<u16>(x < video::viewport_px() ? x : video::viewport_px() - 1),
                  static_cast<u16>(y < video::viewport_py() ? y : video::viewport_py() - 1)};
        }
    }

    input::click.Set(at, touching);
}
