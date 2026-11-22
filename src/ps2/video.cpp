/**
 * @file video.cpp
 * @brief PS2 presentation backend for the shared software PPU (ps2sdk + gsKit).
 *
 * The GS's VRAM is not CPU-addressable at all, unlike the PSP's: every pixel has
 * to arrive via a GIF/DMA transfer, which is what a textured quad is for. So the
 * core renders the native 256x240 frame into an ARGB8888 staging buffer in EE
 * RAM, this uploads it as one texture per frame and draws one scaled quad.
 *
 * A 4:3-only console with no hardware widescreen path worth using, so unlike the
 * Switch/Wii U branch this renders no extra world: it stretches the native frame
 * to fill a true 4:3 area and pillarboxes the rest.
 */
#include "internal.hpp"

#include <sifrpc.h>
#include <kernel.h>
#include <gsKit.h>
#include <dmaKit.h>

#include <pnes/video.hpp>
#include <pnes/interrupts.hpp>
#include <pnes/logger.hpp>
#include "../emu/emu.hpp"

#include <cstring>

static constexpr int VP_W = video::viewport_px();   // 256 -- NES-native, see video.hpp's TARGET_PS2 branch
static constexpr int VP_H = video::viewport_py();   // 240 -- NES-native

static GSGLOBAL  *gsGlobal = nullptr;
static GSTEXTURE  frameTex;

alignas(64) static u32 frameBuf[VP_W * VP_H];

static constexpr float SCREEN_W = 640.0f;
static constexpr float SCREEN_H = 448.0f;

static constexpr float QUAD_H  = SCREEN_H;
static constexpr float QUAD_W  = QUAD_H * 4.0f / 3.0f;
static constexpr float QUAD_X0 = (SCREEN_W - QUAD_W) / 2.0f;
static constexpr float QUAD_Y0 = 0.0f;

// A real kernel wait on vblank rather than a spin, which is what lets the kernel
// switch to the audio thread at all -- see src/ps2/audio.cpp on why that only
// donates time that actually exists.
static s32 vblank_sema = -1;

static s32 vblank_handler(s32) {
    iSignalSema(vblank_sema);
    ExitHandler();
    return 0;
}

static void vblank_pacing_init() {
    ee_sema_t sema{};
    sema.init_count = 0;
    sema.max_count  = 1;
    const s32 sid = CreateSema(&sema);
    if (sid < 0) {
        log("irq::init: CreateSema failed (%d), falling back to gsKit CSR spin", sid);
        return;
    }
    vblank_sema = sid;

    if (AddIntcHandler(INTC_VBLANK_S, vblank_handler, 0) < 0) {
        log("irq::init: AddIntcHandler(INTC_VBLANK_S) failed, falling back to gsKit CSR spin");
        vblank_sema = -1;
        return;
    }
    EnableIntc(INTC_VBLANK_S);
}

static void present_flip() {
    gsKit_queue_exec(gsGlobal);

    if (vblank_sema >= 0) WaitSema(vblank_sema);
    else                  gsKit_vsync_wait();

    gsKit_display_buffer(gsGlobal);
    gsKit_switch_context(gsGlobal);
    gsKit_setactive(gsGlobal);
}

static inline u32 argb_to_rgba(const u32 p) {
    return (p & 0xFF00FF00u) | ((p & 0x00FF0000u) >> 16) | ((p & 0x000000FFu) << 16);
}

static void present_frame() {
    emu::GenerateFrame(frameBuf, VP_W);

    for (int i = 0; i < VP_W * VP_H; i++) frameBuf[i] = argb_to_rgba(frameBuf[i]);

    gsKit_texture_upload(gsGlobal, &frameTex);

    gsKit_clear(gsGlobal, GS_SETREG_RGBAQ(0x00, 0x00, 0x00, 0x00, 0x00));
    gsKit_prim_sprite_texture(gsGlobal, &frameTex,
                               QUAD_X0, QUAD_Y0, 0.0f, 0.0f,
                               QUAD_X0 + QUAD_W, QUAD_Y0 + QUAD_H, static_cast<float>(VP_W), static_cast<float>(VP_H),
                               1, GS_SETREG_RGBAQ(0x80, 0x80, 0x80, 0x80, 0x00));

    present_flip();
}

static void present_blank() {
    gsKit_clear(gsGlobal, GS_SETREG_RGBAQ(0x00, 0x00, 0x00, 0x00, 0x00));
    present_flip();
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
    log("irq::init: entered");
    sceSifInitRpc(0);
    log("irq::init: sceSifInitRpc returned");

    emu::InitMemory(video::vram_bytes());
    log("irq::init: emu::InitMemory returned");

    gsGlobal = gsKit_init_global();
    log("irq::init: gsKit_init_global returned");
    gsGlobal->Mode            = gsKit_check_rom();   // GS_MODE_NTSC or GS_MODE_PAL, from the console's own ROM
    gsGlobal->Interlace       = GS_INTERLACED;
    gsGlobal->Field           = GS_FIELD;
    gsGlobal->Width           = static_cast<int>(SCREEN_W);
    gsGlobal->Height          = static_cast<int>(SCREEN_H);
    gsGlobal->PSM             = GS_PSM_CT24;
    gsGlobal->PSMZ            = GS_PSMZ_16S;
    gsGlobal->DoubleBuffering = GS_SETTING_ON;
    gsGlobal->ZBuffering      = GS_SETTING_OFF;       // flat 2D textured quad; no depth test needed
    gsGlobal->PrimAlphaEnable = GS_SETTING_OFF;

    dmaKit_init(D_CTRL_RELE_OFF, D_CTRL_MFD_OFF, D_CTRL_STS_UNSPEC,
                D_CTRL_STD_OFF, D_CTRL_RCYC_8, 1 << DMA_CHANNEL_GIF);
    dmaKit_chan_init(DMA_CHANNEL_GIF);
    log("irq::init: dmaKit init returned");

    gsKit_init_screen(gsGlobal);
    log("irq::init: gsKit_init_screen returned");
    gsKit_mode_switch(gsGlobal, GS_ONESHOT);
    gsKit_clear(gsGlobal, GS_SETREG_RGBAQ(0x00, 0x00, 0x00, 0x00, 0x00));
    log("irq::init: gsKit_clear returned");

    gsGlobal->FirstFrame = GS_SETTING_OFF;

    vblank_pacing_init();
    log("irq::init: vblank pacing installed (sema=%d)", vblank_sema);

    frameTex.Width  = VP_W;
    frameTex.Height = VP_H;
    frameTex.PSM    = GS_PSM_CT32;
    frameTex.Filter = GS_FILTER_NEAREST;   // crisp NES pixels, same convention every other backend keeps
    frameTex.Mem    = frameBuf;
    frameTex.Vram   = gsKit_vram_alloc(gsGlobal,
                                        gsKit_texture_size_ee(frameTex.Width, frameTex.Height, frameTex.PSM),
                                        GSKIT_ALLOC_USERBUFFER);

    std::memset(frameBuf, 0, sizeof(frameBuf));
    log("irq::init: texture/frameBuf setup done");

    input_init();
    log("irq::init: input_init returned");
}

void irq::post() {
}
