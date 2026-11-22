/**
 * @file video.cpp
 * @brief GPU-native (PICA200) presentation backend for the 3DS, via citro2d.
 *
 * The PICA200 has no paletted texture format, so the GX CI4+TLUT path cannot
 * port: the palette is BAKED into the CHR atlas instead, giving four background
 * and four sprite atlases of RGBA8 tiles. Colour index 0 bakes to alpha 0, so
 * ordinary blending reproduces NES transparency.
 *
 * Both panels are presented -- the viewport is the two screens stacked, and
 * video::panel_rect says where each sits in it -- so each band and sprite is
 * drawn once per panel it overlaps, clipped into that panel's own target. Note
 * that row 240 is a real visible row now: the first row of the bottom screen.
 *
 * Driven by emu::GenerateBands, with painter order reproducing NES priority per
 * panel. Mid-frame CHR banking is why the frame is walked in two passes: an
 * atlas upload is a GX display transfer and may not be issued between
 * C3D_FrameBegin and C3D_FrameEnd, so WaitForPresent first PLANS the frame
 * outside it -- running the game logic, recording each band's scroll and the
 * atlas set its scanlines resolve to -- then bakes every set the frame needs
 * before drawing any of it.
 */
#include "internal.hpp"

#include <3ds.h>
#include <citro2d.h>
#include <citro3d.h>

#include <pnes/video.hpp>
#include <pnes/interrupts.hpp>
#include "../emu/emu.hpp"
#include <cstring>

// 512 tiles, 16 per row: 128x256 RGBA8 texels per atlas, four background and
// four sprite sets.

static constexpr int ATLAS_TILES_X = 16;
static constexpr int ATLAS_W       = ATLAS_TILES_X * 8;          // 128
static constexpr int ATLAS_H       = (512 / ATLAS_TILES_X) * 8;  // 256

static C3D_RenderTarget *panel_target[2];

struct AtlasSet {
    C3D_Tex tex[8];          // [0..3] background palettes, [4..7] sprite palettes
    bool    inited[8];       // texture memory allocated
    bool    baked[8];        // contents match `bases` + the live palette
    u32     bases[512];      // ::ppu::ResolveTile(ti * 16) for every tile slot
    bool    valid;           // `bases` holds a real state
    u32     used;            // frame stamp of last use, for the LRU eviction
};
static constexpr int CHR_SETS = 3;
static AtlasSet  chr_set[CHR_SETS];
static u32       frame_stamp;            // ++ per frame, the LRU clock
static u32       cur_bases[512];         // scratch for snapshot_bases
static u32      *atlas_scratch;          // linear ARGB->tex staging (ATLAS_W*ATLAS_H)
static u8        palette_cache[32];      // last paletteRAM seen
static bool      palette_valid = false;

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

// Gathered per band and bucketed, so each atlas is one texture bind and one
// batch. A single band can cover the whole screen with every cell in one
// bucket, so each bucket needs a full band's worth of capacity.
struct Cell { u16 tile; i16 sx; i16 vy0; u8 c0; u8 w; u8 r0; u8 h; };
static constexpr int MAX_CELLS = (video::panel_tx(0) + 1) * (video::panel_ty(0) + 1);
static Cell  bg_bucket[4][MAX_CELLS];
static int   bg_count[4];

static inline u32 to_tex(const u32 argb) {
    return (argb << 8) | (argb >> 24);
}

static void bake_atlas(u32 *buf, const u32 *bases, const int p, const bool spr) {
    const int base = (spr ? 0x10 : 0) + p * 4;
    for (int ti = 0; ti < 512; ti++) {
        const u8 *tile = CHR_ROM + bases[ti];
        const int ax = (ti & 15) * 8;
        const int ay = (ti >> 4) * 8;
        for (int ry = 0; ry < 8; ry++) {
            const u8 p0 = tile[ry];
            const u8 p1 = tile[ry + 8];
            u32 *dst = buf + (ay + ry) * ATLAS_W + ax;
            for (int rx = 0; rx < 8; rx++) {
                const int bit = 7 - rx;
                const u8  c   = ((p0 >> bit) & 1) | (((p1 >> bit) & 1) << 1);
                dst[rx] = c ? to_tex(nes_rgb[paletteRAM[base + c] & 0x3F]) : 0u;
            }
        }
    }
}

static void upload_atlas(u32 *buf, C3D_Tex *tex) {
    GSPGPU_FlushDataCache(buf, ATLAS_W * ATLAS_H * sizeof(u32));
    C3D_SyncDisplayTransfer(
        buf,             GX_BUFFER_DIM(ATLAS_W, ATLAS_H),
        (u32 *)tex->data, GX_BUFFER_DIM(ATLAS_W, ATLAS_H),
        GX_TRANSFER_FLIP_VERT(0) | GX_TRANSFER_OUT_TILED(1) |
        GX_TRANSFER_RAW_COPY(0) |
        GX_TRANSFER_IN_FORMAT(GX_TRANSFER_FMT_RGBA8) |
        GX_TRANSFER_OUT_FORMAT(GX_TRANSFER_FMT_RGBA8) |
        GX_TRANSFER_SCALING(GX_TRANSFER_SCALE_NO));
}

static void snapshot_bases(u32 *out) {
    for (int ti = 0; ti < 512; ti++)
        out[ti] = ppu::ResolveTile(static_cast<u16>(ti * 16));
}

static int chr_set_now() {
    snapshot_bases(cur_bases);

    for (int i = 0; i < CHR_SETS; i++)
        if (chr_set[i].valid && std::memcmp(chr_set[i].bases, cur_bases, sizeof cur_bases) == 0) {
            chr_set[i].used = frame_stamp;
            return i;
        }

    int victim = 0;
    for (int i = 0; i < CHR_SETS; i++) {
        if (!chr_set[i].valid) { victim = i; break; }
        if (chr_set[i].used < chr_set[victim].used) victim = i;
    }

    AtlasSet &set = chr_set[victim];
    std::memcpy(set.bases, cur_bases, sizeof cur_bases);
    set.valid = true;
    set.used  = frame_stamp;
    for (int k = 0; k < 8; k++) set.baked[k] = false;   // textures kept, contents stale
    return victim;
}

static C3D_Tex *ensure_atlas(const int si, const int p, const bool spr) {
    AtlasSet &set = chr_set[si];
    const int k = (spr ? 4 : 0) + p;
    C3D_Tex *tex = &set.tex[k];

    if (!set.inited[k]) {
        C3D_TexInit(tex, ATLAS_W, ATLAS_H, GPU_RGBA8);
        C3D_TexSetFilter(tex, GPU_NEAREST, GPU_NEAREST);
        C3D_TexSetWrap(tex, GPU_CLAMP_TO_EDGE, GPU_CLAMP_TO_EDGE);
        set.inited[k] = true;
    }
    if (!set.baked[k]) {
        bake_atlas(atlas_scratch, set.bases, p, spr);
        upload_atlas(atlas_scratch, tex);
        set.baked[k] = true;
    }
    return tex;
}

static void note_palette() {
    if (palette_valid && std::memcmp(palette_cache, paletteRAM, 32) == 0) return;
    std::memcpy(palette_cache, paletteRAM, 32);
    palette_valid = true;
    for (auto &set : chr_set)
        for (int k = 0; k < 8; k++) set.baked[k] = false;
}

static inline float row_v(const int texel_row) {
    return 1.0f - (float)texel_row / ATLAS_H;
}

static inline void draw_bg_cell(C3D_Tex *tex, const Cell &c, const video::PanelRect &pr) {
    const int ax = (c.tile & 15) * 8 + c.c0;
    const int ay = (c.tile >> 4) * 8;
    const int top_row = ay + c.r0;
    Tex3DS_SubTexture sub;
    sub.width  = c.w;
    sub.height = c.h;
    sub.left   = (float)ax / ATLAS_W;
    sub.right  = (float)(ax + c.w) / ATLAS_W;
    sub.top    = row_v(top_row);
    sub.bottom = row_v(top_row + c.h);
    C2D_Image img{ tex, &sub };
    C2D_DrawImageAt(img, (float)(c.sx - pr.x), (float)(c.vy0 - pr.y), 0.0f);
}

static inline void draw_spr_tile(C3D_Tex *tex, const int tile, const int sx,
                                 const int sy, const bool flipH, const bool flipV,
                                 const video::PanelRect &pr) {
    const int px1 = pr.x + pr.w, py1 = pr.y + pr.h;
    const int vx0 = sx < pr.x ? pr.x : sx;
    const int vx1 = (sx + 8) > px1 ? px1 : (sx + 8);
    const int vy0 = sy < pr.y ? pr.y : sy;
    const int vy1 = (sy + 8) > py1 ? py1 : (sy + 8);
    if (vx1 <= vx0 || vy1 <= vy0) return;

    const int ax = (tile & 15) * 8;
    const int ay = (tile >> 4) * 8;
    float ul = (float)ax / ATLAS_W, ur = (float)(ax + 8) / ATLAS_W;
    float vt = row_v(ay),           vb = row_v(ay + 8);
    if (flipH) { const float t = ul; ul = ur; ur = t; }
    if (flipV) { const float t = vt; vt = vb; vb = t; }

    const float fxl = (float)(vx0 - sx) / 8.0f, fxr = (float)(vx1 - sx) / 8.0f;
    const float fyl = (float)(vy0 - sy) / 8.0f, fyr = (float)(vy1 - sy) / 8.0f;

    Tex3DS_SubTexture sub;
    sub.width  = static_cast<u16>(vx1 - vx0);
    sub.height = static_cast<u16>(vy1 - vy0);
    sub.left   = ul + (ur - ul) * fxl;
    sub.right  = ul + (ur - ul) * fxr;
    sub.top    = vt + (vb - vt) * fyl;
    sub.bottom = vt + (vb - vt) * fyr;
    C2D_Image img{ tex, &sub };
    C2D_DrawImageAt(img, (float)(vx0 - pr.x), (float)(vy0 - pr.y), 0.0f);
}

struct BandRec { i16 y0, y1; u16 xs, ys; u8 set; };
static constexpr int MAX_BANDS = 64;
static BandRec band_rec[MAX_BANDS];
static int     band_n;

static void plan_band(const int y0, const int y1, const u16 xs, const u16 ys) {
    const int si = chr_set_now();
    for (int p = 0; p < 4; p++) ensure_atlas(si, p, false);

    if (band_n == MAX_BANDS) {
        band_rec[band_n - 1].y1 = static_cast<i16>(y1);
        return;
    }
    band_rec[band_n++] = BandRec{ static_cast<i16>(y0), static_cast<i16>(y1), xs, ys,
                                  static_cast<u8>(si) };
}

static void draw_bg_band(const BandRec &b, const video::PanelRect &pr) {
    const int anchor = b.y0;
    const int y0     = b.y0 < pr.y ? pr.y : b.y0;
    const int y1     = b.y1 > (pr.y + pr.h) ? (pr.y + pr.h) : b.y1;
    if (y1 <= y0) return;

    const u16 xs = b.xs, ys = b.ys;
    const int vpw      = video::viewport_px();
    const emu::NtGeometry geo = emu::ComputeNtGeometry();
    const int world_w  = geo.worldW;
    const int world_h  = geo.worldH;
    const int atlas0   = (ppu::PPUCTRL & ppu::ctrl::BG_ADDR) ? 256 : 0;

    bg_count[0] = bg_count[1] = bg_count[2] = bg_count[3] = 0;

    const int sx0 = -(static_cast<int>(xs) & 7);
    const int sy0 = anchor - (static_cast<int>(ys) & 7);

    for (int syt = sy0; syt < y1; syt += 8) {
        const int vy0 = syt < y0 ? y0 : syt;
        const int vy1 = (syt + 8) > y1 ? y1 : (syt + 8);
        if (vy1 <= vy0) continue;
        const int r0  = vy0 - syt;          // first visible tile row (0..7)
        const int h   = vy1 - vy0;          // visible height (1..8)

        const int world_y   = static_cast<int>(ys) + (syt - anchor);
        const int wym       = ((world_y % world_h) + world_h) % world_h;
        const int trow      = wym / 8;
        const int local_row = trow % geo.tileH;
        const int nt_row    = trow / geo.tileH;
        const int at_roff   = (local_row >> 2) * geo.atW;
        const int at_rbits  = ((local_row >> 1) & 1) * 4;
        const int row32     = local_row * geo.tileW;

        for (int sxt = sx0; sxt < vpw; sxt += 8) {
            const int vx0 = sxt < pr.x ? pr.x : sxt;
            const int vx1 = (sxt + 8) > (pr.x + pr.w) ? (pr.x + pr.w) : (sxt + 8);
            if (vx1 <= vx0) continue;
            const int c0  = vx0 - sxt;          // first visible tile column (0..7)
            const int w   = vx1 - vx0;          // visible width (1..8)

            const int world_x   = static_cast<int>(xs) + sxt;
            const int wxm       = ((world_x % world_w) + world_w) % world_w;
            const int tcol      = wxm / 8;
            const int local_col = tcol % geo.tileW;
            const int nt_col    = tcol / geo.tileW;
            const int nt_off    = (nt_col + nt_row * geo.gridW) * geo.pageBytes;

            const u8 tile_id = ppu::ReadNametable(static_cast<u16>(nt_off + row32 + local_col));
            const u8 attr    = ppu::ReadNametable(static_cast<u16>(nt_off + geo.ntBytes + at_roff + (local_col >> 2)));
            const int pal    = (attr >> (((local_col >> 1) & 1) * 2 + at_rbits)) & 3;

            Cell &cell = bg_bucket[pal][bg_count[pal]++];
            cell.tile  = static_cast<u16>(atlas0 + tile_id);
            cell.sx    = static_cast<i16>(vx0);
            cell.vy0   = static_cast<i16>(vy0);
            cell.c0    = static_cast<u8>(c0);
            cell.w     = static_cast<u8>(w);
            cell.r0    = static_cast<u8>(r0);
            cell.h     = static_cast<u8>(h);
        }
    }

    for (int p = 0; p < 4; p++) {
        if (!chr_set[b.set].baked[p]) continue;
        for (int i = 0; i < bg_count[p]; i++)
            draw_bg_cell(&chr_set[b.set].tex[p], bg_bucket[p][i], pr);
    }
}

static u8 sprite_palette_mask() {
    const oam::sprite_t *oam = emu::OamShadow();
    u8 mask = 0;
    for (int s = 0; s < OAM_SPRITES; s++) mask |= static_cast<u8>(1u << (oam[s].attributes & 3));
    return mask;
}

static void draw_sprites(const bool behind, const video::PanelRect &pr, const int si) {
    const oam::sprite_t *oam = emu::OamShadow();
    const int  vpy    = video::viewport_py();
    const int  vpw    = video::viewport_px();
    const bool tall   = ppu::PPUCTRL & ppu::ctrl::SPRITE_SIZE;
    const int  atlas0 = (ppu::PPUCTRL & ppu::ctrl::SPRITE_ADDR) ? 256 : 0;

    for (int p = 0; p < 4; p++) {
        C3D_Tex *tex = &chr_set[si].tex[4 + p];
        if (!chr_set[si].baked[4 + p]) continue;   // no sprite uses this palette
        for (int s = 0; s < OAM_SPRITES; s++) {
            const oam::sprite_t &o = oam[s];
            if ((o.attributes & 0x03) != p)            continue;
            if (((o.attributes & 0x20) != 0) != behind) continue;
            const int sx = static_cast<int>(o.x);
            const int sy = static_cast<int>(o.y) + 1;
            if (sx >= vpw || sy >= vpy) continue;
            const bool flipH = (o.attributes & 0x40) != 0;
            const bool flipV = (o.attributes & 0x80) != 0;
            if (!tall) {
                draw_spr_tile(tex, atlas0 + o.tile, sx, sy, flipH, flipV, pr);
                continue;
            }
            const int pair = (o.tile & 1) * 256 + (o.tile & 0xFE);
            draw_spr_tile(tex, pair + (flipV ? 1 : 0), sx, sy,     flipH, flipV, pr);
            draw_spr_tile(tex, pair + (flipV ? 0 : 1), sx, sy + 8, flipH, flipV, pr);
        }
    }
}

static void band_noop(int, int, u16, u16) {}

namespace video {

void WaitForPresent() {
    if (!aptMainLoop()) quit = 1;

    const bool bg  = ppu::PPUMASK & ppu::mask::BG;
    const bool spr = ppu::PPUMASK & ppu::mask::SPRITE;

    frame_stamp++;

    band_n = 0;
    int spr_set_behind = 0, spr_set_front = 0;

    if (bg || spr) {
        note_palette();
        spr_set_behind = chr_set_now();
        emu::GenerateBands(bg ? plan_band : band_noop);
        spr_set_front  = chr_set_now();

        if (spr) {
            const u8 mask = sprite_palette_mask();
            for (int p = 0; p < 4; p++) {
                if (!(mask & (1u << p))) continue;
                ensure_atlas(spr_set_behind, p, true);
                ensure_atlas(spr_set_front,  p, true);
            }
        }
    } else {
        emu::GenerateBands(band_noop);
    }

    u32 clear;
    if (bg || spr) {
        const u32 bd = nes_rgb[paletteRAM[0] & 0x3F];
        clear = C2D_Color32((bd >> 16) & 0xFF, (bd >> 8) & 0xFF, bd & 0xFF, 0xFF);
    } else {
        clear = C2D_Color32(0, 0, 0, 0xFF);
    }

    C3D_FrameBegin(C3D_FRAME_SYNCDRAW);
    for (u8 i = 0; i < video::panel_count(); i++) {
        C2D_TargetClear(panel_target[i], clear);
        C2D_SceneBegin(panel_target[i]);
        if (!(bg || spr)) continue;

        const video::PanelRect pr = video::panel_rect(i);
        if (spr) draw_sprites(true, pr, spr_set_behind);
        if (bg)  for (int b = 0; b < band_n; b++) draw_bg_band(band_rec[b], pr);
        if (spr) draw_sprites(false, pr, spr_set_front);
    }
    C3D_FrameEnd(0);

    irq::irqPendingValid = false;
    nmi_vector();
}

}   // namespace video

void irq::init() {
    gfxInitDefault();

    osSetSpeedupEnable(true);

    C3D_Init(C3D_DEFAULT_CMDBUF_SIZE);
    C2D_Init(8192);
    C2D_Prepare();

    panel_target[0] = C2D_CreateScreenTarget(GFX_TOP,    GFX_LEFT);
    panel_target[1] = C2D_CreateScreenTarget(GFX_BOTTOM, GFX_LEFT);


    atlas_scratch = (u32 *)linearAlloc(ATLAS_W * ATLAS_H * sizeof(u32));

    emu::InitMemory(video::vram_bytes());
}

void irq::post() {
    linearFree(atlas_scratch);
    for (auto &set : chr_set)
        for (int k = 0; k < 8; k++)
            if (set.inited[k]) C3D_TexDelete(&set.tex[k]);
    C2D_Fini();
    C3D_Fini();
    gfxExit();
}
