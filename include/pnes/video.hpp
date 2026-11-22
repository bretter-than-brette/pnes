/**
 * @file video.hpp
 * @brief PPU abstraction: CHR ROM embedding, OAM, scrolling, nametable /
 *        attribute / palette writes, sprite-zero events, viewport geometry.
 *
 * The NES build targets the real PPU registers; every other backend presents an
 * equivalent framebuffer with the same conventions, so application code compiles
 * unchanged. Two things need care: the CHR image is placed in ::_CHR_SECTION so
 * the linker lands it on the PPU's 8 KB boundary, and poking the PPU outside
 * VBlank corrupts the display -- the ::VRAM block toggles ::PPUMASK around its
 * body.
 */
#pragma once

#include <intsh>
using namespace br0::intsh;
#include <cstddef>
#include <array>

#include "types.hpp"
#include "technology.hpp"

// SDL3 is the desktop backend only. Every console backend emulates the PPU too
// but presents through its own graphics API and must not see these headers.
#if !defined(TARGET_NES) && !defined(TARGET_OGC) && !defined(TARGET_CTR) && !defined(TARGET_NX) && !defined(TARGET_WIIU) && !defined(TARGET_PSP) && !defined(TARGET_NDS) && !defined(TARGET_GBA) && !defined(TARGET_PS2) && !defined(TARGET_PS3)
#include <SDL3/SDL_video.h>
#endif

namespace video {
extern const u16 PatternTables;

/// Measured by ::DetectRefresh, not built in: a European GameCube configured for
/// EURGB60 hands a PAL build a 60Hz mode, so anything scaled for 50Hz at compile
/// time would run ~20% fast.
extern bool is50Hz;

AI inline bool Is50Hz() { return is50Hz; }

bool DetectRefresh();
} // namespace video

#define OAM_SPRITES 64


#ifdef TARGET_NES
  #define _CHR_SECTION ".chr_rom"
#elif defined(_WIN32)
  #define _CHR_SECTION "chr_rom$m"
#elif defined(__APPLE__)
  #define _CHR_SECTION "__DATA,chr_rom"
#else
  #define _CHR_SECTION "chr_rom"
#endif

#ifndef TARGET_NES
  #if defined(_WIN32)
    extern "C" const u8 _chr_rom[];
  #elif defined(__APPLE__)
    extern const u8 _chr_rom[] __asm("section$start$__DATA$chr_rom");
  #else
    extern const u8 _chr_rom[] __asm("__start_chr_rom");
  #endif
  #define CHR_ROM ((const u8 *)_chr_rom)
#endif

namespace oam {
#ifndef TARGET_NES
    typedef u16 oam_t;
#else
    typedef u8 oam_t;
#endif

    struct sprite_t {
        oam_t y;            /**< Y coordinate of the top-left corner. */
        u8 tile;         /**< Pattern table tile index. */
        u8 attributes;   /**< Palette select, priority, flip flags. */
        oam_t x;            /**< X coordinate of the top-left corner. */
    };

    constexpr auto spriteStride =  sizeof(struct sprite_t);
    constexpr auto spriteSlot(const u16 slot) {return slot * spriteStride;}

    template<auto Off, auto Width>
    struct field_t {
        static constexpr u16 offset = Off;   /**< Byte offset within sprite_t. */
        static constexpr u8  width  = Width; /**< Field width in bytes.        */
    };

    inline constexpr field_t<offsetof(sprite_t, y),          sizeof(sprite_t::y)>          y{};
    inline constexpr field_t<offsetof(sprite_t, tile),       sizeof(sprite_t::tile)>       tile{};
    inline constexpr field_t<offsetof(sprite_t, attributes), sizeof(sprite_t::attributes)> attributes{};
    inline constexpr field_t<offsetof(sprite_t, x),          sizeof(sprite_t::x)>          x{};

    void OAMFromProvider(sprite_t *oam, u8 slot, u16 off,
                 u8 width, oam_t (*fn)(u16), u16 count);
    void OAMFromBuffer(sprite_t *oam, u8 slot, u16 off,
                       u8 width, const u8 *src, u16 count);

    template<auto Off, auto Width>
    void PopulateFromProvider(sprite_t *buf, u8 slot,
                                        field_t<Off, Width> /*field*/,
                                        oam_t (*fn)(u16), u16 count) {
        OAMFromProvider(buf, slot, Off, Width, fn, count);
    }

    template<auto Off, auto Width>
    void PopulateFromBuffer(sprite_t *buf, u8 slot,
                            field_t<Off, Width> /*field*/,
                            const void *src, u16 count) {
        OAMFromBuffer(buf, slot, Off, Width,
                      static_cast<const u8 *>(src), count);
    }
}

namespace ppu {
    extern const unsigned chrRomBytes;

/**
 * @brief Translates a tile's index in character ROM space into the index the PPU
 *        sees it at while its region is banked in at ::origin.
 *
 * A tile has two numbers and only one is a property of the tile: where it sits in
 * the image is fixed by whatever packed it, while the index the PPU reads it as
 * depends on the window it is banked into. Knows nothing about any mapper -- a
 * result holds for as long as the region really is banked there, which is the
 * caller's business to arrange. Costs nothing while the instance is constexpr.
 */
    class ChrWindow {
    public:
        const u16 chr;

        const u8 origin;

        constexpr ChrWindow(const u16 chrOrigin, const u8 ppuOrigin)
            : chr{chrOrigin}, origin{ppuOrigin} {}

        constexpr u8 Get(const u16 tile) const {
            return static_cast<u8>(origin + (tile - chr));
        }

        constexpr u8 operator+(const u16 tile) const {
            return Get(tile);
        }
    };

    namespace raw {
        enum PPU {
            PPUCTRL     = 0x2000, /**< Base nametable, VRAM increment, NMI enable. */
            PPUMASK     = 0x2001, /**< Rendering enable and color emphasis. */
            PPUSTATUS   = 0x2002, /**< VBlank, sprite 0, sprite-overflow flags. */
            OAMADDR     = 0x2003, /**< OAM write address. */
            OAMDATA     = 0x2004, /**< OAM read/write data port. */
            PPUSCROLL   = 0x2005, /**< Fine X/Y scroll write. */
            PPUADDR     = 0x2006, /**< VRAM address write. */
            PPUDATA     = 0x2007, /**< VRAM data port. */

            OAMDMA      = 0x4014  /**< OAM DMA transfer trigger. */
        };
    }


    namespace ctrl {
        enum CTRL {
          GEN_NMI     = 0x80, /**< Generate NMI on VBlank. */
          POLARITY    = 0x04, /**< VRAM address auto-increment direction (horizontal/vertical). */
          BG_ADDR     = 0x10, /**< Background pattern table at \$1000 (otherwise \$0000). */
          SPRITE_ADDR = 0x08, /**< Sprite pattern table at \$1000 (otherwise \$0000). Ignored in 8x16 mode. */
          SPRITE_SIZE = 0x20
      };
    }

    namespace mask {
        enum MASK {
          BG             = 0x08, /**< Show background. */
          SPRITE         = 0x10, /**< Show sprites. */
          BG_L           = 0x0a, /**< Show background in the left 8 pixels of the screen. */
          SPRITE_L       = 0x14, /**< Show sprites in the left 8 pixels of the screen. */
          RED            = 0x20, /**< Emphasise red. */
          GREEN          = 0x40, /**< Emphasise green. */
          BLUE           = 0x80, /**< Emphasise blue. */
      };
    }

#ifdef TARGET_NES
/**
 * @brief NES scroll encoding: the $2006/$2005/$2005/$2006 sequence, pre-packed,
 *        so every shift and mask happens ahead of the raster window.
 *
 * Carries no PPUCTRL byte -- the nametable select goes through $2006 -- so a
 * caller latching these directly is not updating the PPUCTRL shadow's select
 * bits the way ppu::SetScroll does.
 */
    typedef struct { u8 data[4]; } scroll_t;
#else
    typedef vec2<u16> scroll_t;
#endif

        enum PALETTE {
        BG_0          = 0 << 2, /**< Background palette 0. */
        BG_1          = 1 << 2, /**< Background palette 1. */
        BG_2          = 2 << 2, /**< Background palette 2. */
        BG_3          = 3 << 2, /**< Background palette 3. */
        SPRITE_0      = 4 << 2, /**< Sprite palette 0. */
        SPRITE_1      = 5 << 2, /**< Sprite palette 1. */
        SPRITE_2      = 6 << 2, /**< Sprite palette 2. */
        SPRITE_3      = 7 << 2, /**< Sprite palette 3. */
    };

    u16 CartesianToAddress(const vec2<u16>& pos);

    inline u16 CartesianToAddress(const volatile vec2<u16>& pos) {
        return CartesianToAddress(vec2<u16>{pos.x, pos.y});
    }

    constexpr u16 CartesianToAddress(const u16 pos) {
        return pos;
    }


    void StreamFromVideoMemory(u16 offset, atomic u8* target, u8 size);

#ifndef TARGET_NES
/// Resolves a PPU pattern-table address through whatever CHR banks are switched
/// in. Not a fetch: returns an offset, the caller reads. The weak default is
/// identity; a bank-switching mapper's emu TU supplies a strong definition.
    u32 ResolveTile(u16 tileVMA);

/// Bumped by a mapper on every CHR bank switch. The per-pixel rasterizer
/// re-resolves every fetch and never needs it; the native-2D backends bake a
/// tile atlas once and compare this to decide whether to re-bake.
    extern u32 chrGeneration;

    u8 ReadNametable(u16 logical);

    void WriteNametable(u16 logical, u8 value);

/// How many physical nametables the linked mapper provides, fixed at link time:
/// 2 for switchable mirroring, 4 for four-screen, 1 for single-screen. Every
/// backend derives the same grid from this plus the runtime ::mirroring.
    extern const u8 nametableCount;
#endif
}
#ifndef TARGET_NES
extern bool mirroring;
#endif
#if !defined(TARGET_NES) && !defined(TARGET_OGC) && !defined(TARGET_CTR) && !defined(TARGET_NX) && !defined(TARGET_WIIU) && !defined(TARGET_PSP) && !defined(TARGET_NDS) && !defined(TARGET_GBA) && !defined(TARGET_PS2) && !defined(TARGET_PS3)
extern const SDL_DisplayMode* mode;
extern u8 scale;
#endif
#if defined(TARGET_OGC)
extern u16 ogc_world_tx;
#endif
#if defined(TARGET_WIIU)
extern u16 wiiu_world_tx;
#endif

namespace video {
#if defined(TARGET_NES)
    constexpr u16 viewport_tx() { return 32; }
    constexpr u16 viewport_ty() { return 30; }
    constexpr u16 viewport_px() { return viewport_tx() << 3; }
    constexpr u16 viewport_py() { return viewport_ty() << 3; }
#elif defined(TARGET_OGC)
    // Runtime, because the TV resolution and display aspect are: computed at init
    // from the real pixel width so GX renders more world to fill a wider screen
    // rather than stretching a 256px image.
    constexpr u16 viewport_tx() { return ogc_world_tx; }
    constexpr u16 viewport_ty() { return 30; }
    constexpr u16 viewport_px() { return viewport_tx() << 3; }
    constexpr u16 viewport_py() { return viewport_ty() << 3; }

#elif defined(TARGET_CTR)
    // Both panels, as their bounding box: width is the wider (400px top), height
    // the sum. The hinge gap is not modelled, and panel_rect() owns the inset.
    constexpr u16 viewport_tx() { return 50; }
    constexpr u16 viewport_ty() { return 60; }
    constexpr u16 viewport_px() { return viewport_tx() << 3; }
    constexpr u16 viewport_py() { return viewport_ty() << 3; }
#elif defined(TARGET_NDS)
    // Both panels stacked: the NES's 32 tiles wide, 48 tall. The first target
    // TALLER than the NES, so nothing may assume viewport_ty() == 30.
    constexpr u16 viewport_tx() { return 32; }
    constexpr u16 viewport_ty() { return 48; }
    constexpr u16 viewport_px() { return viewport_tx() << 3; }
    constexpr u16 viewport_py() { return viewport_ty() << 3; }
#elif defined(TARGET_GBA)
    // The first target narrower than the NES as well as shorter, so this is a
    // window onto the same world rather than a compromised one.
    constexpr u16 viewport_tx() { return 30; }
    constexpr u16 viewport_ty() { return 20; }
    constexpr u16 viewport_px() { return viewport_tx() << 3; }
    constexpr u16 viewport_py() { return viewport_ty() << 3; }
#elif defined(TARGET_NX)
    // The Switch has no 4:3 mode, so unlike GX/3DS/Wii U this is a real constant.
    // 52x30 is ~16:9 and keeps 240 an integer 3x of 720p; a multiple of 4 tiles
    // keeps the 32px attribute regions aligned.
    constexpr u16 viewport_tx() { return 52; }
    constexpr u16 viewport_ty() { return 30; }
    constexpr u16 viewport_px() { return viewport_tx() << 3; }
    constexpr u16 viewport_py() { return viewport_ty() << 3; }
#elif defined(TARGET_WIIU)
    // Runtime, because the Wii U's aspect is a system setting: 52 tiles on a
    // widescreen set, 40 (exactly 4:3) otherwise.
    constexpr u16 viewport_tx() { return wiiu_world_tx; }
    constexpr u16 viewport_ty() { return 30; }
    constexpr u16 viewport_px() { return viewport_tx() << 3; }
    constexpr u16 viewport_py() { return viewport_ty() << 3; }
#elif defined(TARGET_PS2)
    // The GS scales a textured quad for free, so there is no reason to render
    // more or less than the NES's own 32x30.
    constexpr u16 viewport_tx() { return 32; }
    constexpr u16 viewport_ty() { return 30; }
    constexpr u16 viewport_px() { return viewport_tx() << 3; }
    constexpr u16 viewport_py() { return viewport_ty() << 3; }
#elif defined(TARGET_PS3)
    // No free scaling here (an RSX-mapped blit, no quad), but 240 goes into 720p
    // exactly 3 times, so NES-native scales with one source pixel per
    // destination pixel.
    constexpr u16 viewport_tx() { return 32; }
    constexpr u16 viewport_ty() { return 30; }
    constexpr u16 viewport_px() { return viewport_tx() << 3; }
    constexpr u16 viewport_py() { return viewport_ty() << 3; }
#elif defined(TARGET_PSP)
    // Width matches the 480px panel exactly, so there is no scale step at all.
    // Height cannot follow it to 272: the core's vertical walk wraps at
    // gridH * viewport_py(), so a taller viewport reads back into row 0 part-way
    // down the screen. Letterboxed instead.
    constexpr u16 viewport_tx() { return 60; }
    constexpr u16 viewport_ty() { return 30; }
    constexpr u16 viewport_px() { return viewport_tx() << 3; }
    constexpr u16 viewport_py() { return viewport_ty() << 3; }
#elif defined(LANDSCAPE)
    constexpr u16 viewport_tx() { return ((mode->w / scale) >> 3) & ~3u; }
    constexpr u16 viewport_ty() { return 30; }
    constexpr u16 viewport_px() { return viewport_tx() << 3; }
    constexpr u16 viewport_py() { return viewport_ty() << 3; }
#else
    constexpr u16 viewport_tx() { return 32; }
    constexpr u16 viewport_ty() { return (mode->h / scale) >> 3; }
    constexpr u16 viewport_px() { return viewport_tx() << 3; }
    constexpr u16 viewport_py() { return viewport_ty() << 3; }
#endif

namespace ogc {
#if defined(TARGET_GC)
/**
 * @brief Switches the GameCube between the 4:3 and 16:9 anamorphic viewports,
 *        live. A runtime switch, because the GameCube cannot work the answer out
 *        for itself -- its SRAM has no aspect field and there is no EDID on an
 *        analog cable.
 *
 * @warning The viewport width IS the nametable page width, so changing it
 *          re-strides the plane and everything already in VRAM lands at the wrong
 *          offsets. Switch with rendering disabled, redraw, and re-run the
 *          generated UI's Make_* calls. The allocation never moves.
 */
    void SetWidescreen(bool on);
#else
    inline void SetWidescreen(bool) {}
#endif
}

/// One physical panel inside the viewport. ::viewport_px() x ::viewport_py() is
/// the bounding box of every panel a target presents; on the dual-screen
/// handhelds this is the one place that says where each one sits in it.
    struct PanelRect {
        u16 x;   ///< Panel's left edge within the viewport, in pixels.
        u16 y;   ///< Panel's top edge within the viewport, in pixels.
        u16 w;   ///< Panel width in pixels.
        u16 h;   ///< Panel height in pixels.
    };

#if defined(TARGET_NDS)
    constexpr u8 panel_count() { return 2; }
    constexpr PanelRect panel_rect(const u8 i) {
        return i == 0 ? PanelRect{0, 0, 256, 192}
                      : PanelRect{0, 192, 256, 192};
    }
#elif defined(TARGET_CTR)
    constexpr u8 panel_count() { return 2; }
    constexpr PanelRect panel_rect(const u8 i) {
        return i == 0 ? PanelRect{0, 0, 400, 240}
                      : PanelRect{40, 240, 320, 240};
    }
#else
    constexpr u8 panel_count() { return 1; }
    constexpr PanelRect panel_rect(u8) {
        return PanelRect{0, 0, viewport_px(), viewport_py()};
    }
#endif

    constexpr u16 panel_tx(const u8 i) { return static_cast<u16>(panel_rect(i).w >> 3); }
    constexpr u16 panel_ty(const u8 i) { return static_cast<u16>(panel_rect(i).h >> 3); }

#ifndef TARGET_NES
/// The grid the addressable world forms: ::ppu::nametableCount and the runtime
/// ::mirroring, which is the alias real mirroring wiring gives for free. No
/// viewport term -- each nametable already matches the viewport's own size.
    constexpr u16 nametable_grid_w() {
        return static_cast<u16>(ppu::nametableCount >= 4 ? 2 : (ppu::nametableCount >= 2 ? (mirroring ? 1 : 2) : 1));
    }
    constexpr u16 nametable_grid_h() {
        return static_cast<u16>(ppu::nametableCount >= 4 ? 2 : (ppu::nametableCount >= 2 ? (mirroring ? 2 : 1) : 1));
    }

/// A tile plane (floored at 32x30, width rounded up to whole attribute bytes)
/// plus its attribute plane. Must stay in step with ::emu::ComputeNtGeometry,
/// which is the layout every read and write actually uses.
    constexpr unsigned nametable_plane_bytes() {
        const unsigned txMin = viewport_tx() < 32 ? 32u : viewport_tx();
        const unsigned tx = (txMin + 3u) & ~3u;
        const unsigned ty = viewport_ty() < 30 ? 30u : viewport_ty();
        const unsigned at_w = (tx + 3) / 4, at_h = (ty + 3) / 4;
        return tx * ty + at_w * at_h;
    }

    constexpr unsigned vram_bytes() {
        return static_cast<unsigned>(nametable_grid_w()) * nametable_grid_h() * nametable_plane_bytes();
    }
#endif
}


#ifndef TARGET_NES
extern u8* VideoRAM;
extern u8* paletteRAM;
/// Off-NES shadow of MMC3's $A000 mirroring bit: false vertical, true
/// horizontal. There is no register to poke here, so mapper code writes this.
extern bool mirroring;
#endif

#ifdef TARGET_NES
  /// Reads $2002 first to reset the write toggle, which this sequence depends on:
  /// it is positional, and entering at w == 1 makes the leading $2006 latch a
  /// bogus v immediately.
  #define WRITE_SCROLL(s) do { \
      (void)tech::peek(ppu::raw::PPUSTATUS); \
      tech::poke(ppu::raw::PPUADDR,   (s).data[0]); \
      tech::poke(ppu::raw::PPUSCROLL, (s).data[1]); \
      tech::poke(ppu::raw::PPUSCROLL, (s).data[2]); \
      tech::poke(ppu::raw::PPUADDR,   (s).data[3]); \
  } while(0)
#else
  #define WRITE_SCROLL(s) do { ppu::SetScroll(s); } while(0)
#endif

namespace video {
    void WaitForPresent();
}

namespace ppu {
    void EnableRendering(u8 ppuCtrl_, u8 ppuMask_);

    scroll_t CartesianToScroll(vec2<u16> pos);

    void SetScroll(vec2<u16> pos);

/// The first three writes of the four-write sequence. They only stage `t`, and
/// the PPU renders from `v`, so nothing here is visible and the timing does not
/// matter -- which is the whole point of the pair. Also keeps the PPUCTRL
/// shadow's select bits in step. Off-NES there is no `t`, so this does nothing.
    void StageScroll(scroll_t s);

/// The fourth write. The raster-critical one -- the scroll changes at the exact
/// dot this store lands on -- so put the split's cycle delay before it. Must be
/// paired with a StageScroll of the same value.
    void LatchScroll(scroll_t s);

    void WriteFromBufferToNameTable(vec2<u16> pos, const u8* source, u8 sBuffer, u8 polarity);

    void WriteFromBufferToNameTable(u16 address, const u8* source, u8 sBuffer, u8 polarity);

    void WriteRepeatedToNameTable(vec2<u16> pos, u8 value, u8 amt, u8 polarity);

    void WriteRepeatedToNameTable(u16 address, u8 value, u8 amt, u8 polarity);

    void WriteSingleToNameTable(vec2<u16> pos, u8 value);

/// The (x,y)->address projection is the costly part of a write, so this lets a
/// caller pay it once off the hot path via ::ppu::CartesianToAddress and replay
/// the write inside the vblank window as three pokes.
    void WriteSingleToNameTable(u16 address, u8 value);

    template <typename Idx>
    void WriteFromProviderToNameTable(vec2<u16> pos, u8 (*fn)(Idx), u8 amt, u8 polarity);

    template <typename Idx>
    void WriteFromProviderToNameTable(u16 address, u8 (*fn)(Idx), u8 amt, u8 polarity);

    void WriteFromBufferToAttributeTable(vec2<u16> pos, const u8* source, u8 sBuffer, u8 polarity);

    void WriteFromBufferToAttributeTable(u16 address, const u8* source, u8 sBuffer, u8 polarity);

    void WriteRepeatedToAttributeTable(vec2<u16> pos, u8 value, u8 amt, u8 polarity);

    void WriteRepeatedToAttributeTable(u16 address, u8 value, u8 amt, u8 polarity);

    void WriteSingleToAttributeTable(vec2<u16> pos, u8 value);

    void WriteSingleToAttributeTable(u16 address, u8 value);

    template <typename Idx>
    void WriteFromProviderToAttributeTable(vec2<u16> pos, u8 (*fn)(Idx), u8 amt, u8 polarity);

    template <typename Idx>
    void WriteFromProviderToAttributeTable(u16 address, u8 (*fn)(Idx), u8 amt, u8 polarity);

    void Flush(u8 nt, u8 at);

    void SetColorPriority(u8 priority);

    namespace pal {
        void WriteFromBuffer(u8 offset, const u8* source, u8 sBuffer);

        void WriteSingle(u8 offset, u8 value);
    }
}

namespace oam {
    void RefreshSprites(const sprite_t* oam);
}

namespace video {
#ifdef TARGET_NES
typedef void (*spriteZeroHandler_t)();
#else
typedef struct {
  void (*method)(void); /**< Callback fired on sprite-zero hit. */
  vec2<u16> pos;   /**< Pixel position at which to fire. */
} spriteZeroHandler_t;

void SetSpriteZeroHandler(vec2<u16> pos, void (*fn)(void));
#define SET_SPRITE_ZERO_HANDLER(px, py, fn) ::video::SetSpriteZeroHandler({px, py}, fn)
#endif

void WaitThenReactToSpriteZero(vec2<u16> pos, void (*fn)(), atomic u8* latch);

/**
 * @brief As above, but any invocable rather than a bare function pointer, so the
 *        handler can carry a bank.
 *
 * A `void (*)()` has nowhere to put a bank number, which is fine when the bank is
 * a compile-time constant (a captureless lambda converts to the overload above)
 * and not when the target is chosen at runtime.
 *
 * @note Off-NES @p fn must still convert to `void (*)()`: the handler is one
 *       pointer-shaped slot, and there are no banks to carry.
 */
template <typename Handler>
void WaitThenReactToSpriteZero(const vec2<u16> pos, Handler &&fn,
                               atomic u8* latch) {
#ifdef TARGET_NES
    (void)pos;
    while (!*latch) {
        while (  tech::peek(ppu::raw::PPUSTATUS) & 0x40)  { }
        while (!(tech::peek(ppu::raw::PPUSTATUS) & 0x40)) { }
        fn();
        *latch = true;
    }
#else
    void (*thunk)() = fn;   // captureless off-NES; see @note above
    WaitThenReactToSpriteZero(pos, thunk, latch);
#endif
}
} // namespace video

#ifdef TARGET_NES
namespace ppu {
extern tech::wo_register<ppu::raw::PPUCTRL>   PPUCTRL;
extern tech::wo_register<ppu::raw::PPUMASK>   PPUMASK;
}   // namespace ppu

namespace oam {
extern tech::wo_register<ppu::raw::OAMADDR>   OAMADDR;
extern tech::wo_register<ppu::raw::OAMDMA>    OAMDMA;
}   // namespace oam

#else
namespace ppu {
extern u8 PPUCTRL;
extern u8 PPUMASK;
}   // namespace ppu

#define VRAM \
if (1)

#endif