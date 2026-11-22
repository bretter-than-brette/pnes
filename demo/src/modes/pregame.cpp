#include "pregame.hpp"
#include "../save.hpp"
#include <pnes/save.hpp>

// The save/load flow the title page hands off to: locating a device, showing
// SAVING or a RETRY/CONTINUE wheel where the write spans frames, and restoring
// the loaded config before anything draws against it.
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
void main() {
    save::Response response = { };
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

constexpr u8 kContextRows = 3;
constexpr u8 kOptionCols  = 10;

constexpr u8 kBlankTile = charmap_generic(' ');

constexpr u8 kMaxOptions = 2;

atomic Rect contextRect;
atomic Rect optionRect[kMaxOptions];

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

static oam::oam_t ParkY(const u16) { return ParkedSpriteY(); }

void InitPregameScreen() {
    oam::PopulateFromProvider(OAMBuffer, 0, oam::y, ParkY, 64);

    mmc3::SwitchCHRBank<mmc3::BankOf<blob::ui::__lma>()>(mmc3::chr3Control);   // R3, PPU $0400: background
    mmc3::SwitchCHRBank<mmc3::BankOf<blob::ui::__lma>()>(mmc3::chr1Control);   // R1, PPU $1800: the same art as sprites
    mmc3::SetMirroring(false);

    ppu::Flush(kBlankTile, 0xff);

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
        OAMBuffer[slot].y = ParkedSpriteY();
        return;
    }

    OAMBuffer[slot].tile       = ui::impl::kSelectorTile;
    OAMBuffer[slot].attributes = ui::impl::kSelectorAttributes;
    OAMBuffer[slot].y          = static_cast<oam::oam_t>(y);

    ui::impl::selectorBaseX = static_cast<u16>(x);
    OAMBuffer[slot].x = static_cast<oam::oam_t>(x + ui::impl::SwayOffset(frameCounter));
}

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
    log("pregame: option %d has no label", option);
    return continueText.data();
}

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

        if (ChoiceWheel(eraseOpts, sizeof(eraseOpts)) == Erase) {
            save::Response removed { };
            while (removed = save::Delete(), !removed.done) {
                WaitBody();
            }

            log("pregame: erase -> err=%d", removed.err);

            if (removed.err == save::found || removed.err == save::missing) return false;

            PrintContext(SaveErrorText(removed.err));
            if (ChoiceWheel(errorOpts, sizeof(errorOpts)) == Retry) return false;
            PopulateSaveData();
            return true;
        }

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
}

} // namespace pregame

static void LoadConfig() {

}

char* savePath;

#endif // TARGET_NES
