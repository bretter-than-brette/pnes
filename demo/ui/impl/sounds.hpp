#pragma once


#include <pnes/technology.hpp>
#include <pnes/audio.hpp>

#ifdef TARGET_NES
#include <sfx.hpp>
#else
namespace sfx {
enum Effect : unsigned char {
    toggle = 0,
    select = 1,
    deselect = 2,
    selectprev = 3,
    selectnext = 4,
};
constexpr unsigned char count = 5;
}
#endif

namespace ui::impl {
#ifndef TARGET_NES
    inline constexpr audio::sfx_t kSfxTable[] = {
        { "sfx/Toggle.ogg" },
        { "sfx/Select.ogg" },
        { "sfx/Deselect.ogg" },
        { "sfx/SelectPrev.ogg" },
        { "sfx/SelectNext.ogg" },
    };

    static_assert(sizeof(kSfxTable) / sizeof(*kSfxTable) == sfx::count,
                  "demo/ui/impl/sfx.hpp: kSfxTable and sfx::Effect disagree on how many "
                  "effects there are -- one of the two was added to and the other was not");
#endif

    inline void RegisterSfx() {
#ifndef TARGET_NES
        audio::SetSfx(kSfxTable, static_cast<u8>(sfx::count));
#endif
    }

    /// One slot for the whole UI: these are alternative sounds for one indicator
    /// moving, and starting an effect on a busy slot cuts the old one off, which
    /// is the right answer when a player walks a slider.
    ///
    /// Zero, and it cannot be anything else today: off NES this is a slot index,
    /// but the NES driver passes it straight to famistudio_sfx_play, whose X is a
    /// BYTE OFFSET into the stream structs. So 1 lands misaligned inside stream 0
    /// -- the effect does not play and the music is destroyed with it. Zero is the
    /// one value both readings agree on. The assert catches a different mistake:
    /// naming a slot this platform does not have.
    constexpr u8 kSfxChannel = 0;
    static_assert(kSfxChannel < audio::sfxChannels,
                  "kSfxChannel is past this platform's last voice slot -- raise "
                  "PNES_SFX_STREAMS (NES) or pick a lower slot");

    /// The effect waiting to be played, biased by one so that 0 can mean none.
    ///
    /// Posted here, spent elsewhere, and that split is not tidiness: most hooks
    /// that know a sound is wanted run in vblank, while audio::SfxPlay on NES
    /// lives in a banked engine that has to be mapped to be called at all --
    /// and switching banks from inside an interrupt is the one thing this ROM
    /// cannot do safely. So a hook stores a byte and the mainline plays it where
    /// the engine is already mapped for audio::Update.
    ///
    /// One slot, so a frame that posts twice plays the last thing posted -- the
    /// two would be a focus move and the sound of what it landed on, which share
    /// a voice anyway.
    inline atomic u8 pendingSfx = 0;

    inline void PostSfx(const u8 effect) {
        pendingSfx = static_cast<u8>(effect + 1);
    }

    /// Off NES there is nothing to ask: the effects are a registered table, and a
    /// backend with no SFX path simply ignores the call.
    bool SfxEnabled();

    u8 SfxLevel();
}
