/**
 * @file video.cpp
 * @brief Switch presentation backend: a libnx framebuffer on the default window,
 *        one widescreen ARGB frame from the core, CPU scale-blitted to 720p.
 *
 * The Tegra is hugely overpowered for NES-resolution pixels, so a straight
 * scale-blit is effectively free and avoids pulling in deko3d for a first pass.
 * 16:9, so rather than pillarbox a 4:3 frame this renders a wider viewport: 240
 * maps to 720 at an exact 3x, and 416 -> 1280 is a near-3x nearest-neighbour
 * stretch that fills the panel edge to edge. The present is vsync-locked.
 */
#include "internal.hpp"

#include <pnes/video.hpp>
#include <pnes/interrupts.hpp>
#include "../emu/emu.hpp"

#include <cstring>
#include <switch.h>

static constexpr int VP_W = video::viewport_px();   // 416 (widescreen viewport)
static constexpr int VP_H = video::viewport_py();   // 240
static constexpr int FB_W = 1280;
static constexpr int FB_H = 720;

static Framebuffer fb;
static u32 staging[VP_W * VP_H];

// Nearest-neighbour source-index maps, precomputed in init() so the per-frame
// blit is a pure gather with no per-pixel divides.
static int colmap[FB_W];
static int rowmap[FB_H];

// The core fills ARGB8888; the framebuffer is RGBA_8888, so R and B swap. Done
// once per SOURCE pixel, in place, before the scale expansion.
static inline u32 argb_to_rgba(const u32 p) {
    return (p & 0xFF00FF00u) | ((p & 0x00FF0000u) >> 16) | ((p & 0x000000FFu) << 16);
}

static void present_frame() {
    u32 stride_bytes;
    auto *fbptr = static_cast<u8 *>(framebufferBegin(&fb, &stride_bytes));
    const u32 stride_px = stride_bytes / 4;

    emu::GenerateFrame(staging, VP_W);

    for (int i = 0; i < VP_W * VP_H; i++) staging[i] = argb_to_rgba(staging[i]);

    auto *out = reinterpret_cast<u32 *>(fbptr);
    for (int dy = 0; dy < FB_H; dy++) {
        const u32 *srow = &staging[rowmap[dy] * VP_W];
        u32 *drow = out + static_cast<size_t>(dy) * stride_px;
        for (int dx = 0; dx < FB_W; dx++) drow[dx] = srow[colmap[dx]];
    }

    framebufferEnd(&fb);
}

static void present_blank() {
    u32 stride_bytes;
    auto *fbptr = static_cast<u8 *>(framebufferBegin(&fb, &stride_bytes));
    std::memset(fbptr, 0, static_cast<size_t>(stride_bytes) * FB_H);
    framebufferEnd(&fb);
}

namespace video {

void WaitForPresent() {
    if (!appletMainLoop()) {
        quit = 1;
        return;
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
    emu::InitMemory(video::vram_bytes());

    for (int dx = 0; dx < FB_W; dx++) colmap[dx] = dx * VP_W / FB_W;
    for (int dy = 0; dy < FB_H; dy++) rowmap[dy] = dy * VP_H / FB_H;

    NWindow *win = nwindowGetDefault();
    framebufferCreate(&fb, win, FB_W, FB_H, PIXEL_FORMAT_RGBA_8888, 2);
    framebufferMakeLinear(&fb);

    input_init();
}

void irq::post() {
    framebufferClose(&fb);
}
