/**
 * @file video.cpp
 * @brief Native 2D-hardware presentation backend for the GBA (libgba).
 *
 * The DS backend's model: the GBA is a 2D tile console close enough to the NES
 * PPU that this hands it the PPU's own primitives rather than rasterising. CHR
 * expands to 4bpp tiles at init, paletteRAM becomes BGR555, the nametable becomes
 * a 512x256 text BG, and the OAM shadow goes straight into OAM. Pixel 0 is
 * transparent on both, which reproduces NES transparency for free.
 *
 * Driven by emu::GenerateBands, with each band's scroll and CHR atlas recorded
 * into per-scanline tables an HBlank interrupt programs line by line -- so the
 * mid-frame raster scroll, the sprite-0 split and mid-frame CHR bank switches
 * need no per-pixel CPU work.
 *
 * The 240x160 panel is the first target narrower than the NES as well as
 * shorter, so this is a window onto the unaltered 256x240 world; PPU VRAM stays
 * a full 32-wide nametable, which is where the sub-tile scroll margin lives.
 *
 * Performance: the ARM7TDMI is ~4x slower than the DS's cached ARM9 and runs
 * from wait-stated GamePak ROM, so the per-frame hot path is IWRAM_CODE and the
 * shadows it touches are .bss (which is IWRAM here). The large cold buffers,
 * the CHR atlas staging areas, are pushed to EWRAM so they do not crowd them
 * out. There is no data cache on the ARM7, so no flushes before the DMAs.
 */
#include "internal.hpp"

#include <pnes/video.hpp>
#include <pnes/interrupts.hpp>
#include "../emu/emu.hpp"

#include <gba.h>
#include <string.h>

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

static constexpr int SCREEN_H    = 160;          // visible scanlines
static constexpr unsigned LINES_TOTAL = 228;     // total scanlines (160 visible + 68 vblank)
static constexpr int MAP_COLS    = 64;           // 512px / 8
static constexpr int MAP_ROWS    = 64;           // 2 x (256px / 8)
static constexpr int CHR_TILES   = 512;          // NES tiles in 8 KB CHR
static constexpr int GBA_TILE_SZ = 32;           // bytes per 4bpp 8x8 GBA tile
static constexpr int CHR_WINDOWS = 8;            // 8 KiB of CHR space / 1 KiB
static constexpr int CHR_SLOTS   = 3;            // atlases resident at once

static constexpr int PRIO_BG         = 2;
static constexpr int PRIO_SPR_FRONT  = 1;
static constexpr int PRIO_SPR_BEHIND = 3;

namespace {

constexpr u32 DCNT_MODE0    = 0x0000;   // tiled mode 0
constexpr u32 DCNT_BG0      = 0x0100;   // BG0 enable (bit 8)
constexpr u32 DCNT_OBJ      = 0x1000;   // OBJ enable (bit 12)
constexpr u32 DCNT_OBJ_1D   = 0x0040;   // 1D OBJ tile mapping (bit 6)
constexpr u16 DSTAT_HBL_IRQ = 0x0010;   // DISPSTAT HBlank-IRQ enable (bit 4)

constexpr u16 BGCNT_512x256 = 0x4000;   // size field = 1 (bits 14-15)
constexpr u16 BGCNT_512x512 = 0xC000;   // size field = 3 (bits 14-15) -- four screenblocks, 2x2
inline u16 BGCNT_CHARBASE(int n) { return static_cast<u16>(n << 2); }  // bits 2-3
inline u16 BGCNT_MAPBASE(int n)  { return static_cast<u16>(n << 8); }  // bits 8-12

constexpr u16 ATTR0_DISABLE = 0x0200;   // affine flag 0 + disable bit -> hidden
constexpr u16 ATTR1_HFLIP   = 0x1000;
constexpr u16 ATTR1_VFLIP   = 0x2000;

// Charblock 1 holds the map, so atlases live in 0, 2 and 3.
constexpr int kSlotBlock[CHR_SLOTS] = {0, 2, 3};

inline u8*  bg_tiles(const int slot) { return reinterpret_cast<u8*>(0x06000000 + kSlotBlock[slot] * 0x4000); }
inline u16* bg_map()    { return reinterpret_cast<u16*>(0x06004000); }
inline u8*  obj_tiles() { return reinterpret_cast<u8*>(0x06010000); }
inline u16* bg_pal()    { return reinterpret_cast<u16*>(0x05000000); }
inline u16* obj_pal()   { return reinterpret_cast<u16*>(0x05000200); }
inline void* oam_mem()  { return reinterpret_cast<void*>(0x07000000); }

struct ObjAttr { u16 a0, a1, a2, fill; };

alignas(4) u16 g_map_shadow[MAP_COLS * MAP_ROWS];
alignas(4) ObjAttr g_oam_shadow[128];

// One atlas per distinct CHR bank state, keyed by where each 1 KiB window
// resolves. Staged in EWRAM, uploaded only when a slot is rebuilt.
struct ChrKey { u32 base[CHR_WINDOWS]; };

EWRAM_BSS alignas(4) u8 g_chr4[CHR_SLOTS][CHR_TILES * GBA_TILE_SZ];
ChrKey g_slot_key[CHR_SLOTS];
bool   g_slot_valid[CHR_SLOTS];
bool   g_slot_upload[CHR_SLOTS];
u32    g_slot_frame[CHR_SLOTS];     // last frame a band used it, for eviction
u32    g_frame = 1;
ChrKey g_obj_key;
bool   g_obj_valid;
int    g_last_slot;
u16    g_bgcnt;                     // BG0CNT minus the char base

u16          sh_h[SCREEN_H], sh_v[SCREEN_H], sh_cnt[SCREEN_H];
volatile u16 g_h[SCREEN_H],  g_v[SCREEN_H],  g_cnt[SCREEN_H];

inline u16 to_bgr555(const u32 argb) {
    const u32 r = (argb >> 16) & 0xFF, g = (argb >> 8) & 0xFF, b = argb & 0xFF;
    return static_cast<u16>((r >> 3) | ((g >> 3) << 5) | ((b >> 3) << 10));
}

void expand_tile(const u8* nes, u8* out) {
    for (int row = 0; row < 8; row++) {
        const u8 p0 = nes[row];
        const u8 p1 = nes[row + 8];
        for (int c2 = 0; c2 < 4; c2++) {
            const int cl = c2 * 2, cr = c2 * 2 + 1;
            const u8 vl = ((p0 >> (7 - cl)) & 1) | (((p1 >> (7 - cl)) & 1) << 1);
            const u8 vr = ((p0 >> (7 - cr)) & 1) | (((p1 >> (7 - cr)) & 1) << 1);
            out[row * 4 + c2] = static_cast<u8>(vl | (vr << 4));
        }
    }
}

IWRAM_CODE void build_chr_atlas(const int slot) {
    for (int t = 0; t < CHR_TILES; t++) {
        const u32 lma = ppu::ResolveTile(static_cast<u16>(t * 16));
        expand_tile(CHR_ROM + lma, g_chr4[slot] + t * GBA_TILE_SZ);
    }
}

bool same_key(const ChrKey& a, const ChrKey& b) {
    for (int w = 0; w < CHR_WINDOWS; w++) if (a.base[w] != b.base[w]) return false;
    return true;
}

// The slot holding the current bank state, building one if none does. Evicts
// the least recently used slot this frame has not claimed.
IWRAM_CODE int acquire_slot() {
    ChrKey key;
    for (int w = 0; w < CHR_WINDOWS; w++) key.base[w] = ppu::ResolveTile(static_cast<u16>(w * 0x400));

    int victim = -1;
    for (int s = 0; s < CHR_SLOTS; s++) {
        if (g_slot_valid[s] && same_key(g_slot_key[s], key)) {
            g_slot_frame[s] = g_frame;
            return g_last_slot = s;
        }
        if (g_slot_frame[s] == g_frame) continue;
        if (victim < 0 || !g_slot_valid[s]
            || (g_slot_valid[victim] && g_slot_frame[s] < g_slot_frame[victim])) victim = s;
    }
    if (victim < 0) return g_last_slot;   // more bank states than slots this frame

    build_chr_atlas(victim);
    g_slot_key[victim]    = key;
    g_slot_valid[victim]  = true;
    g_slot_upload[victim] = true;
    g_slot_frame[victim]  = g_frame;
    return g_last_slot = victim;
}

inline u16 bgcnt_for(const int slot) { return static_cast<u16>(g_bgcnt | BGCNT_CHARBASE(kSlotBlock[slot])); }

IWRAM_CODE void build_palettes() {
    for (int p = 0; p < 4; p++) {
        for (int c = 1; c < 4; c++) {
            bg_pal()[p * 16 + c]  = to_bgr555(nes_rgb[paletteRAM[p * 4 + c]        & 0x3F]);
            obj_pal()[p * 16 + c] = to_bgr555(nes_rgb[paletteRAM[0x10 + p * 4 + c] & 0x3F]);
        }
    }
    bg_pal()[0] = to_bgr555(nes_rgb[paletteRAM[0] & 0x3F]);
}

IWRAM_CODE void build_map() {
    const int atlas0 = (ppu::PPUCTRL & ppu::ctrl::BG_ADDR) ? 256 : 0;
    const emu::NtGeometry geo = emu::ComputeNtGeometry();
    const int activeRows = 32 * geo.gridH;

    for (int row = 0; row < activeRows; row++) {
        const int nt_row    = row >> 5;
        const int row_in_sb = row & 31;
        const int local_row = row_in_sb % geo.tileH;
        const int row32     = local_row * geo.tileW;
        const int at_roff   = (local_row >> 2) * geo.atW;
        const int at_rbits  = ((local_row >> 1) & 1) * 4;

        for (int col = 0; col < MAP_COLS; col++) {
            const int nt_col    = col >> 5;          // 0 or 1 (which nametable)
            const int local_col = col & 31;
            const int nt_off    = (nt_col + nt_row * geo.gridW) * geo.pageBytes;

            const u8  tile_id = ppu::ReadNametable(static_cast<u16>(nt_off + row32 + local_col));
            const u8  attr    = ppu::ReadNametable(static_cast<u16>(nt_off + geo.ntBytes + at_roff + (local_col >> 2)));
            const int pal     = (attr >> (((local_col >> 1) & 1) * 2 + at_rbits)) & 3;

            const u16 entry = static_cast<u16>((atlas0 + tile_id) | (pal << 12));
            const int sb  = (col >> 5) + nt_row * 2;
            const int idx = sb * 1024 + row_in_sb * 32 + local_col;
            g_map_shadow[idx] = entry;
        }
    }
}

IWRAM_CODE void build_sprites() {
    const oam::sprite_t* oam = emu::OamShadow();
    const bool tall   = ppu::PPUCTRL & ppu::ctrl::SPRITE_SIZE;
    const int  atlas0 = (ppu::PPUCTRL & ppu::ctrl::SPRITE_ADDR) ? 256 : 0;


    for (int s = 0; s < OAM_SPRITES; s++) {
        const oam::sprite_t& o = oam[s];
        const int sx = static_cast<int>(o.x);
        const int sy = static_cast<int>(o.y) + 1;

        if (sx >= 240 || sy >= SCREEN_H) {
            g_oam_shadow[s].a0 = ATTR0_DISABLE;
            continue;
        }

        const int  pal    = o.attributes & 0x03;
        const bool behind = (o.attributes & 0x20) != 0;
        const bool hflip  = (o.attributes & 0x40) != 0;
        const bool vflip  = (o.attributes & 0x80) != 0;
        const int  ti     = tall ? ((o.tile & 1) * 256 + (o.tile & 0xFE)) : (atlas0 + o.tile);
        const int  prio   = behind ? PRIO_SPR_BEHIND : PRIO_SPR_FRONT;

        g_oam_shadow[s].a0 = static_cast<u16>((sy & 0xFF) | (tall ? ATTR0_TALL : 0));
        g_oam_shadow[s].a1 = static_cast<u16>((sx & 0x1FF)
                                            | (hflip ? ATTR1_HFLIP : 0)
                                            | (vflip ? ATTR1_VFLIP : 0));
        g_oam_shadow[s].a2 = static_cast<u16>(ti | (prio << 10) | (pal << 12));
    }
}

// Called with the banks as they stand for this band, so the split IRQ's
// switch lands on the lines below it.
// A nametable is 30 rows but its screenblock 32, so world Y is walked per line
// and mapped onto the plane, skipping each screenblock's last two rows.
IWRAM_CODE void band_emit(int y0, int y1, u16 xs, u16 ys) {
    const u16 cnt = bgcnt_for(acquire_slot());
    const int a = y0 < 0 ? 0 : y0;
    const int b = y1 > SCREEN_H ? SCREEN_H : y1;
    if (a >= b) return;

    const emu::NtGeometry geo = emu::ComputeNtGeometry();
    const int ntH   = geo.tileH * 8;
    const int planeMask = (ppu::nametableCount > 2 ? 512 : 256) - 1;

    const int w = (static_cast<int>(ys) + (a - y0)) % geo.worldH;
    int nt = w / ntH, r = w % ntH;

    for (int sy = a; sy < b; sy++) {
        sh_h[sy] = xs;
        sh_v[sy] = static_cast<u16>((nt * 256 + r - sy) & planeMask);
        sh_cnt[sy] = cnt;
        if (++r == ntH) { r = 0; if (++nt == geo.gridH) nt = 0; }
    }
}

void band_noop(int, int, u16, u16) {}

// Programs the band scroll tables into the scroll registers line by line, which
// is what reproduces the NES's mid-frame raster scroll.
IWRAM_CODE void hblank_scroll() {
    unsigned next = REG_VCOUNT + 1u;
    if (next >= LINES_TOTAL) next = 0u;     // wrap: last vblank line seeds line 0
    if (next < static_cast<unsigned>(SCREEN_H)) {
        REG_BG0HOFS = g_h[next];
        REG_BG0VOFS = g_v[next];
        REG_BG0CNT  = g_cnt[next];
    }
}

} // namespace

namespace video {

// When the ARM7 misses a VBlank the line-0 wrap-seed keeps the top of the screen
// stable rather than stranding it on the previous scanline's gameplay scroll.
IWRAM_CODE NI void WaitForPresent() {
    const bool bg  = ppu::PPUMASK & ppu::mask::BG;
    const bool spr = ppu::PPUMASK & ppu::mask::SPRITE;

    ++g_frame;
    // Sprites take the banks as the frame starts, before any split moves them.
    const int top = acquire_slot();
    const u16 top_cnt = bgcnt_for(top);

    for (int i = 0; i < SCREEN_H; i++) { sh_h[i] = 0; sh_v[i] = 0; sh_cnt[i] = top_cnt; }

    if (bg) build_map();

    emu::GenerateBands(bg ? band_emit : band_noop);

    if (spr) build_sprites();
    else for (int s = 0; s < OAM_SPRITES; s++) g_oam_shadow[s].a0 = ATTR0_DISABLE;

    VBlankIntrWait();

    for (int s = 0; s < CHR_SLOTS; s++) {
        if (!g_slot_upload[s]) continue;
        dmaCopy(g_chr4[s], bg_tiles(s), sizeof g_chr4[s]);
        g_slot_upload[s] = false;
    }
    if (!g_obj_valid || !same_key(g_obj_key, g_slot_key[top])) {
        dmaCopy(g_chr4[top], obj_tiles(), sizeof g_chr4[top]);
        g_obj_key   = g_slot_key[top];
        g_obj_valid = true;
    }

    build_palettes();

    if (bg) {
        dmaCopy(g_map_shadow, bg_map(), sizeof g_map_shadow);
        REG_DISPCNT |= DCNT_BG0;
    } else {
        REG_DISPCNT &= ~DCNT_BG0;
    }

    memcpy(const_cast<u16*>(g_h), sh_h, sizeof sh_h);
    memcpy(const_cast<u16*>(g_v), sh_v, sizeof sh_v);
    memcpy(const_cast<u16*>(g_cnt), sh_cnt, sizeof sh_cnt);
    REG_BG0HOFS = g_h[0];
    REG_BG0VOFS = g_v[0];
    REG_BG0CNT  = g_cnt[0];

    dmaCopy(g_oam_shadow, oam_mem(), sizeof g_oam_shadow);

    irq::irqPendingValid = false;
    nmi_vector();
}

} // namespace video

void irq::init() {
    irqInit();
    irqSet(IRQ_HBLANK, hblank_scroll);
    irqEnable(IRQ_VBLANK);   // required for VBlankIntrWait frame pacing

    REG_DISPSTAT |= DSTAT_HBL_IRQ;
    irqEnable(IRQ_HBLANK);

    g_bgcnt = static_cast<u16>(PRIO_BG | BGCNT_MAPBASE(8)
        | (ppu::nametableCount > 2 ? BGCNT_512x512 : BGCNT_512x256));

    const int s = acquire_slot();
    dmaCopy(g_chr4[s], bg_tiles(s), sizeof g_chr4[s]);
    dmaCopy(g_chr4[s], obj_tiles(), sizeof g_chr4[s]);
    g_slot_upload[s] = false;
    g_obj_key   = g_slot_key[s];
    g_obj_valid = true;
    REG_BG0CNT  = bgcnt_for(s);

    for (int s = 0; s < 128; s++) g_oam_shadow[s].a0 = ATTR0_DISABLE;
    dmaCopy(g_oam_shadow, oam_mem(), sizeof g_oam_shadow);

    emu::InitMemory(video::vram_bytes());

    REG_DISPCNT = DCNT_MODE0 | DCNT_BG0 | DCNT_OBJ | DCNT_OBJ_1D;
}

void irq::post() {
    REG_DISPSTAT &= ~DSTAT_HBL_IRQ;
    irqDisable(IRQ_HBLANK);
}
