// The play mode: one frame of level logic per present, with the raster split
// that keeps the HUD still while the playfield scrolls under it.
#include "level.hpp"

#include <pnes/apu.hpp>
#include <pnes/mappers/mmc3.hpp>

#include "../banks.hpp"
#include "../main.hpp"

#include "../graphics/colours.hpp"
#include "../graphics/strings.hpp"
#include "../../chr/chr.h"
#include "../graphics/windows.hpp"
#include "../graphics/metatiles.hpp"

#ifdef TARGET_NES
#ifndef GEN_TARGET_DIR
#error "GEN_TARGET_DIR is not set -- add it to this target's CMakeLists.txt branch."
#endif
#include STRCAT(../../gen/options/GEN_TARGET_DIR/options.hpp)
#endif

#include "level/levels.hpp"
#include "level/collision_map.hpp"
#include "level/dynamic.hpp"
#include "pnes/extras/debug/emphasis.hpp"

#include "../../animations/gen/mary_standing.hpp"
#include "../../animations/gen/mary_walking.hpp"
using namespace demo;
using enum level::eLevelStreamCommands;

namespace level {
    bool paused;

    u8 port1;
    u8 port2;

    u8 lastPort1;
    u8 lastPort2;


    constexpr u8 kHudSplitRow   = 16;
    AI inline u8 HudSplitMMC3()  { return 16 - (::video::Is50Hz() ? 5 : 4); }
    AI inline u8 HUDSplitDelay() { return ::video::Is50Hz() ? 80 : 80; }

#ifdef TARGET_NDS
    constexpr u16 kLowerPanelRow = video::panel_rect(1).y;                  // 192
    constexpr u16 kBlankPlaneX   = video::viewport_px();                    // 256: right nametable
    constexpr u16 kBlankPlaneY   = kFrameRows;                              // first plane row below the level
    constexpr u16 kBlankRows     = video::viewport_py() - kBlankPlaneY;     // 144

    static atomic u16 nextBlankRow;

    static FIXED void ApplyLowerPanelBlank();
#endif

    static FIXED void ApplyHudSplit();

#ifdef TARGET_NES
    constexpr u8 kCoinVramRec = 4;
    constexpr u8 kCoinVramCap = 8 * kCoinVramRec;    // 4 coins, 2 records each
#else
    constexpr u8 kCoinVramRec = 3;
    constexpr u8 kCoinVramCap = 16 * kCoinVramRec;   // 4 coins, 4 records each
#endif
    static u8 CoinVram[kCoinVramCap];
    static u8 CoinVramLen;                  // bytes used

    static void CoinVramReset() { CoinVramLen = 0; }
    static bool suppressNmiScroll;

#ifdef TARGET_NES
    ACTORS void CoinVramPush(const u16 address, const u8 v0, const u8 v1) {
        if (CoinVramLen + kCoinVramRec > kCoinVramCap) return;
        CoinVram[CoinVramLen++] = static_cast<u8>(address >> 8);
        CoinVram[CoinVramLen++] = static_cast<u8>(address & 0xFF);
        CoinVram[CoinVramLen++] = v0;
        CoinVram[CoinVramLen++] = v1;
    }
#else
    ACTORS void CoinVramPush(const u16 address, const u8 value) {
        if (CoinVramLen + kCoinVramRec > kCoinVramCap) return;
        CoinVram[CoinVramLen++] = static_cast<u8>(address >> 8);
        CoinVram[CoinVramLen++] = static_cast<u8>(address & 0xFF);
        CoinVram[CoinVramLen++] = value;
    }
#endif

#ifdef TARGET_NES
    AI static void CoinWritePair(const u16 address, const u8 v0, const u8 v1) {
        tech::peek(ppu::raw::PPUSTATUS);
        tech::poke(ppu::raw::PPUADDR, static_cast<u8>(address >> 8));
        tech::poke(ppu::raw::PPUADDR, static_cast<u8>(address & 0xFF));
        tech::poke(ppu::raw::PPUDATA, v0);
        tech::poke(ppu::raw::PPUDATA, v1);
    }
#endif


    Player player1;
#ifdef PLAYER2_SUPPORTED
    Player player2;

    SYSMEM bool multiplayer;
#endif

    i8 lastDeltaScroll;
    atomic u16 lastXWorldSpace;
    static u16 edgeRAbs;
    u16 cameraX;

    static atomic u16 rasterCameraX;

    static ppu::scroll_t hudScroll;
#ifdef PNES_WINDOWED_PANEL
    static atomic oam::oam_t rasterPlayerY;
#endif

    atomic tech::enum_flags<eLevelStreamCommands> levelStreamCommand;
    u8 TileBuffer[56];

    constexpr u8 kPlayerSprites = static_cast<u8>(gen::mary_standing::kSize);
    static_assert(gen::mary_walking::kSize == gen::mary_standing::kSize,
                  "mary's animations disagree about how many sprites she is: one of "
                  "them would run off the end of the slots reserved for her");

    static oam::oam_t Clear(u16 _);
    static oam::oam_t Zero(u16 _);
    static oam::oam_t SpriteY(u16 i);
    static oam::oam_t SpriteX(u16 i);
#ifdef PLAYER2_SUPPORTED
    static oam::oam_t SpriteY2(u16 i);
    static oam::oam_t SpriteX2(u16 i);
#endif
    static i16 ClampRow(u16 y);
    static COLD NI void EnterLevelSetup() {
        suppressNmiScroll = true;   // see ::suppressNmiScroll's own comment
        pNMI = nmi_handler;
        pIRQ = irq_handler;

        video::WaitForPresent();

        mmc3::SwitchCHRBank<mmc3::BankOf<blob::dynamicLevelTiles::__lma>()>(mmc3::chr2Control);   // R2, PPU $0000
        mmc3::SwitchCHRBank<mmc3::BankOf<blob::staticLevelTiles::__lma>()>(mmc3::chr3Control);   // R3, PPU $0400
        mmc3::SwitchCHRBank<mmc3::BankOf<blob::ui::__lma>()>(mmc3::chr3Control);   // R3, PPU $0400
        mmc3::SwitchCHRBank<mmc3::BankOf<blob::normalSprites::__lma>()>(mmc3::chr0Control);   // R0, PPU $1000, 2 KiB

        constexpr u32 padLma = blob::normalSprites::__lma + blob::normalSprites::__size;
        static_assert(padLma + 2 * mmc3::kChrBankSize <= blob::dynamicLevelTiles::__lma,
                      "R5/R1 run past normalSprites into the next entry: give "
                      "PPU $0C00-$0FFF and $1800-$1FFF entries of their own, "
                      "or re-point them");
        mmc3::SwitchCHRBank<mmc3::BankOf<padLma>()>(mmc3::chr5Control);
        mmc3::SwitchCHRBank<mmc3::BankOf<padLma>()>(mmc3::chr1Control);
        mmc3::SetMirroring(false);

        const bool levelLoaded = mmc3::CallInBlock<level_code_tag>([] {
            return LoadLevel(0);
        });
        if (!levelLoaded) {
            irq::reset();    // spin reset on NES, exit on SDL3
        }

        ppu::Flush((windows::ui + blob::ui::hud_whitespace), 0xff);

        oam::PopulateFromProvider(OAMBuffer, 0, oam::y, Clear, 64);

        oam::PopulateFromProvider(OAMBuffer, 0, oam::attributes, Zero, 64);


        video::WaitForPresent();
        CallLevelGraphics([] {
            ppu::pal::WriteFromBuffer(ppu::BG_0,         SIZED_OBJ(BGColours));
            ppu::pal::WriteFromBuffer(13,                SIZED_OBJ(hudColours));   // BG palette 3 (HUD), same colours as the title screen's
            ppu::pal::WriteFromBuffer(ppu::SPRITE_0 + 1, SIZED_OBJ(maryColors));
            ppu::WriteFromBufferToNameTable({static_cast<u16>(video::viewport_tx() - ::tech::nes_str::length(msg_mary)), 0}, SIZED_STR(msg_mary), 0);
        });

        constexpr u8 coinUI[] = {windows::ui + blob::ui::hud_coin, windows::ui + (blob::ui::font + 0), windows::ui + (blob::ui::font + 0)};
        ppu::WriteFromBufferToNameTable({static_cast<u16>(video::viewport_tx() - sizeof(coinUI)), 1}, SIZED_OBJ(coinUI), 0);

        mmc3::CallInBlock<level_code_tag>([] {
            PopulateNameTableColumns(static_cast<u16>(2 + video::viewport_tx()));
        });

        suppressNmiScroll = false;

        edgeRAbs    = (1 + viewport_mx()) * levelHeight;
        edgeL    = { TileData };
        dynEdgeL = { DynLengths, DynData, 0 }; // dyn backward edge, lockstep w/ edgeL

        ppu::SetScroll({0, 0});


#ifdef TARGET_NES
        const bool playMusic = gen::options::audioCanvas::disableMusic::Enabled();
#else
        constexpr bool playMusic = true;
#endif
        if (playMusic) {
            mmc3::CallPairedBlock<audio_tag, audio_data_tag>([] {
                audio::TrackPlay(0);
            });
        }

        mmc3::CallInBlock<level_code_tag>([] {
            ColMapSeed(0, { TileData }, { DynLengths, DynData, 0 });
        });

        mmc3::CallInBlock<actor_tag>([] {
            player1.Start();
#ifdef PLAYER2_SUPPORTED
            player2.Start();
#endif
        });
        mmc3::Call<level::FeedOAM>();
        oam::RefreshSprites(OAMBuffer);   /* seed the first frame's sprite snapshot */

        video::WaitForPresent();
        ppu::EnableRendering(ppu::ctrl::SPRITE_SIZE | ppu::ctrl::SPRITE_ADDR, ppu::mask::BG_L | ppu::mask::SPRITE_L);

        apu::DisableFrameIRQ();
        apu::DisableDMCIRQ();
    }

    LEVEL_CODE void main() {
        InColdBank(EnterLevelSetup);
        irq::EnableInterrupts();
        while (!quit) {
            input::PollControllers(&port1, &port2);
            if (port1 & input::START && (port1 ^ lastPort1) & input::START) {
                paused = !paused;
            }

            mmc3::Call<level::UpdateActors>();
            ColMapTrack(cameraX >> 4);
            mmc3::CallPairedBlock<audio_tag, audio_data_tag>([] { audio::Update(); });

            mmc3::Call<level::FeedOAM>();

            video::WaitForPresent();
        }
    }

    NI ACTORS void FeedOAM() {
#ifdef PLAYER2_SUPPORTED
        const u8 shuffleBegin = multiplayer ? 4 : 2;
#else
        const u8 shuffleBegin = 2;
#endif

        static_assert(gen::mary_standing::kWidth == 2 && gen::mary_standing::kHeight == 1
                   && gen::mary_walking::kWidth  == 2 && gen::mary_walking::kHeight  == 1,
            "FeedOAM mirrors the player by swapping a pair of slots, which assumes a "
            "2x1 metasprite -- a wider or taller one needs row*kWidth + (kWidth-1-col)");

        constexpr u8 kFlipH = 0x40;

        oam::PopulateFromProvider(OAMBuffer, 1, oam::y, SpriteY, kPlayerSprites);
        oam::PopulateFromProvider(OAMBuffer, 1, oam::x, SpriteX, kPlayerSprites);

        {
            oam::sprite_t* const pair  = &OAMBuffer[1];
            const u8*      const frame = player1.actor.animationFrame;
            if (player1.actor.horizontalFacingDir) {
                pair[0].tile = frame[1];
                pair[1].tile = frame[0];
                pair[0].attributes = kFlipH;
                pair[1].attributes = kFlipH;
            } else {
                pair[0].tile = frame[0];
                pair[1].tile = frame[1];
                pair[0].attributes = 0;
                pair[1].attributes = 0;
            }
        }

#ifdef PLAYER2_SUPPORTED
        if (multiplayer) {
            oam::PopulateFromProvider(OAMBuffer, 1 + kPlayerSprites, oam::y, SpriteY2, kPlayerSprites);
            oam::PopulateFromProvider(OAMBuffer, 1 + kPlayerSprites, oam::x, SpriteX2, kPlayerSprites);
            oam::sprite_t* const pair  = &OAMBuffer[1 + kPlayerSprites];
            const u8*      const frame = player2.actor.animationFrame;
            if (player2.actor.horizontalFacingDir) {
                pair[0].tile = frame[1];
                pair[1].tile = frame[0];
                pair[0].attributes = kFlipH;
                pair[1].attributes = kFlipH;
            } else {
                pair[0].tile = frame[0];
                pair[1].tile = frame[1];
                pair[0].attributes = 0;
                pair[1].attributes = 0;
            }
        }
#endif
    }

    FIXED void nmi_handler() {
        lastPort1 = port1; lastPort2 = port2;
        oam::RefreshSprites(OAMBuffer);

        rasterCameraX = cameraX;
#ifdef PNES_WINDOWED_PANEL
        rasterPlayerY = player1.actor.screen.y;
        hudScroll = ppu::CartesianToScroll(
            {rasterCameraX, static_cast<u16>(kHudSplitRow + VerticalWindow(rasterPlayerY))});
#else
        hudScroll = ppu::CartesianToScroll({rasterCameraX, kHudSplitRow});
#endif

        using enum eLevelStreamCommands;
        if (levelStreamCommand & eLevelStreamCommands::STREAM_LEVEL_DONE) SHADOW(ppu::PPUMASK) {
            ppu::PPUMASK = 0;
            if (levelStreamCommand & eLevelStreamCommands::STREAM_LEVEL_RIGHT) {
                ppu::WriteFromBufferToNameTable({static_cast<u16>((lastXWorldSpace >> 3) + video::viewport_tx() + 0), 2}, TileBuffer, 28, 1);
                ppu::WriteFromBufferToNameTable({static_cast<u16>((lastXWorldSpace >> 3) + video::viewport_tx() + 1), 2}, TileBuffer + 28, 28, 1);
                ppu::WriteFromBufferToAttributeTable({static_cast<u16>(((lastXWorldSpace >> 3) + video::viewport_tx()) & ~3), 2}, AttributeBuffer, 8, 1);
            } else {
                ppu::WriteFromBufferToNameTable({static_cast<u16>((lastXWorldSpace >> 3) - 1), 2}, TileBuffer, 28, 1);
                ppu::WriteFromBufferToNameTable({static_cast<u16>((lastXWorldSpace >> 3) - 2), 2}, TileBuffer + 28, 28, 1);
                ppu::WriteFromBufferToAttributeTable({static_cast<u16>(((lastXWorldSpace >> 3) - 2) & ~3), 2}, AttributeBuffer, 8, 1);
            }
        }

        if (CoinVramLen) SHADOW(ppu::PPUMASK) {
            ppu::PPUMASK = 0;
#ifdef TARGET_NES
            ppu::PPUCTRL = ppu::PPUCTRL & ~ppu::ctrl::POLARITY;
            for (u8 i = 0; i < CoinVramLen; i += kCoinVramRec)
                CoinWritePair((CoinVram[i] << 8) | CoinVram[i + 1],
                              CoinVram[i + 2], CoinVram[i + 3]);
#else
            for (u8 i = 0; i < CoinVramLen; i += kCoinVramRec)
                ppu::WriteSingleToNameTable((CoinVram[i] << 8) | CoinVram[i + 1],
                                            CoinVram[i + 2]);
#endif
        }
        CoinVramReset();

        if (!suppressNmiScroll) ppu::SetScroll({0, 0});
        if (levelStreamCommand & STREAM_LEVEL_DONE) {
            levelStreamCommand = {};
        }
        mmc3::SwitchCHRBank<mmc3::BankOf<blob::ui::__lma>()>(mmc3::chr3Control);   // R3, PPU $0400

#ifdef TARGET_NDS
        nextBlankRow = 0;
#endif
        mmc3::ScheduleScanlineIRQ(HudSplitMMC3(), {0, kHudSplitRow});
    }

    FIXED void irq_handler() {
        mmc3::AcknowledgeScanlineIRQ();
#ifdef TARGET_NDS
        if (nextBlankRow) {
            tech::SpinWait(HUDSplitDelay());
            ApplyLowerPanelBlank();
            return;
        }
#endif
        ApplyHudSplit();
    }

    static oam::oam_t Clear(const u16) {
        return ParkedSpriteY();
    }

    static oam::oam_t Zero(const u16) {
        return 0;
    }

#ifdef PNES_WINDOWED_PANEL
    static oam::oam_t SpriteY(const u16 i)  { return WindowedSpriteY(AdjustSpriteY(&player1.actor, i), player1.actor.screen.y); }
#else
    static oam::oam_t SpriteY(const u16 i)  { return AdjustSpriteY(&player1.actor, i); }
#endif
    static oam::oam_t SpriteX(const u16 i)  { return AdjustSpriteX(&player1.actor, i); }
#ifdef PLAYER2_SUPPORTED
#ifdef PNES_WINDOWED_PANEL
    static oam::oam_t SpriteY2(const u16 i) { return WindowedSpriteY(AdjustSpriteY(&player2.actor, i), player1.actor.screen.y); }
#else
    static oam::oam_t SpriteY2(const u16 i) { return AdjustSpriteY(&player2.actor, i); }
#endif
    static oam::oam_t SpriteX2(const u16 i) { return AdjustSpriteX(&player2.actor, i); }
#endif

    static i16 ClampRow(const u16 y) {
        if (y & 0x8000) return 0;
        const i16 r = static_cast<i16>(y >> 4) - kHudRows;
        if (r < 0) return 0;
        return r < levelHeight ? r : levelHeight - 1;
    }

    static FIXED void ApplyHudSplit() {
        ppu::StageScroll(hudScroll);
        tech::SpinWait(HUDSplitDelay());
        ppu::LatchScroll(hudScroll);

        mmc3::SwitchCHRBank<mmc3::BankOf<blob::staticLevelTiles::__lma>()>(mmc3::chr3Control);   // R3, PPU $0400

#ifdef TARGET_NDS
        nextBlankRow = kLowerPanelRow;
        mmc3::ScheduleScanlineIRQ(0, {0, kLowerPanelRow});
#endif
    }

#ifdef TARGET_NDS
    static FIXED void ApplyLowerPanelBlank() {
        ppu::SetScroll({kBlankPlaneX, kBlankPlaneY});

        const u16 next = static_cast<u16>(nextBlankRow + kBlankRows);
        if (next < video::viewport_py()) {
            nextBlankRow = next;
            mmc3::ScheduleScanlineIRQ(0, {0, next});
        } else {
            nextBlankRow = 0;
        }
    }
#endif
}
