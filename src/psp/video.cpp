/**
 * @file video.cpp
 * @brief PSP presentation backend: the core renders straight into the VRAM back
 *        buffer at native resolution, with no scale step at all.
 *
 * No GU, and deliberately no scale-blit of any kind -- every destination pixel is
 * exactly one source pixel. viewport_px() matches the panel's 480 exactly, but
 * viewport_py() is capped at the NES's 240 rather than the panel's 272: the
 * shared core's vertical walk wraps at the nametable grid's height, so a taller
 * viewport reads back into row 0 part-way down the screen -- which is the
 * mirroring a first cut of this backend hit. So the frame is letterboxed inside
 * 480x272, with the bars filled once at init.
 */
#include "internal.hpp"

#include <pnes/video.hpp>
#include <pnes/interrupts.hpp>
#include "../emu/emu.hpp"

#include <cstring>
#include <pspkernel.h>
#include <pspdisplay.h>
#include <psppower.h>

PSP_MODULE_INFO(PROJECT_NAME, 0, 1, 0);
PSP_MAIN_THREAD_ATTR(THREAD_ATTR_USER | THREAD_ATTR_VFPU);

static int exit_callback(int, int, void *) {
    quit = 1;
    return 0;
}

static int callback_thread(SceSize, void *) {
    const int cbid = sceKernelCreateCallback("Exit Callback", exit_callback, nullptr);
    sceKernelRegisterExitCallback(cbid);
    sceKernelSleepThreadCB();
    return 0;
}

void psp_setup_exit_callback() {
    const int thid = sceKernelCreateThread("exit_callback_thread", callback_thread, 0x11, 0xFA0, 0, nullptr);
    if (thid >= 0) sceKernelStartThread(thid, 0, nullptr);
}

static constexpr int PANEL_W = 480;                   // real screen width
static constexpr int PANEL_H = 272;                   // real screen height
static constexpr int VP_W = video::viewport_px();     // 480 -- matches PANEL_W exactly
static constexpr int VP_H = video::viewport_py();     // 240 -- capped below PANEL_H, see file header
static constexpr int LETTERBOX_Y = (PANEL_H - VP_H) / 2;   // 16px black bar, top and bottom

static_assert(VP_W == PANEL_W, "PSP viewport width must match the panel exactly -- no horizontal scale step exists here");
static_assert(VP_H <= PANEL_H, "PSP viewport height must fit within the panel -- see viewport_ty()'s comment (video.hpp) on why it can't just follow PANEL_H");

static constexpr int VRAM_STRIDE = 512;
static constexpr uintptr_t VRAM_UNCACHED_BASE = 0x44000000;
static u32 *const vram_buf[2] = {
    reinterpret_cast<u32 *>(VRAM_UNCACHED_BASE),
    reinterpret_cast<u32 *>(VRAM_UNCACHED_BASE + VRAM_STRIDE * PANEL_H * sizeof(u32)),
};
static int back_buf = 0;   // index of vram_buf[] this frame draws into

static inline u32 argb_to_rgba(const u32 p) {
    return (p & 0xFF00FF00u) | ((p & 0x00FF0000u) >> 16) | ((p & 0x000000FFu) << 16);
}

static void present_frame() {
    u32 *const out = vram_buf[back_buf];
    u32 *const vp  = out + static_cast<size_t>(LETTERBOX_Y) * VRAM_STRIDE;
    emu::GenerateFrame(vp, VRAM_STRIDE);

    for (int py = 0; py < VP_H; py++) {
        u32 *row = vp + static_cast<size_t>(py) * VRAM_STRIDE;
        for (int px = 0; px < VP_W; px++) row[px] = argb_to_rgba(row[px]);
    }

    sceDisplaySetFrameBuf(vram_buf[back_buf], VRAM_STRIDE,
                          PSP_DISPLAY_PIXEL_FORMAT_8888, PSP_DISPLAY_SETBUF_NEXTFRAME);
    sceDisplayWaitVblankStart();
    back_buf ^= 1;
}

static void present_blank() {
    std::memset(vram_buf[back_buf], 0, static_cast<size_t>(VRAM_STRIDE) * PANEL_H * sizeof(u32));
    sceDisplaySetFrameBuf(vram_buf[back_buf], VRAM_STRIDE,
                          PSP_DISPLAY_PIXEL_FORMAT_8888, PSP_DISPLAY_SETBUF_NEXTFRAME);
    sceDisplayWaitVblankStart();
    back_buf ^= 1;
}

namespace video {

void WaitForPresent() {
    if (quit) return;

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
    scePowerSetClockFrequency(333, 333, 166);

    emu::InitMemory(video::vram_bytes());

    std::memset(vram_buf[0], 0, static_cast<size_t>(VRAM_STRIDE) * PANEL_H * sizeof(u32));
    std::memset(vram_buf[1], 0, static_cast<size_t>(VRAM_STRIDE) * PANEL_H * sizeof(u32));

    sceDisplaySetMode(0, PANEL_W, PANEL_H);
    sceDisplaySetFrameBuf(vram_buf[0], VRAM_STRIDE,
                          PSP_DISPLAY_PIXEL_FORMAT_8888, PSP_DISPLAY_SETBUF_IMMEDIATE);
    back_buf = 1;

    psp_setup_exit_callback();
    input_init();
}

void irq::post() {
}
