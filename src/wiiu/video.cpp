/**
 * @file video.cpp
 * @brief Native Wii U presentation backend for the shared software PPU (SDL2).
 *
 * Opens an SDL2 window and renderer -- the wiiu portlib drives GX2, ProcUI and
 * the TV scan buffer underneath -- asks the core for one widescreen ARGB frame
 * straight into a streaming texture, and lets the GPU scale-blit it.
 *
 * Rather than pillarbox a 4:3 frame it renders a WIDER viewport, showing more of
 * the world horizontally. How wide depends on the TV, which here is a system
 * setting rather than a fixed property: AVMGetTVAspectRatio reports what the user
 * configured, and the viewport follows it -- 52 tiles on a widescreen set, 40 on
 * a 4:3 one.
 */
#define SDL_MAIN_HANDLED   // the demo supplies its own main() (RESET macro)
#include "internal.hpp"

#include <pnes/video.hpp>
#include <pnes/interrupts.hpp>
#include "../emu/emu.hpp"

#include <SDL2/SDL.h>
#include <avm/tv.h>
#include <coreinit/time.h>   // OSGetSystemTime / OSTicksToMicroseconds, for video::DetectRefresh

// Read once at init from AVMGetTVAspectRatio; video.hpp's viewport_tx() reads it
// from every other translation unit.
u16 wiiu_world_tx;

static constexpr int VP_H = video::viewport_py();   // 240
static constexpr int WIN_W = 1280;
static constexpr int WIN_H = 720;

static SDL_Window   *window  = nullptr;
static SDL_Renderer *renderer = nullptr;
static SDL_Texture  *frame    = nullptr;

static void present_frame() {
    void *pixels = nullptr;
    int pitch = 0;
    if (SDL_LockTexture(frame, nullptr, &pixels, &pitch) == 0) {
        emu::GenerateFrame(static_cast<u32 *>(pixels), pitch / 4);

        static u32 parity = 0;
        parity ^= 1;
        static_cast<u32 *>(pixels)[0] ^= parity;

        SDL_UnlockTexture(frame);
    }

    SDL_RenderClear(renderer);
    SDL_RenderCopy(renderer, frame, nullptr, nullptr);  // src=full tex, dst=full window
    SDL_RenderPresent(renderer);
}

static void present_blank() {
    SDL_SetRenderDrawColor(renderer, 0, 0, 0, 255);
    SDL_RenderClear(renderer);
    SDL_RenderPresent(renderer);
}

namespace video {

void WaitForPresent() {
    SDL_Event ev;
    while (SDL_PollEvent(&ev)) {
        if (ev.type == SDL_QUIT) {
            quit = 1;
            return;
        }
        input_handle_event(&ev);
    }

    if (ppu::PPUMASK & (ppu::mask::BG | ppu::mask::SPRITE)) {
        present_frame();
    } else {
        present_blank();
    }

    irq::irqPendingValid = false;
    nmi_vector();
}

}   // namespace video

void irq::init() {
    AVMTvAspectRatio aspect = AVM_TV_ASPECT_RATIO_16_9;
    if (!AVMGetTVAspectRatio(&aspect)) aspect = AVM_TV_ASPECT_RATIO_16_9;
    wiiu_world_tx = (aspect == AVM_TV_ASPECT_RATIO_4_3) ? 40 : 52;

    emu::InitMemory(video::vram_bytes());

    SDL_SetMainReady();
    SDL_Init(SDL_INIT_VIDEO | SDL_INIT_GAMECONTROLLER | SDL_INIT_AUDIO);

    window = SDL_CreateWindow(PROJECT_NAME,
                              SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED,
                              WIN_W, WIN_H, SDL_WINDOW_SHOWN);
    renderer = SDL_CreateRenderer(window, -1,
                                  SDL_RENDERER_ACCELERATED | SDL_RENDERER_PRESENTVSYNC);

    frame = SDL_CreateTexture(renderer, SDL_PIXELFORMAT_ARGB8888,
                              SDL_TEXTUREACCESS_STREAMING,
                              video::viewport_px(), VP_H);

    input_init();
}

void irq::post() {
    if (frame)    SDL_DestroyTexture(frame);
    if (renderer) SDL_DestroyRenderer(renderer);
    if (window)   SDL_DestroyWindow(window);
    SDL_Quit();
}

/// Times the real flip cadence rather than trusting the mode, as the OGC backend
/// does: a 50Hz set and an EURGB60 one come out of the same build.
bool video::DetectRefresh() {
    constexpr u32 kIntervals = 8;

    if (!renderer) return false;   // init failed; nothing is being paced.

    present_blank();                               // Align to a retrace.
    const u64 start = OSGetSystemTime();
    for (u32 i = 0; i < kIntervals; i++) present_blank();
    const u64 end = OSGetSystemTime();

    const u32 perFrameUs =
        static_cast<u32>(OSTicksToMicroseconds(end - start) / kIntervals);

    return perFrameUs > 18300;
}
