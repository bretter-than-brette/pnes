#pragma once

#include <pnes/video.hpp>
#include STRCAT(../../gen/title/GEN_TARGET_DIR/title.hpp)
#include STRCAT(../../gen/gameOptions/GEN_TARGET_DIR/gameOptions.hpp)
#include "../../chr/chr.h"
#include "../../src/main.hpp"
#include "../../src/graphics/windows.hpp"
#include "sounds.hpp"

// A single-choice row: one option visible at a time, with a sprite arrow placed
// over whichever is current.
namespace ui::impl {
    u8 SelectorSlot();
    vec2<u16> SelectorScroll();

    constexpr u8 kSelectorTile = windows::uiSprites + blob::ui::arrow;

    constexpr u8 kSelectorAttributes = 1;

    constexpr u16 kSelectorGap = 2 * 8;

    constexpr u8 kSwayOffsets[8] = {0, 1, 2, 3, 3, 2, 1, 0};

    constexpr u8 kSwayShift = 3;

    constexpr u8 SwayOffset(const u16 frame) {
        return kSwayOffsets[(frame >> kSwayShift) & 0x7];
    }

    inline atomic u16 selectorBaseX = 0;

    inline void ParkSelector() {
        OAMBuffer[SelectorSlot()].y = ParkedSpriteY();
    }

    inline void PlaceSelector(const Rect& option) {
        const vec2<u16> scroll = SelectorScroll();
        const i16 x = static_cast<i16>(option.pos.x * 8 - kSelectorGap - scroll.x);
        const i16 y = static_cast<i16>(option.pos.y * 8 - scroll.y);

        const u8 slot = SelectorSlot();
        if (x < 0 || y < 0) {
            OAMBuffer[slot].y = ParkedSpriteY();
            return;
        }
        OAMBuffer[slot].tile       = kSelectorTile;
        OAMBuffer[slot].attributes = kSelectorAttributes;
        OAMBuffer[slot].y          = static_cast<oam::oam_t>(y);

        selectorBaseX     = static_cast<u16>(x);
        OAMBuffer[slot].x = static_cast<oam::oam_t>(x + SwayOffset(frameCounter));
    }

    inline void AnimateSelector() {
        const u8 slot = SelectorSlot();
        if (OAMBuffer[slot].y == ParkedSpriteY()) return;

        const u16 base = selectorBaseX;
        OAMBuffer[slot].x = static_cast<oam::oam_t>(base + SwayOffset(frameCounter));
    }

    template <typename Options>
    inline void DrawSelectorChange(const Options& options, const u8 to) {
        const Rect r = options[to];
        PlaceSelector(r);
    }
}

namespace gen::title::TitleOptions {
    static_assert(vertical, "horizontal SingleChoice menus are not implemented in this project -- "
                             "retick Vertical on this menu in uitk, or implement the above-the-option "
                             "indicator placement in ui::impl");

    inline void DrawChange(const u8 from, const u8 to) {
        if (from != to) ui::impl::PostSfx(sfx::selectnext);
        ui::impl::DrawSelectorChange(options, to);
    }
}

namespace gen::gameOptions::gameOptions {
    static_assert(vertical, "horizontal SingleChoice menus are not implemented in this project -- "
                             "retick Vertical on this menu in uitk, or implement the above-the-option "
                             "indicator placement in ui::impl");

    inline void DrawChange(const u8 from, const u8 to) {
        if (from != to) ui::impl::PostSfx(sfx::selectnext);
        ui::impl::DrawSelectorChange(options, to);
    }
}
