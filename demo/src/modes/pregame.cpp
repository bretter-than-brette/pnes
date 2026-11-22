#include "pregame.hpp"
#include "../save.hpp"
#include <pnes/save.hpp>

// NOT AVAILABLE ON NES (llvm-mos is freestanding here). Nothing in this file
// needs it any more -- ::PopulateSaveData and the config defaults moved to
// demo/src/save.cpp -- but the drawn flow below still does.
#ifndef TARGET_NES
#include <cstring>
#endif

#include <intsh>

#include "../main.hpp"   // ::gameMode, eGameModes -- this mode names its successor
#include "../graphics/colours.hpp"
#include "../graphics/windows.hpp"
#include "../../chr/chr.h"
#include "../../ui/impl/singleChoice.hpp"
#include "../../ui/impl/sounds.hpp"

#include "pnes/audio.hpp"
#include "pnes/extras/ui/text.hpp"
#include "pnes/input.hpp"
#include "pnes/interrupts.hpp"
#include "pnes/logger.hpp"
#include "pnes/mappers/mmc3.hpp"
#include "pnes/video.hpp"
#include "../../ui/impl/buttons.hpp"
using namespace br0::intsh;

namespace pregame {

#ifdef TARGET_NES
/**
 * @brief The NES's whole save flow: load it, check it, default it if it is bad.
 *
 * NO SCREEN, NO VECTORS, NO FRAMES. This draws nothing, installs no NMI or IRQ
 * handler and never pumps a frame, which is what lets it run before any mode
 * has set a scene up -- and why main.cpp dispatches it without the
 * ::mmc3::CallInBlock the drawn modes need. The drawn flow exists because a
 * device can be missing and a file can be absent; neither is reachable here.
 *
 * THE LOAD CANNOT FAIL, so nothing branches on its error. The save IS the cart
 * (src/nes/mappers/mmc3.cpp): every call there answers `done` true and
 * save::found on its first turn, so the loop below runs exactly once and folds
 * away -- those backends are ::AI, so at -O2 there is no call left at all. With
 * SAVE_FILE unset it is the weak backend answering instead (src/all/save.cpp),
 * which reports no_device and leaves ::saveData alone. Both end in the same
 * place, because what matters is not what the medium said but whether what came
 * back MEANS anything.
 *
 * WHICH IS ::ValidateSave'S QUESTION, and the only one this path asks. A fresh
 * battery reads as garbage and a cart written by an older build reads as the
 * wrong version; both are "there is no save here yet", so the defaults go in
 * and the next boot validates clean. The player is told none of it -- there is
 * nothing for them to decide and nothing they could have done differently.
 */
void main() {
    save::Response response = { };
    // NO WaitBody() IN THIS LOOP, unlike the drawn flow's: there is no scene to
    // keep alive and no audio to tick, and the condition is false on the first
    // turn regardless. Written as a loop rather than a bare call so it stays
    // correct if this target ever gains a backend needing more than one turn.
    while (response = save::Load(&saveData, sizeof(saveData)), !response.done) { }

    if (!ValidateSave()) {
        PopulateSaveData();
    }

    gameMode = eGameModes::Title;
}
#endif

} // namespace pregame

#ifndef TARGET_NES

namespace pregame {
static u8 SfxLevel() { return ui::impl::SfxLevel(); }

static void WaitBody() {
    audio::Update();
    if (const u8 queued = ui::impl::pendingSfx) {
        ui::impl::pendingSfx = 0;
        if (const u8 level = SfxLevel(); ui::impl::SfxEnabled() && level) {
            audio::SfxPlay(static_cast<u8>(queued - 1), ui::impl::kSfxChannel, level);
        }
    }
    video::WaitForPresent();
}

static bool LoadSave();
static bool TryCreateSave();
static void PrintContext(const u8* infoText);
static eOptions ChoiceWheel(const eOptions* pOptions, u8 sOptions);
static void MakeScene();
static void ApplyLoadedConfig();
static void InitPregameScreen();
static void ClearOptions();
static void LoadConfig();
void nmi_handler();
void irq_handler();

/// Rows the context message may wrap to, and the width of an option's box.
constexpr u8 kContextRows = 3;
constexpr u8 kOptionCols  = 10;

/// What this scene blanks with: the tile ' ' encodes to, so a Draw and a
/// Clear through the same charmap round-trip exactly.
constexpr u8 kBlankTile = charmap_generic(' ');

/// The wheel's two anchors are all the layout has, so a longer option list
/// would have nowhere to put its third label.
constexpr u8 kMaxOptions = 2;

atomic Rect contextRect;
atomic Rect optionRect[kMaxOptions];

/// The rows ::ui::text::Make built for what is currently on screen, kept so it
/// can be Clear()ed -- Clear needs the same wrapping Draw used, and only the
/// row table it was given holds that.
ui::text::textBuffer* contextRows            = nullptr;
ui::text::textBuffer* optionRows[kMaxOptions] = {};

constexpr eOptions createOpts[] = { Yes, No };
constexpr eOptions errorOpts[] = {Retry, Continue};
constexpr eOptions eraseOpts[] = {Continue, Erase};
constexpr eOptions cardOpts[]  = { Card1, Card2 };

void main() {
    log("pregame::main: entered");
    InitPregameScreen();

    PopulateSaveData();
    save::Response response = { };

    retry:
    while (response = save::GetSaveLocation(), !response.done) {
        WaitBody();
    }

    log("pregame: GetSaveLocation err=%d", response.err);

    switch (response.err) {
       case save::found: {
            const std::size_t sPath = strlen(response.path);
            if (!savePath) {    // we don't need to free, as it will be free on next load, I think
                savePath = new char[sPath + 1];   // + the terminator strcpy writes
                strcpy(savePath, response.path);
            }
            // THE ANSWER IS ACTED ON, which it was not before: false means
            // the player asked for the whole flow to be run again (a failed
            // erase, so far), and dropping it on the floor is what made ERASE
            // look like "continue to the game".
            if (!LoadSave()) goto retry;
            break;
        }
        case save::missing:
#ifdef TARGET_OGC
            PrintContext(saveMissingText.data());
            switch (ChoiceWheel(createOpts, sizeof(createOpts))) {
                case Yes:
                    if (!TryCreateSave()) goto retry;
                    break;

                case No:
                    break;

                default:
                    log("unknown choice selected");
            }
#else
            if (!TryCreateSave()) goto retry;
#endif

            break;
        case save::no_device:
            PrintContext(saveNoDeviceText.data());
            switch (ChoiceWheel(errorOpts, sizeof(errorOpts))) {
                case Retry:
                    goto retry; // re attempt

                case Continue:
                    break;

                default:
                    log("unknown choice selected");
            }

            break;
        case save::busy:
            PrintContext(saveBadErrorText.data());
            // card is reported busy, this should NOT fire under healthy circumstances
            // should show some message, that the card is busy with 'retry' or 'continue without save'
            log("card is busy, this is a serious bug that should be solved");
            switch (ChoiceWheel(errorOpts, sizeof(errorOpts))) {
                case Retry:
                    goto retry; // re attempt

                case Continue:
                    break;

                default:
                    log("unknown choice selected");
            }
            break;
        case save::exists:
            PrintContext(saveBadErrorText.data());
            // should only appear on save creation, not save searching, erroneous
            // should show some message, that the card is erroring with 'retry' or 'continue without save'
            log("cannot find file because it already exists, bad error");
            switch (ChoiceWheel(errorOpts, sizeof(errorOpts))) {
                case Retry:
                    goto retry; // re attempt

                case Continue:
                    break;

                default:
                    log("unknown choice selected");
            }
            break;

        case save::full:
            PrintContext(saveBadErrorText.data());
            // should not fire on try find, if this is being seen its a big error
            // report that an unusual bug is shown, and should be reported immediately
            log("cannot find file because out of storage, bad error");
            switch (ChoiceWheel(errorOpts, sizeof(errorOpts))) {
                case Retry:
                    goto retry; // re attempt

                case Continue:
                    break;

                default:
                    log("unknown choice selected");
            }
            break;
        case save::too_large:
            PrintContext(saveBadErrorText.data());
            // should not fire on load, if this is being seen its a big error
            // report that an unusual bug is shown, and should be reported immediately
            log("cannot find file because it might be too big to be loaded, bad error");
            switch (ChoiceWheel(errorOpts, sizeof(errorOpts))) {
                case Retry:
                    goto retry; // re attempt

                case Continue:
                    break;

                default:
                    log("unknown choice selected");
            }
            break;
        case save::denied:
            PrintContext(saveBadErrorText.data());
            // report operation has been denied, should not occur under typical circumstances
            // report the operation is denied, and should be reported immediately
            log("cannot find file, memory card denied operation");
            switch (ChoiceWheel(errorOpts, sizeof(errorOpts))) {
                case Retry:
                    goto retry; // re attempt

                case Continue:
                    break;

                default:
                    log("unknown choice selected");
            }
            break;
        case save::bad_path:
            PrintContext(saveBadErrorText.data());
            // report operation has bad path, not a user error
            // report the operation is denied, and should be reported immediately
            log("cannot find file, path is bad, bad error");
            switch (ChoiceWheel(errorOpts, sizeof(errorOpts))) {
                case Retry:
                    goto retry; // re attempt

                case Continue:
                    break;

                default:
                    log("unknown choice selected");
            }
            break;
        case save::failed:
            PrintContext(saveBadErrorText.data());
            // unknown error, just let the user retry or continue without save
            log("unknown error, bad error normally");
            switch (ChoiceWheel(errorOpts, sizeof(errorOpts))) {
                case Retry:
                    goto retry; // re attempt

                case Continue:
                    break;

                default:
                    log("unknown choice selected");
            }
            break;
    }

    ApplyLoadedConfig();

    log("pregame::main: handing over to the title screen");
    gameMode = eGameModes::Title;
}

void ApplyLoadedConfig() {
#ifdef TARGET_GC
    const bool wide = gen::options::videoCanvas::enableWidescreenButton::Enabled();
    log("pregame: restoring widescreen=%d from save", wide);
    if (wide) video::ogc::SetWidescreen(true);
#else

    log("pregame: no display settings to restore on this target");
#endif
}

static vec2<u16> SceneOriginTiles() {
    const u16 tw = video::viewport_tx() < 32 ? 32 : video::viewport_tx();
    const u16 th = video::viewport_ty() < 30 ? 30 : video::viewport_ty();
    return {static_cast<u16>((tw + 3) & ~3u), th};
}

/** @brief ::SceneOriginTiles in pixels: the scroll this mode parks the camera at. */
static vec2<u16> SceneOriginPixels() {
    const vec2<u16> o = SceneOriginTiles();
    return {static_cast<u16>(o.x << 3), static_cast<u16>(o.y << 3)};
}

void MakeScene() {
    const u16 tx = video::viewport_tx();
    const u16 ty = video::viewport_ty();
    const vec2<u16> origin = SceneOriginTiles();

    contextRect = Rect{{static_cast<u16>(origin.x + 4),
                        static_cast<u16>(origin.y + (ty >> 1) - 2)},
                       {static_cast<u8>(tx - 8), kContextRows}};

    const u16 optionY = static_cast<u16>(origin.y + (ty >> 1) + 2);
    optionRect[0] = Rect{{static_cast<u16>(origin.x + (tx >> 2) - (kOptionCols >> 1)), optionY},
                         {kOptionCols, 1}};
    optionRect[1] = Rect{{static_cast<u16>(origin.x + (tx >> 1) + (tx >> 2) - (kOptionCols >> 1)), optionY},
                         {kOptionCols, 1}};
}

/** @brief Parks every sprite; the provider ::oam::PopulateFromProvider wants. */
static oam::oam_t ParkY(const u16) { return ParkedSpriteY(); }

void InitPregameScreen() {
    // Nothing of the previous mode's sprites may survive into this one: every
    // slot is parked, and the arrow is placed by ::DrawOptions when there is a
    // wheel to point at.
    oam::PopulateFromProvider(OAMBuffer, 0, oam::y, ParkY, 64);

    mmc3::SwitchCHRBank<mmc3::BankOf<blob::ui::__lma>()>(mmc3::chr3Control);   // R3, PPU $0400: background
    mmc3::SwitchCHRBank<mmc3::BankOf<blob::ui::__lma>()>(mmc3::chr1Control);   // R1, PPU $1800: the same art as sprites
    mmc3::SetMirroring(false);

    ppu::Flush(kBlankTile, 0xff);

    // BG palette 3 is what the font is drawn in, sprite palette 1 the arrow's
    // (ui/impl/singleChoice.hpp's ::kSelectorAttributes) -- the same three
    // colours in both, exactly as title mode writes them.
    ppu::pal::WriteFromBuffer(ppu::BG_3 + 1, titleScreenColours, 3);
    ppu::pal::WriteFromBuffer(ppu::SPRITE_1 + 1, titleScreenColours, 3);

    mmc3::AcknowledgeScanlineIRQ();

    pNMI = nmi_handler;
    pIRQ = irq_handler;

    MakeScene();


    ppu::SetScroll(SceneOriginPixels());

    ppu::EnableRendering(ppu::ctrl::SPRITE_ADDR | ppu::ctrl::GEN_NMI,
                         ppu::mask::BG_L | ppu::mask::SPRITE_L);
    irq::EnableInterrupts();
}

void nmi_handler() {
    ui::impl::AnimateSelector();
    oam::RefreshSprites(OAMBuffer);
}


void irq_handler() {
    mmc3::AcknowledgeScanlineIRQ();
}

static void PlaceArrow(const Rect& option) {
    const u8 slot = ui::impl::SelectorSlot();

    const vec2<u16> scroll = SceneOriginPixels();
    const i16 x = static_cast<i16>(option.pos.x * 8 - ui::impl::kSelectorGap - scroll.x);
    const i16 y = static_cast<i16>(option.pos.y * 8 - scroll.y);
    if (x < 0 || y < 0) {
        // Signed, and parked rather than clamped, same guard as PlaceSelector:
        // a u16 would wrap an off-screen arrow into somewhere visible.
        OAMBuffer[slot].y = ParkedSpriteY();
        return;
    }

    OAMBuffer[slot].tile       = ui::impl::kSelectorTile;
    OAMBuffer[slot].attributes = ui::impl::kSelectorAttributes;
    OAMBuffer[slot].y          = static_cast<oam::oam_t>(y);

    ui::impl::selectorBaseX = static_cast<u16>(x);
    OAMBuffer[slot].x = static_cast<oam::oam_t>(x + ui::impl::SwayOffset(frameCounter));
}

/** @brief The label an option is drawn with. */
static const u8* LabelFor(const eOptions option) {
    switch (option) {
        case Continue: return continueText.data();
        case Retry:    return retryText.data();
        case No:       return noText.data();
        case Yes:      return yesText.data();
        case Erase:    return eraseText.data();
        case Card1:    return card1Text.data();
        case Card2:    return card2Text.data();
    }
    // Unreachable through the option arrays this file declares, and a label is
    // better than a blank row if one is ever added without a case here.
    log("pregame: option %d has no label", option);
    return continueText.data();
}

/** @brief Blanks whatever labels a wheel left on screen, and parks the arrow. */
void ClearOptions() {
    for (u8 i = 0; i < kMaxOptions; i++) {
        if (!optionRows[i]) continue;
        const Rect r = optionRect[i];
        ui::text::Clear(optionRows[i], ppu::CartesianToAddress(r.pos), r.size,
                        ui::text::Centre, kBlankTile);
        delete[] optionRows[i];
        optionRows[i] = nullptr;
    }
    OAMBuffer[ui::impl::SelectorSlot()].y = ParkedSpriteY();
}

void PrintContext(const u8* infoText) {
    const Rect r = contextRect;
    const u16  addr = ppu::CartesianToAddress(r.pos);

    if (contextRows) {
        ui::text::Clear(contextRows, addr, r.size, ui::text::Centre, kBlankTile);
        delete[] contextRows;
        contextRows = nullptr;
    }

    contextRows = ui::text::Make<charmap_generic, ' '>(infoText, r.size);
    ui::text::Draw(contextRows, addr, r.size, ui::text::Centre);
}

constexpr u8 kWheelRepeatBits  = input::LEFT | input::RIGHT;
constexpr u8 kWheelRepeatDelay = 16;
constexpr u8 kWheelRepeatTick  = 8;


static void DrawOptions(const eOptions* const pOptions, const u8 sOptions, const u8 opt) {
    for (u8 i = 0; i < sOptions; i++) {
        const Rect r = optionRect[i];
        optionRows[i] = ui::text::Make<charmap_generic, ' '>(LabelFor(pOptions[i]), r.size);
        ui::text::Draw(optionRows[i], ppu::CartesianToAddress(r.pos), r.size, ui::text::Centre);
    }
    const Rect selected = optionRect[opt];
    PlaceArrow(selected);
}

eOptions ChoiceWheel(const eOptions* const pOptions, const u8 sOptionsIn) {
    u8 sOptions = sOptionsIn;
    if (sOptions > kMaxOptions) {
        log("pregame: ChoiceWheel given %d options, showing %d", sOptionsIn, kMaxOptions);
        sOptions = kMaxOptions;
    }

    u8 opt = 0;
    DrawOptions(pOptions, sOptions, opt);

    u8 seed1 = 0, seed2 = 0;
    input::PollControllers(&seed1, &seed2);
    u8 prevInputs = static_cast<u8>(seed1 | seed2);

    u8   repeatHeld  = 0;
    u8   repeatFrame = 0;
    bool repeating   = false;

    while (true) {
        u8 port1 = 0, port2 = 0;
        input::PollControllers(&port1, &port2);
        const u8 inputs  = static_cast<u8>(port1 | port2);
        const u8 pressed = static_cast<u8>(inputs & ~prevInputs);
        prevInputs = inputs;

        u8 step = static_cast<u8>(pressed & kWheelRepeatBits);
        if (step) {
            repeatHeld  = step;
            repeatFrame = 0;
            repeating   = false;   // the delay is served before any tick
        } else if (const u8 held = static_cast<u8>(inputs & repeatHeld); !held) {
            repeatHeld  = 0;
            repeatFrame = 0;
            repeating   = false;
        } else if (++repeatFrame >= (repeating ? kWheelRepeatTick : kWheelRepeatDelay)) {
            repeatFrame = 0;
            repeating   = true;
            step        = held;
        }

        const u8 was = opt;
        if (step & input::LEFT) {
            if (opt) --opt;
        } else if (step & input::RIGHT) {
            if (opt + 1 < sOptions) ++opt;
        }
        if (opt != was) {
            ui::impl::PostSfx(sfx::selectnext);
            // The labels stay put; only the sprite moves. No nametable write,
            // so this is safe from the mainline on any frame.
            const Rect moved = optionRect[opt];
            PlaceArrow(moved);
        }

        if (pressed & input::A) {
            ui::impl::PostSfx(sfx::select);
            break;
        }

        WaitBody();
    }

    ClearOptions();
    return pOptions[opt];
}

/** @brief Blanks the context band and gives its row table back. */
static void ClearContext() {
    if (!contextRows) return;
    const Rect r = contextRect;
    ui::text::Clear(contextRows, ppu::CartesianToAddress(r.pos), r.size,
                    ui::text::Centre, kBlankTile);
    delete[] contextRows;
    contextRows = nullptr;
}

static const u8* SaveErrorText(const save::eError err) {
    switch (err) {
        case save::no_device: return saveNoDeviceText.data();
        case save::missing:   return saveNoFileText.data();
        case save::full:      return saveFullText.data();
        case save::too_large: return saveLargeText.data();
        default:              return saveWriteFailedText.data();
    }
}

bool saveScreenBuilt = false;
void (*saveScreenCallerNMI)() = nullptr;

void BuildSaveScreen() {
    if (saveScreenBuilt) return;
    saveScreenBuilt = true;


    saveScreenCallerNMI = pNMI;
    pNMI = nmi_handler;

    mmc3::AcknowledgeScanlineIRQ();

    MakeScene();
    ppu::SetScroll(SceneOriginPixels());
}

bool saveScreenSaid = false;

void SaveScreenPump() {
    if (!saveScreenSaid) {
        BuildSaveScreen();
        PrintContext(saveSavingText.data());
        saveScreenSaid = true;
    }
    WaitBody();
}

bool SaveScreen() {
    saveScreenBuilt = false;

    bool saved = false;
    while (true) {
        saveScreenSaid = false;

        const save::Response response = SaveGame(SaveScreenPump);

        if (response.err == save::found) {
            saved = true;
            break;
        }

        log("pregame::SaveScreen: save failed err=%d", response.err);

        BuildSaveScreen();
        PrintContext(SaveErrorText(response.err));
        if (ChoiceWheel(errorOpts, sizeof(errorOpts)) != Retry) break;
    }

    if (saveScreenBuilt) {
        ClearContext();
        pNMI = saveScreenCallerNMI;
    }

    return saved;
}

bool LoadSave() {
    save::Response response { };
    while (response = save::Load(&saveData, sizeof(saveData), &response), !response.done) {
        WaitBody();
    }

    if (!ValidateSave()) {
        PrintContext(saveCorruptText.data());

        // ERASE REMOVES THE FILE AND THEN STARTS THE FLOW AGAIN, which is the
        // whole point of returning false: ::main's `goto retry` runs
        // ::save::GetSaveLocation afresh, and with the save gone that now
        // answers save::missing -- the branch that offers to CREATE one. So the
        // player who said ERASE is asked whether to make a new save, instead of
        // being dropped into the game on a card they just cleared.
        //
        // IT USED TO `return false` WITHOUT DELETING ANYTHING, and ::main
        // DISCARDED the value: `LoadSave();` with no `if`. So ERASE wrote
        // nothing, removed nothing, said nothing, and went straight to the
        // title with the corrupt file still on the card. Both halves of that
        // are fixed -- the delete here, and the caller actually reading this.
        if (ChoiceWheel(eraseOpts, sizeof(eraseOpts)) == Erase) {
            save::Response removed { };
            while (removed = save::Delete(), !removed.done) {
                WaitBody();
            }

            log("pregame: erase -> err=%d", removed.err);

            // save::missing IS A SUCCESS HERE, exactly as save.hpp says: it
            // means there was nothing to remove, which is the state this was
            // asked to reach. Either way the save is gone and the retry finds
            // nothing, so both land on the create prompt.
            if (removed.err == save::found || removed.err == save::missing) return false;

            // The card refused. Reported the way every other failure in this
            // mode is, with the same two answers: try the whole flow again, or
            // carry on without a save -- on the defaults already in RAM, since
            // the malformed file is still out there and unreadable.
            PrintContext(SaveErrorText(removed.err));
            if (ChoiceWheel(errorOpts, sizeof(errorOpts)) == Retry) return false;
            PopulateSaveData();
            return true;
        }

        // CONTINUE leaves the malformed file exactly where it is and plays on
        // the defaults in RAM, which is the point of offering it: a player who
        // would rather keep a chance of recovering that card elsewhere than
        // have this game overwrite it.
        PopulateSaveData();
        return true;
    }


    if (response.err != save::found) {
        log("pregame: load did not complete (err=%d) -- keeping the defaults "
            "seeded before it", response.err);
    }

    return true;
}

bool TryCreateSave() {
    bool useCard2 = false;
    const u8 saveDevices = save::AccessibleDevices();
    if (!saveDevices) {
        PrintContext(saveNoDeviceText.data());
        switch (ChoiceWheel(errorOpts, sizeof(errorOpts))) {
            case Retry:
                return false; // re attempt

            case Continue:
                break;

            default:
                log("unknown choice selected");
                break;  // something bad happened
        }
    }

    if (saveDevices == 0b11) {
        PrintContext(saveChooseCard.data());
        switch (ChoiceWheel(cardOpts, sizeof(cardOpts))) {
            case Card1:
                useCard2 = false;
                break;

            // Card2, NOT Continue. ::cardOpts is { Card1, Card2 }, so the
            // right-hand option comes back as Card2 -- which matched neither
            // case, fell to the default, logged "unknown choice selected" and
            // left ::useCard2 false. Picking card 2 created the save on card 1.
            case Card2:
                useCard2 = true;
                break;

            default:
                log("unknown choice selected");
                break;  // something bad happened
        }

        create:
        save::Response response { };

        while (response = save::GetDeviceStatus(useCard2), !response.done) {
            WaitBody();
        }

        if (response.err != save::found) {
            PrintContext(saveNoDeviceText.data());
            log("pregame: card %d unusable before create (err=%d)",
                static_cast<u8>(useCard2 + 1), response.err);
            switch (ChoiceWheel(errorOpts, sizeof(errorOpts))) {
                case Retry:
                    return false; // re attempt

                case Continue:
                    break;  // no save load

                default:
                    log("unknown choice selected");
                    break;  // something bad happened
            }
            return true;
        }

        while (response = save::CreateSave(&saveData, sizeof(saveData), &response), !response.done) {
            WaitBody();
        }

        log("pregame: create on %s -> err=%d",
            response.path ? response.path : "(no path)", response.err);

        switch (response.err) {
            case save::found:
                break;
            case save::missing:
                PrintContext(saveBadErrorText.data());
                log("cannot create file because it does not exist, bad error");
                switch (ChoiceWheel(errorOpts, sizeof(errorOpts))) {
                    case Retry:
                        return false; // re attempt

                    case Continue:
                        break;  // no save load

                    default:
                        log("unknown choice selected");
                        break;  // something bad happened
                }
                break;
            case save::no_device:
                break;
            case save::busy:
                PrintContext(saveBadErrorText.data());
                log("cannot create file, card busy, bad error");
                switch (ChoiceWheel(errorOpts, sizeof(errorOpts))) {
                    case Retry:
                        return false; // re attempt

                    case Continue:
                        break;  // no save load

                    default:
                        log("unknown choice selected");
                        break;  // something bad happened
                }
                break;
            case save::exists:
                PrintContext(saveBadErrorText.data());
                log("cannot create file because it already exists, should have been verified earlier");
                switch (ChoiceWheel(errorOpts, sizeof(errorOpts))) {
                    case Retry:
                        return false; // re attempt

                    case Continue:
                        break;  // no save load

                    default:
                        log("unknown choice selected");
                        break;  // something bad happened
                }
                break;
            case save::
                full:PrintContext(saveFullText.data());
                log("Should have known at this point, bad error");
                switch (ChoiceWheel(errorOpts, sizeof(errorOpts))) {
                    case Retry:
                        return false; // re attempt

                    case Continue:
                        break;  // no save load

                    default:
                        log("unknown choice selected");
                        break;  // something bad happened
                }
                break;
            case save::too_large:
                PrintContext(saveBadErrorText.data());
                log("cannot create file because the file that we haven't confirmed to exist may be too large to load which isn't the operation at hand, bad error");
                switch (ChoiceWheel(errorOpts, sizeof(errorOpts))) {
                    case Retry:
                        return false; // re attempt

                    case Continue:
                        break;  // no save load

                    default:
                        log("unknown choice selected");
                        break;  // something bad happened
                }
                break;
            case save::denied:
                PrintContext(saveBadErrorText.data());
                log("cannot create file, memory card denied operation");
                switch (ChoiceWheel(errorOpts, sizeof(errorOpts))) {
                    case Retry:
                        return false; // re attempt

                    case Continue:
                        break;  // no save load

                    default:
                        log("unknown choice selected");
                        break;  // something bad happened
                }
                break;
            case save::bad_path:
                PrintContext(saveBadErrorText.data());
                log("cannot create file, bad path");
                switch (ChoiceWheel(errorOpts, sizeof(errorOpts))) {
                    case Retry:
                        return true; // re attempt

                    case Continue:
                        break;  // no save load

                    default:
                        log("unknown choice selected");
                        break;  // something bad happened
                }
                break;
            case save::failed:
                PrintContext(saveBadErrorText.data());
                log("cannot create file, unknown reason");
                switch (ChoiceWheel(errorOpts, sizeof(errorOpts))) {
                    case Retry:
                        return false; // re attempt

                    case Continue:
                        break;  // no save load

                    default:
                        log("unknown choice selected");
                        break;  // something bad happened
                }
                break;
        }

    } else {
#ifdef TARGET_OGC
        useCard2 = saveDevices - 1;
        PrintContext(saveUseCard.data());
        // TODO: string interpolation, emit message 'slot %d is available, wish to save?"
        switch (ChoiceWheel(createOpts, sizeof(createOpts))) {
            case Yes:
                goto create;

            case No:
                break;  // no save load

            default:
                log("unknown choice selected");
                break;  // something bad happened
        }
#else
        if (saveDevices) goto create;
#endif
    }

    return true;    //for no retry
    // using this information, we can present what memory cards are accessible, should work on something here nicely
}

} // namespace pregame

static void LoadConfig() {

}

char* savePath;

#endif // TARGET_NES
