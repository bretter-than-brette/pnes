/**
 * @file video.cpp
 * @brief GX-native presentation backend for GameCube and Wii.
 *
 * The PPU emulation is in src/emu/; this hands its primitives to GX instead of
 * rasterising. CHR becomes a CI4 atlas, paletteRAM becomes RGB5A3 TLUTs, and the
 * nametable and sprites are textured quads. Driven by emu::GenerateBands so
 * scanline IRQ handlers still fire in raster order.
 */
#include "internal.hpp"

#include <pnes/video.hpp>
#include <pnes/interrupts.hpp>
#include "../emu/emu.hpp"
#if defined(OGC_PROFILER)
#include "profiler.hpp"
#endif

#include <ogc/gx.h>
#include <ogc/gu.h>
#include <ogc/pad.h>
#include <ogc/system.h>
#include <ogc/lwp_watchdog.h>   // gettime / ticks_to_microsecs, for video::DetectRefresh
#include <malloc.h>
#include <string.h>
#ifdef TARGET_WII
#include <ogc/conf.h>
#include <wiiuse/wpad.h>
#endif

// Without these a Wii title never answers IOS/STM's power-down request, which
// leaves Dolphin's Stop stuck on "Shutting Down" forever. post() completes it.
#ifdef TARGET_WII
enum ExitIntent { EXIT_RETURN, EXIT_POWEROFF };
static ExitIntent exit_intent = EXIT_RETURN;
static void wii_power_callback() { exit_intent = EXIT_POWEROFF; quit = 1; }
static void wii_remote_power_callback(s32 chan) { exit_intent = EXIT_POWEROFF; quit = 1; }

// HBC-style loaders stamp "STUBHAXX" after their return stub at 0x80001800.
// With it present, returning from post() hands the machine back to the loader;
// SYS_RETURNTOMENU would skip it and drop the user at the System Menu.
static bool loader_stub_present() {
    return memcmp(reinterpret_cast<const void *>(0x80001804), "STUBHAXX", 8) == 0;
}
#endif
static void wii_reset_callback(u32 irq, void *ctx) { quit = 1; }

static void       *xfb[2]  = { nullptr, nullptr };
static u32         fbi     = 0;           // index GX copies into this frame
static GXRModeObj *rmode   = nullptr;     // preferred TV mode (NTSC/PAL/MPAL)
static void       *gp_fifo = nullptr;     // GX command FIFO

static constexpr u32 FIFO_SIZE = 256 * 1024;
static int         efbH, fbW;             // EFB dimensions (scissor / overlay)

u16 ogc_world_tx;

static Mtx44       proj_nes;
#if defined(OGC_PROFILER)
static Mtx44       proj_screen;
#endif

// The GameCube has no aspect field in SRAM and no EDID to probe, so it has to
// be told (video::ogc::SetWidescreen); the Wii reads SYSCONF. Both widths are
// resolved up front because the GameCube can switch between them at runtime.
static u16  world_tx_4_3   = 32;
static u16  world_tx_16_9  = 32;
static bool widescreen_now = false;

static void apply_aspect() {
    ogc_world_tx = widescreen_now ? world_tx_16_9 : world_tx_4_3;
    guOrtho(proj_nes, 0, video::viewport_py(), 0, video::viewport_px(), 0, 300);
    GX_LoadProjectionMtx(proj_nes, GX_ORTHOGRAPHIC);
}

#if defined(TARGET_GC)
void video::ogc::SetWidescreen(const bool on) {
    if (on == widescreen_now) return;
    widescreen_now = on;
    apply_aspect();
}
#endif

static constexpr int ATLAS_TILES_X = 16;
static constexpr int ATLAS_W       = ATLAS_TILES_X * 8;          // 128
static constexpr int ATLAS_H       = (512 / ATLAS_TILES_X) * 8;  // 256
// A ring, because the GP consumes the FIFO asynchronously: re-baking one buffer
// mid-frame would repaint bands already queued from the older bank.
static constexpr int ATLAS_SLOTS = 4;
static u8       *chr_atlas[ATLAS_SLOTS] = {};
static GXTexObj  chrTexObj[ATLAS_SLOTS];
static u32       atlas_gen[ATLAS_SLOTS]  = { ~0u, ~0u, ~0u, ~0u };
static int       atlas_cur  = 0;   // slot currently bound / matching the banks
static int       atlas_ring = 0;   // next slot the ring hands out
static int       atlas_baked_this_frame = 0;   // bakes since the last GX_DrawDone

// 32-byte aligned: GX_LoadTlut DMAs by physical address and reads from the
// aligned-down one otherwise.
alignas(32) static u16 tlut_bg[4][16];
alignas(32) static u16 tlut_spr[4][16];
static GXTlutObj tlutBgObj[4];
static GXTlutObj tlutSprObj[4];

static constexpr u32 nes_rgb[64] = {
    0xFF626262, 0xFF012090, 0xFF1B0CA4, 0xFF3B009E,
    0xFF520080, 0xFF5A004E, 0xFF521610, 0xFF3F2E00,
    0xFF234400, 0xFF0A5200, 0xFF005804, 0xFF004E30,
    0xFF003C62, 0xFF000000, 0xFF000000, 0xFF000000,
    0xFFABABAB, 0xFF1F56D8, 0xFF423CF2, 0xFF6E24EC,
    0xFF9218C4, 0xFF9E1A80, 0xFF933434, 0xFF7A5200,
    0xFF576E00, 0xFF2E8400, 0xFF118E0E, 0xFF008848,
    0xFF007898, 0xFF000000, 0xFF000000, 0xFF000000,
    0xFFFBFBFB, 0xFF6BA4FF, 0xFF8C88FF, 0xFFB87AFF,
    0xFFE072FF, 0xFFF076D0, 0xFFE88C78, 0xFFCCA830,
    0xFFA8C410, 0xFF7EDC24, 0xFF5AE84E, 0xFF48E490,
    0xFF48D4E0, 0xFF4E4E4E, 0xFF000000, 0xFF000000,
    0xFFFBFBFB, 0xFFBED4FF, 0xFFCACAFF, 0xFFDCC4FF,
    0xFFECC0FF, 0xFFF2C0EA, 0xFFF2C8C4, 0xFFE8D4A4,
    0xFFD8E09C, 0xFFC8EC9C, 0xFFBCF0AC, 0xFFB4F0CC,
    0xFFB4E8F0, 0xFFB8B8B8, 0xFF000000, 0xFF000000,
};

// One bucket must hold a whole band's cells, not band_cells/4 -- a single
// palette can cover the screen. Sized for the 104x30 worst case (240p
// double-strike at 16:9); too small overflows into adjacent statics.
struct Cell { u16 atlas; i16 sx; i16 sy; };
static constexpr int MAX_CELLS = 105 * 31;
static Cell  bg_bucket[4][MAX_CELLS];
static int   bg_count[4];

static inline u16 rgb5a3_opaque(const u32 argb) {
    const u32 r = (argb >> 16) & 0xFF, g = (argb >> 8) & 0xFF, b = argb & 0xFF;
    return static_cast<u16>(0x8000 | ((r >> 3) << 10) | ((g >> 3) << 5) | (b >> 3));
}

static void build_chr_atlas(const int slot) {
    constexpr int blocks_per_row = ATLAS_W / 8;   // 16
    u8 *const dst = chr_atlas[slot];
    for (int ti = 0; ti < 512; ti++) {
        const u8 *tile = CHR_ROM + ppu::ResolveTile(static_cast<u16>(ti * 16));
        u8       *blk  = dst + (static_cast<size_t>((ti >> 4)) * blocks_per_row
                                       + (ti & 15)) * 32;
        for (int row = 0; row < 8; row++) {
            const u8 p0 = tile[row];
            const u8 p1 = tile[row + 8];
            for (int col = 0; col < 8; col += 2) {
                const int b0 = 7 - col, b1 = 6 - col;
                const u8 c0 = ((p0 >> b0) & 1) | (((p1 >> b0) & 1) << 1);
                const u8 c1 = ((p0 >> b1) & 1) | (((p1 >> b1) & 1) << 1);
                blk[row * 4 + col / 2] = static_cast<u8>((c0 << 4) | c1);  // hi nibble = left
            }
        }
    }
    DCFlushRange(dst, ATLAS_W * ATLAS_H / 2);
    atlas_gen[slot] = ppu::chrGeneration;
}

static void sync_chr_atlas() {
    if (atlas_gen[atlas_cur] == ppu::chrGeneration) return;

    for (int i = 0; i < ATLAS_SLOTS; i++) {
        if (atlas_gen[i] == ppu::chrGeneration) {   // already baked, contents valid
            atlas_cur = i;
            GX_InvalidateTexAll();
            return;
        }
    }

    // More bank states this frame than the ring has buffers: stall, not corrupt.
    if (atlas_baked_this_frame >= ATLAS_SLOTS) {
        GX_DrawDone();
        atlas_baked_this_frame = 0;
    }

    atlas_cur  = atlas_ring;
    atlas_ring = (atlas_ring + 1) % ATLAS_SLOTS;
    atlas_baked_this_frame++;
    build_chr_atlas(atlas_cur);
    GX_InvalidateTexAll();
}

static inline void load_chr_tex() {
    GX_InitTexObjTlut(&chrTexObj[atlas_cur], GX_TLUT0);
    GX_LoadTexObj(&chrTexObj[atlas_cur], GX_TEXMAP0);
}

static void build_tluts() {
    for (int p = 0; p < 4; p++) {
        tlut_bg[p][0]  = 0x0000;
        tlut_spr[p][0] = 0x0000;
        for (int c = 1; c < 4; c++) {
            tlut_bg[p][c]  = rgb5a3_opaque(nes_rgb[paletteRAM[p * 4 + c]        & 0x3F]);
            tlut_spr[p][c] = rgb5a3_opaque(nes_rgb[paletteRAM[0x10 + p * 4 + c] & 0x3F]);
        }
        DCFlushRange(tlut_bg[p],  sizeof tlut_bg[p]);
        DCFlushRange(tlut_spr[p], sizeof tlut_spr[p]);
        GX_InitTlutObj(&tlutBgObj[p],  tlut_bg[p],  GX_TL_RGB5A3, 16);
        GX_InitTlutObj(&tlutSprObj[p], tlut_spr[p], GX_TL_RGB5A3, 16);
    }

    const u32 bd = nes_rgb[paletteRAM[0] & 0x3F];
    const GXColor c = { static_cast<u8>(bd >> 16), static_cast<u8>(bd >> 8),
                        static_cast<u8>(bd), 0xFF };
    GX_SetCopyClear(c, GX_MAX_Z24);
}

static inline void quad(const int ax, const int ay, const int sx, const int sy,
                        const bool flipH, const bool flipV) {
    const f32 u0 = static_cast<f32>(ax)     / ATLAS_W;
    const f32 u1 = static_cast<f32>(ax + 8) / ATLAS_W;
    const f32 v0 = static_cast<f32>(ay)     / ATLAS_H;
    const f32 v1 = static_cast<f32>(ay + 8) / ATLAS_H;
    const f32 ul = flipH ? u1 : u0, ur = flipH ? u0 : u1;
    const f32 vt = flipV ? v1 : v0, vb = flipV ? v0 : v1;
    const f32 x0 = static_cast<f32>(sx), x1 = static_cast<f32>(sx + 8);
    const f32 y0 = static_cast<f32>(sy), y1 = static_cast<f32>(sy + 8);
    GX_Position2f32(x0, y0); GX_TexCoord2f32(ul, vt);
    GX_Position2f32(x1, y0); GX_TexCoord2f32(ur, vt);
    GX_Position2f32(x1, y1); GX_TexCoord2f32(ur, vb);
    GX_Position2f32(x0, y1); GX_TexCoord2f32(ul, vb);
}

static void draw_bg_band(const int y0, const int y1, const u16 xs, const u16 ys) {
    sync_chr_atlas();

    const int vpw      = video::viewport_px();
    const emu::NtGeometry geo = emu::ComputeNtGeometry();
    const int world_w  = geo.worldW;
    const int world_h  = geo.worldH;
    const int atlas0   = (ppu::PPUCTRL & ppu::ctrl::BG_ADDR) ? 256 : 0;

    bg_count[0] = bg_count[1] = bg_count[2] = bg_count[3] = 0;

    const int sx0 = -(static_cast<int>(xs) & 7);
    const int sy0 = y0 - (static_cast<int>(ys) & 7);

    for (int syt = sy0; syt < y1; syt += 8) {
        const int world_y = static_cast<int>(ys) + (syt - y0);
        const int wym       = ((world_y % world_h) + world_h) % world_h;
        const int trow      = wym / 8;
        const int local_row = trow % geo.tileH;
        const int nt_row    = trow / geo.tileH;
        const int at_roff   = (local_row >> 2) * geo.atW;
        const int at_rbits  = ((local_row >> 1) & 1) * 4;
        const int row32     = local_row * geo.tileW;

        for (int sxt = sx0; sxt < vpw; sxt += 8) {
            const int world_x = static_cast<int>(xs) + sxt;
            const int wxm       = ((world_x % world_w) + world_w) % world_w;
            const int tcol      = wxm / 8;
            const int local_col = tcol % geo.tileW;
            const int nt_col    = tcol / geo.tileW;
            const int nt_off    = (nt_col + nt_row * geo.gridW) * geo.pageBytes;

            const u8 tile_id = ppu::ReadNametable(static_cast<u16>(nt_off + row32 + local_col));
            const u8 attr    = ppu::ReadNametable(static_cast<u16>(nt_off + geo.ntBytes + at_roff + (local_col >> 2)));
            const int pal    = (attr >> (((local_col >> 1) & 1) * 2 + at_rbits)) & 3;

            const int ti = atlas0 + tile_id;
            Cell &cell = bg_bucket[pal][bg_count[pal]++];
            cell.atlas = static_cast<u16>(ti);
            cell.sx    = static_cast<i16>(sxt);
            cell.sy    = static_cast<i16>(syt);
        }
    }

    const int e0 = y0 * efbH / 240;
    const int e1 = y1 * efbH / 240;
    GX_SetScissor(0, e0, fbW, e1 - e0);

    for (int p = 0; p < 4; p++) {
        if (!bg_count[p]) continue;
        GX_LoadTlut(&tlutBgObj[p], GX_TLUT0);
        load_chr_tex();
        GX_Begin(GX_QUADS, GX_VTXFMT0, bg_count[p] * 4);
        for (int i = 0; i < bg_count[p]; i++) {
            const Cell &c = bg_bucket[p][i];
            quad((c.atlas & 15) * 8, (c.atlas >> 4) * 8, c.sx, c.sy, false, false);
        }
        GX_End();
    }
}

static void draw_sprites(const bool behind) {
    sync_chr_atlas();

    const oam::sprite_t *oam = emu::OamShadow();
    const int  vpw    = video::viewport_px();
    const bool tall   = ppu::PPUCTRL & ppu::ctrl::SPRITE_SIZE;
    const int  atlas0 = (ppu::PPUCTRL & ppu::ctrl::SPRITE_ADDR) ? 256 : 0;
    const int  quads_per_sprite = tall ? 2 : 1;

    GX_SetScissor(0, 0, fbW, efbH);

    for (int p = 0; p < 4; p++) {
        int n = 0;
        for (int s = 0; s < OAM_SPRITES; s++) {
            const oam::sprite_t &o = oam[s];
            if ((o.attributes & 0x03) != p)            continue;
            if (((o.attributes & 0x20) != 0) != behind) continue;
            const int sx = static_cast<int>(o.x);
            const int sy = static_cast<int>(o.y) + 1;
            if (sx >= vpw || sy >= 240) continue;
            n++;
        }
        if (!n) continue;

        GX_LoadTlut(&tlutSprObj[p], GX_TLUT0);
        load_chr_tex();
        GX_Begin(GX_QUADS, GX_VTXFMT0, n * 4 * quads_per_sprite);
        for (int s = 0; s < OAM_SPRITES; s++) {
            const oam::sprite_t &o = oam[s];
            if ((o.attributes & 0x03) != p)            continue;
            if (((o.attributes & 0x20) != 0) != behind) continue;
            const int sx = static_cast<int>(o.x);
            const int sy = static_cast<int>(o.y) + 1;
            if (sx >= vpw || sy >= 240) continue;
            const bool flipH = (o.attributes & 0x40) != 0;
            const bool flipV = (o.attributes & 0x80) != 0;
            if (!tall) {
                const int ti = atlas0 + o.tile;
                quad((ti & 15) * 8, (ti >> 4) * 8, sx, sy, flipH, flipV);
                continue;
            }
            const int pair = (o.tile & 1) * 256 + (o.tile & 0xFE);
            const int top_ti = pair + (flipV ? 1 : 0);
            const int bot_ti = pair + (flipV ? 0 : 1);
            quad((top_ti & 15) * 8, (top_ti >> 4) * 8, sx, sy,     flipH, flipV);
            quad((bot_ti & 15) * 8, (bot_ti >> 4) * 8, sx, sy + 8, flipH, flipV);
        }
        GX_End();
    }
}

static void band_noop(int, int, u16, u16) {}


static inline void gx_mode_tiles() {
    GX_SetNumTexGens(1);
    GX_SetNumChans(1);
    GX_SetTevOrder(GX_TEVSTAGE0, GX_TEXCOORD0, GX_TEXMAP0, GX_COLORNULL);
    GX_SetTevOp(GX_TEVSTAGE0, GX_REPLACE);
    // Discards CI index 0, which is how NES transparency is reproduced.
    GX_SetAlphaCompare(GX_GREATER, 0, GX_AOP_AND, GX_GREATER, 0);
    GX_ClearVtxDesc();
    GX_SetVtxDesc(GX_VA_POS,  GX_DIRECT);
    GX_SetVtxDesc(GX_VA_TEX0, GX_DIRECT);
}

#if defined(OGC_PROFILER)
static inline void gx_mode_flat() {
    GX_SetNumTexGens(0);
    GX_SetNumChans(1);
    GX_SetChanCtrl(GX_COLOR0A0, GX_DISABLE, GX_SRC_REG, GX_SRC_VTX,
                   0, GX_DF_NONE, GX_AF_NONE);
    GX_SetTevOrder(GX_TEVSTAGE0, GX_TEXCOORDNULL, GX_TEXMAP_NULL, GX_COLOR0A0);
    GX_SetTevOp(GX_TEVSTAGE0, GX_PASSCLR);
    GX_SetAlphaCompare(GX_ALWAYS, 0, GX_AOP_AND, GX_ALWAYS, 0);
    GX_ClearVtxDesc();
    GX_SetVtxDesc(GX_VA_POS,  GX_DIRECT);
    GX_SetVtxDesc(GX_VA_CLR0, GX_DIRECT);
}

static constexpr int   PROF_ROWS = 12;
static constexpr float PX        = 2.0f;    // overlay "pixel" size, ortho units

static const u8 kFont[16][5] = {
    {0b111,0b101,0b101,0b101,0b111}, // 0
    {0b010,0b110,0b010,0b010,0b111}, // 1
    {0b111,0b001,0b111,0b100,0b111}, // 2
    {0b111,0b001,0b111,0b001,0b111}, // 3
    {0b101,0b101,0b111,0b001,0b001}, // 4
    {0b111,0b100,0b111,0b001,0b111}, // 5
    {0b111,0b100,0b111,0b101,0b111}, // 6
    {0b111,0b001,0b010,0b010,0b010}, // 7
    {0b111,0b101,0b111,0b101,0b111}, // 8
    {0b111,0b101,0b111,0b001,0b111}, // 9
    {0b111,0b101,0b111,0b101,0b101}, // A
    {0b110,0b101,0b110,0b101,0b110}, // B
    {0b111,0b100,0b100,0b100,0b111}, // C
    {0b110,0b101,0b101,0b101,0b110}, // D
    {0b111,0b100,0b111,0b100,0b111}, // E
    {0b111,0b100,0b111,0b100,0b100}, // F
};

static inline void flat_rect(float x0, float y0, float x1, float y1,
                             u8 r, u8 g, u8 b) {
    GX_Begin(GX_QUADS, GX_VTXFMT0, 4);
        GX_Position2f32(x0, y0); GX_Color4u8(r, g, b, 0xFF);
        GX_Position2f32(x1, y0); GX_Color4u8(r, g, b, 0xFF);
        GX_Position2f32(x1, y1); GX_Color4u8(r, g, b, 0xFF);
        GX_Position2f32(x0, y1); GX_Color4u8(r, g, b, 0xFF);
    GX_End();
}

static float draw_glyph(int nibble, float gx, float gy, u8 r, u8 g, u8 b) {
    const u8* rows = kFont[nibble & 0xF];
    for (int ry = 0; ry < 5; ry++)
        for (int cx = 0; cx < 3; cx++)
            if (rows[ry] & (1 << (2 - cx))) {
                const float x0 = gx + cx * PX, y0 = gy + ry * PX;
                flat_rect(x0, y0, x0 + PX, y0 + PX, r, g, b);
            }
    return 4 * PX;
}

static void draw_overlay() {
    ProfEntry top[PROF_ROWS];
    ProfStats st;
    const int n = prof_top(top, PROF_ROWS, &st);

    const float ox   = 8.0f, oy = 8.0f;
    const float rowh = 7 * PX;               // 5px glyph + 2px gap
    const float hexw = 8 * (4 * PX);         // 8 hex chars
    const float barx = ox + hexw + 4 * PX;
    const float barw = 160.0f;
    const float h    = rowh * (n > 0 ? n : 1);

    flat_rect(ox - 4, oy - 4, barx + barw + 4, oy + h + 4, 16, 16, 16);

    const u32 hot = (n > 0) ? top[0].count : 1;
    for (int i = 0; i < n; i++) {
        const float ry = oy + i * rowh;
        float gx = ox;
        for (int d = 0; d < 8; d++)
            gx += draw_glyph((top[i].addr >> (28 - 4 * d)) & 0xF, gx, ry, 0, 255, 0);
        const float bw = (float)top[i].count * barw / (float)hot;
        flat_rect(barx, ry, barx + bw, ry + 5 * PX, 255, 128, 0);
    }
}
#endif // OGC_PROFILER


static void render_gx() {
    atlas_baked_this_frame = 0;

    GX_LoadProjectionMtx(proj_nes, GX_ORTHOGRAPHIC);

    const bool bg  = ppu::PPUMASK & ppu::mask::BG;
    const bool spr = ppu::PPUMASK & ppu::mask::SPRITE;

    gx_mode_tiles();

    if (bg || spr) {
        // Not resolved here: a scanline IRQ can switch CHR part-way through
        // GenerateBands, so sync_chr_atlas() bakes per band instead.
        build_tluts();
        GX_InvalidateTexAll();

        if (spr) draw_sprites(true);
        emu::GenerateBands(bg ? draw_bg_band : band_noop);  // also runs game logic
        if (spr) draw_sprites(false);
    } else {
        emu::GenerateBands(band_noop);
    }
}

namespace video {

void WaitForPresent() {
#if defined(OGC_PROFILER)
    static u32 prevHeld = 0;
    PAD_ScanPads();
    const u32 held = PAD_ButtonsHeld(0);
    if ((held & PAD_TRIGGER_L) && !(prevHeld & PAD_TRIGGER_L)) prof_reset();
    prevHeld = held;
#endif

    render_gx();

#if defined(OGC_PROFILER)
    GX_LoadProjectionMtx(proj_screen, GX_ORTHOGRAPHIC);
    GX_SetScissor(0, 0, fbW, efbH);
    gx_mode_flat();
    draw_overlay();
#endif

    GX_DrawDone();
    GX_CopyDisp(xfb[fbi], GX_TRUE);
    GX_Flush();

    VIDEO_SetNextFramebuffer(xfb[fbi]);
    VIDEO_Flush();
    VIDEO_WaitVSync();
    fbi ^= 1;

    irq::irqPendingValid = false;
    nmi_vector();
}

}   // namespace video

void irq::init() {
    VIDEO_Init();
    rmode = VIDEO_GetPreferredMode(nullptr);

    // Whatever mode the console prefers, in both directions: demo::Framerated
    // scales off DetectRefresh below, and forcing NTSC on a 50Hz-only PAL TV
    // would need an EURGB60 guard that was never written.

    xfb[0] = MEM_K0_TO_K1(SYS_AllocateFramebuffer(rmode));
    xfb[1] = MEM_K0_TO_K1(SYS_AllocateFramebuffer(rmode));
    VIDEO_Configure(rmode);
    VIDEO_SetNextFramebuffer(xfb[fbi]);
    VIDEO_SetBlack(FALSE);
    VIDEO_Flush();
    VIDEO_WaitVSync();
    if (rmode->viTVMode & VI_NON_INTERLACE) VIDEO_WaitVSync();

    gp_fifo = memalign(32, FIFO_SIZE);
    memset(gp_fifo, 0, FIFO_SIZE);
    GX_Init(gp_fifo, FIFO_SIZE);

    GXColor black = {0, 0, 0, 0xff};
    GX_SetCopyClear(black, GX_MAX_Z24);

    GX_SetViewport(0, 0, rmode->fbWidth, rmode->efbHeight, 0, 1);
    GX_SetDispCopyYScale(static_cast<f32>(rmode->xfbHeight) / static_cast<f32>(rmode->efbHeight));
    GX_SetScissor(0, 0, rmode->fbWidth, rmode->efbHeight);
    GX_SetDispCopySrc(0, 0, rmode->fbWidth, rmode->efbHeight);
    GX_SetDispCopyDst(rmode->fbWidth, rmode->xfbHeight);
    GX_SetCopyFilter(rmode->aa, rmode->sample_pattern, GX_TRUE, rmode->vfilter);
    GX_SetFieldMode(rmode->field_rendering,
                    ((rmode->viHeight == 2 * rmode->xfbHeight) ? GX_ENABLE : GX_DISABLE));

    if (rmode->aa) GX_SetPixelFmt(GX_PF_RGB565_Z16, GX_ZC_LINEAR);
    else           GX_SetPixelFmt(GX_PF_RGB8_Z24,   GX_ZC_LINEAR);

    GX_SetCullMode(GX_CULL_NONE);
    GX_CopyDisp(xfb[fbi], GX_TRUE);
    GX_SetDispCopyGamma(GX_GM_1_0);

    GX_SetZMode(GX_FALSE, GX_ALWAYS, GX_FALSE);
    GX_SetColorUpdate(GX_TRUE);
    GX_SetBlendMode(GX_BM_NONE, GX_BL_ONE, GX_BL_ZERO, GX_LO_CLEAR);
    GX_SetZCompLoc(GX_TRUE);

    GX_ClearVtxDesc();
    GX_SetVtxDesc(GX_VA_POS,  GX_DIRECT);
    GX_SetVtxDesc(GX_VA_TEX0, GX_DIRECT);
    GX_SetVtxAttrFmt(GX_VTXFMT0, GX_VA_POS,  GX_POS_XY,   GX_F32,   0);
    GX_SetVtxAttrFmt(GX_VTXFMT0, GX_VA_TEX0, GX_TEX_ST,   GX_F32,   0);
    GX_SetVtxAttrFmt(GX_VTXFMT0, GX_VA_CLR0, GX_CLR_RGBA, GX_RGBA8, 0);  // overlay

    GX_SetNumChans(1);
    GX_SetNumTexGens(1);
    GX_SetTexCoordGen(GX_TEXCOORD0, GX_TG_MTX2x4, GX_TG_TEX0, GX_IDENTITY);
    GX_SetTevOrder(GX_TEVSTAGE0, GX_TEXCOORD0, GX_TEXMAP0, GX_COLORNULL);
    GX_SetTevOp(GX_TEVSTAGE0, GX_REPLACE);

    // A 16:9 framebuffer pixel is 4/3 as wide as it is tall, so the viewport has
    // to cover fbWidth * 4/3 square pixels. Aligned to the 32px attribute grid.
    const u32 ogc_scale = rmode->efbHeight >= 240 ? rmode->efbHeight / 240 : 1;
    world_tx_4_3  = static_cast<u16>(((rmode->fbWidth / ogc_scale) >> 3) & ~3u);
    world_tx_16_9 = static_cast<u16>(((((rmode->fbWidth * 4u) / 3u) / ogc_scale) >> 3) & ~3u);

#if defined(TARGET_WII)
    widescreen_now = CONF_GetAspectRatio() == CONF_ASPECT_16_9;
#else
    widescreen_now = false;
#endif

    apply_aspect();
#if defined(OGC_PROFILER)
    guOrtho(proj_screen, 0, rmode->efbHeight - 1, 0, rmode->fbWidth - 1, 0, 300);
#endif

    Mtx mv;
    guMtxIdentity(mv);
    GX_LoadPosMtxImm(mv, GX_PNMTX0);

    efbH = rmode->efbHeight;
    fbW  = rmode->fbWidth;

    // Sized for the wider aspect so SetWidescreen never reallocates:
    // emu::InitMemory is a plain malloc, and every pointer into VideoRAM would
    // move under the game.
    const u16 tx_now = ogc_world_tx;
    ogc_world_tx = world_tx_16_9 > world_tx_4_3 ? world_tx_16_9 : world_tx_4_3;
    const unsigned vram = video::vram_bytes();
    ogc_world_tx = tx_now;
    emu::InitMemory(vram);

    for (int i = 0; i < ATLAS_SLOTS; i++) {
        chr_atlas[i] = static_cast<u8 *>(memalign(32, ATLAS_W * ATLAS_H / 2));
        GX_InitTexObjCI(&chrTexObj[i], chr_atlas[i], ATLAS_W, ATLAS_H, GX_TF_CI4,
                        GX_CLAMP, GX_CLAMP, GX_FALSE, GX_TLUT0);
        GX_InitTexObjFilterMode(&chrTexObj[i], GX_NEAR, GX_NEAR);
    }
    build_chr_atlas(0);
    atlas_cur  = 0;
    atlas_ring = 1;

    ogc_input_init();

    SYS_SetResetCallback(wii_reset_callback);
#ifdef TARGET_WII
    SYS_SetPowerCallback(wii_power_callback);
    WPAD_SetPowerButtonCallback(wii_remote_power_callback);
#endif

#if defined(OGC_PROFILER)
    prof_init();
#endif
}

void irq::post() {
    for (u8 *a : chr_atlas) free(a);
    free(gp_fifo);

#ifdef TARGET_WII
    if (exit_intent == EXIT_POWEROFF) {
        SYS_ResetSystem(SYS_POWEROFF, 0, 0);
    } else if (!loader_stub_present()) {
        // Installed channel: no loader stub to fall back into.
        SYS_ResetSystem(SYS_RETURNTOMENU, 0, 0);
    }
#endif
}

/**
 * @brief Strong override of src/all/video.cpp's weak default: times the vsync
 *        cadence rather than reading the mode's region field.
 *
 * EURGB60 means one European build can come up at either rate, so this cannot
 * be a build-time fact. Eight intervals, since WaitForVSync's first return is
 * an arbitrary part of a frame away. Must run after irq::init() configures VI.
 */
bool video::DetectRefresh() {
    constexpr u32 kIntervals = 8;

    VIDEO_WaitVSync();                     // Align to a retrace before timing.
    const u64 start = gettime();
    for (u32 i = 0; i < kIntervals; i++) VIDEO_WaitVSync();
    const u64 end = gettime();

    const u32 perFrameUs =
        static_cast<u32>(ticks_to_microsecs(end - start) / kIntervals);

    // Between 60Hz's 16667us and 50Hz's 20000us.
    return perFrameUs > 18300;
}
