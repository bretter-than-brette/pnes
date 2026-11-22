#include "title.hpp"

#ifndef GEN_TARGET_DIR
#error "GEN_TARGET_DIR is not set -- add it to this target's CMakeLists.txt branch."
#endif
#include STRCAT(../../gen/title/GEN_TARGET_DIR/title.hpp)
#include STRCAT(../../gen/gameOptions/GEN_TARGET_DIR/gameOptions.hpp)
#include "../../ui/impl/buttons.hpp"
#include "../../ui/impl/sliders.hpp"
#include "../../ui/impl/singleChoice.hpp"
#include "../../ui/impl/canvas.hpp"
#include "../../ui/impl/sounds.hpp"
#include "../main.hpp"
#include "pnes/logger.hpp"
#include "../banks.hpp"
#include "../graphics/colours.hpp"
#include "../graphics/strings.hpp"
#include "../../chr/chr.h"
#include "../graphics/windows.hpp"
#include "../../animations/gen/mary_standing.hpp"
#include "level.hpp"
#include "level/levels.hpp"
#include "pregame.hpp"
#include "pnes/mappers/mmc3.hpp"
#include "pnes/extras/ui/text.hpp"

namespace title {
    static_assert(gen::title::TitleOptions::nOptions == End + 1,
                  "title.uis and title.hpp's titleOptions enum disagree on the menu's options");

    static atomic bool playModeActive = false;
    static atomic bool optionsLocked = false;

    static void (*pOptionsPass)(u8) = gen::options::videoCanvas::Pass;

    static atomic bool canvasEntered = false;

    static atomic u8 canvasInput = 0;

    // The frame's click for the options page: posted here, spent in vblank by
    // ::nmi_handler_locked, because a component's Click() repaints itself and a
    // nametable write is only legal there. Three scalars rather than an
    // `atomic vec2`, since a volatile vec2 can only be moved field by field --
    // and a pending flag rather than a sentinel, (0,0) being a real tile.
    static atomic u16  canvasClickX = 0;
    static atomic u16  canvasClickY = 0;
    static atomic bool canvasClickPending = false;

    // A press strobe gives one step per press, which is useless for walking a
    // slider's handle, so the payload a Canvas gets carries a repeated L/R.
    // Two constants: the delay has to outlast how long a player holds a button
    // they meant to tap once, while the tick is how fast the handle then walks.
    // The column-level LEFT/RIGHT stays on the raw strobe, or holding a
    // direction would walk through whole columns.
    constexpr u8 kCanvasRepeatBits  = input::LEFT | input::RIGHT;
    constexpr u8 kCanvasRepeatDelay = 16;
    constexpr u8 kCanvasRepeatTick  = 8;

    static u8   canvasRepeatHeld  = 0;
    static u8   canvasRepeatFrame = 0;
    static bool canvasRepeating   = false;

    constexpr u8 kPlayerSprites = static_cast<u8>(gen::mary_standing::kSize) * 2;

    constexpr bool StandingIsInSpriteTable() {
        for (u16 i = 0; i < gen::mary_standing::kSize; i++) {
            if ((gen::mary_standing::Get()[i] & 1) == 0) return false;
        }
        return true;
    }
    static_assert(StandingIsInSpriteTable(),
                  "the standing pose was exported against a frame in the $0000 pattern table, "
                  "but title mode's 8x8 sprites read $1000 (ppu::ctrl::SPRITE_ADDR)");

    constexpr u8 kPlayerOneSlot = 0;
    constexpr u8 kPlayerTwoSlot = kPlayerOneSlot + kPlayerSprites;

#ifdef PLAYER2_SUPPORTED
    constexpr u8 kSelectorSlot = kPlayerTwoSlot + kPlayerSprites;
#else
    constexpr u8 kSelectorSlot = kPlayerOneSlot + kPlayerSprites;
#endif
    constexpr u8 kCornerSlot = kSelectorSlot + 1;
    static_assert(kCornerSlot + ui::impl::kCorners <= 64,
                  "the title screen's OAM reservations run past the sprite table");

    constexpr oam::oam_t kPlayerOneX = 32;
    constexpr oam::oam_t kPlayerTwoX = kPlayerOneX + gen::mary_standing::kWidth * 8 * 2;

    static_assert(gen::mary_standing::kHeight == 1,
                  "the title's standing pose is more than one sprite tall: its rows need "
                  "offsetting by the sprite height, which the Y placement here does not do");

    static void DrawTitleSelector() {
        namespace menu = gen::title::TitleOptions;
        menu::DrawChange(menu::option, menu::option);
    }

    static void DrawPlayModeSelector() {
        namespace menu = gen::gameOptions::gameOptions;
        menu::DrawChange(menu::option, menu::option);
    }

    static oam::oam_t Clear(u16 _);
    static oam::oam_t Zero(u16 _);
    static oam::oam_t StandingTile(u16 i);
    static NI void DrawLevelPreview();
    static void PlaceStandingSprites();
    static void ParkStandingSprites();
    static void nmi_handler_drawPlayMode();
    static void nmi_handler_drawMenu();
    static void BuildPaletteFadePhases();
    static void nmi_handler_fadeToOptions();
    static void nmi_handler_lockOptions();
    static void nmi_handler_lockAnim();
    static void nmi_handler_locked();
    static void nmi_handler_lockAnimReverse();
    static void nmi_handler_fadeFromOptions();
#if TARGET_GC
    static void ApplyWidescreenChange(bool on);
#endif

    static u16 kMenuNT;

    static u16 maryGroundNtRow;

    static u16 lockAnimScanline;

    constexpr u16 kScanlinesPerFrame = 4;

    struct FadeRun { u8 offset; u8 count; };
    constexpr FadeRun kFadeEntries[] = {
        {ppu::BG_0,         4}, // BG_0 sub-palette, incl. the shared backdrop
        {ppu::BG_1 + 1,     3}, // BG_1 colours 1-3
        {ppu::SPRITE_0 + 1, 3}, // Mary's sprite colours
    };

    constexpr u8 kFadePhaseCount = 5;

    static u8 paletteFadePhases[kFadePhaseCount][4 + 3 + 3];

    static u8 fadePhase;

    constexpr u8 kFadeStepFrames = 2;

    static u8 fadeFrameCounter;

    constexpr u8 kMenuBandRows = 6;

    constexpr bool kUiOnOwnPanel = video::panel_count() > 1;

#if defined(TARGET_NDS) || defined(TARGET_CTR)
    constexpr bool kUiBankSplit = kUiOnOwnPanel;
#else
    constexpr bool kUiBankSplit = false;
#endif

    static u8 SplitRow() {
        if (video::panel_ty(0) < 30) return static_cast<u8>(video::panel_ty(0) - kMenuBandRows);
        return (((viewport_my() + 1) >> 1) - 2) << 2;
    }

    static u16 PreviewScrollYForPixels(const u16 splitPixelRow) {
        if constexpr (kUiOnOwnPanel) {
            (void)splitPixelRow;
            return static_cast<u16>(240 - video::panel_rect(0).h);
        }
        const u16 ntHeight = video::viewport_py() < 240 ? 240 : video::viewport_py();
        return ntHeight - splitPixelRow;
    }

    static u16 PreviewScrollYFor(const u8 splitRow) {
        return PreviewScrollYForPixels(static_cast<u16>(splitRow) << 3);
    }

    static u16 PreviewScrollY() {
        return PreviewScrollYFor(SplitRow());
    }

    // The absolute plane Y the menu band begins at, and so the Y ApplySplit
    // writes at the raster split: SetScroll's Y names the plane row sourced AT
    // that scanline, so there is nothing to add for the rows behind it. Zero
    // because both scenes sit in nametable quadrant 1.
    constexpr u16 kMenuBandOriginPy = 0;

    // The scroll the menu band is drawn under, not the frame's top-of-screen
    // one, which is what converts a plane coordinate to a screen position.
    //
    // Negative on a single-screen target: the band's plane rows start at 0 but
    // its scanlines start at the split. Returned as the u16 that wraps to it,
    // which is exactly what PlaceSelector's own subtraction undoes -- and why it
    // cannot be folded into the plane height the way SetScroll folds its own.
    static vec2<u16> MenuBandScroll() {
        if constexpr (kUiOnOwnPanel) return {0, PreviewScrollY()};
        return {static_cast<u16>(kMenuNT << 3),
                static_cast<u16>(kMenuBandOriginPy - (static_cast<u16>(SplitRow()) << 3))};
    }

    AI inline u8 SplitDelay()   { return ::video::Is50Hz() ? 58 : 88; }
    static void ApplySplit();

    AI inline u8 SplitLatency() { return ::video::Is50Hz() ? 5 : 4; }

    static void ArmSplitIRQForPixels(const u16 splitPixelRow) {
        if constexpr (kUiOnOwnPanel && !kUiBankSplit) {
            (void)splitPixelRow;
            return;
        }
        const u16 row = kUiBankSplit ? static_cast<u16>(video::panel_rect(0).h)
                                     : splitPixelRow;
        const u8 splitReload = row > SplitLatency()
            ? static_cast<u8>(row - SplitLatency()) : 0;
        mmc3::ScheduleScanlineIRQ(splitReload, {0, row});
    }

    static void ArmSplitIRQFor(const u8 splitRow) {
        ArmSplitIRQForPixels(static_cast<u16>(splitRow) << 3);
    }

    static void DrawOptionsPanel() {
        gen::options::optionsTitle::Draw();
        gen::options::superMarySistersText::Draw();
        gen::options::videoOptions::Draw();
        gen::options::audioText::Draw();

        gen::options::videoCanvas::DrawComponents();
        gen::options::audioCanvas::DrawComponents();
    }

    static bool AudioCanvasSelected() {
        return pOptionsPass == gen::options::audioCanvas::Pass;
    }

    static Rect SelectedCanvasRect() {
        if (AudioCanvasSelected()) {
            return gen::options::audioCanvas::audioCanvasRect::rect;
        }
        return gen::options::videoCanvas::videoCanvasRect::rect;
    }

    static void SelectFocusedComponent() {
        if (AudioCanvasSelected()) {
            namespace ac = gen::options::audioCanvas;
            const Rect r = *ac::components[ac::index].rect;
            ac::DrawComponentSelection(r);
            return;
        }
        namespace vc = gen::options::videoCanvas;
        const Rect r = *vc::components[vc::index].rect;
        vc::DrawComponentSelection(r);
    }

    static u8 CanvasRepeat(const u8 inputs, const u8 pressed) {
        if (const u8 edge = static_cast<u8>(pressed & kCanvasRepeatBits); edge) {
            canvasRepeatHeld  = edge;
            canvasRepeatFrame = 0;
            canvasRepeating   = false;   // the delay is served before any tick
            return edge;                 // the strobe itself, passed straight through
        }

        const u8 held = static_cast<u8>(inputs & canvasRepeatHeld);
        if (!held) {
            canvasRepeatHeld  = 0;
            canvasRepeatFrame = 0;
            canvasRepeating   = false;
            return 0;
        }

        if (++canvasRepeatFrame < (canvasRepeating ? kCanvasRepeatTick
                                                   : kCanvasRepeatDelay)) return 0;
        canvasRepeatFrame = 0;
        canvasRepeating   = true;
        return held;
    }

#if TARGET_GC
    static bool WidescreenBit() {
        return gen::options::videoCanvas::enableWidescreenButton::Enabled();
    }
#endif

    static constexpr u8 kButtonStateBytes =
        static_cast<u8>(sizeof(gen::options::button_state) /
                        sizeof(gen::options::button_state[0]));

    static void ReadButtonState(u8 (&out)[kButtonStateBytes]) {
        for (u8 i = 0; i < kButtonStateBytes; i++) out[i] = gen::options::button_state[i];
    }

    static bool ButtonStateChanged(const u8 (&before)[kButtonStateBytes]) {
        for (u8 i = 0; i < kButtonStateBytes; i++) {
            if (before[i] != gen::options::button_state[i]) return true;
        }
        return false;
    }

    static void SpendCanvasInput() {
        const u8 payload = canvasInput;
        if (!payload) return;
        canvasInput = 0;
#if TARGET_GC
        const bool wideBefore = WidescreenBit();
#endif
        u8 buttonsBefore[kButtonStateBytes];
        ReadButtonState(buttonsBefore);

        pOptionsPass(payload);

        if (ButtonStateChanged(buttonsBefore)) ui::impl::PostSfx(sfx::toggle);
#if TARGET_GC
        if (const bool wideNow = WidescreenBit(); wideNow != wideBefore) {
            ApplyWidescreenChange(wideNow);
        }
#endif
    }

    // The pointer in the plane TILE space every generated rect is in. The
    // scroll comes from ui::impl::CanvasScroll rather than being recomputed, so
    // this resolves to the component the player can actually see under their
    // finger -- two copies of that scroll could disagree, one cannot.
    //
    // The add comes BEFORE the shift, which is what makes it exact: shifting
    // first quantizes the scroll separately and loses up to a whole tile.
    static vec2<u16> ClickTile() {
        const vec2<u16> px = input::click.Get();
        const vec2<u16> scroll = ui::impl::CanvasScroll();
        return { static_cast<u16>((px.x + scroll.x) >> 3),
                 static_cast<u16>((px.y + scroll.y) >> 3) };
    }

    static void EnterCanvasFromClick(void (* const pass)(u8)) {
        const bool switched = pOptionsPass != pass;
        pOptionsPass = pass;
        if (switched || !canvasEntered) {
            canvasEntered = true;
            SelectFocusedComponent();
        }
    }

    static void SpendCanvasClick() {
        if constexpr (!input::has_click) return;

        if (!canvasClickPending) return;
        canvasClickPending = false;

        const vec2<u16> at{canvasClickX, canvasClickY};
#if TARGET_GC
        const bool wideBefore = WidescreenBit();
#endif
        u8 buttonsBefore[kButtonStateBytes];
        ReadButtonState(buttonsBefore);

        if (gen::options::videoCanvas::PassClick(at)) {
            EnterCanvasFromClick(gen::options::videoCanvas::Pass);
        } else if (gen::options::audioCanvas::PassClick(at)) {
            EnterCanvasFromClick(gen::options::audioCanvas::Pass);
        }

        if (ButtonStateChanged(buttonsBefore)) ui::impl::PostSfx(sfx::toggle);
#if TARGET_GC
        if (const bool wideNow = WidescreenBit(); wideNow != wideBefore) {
            ApplyWidescreenChange(wideNow);
        }
#endif
    }

    static void ParkOptionsCorners() {
        ui::impl::ParkCorners();
    }

    static void PlaceOptionsCorners() {
        ui::impl::SelectComponent(SelectedCanvasRect());
    }

    static constexpr u16 kUiBandRow0 = 30;
    static void ClearUiBand() {
        for (u16 r = kUiBandRow0; r < video::viewport_ty(); r++)
            ppu::WriteRepeatedToNameTable({0, r}, (windows::ui + blob::ui::hud_whitespace),
                                          static_cast<u8>(kMenuNT), 0);
    }

    static void BankPreviewTiles() {
        mmc3::SwitchCHRBank<mmc3::BankOf<blob::staticLevelTiles::__lma>()>(mmc3::chr3Control);   // R3, PPU $0400
    }

    static void BankMenuTiles() {
        mmc3::SwitchCHRBank<mmc3::BankOf<blob::ui::__lma>()>(mmc3::chr3Control);   // R3, PPU $0400
    }


    // One frame of the audio engine and the UI effect waiting on it, under a
    // single bank pair. The engine has to be driven here at all because nothing
    // sounds until famistudio_update walks the stream; it costs a stopped song
    // nothing, which is why it is unconditional. The effect is played here
    // because this is where the engine is already mapped -- see
    // ui::impl::pendingSfx. Both in ONE CallPairedBlock, since the pair is most
    // of the cost. Nothing in the block may touch ordinary banked code.
    static void DriveAudio() {
        mmc3::CallPairedBlock<audio_tag, audio_data_tag>([] {
            audio::Update();
            if (const u8 queued = ui::impl::pendingSfx) {
                ui::impl::pendingSfx = 0;
                if (ui::impl::SfxEnabled()) {
                    audio::SfxPlay(static_cast<u8>(queued - 1), ui::impl::kSfxChannel,
                                   ui::impl::SfxLevel());
                }
            }
        });
    }


    TITLE NI void main() {
        log("title::main: entered");

#if TARGET_GC
        video::ogc::SetWidescreen(gen::options::videoCanvas::enableWidescreenButton::Enabled());
#endif

        kMenuNT = video::viewport_tx() < 32 ? 32 : video::viewport_tx();

        oam::PopulateFromProvider(OAMBuffer, 0, oam::y, Clear, 64);

        for (u8 i = 0; i < ui::impl::kCorners; i++) {
            OAMBuffer[kCornerSlot + i].tile       = ui::impl::kCornerTiles[i];
            OAMBuffer[kCornerSlot + i].attributes = ui::impl::kCornerAttributes;
        }
        pIRQ = irq_handler;
        pNMI = nmi_handler;
        log("title::main: pIRQ/pNMI set");

        mmc3::SwitchCHRBank<mmc3::BankOf<blob::dynamicLevelTiles::__lma>()>(mmc3::chr2Control);   // R2, PPU $0000
        mmc3::SwitchCHRBank<mmc3::BankOf<blob::staticLevelTiles::__lma>()>(mmc3::chr3Control);   // R3, PPU $0400
        mmc3::SwitchCHRBank<mmc3::BankOf<blob::ui::__lma>()>(mmc3::chr3Control);   // R3, PPU $0400
        mmc3::SwitchCHRBank<mmc3::BankOf<blob::normalSprites::__lma>()>(mmc3::chr0Control);   // R0, PPU $1000, 2 KiB

        static_assert(blob::normalSprites::__lma + blob::normalSprites::__size <= blob::ui::__lma &&
                      blob::dynamicLevelTiles::__lma + blob::dynamicLevelTiles::__size <= blob::ui::__lma &&
                      blob::staticLevelTiles::__lma + blob::staticLevelTiles::__size <= blob::ui::__lma,
                      "something is packed after ui, so R1's upper 1 KiB "
                      "($1C00-$1FFF) shows that art instead of padding: give "
                      "it an entry of its own, or re-point R1");
        mmc3::SwitchCHRBank<mmc3::BankOf<blob::ui::__lma>()>(mmc3::chr1Control);

        constexpr u32 padLma = blob::normalSprites::__lma + blob::normalSprites::__size;
        static_assert(padLma + mmc3::kChrBankSize <= blob::dynamicLevelTiles::__lma,
                      "R5 runs past normalSprites into the next entry: give "
                      "PPU $0C00-$0FFF an entry of its own, or re-point it");
        mmc3::SwitchCHRBank<mmc3::BankOf<padLma>()>(mmc3::chr5Control);
        mmc3::SetMirroring(false);
        ppu::Flush((windows::ui + blob::ui::hud_whitespace), 0xff);
        ppu::pal::WriteFromBuffer(13, titleScreenColours, 3);
        ppu::pal::WriteFromBuffer(ppu::SPRITE_1 + 1, titleScreenColours, 3);

        log("title::main: before DrawLevelPreview");
        DrawLevelPreview();
        log("title::main: after DrawLevelPreview");

        BuildPaletteFadePhases();

        // The loaded settings, lifted out from under Make(): every control on
        // this page IS a field of saveData.config, and every component's Make()
        // writes its authored default into that same field. The two cannot both
        // be in one byte, so the file's copy comes out first and goes back
        // after. A copy, because there is nowhere else the config exists.
        const SaveFile::Config loadedConfig = saveData.config;

        gen::options::Make();

        saveData.config = loadedConfig;

#ifndef TARGET_NES
        namespace ac = gen::options::audioCanvas;
        const Rect mr = ac::musicSlider::rect;
        const Rect sr = ac::SFXSlider::rect;

        // A stored handle position is not portable between track widths, and
        // the save holds the position rather than the proportion -- travel moves
        // with the GameCube's aspect and with a desktop window. Clamped rather
        // than rescaled, because with no record of what travel it meant the
        // nearest legal cell is all this can honestly recover.
        //
        // It is also what resolves "full volume" on a cold boot: pregame parks
        // both amounts at 0xFF -- the right-hand end, whatever that turns out to
        // be -- which nothing there could compute.
        const u8 mTravel = ui::impl::SliderTravel(mr.size.x);
        const u8 sTravel = ui::impl::SliderTravel(sr.size.x);
        if (mTravel && ac::musicSlider::amount >= mTravel) {
            ac::musicSlider::amount = static_cast<u8>(mTravel - 1);
        }
        if (sTravel && ac::SFXSlider::amount >= sTravel) {
            ac::SFXSlider::amount = static_cast<u8>(sTravel - 1);
        }

        audio::TrackVolume(ui::impl::SliderPercent(mr.size.x, ac::musicSlider::amount));
#endif

        if constexpr (!kUiOnOwnPanel) DrawOptionsPanel();

        InitTitleScreen();

        gen::title::Make();
        gen::gameOptions::Make();
        playModeActive = false;

        gen::title::TitleOptions::Draw();
        DrawTitleSelector();

        ppu::SetScroll({0, 0xff});
        ppu::EnableRendering(ppu::ctrl::SPRITE_ADDR | ppu::ctrl::GEN_NMI, ppu::mask::BG_L | ppu::mask::SPRITE_L);
        irq::EnableInterrupts();
        log("title::main: entering input loop");

        u8 seed1 = 0, seed2 = 0;
        input::PollControllers(&seed1, &seed2);
        u8 prevInputs = static_cast<u8>(seed1 | seed2);
        while (true) {
            u8 port1, port2;
            input::PollControllers(&port1, &port2);
            const u8 inputs  = port1 | port2;
            const u8 pressed = inputs & static_cast<u8>(~prevInputs); // strobe: only the frame a button goes down
            prevInputs = inputs;
            const u8 repeated = CanvasRepeat(inputs, pressed);

            if (!optionsLocked) {
                if (playModeActive) gen::gameOptions::gameOptions::Pass(pressed);
                else                gen::title::TitleOptions::Pass(pressed);

                if (pressed & input::A) {
                    ui::impl::PostSfx(sfx::select);

                    if (playModeActive) {
#ifdef PLAYER2_SUPPORTED
                        level::multiplayer = gen::gameOptions::gameOptions::option != 0;
#endif
                        ppu::PPUMASK = 0;
                        gameMode = eGameModes::Level;
                        DriveAudio();
                        return;
                    }

                    switch (gen::title::TitleOptions::option) {
                        case NewGame:
                        case Continue:
                            playModeActive = true;
                            pNMI = nmi_handler_drawPlayMode;
                            break;

                        case Options:
                            fadePhase = 0;
                            fadeFrameCounter = 0;
                            optionsLocked = true;
                            pNMI = nmi_handler_fadeToOptions;
                            break;

#if defined(TARGET_MACOS) || defined(TARGET_WINDOWS) || defined(TARGET_LINUX)
                        case Quit:
                            quit = true;
                            return;
#endif

                        default: ;
                    }
                }

                if (pressed & input::B && playModeActive) {
                    ui::impl::PostSfx(sfx::deselect);
                    playModeActive = false;
                    pNMI = nmi_handler_drawMenu;
                }
            } else if (pNMI == nmi_handler_locked) {
                // The options page, once it has settled: every branch below
                // resolves against the resting scroll, which is not where the
                // frame is until this handler has the screen.
                //
                // Two levels: LEFT/RIGHT pick a Canvas at the column level, and
                // inside one every press goes to its Pass(). That split is what
                // lets a vertical Canvas spend UP/DOWN on focus and pass
                // LEFT/RIGHT to a horizontal slider.
                //
                // A click is offered the page first, and only on a frame the
                // controller said nothing: a frame carrying both is a player
                // whose thumb and finger disagree, and a press is unambiguous
                // about which control it means. `repeated` is in the guard as
                // well as `pressed`, since a held direction produces no strobe
                // on most of its frames.
                bool clickPosted = false;
                if constexpr (input::has_click) {
                    if (!pressed && !repeated && input::click.Down()) {
                        const vec2<u16> at = ClickTile();
                        canvasClickX = at.x;
                        canvasClickY = at.y;
                        canvasClickPending = true;
                        clickPosted = true;
                    }
                }

                if (clickPosted) {
                } else if (!canvasEntered) {
                    if (pressed & (input::LEFT | input::RIGHT)) {
                        void (* const want)(u8) = (pressed & input::LEFT)
                                                      ? gen::options::videoCanvas::Pass
                                                      : gen::options::audioCanvas::Pass;
                        if (want != pOptionsPass) {
                            pOptionsPass = want;
                            ui::impl::PostSfx(sfx::selectnext);
                            PlaceOptionsCorners();
                        }
                    } else if (pressed & input::A) {
                        canvasEntered = true;
                        SelectFocusedComponent();
                    } else if (pressed & input::B) {
                        ui::impl::PostSfx(sfx::deselect);
#ifndef TARGET_NES
                        // The one place the page's settings are written out:
                        // every control writes straight into saveData.config, so
                        // leaving the page is the moment the file and the screen
                        // stop agreeing. pregame::SaveScreen runs its own
                        // RETRY/CONTINUE wheel if the device will not take it.
                        //
                        // No platform list here, and that is deliberate: gated
                        // to the GameCube, a Wii player could change anything on
                        // this page and nothing was ever written. Every target
                        // reaches this line and the backend it linked decides
                        // what happens.
                        //
                        // The corners go first, being screen-space sprites that
                        // would otherwise bracket a column no longer on screen.
                        // The return value is deliberately unused: the player has
                        // already answered that wheel, and refusing to leave
                        // would trap them here with nothing to save to.
                        ParkOptionsCorners();
                        (void)pregame::SaveScreen();

                        {
                            u8 back1 = 0, back2 = 0;
                            input::PollControllers(&back1, &back2);
                            prevInputs = static_cast<u8>(back1 | back2);
                        }
#endif
                        pNMI = nmi_handler_lockAnimReverse;
                    }
                } else if (pressed & input::B) {
                    ui::impl::PostSfx(sfx::deselect);
                    canvasEntered = false;
                    PlaceOptionsCorners();
                } else {
                    if (const u8 payload = static_cast<u8>(
                            (pressed & static_cast<u8>(~kCanvasRepeatBits)) | repeated); payload) {
                        canvasInput = payload;
                    }
                }
            }

            video::WaitForPresent();
            {
                static bool firstPresentLogged = false;
                if (!firstPresentLogged) {
                    firstPresentLogged = true;
                    log("title::main: first WaitForPresent returned");
                }
            }
            if (quit) return;

            DriveAudio();
        }
    }

    static void ArmSplitIRQ() {
        ArmSplitIRQFor(SplitRow());
    }

    void nmi_handler() {
        ui::impl::AnimateSelector();
        oam::RefreshSprites(OAMBuffer);

        ppu::SetScroll({0, PreviewScrollY()});
        BankPreviewTiles();

        ArmSplitIRQ();
    }

    static void nmi_handler_drawPlayMode() {
        gen::title::TitleOptions::Erase();
        gen::gameOptions::gameOptions::Draw();
        DrawPlayModeSelector();
        ppu::SetScroll({0, PreviewScrollY()});
        BankPreviewTiles();
        ArmSplitIRQ();

        pNMI = nmi_handler;
    }

    static void nmi_handler_drawMenu() {
        gen::gameOptions::gameOptions::Erase();
        gen::title::TitleOptions::Draw();
        DrawTitleSelector();
        ppu::SetScroll({0, PreviewScrollY()});
        BankPreviewTiles();
        ArmSplitIRQ();

        pNMI = nmi_handler;
    }

    static u8 FadeStep(const u8 colour) {
        if ((colour & 0xf0) == 0) return 0x1f;
        return static_cast<u8>(colour - 0x10);
    }

    static void BuildPaletteFadePhases() {
        CallLevelGraphics([] {
            u8* dst = paletteFadePhases[0];
            *dst++ = BGColours[0]; *dst++ = BGColours[1]; *dst++ = BGColours[2]; *dst++ = BGColours[3];
            *dst++ = BGColours[5]; *dst++ = BGColours[6]; *dst++ = BGColours[7];
            *dst++ = maryColors[0]; *dst++ = maryColors[1]; *dst++ = maryColors[2];
        });
        for (u8 phase = 1; phase < kFadePhaseCount; ++phase) {
            for (u8 i = 0; i < sizeof(paletteFadePhases[0]); ++i) {
                paletteFadePhases[phase][i] = FadeStep(paletteFadePhases[phase - 1][i]);
            }
        }
    }

    static void WritePaletteFadePhase(const u8 phase) {
        const u8* src = paletteFadePhases[phase];
        for (const FadeRun& run : kFadeEntries) {
            ppu::pal::WriteFromBuffer(run.offset, src, run.count);
            src += run.count;
        }
    }

    static bool StepPaletteFade() {
        if (fadePhase >= kFadePhaseCount - 1) return false;
        WritePaletteFadePhase(++fadePhase);
        return fadePhase < kFadePhaseCount - 1;
    }

    static bool StepPaletteFadeIn() {
        if (fadePhase == 0) return false;
        WritePaletteFadePhase(--fadePhase);
        return fadePhase != 0;
    }

#if TARGET_GC
    static void ApplyWidescreenChange(const bool on) {
        const u8 mask = ppu::PPUMASK;
        ppu::PPUMASK = 0;

        video::ogc::SetWidescreen(on);

        kMenuNT = video::viewport_tx() < 32 ? 32 : video::viewport_tx();

        ppu::Flush((windows::ui + blob::ui::hud_whitespace), 0xff);

        // Re-resolve every UI anchor through each scene's OWN top-level Make(),
        // not the two Canvas ones -- those reach only what is under a Canvas,
        // and this scene has four nodes outside them. Each Make() also zeroes
        // its scene's running state, which must not happen here: the display
        // changing shape is no reason to throw away the player's place in the
        // menu, so both scenes go through the same save/restore.
        namespace ac = gen::options::audioCanvas;
        const u8 musicPct = [] {
            const Rect r = ac::musicSlider::rect;
            return ui::impl::SliderPercent(r.size.x, ac::musicSlider::amount);
        }();
        const u8 sfxPct = [] {
            const Rect r = ac::SFXSlider::rect;
            return ui::impl::SliderPercent(r.size.x, ac::SFXSlider::amount);
        }();

        const u8 videoIndex  = gen::options::videoCanvas::index;
        const u8 audioIndex  = gen::options::audioCanvas::index;
        const u8 titleOption = gen::title::TitleOptions::option;
        const u8 playOption  = gen::gameOptions::gameOptions::option;

        const SaveFile::Config cfg = saveData.config;

        gen::options::Make();
        gen::title::Make();
        gen::gameOptions::Make();

        saveData.config = cfg;
        gen::options::videoCanvas::index  = videoIndex;
        gen::options::audioCanvas::index  = audioIndex;
        gen::title::TitleOptions::option  = titleOption;
        gen::gameOptions::gameOptions::option = playOption;

        {
            const Rect mr = ac::musicSlider::rect;
            const Rect sr = ac::SFXSlider::rect;
            ac::musicSlider::amount = ui::impl::SliderAmountFor(mr.size.x, musicPct);
            ac::SFXSlider::amount   = ui::impl::SliderAmountFor(sr.size.x, sfxPct);
            audio::TrackVolume(ui::impl::SliderPercent(mr.size.x, ac::musicSlider::amount));
        }

        // DrawLevelPreview is written for a cold start, so the two things it
        // does for one are undone here. It uploads the unfaded palettes, so the
        // fade goes back over the top -- the preview has to stay black behind
        // the page. And it places the characters on the ground, which is
        // precisely what this page must not show: they were parked once for the
        // whole page, and OAM is refreshed immediately after this returns.
        DrawLevelPreview();
        WritePaletteFadePhase(fadePhase);
        ParkStandingSprites();
        InitTitleScreen();
        DrawOptionsPanel();

        if (canvasEntered) SelectFocusedComponent();
        else               PlaceOptionsCorners();

        ppu::PPUMASK = mask;
    }
#endif

    static void nmi_handler_fadeToOptions() {
        ui::impl::AnimateSelector();
        oam::RefreshSprites(OAMBuffer);
        mmc3::AcknowledgeScanlineIRQ();

        bool stillFading = false;
        if constexpr (!kUiOnOwnPanel) {
            stillFading = true;
            if (++fadeFrameCounter >= kFadeStepFrames) {
                fadeFrameCounter = 0;
                stillFading = StepPaletteFade();
            }
        }

        ppu::SetScroll({0, PreviewScrollY()});
        BankPreviewTiles();

        ArmSplitIRQ();

        if (!stillFading) {
            if constexpr (!kUiOnOwnPanel) ParkStandingSprites();
            pNMI = nmi_handler_lockOptions;
        }
    }

    static void nmi_handler_lockOptions() {
        ui::impl::ParkSelector();
        gen::title::TitleOptions::Erase();
        mmc3::AcknowledgeScanlineIRQ();
        oam::RefreshSprites(OAMBuffer);

        if constexpr (kUiOnOwnPanel) {
            ClearUiBand();
            DrawOptionsPanel();
        }

        lockAnimScanline = static_cast<u16>(SplitRow()) << 3;
        ppu::SetScroll({0, PreviewScrollY()});
        BankPreviewTiles();

        ArmSplitIRQ();

        if constexpr (kUiOnOwnPanel) {
            lockAnimScanline = 0;
            PlaceOptionsCorners();
            pNMI = nmi_handler_locked;
            return;
        }

        pNMI = nmi_handler_lockAnim;
    }

    static void nmi_handler_lockAnim() {
        ui::impl::AnimateSelector();
        oam::RefreshSprites(OAMBuffer);

        lockAnimScanline = lockAnimScanline > kScanlinesPerFrame
            ? static_cast<u16>(lockAnimScanline - kScanlinesPerFrame) : 0;

        ppu::SetScroll({0, PreviewScrollYForPixels(lockAnimScanline)});
        BankMenuTiles();

        if (lockAnimScanline == 0) {
            PlaceOptionsCorners();
            pNMI = nmi_handler_locked;
        }
    }

    static void nmi_handler_locked() {
        ui::impl::AnimateSelector();
        ui::impl::AnimateCorners();
        SpendCanvasInput();
        SpendCanvasClick();
        oam::RefreshSprites(OAMBuffer);

        if constexpr (kUiOnOwnPanel) {
            ppu::SetScroll({0, PreviewScrollY()});
            BankPreviewTiles();
            ArmSplitIRQ();
            return;
        }

        ppu::SetScroll({0, PreviewScrollYForPixels(0)});

        BankMenuTiles();
    }

    static void nmi_handler_lockAnimReverse() {
        ui::impl::AnimateSelector();
        ParkOptionsCorners();
        oam::RefreshSprites(OAMBuffer);

        const u16 target = static_cast<u16>(SplitRow()) << 3;
        const u16 next = static_cast<u16>(lockAnimScanline + kScanlinesPerFrame);
        lockAnimScanline = next < target ? next : target;
        if constexpr (kUiOnOwnPanel) lockAnimScanline = target;

        if (lockAnimScanline >= target) {
            if constexpr (kUiOnOwnPanel) {
                ClearUiBand();
                gen::title::GameTitle::Draw();
            }
            gen::title::TitleOptions::Draw();
            DrawTitleSelector();
            ppu::SetScroll({0, PreviewScrollY()});
            BankPreviewTiles();
            ArmSplitIRQ();

            fadeFrameCounter = 0;
            if constexpr (!kUiOnOwnPanel) PlaceStandingSprites();
            pNMI = nmi_handler_fadeFromOptions;
            return;
        }

        ppu::SetScroll({0, PreviewScrollYForPixels(lockAnimScanline)});
        BankMenuTiles();
    }

    static void nmi_handler_fadeFromOptions() {
        ui::impl::AnimateSelector();
        oam::RefreshSprites(OAMBuffer);

        bool stillFading = false;
        if constexpr (!kUiOnOwnPanel) {
            stillFading = true;
            if (++fadeFrameCounter >= kFadeStepFrames) {
                fadeFrameCounter = 0;
                stillFading = StepPaletteFadeIn();
            }
        }

        ppu::SetScroll({0, PreviewScrollY()});
        BankPreviewTiles();
        ArmSplitIRQ();

        if (!stillFading) {
            optionsLocked = false;
            pNMI = nmi_handler;
        }
    }

    void irq_handler() {
        mmc3::AcknowledgeScanlineIRQ();
        tech::SpinWait(SplitDelay());
        ApplySplit();
    }

    static void ApplySplit() {
        if constexpr (kUiBankSplit) {
            BankMenuTiles();
            return;
        }
        ppu::SetScroll({static_cast<u16>(kMenuNT << 3), kMenuBandOriginPy});

        BankMenuTiles();
    }

    void InitTitleScreen() {
        gen::title::GameTitle::Draw();
    }

    static oam::oam_t Clear(const u16) {
        return ParkedSpriteY();
    }

    static oam::oam_t Zero(const u16) {
        return 0;
    }

    static oam::oam_t StandingTile(const u16 i) {
        const u8 pair = gen::mary_standing::Get()[i / 2] & 0xFE;
        return static_cast<oam::oam_t>(pair + (i % 2));
    }

    static void PlaceStandingX(const u8 slot, const oam::oam_t x) {
        for (u16 i = 0; i < kPlayerSprites; i++) {
            OAMBuffer[slot + i].x = static_cast<oam::oam_t>(x + (i / 2) * 8);
        }
    }

    static void PlaceStandingY(const u8 slot, const oam::oam_t y) {
        for (u16 i = 0; i < kPlayerSprites; i++) {
            OAMBuffer[slot + i].y = static_cast<oam::oam_t>(y + (i % 2) * 8);
        }
    }

    static NI void DrawLevelPreview() {
        using namespace level;

        if (const bool loaded = mmc3::CallInBlock<level_code_tag>([] { return LoadLevel(0); }); !loaded) return;

        constexpr u16 tyBase = kHudRows * 2;

        CallLevelGraphics([] {
            ppu::WriteFromBufferToNameTable({static_cast<u16>(video::viewport_tx() - ::tech::nes_str::length(msg_mary)), 0}, SIZED_STR(msg_mary), 0);
        });
        constexpr u8 coinUI[] = {windows::ui + blob::ui::hud_coin, windows::ui + (blob::ui::font + 0), windows::ui + (blob::ui::font + 0)};
        ppu::WriteFromBufferToNameTable({static_cast<u16>(video::viewport_tx() - sizeof(coinUI)), 1}, SIZED_OBJ(coinUI), 0);

        const u16 wantMt    = static_cast<u16>((video::viewport_tx() + 1) >> 1);
        const u16 colsWide  = nColumns < wantMt ? nColumns : wantMt;

        const u16 previewTileCols = static_cast<u16>(colsWide * 2);
        mmc3::CallInBlock<level_code_tag>([previewTileCols] {
            PopulateNameTableColumns(previewTileCols);
        });

        CallLevelGraphics([] {
            oam::PopulateFromProvider(OAMBuffer, kPlayerOneSlot, oam::tile,
                                      StandingTile, kPlayerSprites);
            oam::PopulateFromProvider(OAMBuffer, kPlayerOneSlot, oam::attributes,
                                      Zero, kPlayerSprites);
#ifdef PLAYER2_SUPPORTED
            oam::PopulateFromProvider(OAMBuffer, kPlayerTwoSlot, oam::tile,
                                      StandingTile, kPlayerSprites);
            oam::PopulateFromProvider(OAMBuffer, kPlayerTwoSlot, oam::attributes,
                                      Zero, kPlayerSprites);
#endif
            ppu::pal::WriteFromBuffer(ppu::SPRITE_0 + 1, SIZED_OBJ(maryColors));
            ppu::pal::WriteFromBuffer(0, BGColours, 4);
            ppu::pal::WriteFromBuffer(5, BGColours + 5, 3);
        });

        maryGroundNtRow = tyBase * 8 + (levelHeight - 2) * 16;
        PlaceStandingX(kPlayerOneSlot, kPlayerOneX);
#ifdef PLAYER2_SUPPORTED
        PlaceStandingX(kPlayerTwoSlot, kPlayerTwoX);
#endif
        PlaceStandingSprites();
    }

    static void PlaceStandingSprites() {
        const u16 spriteScroll = (!kUiOnOwnPanel && video::panel_ty(0) < 30)
            ? 0
            : PreviewScrollY();
        const i16 rawFeetY    = static_cast<i16>(maryGroundNtRow) - 16 - static_cast<i16>(spriteScroll);
        const auto feetY      = rawFeetY < 0 ? ParkedSpriteY() : static_cast<oam::oam_t>(rawFeetY);
        PlaceStandingY(kPlayerOneSlot, feetY);
#ifdef PLAYER2_SUPPORTED
        PlaceStandingY(kPlayerTwoSlot, feetY);
#endif
    }

    static void ParkStandingSprites() {
        PlaceStandingY(kPlayerOneSlot, ParkedSpriteY());
#ifdef PLAYER2_SUPPORTED
        PlaceStandingY(kPlayerTwoSlot, ParkedSpriteY());
#endif
    }

}

namespace ui::impl {
    u8 SelectorSlot() { return title::kSelectorSlot; }
    vec2<u16> SelectorScroll() { return title::MenuBandScroll(); }
    u8 CornerSlot() { return title::kCornerSlot; }

    vec2<u16> CanvasScroll() { return {0, title::PreviewScrollYForPixels(0)}; }

    bool SfxEnabled() {
#ifdef TARGET_NES
        return gen::options::audioCanvas::disableSFX::Enabled();
#else
        return true;
#endif
    }

    u8 SfxLevel() {
#ifdef TARGET_NES
        return 0;
#else
        namespace ac = gen::options::audioCanvas;
        const Rect r = ac::SFXSlider::rect;
        if (!r.size.x) return ac::SFXSlider::amount ? 255 : 0;
        return SliderCoefficient(r.size.x, ac::SFXSlider::amount);
#endif
    }
}