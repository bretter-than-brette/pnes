/**
 * @file video.cpp
 * @brief Native 2D-hardware presentation backend for DS (libnds).
 *
 * The DS is a 2D tile console whose hardware is a near-cousin of the NES PPU, so
 * this hands the PPU's own primitives to the DS 2D core rather than rasterising:
 * CHR expands from 2bpp to 4bpp tiles once at init, paletteRAM becomes BGR555
 * palette RAM, the nametable becomes a 512x512 text BG, and the OAM shadow
 * becomes hardware OBJ. Pixel 0 is transparent on both, which reproduces NES
 * transparency for free.
 *
 * Driven by emu::GenerateBands, so the scanline IRQ handlers still fire in raster
 * order; each band's scroll goes into a per-scanline table an HBlank interrupt
 * programs line by line, which is how the mid-frame raster scroll and the
 * sprite-0 split work with no per-pixel CPU work.
 *
 * Both panels are driven from one plane, one map and one CHR, differing only in
 * the scroll the HBlank ISR programs -- the two LCDs scan in lockstep off one
 * REG_VCOUNT. A DS text BG is at most 512px tall, so only the first 512px of the
 * grid's 768px world can be resident (band_emit's `& 0x1FF`).
 */
#include "internal.hpp"

#include <pnes/video.hpp>
#include <pnes/interrupts.hpp>
#include "../emu/emu.hpp"

#include <nds.h>
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

static constexpr int SCREEN_H   = video::panel_rect(0).h;   // 192, visible scanlines per panel
static constexpr int PANELS     = video::panel_count();    // 2 -- both LCDs are driven
static constexpr int MAP_COLS   = 64;           // 512px / 8
static constexpr int MAP_ROWS   = 64;           // 512px / 8, the BG's max height
static constexpr int DS_TILE_SZ = 32;           // bytes per 4bpp 8x8 DS tile

static constexpr int PRIO_SPR_FRONT = 1;
static constexpr int PRIO_BG        = 2;
static constexpr int PRIO_SPR_BEHIND = 3;

namespace {

int  g_bg     = 0;                               // bgInit() handle for main BG0
int  g_bg_sub = 0;                               // bgInitSub() handle for sub BG0
alignas(4) u16 g_map_shadow[MAP_COLS * MAP_ROWS];
// CHR bank switching: any kind, anywhere, any number of times per frame.
//
// A DS BG's tiles are a resident VRAM resource rather than something it fetches,
// so the obvious model is to mirror each BANK the frame uses -- which is bounded
// at 7 atlases per 128 KB bank. This mirrors TILES instead, keyed by content: the
// resolved CHR address each of a tile's eight pixel rows comes from. Every cell
// is resolved under the bank state in effect on the scanlines that display it, so
// the number of switches stops mattering.
//
// That also makes it exact where bank-mirroring was not: a switch at any scanline
// is honoured, and where a tile row's eight scanlines span more than one state
// the cache synthesises a COMPOSITE tile, taking each pixel row from the state
// that displays it (g_slot_key, expand_rows). Sprites get the same treatment. In
// steady state it is cheaper too -- a hit is a hash probe, and a miss expands 32
// bytes rather than a 16 KB atlas.
static constexpr int TILE_SLOTS  = 1024;        // the map entry's 10-bit index
static constexpr int CHR_WINDOWS = 8;           // 8 KiB of CHR space / 1 KiB
static constexpr int TILE_BASE   = 0;           // 16 KB units: tiles at VRAM+0
static constexpr int MAP_BASE    = 16;          // 2 KB units: map after the pool
static constexpr int HASH_SIZE   = 2048;        // power of two, > TILE_SLOTS
static constexpr u16 HASH_EMPTY  = 0;           // stored value is slot + 1
static constexpr u16 HASH_TOMB   = 0xFFFF;

static constexpr int MAX_BANDS = video::viewport_py() + 2;

struct Band {
    u16 xs, ys;                  // this band's scroll
    u16 y0;                       // first viewport row it covers
    u32 base[CHR_WINDOWS];        // resolved LMA of each 1 KiB CHR window
};
Band g_band[MAX_BANDS];
int  g_bands;

static constexpr int VPH = video::viewport_py();
u16  s_band_of[VPH];
u16  s_worldy[VPH];

u16  g_rowband[MAP_ROWS][8];
bool g_row_used[MAP_ROWS];
bool g_row_uniform[MAP_ROWS];

alignas(4) u8 g_tiles[TILE_SLOTS * DS_TILE_SZ];
u32  g_slot_key[TILE_SLOTS][8];
bool g_slot_valid[TILE_SLOTS];
bool g_slot_uniform[TILE_SLOTS];
u32  g_slot_frame[TILE_SLOTS];   // frame it was last referenced on
u16  g_slot_home[TILE_SLOTS];    // its hash index, so eviction can tombstone it
u16  g_hash[HASH_SIZE];
u32  g_frame;                     // frame counter for the eviction guard
int  g_alloc_cursor;              // rolling allocation, keeps misses contiguous
int  g_dirty_lo, g_dirty_hi;      // slot range rebuilt this frame (-1 = none)
u32  g_overflow;                  // frames that wanted > TILE_SLOTS distinct tiles
int  g_tombs;                     // tombstones in g_hash, see rehash()

// Per-scanline scroll, one table per engine: the HBlank ISR programs both at the
// same line index, since the two LCDs scan off one REG_VCOUNT.
u16          sh_h[PANELS][SCREEN_H], sh_v[PANELS][SCREEN_H];
volatile u16 g_h[PANELS][SCREEN_H],  g_v[PANELS][SCREEN_H];

inline u16 to_bgr555(const u32 argb) {
    const u32 r = (argb >> 16) & 0xFF, g = (argb >> 8) & 0xFF, b = argb & 0xFF;
    return static_cast<u16>(RGB15(r >> 3, g >> 3, b >> 3));
}

// Takes each pixel row from the bank state that actually displays it, which is
// what makes a sub-tile-row switch exact.
void expand_rows(const u32 key[8], u8* out) {
    for (int row = 0; row < 8; row++) {
        const u8* nes = CHR_ROM + key[row];
        const u8  p0   = nes[row];
        const u8  p1   = nes[row + 8];
        for (int col = 0; col < 8; col += 2) {
            const u8 hiA = static_cast<u8>(7 - col), hiB = static_cast<u8>(6 - col);
            const u8 a = static_cast<u8>(((p0 >> hiA) & 1) | (((p1 >> hiA) & 1) << 1));
            const u8 b = static_cast<u8>(((p0 >> hiB) & 1) | (((p1 >> hiB) & 1) << 1));
            out[row * 4 + (col >> 1)] = static_cast<u8>(a | (b << 4));
        }
    }
}

inline u32 lma_of(const Band& b, const unsigned vma) {
    u32 lma = b.base[(vma >> 10) & (CHR_WINDOWS - 1)] + (vma & 0x3FF);
    if (lma >= ppu::chrRomBytes) lma -= ppu::chrRomBytes;
    return lma;
}

inline u32 hash_mix(const u32 a, const u32 b, const u32 c) {
    u32 h = a * 2654435761u;
    h ^= b * 2246822519u;
    h ^= c * 3266489917u;
    return (h ^ (h >> 15)) & (HASH_SIZE - 1);
}

void rehash() {
    for (u32 i = 0; i < HASH_SIZE; i++) g_hash[i] = HASH_EMPTY;
    g_tombs = 0;
    for (int s = 0; s < TILE_SLOTS; s++) {
        if (!g_slot_valid[s]) continue;
        const u32 h0 = g_slot_uniform[s]
            ? hash_mix(g_slot_key[s][0], g_slot_key[s][0], 0u)
            : hash_mix(g_slot_key[s][0], g_slot_key[s][3], g_slot_key[s][7]);
        for (u32 i = 0; i < HASH_SIZE; i++) {
            const u32 h = (h0 + i) & (HASH_SIZE - 1);
            if (g_hash[h] == HASH_EMPTY) {
                g_hash[h] = static_cast<u16>(s + 1);
                g_slot_home[s] = static_cast<u16>(h);
                break;
            }
        }
    }
}

int alloc_slot() {
    for (int tries = 0; tries < TILE_SLOTS; tries++) {
        const int s = g_alloc_cursor;
        g_alloc_cursor = (g_alloc_cursor + 1 == TILE_SLOTS) ? 0 : g_alloc_cursor + 1;
        if (!g_slot_valid[s] || g_slot_frame[s] != g_frame) {
            if (g_slot_valid[s]) {
                if (g_hash[g_slot_home[s]] == static_cast<u16>(s + 1)) {
                    g_hash[g_slot_home[s]] = HASH_TOMB;
                    g_tombs++;
                }
            }
            return s;
        }
    }
    return -1;   // every slot is live this frame -- see g_overflow
}

void commit_slot(const int s, const u32 key[8], const bool uniform, const u16 home) {
    __builtin_memcpy(g_slot_key[s], key, 8 * sizeof(u32));
    g_slot_valid[s]   = true;
    g_slot_uniform[s] = uniform;
    g_slot_frame[s]   = g_frame;
    g_slot_home[s]    = home;
    g_hash[home]      = static_cast<u16>(s + 1);
    expand_rows(key, g_tiles + s * DS_TILE_SZ);
    if (g_dirty_lo < 0 || s < g_dirty_lo) g_dirty_lo = s;
    if (s > g_dirty_hi) g_dirty_hi = s;
}

int intern_uniform(const u32 lma) {
    const u32 h0 = hash_mix(lma, lma, 0u);
    int tomb = -1;
    for (u32 i = 0; i < HASH_SIZE; i++) {
        const u32 h = (h0 + i) & (HASH_SIZE - 1);
        const u16 e = g_hash[h];
        if (e == HASH_EMPTY) {
            const u32 home = (tomb >= 0) ? static_cast<u32>(tomb) : h;
            const int s = alloc_slot();
            if (s < 0) { g_overflow++; return 0; }
            u32 key[8];
            for (int f = 0; f < 8; f++) key[f] = lma;
            commit_slot(s, key, true, static_cast<u16>(home));
            return s;
        }
        if (e == HASH_TOMB) { if (tomb < 0) tomb = static_cast<int>(h); continue; }
        const int s = e - 1;
        if (g_slot_valid[s] && g_slot_uniform[s] && g_slot_key[s][0] == lma) {
            g_slot_frame[s] = g_frame;
            return s;
        }
    }
    g_overflow++;
    return 0;
}

int intern_rows(const u32 key[8]) {
    const u32 h0 = hash_mix(key[0], key[3], key[7]);
    int tomb = -1;
    for (u32 i = 0; i < HASH_SIZE; i++) {
        const u32 h = (h0 + i) & (HASH_SIZE - 1);
        const u16 e = g_hash[h];
        if (e == HASH_EMPTY) {
            const u32 home = (tomb >= 0) ? static_cast<u32>(tomb) : h;
            const int s = alloc_slot();
            if (s < 0) { g_overflow++; return 0; }
            commit_slot(s, key, false, static_cast<u16>(home));
            return s;
        }
        if (e == HASH_TOMB) { if (tomb < 0) tomb = static_cast<int>(h); continue; }
        const int s = e - 1;
        if (g_slot_valid[s] && !g_slot_uniform[s] &&
            __builtin_memcmp(g_slot_key[s], key, 8 * sizeof(u32)) == 0) {
            g_slot_frame[s] = g_frame;
            return s;
        }
    }
    g_overflow++;
    return 0;
}

void build_row_sources() {
    for (int r = 0; r < MAP_ROWS; r++) { g_row_used[r] = false; g_row_uniform[r] = true; }

    for (int sy = 0; sy < VPH; sy++) {
        const u16 b = s_band_of[sy];
        if (b == 0xFFFF) continue;
        const u32 w    = s_worldy[sy];
        const int row  = static_cast<int>((w >> 3) & (MAP_ROWS - 1));
        const int fine = static_cast<int>(w & 7);
        if (!g_row_used[row]) {
            g_row_used[row] = true;
            for (int f = 0; f < 8; f++) g_rowband[row][f] = b;
        }
        g_rowband[row][fine] = b;
    }

    for (int r = 0; r < MAP_ROWS; r++) {
        if (!g_row_used[r]) continue;
        const u16 b0 = g_rowband[r][0];
        for (int f = 1; f < 8; f++)
            if (g_rowband[r][f] != b0) { g_row_uniform[r] = false; break; }
    }
}

void build_palettes() {
    for (int p = 0; p < 4; p++) {
        for (int c = 1; c < 4; c++) {
            const u16 bg  = to_bgr555(nes_rgb[paletteRAM[p * 4 + c]        & 0x3F]);
            const u16 spr = to_bgr555(nes_rgb[paletteRAM[0x10 + p * 4 + c] & 0x3F]);
            BG_PALETTE[p * 16 + c]         = bg;
            SPRITE_PALETTE[p * 16 + c]     = spr;
            BG_PALETTE_SUB[p * 16 + c]     = bg;
            SPRITE_PALETTE_SUB[p * 16 + c] = spr;
        }
    }
    BG_PALETTE[0]     = to_bgr555(nes_rgb[paletteRAM[0] & 0x3F]);
    BG_PALETTE_SUB[0] = BG_PALETTE[0];
}

void build_map() {
    const unsigned chr_space = (ppu::PPUCTRL & ppu::ctrl::BG_ADDR) ? 0x1000u : 0x0000u;
    const emu::NtGeometry geo = emu::ComputeNtGeometry();

    const int planeRows = geo.tileH;

    for (int row = 0; row < MAP_ROWS; row++) {
        if (!g_row_used[row]) continue;

        const int nt_row    = (row / planeRows) % geo.gridH;
        const int local_row = row % planeRows;
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

            const unsigned vma = static_cast<unsigned>(chr_space + tile_id * 16);
            int slot;
            if (g_row_uniform[row]) {
                slot = intern_uniform(lma_of(g_band[g_rowband[row][0]], vma));
            } else {
                u32 key[8];
                for (int f = 0; f < 8; f++)
                    key[f] = lma_of(g_band[g_rowband[row][f]], vma);
                slot = intern_rows(key);
            }

            const u16 entry = static_cast<u16>(slot | (pal << 12));
            const int sb  = (col >> 5) + ((row >> 5) * 2);
            const int idx = sb * 1024 + (row & 31) * 32 + local_col;
            g_map_shadow[idx] = entry;
        }
    }
}

void build_sprites() {
    const oam::sprite_t* oam = emu::OamShadow();
    const bool tall   = ppu::PPUCTRL & ppu::ctrl::SPRITE_SIZE;
    const int  height = tall ? 16 : 8;
    const unsigned spr_space = (ppu::PPUCTRL & ppu::ctrl::SPRITE_ADDR) ? 0x1000u : 0x0000u;


    for (int s = 0; s < OAM_SPRITES; s++) {
        const oam::sprite_t& o = oam[s];
        const int sx = static_cast<int>(o.x);
        const int sy = static_cast<int>(o.y) + 1;

        if (sx >= 256) {
            for (int half = 0; half < 2; half++) {
                oamClearSprite(&oamMain, s * 2 + half);
                oamClearSprite(&oamSub,  s * 2 + half);
            }
            continue;
        }

        const int  pal    = o.attributes & 0x03;
        const bool behind = (o.attributes & 0x20) != 0;
        const bool hflip  = (o.attributes & 0x40) != 0;
        const bool vflip  = (o.attributes & 0x80) != 0;
        const int halves = tall ? 2 : 1;
        for (int half = 0; half < halves; half++) {
            const int vhalf = vflip ? (halves - 1 - half) : half;
            const unsigned vma = tall
                ? static_cast<unsigned>(((o.tile & 1) * 0x1000) + ((o.tile & 0xFE) + vhalf) * 16)
                : static_cast<unsigned>(spr_space + o.tile * 16);

            const int ssy  = sy + half * 8;
            const int line = (ssy >= 0 && ssy < VPH) ? ssy : (ssy < 0 ? 0 : VPH - 1);
            const u16 bi   = s_band_of[line] == 0xFFFF ? 0 : s_band_of[line];
            const int slot = intern_uniform(lma_of(g_band[bi], vma));

            OamState* const oams[PANELS] = {&oamMain, &oamSub};
            const int oi = s * 2 + half;
            for (int panel = 0; panel < PANELS; panel++) {
                const int py = ssy - static_cast<int>(video::panel_rect(static_cast<u8>(panel)).y);
                if (py <= -8 || py >= SCREEN_H) {
                    oamClearSprite(oams[panel], oi);
                    continue;
                }
                oamSet(oams[panel], oi, sx, py,
                       behind ? PRIO_SPR_BEHIND : PRIO_SPR_FRONT,
                       pal, SpriteSize_8x8, SpriteColorFormat_16Color,
                       oamGetGfxPtr(oams[panel], slot),
                       -1, false, false, hflip, vflip, false);
            }
        }
    }
}

// emu::band_emit_fn. Records this band's scroll into the per-scanline tables
// rather than drawing anything: the HBlank ISR is what applies it.

void band_emit(int y0, int y1, u16 xs, u16 ys) {
    const emu::NtGeometry geo = emu::ComputeNtGeometry();
    const int worldH = geo.worldH;
    const int vph    = video::viewport_py();
    const int a = y0 < 0 ? 0 : y0;
    const int b = y1 > vph ? vph : y1;

    const int bi = g_bands < MAX_BANDS ? g_bands++ : MAX_BANDS - 1;
    Band& band = g_band[bi];
    band.xs = xs;
    band.ys = ys;
    band.y0 = static_cast<u16>(y0);
    for (int w = 0; w < CHR_WINDOWS; w++)
        band.base[w] = ppu::ResolveTile(static_cast<u16>(w * 0x400));

    for (int sy = a; sy < b; sy++) {
        const int panel = sy / SCREEN_H;
        const int line  = sy % SCREEN_H;
        if (panel >= PANELS) continue;

        const int w = (static_cast<int>(ys) + (sy - y0)) % worldH;
    // & 0x1FF: a DS text BG is 512px tall, so the grid's upper rows are not
    // resident and a scroll past them shows blank.
        const int phys = w & 0x1FF;
        sh_h[panel][line] = xs;
        sh_v[panel][line] = static_cast<u16>((phys - line) & 0x1FF);
        s_band_of[sy] = static_cast<u16>(bi);
        s_worldy[sy]  = static_cast<u16>(w);
    }
}

void band_noop(int, int, u16, u16) {}

void hblank_scroll() {
    const unsigned next = REG_VCOUNT + 1u;
    if (next < static_cast<unsigned>(SCREEN_H)) {
        REG_BG0HOFS     = g_h[0][next];
        REG_BG0VOFS     = g_v[0][next];
        REG_BG0HOFS_SUB = g_h[1][next];
        REG_BG0VOFS_SUB = g_v[1][next];
    }
}

} // namespace

namespace video {

void WaitForPresent() {
    const bool bg  = ppu::PPUMASK & ppu::mask::BG;
    const bool spr = ppu::PPUMASK & ppu::mask::SPRITE;

    g_frame++;
    if (g_tombs > HASH_SIZE / 4) rehash();
    g_bands = 0;
    g_dirty_lo = -1;
    g_dirty_hi = -1;
    for (int i = 0; i < VPH; i++) s_band_of[i] = 0xFFFF;

    for (int p = 0; p < PANELS; p++)
        for (int i = 0; i < SCREEN_H; i++) { sh_h[p][i] = 0; sh_v[p][i] = 0; }

    emu::GenerateBands(bg ? band_emit : band_noop);

    if (bg) {
        build_row_sources();
        build_map();
    }

    if (spr) build_sprites();
    else for (int s = 0; s < OAM_SPRITES * 2; s++) {
        oamClearSprite(&oamMain, s);
        oamClearSprite(&oamSub, s);
    }

    swiWaitForVBlank();

    if (g_dirty_lo >= 0) {
        const int off = g_dirty_lo * DS_TILE_SZ;
        const int len = (g_dirty_hi - g_dirty_lo + 1) * DS_TILE_SZ;
        DC_FlushRange(g_tiles + off, len);
        dmaCopy(g_tiles + off, (u8*)bgGetGfxPtr(g_bg)         + off, len);
        dmaCopy(g_tiles + off, (u8*)bgGetGfxPtr(g_bg_sub)     + off, len);
        dmaCopy(g_tiles + off, (u8*)oamGetGfxPtr(&oamMain, 0) + off, len);
        dmaCopy(g_tiles + off, (u8*)oamGetGfxPtr(&oamSub, 0)  + off, len);
    }

    build_palettes();

    if (bg) {
        DC_FlushRange(g_map_shadow, sizeof g_map_shadow);
        dmaCopy(g_map_shadow, bgGetMapPtr(g_bg),     sizeof g_map_shadow);
        dmaCopy(g_map_shadow, bgGetMapPtr(g_bg_sub), sizeof g_map_shadow);
        bgShow(g_bg);
        bgShow(g_bg_sub);
    } else {
        bgHide(g_bg);
        bgHide(g_bg_sub);
    }

    memcpy(const_cast<u16*>(&g_h[0][0]), sh_h, sizeof sh_h);
    memcpy(const_cast<u16*>(&g_v[0][0]), sh_v, sizeof sh_v);
    REG_BG0HOFS     = g_h[0][0];
    REG_BG0VOFS     = g_v[0][0];
    REG_BG0HOFS_SUB = g_h[1][0];
    REG_BG0VOFS_SUB = g_v[1][0];

    oamUpdate(&oamMain);
    oamUpdate(&oamSub);

    irq::irqPendingValid = false;
    nmi_vector();
}

} // namespace video

void irq::init() {
    videoSetMode(MODE_0_2D | DISPLAY_BG0_ACTIVE | DISPLAY_SPR_ACTIVE | DISPLAY_SPR_1D);
    vramSetBankA(VRAM_A_MAIN_BG);       // 128 KB: BG map + tiles
    vramSetBankB(VRAM_B_MAIN_SPRITE);   // 128 KB: OBJ tiles

    g_bg = bgInit(0, BgType_Text4bpp, BgSize_T_512x512, MAP_BASE, TILE_BASE);
    bgSetPriority(g_bg, PRIO_BG);

    oamInit(&oamMain, SpriteMapping_1D_32, false);

    videoSetModeSub(MODE_0_2D | DISPLAY_BG0_ACTIVE | DISPLAY_SPR_ACTIVE | DISPLAY_SPR_1D);
    vramSetBankC(VRAM_C_SUB_BG);        // 128 KB: sub BG map + tiles
    vramSetBankD(VRAM_D_SUB_SPRITE);    // 128 KB: sub OBJ tiles
    g_bg_sub = bgInitSub(0, BgType_Text4bpp, BgSize_T_512x512, MAP_BASE, TILE_BASE);
    bgSetPriority(g_bg_sub, PRIO_BG);
    oamInit(&oamSub, SpriteMapping_1D_32, false);

    for (u32 i = 0; i < HASH_SIZE; i++) g_hash[i] = HASH_EMPTY;
    __builtin_memset(g_tiles, 0, sizeof g_tiles);
    DC_FlushRange(g_tiles, sizeof g_tiles);
    dmaCopy(g_tiles, bgGetGfxPtr(g_bg),         sizeof g_tiles);
    dmaCopy(g_tiles, bgGetGfxPtr(g_bg_sub),     sizeof g_tiles);
    dmaCopy(g_tiles, oamGetGfxPtr(&oamMain, 0), sizeof g_tiles);
    dmaCopy(g_tiles, oamGetGfxPtr(&oamSub, 0),  sizeof g_tiles);

    emu::InitMemory(video::vram_bytes());

    irqSet(IRQ_HBLANK, hblank_scroll);
    irqEnable(IRQ_HBLANK);
    lcdSetHBlankIrq(true);
}

void irq::post() {
    lcdSetHBlankIrq(false);
    irqDisable(IRQ_HBLANK);
    irqClear(IRQ_HBLANK);
}
