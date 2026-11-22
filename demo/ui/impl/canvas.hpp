#pragma once

#include <pnes/extras/ui/types.hpp>

#include STRCAT(../../gen/options/GEN_TARGET_DIR/options.hpp)
#include "../../chr/chr.h"
#include "../../src/main.hpp"
#include "../../src/graphics/windows.hpp"
#include "singleChoice.hpp"
#include "sounds.hpp"

namespace ui::impl {
    u8 CornerSlot();
    vec2<u16> CanvasScroll();

    enum eCorner : u8 {
        UpperLeft,
        UpperRight,
        LowerLeft,
        LowerRight,
        kCorners
    };

    constexpr u8 kCornerTiles[kCorners] = {
        static_cast<u8>(windows::uiSprites + blob::ui::box_ul),
        static_cast<u8>(windows::uiSprites + blob::ui::box_ur),
        static_cast<u8>(windows::uiSprites + blob::ui::box_bl),
        static_cast<u8>(windows::uiSprites + blob::ui::box_br),
    };

    constexpr u8 kCornerAttributes = kSelectorAttributes;

    constexpr i16 kSelectionGap = 7;

    // The honing offsets the focus ring walks in, one per frame of the wobble.
    constexpr u8 kHoneOffsets[8] = {0, 1, 1, 2, 2, 1, 1, 0};

    constexpr u8 kHoneShift = 3;

    constexpr u8 HoneOffset(const u16 frame) {
        return kHoneOffsets[(frame >> kHoneShift) & 0x7];
    }

    constexpr i16 kSpriteYBias = 1;

    inline atomic u16 ringLeft   = 0;
    inline atomic u16 ringRight  = 0;
    inline atomic u16 ringTop    = 0;
    inline atomic u16 ringBottom = 0;

    inline void ApplyCornerHone(const u8 off) {
        const u16 left   = ringLeft;
        const u16 right  = ringRight;
        const u16 top    = ringTop;
        const u16 bottom = ringBottom;

        const u8 slot = CornerSlot();
        OAMBuffer[slot + UpperLeft].x  = static_cast<oam::oam_t>(left + off);
        OAMBuffer[slot + UpperLeft].y  = static_cast<oam::oam_t>(top + off);
        OAMBuffer[slot + UpperRight].x = static_cast<oam::oam_t>(right - off);
        OAMBuffer[slot + UpperRight].y = static_cast<oam::oam_t>(top + off);
        OAMBuffer[slot + LowerLeft].x  = static_cast<oam::oam_t>(left + off);
        OAMBuffer[slot + LowerLeft].y  = static_cast<oam::oam_t>(bottom - off);
        OAMBuffer[slot + LowerRight].x = static_cast<oam::oam_t>(right - off);
        OAMBuffer[slot + LowerRight].y = static_cast<oam::oam_t>(bottom - off);
    }

    inline void AnimateCorners() {
        if (OAMBuffer[CornerSlot() + UpperLeft].y == ParkedSpriteY()) return;
        ApplyCornerHone(HoneOffset(frameCounter));
    }

    inline void ParkCorners() {
        const u8 slot = CornerSlot();
        for (u8 i = 0; i < kCorners; i++) {
            OAMBuffer[slot + i].y = ParkedSpriteY();
        }
    }

    // Places the ring's four corners around @p current, converting a plane rect
    // to screen space through the same scroll ClickTile's inverse uses -- two
    // copies of that scroll could disagree, one cannot.
    inline void SelectComponent(const Rect& current) {
        const vec2<u16> scroll = CanvasScroll();

        const i16 left   = static_cast<i16>(current.pos.x * 8 - scroll.x - kSelectionGap);
        const i16 right  = static_cast<i16>((current.pos.x + current.size.x) * 8 - scroll.x);
        const i16 top    = static_cast<i16>(current.pos.y * 8 - scroll.y - kSelectionGap - kSpriteYBias);
        const i16 bottom = static_cast<i16>((current.pos.y + current.size.y) * 8 - scroll.y - kSpriteYBias);

        if (left < 0 || top < 0) {
            ParkCorners();
            return;
        }

        ringLeft   = static_cast<u16>(left);
        ringRight  = static_cast<u16>(right);
        ringTop    = static_cast<u16>(top);
        ringBottom = static_cast<u16>(bottom);

        ApplyCornerHone(HoneOffset(frameCounter));
    }

    inline void UpdateCanvas(const Rect&, const Rect& current) {
        PostSfx(sfx::selectnext);
        SelectComponent(current);
    }

    inline void ConfirmComponent(const Rect& current) {
        PostSfx(sfx::select);
        SelectComponent(current);
    }
}

namespace gen::options {
    namespace videoCanvas {
        inline void DrawUpdate(const Rect& last, const Rect& current) {
            ui::impl::UpdateCanvas(last, current);
        }

        inline void DrawComponentSelection(const Rect& current) {
            ui::impl::ConfirmComponent(current);
        }
    }

    namespace audioCanvas {
        inline void DrawUpdate(const Rect& last, const Rect& current) {
            ui::impl::UpdateCanvas(last, current);
        }

        inline void DrawComponentSelection(const Rect& current) {
            ui::impl::ConfirmComponent(current);
        }
    }
}
