/**
 * @file video.cpp
 * @brief PS3 presentation backend: asks the shared software PPU for a 256x240
 *        frame, integer-scale-blits it into an RSX-mapped display buffer, flips.
 *
 * None of the RSX 3D pipeline is used. The obvious present is the PS2's -- upload
 * a texture, draw one quad -- but on the RSX that needs a vertex and fragment
 * program compiled by cgcomp, a gcmSurface target, a vertex array and the upload
 * path: a lot of first-pass surface area for an image the PPU can place itself.
 * So this writes pixels directly, as the PSP and Switch backends do. Nothing
 * here forecloses the textured quad later.
 *
 * The output size is whatever video mode the console is in, so the scale is the
 * largest integer that fits both axes -- 2x at 480p, 3x at 720p (240 divides 720
 * exactly), 4x at 1080p -- centred, with every destination pixel exactly one
 * source pixel. The margin is pillar/letterboxing.
 */
#include "internal.hpp"

#include <pnes/video.hpp>
#include <pnes/interrupts.hpp>
#include "../emu/emu.hpp"

#include <cstring>
#include <malloc.h>

#include "prxptr_clang_shim.hpp"

#include <rsx/rsx.h>
#include <sysutil/video.h>
#include <sysutil/sysutil.h>
#include <sys/systime.h>

static constexpr int VP_W = video::viewport_px();   // 256 -- NES-native
static constexpr int VP_H = video::viewport_py();   // 240 -- NES-native

static_assert(VP_W == 256 && VP_H == 240,
              "PS3 backend assumes the NES-native viewport -- see video.hpp's TARGET_PS3 branch");

static constexpr u32 CB_SIZE   = 0x100000;   // 1MB RSX command buffer
static constexpr u32 HOST_SIZE = 0x400000;   // 4MB host (main memory) IO area

static constexpr int NUM_BUFFERS = 2;        // double buffered

static gcmContextData *context = nullptr;
static u32 *display_buf[NUM_BUFFERS] = {};
static u32  display_off[NUM_BUFFERS] = {};
static int  back_buf = 0;                    // index being drawn into this frame

static int screen_w = 0;                     // current video mode, in pixels
static int screen_h = 0;
static int stride_px = 0;                    // display buffer pitch, in u32s
static int scale = 1;                        // integer upscale factor
static int origin_x = 0;                     // top-left of the scaled image
static int origin_y = 0;

static bool init_ok = false;

static u32 staging[VP_W * VP_H];

static void sysutil_callback(u64 status, u64 /*param*/, void * /*usrdata*/) {
    if (status == SYSUTIL_EXIT_GAME) quit = 1;
}

void ps3_setup_exit_callback() {
    sysUtilRegisterCallback(SYSUTIL_EVENT_SLOT0, sysutil_callback, nullptr);
}

void ps3_poll_sysutil() {
    sysUtilCheckCallback();
}

static void queue_flip(const int buffer) {
    gcmSetFlip(context, static_cast<u8>(buffer));
    rsxFlushBuffer(context);
    gcmSetWaitFlip(context);
}

// A short spin rather than a blocking wait: the flip callback is the only signal
// PSL1GHT gives, and a frame is already paced by the core's own work.
static constexpr int FLIP_WAIT_US    = 200;
static constexpr int FLIP_WAIT_LIMIT = 250000 / FLIP_WAIT_US;

static void wait_flip() {
    for (int spins = 0; gcmGetFlipStatus() != 0; spins++) {
        if (spins >= FLIP_WAIT_LIMIT) {
            static bool warned = false;
            if (!warned) {
                warned = true;
                ps3_trace("wait_flip: timed out -- flip never retired. "
                          "gcmGetFlipStatus polarity may be inverted "
                          "(see wait_flip in src/ps3/video.cpp)");
            }
            break;
        }
        sysUsleep(FLIP_WAIT_US);
    }
    gcmResetFlipStatus();
}

static void blit_scaled() {
    u32 *const dst_base = display_buf[back_buf]
                        + static_cast<size_t>(origin_y) * stride_px + origin_x;
    const int row_px = VP_W * scale;

    for (int sy = 0; sy < VP_H; sy++) {
        const u32 *src = staging + static_cast<size_t>(sy) * VP_W;
        u32 *row = dst_base + static_cast<size_t>(sy) * scale * stride_px;

        u32 *out = row;
        for (int sx = 0; sx < VP_W; sx++) {
            const u32 p = src[sx];
            for (int k = 0; k < scale; k++) *out++ = p;
        }
        for (int k = 1; k < scale; k++)
            std::memcpy(row + static_cast<size_t>(k) * stride_px, row,
                        static_cast<size_t>(row_px) * sizeof(u32));
    }
}

static void present_frame() {
    emu::GenerateFrame(staging, VP_W);
    blit_scaled();
    queue_flip(back_buf);
    wait_flip();
    back_buf = (back_buf + 1) % NUM_BUFFERS;
}

static void present_blank() {
    u32 *const dst_base = display_buf[back_buf]
                        + static_cast<size_t>(origin_y) * stride_px + origin_x;
    for (int y = 0; y < VP_H * scale; y++)
        std::memset(dst_base + static_cast<size_t>(y) * stride_px, 0,
                    static_cast<size_t>(VP_W) * scale * sizeof(u32));
    queue_flip(back_buf);
    wait_flip();
    back_buf = (back_buf + 1) % NUM_BUFFERS;
}

namespace video {

void WaitForPresent() {
    if (quit) return;
    if (!init_ok) {
        static bool warned = false;
        if (!warned) {
            warned = true;
            ps3_trace("WaitForPresent: irq::init() did not complete -- "
                      "presenting nothing (see the init trace above)");
        }
        ps3_poll_sysutil();
        irq::irqPendingValid = false;
        nmi_vector();
        return;
    }

    if (ppu::PPUMASK & (ppu::mask::BG | ppu::mask::SPRITE)) {
        present_frame();
    } else {
        present_blank();
    }

    ps3_poll_sysutil();

    irq::irqPendingValid = false;
    nmi_vector();
}

}   // namespace video

void irq::init() {
    emu::InitMemory(video::vram_bytes());

    ps3_trace("irq::init: entered");

    void *host_addr = memalign(HOST_SIZE, HOST_SIZE);
    if (host_addr == nullptr) {
        ps3_trace("irq::init: memalign for the RSX IO area FAILED");
        return;
    }
    if (rsxInit(&context, CB_SIZE, HOST_SIZE, host_addr) != 0 || context == nullptr) {
        ps3_trace("irq::init: rsxInit FAILED");
        return;
    }
    ps3_trace("irq::init: rsxInit ok");

    videoState state;
    if (videoGetState(0, 0, &state) != 0) {
        ps3_trace("irq::init: videoGetState FAILED");
        return;
    }
    if (state.state != 0) {
        ps3_trace("irq::init: video output is disabled (state.state != 0)");
        return;
    }
    videoResolution res;
    if (videoGetResolution(state.displayMode.resolution, &res) != 0) {
        ps3_trace("irq::init: videoGetResolution FAILED");
        return;
    }

    screen_w  = res.width;
    screen_h  = res.height;
    stride_px = screen_w;                       // tightly packed, 4 bytes/pixel

    videoConfiguration vconfig;
    std::memset(&vconfig, 0, sizeof(vconfig));
    vconfig.resolution = state.displayMode.resolution;
    vconfig.format     = VIDEO_BUFFER_FORMAT_XRGB;
    vconfig.aspect     = state.displayMode.aspect;
    vconfig.pitch      = static_cast<u32>(stride_px) * sizeof(u32);
    if (videoConfigure(0, &vconfig, nullptr, 0) != 0) {
        ps3_trace("irq::init: videoConfigure FAILED");
        return;
    }

    for (int tries = 0; tries < 500; tries++) {
        if (videoGetState(0, 0, &state) == 0 && state.state == 0) break;
        sysUsleep(1000);
    }
    if (state.state != 0) {
        ps3_trace("irq::init: video output never re-enabled after "
                  "videoConfigure -- continuing anyway");
    }
    ps3_trace("irq::init: video mode configured");

    gcmSetFlipMode(GCM_FLIP_VSYNC);

    const int fit_x = screen_w / VP_W;
    const int fit_y = screen_h / VP_H;
    scale = fit_x < fit_y ? fit_x : fit_y;
    if (scale < 1) scale = 1;

    origin_x = (screen_w - VP_W * scale) / 2;
    origin_y = (screen_h - VP_H * scale) / 2;
    if (origin_x < 0) origin_x = 0;
    if (origin_y < 0) origin_y = 0;

    const size_t buf_bytes = static_cast<size_t>(stride_px) * screen_h * sizeof(u32);
    for (int i = 0; i < NUM_BUFFERS; i++) {
        display_buf[i] = static_cast<u32 *>(rsxMemalign(64, buf_bytes));
        if (display_buf[i] == nullptr) {
            ps3_trace("irq::init: rsxMemalign for a display buffer FAILED");
            return;
        }
        rsxAddressToOffset(display_buf[i], &display_off[i]);
        gcmSetDisplayBuffer(static_cast<u8>(i), display_off[i], vconfig.pitch,
                            static_cast<u32>(screen_w), static_cast<u32>(screen_h));
        std::memset(display_buf[i], 0, buf_bytes);
    }

    gcmResetFlipStatus();
    queue_flip(0);
    wait_flip();
    back_buf = 1;
    ps3_trace("irq::init: first flip retired");

    ps3_setup_exit_callback();
    input_init();

    init_ok = true;
    ps3_trace("irq::init: complete");
}

void irq::post() {
    input_exit();
}

/// Times the flip cadence rather than reading the mode, so a 50Hz European
/// console is correct without enumerating which modes are which.
bool video::DetectRefresh() {
    videoState state;
    if (videoGetState(0, 0, &state) != 0) {
        ps3_trace("DetectRefresh: videoGetState FAILED -- assuming 60Hz");
        return false;
    }
    if (state.state != VIDEO_STATE_ENABLED) {
        ps3_trace("DetectRefresh: video output not enabled -- assuming 60Hz");
        return false;
    }

    const bool is50 = (state.displayMode.refreshRates & VIDEO_REFRESH_50HZ) != 0;
    ps3_trace(is50 ? "DetectRefresh: 50Hz" : "DetectRefresh: 60Hz");
    return is50;
}
